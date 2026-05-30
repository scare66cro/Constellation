/*
 * lp_enet_dma_pause.c — Pause / resume CPSW DMA TX+RX events around an
 * OSPI Flash_write call, so the controller's INDIRECT_WRITE_XFER state
 * machine can complete without contention from the CPSW UDMA channels
 * that lwip-port has running autonomously.
 *
 * Why: across the 0.A.78–0.A.121 spiral we established (Experiment 1,
 * 2026-05-12) that the SDK's `Flash_write` SUCCEEDS in NoRTOS context
 * (auto-flasher at offset 0x900000) but FAILS in our FreeRTOS runtime
 * context — same chip, same SDK, same OSPI controller, same protocol.
 * The only material difference is the auto-flasher's syscfg has no
 * Enet/CPSW/lwIP. With CPSW running, the OSPI controller's DONE_STATUS
 * asserts late (after the SDK's poll loop times out), the chip never
 * accepts the PP, and Flash_write returns -1 with no data programmed.
 * `vTaskSuspendAll` + `HwiP_disable` together (0.A.121) did not fix it,
 * which ruled out CPU IRQ servicing — the contention is at the AHB
 * fabric / autonomous-DMA layer.
 *
 * This helper is intentionally minimal — just disables the TX/RX event
 * notification path on each open CPSW DMA channel via
 * `EnetDma_disableTxEvent` / `disableRxEvent`. This stops the lwip
 * notify callback from being invoked (so lwip stops processing the
 * rings) but does NOT halt the UDMA hardware itself, so packets in
 * flight continue to land in MSRAM. After Flash_write completes, the
 * inverse enable calls re-arm the events; lwip resumes processing the
 * accumulated rings; TCP retransmits anything dropped due to ring
 * overflow during the pause.
 *
 * Channel handle lookup goes through `EnetApp_lookupTxCh` /
 * `EnetApp_lookupRxFlow` — accessor functions injected into the
 * SysConfig-generated `ti_enet_open_close.c` by
 * `patch_enet_accessor.py`. The patch is idempotent and re-runs after
 * every SysConfig regeneration via the makefile.
 *
 * Brick-safety: no chip-side writes happen here at all. Only ENABLE/
 * DISABLE_EVENT operations on the CPSW DMA event subsystem. Worst
 * case: events stay disabled (lwip stops receiving) — recoverable via
 * SoC reboot; chip stays intact.
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdbool.h>
#include <kernel/dpl/DebugP.h>
#include <networking/enet/core/include/core/enet_dma.h>
#include <networking/enet/core/include/core/enet_per.h>   /* EnetPer_Handle */
#include <networking/enet/soc/k3/am64x_am243x/enet_soc.h> /* ENET_UDMA_NUM_RXCHAN_MAX */
#include <drivers/udma.h>
/* SDK-private header so we can cast EnetDma_*Handle to the underlying
 * EnetUdma struct and reach its `hUdmaCh` member. The Udma_chPause /
 * Udma_chResume APIs operate on `Udma_ChHandle`. */
#include <networking/enet/core/src/dma/udma/enet_udma_priv.h>

/* Provided by patch_enet_accessor.py post-syscfg, in
 * generated/ti_enet_open_close.c. */
extern EnetDma_TxChHandle EnetApp_lookupTxCh(uint32_t idx);
extern EnetDma_RxChHandle EnetApp_lookupRxFlow(uint32_t idx);

/* Number of channels matches ENET_SYSCFG_TX_CHANNELS_NUM /
 * ENET_SYSCFG_RX_FLOWS_NUM from ti_drivers_config.h. Our lp_am2434 build
 * configures 1 of each (default CPSW3g + lwip-cpsw setup). If a future
 * syscfg change adds more, the loops below silently iterate over them
 * (NULL handles short-circuit). */
#define LP_ENET_MAX_TX_CH    4U
#define LP_ENET_MAX_RX_FLOW  4U

/** Pause CPSW DMA event delivery on all open TX/RX channels.
 *  Safe to call multiple times (idempotent). Safe to call before any
 *  Enet driver has been opened — all lookups return NULL and the
 *  function is a no-op. */
