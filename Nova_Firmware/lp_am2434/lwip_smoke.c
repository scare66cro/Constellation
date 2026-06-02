/*
 * Constellation Nova LP-AM2434 â€” Phase C smoke test
 *
 * Opens a TCP socket from the LaunchPad lwIP stack to the host-side
 * orbit-simulator (Modbus TCP @ 10.1.2.100:5502) and reads HR 200..203
 * once. Logs the request, response, and 4 register values on UART0.
 *
 * Goal: prove BSD socket API + connect + Modbus TCP framing all work
 * end-to-end. Once green, the existing `Nova_Firmware/Platform/`
 * `hal_modbus_tcp.c` + `hal_orbit.c` are drop-in usable (Phase D).
 *
 * Failure modes are logged, not asserted â€” the smoke test never blocks
 * the rest of the firmware (UART2 bridge, banner, etc.).
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include <kernel/dpl/DebugP.h>
#include <kernel/dpl/ClockP.h>

#include "FreeRTOS.h"
#include "task.h"

#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"

#include "orbit_client.h"

/* SDK polling-task control — used by the 0.A.216 post-fixup stop of the
 * 1-sec polling task that re-injects the cached state->speed=1GBIT and
 * causes the recurring post-Activate "half-deaf" wedge. See
 * memories/repo/wd-required-mask-dead-confirmboot-2026-06-01.md and
 * lp-am2434-cpsw-tx-debug.md update 12. The function lives in
 * enet/test_enet_cpsw.c. */
extern void EnetApp_stopPhyRegisterPollingTask(void);

/* CPSW stats â€” diagnose where TX dies (host port? MAC port? PHY?) */
#include <enet.h>
#include <enet_cfg.h>
#include <include/core/enet_mod_stats.h>
#include <include/core/enet_mod_macport.h>
#include <include/core/enet_mod_mdio.h>
#include <include/core/enet_per.h>
#include <include/mod/cpsw_stats.h>
#include <include/per/cpsw.h>
#include <include/phy/enetphy.h>
#include <enet_apputils.h>
#include <drivers/gpio.h>
#include <drivers/hw_include/cslr_soc.h>
#include "ti_drivers_config.h"

/* ---- Production orbit polling ----
 * Real orbits run on dedicated LP-AM2434 boards at 10.47.27.2 (STORAGE),
 * 10.47.27.3 (GDC), 10.47.27.4 (TRITON). The OrbitClient list near the
 * bottom of lwip_smoke_task is the authoritative roster. The single-host
 * orbit-simulator path that originally lived here (port-shifted on the
 * workstation IP) is gone \u2014 removed 2026-05-20 along with the stale
 * try_l2_probe diagnostic calls. */

/* build_read_hr_request + log_hex removed 2026-05-20 — only used by the
 * deleted do_one_modbus_read(); production polling uses orbit_client.c. */

/* Dump CPSW host-port + MAC-port-1 frame counters. Tells us whether TX
 * frames actually leave the SoC (and where the ALE drops them, if any). */
static void dump_cpsw_stats(const char *tag)
{
    DebugP_log("[STATS:%s] ENTER\r\n", tag);

    Enet_Handle hEnet = Enet_getHandle(ENET_CPSW_3G, 0U);
    if (hEnet == NULL) {
        DebugP_log("[STATS:%s] Enet_getHandle returned NULL\r\n", tag);
        return;
    }
    uint32_t coreId = EnetSoc_getCoreId();

    CpswStats_HostPort_Ng hostStats;
    CpswStats_MacPort_Ng  macStats;
    Enet_IoctlPrms prms;
    int32_t status;

    memset(&hostStats, 0, sizeof(hostStats));
    ENET_IOCTL_SET_OUT_ARGS(&prms, &hostStats);
    ENET_IOCTL(hEnet, coreId, ENET_STATS_IOCTL_GET_HOSTPORT_STATS, &prms, status);
    if (status != ENET_SOK) {
        DebugP_log("[STATS:%s] host stats ioctl failed: %d\r\n", tag, (int)status);
    } else {
        DebugP_log("[STATS:%s] HOST txGood=%u txBcast=%u txOctets=%u rxGood=%u rxCrc=%u aleDrop=%u\r\n",
                   tag,
                   (unsigned)hostStats.txGoodFrames,
                   (unsigned)hostStats.txBcastFrames,
                   (unsigned)hostStats.txOctets,
                   (unsigned)hostStats.rxGoodFrames,
                   (unsigned)hostStats.rxCrcErrors,
                   (unsigned)hostStats.aleDrop);
    }

    memset(&macStats, 0, sizeof(macStats));
    Enet_MacPort port = ENET_MAC_PORT_1;
    ENET_IOCTL_SET_INOUT_ARGS(&prms, &port, &macStats);
    ENET_IOCTL(hEnet, coreId, ENET_STATS_IOCTL_GET_MACPORT_STATS, &prms, status);
    if (status != ENET_SOK) {
        DebugP_log("[STATS:%s] MAC1 stats ioctl failed: %d\r\n", tag, (int)status);
    } else {
        unsigned rxUcast = (unsigned)(macStats.rxGoodFrames - macStats.rxBcastFrames - macStats.rxMcastFrames);
        DebugP_log("[STATS:%s] MAC1 txGood=%u txBcast=%u txOctets=%u rxGood=%u rxBcast=%u rxMcast=%u rxUcast=%u rxCrc=%u aleDrop=%u\r\n",
                   tag,
                   (unsigned)macStats.txGoodFrames,
                   (unsigned)macStats.txBcastFrames,
                   (unsigned)macStats.txOctets,
                   (unsigned)macStats.rxGoodFrames,
                   (unsigned)macStats.rxBcastFrames,
                   (unsigned)macStats.rxMcastFrames,
                   rxUcast,
                   (unsigned)macStats.rxCrcErrors,
                   (unsigned)macStats.aleDrop);
        /* TX-error counters â€” the previously-unseen smoking-gun fields.
         * If any of these are non-zero (especially carrierSense,
         * excessColl, lateColl, deferred), the MAC is aborting frames
         * at the wire interface even though txGood++. That would
         * explain "MAC says it sent N, but no host on the LAN ever
         * sees us". See cpsw_stats.h CpswStats_MacPort_Ng. */
        DebugP_log("[STATS:%s] MAC1 txErr defer=%u coll=%u single=%u multi=%u "
                   "excess=%u late=%u carrier=%u\r\n",
                   tag,
                   (unsigned)macStats.txDeferredFrames,
                   (unsigned)macStats.txCollisionFrames,
                   (unsigned)macStats.txSingleCollFrames,
                   (unsigned)macStats.txMultipleCollFrames,
                   (unsigned)macStats.txExcessiveCollFrames,
                   (unsigned)macStats.txLateCollFrames,
                   (unsigned)macStats.txCarrierSenseErrors);
    }
}

/* Read DP83869 PHY (MDIO addr 3 on MAC1) and CPSW MAC1 link cfg.
 *
 * Goal: confirm what the PHY thinks the link is doing (speed, duplex,
 * autoneg complete) and what the MAC was actually programmed to. A
 * speed/duplex mismatch between MAC and PHY would explain "TX leaves
 * the SoC counters but the switch sees nothing" â€” the MAC clocks the
 * RGMII bus at the wrong rate so every frame is malformed. */
#define DP83869_BMCR    0x0000U
#define DP83869_BMSR    0x0001U
#define DP83869_PHYSTS  0x0011U   /* speed/duplex/link in upper bits */
#define DP83869_PHYCR   0x0010U   /* MDIX/loopback control */
#define DP83869_CFG2    0x0014U
#define DP83869_PHY_ADDR 3U

static uint16_t mdio_read16(Enet_Handle hEnet, uint32_t coreId,
                            uint32_t phyAddr, uint16_t reg)
{
    EnetMdio_C22ReadInArgs in = {
        .group   = ENET_MDIO_GROUP_0,
        .phyAddr = phyAddr,
        .reg     = reg,
    };
    uint16_t val = 0xFFFFU;
    Enet_IoctlPrms prms;
    int32_t status;
    ENET_IOCTL_SET_INOUT_ARGS(&prms, &in, &val);
    ENET_IOCTL(hEnet, coreId, ENET_MDIO_IOCTL_C22_READ, &prms, status);
    if (status != ENET_SOK) {
        return 0xFFFFU;
    }
    return val;
}

static int32_t mdio_write16(Enet_Handle hEnet, uint32_t coreId,
                            uint32_t phyAddr, uint16_t reg, uint16_t val)
{
    EnetMdio_C22WriteInArgs in = {
        .group   = ENET_MDIO_GROUP_0,
        .phyAddr = phyAddr,
        .reg     = reg,
        .val     = val,
    };
    Enet_IoctlPrms prms;
    int32_t status;
    ENET_IOCTL_SET_IN_ARGS(&prms, &in);
    ENET_IOCTL(hEnet, coreId, ENET_MDIO_IOCTL_C22_WRITE, &prms, status);
    return status;
}