void lp_enet_dma_pause(void)
{
    int n_tx = 0, n_rx = 0, n_tx_err = 0, n_rx_err = 0;
    int rx_ch_pause_rc = -999;   /* -999 = "never called" sentinel */
    uint32_t rx_ch_num = 0;
    /* TX: cast to EnetUdma_TxChObj * to reach hUdmaCh, then Udma_chPause.
     * This sets the channel's pause bit in the UDMAP runtime register —
     * halts new descriptor processing; in-flight transfers drain. */
    for (uint32_t i = 0; i < LP_ENET_MAX_TX_CH; i++) {
        EnetDma_TxChHandle h = EnetApp_lookupTxCh(i);
        if (h != NULL) {
            EnetUdma_TxChObj *tx = (EnetUdma_TxChObj *)h;
            int32_t rc = Udma_chPause(tx->hUdmaCh);
            if (rc == UDMA_SOK) n_tx++; else n_tx_err++;
        }
    }
    /* RX: walk EnetDma_RxChHandle (= EnetUdma_RxFlowObj*) → .hDma
     * (= EnetUdma_DrvObj*) → rxChObj[0].udmaChObj. The shared RX
     * channel is at index 0 (CPSW uses a single RX channel; multiple
     * flows attach to it). Halt the channel's UDMAP HW via the
     * inline `udmaChObj` member's address as a Udma_ChHandle.
     *
     * Also keep `disableRxEvent` so the lwip notify callback stays
     * silenced — defense in depth. */
    for (uint32_t i = 0; i < LP_ENET_MAX_RX_FLOW; i++) {
        EnetDma_RxChHandle h = EnetApp_lookupRxFlow(i);
        if (h != NULL) {
            int32_t rc = EnetDma_disableRxEvent(h);
            if (rc == 0) n_rx++; else n_rx_err++;
            /* Only pause the shared RX channel once. Since all flows
             * share the same parent channel, the channel-pause via
             * any flow's `hDma->rxChObj[0]` produces the same effect. */
            if (i == 0) {
                EnetUdma_RxFlowObj *flow = (EnetUdma_RxFlowObj *)h;
                EnetUdma_DrvObj *drv = (EnetUdma_DrvObj *)flow->hDma;
                if (drv != NULL) {
                    rx_ch_num = drv->numRxCh;
                    if (drv->numRxCh > 0) {
                        rx_ch_pause_rc = Udma_chPause(&drv->rxChObj[0].udmaChObj);
                    } else {
                        rx_ch_pause_rc = -2;   /* numRxCh == 0 */
                    }
                } else {
                    rx_ch_pause_rc = -1;   /* hDma was NULL */
                }
            }
        }
    }
    /* Log first 3 invocations only — confirms the pause is reaching real
     * handles. After that, silent so it doesn't flood UART during OTA. */
    static uint8_t s_logged = 0;
    if (s_logged < 3) {
        s_logged++;
        DebugP_log("[Flash] CPSW pause: tx_pause_ok=%d tx_err=%d rx_evt_ok=%d rx_err=%d "
                   "rx_ch_pause_rc=%d numRxCh=%u (call #%u)\r\n",
                   n_tx, n_tx_err, n_rx, n_rx_err,
                   rx_ch_pause_rc, (unsigned)rx_ch_num, (unsigned)s_logged);
    }
}

/** Resume CPSW DMA channels. Inverse of `lp_enet_dma_pause`. */
void lp_enet_dma_resume(void)
{
    for (uint32_t i = 0; i < LP_ENET_MAX_TX_CH; i++) {
        EnetDma_TxChHandle h = EnetApp_lookupTxCh(i);
        if (h != NULL) {
            EnetUdma_TxChObj *tx = (EnetUdma_TxChObj *)h;
            (void)Udma_chResume(tx->hUdmaCh);
        }
    }
    for (uint32_t i = 0; i < LP_ENET_MAX_RX_FLOW; i++) {
        EnetDma_RxChHandle h = EnetApp_lookupRxFlow(i);
        if (h != NULL) {
            (void)EnetDma_enableRxEvent(h);
            if (i == 0) {
                EnetUdma_RxFlowObj *flow = (EnetUdma_RxFlowObj *)h;
                EnetUdma_DrvObj *drv = (EnetUdma_DrvObj *)flow->hDma;
                if (drv != NULL && drv->numRxCh > 0) {
                    (void)Udma_chResume(&drv->rxChObj[0].udmaChObj);
                }
            }
        }
    }
}