static const char *speed_str(Enet_Speed s)
{
    switch (s) {
        case ENET_SPEED_10MBIT:   return "10";
        case ENET_SPEED_100MBIT:  return "100";
        case ENET_SPEED_1GBIT:    return "1000";
        case ENET_SPEED_AUTO:     return "AUTO";
        default:                  return "?";
    }
}

static void dump_phy_state(const char *tag)
{
    Enet_Handle hEnet = Enet_getHandle(ENET_CPSW_3G, 0U);
    if (hEnet == NULL) {
        DebugP_log("[PHY:%s] Enet_getHandle NULL\r\n", tag);
        return;
    }
    uint32_t coreId = EnetSoc_getCoreId();

    uint16_t bmcr   = mdio_read16(hEnet, coreId, DP83869_PHY_ADDR, DP83869_BMCR);
    uint16_t bmsr   = mdio_read16(hEnet, coreId, DP83869_PHY_ADDR, DP83869_BMSR);
    uint16_t physts = mdio_read16(hEnet, coreId, DP83869_PHY_ADDR, DP83869_PHYSTS);
    uint16_t phycr  = mdio_read16(hEnet, coreId, DP83869_PHY_ADDR, DP83869_PHYCR);
    uint16_t cfg2   = mdio_read16(hEnet, coreId, DP83869_PHY_ADDR, DP83869_CFG2);
    DebugP_log("[PHY:%s] addr=%u BMCR=%04X BMSR=%04X PHYSTS=%04X PHYCR=%04X CFG2=%04X\r\n",
               tag, (unsigned)DP83869_PHY_ADDR,
               (unsigned)bmcr, (unsigned)bmsr,
               (unsigned)physts, (unsigned)phycr, (unsigned)cfg2);

    /* PHYSTS bits (DP83869): [15:14]=speed (00=10M,01=100M,10=1G), [13]=duplex
     * (1=full), [10]=link up, [4]=ANEG complete, [3]=MDIX state. */
    unsigned spd_bits = (physts >> 14) & 0x3U;
    const char *spd_s = (spd_bits == 0) ? "10" :
                        (spd_bits == 1) ? "100" :
                        (spd_bits == 2) ? "1000" : "rsvd";
    DebugP_log("[PHY:%s] PHYSTS-decoded: link=%u dup=%s speed=%sM aneg_done=%u mdix=%u\r\n",
               tag,
               (unsigned)((physts >> 10) & 0x1U),
               ((physts >> 13) & 0x1U) ? "FULL" : "HALF",
               spd_s,
               (unsigned)((physts >> 4) & 0x1U),
               (unsigned)((physts >> 3) & 0x1U));

    /* CPSW MAC1 link cfg as programmed by Cpsw_handleExternalPhyLinkUp(). */
    EnetMacPort_LinkCfg linkCfg = { 0 };
    Enet_MacPort port = ENET_MAC_PORT_1;
    Enet_IoctlPrms prms;
    int32_t status;
    ENET_IOCTL_SET_INOUT_ARGS(&prms, &port, &linkCfg);
    ENET_IOCTL(hEnet, coreId, ENET_PER_IOCTL_GET_PORT_LINK_CFG, &prms, status);
    if (status != ENET_SOK) {
        DebugP_log("[PHY:%s] MAC1 LinkCfg ioctl failed: %d\r\n", tag, (int)status);
    } else {
        DebugP_log("[PHY:%s] MAC1 programmed: speed=%sM duplex=%s\r\n",
                   tag, speed_str(linkCfg.speed),
                   (linkCfg.duplexity == ENET_DUPLEX_FULL) ? "FULL" :
                   (linkCfg.duplexity == ENET_DUPLEX_HALF) ? "HALF" : "?");
    }

    bool linkUp = false;
    ENET_IOCTL_SET_INOUT_ARGS(&prms, &port, &linkUp);
    ENET_IOCTL(hEnet, coreId, ENET_PER_IOCTL_IS_PORT_LINK_UP, &prms, status);
    if (status == ENET_SOK) {
        DebugP_log("[PHY:%s] CPSW IS_PORT_LINK_UP=%u\r\n",
                   tag, (unsigned)linkUp);
    }
}

/* Read DP83869 extended (MMD-31 indirect) registers via the standard
 * C22 indirect-access sequence:
 *   write 0x000D = 0x001F                  // function=address, MMD=31
 *   write 0x000E = <reg>                   // target ext reg address
 *   write 0x000D = 0x401F                  // function=data, MMD=31
 *   read  0x000E                           // returns ext reg data
 *
 * Confirms what TX/RX clock delays + strap latches the PHY actually
 * sees, vs. what the SysCfg-generated init code claims to write. */
#define DP83869_REGCR        0x000DU
#define DP83869_ADDAR        0x000EU
#define DP83869_RGMIICTL_EXT 0x0032U
#define DP83869_STRAPSTS1    0x006EU
#define DP83869_STRAPSTS2    0x006FU
#define DP83869_RGMIIDCTL    0x0086U

static uint16_t phy_ext_read(Enet_Handle hEnet, uint32_t coreId,
                             uint32_t phyAddr, uint16_t extReg)
{
    mdio_write16(hEnet, coreId, phyAddr, DP83869_REGCR, 0x001FU);
    mdio_write16(hEnet, coreId, phyAddr, DP83869_ADDAR, extReg);
    mdio_write16(hEnet, coreId, phyAddr, DP83869_REGCR, 0x401FU);
    return mdio_read16(hEnet, coreId, phyAddr, DP83869_ADDAR);
}

static int32_t phy_ext_write(Enet_Handle hEnet, uint32_t coreId,
                             uint32_t phyAddr, uint16_t extReg, uint16_t val)
{
    mdio_write16(hEnet, coreId, phyAddr, DP83869_REGCR, 0x001FU);
    mdio_write16(hEnet, coreId, phyAddr, DP83869_ADDAR, extReg);
    mdio_write16(hEnet, coreId, phyAddr, DP83869_REGCR, 0x401FU);
    return mdio_write16(hEnet, coreId, phyAddr, DP83869_ADDAR, val);
}

/* Read MUX_SEL GPIO and report current routing of the muxable RJ45 jack.
 * On the LP-AM2434 E3 board: GPIO0_27 HIGH = jack routed to CPSW MAC1;
 * LOW = jack routed to ICSSG. ti_board_config.c writes HIGH at boot.
 * If the read-back is LOW, something else stomped it (or board variant
 * is different from what the SDK assumes). */
static void dump_mux_state(void)
{
    uint32_t base = (uint32_t)AddrTranslateP_getLocalAddr(
                        CONFIG_ENET_RGMII_MUX_SEL_BASE_ADDR);
    uint32_t pin  = CONFIG_ENET_RGMII_MUX_SEL_PIN;
    uint32_t v    = GPIO_pinRead(base, pin);
    DebugP_log("[MUX] GPIO0_27 (RGMII1 MUX_SEL) = %u  (1=CPSW, 0=ICSS)\r\n",
               (unsigned)v);
}

/* Scan MDIO addresses 0..31 reading PHYID1 (reg 2). Any value other
 * than 0x0000 / 0xFFFF means a PHY chip is responding at that address.
 * DP83869 PHYID1 = 0x2000. Comparing against any second PHY found on
 * the bus tells us if we have a known-good comparator on the same
 * board. */
static void scan_mdio_bus(void)
{
    Enet_Handle hEnet = Enet_getHandle(ENET_CPSW_3G, 0U);
    if (hEnet == NULL) {
        DebugP_log("[MDIOSCAN] Enet_getHandle NULL\r\n");
        return;
    }
    uint32_t coreId = EnetSoc_getCoreId();
    int found = 0;
    for (uint32_t a = 0; a < 32U; a++) {
        uint16_t id1 = mdio_read16(hEnet, coreId, a, 0x02U);
        if (id1 != 0x0000U && id1 != 0xFFFFU) {
            uint16_t id2  = mdio_read16(hEnet, coreId, a, 0x03U);
            uint16_t bmcr = mdio_read16(hEnet, coreId, a, 0x00U);
            uint16_t bmsr = mdio_read16(hEnet, coreId, a, 0x01U);
            DebugP_log("[MDIOSCAN] addr=%2u PHYID=%04X:%04X "
                       "BMCR=%04X BMSR=%04X (link=%u, aneg=%u)\r\n",
                       (unsigned)a, (unsigned)id1, (unsigned)id2,
                       (unsigned)bmcr, (unsigned)bmsr,
                       (unsigned)((bmsr >> 2) & 1U),
                       (unsigned)((bmsr >> 5) & 1U));
            found++;
        }
    }
    DebugP_log("[MDIOSCAN] %d PHY%s responding on the bus\r\n",
               found, found == 1 ? "" : "s");
}

static void dump_phy_extended_regs(void)
{
    Enet_Handle hEnet = Enet_getHandle(ENET_CPSW_3G, 0U);
    if (hEnet == NULL) {
        DebugP_log("[PHYEXT] Enet_getHandle NULL\r\n");
        return;
    }
    uint32_t coreId = EnetSoc_getCoreId();
    uint32_t addr   = DP83869_PHY_ADDR;
    uint16_t rgmiictl   = phy_ext_read(hEnet, coreId, addr, DP83869_RGMIICTL_EXT);
    uint16_t rgmiidctl  = phy_ext_read(hEnet, coreId, addr, DP83869_RGMIIDCTL);
    uint16_t strapsts1  = phy_ext_read(hEnet, coreId, addr, DP83869_STRAPSTS1);
    uint16_t strapsts2  = phy_ext_read(hEnet, coreId, addr, DP83869_STRAPSTS2);

    DebugP_log("[PHYEXT] RGMIICTL=%04X (bit1=TXCLKDLY bit0=RXCLKDLY)\r\n",
               (unsigned)rgmiictl);
    DebugP_log("[PHYEXT] RGMIIDCTL=%04X (hi-nib=TX dly, lo-nib=RX dly, "
               "step~250ps, max=15)\r\n",
               (unsigned)rgmiidctl);
    DebugP_log("[PHYEXT] STRAPSTS1=%04X STRAPSTS2=%04X\r\n",
               (unsigned)strapsts1, (unsigned)strapsts2);
}

/* RECOVERY: force RGMIICTL back to 0x00D0 (both delay-enable bits CLEAR).
 *
 * Backstory: an earlier session set RGMIICTL=0x00D3 to test enabling
 * the RGMII clock-delay lines. That broke MAC1 RX completely (Update 6
 * in /memories/repo/lp-am2434-cpsw-tx-debug.md) because this board
 * needs the delay-enable bits CLEAR. The call was reverted in source,
 * but **PHY MDIO writes are persistent across warm reset** â€” only a
 * cold power-cycle resets them to strap-latched defaults. Since we
 * can't physically power-cycle for ~2 days, we explicitly write back
 * 0x00D0 here at every boot to undo the stuck state.
 *
 * Without this, the prior log captures showing MAC1.rxGood=0 over
 * 75+ seconds on a busy LAN are NOT evidence of a hardware fault â€”
 * they're evidence of the stale stuck PHY register. */
static void restore_rgmii_ctl(void)
{
    Enet_Handle hEnet = Enet_getHandle(ENET_CPSW_3G, 0U);
    if (hEnet == NULL) { return; }
    uint32_t coreId = EnetSoc_getCoreId();
    uint32_t addr   = DP83869_PHY_ADDR;
    uint16_t before = phy_ext_read(hEnet, coreId, addr, DP83869_RGMIICTL_EXT);
    if (before != 0x00D0U) {
        phy_ext_write(hEnet, coreId, addr, DP83869_RGMIICTL_EXT, 0x00D0U);
        uint16_t after = phy_ext_read(hEnet, coreId, addr, DP83869_RGMIICTL_EXT);
        DebugP_log("[RGMII-RESTORE] RGMIICTL %04X -> %04X (target 00D0)\r\n",
                   (unsigned)before, (unsigned)after);
    } else {
        DebugP_log("[RGMII-RESTORE] RGMIICTL already 00D0, no change\r\n");
    }
}

/* SUSPECTED ROOT-CAUSE FIX: enable RGMII TX clock delay in DP83869.
 *
 * RGMIIDCTL = 0x0077 confirms TX delay value is pre-programmed for
 * 2 ns (TXDLYCTRL=7 -> (7+1)*250ps = 2000ps). But RGMIICTL = 0x00D0
 * has bit 1 (TXCLKDLY enable) CLEAR â€” so the delay-line is bypassed
 * and the PHY drives RGMII TX with zero clock skew. The switch's
 * RGMII receiver can't sample reliably with zero skew at 1000M
 * (125 MHz DDR) and silently CRC-drops every frame at L1. CPSW MAC
 * counters increment normally because the frames look fine to the MAC
 * â€” only the line-side sees corruption.
 *
 * Why didn't the SDK init code program this? Investigation TODO â€”
 * either Dp83869_setClkShift() never runs on the externally-managed
 * PHY path, the rmw is overwritten by autoneg-restart, or a strap
 * locks the bit. For now we just write it directly here and prove
 * the diagnosis by seeing TX become visible to the PC. */
static void fix_phy_tx_delay(void) __attribute__((unused));
static void fix_phy_tx_delay(void)
{
    Enet_Handle hEnet = Enet_getHandle(ENET_CPSW_3G, 0U);
    if (hEnet == NULL) {
        DebugP_log("[TXDLY] Enet_getHandle NULL\r\n");
        return;
    }
    uint32_t coreId = EnetSoc_getCoreId();
    uint32_t addr   = DP83869_PHY_ADDR;

    uint16_t before = phy_ext_read(hEnet, coreId, addr, DP83869_RGMIICTL_EXT);
    /* Set bit 1 (TXCLKDLY) AND bit 0 (RXCLKDLY) â€” both should be on
     * for symmetric RGMII timing. Preserve everything else. */
    uint16_t after  = before | 0x0003U;
    phy_ext_write(hEnet, coreId, addr, DP83869_RGMIICTL_EXT, after);
    uint16_t verify = phy_ext_read(hEnet, coreId, addr, DP83869_RGMIICTL_EXT);
    DebugP_log("[TXDLY] RGMIICTL: before=%04X wrote=%04X readback=%04X\r\n",
               (unsigned)before, (unsigned)after, (unsigned)verify);
    if (verify != after) {
        DebugP_log("[TXDLY] WARN: readback mismatch â€” strap may lock these bits\r\n");
    }
    /* Brief settle. Don't restart autoneg; the link is already up at
     * 1000M FD per the prior dump_phy_state. */
    vTaskDelay(pdMS_TO_TICKS(500));
}

/* Force PHY to 100M FULL DUPLEX with autoneg disabled, then re-inject
 * link-up to CPSW so the MAC follows. Cheap unmanaged switches usually
 * tolerate RGMII timing far better at 100M than 1000M; if the host PC
 * starts seeing our frames after this, the diagnosis is "1000M-T RGMII
 * timing margin too tight" â€” fixable by tuning Dp83869 TX delay.
 *
 * NOTE: superseded by fix_phy_tx_delay() which targets the actual
 * confirmed root cause (RGMIICTL bit 1 not getting programmed). Kept
 * for future reference. */
static void force_phy_100m_fd(void) __attribute__((unused));
static void force_phy_100m_fd(void)
{
    Enet_Handle hEnet = Enet_getHandle(ENET_CPSW_3G, 0U);
    if (hEnet == NULL) {
        DebugP_log("[FORCE100M] Enet_getHandle NULL\r\n");
        return;
    }
    uint32_t coreId = EnetSoc_getCoreId();

    /* BMCR=0x2100: speed-LSB=1 (100M), aneg-en=0, duplex=1 (FD). */
    int32_t status = mdio_write16(hEnet, coreId, DP83869_PHY_ADDR,
                                  DP83869_BMCR, 0x2100U);
    DebugP_log("[FORCE100M] BMCR=0x2100 write status=%d\r\n", (int)status);
    vTaskDelay(pdMS_TO_TICKS(2000));

    uint16_t bmcr   = mdio_read16(hEnet, coreId, DP83869_PHY_ADDR, DP83869_BMCR);
    uint16_t bmsr   = mdio_read16(hEnet, coreId, DP83869_PHY_ADDR, DP83869_BMSR);
    uint16_t physts = mdio_read16(hEnet, coreId, DP83869_PHY_ADDR, DP83869_PHYSTS);
    DebugP_log("[FORCE100M] BMCR=%04X BMSR=%04X PHYSTS=%04X\r\n",
               (unsigned)bmcr, (unsigned)bmsr, (unsigned)physts);

    /* Re-inject link-up to CPSW so MAC1 reprograms its RGMII clock
     * (125 MHz @ 1000M, 25 MHz @ 100M, 2.5 MHz @ 10M). */
    Enet_ExtPhyLinkUpEventInfo info = {
        .macPort = ENET_MAC_PORT_1,
        .phyLinkCfg = {
            .speed     = ENETPHY_SPEED_100MBIT,
            .duplexity = ENETPHY_DUPLEX_FULL,
        },
    };
    Enet_IoctlPrms prms;
    int32_t istatus;
    ENET_IOCTL_SET_IN_ARGS(&prms, &info);
    ENET_IOCTL(hEnet, coreId, ENET_PER_IOCTL_HANDLE_EXTPHY_LINKUP_EVENT,
               &prms, istatus);
    DebugP_log("[FORCE100M] CPSW MAC1 re-link 100M FD: status=%d\r\n",
               (int)istatus);

    if (netif_default != NULL) {
        LOCK_TCPIP_CORE();
        netif_set_link_up(netif_default);
        UNLOCK_TCPIP_CORE();
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    EnetMacPort_LinkCfg linkCfg = { 0 };
    Enet_MacPort port = ENET_MAC_PORT_1;
    ENET_IOCTL_SET_INOUT_ARGS(&prms, &port, &linkCfg);
    ENET_IOCTL(hEnet, coreId, ENET_PER_IOCTL_GET_PORT_LINK_CFG, &prms, istatus);
    if (istatus == ENET_SOK) {
        DebugP_log("[FORCE100M] MAC1 reprogrammed: speed=%sM duplex=%s\r\n",
                   speed_str(linkCfg.speed),
                   (linkCfg.duplexity == ENET_DUPLEX_FULL) ? "FULL" : "HALF");
    }
}

/* Downshift PHY to advertise 100BASE-TX only via auto-negotiation.
 *
 * Why: 1000BASE-T uses ALL FOUR twisted pairs (1-2, 3-6, 4-5, 7-8).
 * 100BASE-TX uses only the 1-2 + 3-6 pairs. If a single TX conductor
 * in the 4-5 or 7-8 pair is broken, 1000M-T silently fails (the PHY
 * even believes link is up because of the IDLE codes still seen on
 * good pairs) but 100M-FD works fine.
 *
 * Why this is different from force_phy_100m_fd(): direct BMCR=0x2100
 * disables autoneg. Most modern switches won't accept a forced-mode
 * partner and will drop the link entirely. The correct way is to
 * keep autoneg enabled, but only ADVERTISE 100M-FD/HD, then restart
 * the negotiation so the switch agrees on 100M.
 *
 * Procedure:
 *   1. write MII reg 0x09 (1000T_CTL) = 0       â€” don't advertise 1000M
 *   2. modify ANAR (reg 0x04) â€” clear 1000M bits, set 100M-FD/HD bits,
 *      keep IEEE-802.3 selector field (bits 4:0 = 0x01)
 *   3. write BMCR (reg 0x00) = 0x1200           â€” enable aneg + restart
 *   4. wait 3 s for autoneg to converge
 *   5. dump BMCR/BMSR/PHYSTS â€” confirm link came back at 100M-FD
 *
 * After this returns, the SDK's link-monitor task should see the new
 * link state and reprogram CPSW MAC1 to 100M automatically. (If not,
 * we'll need an explicit ENET_PER_IOCTL_HANDLE_EXTPHY_LINKUP_EVENT
 * re-injection â€” same shape as force_phy_100m_fd above.)
 */
#define DP83869_ANAR        0x0004U
#define DP83869_1000T_CTL   0x0009U
#define ANAR_SELECTOR_802_3 0x0001U
#define ANAR_10HD           (1U << 5)
#define ANAR_10FD           (1U << 6)
#define ANAR_100HD          (1U << 7)
#define ANAR_100FD          (1U << 8)
#define ANAR_PAUSE_SYM      (1U << 10)
#define ANAR_PAUSE_ASYM     (1U << 11)
#define BMCR_ANEG_ENABLE    (1U << 12)
#define BMCR_ANEG_RESTART   (1U <<  9)

static void downshift_phy_to_100m_aneg(void)
{
    Enet_Handle hEnet = Enet_getHandle(ENET_CPSW_3G, 0U);
    if (hEnet == NULL) {
        DebugP_log("[100M-DOWN] Enet_getHandle NULL\r\n");
        return;
    }
    uint32_t coreId = EnetSoc_getCoreId();
    uint32_t addr   = DP83869_PHY_ADDR;

    /* Snapshot BEFORE so we know what we changed. */
    uint16_t anar_b   = mdio_read16(hEnet, coreId, addr, DP83869_ANAR);
    uint16_t gtctl_b  = mdio_read16(hEnet, coreId, addr, DP83869_1000T_CTL);
    uint16_t bmcr_b   = mdio_read16(hEnet, coreId, addr, DP83869_BMCR);
    DebugP_log("[100M-DOWN] BEFORE: BMCR=%04X ANAR=%04X 1000T_CTL=%04X\r\n",
               (unsigned)bmcr_b, (unsigned)anar_b, (unsigned)gtctl_b);

    /* Step 1: stop advertising 1000M-T (clear bits 9 = 1000M-FD-cap,
     * 8 = 1000M-HD-cap). Keep master/slave manual config bits cleared. */
    int32_t s1 = mdio_write16(hEnet, coreId, addr, DP83869_1000T_CTL, 0x0000U);

    /* Step 2: ANAR â€” advertise 100M-FD/HD, 10M-FD/HD, sym/asym pause,
     * IEEE 802.3 selector. Explicitly NOT setting any 1000M bits
     * (1000M lives in 1000T_CTL, not ANAR â€” but ANAR bit positions
     * 9-10 are reserved/pause; we set pause via 10-11). */
    uint16_t anar_new = ANAR_SELECTOR_802_3 |
                        ANAR_10HD  | ANAR_10FD  |
                        ANAR_100HD | ANAR_100FD |
                        ANAR_PAUSE_SYM | ANAR_PAUSE_ASYM;
    int32_t s2 = mdio_write16(hEnet, coreId, addr, DP83869_ANAR, anar_new);

    /* Step 3: kick autoneg restart (preserves aneg-enable bit). */
    int32_t s3 = mdio_write16(hEnet, coreId, addr, DP83869_BMCR,
                              BMCR_ANEG_ENABLE | BMCR_ANEG_RESTART);

    DebugP_log("[100M-DOWN] writes: 1000T_CTL=%d ANAR=%d BMCR-restart=%d\r\n",
               (int)s1, (int)s2, (int)s3);

    /* Step 4: give negotiation 3 s. Autoneg typically completes in
     * 200-400 ms but partner switches with stale state can take longer. */
    vTaskDelay(pdMS_TO_TICKS(3000));

    /* Step 5: snapshot AFTER. */
    uint16_t bmcr_a   = mdio_read16(hEnet, coreId, addr, DP83869_BMCR);
    uint16_t bmsr_a   = mdio_read16(hEnet, coreId, addr, DP83869_BMSR);
    uint16_t physts_a = mdio_read16(hEnet, coreId, addr, DP83869_PHYSTS);
    uint16_t anar_a   = mdio_read16(hEnet, coreId, addr, DP83869_ANAR);
    uint16_t gtctl_a  = mdio_read16(hEnet, coreId, addr, DP83869_1000T_CTL);
    DebugP_log("[100M-DOWN] AFTER:  BMCR=%04X BMSR=%04X PHYSTS=%04X "
               "ANAR=%04X 1000T_CTL=%04X\r\n",
               (unsigned)bmcr_a, (unsigned)bmsr_a, (unsigned)physts_a,
               (unsigned)anar_a, (unsigned)gtctl_a);
    /* BMSR bit 5 = aneg complete; bit 2 = link up. PHYSTS upper bits
     * encode actual negotiated speed/duplex (DP83869 datasheet). */
    DebugP_log("[100M-DOWN] BMSR: aneg_done=%u link=%u\r\n",
               (unsigned)((bmsr_a >> 5) & 1U),
               (unsigned)((bmsr_a >> 2) & 1U));

    /* Force CPSW MAC1 to re-program for the new link. The SDK
     * link-monitor SHOULD do this automatically on the next poll, but
     * being explicit removes a race window before the L2 probes run. */
    Enet_ExtPhyLinkUpEventInfo info = {
        .macPort = ENET_MAC_PORT_1,
        .phyLinkCfg = {
            .speed     = ENETPHY_SPEED_100MBIT,
            .duplexity = ENETPHY_DUPLEX_FULL,
        },
    };
    Enet_IoctlPrms prms;
    int32_t istatus;
    ENET_IOCTL_SET_IN_ARGS(&prms, &info);
    ENET_IOCTL(hEnet, coreId, ENET_PER_IOCTL_HANDLE_EXTPHY_LINKUP_EVENT,
               &prms, istatus);
    DebugP_log("[100M-DOWN] CPSW MAC1 re-link 100M FD: status=%d\r\n",
               (int)istatus);

    if (netif_default != NULL) {
        LOCK_TCPIP_CORE();
        netif_set_link_up(netif_default);
        UNLOCK_TCPIP_CORE();
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    /* The SDK's internal link-monitor sometimes fires AFTER our inject
     * and re-programs MAC1 to 1000M (apparently because it samples the
     * PHY again and misinterprets a transient bit). Verify the MAC
     * speed and re-inject 100M FD until it sticks. Without this loop,
     * MAC1 ends up at 1000M while the wire is 100M â†’ broadcasts (ARP)
     * go out but unicast RX is silently dropped â†’ all connect() calls
     * fail with errno=103. See lp-am2434-cpsw-tx-debug update 12. */
    for (int attempt = 0; attempt < 10; ++attempt) {
        EnetMacPort_LinkCfg linkCfg = { 0 };
        Enet_MacPort port = ENET_MAC_PORT_1;
        ENET_IOCTL_SET_INOUT_ARGS(&prms, &port, &linkCfg);
        ENET_IOCTL(hEnet, coreId, ENET_PER_IOCTL_GET_PORT_LINK_CFG, &prms, istatus);
        DebugP_log("[100M-DOWN] check #%d: MAC1 speed=%sM duplex=%s\r\n",
                   attempt,
                   (istatus == ENET_SOK) ? speed_str(linkCfg.speed) : "?",
                   (istatus == ENET_SOK && linkCfg.duplexity == ENET_DUPLEX_FULL) ? "FULL" : "HALF");
        if (istatus == ENET_SOK && linkCfg.speed == ENET_SPEED_100MBIT) {
            break;
        }
        /* Re-inject 100M FD. */
        ENET_IOCTL_SET_IN_ARGS(&prms, &info);
        ENET_IOCTL(hEnet, coreId, ENET_PER_IOCTL_HANDLE_EXTPHY_LINKUP_EVENT,
                   &prms, istatus);
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

/* === GIGABIT DIAGNOSTIC ===
 *
 * Force 1000BASE-T autoneg, wait for link, then read the per-side
 * receiver-status bits in 1000BT_STATUS (reg 0x0A) so we can tell
 * which end of the gigabit pair is unhappy.
 *
 * 1000BT_STATUS (IEEE 802.3 Clause 28 reg 0x0A) layout:
 *   bit 15: master/slave config FAULT (1 = manual config conflict)
 *   bit 14: master/slave RESOLUTION   (1 = local is master)
 *   bit 13: LOCAL  receiver status    (1 = OK,  0 = NOT OK)
 *   bit 12: REMOTE receiver status    (1 = OK,  0 = NOT OK)
 *   bit 11: link partner 1000BASE-T FD capable
 *   bit 10: link partner 1000BASE-T HD capable
 *   bits 7..0: idle error count (saturating, cleared on read)
 *
 * Decision tree after one read:
 *   local=OK,   remote=OK,   idle==0  -> gigabit fully healthy on the wire
 *   local=OK,   remote=OK,   idle>0   -> marginal pair, occasional CRC
 *   local=NOT,  remote=*              -> our PHY's RX path is broken
 *                                       (board RX magnetic, or RX twisted
 *                                       pair to switch â€” pairs 4-5 / 7-8
 *                                       at 1000M, none of which exist at 100M)
 *   local=OK,   remote=NOT            -> switch can't decode our TX
 *                                       (board TX magnetic, or TX pair)
 *   master fault                      -> retry forcing master via 1000T_CTL
 *
 * Also re-asserts RGMIIDCTL post-link (the SDK clobbers extended regs on
 * autoneg-restart on some boards) and counts MAC1 RX frames so we can
 * see if even one unicast lands. */
static void gigabit_diag(void) __attribute__((unused));
static void gigabit_diag(void)
{
    Enet_Handle hEnet = Enet_getHandle(ENET_CPSW_3G, 0U);
    if (hEnet == NULL) {
        DebugP_log("[GIGABIT] Enet_getHandle NULL\r\n");
        return;
    }
    uint32_t coreId = EnetSoc_getCoreId();
    uint32_t addr   = DP83869_PHY_ADDR;

    /* Snapshot extended regs BEFORE so we can see if autoneg-restart
     * clobbers them on this board variant. */
    uint16_t rgmiictl_b  = phy_ext_read(hEnet, coreId, addr, DP83869_RGMIICTL_EXT);
    uint16_t rgmiidctl_b = phy_ext_read(hEnet, coreId, addr, DP83869_RGMIIDCTL);
    DebugP_log("[GIGABIT] BEFORE ext: RGMIICTL=%04X RGMIIDCTL=%04X\r\n",
               (unsigned)rgmiictl_b, (unsigned)rgmiidctl_b);

    /* Step 1: re-advertise 1000M-FD only (clear 100M/10M to force gigabit).
     * 1000T_CTL bit 9 = 1000M-FD-cap, bit 8 = 1000M-HD-cap.
     * bit 12 = master/slave manual-config enable, bit 11 = manual master.
     * For now leave master/slave automatic. Experiment A on 2026-04-27
     * proved forcing MASTER (0x1A00) doesn't help â€” the L2 failure is
     * MAC<->PHY RGMII trace timing on the LAUNCHXL, not a clock-source
     * issue. See /memories/repo/lp-am2434-cpsw-tx-debug.md update 13. */
    uint16_t gtctl_new = (1U << 9);                /* advertise 1000M-FD */
    int32_t s1 = mdio_write16(hEnet, coreId, addr, DP83869_1000T_CTL, gtctl_new);

    /* ANAR: drop 10M/100M advertisements so the link can ONLY come up
     * at 1000M. If it can't negotiate gigabit, link will fail entirely
     * and we'll see PHYSTS link=0 â€” which is the failure we WANT to
     * see (vs the silent "link up but no data" 1000M failure mode). */
    uint16_t anar_new = ANAR_SELECTOR_802_3 |
                        ANAR_PAUSE_SYM | ANAR_PAUSE_ASYM;  /* no 10/100M bits */
    int32_t s2 = mdio_write16(hEnet, coreId, addr, DP83869_ANAR, anar_new);

    /* Step 2: kick autoneg restart. */
    int32_t s3 = mdio_write16(hEnet, coreId, addr, DP83869_BMCR,
                              BMCR_ANEG_ENABLE | BMCR_ANEG_RESTART);
    DebugP_log("[GIGABIT] writes: 1000T_CTL=%d ANAR=%d BMCR-restart=%d\r\n",
               (int)s1, (int)s2, (int)s3);

    /* Step 3: wait for autoneg. Spec is up to ~3.5 s; allow 5. */
    vTaskDelay(pdMS_TO_TICKS(5000));

    /* Step 4: dump the post-aneg state. */
    uint16_t bmcr_a    = mdio_read16(hEnet, coreId, addr, DP83869_BMCR);
    uint16_t bmsr_a    = mdio_read16(hEnet, coreId, addr, DP83869_BMSR);
    uint16_t physts_a  = mdio_read16(hEnet, coreId, addr, DP83869_PHYSTS);
    uint16_t anar_a    = mdio_read16(hEnet, coreId, addr, DP83869_ANAR);
    uint16_t gtctl_a   = mdio_read16(hEnet, coreId, addr, DP83869_1000T_CTL);
    uint16_t gtsts_a   = mdio_read16(hEnet, coreId, addr, 0x000AU);  /* 1000T_STS */
    DebugP_log("[GIGABIT] AFTER:  BMCR=%04X BMSR=%04X PHYSTS=%04X ANAR=%04X\r\n",
               (unsigned)bmcr_a, (unsigned)bmsr_a, (unsigned)physts_a, (unsigned)anar_a);
    DebugP_log("[GIGABIT] AFTER:  1000T_CTL=%04X 1000T_STS=%04X\r\n",
               (unsigned)gtctl_a, (unsigned)gtsts_a);
    DebugP_log("[GIGABIT] BMSR: aneg_done=%u link=%u\r\n",
               (unsigned)((bmsr_a >> 5) & 1U),
               (unsigned)((bmsr_a >> 2) & 1U));
    DebugP_log("[GIGABIT] 1000BT_STS: ms_fault=%u ms_role=%s "
               "local_rcvr=%s remote_rcvr=%s lp_cap=%c%c idle_err=%u\r\n",
               (unsigned)((gtsts_a >> 15) & 1U),
               ((gtsts_a >> 14) & 1U) ? "MASTER" : "SLAVE",
               ((gtsts_a >> 13) & 1U) ? "OK" : "NOT_OK",
               ((gtsts_a >> 12) & 1U) ? "OK" : "NOT_OK",
               ((gtsts_a >> 11) & 1U) ? 'F' : '-',
               ((gtsts_a >> 10) & 1U) ? 'H' : '-',
               (unsigned)(gtsts_a & 0xFFU));

    /* Step 5: re-snapshot extended regs to see if autoneg-restart
     * changed them. */
    uint16_t rgmiictl_a  = phy_ext_read(hEnet, coreId, addr, DP83869_RGMIICTL_EXT);
    uint16_t rgmiidctl_a = phy_ext_read(hEnet, coreId, addr, DP83869_RGMIIDCTL);
    DebugP_log("[GIGABIT] AFTER ext: RGMIICTL=%04X RGMIIDCTL=%04X (was %04X/%04X)\r\n",
               (unsigned)rgmiictl_a, (unsigned)rgmiidctl_a,
               (unsigned)rgmiictl_b, (unsigned)rgmiidctl_b);
    if (rgmiictl_a != rgmiictl_b || rgmiidctl_a != rgmiidctl_b) {
        DebugP_log("[GIGABIT] WARN: extended regs changed across autoneg-restart\r\n");
    }

    /* Step 6: tell CPSW MAC1 to re-program for 1000M (mirror of the
     * 100M downshift's loop). */
    Enet_ExtPhyLinkUpEventInfo info = {
        .macPort = ENET_MAC_PORT_1,
        .phyLinkCfg = {
            .speed     = ENETPHY_SPEED_1GBIT,
            .duplexity = ENETPHY_DUPLEX_FULL,
        },
    };
    Enet_IoctlPrms prms;
    int32_t istatus;
    ENET_IOCTL_SET_IN_ARGS(&prms, &info);
    ENET_IOCTL(hEnet, coreId, ENET_PER_IOCTL_HANDLE_EXTPHY_LINKUP_EVENT,
               &prms, istatus);
    DebugP_log("[GIGABIT] CPSW MAC1 re-link 1000M FD: status=%d\r\n",
               (int)istatus);

    if (netif_default != NULL) {
        LOCK_TCPIP_CORE();
        netif_set_link_up(netif_default);
        UNLOCK_TCPIP_CORE();
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    /* Step 7: pin MAC1 at 1000M against the SDK's link-monitor that
     * sometimes re-resamples and downgrades. Same shape as the 100M
     * loop in downshift_phy_to_100m_aneg. */
    for (int attempt = 0; attempt < 10; ++attempt) {
        EnetMacPort_LinkCfg linkCfg = { 0 };
        Enet_MacPort port = ENET_MAC_PORT_1;
        ENET_IOCTL_SET_INOUT_ARGS(&prms, &port, &linkCfg);
        ENET_IOCTL(hEnet, coreId, ENET_PER_IOCTL_GET_PORT_LINK_CFG, &prms, istatus);
        DebugP_log("[GIGABIT] check #%d: MAC1 speed=%sM duplex=%s\r\n",
                   attempt,
                   (istatus == ENET_SOK) ? speed_str(linkCfg.speed) : "?",
                   (istatus == ENET_SOK && linkCfg.duplexity == ENET_DUPLEX_FULL) ? "FULL" : "HALF");
        if (istatus == ENET_SOK && linkCfg.speed == ENET_SPEED_1GBIT) {
            break;
        }
        ENET_IOCTL_SET_IN_ARGS(&prms, &info);
        ENET_IOCTL(hEnet, coreId, ENET_PER_IOCTL_HANDLE_EXTPHY_LINKUP_EVENT,
                   &prms, istatus);
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    /* Step 8: re-read 1000BT_STS one more time after link is fully
     * established + traffic has had a chance to flow, so the idle-error
     * counter reflects steady-state. */
    vTaskDelay(pdMS_TO_TICKS(2000));
    uint16_t gtsts_final = mdio_read16(hEnet, coreId, addr, 0x000AU);
    DebugP_log("[GIGABIT] FINAL 1000BT_STS=%04X "
               "local_rcvr=%s remote_rcvr=%s idle_err=%u\r\n",
               (unsigned)gtsts_final,
               ((gtsts_final >> 13) & 1U) ? "OK" : "NOT_OK",
               ((gtsts_final >> 12) & 1U) ? "OK" : "NOT_OK",
               (unsigned)(gtsts_final & 0xFFU));

    /* If both rcvrs OK + zero idle errors but L2 still fails, the bug
     * isn't on the wire â€” it's RGMII MAC<->PHY timing at 125 MHz DDR.
     * Next experiment would be to flip RGMIICTL TXCLKDLY+RXCLKDLY (bits
     * 1+0) ON for 1000M only â€” but Update 6 in lp-am2434-cpsw-tx-debug.md
     * proved that breaks RX completely on this board. So the actual fix
     * for gigabit on the LAUNCHXL is probably a board-revision issue
     * (RGMII trace lengths) and the production solution is to keep the
     * 100M downshift, OR to redesign with the AM2434 ICSSG path which
     * has its own internal delay-lines. */
}

/* DP83869 internal loopback test â€” proves whether MACâ†”PHY internal
 * datapath works in isolation from the wire side. INCONCLUSIVE in
 * practice, see /memories/repo/lp-am2434-cpsw-tx-debug.md update 5. */
static void run_phy_loopback_test(void)
{
    Enet_Handle hEnet = Enet_getHandle(ENET_CPSW_3G, 0U);
    if (hEnet == NULL) {
        DebugP_log("[LOOPBACK] Enet_getHandle NULL, skipping\r\n");
        return;
    }
    uint32_t coreId = EnetSoc_getCoreId();

    uint16_t bmcr_save = mdio_read16(hEnet, coreId, DP83869_PHY_ADDR, DP83869_BMCR);
    DebugP_log("[LOOPBACK] saved BMCR=%04X\r\n", (unsigned)bmcr_save);

    /* Force 1000M FULL + LOOPBACK, autoneg DISABLED. BMCR layout:
     *  bit15=reset, bit14=loopback, bit13=speed-LSB, bit12=aneg-en,
     *  bit11=power-down, bit10=isolate, bit9=restart-aneg, bit8=duplex,
     *  bit6=speed-MSB. 1000M FD = 0x0140; +loopback = 0x4140. */
    int32_t status = mdio_write16(hEnet, coreId, DP83869_PHY_ADDR,
                                  DP83869_BMCR, 0x4140U);
    if (status != ENET_SOK) {
        DebugP_log("[LOOPBACK] BMCR write failed: %d\r\n", (int)status);
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    uint16_t bmcr_after = mdio_read16(hEnet, coreId, DP83869_PHY_ADDR, DP83869_BMCR);
    DebugP_log("[LOOPBACK] BMCR now=%04X (loopback=%u)\r\n",
               (unsigned)bmcr_after, (unsigned)((bmcr_after >> 14) & 1U));

    /* The SDK's link monitor will see PHY link drop and call
     * Cpsw_handleLinkDown, which disables MAC1 TX. Re-inject a link-up
     * event so the MAC stays enabled, and force the lwIP netif back to
     * link-up so the IP layer keeps emitting ARP/DHCP traffic. */
    Enet_ExtPhyLinkUpEventInfo linkUpInfo = {
        .macPort = ENET_MAC_PORT_1,
        .phyLinkCfg = {
            .speed     = ENETPHY_SPEED_1GBIT,
            .duplexity = ENETPHY_DUPLEX_FULL,
        },
    };
    Enet_IoctlPrms lprms;
    int32_t lstatus;
    ENET_IOCTL_SET_IN_ARGS(&lprms, &linkUpInfo);
    ENET_IOCTL(hEnet, coreId, ENET_PER_IOCTL_HANDLE_EXTPHY_LINKUP_EVENT,
               &lprms, lstatus);
    DebugP_log("[LOOPBACK] re-injected MAC1 link-up: status=%d\r\n", (int)lstatus);
    if (netif_default != NULL) {
        LOCK_TCPIP_CORE();
        netif_set_link_up(netif_default);
        UNLOCK_TCPIP_CORE();
        DebugP_log("[LOOPBACK] forced netif link_up\r\n");
    }
    vTaskDelay(pdMS_TO_TICKS(300));

    /* Snapshot MAC1 stats. */
    Enet_MacPort port = ENET_MAC_PORT_1;
    Enet_IoctlPrms prms;
    CpswStats_MacPort_Ng before, after;
    memset(&before, 0, sizeof(before));
    memset(&after, 0, sizeof(after));

    ENET_IOCTL_SET_INOUT_ARGS(&prms, &port, &before);
    ENET_IOCTL(hEnet, coreId, ENET_STATS_IOCTL_GET_MACPORT_STATS, &prms, status);
    DebugP_log("[LOOPBACK] before: txGood=%u txOctets=%u rxGood=%u rxOctets=%u\r\n",
               (unsigned)before.txGoodFrames, (unsigned)before.txOctets,
               (unsigned)before.rxGoodFrames, (unsigned)before.rxOctets);

    /* Actively push a few broadcast UDP frames so we GUARANTEE TX
     * traffic, instead of relying on lwIP's natural cadence. */
    int sock = lwip_socket(AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0) {
        int bcast = 1;
        lwip_setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &bcast, sizeof(bcast));
        struct sockaddr_in dst;
        memset(&dst, 0, sizeof(dst));
        dst.sin_family = AF_INET;
        dst.sin_port   = lwip_htons(7);  /* echo */
        dst.sin_addr.s_addr = ipaddr_addr("255.255.255.255");
        const char *pl = "LOOPBACK_TEST_PACKET";
        int sent = 0;
        for (int i = 0; i < 10; i++) {
            int n = lwip_sendto(sock, pl, (int)strlen(pl), 0,
                                (struct sockaddr *)&dst, sizeof(dst));
            if (n > 0) sent++;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        DebugP_log("[LOOPBACK] sent %d/10 UDP broadcast frames\r\n", sent);
        lwip_close(sock);
    } else {
        DebugP_log("[LOOPBACK] socket() failed\r\n");
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    ENET_IOCTL_SET_INOUT_ARGS(&prms, &port, &after);
    ENET_IOCTL(hEnet, coreId, ENET_STATS_IOCTL_GET_MACPORT_STATS, &prms, status);
    DebugP_log("[LOOPBACK] after:  txGood=%u txOctets=%u rxGood=%u rxOctets=%u\r\n",
               (unsigned)after.txGoodFrames, (unsigned)after.txOctets,
               (unsigned)after.rxGoodFrames, (unsigned)after.rxOctets);

    unsigned txD = (unsigned)(after.txGoodFrames - before.txGoodFrames);
    unsigned rxD = (unsigned)(after.rxGoodFrames - before.rxGoodFrames);
    DebugP_log("[LOOPBACK] DELTA: tx=%u rx=%u  -> %s\r\n",
               txD, rxD,
               (txD == 0)
                   ? "INCONCLUSIVE: MAC didn't TX (CPSW or lwIP gated)."
                   : (rxD >= (txD - 1U))
                       ? "MAC<->PHY OK (echo received). PHY wire-side is the fault."
                       : "NO ECHO. MAC<->PHY interface itself is broken (board-level fault).");

    /* Restore: clear loopback, restart autoneg so the rest of the
     * smoke test can run normally. BMCR=0x1340 = aneg+restart+1000M+FD. */
    mdio_write16(hEnet, coreId, DP83869_PHY_ADDR, DP83869_BMCR, 0x1340U);
    DebugP_log("[LOOPBACK] restored, restarting autoneg, waiting 3 s for relink...\r\n");
    vTaskDelay(pdMS_TO_TICKS(3000));
}

/* Wait until lwIP has a default netif with a non-zero IPv4 address AND
 * the PHY link is actually up. netif_is_up() flips ~0.5 s before
 * Cpsw_handleExternalPhyLinkUp fires, so connecting on netif_is_up()
 * alone reliably returns errno=113 (EHOSTUNREACH). We must also gate on
 * netif_is_link_up(). Returns true on full link-up, false on timeout. */
static bool wait_for_netif(uint32_t timeout_ms)
{
    uint32_t waited = 0;
    bool announced_admin_up = false;
    while (waited < timeout_ms) {
        struct netif *nif = netif_default;
        if (nif != NULL && netif_is_up(nif) &&
            !ip4_addr_isany_val(*netif_ip4_addr(nif))) {
            if (!announced_admin_up) {
                DebugP_log("[SMOKE] netif up: %s (waiting for PHY link)\r\n",
                           ip4addr_ntoa(netif_ip4_addr(nif)));
                announced_admin_up = true;
            }
            if (netif_is_link_up(nif)) {
                DebugP_log("[SMOKE] PHY link up after %u ms\r\n",
                           (unsigned)waited);
                return true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(250));
        waited += 250;
    }
    return false;
}

/* Phase C-ter L2 reachability probe. Attempts a TCP connect with a 2 s
 * timeout and prints the outcome. Outcomes:
 *   - "ok"           SYN+ACK, full handshake, port open
 *   - "rst"          RST received => port closed, but ARP+L2 confirmed
 *   - "no-route"     errno=113 EHOSTUNREACH = ARP failed
 *   - "timeout"      errno=110/116 = no L2 reply at all
 *
 * "ok" or "rst" on ANY target proves board's TX path works on the wire
 * â€” only a specific peer (likely the PC) is failing to see/respond to
 * our frames. "timeout"/"no-route" on EVERY target means board TX is
 * the common failure (or every host on the LAN ignores us).
 */
static void try_l2_probe(const char *label, const char *host, uint16_t port)
{
    int sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        DebugP_log("[L2-PROBE] %s socket() failed\r\n", label);
        return;
    }
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port   = lwip_htons(port);
    dst.sin_addr.s_addr = ipaddr_addr(host);
    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    lwip_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    DebugP_log("[L2-PROBE] %-7s %s:%u ...\r\n", label, host, (unsigned)port);
    int rc = lwip_connect(sock, (struct sockaddr *)&dst, sizeof(dst));
    if (rc == 0) {
        DebugP_log("[L2-PROBE] %-7s OK (port open) -- L2 to %s WORKS\r\n",
                   label, host);
    } else {
        int e = errno;
        const char *kind;
        switch (e) {
            case 111: kind = "rst (port closed, ARP+L2 OK)"; break;
            case 113: kind = "no-route (ARP failed)";        break;
            case 110: kind = "timeout (no L2 reply)";        break;
            case 116: kind = "timeout (recv)";               break;
            default:  kind = "other";                        break;
        }
        DebugP_log("[L2-PROBE] %-7s FAIL errno=%d %s\r\n", label, e, kind);
    }
    lwip_close(sock);
    /* Tiny gap so socket fully closes & ARP cache entry can settle. */
    vTaskDelay(pdMS_TO_TICKS(250));
}

/* do_one_modbus_read() removed 2026-05-20.
 * Was the original Phase-C single-shot Modbus probe against the orbit-sim
 * on the workstation at 10.47.27.100. Already marked __attribute__((unused))
 * for weeks before removal — production polling lives in orbit_client.c
 * (the OrbitClient_Init list at the bottom of lwip_smoke_task). */

/* Public single-shot fixup task for orbit roles (GDC / STORAGE / TRITON).
 *
 * Orbit firmwares don't run the full lwip_smoke_task (which is the
 * controller-side TCP probe rig), but the LAUNCHXL board still needs
 * the same PHY workarounds before TCP is reachable from a peer:
 *   1. restore_rgmii_ctl()         â€” undo any leftover write to the
 *                                    PHY's RGMIICTL from past experiments
 *                                    (PHY MDIO writes survive warm reset).
 *   2. downshift_phy_to_100m_aneg()â€” drop the negotiated rate from 1000M
 *                                    to 100M to dodge the documented
 *                                    LAUNCHXL gigabit RGMII timing issue
 *                                    (TX silently fails at 1000M; see
 *                                    /memories/repo/lp-am2434-cpsw-tx-debug.md
 *                                    update 13).
 *
 * Runs once after lwIP comes up, then exits. Custom Nova hardware with
 * proper RGMII layout will be able to skip step 2 (re-validate via
 * gigabit_diag), but step 1 is harmless on any board. */
/* ─── NET-WATCHDOG (2026-06-02, 0.A.216) ──────────────────────────────
 *
 * Defense-in-depth against the post-Activate "MAC half-deaf" wedge that
 * survived the polling-task stop. Polls MAC1 rxGoodFrames every 15 s.
 * If the counter stays unchanged for 4 consecutive checks (60 s with
 * NO incoming frames at all), re-inject EXTPHY_LINKUP_EVENT with
 * 100M-FD so the CPSW MAC1 RGMII clock generator gets reprogrammed.
 *
 * On a normally-functioning orbit, rxGoodFrames climbs ~once per
 * controller Modbus poll (1 Hz) = ~60 frames/min minimum, so any 60 s
 * with zero increment is unambiguously a wedge.
 *
 * Rationale: even with the polling task stopped (Fix 1 of this
 * commit), the wedge could in principle still arise from some other
 * race we haven't characterised. This watchdog catches it without
 * needing operator intervention.
 *
 * See memories/repo/lp-am2434-storage-phy-wedge.md "Workaround for
 * now" — this is the [NET-WATCHDOG] task that note flagged but was
 * never built. */

static uint32_t read_mac1_rxgood(void)
{
    Enet_Handle hEnet = Enet_getHandle(ENET_CPSW_3G, 0U);
    if (hEnet == NULL) return 0u;
    uint32_t coreId = EnetSoc_getCoreId();
    CpswStats_MacPort_Ng macStats;
    memset(&macStats, 0, sizeof(macStats));
    Enet_MacPort port = ENET_MAC_PORT_1;
    Enet_IoctlPrms prms;
    int32_t status;
    ENET_IOCTL_SET_INOUT_ARGS(&prms, &port, &macStats);
    ENET_IOCTL(hEnet, coreId, ENET_STATS_IOCTL_GET_MACPORT_STATS, &prms, status);
    if (status != ENET_SOK) return 0u;
    return (uint32_t)macStats.rxGoodFrames;
}

static int32_t reinject_100m_link(void)
{
    Enet_Handle hEnet = Enet_getHandle(ENET_CPSW_3G, 0U);
    if (hEnet == NULL) return -1;
    uint32_t coreId = EnetSoc_getCoreId();
    Enet_ExtPhyLinkUpEventInfo info = {
        .macPort = ENET_MAC_PORT_1,
        .phyLinkCfg = {
            .speed     = ENETPHY_SPEED_100MBIT,
            .duplexity = ENETPHY_DUPLEX_FULL,
        },
    };
    Enet_IoctlPrms prms;
    int32_t status;
    ENET_IOCTL_SET_IN_ARGS(&prms, &info);
    ENET_IOCTL(hEnet, coreId, ENET_PER_IOCTL_HANDLE_EXTPHY_LINKUP_EVENT,
               &prms, status);
    return status;
}

void lwip_net_watchdog_task(void *args)
{
    (void)args;

    /* Let the fixup task complete first (it waits ~2.5 s for netif + 3 s
     * for autoneg + ~3 s for the 10-iter check loop ≈ 9 s worst case).
     * 15 s baseline gives generous margin. */
    vTaskDelay(pdMS_TO_TICKS(15000));

    DebugP_log("[NET-WD] task started — polling MAC1 rxGood every 15 s, "
               "trigger on 4× unchanged (= 60 s no RX)\r\n");

    uint32_t last_rxGood = read_mac1_rxgood();
    uint32_t stuck_count = 0;
    uint32_t recoveries = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(15000));

        uint32_t now_rxGood = read_mac1_rxgood();
        if (now_rxGood == last_rxGood) {
            stuck_count++;
            DebugP_log("[NET-WD] rxGood=%u stuck for %u/4 checks "
                       "(recoveries so far=%u)\r\n",
                       (unsigned)now_rxGood, (unsigned)stuck_count,
                       (unsigned)recoveries);
            if (stuck_count >= 4u) {
                int32_t rc = reinject_100m_link();
                recoveries++;
                DebugP_log("[NET-WD] !!! 60 s no RX → re-injected 100M FD "
                           "(rc=%d) — total recoveries=%u\r\n",
                           (int)rc, (unsigned)recoveries);
                stuck_count = 0;
                /* Skip one cycle so the freshly-reprogrammed MAC has
                 * time to actually see frames before we judge again. */
                vTaskDelay(pdMS_TO_TICKS(15000));
                last_rxGood = read_mac1_rxgood();
            }
        } else {
            if (stuck_count > 0u) {
                DebugP_log("[NET-WD] rxGood recovered: %u → %u (was stuck %u)\r\n",
                           (unsigned)last_rxGood, (unsigned)now_rxGood,
                           (unsigned)stuck_count);
            }
            stuck_count = 0;
            last_rxGood = now_rxGood;
        }
    }
}

void lwip_phy_fixup_task(void *args)
{
    (void)args;

    /* Same wait-for-netif rhythm as lwip_smoke_task. */
    vTaskDelay(pdMS_TO_TICKS(2000));
    if (!wait_for_netif(20000)) {
        DebugP_log("[PHY-FIXUP] no netif within 20 s, giving up\r\n");
        vTaskDelete(NULL);
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    restore_rgmii_ctl();
    downshift_phy_to_100m_aneg();

    /* 0.A.216: stop the SDK polling task that races our 100M re-inject.
     * Without this, the SDK task fires up to 1 s after our retry loop
     * exits and reprograms MAC1 to the CACHED state->speed=1GBIT,
     * leaving the MAC clocking at 125 MHz against a 25 MHz wire (the
     * recurring post-Activate "half-deaf" wedge). Tradeoff: cable-
     * unplug/replug is no longer auto-detected — acceptable for fixed
     * industrial install. See lp-am2434-cpsw-tx-debug.md update 12
     * for the race details. */
    EnetApp_stopPhyRegisterPollingTask();
    DebugP_log("[PHY-FIXUP] SDK polling task stopped (anti-race)\r\n");

    DebugP_log("[PHY-FIXUP] done\r\n");
    vTaskDelete(NULL);
}

void lwip_smoke_task(void *args)
{
    (void)args;

    /* Give the SDK enet task time to spawn and bring up the link. */
    vTaskDelay(pdMS_TO_TICKS(2000));

    if (!wait_for_netif(20000)) {
        DebugP_log("[SMOKE] no netif within 20 s, giving up\r\n");
        vTaskDelete(NULL);
        return;
    }

    /* Even after link_up, give CPSW + ARP a moment to settle before the
     * first connect attempt (otherwise the very first SYN can lose to
     * ARP resolution and surface as EHOSTUNREACH). */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* One-shot diagnostic: PHY internal-loopback test. Tells us whether
     * MAC<->PHY interface is healthy (so the wire side is the fault) or
     * whether even that internal datapath is dead (board fault). Runs
     * once; restores autoneg before the connect loop.
     *
     * NOTE (2026-04-24): proven INCONCLUSIVE â€” CPSW or its lwIP shim
     * has its own link-status gate on TX submission that we can't
     * bypass without deeper SDK surgery. Keeping the function for
     * future reference but disabling the call. See
     * /memories/repo/lp-am2434-cpsw-tx-debug.md update 5. */
    /* run_phy_loopback_test(); */

    /* Productive diagnostics: dump extended PHY config registers
     * (RGMIICTL/RGMIIDCTL/STRAPSTS) to confirm what the PHY actually
     * has programmed.
     *
     * NOTE (2026-04-24): we tried setting RGMIICTL bits 1+0 (TX+RX
     * clock-delay enables) and proved this is a DEAD END â€” setting
     * the RX delay bit completely killed MAC1 RX (rxGood went
     * 130 -> 0). The original RGMIICTL=0x00D0 (both delay bits 0)
     * is the correct config for this board layout. Don't try to
     * tune RGMIICTL bits 1/0 again. */
    restore_rgmii_ctl();
    dump_phy_extended_regs();

    /* Phase C-bis: confirm the RGMII1 jack mux is actually routed to
     * CPSW (not ICSS), and enumerate every PHY chip responding on the
     * MDIO bus. If a second PHY answers (e.g. ICSSG-side DP83869), we
     * have a known-good comparator on the same board. */
    dump_mux_state();
    scan_mdio_bus();

    /* === MONDAY TODO (after cable swap) ===
     * (2026-04-27 RESOLVED) Cable swap was unnecessary â€” the failure is
     * a documented LP-AM243 LaunchPad limitation. TI E2E forum reports
     * the same symptom: "1-Gbps IP Failure: When operating in 1-Gbps
     * full-duplex mode, the board may fail to acquire an IP address
     * (showing 0.0.0.0), while 100 Mbps may work fine." Our diagnostic
     * confirmed L1 is healthy at 1000M (1000BT_STS local_rcvr=OK
     * remote_rcvr=OK idle_err=0, both auto-slave AND forced-master),
     * but every TCP connect at 1000M returns errno=103. Root cause is
     * MAC<->PHY RGMII trace timing on the LAUNCHXL (125 MHz DDR
     * margin) which is not firmware-fixable. Production custom Nova
     * hardware will have proper RGMII trace layout.
     *
     * Decision: 100M downshift is the permanent answer for the
     * LAUNCHXL. Modbus polling has zero gigabit need. The gigabit_diag
     * function is kept (LP_GIGABIT_DIAG=0) for future re-validation
     * if a board revision changes.
     * See: /memories/repo/lp-am2434-cpsw-tx-debug.md update 13,
     *      docs/LP-AM2434-Hardware-Bringup-Plan.md "Phase C closure".
     */
#define LP_GIGABIT_DIAG 0
#if LP_GIGABIT_DIAG
    gigabit_diag();
#else
    downshift_phy_to_100m_aneg();
    /* 0.A.216: see comment in lwip_phy_fixup_task. Stops the SDK 1-sec
     * polling task that races our 100M re-inject and causes the
     * recurring post-Activate "half-deaf" wedge. */
    EnetApp_stopPhyRegisterPollingTask();
    DebugP_log("[SMOKE] SDK polling task stopped (anti-race)\r\n");
#endif

    /* Phase C-ter (2026-04-25): multi-target L2 reachability probe.
     * RESOLVED 2026-04-27 — LAUNCHXL 1000M TX issue confirmed as
     * board-layout limit, fixed via the 100M downshift above.
     * Removed the bench .100/.200 try_l2_probe calls 2026-05-20 —
     * stale (real STORAGE/GDC/TRITON live at .2/.3/.4, PC at .10).
     * The dump_cpsw_stats snapshot is kept — informative on cold boot,
     * shows the TX-error breakdown (defer/coll/excess/late/carrier).
     * If carrier counter is close to txGood, PHY isn't sensing its
     * own carrier and frames are being dropped at the wire interface. */
    dump_cpsw_stats("after-probes");

    /* Phase D-A: hand off from one-shot smoke to continuous multi-orbit
     * polling. Production topology: each orbit is its own AM2434 board
     * on the LAN, listening on Modbus TCP port 5502.
     *
     * Roster (LAN map: /memories/repo/lan-ip-map.md):
     *   STORAGE = 10.47.27.2
     *   GDC     = 10.47.27.3
     *   TRITON  = 10.47.27.4
     *
     * History: until May 2026 this list pointed at the orbit-simulator
     * (single host 10.1.2.230, port-shifted 5502..5506). The simulator
     * is retained for desk-side dev but the on-bench rig now uses the
     * three physical LP-AM2434 boards above. */
    static const OrbitConfig kOrbits[] = {
        { .index = 0, .ipv4 = "10.47.27.2", .port = 5502, .pollIntervalMs = 1000 },
        { .index = 1, .ipv4 = "10.47.27.3", .port = 5502, .pollIntervalMs = 1000 },
        { .index = 2, .ipv4 = "10.47.27.4", .port = 5502, .pollIntervalMs = 1000 },
    };
    int rc = OrbitClient_Init(kOrbits, sizeof(kOrbits) / sizeof(kOrbits[0]));
    DebugP_log("[ORBIT] OrbitClient_Init rc=%d (%u workers)\r\n",
               rc, (unsigned)OrbitClient_Count());

    /* Roll-up status every 10 s so we can see at a glance which orbits
     * are alive and how many polls have completed each. The detailed
     * per-orbit logs come from inside the worker tasks. */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        DebugP_log("[ORBIT] === roll-up ===\r\n");
        for (size_t i = 0; i < OrbitClient_Count(); i++) {
            OrbitSample s;
            if (OrbitClient_GetSample((uint8_t)i, &s)) {
                DebugP_log("[ORBIT %u] %s polls=%u errs=%u "
                           "HR200=%u HR201=%u HR202=%u HR203=%u "
                           "id=%u uptime=%u\r\n",
                           (unsigned)i, s.online ? "UP  " : "DOWN",
                           (unsigned)s.pollCount, (unsigned)s.errorCount,
                           (unsigned)s.sensorHr[0], (unsigned)s.sensorHr[1],
                           (unsigned)s.sensorHr[2], (unsigned)s.sensorHr[3],
                           (unsigned)s.ident[0], (unsigned)s.ident[5]);
            } else {
                DebugP_log("[ORBIT %u] (no sample yet)\r\n", (unsigned)i);
            }
        }
    }
}
