/*
 * Constellation Nova — LP-AM2434 hardware bringup (Phase 2)
 *
 * Proves the full COBS+CRC+protobuf wire link between the LP-AM2434
 * and the RPi5 bridge server.
 *
 * Uses nova_protocol.c / nova_cobs.c / nova_crc16.c from Platform/
 * to send properly framed Envelope messages that the bridge can decode.
 *
 * Hand-encodes a minimal Heartbeat envelope (no nanopb dependency)
 * since the full .pb.c files aren't generated for the LP build yet.
 */

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <kernel/dpl/DebugP.h>
#include <kernel/dpl/ClockP.h>
#include <kernel/dpl/HwiP.h>
#include <drivers/uart.h>
#include "ti_drivers_config.h"
#include "ti_drivers_open_close.h"
#include "ti_board_config.h"
#include "ti_board_open_close.h"
#include "FreeRTOS.h"
#include "task.h"

/* Platform protocol layer */
#include "nova_protocol.h"

/* DMSC reset path — used by CMD_REBOOT_SOC handler so the bridge
 * (and ultimately Azure → unattended OTA) can warm-reset the SoC
 * without JTAG. Same path the auto-flasher uses post-erase. */
#include <drivers/sciclient.h>

/* Multi-orbit Modbus TCP poller (Phase D-A) */
#include "orbit_client.h"
#include "nova_vfd_client.h"

/* Firmware version strings (sent in VersionInfo envelope, tag 22) */
#include "lp_version.h"

/* Engine alarm latches — needed for CMD_CLEAR_ALARM (cmd_type=1).
 * `ClearAlarms(0)` zeros SystemAlarm[]/AlarmTimer[] (legacy hard reset);
 * `WarningsClear()` resets the WARNING[] backing store. After clearing,
 * the engine's per-equipment FailChk re-evaluates next tick — transient
 * alarms stay cleared, persistent ones immediately re-trigger. */
#include "Timer.h"
#include "Warnings.h"

/* Wall-clock for DateTime envelope (tag 21). Set by SettingsUpdate.field 26
 * arriving from the bridge, sourced from the host's clock. */
#include "lp_rtc.h"
#include "lp_settings_store.h"
#include "lp_settings.h"
#include "lp_device_config.h"
#include "lp_ota_task.h"
#include "lp_remote_outside.h"
#include "nova_ota_broker.h"

/* CPLD-elimination diagnostic fields (May 2026): SystemStatus tag 10
 * fields 18-23 expose engine-internal state (SystemState, RunClockMode,
 * raw PWM ticks, RemoteOff bits, E-Stop) so we can see what the engine
 * is deciding without flashing diag-only firmware. These pull externs
 * from already-linked Platform translation units. */
#include "Settings.h"   /* Settings.RemoteOff[], RO_* enum */
#include "States.h"     /* SystemState, RunClockMode(), RC_*, ST_* */
#include "PWM.h"        /* PwmChannel[], PWM_DOORS, PWM_REFRIGERATION,
                         * PWM_MIN_VALUE, PWM_RANGE */
#include "SerialShift.h" /* CheckInputs/CheckOutputs, EQ_FAN, SW_FAN_AUTO/MANUAL */
/* E-Stop is published by lp_engine_shim.c::build_switch_state from
 * the STORAGE orbit's DI11 (di_bitmap bit 10). */
extern uint8_t Nova_GetEStopActive(void);
#include "orbit_server/orbit_role.h"
#include "orbit_server/orbit_state.h"
#include "orbit_server/orbit_modbus_tcp.h"
#include "orbit_server/orbit_safemode.h"
#include "orbit_server/orbit_sensor_rtu.h"
#include "orbit_server/orbit_gdc.h"
#include "orbit_server/orbit_triton.h"
#include "lp_orbit_io.h"

/* lwIP sys port — sys_init() creates the SYS_ARCH_PROTECT mutex used by
 * heap and stats macros. The SDK's freertos sys_arch.c is built with
 * LWIP_FREERTOS_SYS_ARCH_PROTECT_USES_MUTEX=1, so any lwIP API touched
 * before sys_init() runs trips an assert ("sys_arch_protect_mutex != NULL").
 * lwip_init()/tcpip_init() would call sys_init() — but they don't run until
 * the enet task starts main_loop(), which is too late: orbit_modbus_tcp
 * tasks (created by GDC/STORAGE/TRITON init below) may touch lwIP first.
 * Calling sys_init() pre-scheduler is safe (just creates a recursive mutex). */
#include "lwip/sys.h"

/* ---- UART register-level access ---- */
/* AM2434 UART is 16550-compatible. We use polled TX (IER=0) to keep the
 * bringup firmware minimal and avoid pulling in the SDK UART driver's ISR
 * machinery. */
#include <drivers/hw_include/cslr_soc.h>

/* 16550 register offsets */
#define UART_REG_THR    (0x00U)   /* Transmit Holding Register (write) */
#define UART_REG_RHR    (0x00U)   /* Receive Holding Register (read) */
#define UART_REG_IER    (0x04U)   /* Interrupt Enable Register */
#define UART_REG_IIR    (0x08U)   /* Interrupt Identification (read) */
#define UART_REG_FCR    (0x08U)   /* FIFO Control Register (write only) */
#define UART_REG_LSR    (0x14U)   /* Line Status Register */

/* IER bits */
#define UART_IER_ERBI   (1U << 0)  /* Enable Received Data Available IRQ */
#define UART_IER_ELSI   (1U << 2)  /* Enable Line Status IRQ (overrun, etc.) */

/* LSR bits */
#define UART_LSR_DR     (1U << 0)  /* Data Ready */
#define UART_LSR_OE     (1U << 1)  /* Overrun Error */
#define UART_LSR_THRE   (1U << 5)  /* TX Holding Register Empty */

/* AM2434 R5F0 interrupt number for UART2 (from cslr_intr_r5fss0_core0.h). */
#define UART2_IRQ_NUM   (212U)

static volatile uint32_t *s_uart2_base;

/* ─── UART2 RX ring buffer + custom ISR ────────────────────────────────
 *
 * The original bringup firmware drained the 64-byte UART2 hardware FIFO
 * from `bridge_uart_task` on a 100 ms cadence. At 921600 baud the FIFO
 * fills in ~700 µs, so any envelope wider than ~64 wire bytes overflowed
 * silently between polls — bytes were dropped before COBS framing, so
 * `/health` showed zero CRC/COBS errors yet every SettingsUpdate body
 * larger than ~48 B timed out at the bridge's 3 s ACK deadline (runclock,
 * climacell, email/log saves all observed broken).
 *
 * Fix: a narrow custom RX-only ISR drains the hardware FIFO into a 4 KB
 * software ring on every IRQ; `bridge_uart_task` pops from the ring at
 * its own pace and feeds NovaProto. Polled TX is preserved unchanged
 * (the SDK's full UART driver was rejected during bringup because its
 * combined RX/TX ISR stalled TX while RX bytes arrived — our ISR never
 * touches TX state, so that hazard cannot recur).
 *
 * The ring is sized as a power-of-two so wrap is a single AND mask, and
 * head/tail are `volatile uint32_t` so the lock-free single-producer
 * (ISR) / single-consumer (task) pattern is safe on R5F without a
 * critical section. Overrun is counted but does NOT block the producer —
 * if the consumer falls behind, the ring drops the *new* byte, mirroring
 * the legacy hardware-FIFO behaviour that NovaProto already tolerates
 * (CRC failure → frame retry by the bridge). */
#define UART2_RX_RING_SIZE  4096U
#define UART2_RX_RING_MASK  (UART2_RX_RING_SIZE - 1U)

static uint8_t  s_rx_ring[UART2_RX_RING_SIZE];
static volatile uint32_t s_rx_head = 0;   /* written by ISR only */
static volatile uint32_t s_rx_tail = 0;   /* written by task only */
static volatile uint32_t s_rx_dropped = 0; /* ring-full counter */
static volatile uint32_t s_rx_overrun = 0; /* hardware OE counter */

static HwiP_Object s_uart2_hwi;

static void uart2_rx_isr(void *args)
{
    (void)args;
    if (s_uart2_base == NULL) return;

    /* Reading IIR clears the IRQ identification latch; we don't branch
     * on it because both RDA (0x04) and CTI (0x0C) require the same
     * action: drain RHR until LSR.DR clears. Reading LSR also clears
     * the overrun flag, which we count for diagnostics. */
    (void)s_uart2_base[UART_REG_IIR / 4];

    uint32_t lsr;
    while ((lsr = s_uart2_base[UART_REG_LSR / 4]) & UART_LSR_DR) {
        if (lsr & UART_LSR_OE) s_rx_overrun++;
        uint8_t byte = (uint8_t)(s_uart2_base[UART_REG_RHR / 4] & 0xFF);
        uint32_t head = s_rx_head;
        uint32_t next = (head + 1U) & UART2_RX_RING_MASK;
        if (next == s_rx_tail) {
            /* Ring full — drop. Bridge will retry on missing ACK. */
            s_rx_dropped++;
        } else {
            s_rx_ring[head] = byte;
            s_rx_head = next;
        }
    }
}

/* Single-byte ring pop for the consumer (bridge_uart_task). Returns
 * true iff a byte was available. */
static inline bool uart2_rx_ring_pop(uint8_t *out)
{
    uint32_t tail = s_rx_tail;
    if (tail == s_rx_head) return false;
    *out = s_rx_ring[tail];
    s_rx_tail = (tail + 1U) & UART2_RX_RING_MASK;
    return true;
}

/* ---- Task config ---- */
#define NOVA_TASK_PRI   (configMAX_PRIORITIES - 1)
#define NOVA_TASK_STACK (16384U / sizeof(configSTACK_DEPTH_TYPE))

static StackType_t  gNovaTaskStack[NOVA_TASK_STACK] __attribute__((aligned(32)));
static StaticTask_t gNovaTaskObj;

#define BRIDGE_TASK_PRI   (configMAX_PRIORITIES - 2)
#define BRIDGE_TASK_STACK (16384U / sizeof(configSTACK_DEPTH_TYPE))

static StackType_t  gBridgeTaskStack[BRIDGE_TASK_STACK] __attribute__((aligned(32)));
static StaticTask_t gBridgeTaskObj;

/* Ethernet (lwIP) task — runs the SDK enet_lwip_cpsw reference example.
 * Brings up CPSW2G + 2 PHYs + lwIP TCP/IP stack with static IPs
 * 10.1.2.249 (port 0) / 10.1.2.250 (port 1), gateway 10.1.2.1.
 * (Bumped from .220/.221 + GW .3 on 2026-04-25 to rule out IP
 * conflict and fix the wrong gateway.) Runs at lower priority
 * than the UART2 bridge task so heartbeat/protobuf framing is never
 * blocked by Ethernet activity. Defined in enet/test_enet.c. */
extern int enet_lwip_example(void *args);

#define ENET_TASK_PRI   (configMAX_PRIORITIES - 4)
#define ENET_TASK_STACK (16384U / sizeof(configSTACK_DEPTH_TYPE))

static StackType_t  gEnetTaskStack[ENET_TASK_STACK] __attribute__((aligned(32)));
static StaticTask_t gEnetTaskObj;

static void enet_task_entry(void *args)
{
    (void)args;
    enet_lwip_example(NULL);
    /* Should never return; if it does, sleep to keep the task alive. */
    while (1) { vTaskDelay(pdMS_TO_TICKS(60000)); }
}

/* Controller orbit-polling task. Misnamed "smoke" for historical reasons
 * (started life as a bench TCP probe in Phase C). It now does the
 * production OrbitClient init + 10s `[ORBIT] === roll-up ===` logging
 * loop. Defined in lwip_smoke.c. Lower priority than the bridge so it
 * never starves heartbeat framing. */
extern void lwip_smoke_task(void *args);

#define SMOKE_TASK_PRI   (configMAX_PRIORITIES - 5)
#define SMOKE_TASK_STACK (8192U / sizeof(configSTACK_DEPTH_TYPE))

static StackType_t  gSmokeTaskStack[SMOKE_TASK_STACK] __attribute__((aligned(32)));
static StaticTask_t gSmokeTaskObj;

/* PHY fixup task: orbit-role equivalent of the prologue inside
 * lwip_smoke_task that runs restore_rgmii_ctl + downshift_phy_to_100m_aneg.
 * Required because the LAUNCHXL board can't reliably TX at 1000M (RGMII
 * trace timing) — without this the Modbus TCP listen socket comes up
 * but no peer can ever ARP us at gigabit. See
 * /memories/repo/lp-am2434-cpsw-tx-debug.md update 13. One-shot task,
 * vTaskDelete(NULL) on completion. */
extern void lwip_phy_fixup_task(void *args);
#define PHY_FIXUP_TASK_PRI   (configMAX_PRIORITIES - 5)
#define PHY_FIXUP_TASK_STACK (4096U / sizeof(configSTACK_DEPTH_TYPE))
static StackType_t  gPhyFixupStack[PHY_FIXUP_TASK_STACK] __attribute__((aligned(32)));
static StaticTask_t gPhyFixupObj;

/* NET-WATCHDOG task (0.A.216): polls MAC1 rxGoodFrames every 15 s and
 * re-injects 100M-FD EXTPHY_LINKUP_EVENT if 60 s elapse with zero
 * incoming frames. Defense in depth on top of the SDK-polling-stop in
 * lwip_phy_fixup_task / lwip_smoke_task. Spawned for ALL roles —
 * controller and orbits both vulnerable to the post-Activate
 * MAC↔PHY rate-mismatch wedge. Long-running, no vTaskDelete. */
extern void lwip_net_watchdog_task(void *args);
#define NET_WD_TASK_PRI   (configMAX_PRIORITIES - 6)
#define NET_WD_TASK_STACK (3072U / sizeof(configSTACK_DEPTH_TYPE))
static StackType_t  gNetWdStack[NET_WD_TASK_STACK] __attribute__((aligned(32)));
static StaticTask_t gNetWdObj;

/* Orbit Modbus TCP server task — only spawned when the LP boots in an
 * orbit role (STORAGE / GDC / TRITON). Same priority as the smoke task
 * (well below bridge UART) since orbit polls are not latency critical.
 * Stack is 8 KB to leave room for the 280-byte RX/TX buffers plus a
 * generous lwIP socket-call working set. */
#define ORBIT_MBT_TASK_PRI   (configMAX_PRIORITIES - 5)
#define ORBIT_MBT_TASK_STACK (8192U / sizeof(configSTACK_DEPTH_TYPE))

static StackType_t  gOrbitMbtStack[ORBIT_MBT_TASK_STACK] __attribute__((aligned(32)));
static StaticTask_t gOrbitMbtObj;

/* Comm-loss safe-mode watchdog — 1 Hz, holds OrbitState lock during
 * its tick. Higher priority than the TCP task so a stuck Modbus
 * handler can't starve the watchdog and leave outputs hot. */
#define ORBIT_SM_TASK_PRI    (configMAX_PRIORITIES - 4)
#define ORBIT_SM_TASK_STACK  (4096U / sizeof(configSTACK_DEPTH_TYPE))
static StackType_t  gOrbitSmStack[ORBIT_SM_TASK_STACK] __attribute__((aligned(32)));
static StaticTask_t gOrbitSmObj;

/* Sensor-board RTU master (USART4) — Phase 2 stub task. */
#define ORBIT_SR_TASK_PRI    (configMAX_PRIORITIES - 5)
#define ORBIT_SR_TASK_STACK  (4096U / sizeof(configSTACK_DEPTH_TYPE))
static StackType_t  gOrbitSrStack[ORBIT_SR_TASK_STACK] __attribute__((aligned(32)));
static StaticTask_t gOrbitSrObj;

/* GDC actuator state-machine — 10 Hz, only spawned in GDC role. Lower
 * priority than the safemode watchdog so a stuck tick can't block the
 * comm-loss check; higher than the TCP server so register-write traffic
 * can't starve the actuator update. */
#define ORBIT_GDC_TASK_PRI   (configMAX_PRIORITIES - 4)
#define ORBIT_GDC_TASK_STACK (4096U / sizeof(configSTACK_DEPTH_TYPE))
static StackType_t  gOrbitGdcStack[ORBIT_GDC_TASK_STACK] __attribute__((aligned(32)));
static StaticTask_t gOrbitGdcObj;

/* TRITON refrigeration tick — 10 Hz, only spawned in TRITON role. Same
 * priority slot as the GDC tick so the safemode watchdog can preempt
 * either of them. */
#define ORBIT_TRITON_TASK_PRI   (configMAX_PRIORITIES - 4)
#define ORBIT_TRITON_TASK_STACK (4096U / sizeof(configSTACK_DEPTH_TYPE))
static StackType_t  gOrbitTritonStack[ORBIT_TRITON_TASK_STACK] __attribute__((aligned(32)));
static StaticTask_t gOrbitTritonObj;

/* OTA listener (:5503) — Phase 1A version-reporting only, no flash
 * writes yet.  Spawned on every role that has Ethernet (CONTROLLER +
 * all orbit roles).  Lowest-tier priority since it must never preempt
 * the safemode watchdog or the orbit tick handlers. */
#define LP_OTA_TASK_PRI    (configMAX_PRIORITIES - 6)
#define LP_OTA_TASK_STACK  (8192U / sizeof(configSTACK_DEPTH_TYPE))
static StackType_t  gLpOtaStack[LP_OTA_TASK_STACK] __attribute__((aligned(32)));
static StaticTask_t gLpOtaObj;

/* Equipment-output sync (Phase E2, fw 0.A.15+) — CONTROLLER only.
 *
 * Walks `LpSettings.io_config.output_map[]` × `remote_off.state[]`
 * every ~250 ms and drives the corresponding STORAGE-orbit DO coils
 * via FC05.  Only writes when the desired coil state actually changes
 * (`s_last_driven_*` cache), so an idle system generates zero Modbus
 * traffic on top of the FC03 sensor poll.
 *
 * For 0.A.15 the "desired" state is purely the operator override:
 *   `desired = (remote_off.state[eq_index] == REMOTE_MANUAL)`.
 * Real automatic equipment control (PID, scheduler, cross-couple)
 * lands in 0.A.16+; this task is the wiring that those higher-level
 * deciders will publish into.
 *
 * Priority below the safemode watchdog and orbit tick handlers because
 * a stuck FC05 socket call must never block them. Stack is generous
 * because the loop calls into lwIP socket APIs. */
#define EQ_OUT_TASK_PRI    (configMAX_PRIORITIES - 6)
#define EQ_OUT_TASK_STACK  (4096U / sizeof(configSTACK_DEPTH_TYPE))
static StackType_t  gEqOutStack[EQ_OUT_TASK_STACK] __attribute__((aligned(32)));
static StaticTask_t gEqOutObj;

/* Equipment-AO sync task. Mirrors `equipment_output_sync_task` but
 * for analog outputs: walks `Settings.AoEquip[slot][channel]` and
 * pushes the corresponding `PwmChannel[].Output` (converted to 0..100
 * percent) to each orbit's HR0/HR1 via FC06. CONTROLLER-only,
 * spawned alongside `equipment_output_sync_task`.
 *
 * Cadence is slower (1 s) than the digital path (250 ms) because
 * 4-20 mA actuators (door dampers, refrig EEVs) physically can't
 * track sub-second steps and the underlying PWM values themselves
 * are PID-smoothed. Same generous stack for lwIP socket calls. */
#define EQ_AO_TASK_PRI     (configMAX_PRIORITIES - 6)
#define EQ_AO_TASK_STACK   (4096U / sizeof(configSTACK_DEPTH_TYPE))
#define EQ_AO_TICK_MS      1000U
static StackType_t  gEqAoStack[EQ_AO_TASK_STACK] __attribute__((aligned(32)));
static StaticTask_t gEqAoObj;

/* AS2 equipment-engine tick. Calls into lp_engine_shim.c which mirrors
 * LpSettings + sensor sample into the legacy `Settings` shadow + builds
 * IoBoard switch state, then runs SetSystemState() / SetMode() to
 * compute CurrentMode. CONTROLLER-only. Big stack because the engine
 * recurses through Mode*() into PWM / failure / warning paths. */
#define LP_ENGINE_TASK_PRI    (configMAX_PRIORITIES - 7)
#define LP_ENGINE_TASK_STACK  (8192U / sizeof(configSTACK_DEPTH_TYPE))
#define LP_ENGINE_TICK_MS     1000U
static StackType_t  gLpEngineStack[LP_ENGINE_TASK_STACK] __attribute__((aligned(32)));
static StaticTask_t gLpEngineObj;
extern void lp_engine_init(void);
extern void lp_engine_tick(void);
extern unsigned char lp_engine_get_current_mode(void);
extern int lp_pidlog_drain_one(uint32_t *out_epoch_sec,
                               uint32_t *out_loop_index,
                               float *out_p, float *out_i, float *out_d,
                               int32_t *out_output, float *out_error,
                               uint32_t *out_sequence);
static void lp_engine_task(void *args);

/* Independent dual-core watchdog (R5FSS1_0) — see
 * docs/lp-am2434-watchdog-design.md. Producers ping per-subsystem
 * alive bits; client task aggregates + writes the heartbeat struct
 * in MSRAM that the watchdog core polls. */
#include "lp_watchdog_ipc.h"
extern void LpWatchdog_Ping(uint32_t alive_bit);
extern void *LpWatchdogClient_Start(void);  /* returns TaskHandle_t */


/* ---- Stub: debug_printf (required by nova_protocol.c) ---- */
void debug_printf(const char *fmt, ...)
{
    /* DIAGNOSTIC: route NovaProto COBS/CRC error logs to UART0 via
     * DebugP_log so we can see them on COM4. NovaProto only calls this
     * on error paths (CRC mismatch, COBS decode fail, hex dump), so it
     * won't flood. ` __VA_ARGS__` not available here because we got
     * `fmt` and a `va_list`-shaped tail — pass through with vprintf-
     * style. DebugP_log doesn't have a `v` variant, so use snprintf
     * into a stack buffer. */
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        DebugP_log("%s", buf);
    }
}

/* ---- Polled UART TX (bypasses SDK ISR to avoid TX/RX stall) ---- */
static void bridge_uart_tx(const uint8_t *data, size_t len)
{
    if (s_uart2_base == NULL || len == 0) return;

    for (size_t i = 0; i < len; i++) {
        while ((s_uart2_base[UART_REG_LSR / 4] & UART_LSR_THRE) == 0) {}
        s_uart2_base[UART_REG_THR / 4] = data[i];
    }
}

/* ---- Hand-rolled varint helper for the RX decoder ----
 * Reads a base-128 varint into *out, returns the number of bytes
 * consumed, or 0 on overflow / truncation. Mirrors the encoder we use
 * in build_*_envelope() helpers — no nanopb runtime on the LP yet. */
/* Forward decl; body defined alongside the envelope encoders below.
 * Needed early because bridge_rx_callback uses it to build Ack envelopes
 * for bridge sendCommand() callers. */
static size_t pb_encode_varint(uint8_t *buf, uint32_t value);

/* Pending Ack queue — populated by bridge_rx_callback, drained by
 * bridge_uart_task on its next 100 ms iteration. Single-entry; the
 * bridge sends commands serially (sendCommand awaits each Ack before
 * issuing the next), so a deeper queue is unnecessary. */
static volatile bool     s_pending_ack_ready = false;
static volatile uint32_t s_pending_ack_seq   = 0;

/* Set by bridge_rx_callback when an EquipmentCmd flips a remote_off
 * slot.  The main task notices and pushes an extra EquipmentStatus
 * envelope on its next loop iteration so the UI repaints quickly
 * without waiting for the next 2 s cadence tick. */
volatile bool s_equip_status_dirty = false;

static size_t pb_decode_varint(const uint8_t *buf, size_t len, uint32_t *out)
{
    uint32_t v = 0;
    uint32_t shift = 0;
    for (size_t i = 0; i < len && i < 5U; i++) {
        v |= ((uint32_t)(buf[i] & 0x7F)) << shift;
        if ((buf[i] & 0x80) == 0U) {
            *out = v;
            return i + 1U;
        }
        shift += 7U;
    }
    return 0U;
}

/* Skip a single proto field of any wire type. Returns bytes consumed
 * starting at the byte AFTER the tag, or 0 on malformed input. */
static size_t pb_skip_field(uint8_t wire, const uint8_t *buf, size_t len)
{
    switch (wire) {
        case 0: { /* varint */
            uint32_t dummy;
            return pb_decode_varint(buf, len, &dummy);
        }
        case 1: /* fixed64 */
            return (len >= 8U) ? 8U : 0U;
        case 2: { /* length-delimited */
            uint32_t sublen;
            size_t n = pb_decode_varint(buf, len, &sublen);
            if (n == 0U || n + sublen > len) return 0U;
            return n + sublen;
        }
        case 5: /* fixed32 */
            return (len >= 4U) ? 4U : 0U;
        default:
            return 0U;
    }
}

/* ---- RX callback: dispatch incoming envelopes ----
 *
 * Walks the Envelope wire bytes looking for downlink command tags.
 * Currently handled:
 *   90  SettingsUpdate         (DateTimeUpdate, IoNameUpdate)
 *   91  SettingsBlobRestore    (Phase D blob replay)
 *   92  PanelDefaultsRestore   (panel-default snapshot replay)
 *   82  SystemCmd              (CMD_CLEAR_DIAG)
 *   122 OrbitRoleAssign        (persisted via LpSettings field 78)
 *   123 AoEquipAssign          (persisted via LpSettings field 80)
 *   125 TritonRegWrite         (Modbus passthrough to a remote orbit)
 *   126 OrbitRegWrite          (role-agnostic FC06/FC16 to a remote orbit)
 *   127 VfdConfig              (VFD Modbus TCP endpoint + scan config)
 *   129 VfdRawWrite            (FC06/FC16 to a VFD unit; bridge composes CW)
 *   130..136 Fw{Install,Fleet}* (forwarded to NovaOtaBroker_OnEnvelope)
 *
 * Future downlink commands extend the same switch — keep additions
 * tag-ordered. */
/* Single shared scratch buffer for LpSettings_Serialize() — bridge_rx_callback
 * runs single-threaded on the bridge UART RX path, so all save sites can
 * share one ~4 KB buffer instead of the compiler instantiating one per
 * switch-case branch (was burning ~180 KB of MSRAM, May 2026 bloat audit). */
static uint8_t save_blob[LP_SETTINGS_MAX_BLOB_SIZE];

static void bridge_rx_callback(const uint8_t *payload, size_t len)
{
    /* envelope_seq is captured from envelope field 2 as we walk; the
     * bridge tags every command with a sequence number and waits for an
     * Ack referencing it (3-second timeout in
     * novaSerialBridge.ts::sendCommand). Field 2 is encoded BEFORE any
     * other top-level field by the bridge's PbEncoder, so by the time
     * we see SettingsUpdate (field 90) we already have it. */
    uint32_t envelope_seq = 0;
    bool envelope_seq_valid = false;
    bool needs_ack = false;

    size_t pos = 0;
    while (pos < len) {
        uint32_t tag_word;
        size_t n = pb_decode_varint(payload + pos, len - pos, &tag_word);
        if (n == 0U) goto done;
        pos += n;
        const uint32_t field = tag_word >> 3;
        const uint8_t  wire  = (uint8_t)(tag_word & 0x07U);

        if (field == 2U && wire == 0U) {
            /* Envelope.seq — capture for the Ack response. */
            size_t sn = pb_decode_varint(payload + pos, len - pos, &envelope_seq);
            if (sn == 0U) goto done;
            envelope_seq_valid = true;
            pos += sn;
            continue;
        }

        if (field == 122U && wire == 2U) {
            /* OrbitRoleAssign — operator picked a role for an orbit
             * slot from the Level 2 IO Config page.  We persist the
             * pick to OSPI via LpSettings (top-level field 78), and
             * the next OrbitStatus push (field 10 of OrbitBoardStatus,
             * built by build_orbit_board) carries the new role back to
             * the bridge.  No round-trip with the remote orbit board
             * is required — the orbit firmware reads its own role
             * from lp_device_config at boot; this table is the
             * CONTROLLER's view of "operator-assigned role per slot",
             * which is what the UI cares about. */
            uint32_t inner_len;
            size_t ln = pb_decode_varint(payload + pos, len - pos, &inner_len);
            if (ln == 0U || pos + ln + inner_len > len) goto done;
            pos += ln;

            const uint8_t *inner = payload + pos;
            size_t ipos = 0;
            uint32_t slot = 0, role = 0, zone_id = 0, refrig_stage = 0;
            int32_t  legacy_slot = -1;
            while (ipos < inner_len) {
                uint32_t itag;
                size_t in = pb_decode_varint(inner + ipos, inner_len - ipos, &itag);
                if (in == 0U) break;
                ipos += in;
                const uint32_t ifield = itag >> 3;
                const uint8_t  iwire  = (uint8_t)(itag & 0x07U);
                if (iwire == 0U) {
                    uint32_t v;
                    size_t vn = pb_decode_varint(inner + ipos, inner_len - ipos, &v);
                    if (vn == 0U) break;
                    ipos += vn;
                    if      (ifield == 1U) slot         = v;
                    else if (ifield == 2U) role         = v;
                    else if (ifield == 3U) zone_id      = v;
                    else if (ifield == 4U) legacy_slot  = (int32_t)v; /* zigzag would be sint32 */
                    else if (ifield == 5U) refrig_stage = v;
                } else {
                    size_t sk = pb_skip_field(iwire, inner + ipos, inner_len - ipos);
                    if (sk == 0U) break;
                    ipos += sk;
                }
            }
            pos += inner_len;

            /* Range-check before touching the table. */
            if (slot >= LP_ORBIT_ROLE_MAX) {
                DebugP_log("[RX] OrbitRoleAssign slot=%u OUT OF RANGE\r\n",
                           (unsigned)slot);
                needs_ack = true;
                continue;
            }
            if (role > 3U) {
                DebugP_log("[RX] OrbitRoleAssign role=%u OUT OF RANGE\r\n",
                           (unsigned)role);
                needs_ack = true;
                continue;
            }

            bool changed = LpSettings_SetOrbitRole(slot, role,
                                                   zone_id, legacy_slot,
                                                   refrig_stage);
            if (changed) {
                size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                if (blen > 0U) {
                    LpNssResult sr = LpSettings_Save(save_blob, blen);
                    DebugP_log("[RX] OrbitRoleAssign slot=%u role=%u "
                               "zone=%u legacy=%d stage=%u saved rc=%d\r\n",
                               (unsigned)slot, (unsigned)role,
                               (unsigned)zone_id, (int)legacy_slot,
                               (unsigned)refrig_stage, (int)sr);
                } else {
                    DebugP_log("[RX] OrbitRoleAssign slot=%u serialize FAILED\r\n",
                               (unsigned)slot);
                }
            } else {
                DebugP_log("[RX] OrbitRoleAssign slot=%u role=%u "
                           "(no change, skip save)\r\n",
                           (unsigned)slot, (unsigned)role);
            }
            needs_ack = true;
            continue;
        }

        if (field == 123U && wire == 2U) {
            /* AoEquipAssign — operator picked an equipment program for
             * one orbit AO from the Level 2 4-20 mA Output Setup page.
             * Wire (orbit.proto AoEquipAssign):
             *   field 1 uint32 slot     (0..15)
             *   field 2 uint32 channel  (0..1)
             *   field 3 uint32 equip    (ao_equip_t value)
             *
             * We persist into LpSettings.ao_equip (top-level save-blob
             * field 80). The next OrbitStatus push (field 15 of
             * OrbitBoardStatus, built by build_orbit_board) carries
             * the table back to the bridge so the UI re-syncs even
             * after a refresh / reconnect. */
            uint32_t inner_len;
            size_t ln = pb_decode_varint(payload + pos, len - pos, &inner_len);
            if (ln == 0U || pos + ln + inner_len > len) goto done;
            pos += ln;

            const uint8_t *inner = payload + pos;
            size_t ipos = 0;
            uint32_t slot = 0xFFFFFFFFu, channel = 0xFFFFFFFFu, equip = 0;
            bool have_slot = false, have_channel = false, have_equip = false;
            while (ipos < inner_len) {
                uint32_t itag;
                size_t in = pb_decode_varint(inner + ipos, inner_len - ipos, &itag);
                if (in == 0U) break;
                ipos += in;
                const uint32_t ifield = itag >> 3;
                const uint8_t  iwire  = (uint8_t)(itag & 0x07U);
                if (iwire == 0U) {
                    uint32_t v;
                    size_t vn = pb_decode_varint(inner + ipos, inner_len - ipos, &v);
                    if (vn == 0U) break;
                    ipos += vn;
                    if      (ifield == 1U) { slot    = v; have_slot    = true; }
                    else if (ifield == 2U) { channel = v; have_channel = true; }
                    else if (ifield == 3U) { equip   = v; have_equip   = true; }
                } else {
                    size_t sk = pb_skip_field(iwire, inner + ipos, inner_len - ipos);
                    if (sk == 0U) break;
                    ipos += sk;
                }
            }
            pos += inner_len;

            if (!have_slot || !have_channel || !have_equip) {
                DebugP_log("[RX] AoEquipAssign missing field(s) "
                           "(slot=%d channel=%d equip=%d)\r\n",
                           (int)have_slot, (int)have_channel, (int)have_equip);
                needs_ack = true;
                continue;
            }
            if (slot >= LP_AO_EQUIP_SLOT_MAX) {
                DebugP_log("[RX] AoEquipAssign slot=%u OUT OF RANGE\r\n",
                           (unsigned)slot);
                needs_ack = true;
                continue;
            }
            if (channel >= LP_AO_EQUIP_CH_MAX) {
                DebugP_log("[RX] AoEquipAssign channel=%u OUT OF RANGE\r\n",
                           (unsigned)channel);
                needs_ack = true;
                continue;
            }

            bool changed = LpSettings_SetAoEquip(slot, channel, equip);
            if (changed) {
                size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                if (blen > 0U) {
                    LpNssResult sr = LpSettings_Save(save_blob, blen);
                    DebugP_log("[RX] AoEquipAssign slot=%u ch=%u equip=%u "
                               "saved rc=%d\r\n",
                               (unsigned)slot, (unsigned)channel,
                               (unsigned)equip, (int)sr);
                } else {
                    DebugP_log("[RX] AoEquipAssign slot=%u ch=%u "
                               "serialize FAILED\r\n",
                               (unsigned)slot, (unsigned)channel);
                }
            } else {
                DebugP_log("[RX] AoEquipAssign slot=%u ch=%u equip=%u "
                           "(no change, skip save)\r\n",
                           (unsigned)slot, (unsigned)channel,
                           (unsigned)equip);
            }
            needs_ack = true;
            continue;
        }

        if (field == 125U && wire == 2U) {
            /* TritonRegWrite — decode the length-delimited inner. */
            uint32_t inner_len;
            size_t ln = pb_decode_varint(payload + pos, len - pos, &inner_len);
            if (ln == 0U || pos + ln + inner_len > len) return;
            pos += ln;

            const uint8_t *inner = payload + pos;
            size_t ipos = 0;
            uint32_t slot = 0, addr = 0, value = 0;
            bool have_slot = false, have_addr = false, have_value = false;
            while (ipos < inner_len) {
                uint32_t itag;
                size_t in = pb_decode_varint(inner + ipos, inner_len - ipos, &itag);
                if (in == 0U) break;
                ipos += in;
                const uint32_t ifield = itag >> 3;
                const uint8_t  iwire  = (uint8_t)(itag & 0x07U);
                if (iwire == 0U) {
                    uint32_t v;
                    size_t vn = pb_decode_varint(inner + ipos, inner_len - ipos, &v);
                    if (vn == 0U) break;
                    ipos += vn;
                    if      (ifield == 1U) { slot  = v; have_slot  = true; }
                    else if (ifield == 2U) { addr  = v; have_addr  = true; }
                    else if (ifield == 3U) { value = v; have_value = true; }
                } else {
                    size_t sk = pb_skip_field(iwire, inner + ipos, inner_len - ipos);
                    if (sk == 0U) break;
                    ipos += sk;
                }
            }
            pos += inner_len;

            if (have_slot && have_addr && have_value && slot < 256U) {
                int rc = OrbitClient_WriteHoldingRegister(
                    (uint8_t)slot, (uint16_t)addr, (uint16_t)value);
                DebugP_log("[RX] TritonRegWrite slot=%u addr=%u val=%u rc=%d\r\n",
                           (unsigned)slot, (unsigned)addr,
                           (unsigned)value, rc);
            } else {
                DebugP_log("[RX] TritonRegWrite missing field(s) "
                           "(slot=%u addr=%u value=%u)\r\n",
                           have_slot, have_addr, have_value);
            }
            needs_ack = true;
            continue;
        }

        if (field == 126U && wire == 2U) {
            /* OrbitRegWrite — role-agnostic Modbus HR write. Phase 4b
             * Sub-phase 2 (2026-06-01). Dispatches to FC06 for one
             * value or FC16 for a multi-value block.
             *
             * Inner layout: { slot:varint=1, addr:varint=2,
             *                 values: repeated uint32 (packed OR
             *                 unpacked — accept both per proto3 spec). } */
            uint32_t inner_len;
            size_t ln = pb_decode_varint(payload + pos, len - pos, &inner_len);
            if (ln == 0U || pos + ln + inner_len > len) return;
            pos += ln;

            const uint8_t *inner = payload + pos;
            size_t ipos = 0;
            uint32_t slot = 0, addr = 0;
            bool have_slot = false, have_addr = false;
            /* Worst-case 123 values (FC16 spec cap); accept up to that. */
            uint16_t values[123];
            uint16_t count = 0;
            while (ipos < inner_len) {
                uint32_t itag;
                size_t in = pb_decode_varint(inner + ipos, inner_len - ipos, &itag);
                if (in == 0U) break;
                ipos += in;
                const uint32_t ifield = itag >> 3;
                const uint8_t  iwire  = (uint8_t)(itag & 0x07U);
                if (iwire == 0U) {
                    uint32_t v;
                    size_t vn = pb_decode_varint(inner + ipos, inner_len - ipos, &v);
                    if (vn == 0U) break;
                    ipos += vn;
                    if      (ifield == 1U) { slot = v; have_slot = true; }
                    else if (ifield == 2U) { addr = v; have_addr = true; }
                    else if (ifield == 3U) {
                        /* Unpacked repeated — one varint per tag. */
                        if (count < (uint16_t)(sizeof(values) / sizeof(values[0]))) {
                            values[count++] = (uint16_t)v;
                        }
                    }
                } else if (iwire == 2U && ifield == 3U) {
                    /* Packed repeated — concatenated varints inside one LD. */
                    uint32_t plen;
                    size_t pn = pb_decode_varint(inner + ipos, inner_len - ipos, &plen);
                    if (pn == 0U) break;
                    ipos += pn;
                    if (ipos + plen > inner_len) break;
                    size_t ppos = 0;
                    while (ppos < plen) {
                        uint32_t pv;
                        size_t pvn = pb_decode_varint(inner + ipos + ppos,
                                                      plen - ppos, &pv);
                        if (pvn == 0U) break;
                        ppos += pvn;
                        if (count < (uint16_t)(sizeof(values) / sizeof(values[0]))) {
                            values[count++] = (uint16_t)pv;
                        }
                    }
                    ipos += plen;
                } else {
                    size_t sk = pb_skip_field(iwire, inner + ipos, inner_len - ipos);
                    if (sk == 0U) break;
                    ipos += sk;
                }
            }
            pos += inner_len;

            if (have_slot && have_addr && count > 0U && slot < 256U) {
                int rc;
                if (count == 1U) {
                    rc = OrbitClient_WriteHoldingRegister(
                        (uint8_t)slot, (uint16_t)addr, values[0]);
                } else {
                    rc = OrbitClient_WriteHoldingRegisters(
                        (uint8_t)slot, (uint16_t)addr, values, count);
                }
                DebugP_log("[RX] OrbitRegWrite slot=%u addr=%u count=%u rc=%d\r\n",
                           (unsigned)slot, (unsigned)addr,
                           (unsigned)count, rc);
            } else {
                DebugP_log("[RX] OrbitRegWrite missing/empty field(s) "
                           "(slot=%d addr=%d count=%u)\r\n",
                           have_slot, have_addr, count);
            }
            needs_ack = true;
            continue;
        }

        if (field == 127U && wire == 2U) {
            /* VfdConfig — bridge tells Nova which VFD endpoint to poll.
             * Phase 4b Sub-phase 3 (2026-06-01). Inner shape:
             *   { vfd_host_ipv4:varint=1, vfd_port:varint=2,
             *     max_scan_unit_id:varint=3, poll_interval_ms:varint=4 } */
            uint32_t inner_len;
            size_t ln = pb_decode_varint(payload + pos, len - pos, &inner_len);
            if (ln == 0U || pos + ln + inner_len > len) return;
            pos += ln;

            const uint8_t *inner = payload + pos;
            size_t ipos = 0;
            uint32_t host = 0, port = 0, max_scan = 0, poll_ms = 0;
            while (ipos < inner_len) {
                uint32_t itag;
                size_t in = pb_decode_varint(inner + ipos, inner_len - ipos, &itag);
                if (in == 0U) break;
                ipos += in;
                const uint32_t ifield = itag >> 3;
                const uint8_t  iwire  = (uint8_t)(itag & 0x07U);
                if (iwire == 0U) {
                    uint32_t v;
                    size_t vn = pb_decode_varint(inner + ipos, inner_len - ipos, &v);
                    if (vn == 0U) break;
                    ipos += vn;
                    if      (ifield == 1U) host     = v;
                    else if (ifield == 2U) port     = v;
                    else if (ifield == 3U) max_scan = v;
                    else if (ifield == 4U) poll_ms  = v;
                } else {
                    size_t sk = pb_skip_field(iwire, inner + ipos, inner_len - ipos);
                    if (sk == 0U) break;
                    ipos += sk;
                }
            }
            pos += inner_len;

            if (LpSettings_SetVfdConfig(host,
                                        (uint16_t)(port      & 0xFFFFU),
                                        (uint8_t) (max_scan  & 0xFFU),
                                        (uint16_t)(poll_ms   & 0xFFFFU))) {
                NovaVfdClient_ConfigChanged();
            }
            DebugP_log("[RX] VfdConfig host=0x%08X port=%u scan=%u poll=%u\r\n",
                       (unsigned)host, (unsigned)port,
                       (unsigned)max_scan, (unsigned)poll_ms);
            needs_ack = true;
            continue;
        }

        if (field == 129U && wire == 2U) {
            /* VfdRawWrite — bridge-issued FC06 / FC16 to a VFD unit.
             * Phase 4b Sub-phase 3 (2026-06-01). Inner shape mirrors
             * `OrbitRegWrite` exactly:
             *   { unit_id:varint=1, addr:varint=2,
             *     values: repeated uint32 (packed OR unpacked). } */
            uint32_t inner_len;
            size_t ln = pb_decode_varint(payload + pos, len - pos, &inner_len);
            if (ln == 0U || pos + ln + inner_len > len) return;
            pos += ln;

            const uint8_t *inner = payload + pos;
            size_t ipos = 0;
            uint32_t unit_id = 0, addr = 0;
            bool have_unit = false, have_addr = false;
            uint16_t values[123];
            uint16_t count = 0;
            while (ipos < inner_len) {
                uint32_t itag;
                size_t in = pb_decode_varint(inner + ipos, inner_len - ipos, &itag);
                if (in == 0U) break;
                ipos += in;
                const uint32_t ifield = itag >> 3;
                const uint8_t  iwire  = (uint8_t)(itag & 0x07U);
                if (iwire == 0U) {
                    uint32_t v;
                    size_t vn = pb_decode_varint(inner + ipos, inner_len - ipos, &v);
                    if (vn == 0U) break;
                    ipos += vn;
                    if      (ifield == 1U) { unit_id = v; have_unit = true; }
                    else if (ifield == 2U) { addr    = v; have_addr = true; }
                    else if (ifield == 3U) {
                        if (count < (uint16_t)(sizeof(values) / sizeof(values[0]))) {
                            values[count++] = (uint16_t)v;
                        }
                    }
                } else if (iwire == 2U && ifield == 3U) {
                    uint32_t plen;
                    size_t pn = pb_decode_varint(inner + ipos, inner_len - ipos, &plen);
                    if (pn == 0U) break;
                    ipos += pn;
                    if (ipos + plen > inner_len) break;
                    size_t ppos = 0;
                    while (ppos < plen) {
                        uint32_t pv;
                        size_t pvn = pb_decode_varint(inner + ipos + ppos,
                                                      plen - ppos, &pv);
                        if (pvn == 0U) break;
                        ppos += pvn;
                        if (count < (uint16_t)(sizeof(values) / sizeof(values[0]))) {
                            values[count++] = (uint16_t)pv;
                        }
                    }
                    ipos += plen;
                } else {
                    size_t sk = pb_skip_field(iwire, inner + ipos, inner_len - ipos);
                    if (sk == 0U) break;
                    ipos += sk;
                }
            }
            pos += inner_len;

            if (have_unit && have_addr && count > 0U && unit_id < 256U) {
                int rc = NovaVfdClient_WriteRegisters(
                    (uint8_t)unit_id, (uint16_t)addr, values, count);
                DebugP_log("[RX] VfdRawWrite unit=%u addr=%u count=%u rc=%d\r\n",
                           (unsigned)unit_id, (unsigned)addr,
                           (unsigned)count, rc);
            } else {
                DebugP_log("[RX] VfdRawWrite missing/empty field(s) "
                           "(unit=%d addr=%d count=%u)\r\n",
                           have_unit, have_addr, count);
            }
            needs_ack = true;
            continue;
        }

        /* OTA broker — Pi5 ↔ Nova install-orchestration (Phase 3 scaffold).
         *
         * Envelope tags 130..136 are all length-delimited Pi5 → Nova
         * messages handled by nova_ota_broker. Decoders + state machine
         * + outbound progress/result emitters all live in
         * nova_ota_broker.c. Phase 3 stubs every leaf op and emits
         * FwInstallProgress(state=FAILED, error_code=99) so Pi5's
         * firmwareInstaller.ts surfaces the gap immediately.
         *
         * Mirrors the OrbitRoleAssign / AoEquipAssign / TritonRegWrite
         * dispatch shape: pb_decode_varint(inner_len) → bounds-check →
         * forward (field, inner_bytes, inner_len, envelope_seq). The
         * broker drives an ack itself via FwInstallProgress, so
         * needs_ack is NOT raised — these tags don't use the
         * sendCommand/Ack handshake (the bridge listens for
         * FwInstallProgress instead). */
        if (wire == 2U && field >= 130U && field <= 136U) {
            uint32_t inner_len;
            size_t ln = pb_decode_varint(payload + pos, len - pos, &inner_len);
            if (ln == 0U || pos + ln + inner_len > len) goto done;
            pos += ln;
            NovaOtaBroker_OnEnvelope(field, payload + pos,
                                     (size_t)inner_len, envelope_seq);
            pos += inner_len;
            continue;
        }

        if (field == 90U && wire == 2U) {
            /* SettingsUpdate (envelope field 90, length-delimited).
             * Walk the inner SettingsUpdate body looking for sub-fields
             * we know how to apply. Today only field 26 (DateTimeUpdate)
             * is implemented; future settings dispatch (basic setup,
             * plenum, etc.) extends this same loop in tag-numeric order
             * once the OSPI persistence layer lands (Phase D / E of the
             * LP envelope-emission plan).
             *
             * Source: proto/agristar/settings.proto :: SettingsUpdate
             *         constellation-ui/server/src/index.ts ::
             *           buildSettingsUpdate / "RTC sync" emitter
             */
            uint32_t su_len;
            size_t ln = pb_decode_varint(payload + pos, len - pos, &su_len);
            if (ln == 0U || pos + ln + su_len > len) return;
            pos += ln;

            const uint8_t *su = payload + pos;
            size_t spos = 0;
            while (spos < su_len) {
                uint32_t stag;
                size_t sn = pb_decode_varint(su + spos, su_len - spos, &stag);
                if (sn == 0U) break;
                spos += sn;
                const uint32_t sfield = stag >> 3;
                const uint8_t  swire  = (uint8_t)(stag & 0x07U);

                if (sfield == 26U && swire == 2U) {
                    /* DateTimeUpdate { 1=date_str, 2=time_str, 3=am_pm } */
                    uint32_t dt_len;
                    size_t dn = pb_decode_varint(su + spos, su_len - spos, &dt_len);
                    if (dn == 0U || spos + dn + dt_len > su_len) break;
                    spos += dn;

                    const uint8_t *dt = su + spos;
                    size_t dpos = 0;
                    char   date_str[16] = {0};
                    char   time_str[16] = {0};
                    uint32_t am_pm = 0;
                    bool have_date = false, have_time = false;

                    while (dpos < dt_len) {
                        uint32_t dtag;
                        size_t dnn = pb_decode_varint(dt + dpos, dt_len - dpos, &dtag);
                        if (dnn == 0U) break;
                        dpos += dnn;
                        const uint32_t dfield = dtag >> 3;
                        const uint8_t  dwire  = (uint8_t)(dtag & 0x07U);

                        if (dwire == 2U && (dfield == 1U || dfield == 2U)) {
                            uint32_t slen;
                            size_t sln = pb_decode_varint(dt + dpos, dt_len - dpos, &slen);
                            if (sln == 0U || dpos + sln + slen > dt_len) break;
                            dpos += sln;
                            char *dst = (dfield == 1U) ? date_str : time_str;
                            size_t cp = (slen < sizeof(date_str) - 1U) ? slen : sizeof(date_str) - 1U;
                            memcpy(dst, dt + dpos, cp);
                            dst[cp] = '\0';
                            if (dfield == 1U) have_date = true; else have_time = true;
                            dpos += slen;
                        } else if (dwire == 0U && dfield == 3U) {
                            uint32_t v;
                            size_t vn = pb_decode_varint(dt + dpos, dt_len - dpos, &v);
                            if (vn == 0U) break;
                            am_pm = v;
                            dpos += vn;
                        } else {
                            size_t sk = pb_skip_field(dwire, dt + dpos, dt_len - dpos);
                            if (sk == 0U) break;
                            dpos += sk;
                        }
                    }
                    spos += dt_len;

                    if (have_date && have_time) {
                        bool ok = LpRtc_SetFromStrings(date_str, time_str, am_pm);
                        DebugP_log("[RX] DateTimeUpdate %s %s %s rc=%d\r\n",
                                   date_str, time_str, am_pm ? "PM" : "AM",
                                   ok ? 1 : 0);
                    }
                    continue;
                }

                if (sfield == 32U && swire == 2U) {
                    /* BasicSetupUpdate (settings.proto field 32). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyBasicSetup(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] BasicSetupUpdate (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 1U && swire == 2U) {
                    /* PlenumSettings (settings.proto field 1). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyPlenum(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] PlenumSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 2U && swire == 2U) {
                    /* FanSpeedSettings (settings.proto field 2). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyFanSpeed(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] FanSpeedSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 3U && swire == 2U) {
                    /* FanBoostSettings (settings.proto field 3). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyFanBoost(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] FanBoostSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 4U && swire == 2U) {
                    /* RampRateSettings (settings.proto field 4). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyRampRate(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] RampRateSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 5U && swire == 2U) {
                    /* RefrigSettings (settings.proto field 5).
                     * First page with nested repeated `stages`. */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyRefrig(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] RefrigSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 6U && swire == 2U) {
                    /* BurnerSettings (settings.proto field 6). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyBurner(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] BurnerSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 7U && swire == 2U) {
                    /* Co2Settings (settings.proto field 7). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyCo2(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] Co2Settings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 8U && swire == 2U) {
                    /* CureSettings (settings.proto field 8). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyCure(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] CureSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 9U && swire == 2U) {
                    /* ClimacellSettings (settings.proto field 9). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyClimacell(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] ClimacellSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 10U && swire == 2U) {
                    /* ClimacellTimes (settings.proto field 10).
                     * Packed-repeated uint32 hourly schedule. */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyClimacellTimes(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] ClimacellTimes (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 11U && swire == 2U) {
                    /* HumidCtrlSettings (settings.proto field 11).
                     * Splice-by-index: UI sends one entry at a time. */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyHumidCtrl(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] HumidCtrlSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 12U && swire == 2U) {
                    /* OutsideAirSettings (settings.proto field 12). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyOutsideAir(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] OutsideAirSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 13U && swire == 2U) {
                    /* MiscSettings (settings.proto field 13). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyMisc(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] MiscSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 14U && swire == 2U) {
                    /* FailureSettings (settings.proto field 14). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyFailure(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] FailureSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 15U && swire == 2U) {
                    /* FailureSettings2 (settings.proto field 15). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyFailure2(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] FailureSettings2 (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 16U && swire == 2U) {
                    /* TempAlarmSettings (settings.proto field 16). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyTempAlarm(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] TempAlarmSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 19U && swire == 2U) {
                    /* DoorSettings (settings.proto field 19). Note SU
                     * sub-field is 19 even though envelope tag is 56. */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyDoor(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] DoorSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 17U && swire == 2U) {
                    /* CureLimitSettings (settings.proto field 17). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyCureLimit(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] CureLimitSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 22U && swire == 2U) {
                    /* UserLogSettings (settings.proto field 22). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyUserLog(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] UserLogSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 23U && swire == 2U) {
                    /* PidSettings (settings.proto field 23). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyPid(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] PidSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 25U && swire == 2U) {
                    /* MasterSlaveSettings (settings.proto field 25). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyMasterSlave(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] MasterSlaveSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 33U && swire == 2U) {
                    /* HttpPortSettings (settings.proto field 33). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyHttpPort(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] HttpPortSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 34U && swire == 2U) {
                    /* PublicAddressSettings (settings.proto field 34). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyPublicAddress(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] PublicAddressSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 36U && swire == 2U) {
                    /* SysModeSettings (settings.proto field 36). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplySysMode(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] SysModeSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 37U && swire == 2U) {
                    /* PidLogSettings (settings.proto field 37). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyPidLog(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] PidLogSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 31U && swire == 2U) {
                    /* ServiceInfoUpdate (settings.proto field 31). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyServiceInfo(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] ServiceInfoUpdate (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 27U && swire == 2U) {
                    /* EmailSettings (settings.proto field 27). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyEmail(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] EmailSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 24U && swire == 2U) {
                    /* GraphFavoriteSettings (settings.proto field 24). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyGraphFavorites(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] GraphFavoriteSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 28U && swire == 2U) {
                    /* AlertSettings (settings.proto field 28). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyAlert(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] AlertSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 35U && swire == 2U) {
                    /* BayNameSettings (settings.proto field 35). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyBayName(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] BayNameSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 20U && swire == 2U) {
                    /* LoadMonitorSettings (settings.proto field 20). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyLoadMonitor(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] LoadMonitorSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 18U && swire == 2U) {
                    /* RuntimeSettings (settings.proto field 18). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyRuntime(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] RuntimeSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 21U && swire == 2U) {
                    /* AuxProgramSettings (settings.proto field 21). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyAuxProgram(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] AuxProgramSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 29U && swire == 2U) {
                    /* AnalogBoardSettings (settings.proto field 29). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyAnalogBoard(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] AnalogBoardSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 30U && swire == 2U) {
                    /* PwmChannelSettings (settings.proto field 30). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyPwmChannel(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] PwmChannelSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 38U && swire == 2U) {
                    /* AccountSettings (settings.proto field 38). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyAccount(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] AccountSettings (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 39U && swire == 2U) {
                    /* IoConfig (settings.proto field 39). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyIoConfig(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] IoConfig (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 40U && swire == 2U) {
                    /* IoNameUpdate (settings.proto field 40, see
                     * io.proto IoNameUpdate { index, new_name }). One
                     * rename event per envelope; firmware validates the
                     * target slot's renamable flag and rejects silently
                     * for non-renamable / OOB indices, then re-saves
                     * the full settings blob so the rename survives a
                     * power cycle. */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    bool changed = LpSettings_ApplyIoName(su + spos, bs_len);
                    spos += bs_len;
                    if (changed) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            LpNssResult sr = LpSettings_Save(save_blob, blen);
                            DebugP_log("[RX] IoNameUpdate (%u B) saved rc=%d\r\n",
                                       (unsigned)bs_len, (int)sr);
                        }
                    }
                    continue;
                }

                if (sfield == 41U && swire == 2U) {
                    /* RemoteOutsideAir (settings.proto field 41).
                     * TRANSIENT — bridge masterSlaveSync pushes this
                     * every 5 s while in slave mode; firmware caches in
                     * RAM and applies as an override on the storage
                     * orbit's outside-air HRs. NEVER persisted to OSPI
                     * (no LpSettings_Save call). */
                    uint32_t bs_len;
                    size_t bn = pb_decode_varint(su + spos, su_len - spos, &bs_len);
                    if (bn == 0U || spos + bn + bs_len > su_len) break;
                    spos += bn;
                    (void)LpRemoteOutside_ApplyProto(su + spos, bs_len);
                    spos += bs_len;
                    continue;
                }

                /* Unknown SettingsUpdate field — skip. */
                size_t sk = pb_skip_field(swire, su + spos, su_len - spos);
                if (sk == 0U) break;
                spos += sk;
            }
            pos += su_len;
            needs_ack = true;
            continue;
        }

        if (field == 91U && wire == 2U) {
            /* SettingsBlobRestore (envelope field 91, length-delimited).
             *
             * Phase D: bridge replays ~/.constellation/lp_settings.bin
             * verbatim on firmware-ready. The payload is exactly the
             * bytes LpSettings_TakeBlob produced (LpBankHeader || blob),
             * so we parse the on-wire LpBankHeader to recover image_size
             * and sequence, then hand the blob bytes to LpSettings_Restore
             * which re-CRCs and re-writes bank A. */
            uint32_t blob_total = 0;
            size_t   ln = pb_decode_varint(payload + pos, len - pos,
                                            &blob_total);
            if (ln == 0U) goto done;
            pos += ln;
            if (pos + blob_total > len) goto done;

            /* LpBankHeader layout (24B): magic[4] image_size[4] crc[4]
             * sequence[4] schema[8]. Field offsets must stay in sync
             * with lp_settings_store.c. */
            const uint8_t *raw = payload + pos;
            if (blob_total >= 24U) {
                uint32_t hdr_image_size;
                uint32_t hdr_sequence;
                memcpy(&hdr_image_size, raw + 4,  sizeof(uint32_t));
                memcpy(&hdr_sequence,   raw + 12, sizeof(uint32_t));
                if (24U + hdr_image_size <= blob_total) {
                    LpSettings_Restore(raw + 24U, hdr_image_size,
                                       hdr_sequence);
                }
            }
            pos += blob_total;
            needs_ack = true;
            continue;
        }

        if (field == 92U && wire == 2U) {
            /* PanelDefaultsRestore (envelope field 92, length-delimited).
             *
             * Bridge replays ~/.constellation/lp_panel_defaults.bin on
             * firmware-ready, parallel to the active-bank field-91
             * replay. Same wire format (LpBankHeader || blob) — we
             * parse the header, hand the blob bytes to
             * LpSettings_RestorePanelDefaults which re-CRCs and
             * commits to the panel bank without touching s_data or
             * the active A/B banks. */
            uint32_t blob_total = 0;
            size_t   ln = pb_decode_varint(payload + pos, len - pos,
                                            &blob_total);
            if (ln == 0U) goto done;
            pos += ln;
            if (pos + blob_total > len) goto done;

            const uint8_t *raw = payload + pos;
            if (blob_total >= 24U) {
                uint32_t hdr_image_size;
                uint32_t hdr_sequence;
                memcpy(&hdr_image_size, raw + 4,  sizeof(uint32_t));
                memcpy(&hdr_sequence,   raw + 12, sizeof(uint32_t));
                if (24U + hdr_image_size <= blob_total) {
                    LpSettings_RestorePanelDefaults(raw + 24U,
                                                     hdr_image_size,
                                                     hdr_sequence);
                }
            }
            pos += blob_total;
            needs_ack = true;
            continue;
        }

        if (field == 80U && wire == 2U) {
            /* EquipmentCmd (envelope field 80, length-delimited).
             *
             * Inner shape (equipment.proto :: EquipmentCmd):
             *   field 1 (varint) eq_index  — EQUIPMENT_IO ordinal
             *   field 2 (varint) new_state — RemoteOffState enum
             *
             * Bridge sends this on every Equipment Control button press
             * (Auto/Off/Manual click).  We update the in-RAM mirror,
             * persist iff anything actually changed, and trigger an
             * immediate EquipmentStatus push so the UI repaints quickly
             * (without waiting for the next 2 s cadence tick). */
            uint32_t inner_len;
            size_t ln = pb_decode_varint(payload + pos, len - pos, &inner_len);
            if (ln == 0U || pos + ln + inner_len > len) goto done;
            pos += ln;

            const uint8_t *inner = payload + pos;
            size_t ipos = 0;
            uint32_t eq_index = 0xFFFFFFFFU, new_state = 0;
            while (ipos < inner_len) {
                uint32_t itag;
                size_t in = pb_decode_varint(inner + ipos, inner_len - ipos, &itag);
                if (in == 0U) break;
                ipos += in;
                const uint32_t ifield = itag >> 3;
                const uint8_t  iwire  = (uint8_t)(itag & 0x07U);
                if (iwire == 0U) {
                    uint32_t v;
                    size_t vn = pb_decode_varint(inner + ipos, inner_len - ipos, &v);
                    if (vn == 0U) break;
                    ipos += vn;
                    if      (ifield == 1U) eq_index  = v;
                    else if (ifield == 2U) new_state = v;
                } else {
                    size_t sk = pb_skip_field(iwire, inner + ipos, inner_len - ipos);
                    if (sk == 0U) break;
                    ipos += sk;
                }
            }
            pos += inner_len;

            if (eq_index == 0xFFFFFFFFU) {
                DebugP_log("[RX] EquipmentCmd missing eq_index\r\n");
                needs_ack = true;
                continue;
            }
            bool changed = LpSettings_SetRemoteOff(eq_index, (uint8_t)new_state);
            if (changed) {
                size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                if (blen > 0U) {
                    LpNssResult sr = LpSettings_Save(save_blob, blen);
                    DebugP_log("[RX] EquipmentCmd eq=%u state=%u saved rc=%d\r\n",
                               (unsigned)eq_index, (unsigned)new_state, (int)sr);
                } else {
                    DebugP_log("[RX] EquipmentCmd eq=%u state=%u serialize FAILED\r\n",
                               (unsigned)eq_index, (unsigned)new_state);
                }
                /* Flag for the main task to push a fresh EquipmentStatus
                 * envelope on its next loop iteration (rather than wait
                 * for the 2 s cadence tick).  Defined in main.c near
                 * the other shared RX→TX flags. */
                extern volatile bool s_equip_status_dirty;
                s_equip_status_dirty = true;
            } else {
                DebugP_log("[RX] EquipmentCmd eq=%u state=%u (no change)\r\n",
                           (unsigned)eq_index, (unsigned)new_state);
            }
            needs_ack = true;
            continue;
        }

        if (field == 82U && wire == 2U) {
            /* SystemCmd (envelope field 82, length-delimited).
             *
             * Inner shape (alarms.proto :: SystemCmd):
             *   field 1 (varint) cmd_type   — SystemCmdType enum
             *   field 2 (varint) param      — command-specific scalar
             *   field 3 (string) str_param  — command-specific text
             *
             * Handled cmd_types:
             *   2 = CMD_SET_DEFAULT    — snapshot current settings as
             *                            the operator's panel default
             *   3 = CMD_PANEL_DEFAULT  — restore from saved panel default
             *   4 = CMD_FACTORY_DEFAULT — LpSettings_DataInit() + persist
             *   6 = CMD_CLEAR_DIAG     — clear refrig diagnostic latches
             *   8 = CMD_RESET_IO_CONFIG — reset only IO Config to defaults
             *
             * The periodic emitters in nova_main_task push updated settings
             * within the next 5 s window so we don't force-broadcast here. */
            uint32_t cmd_len;
            size_t   ln = pb_decode_varint(payload + pos, len - pos, &cmd_len);
            if (ln == 0U) goto done;
            pos += ln;
            if (pos + cmd_len > len) goto done;

            const uint8_t *cmd = payload + pos;
            size_t cpos = 0;
            uint32_t cmd_type = 0;
            uint32_t cmd_param = 0;  /* reserved — unused today */
            (void)cmd_param;
            while (cpos < cmd_len) {
                uint32_t ctag;
                size_t cn = pb_decode_varint(cmd + cpos, cmd_len - cpos, &ctag);
                if (cn == 0U) break;
                cpos += cn;
                const uint32_t cfield = ctag >> 3;
                const uint8_t  cwire  = (uint8_t)(ctag & 0x07U);
                if (cwire == 0U && cfield == 1U) {
                    size_t vn = pb_decode_varint(cmd + cpos, cmd_len - cpos, &cmd_type);
                    if (vn == 0U) break;
                    cpos += vn;
                } else if (cwire == 0U && cfield == 2U) {
                    size_t vn = pb_decode_varint(cmd + cpos, cmd_len - cpos, &cmd_param);
                    if (vn == 0U) break;
                    cpos += vn;
                } else {
                    size_t sk = pb_skip_field(cwire, cmd + cpos, cmd_len - cpos);
                    if (sk == 0U) break;
                    cpos += sk;
                }
            }
            pos += cmd_len;

            switch (cmd_type) {
            case 1: { /* CMD_CLEAR_ALARM — Level 1 alarm-monitor popup
                       * "Clear" button. Reset the engine's alarm latches
                       * so persistent FAIL conditions can re-evaluate;
                       * transient ones (sensor blip, board NoBroadcast)
                       * stay cleared and the system returns to a normal
                       * mode on the next tick. Note: this does NOT clear
                       * Settings.RemoteOff[] — operator-forced OFFs are
                       * sticky and cleared via the Equipment Control page. */
                ClearAlarms(0);
                WarningsClear();
                /* Also clear the SysStop latch (`remote_off.state[EQ_FAN]
                 * == 3`).  The home-page Start button POSTs
                 * `remoteStop=Start` which the bridge translates to
                 * CMD_CLEAR_ALARM — user expectation is "system
                 * resumes".  Only flip when the slot is currently in
                 * the SysStop state so we don't clobber a real OFF
                 * (1) or MANUAL (2) the operator set on the Equipment
                 * Control page. */
                if (LpSettings_GetRemoteOff(EQ_FAN) == 3U /* SYSSTOP */) {
                    if (LpSettings_SetRemoteOff(EQ_FAN, 0U /* AUTO */)) {
                        size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                        if (blen > 0) {
                            (void)LpSettings_Save(save_blob, blen);
                        }
                    }
                }
                DebugP_log("[RX] CMD_CLEAR_ALARM → SystemAlarm[] + Warning[] cleared\r\n");
                break;
            }
            case 2: { /* CMD_SET_DEFAULT — Save Settings page "Save as
                       * Panel Default" button. Snapshot the currently-
                       * active OSPI bank into the panel-default bank.
                       * Bridge mirrors the snapshot to disk via the
                       * field-33 envelope on the next cadence tick. */
                LpNssResult sr = LpSettings_SavePanelDefaults();
                DebugP_log("[RX] CMD_SET_DEFAULT (save panel default) rc=%d\r\n",
                           (int)sr);
                break;
            }
            case 3: { /* CMD_PANEL_DEFAULT — Save Settings page "Restore
                       * Panel Default" button. Load the saved panel-
                       * default blob, replay it through Deserialize so
                       * s_data reflects the snapshot, then persist as a
                       * new active-bank generation. The periodic
                       * envelope cadence (5 s window) re-broadcasts every
                       * settings page so the UI catches up. */
                static uint8_t panel_blob[LP_SETTINGS_MAX_BLOB_SIZE];
                size_t panel_len = 0;
                LpNssResult lr = LpSettings_LoadPanelDefaults(
                    panel_blob, sizeof(panel_blob), &panel_len);
                if (lr != LP_NSS_OK || panel_len == 0) {
                    DebugP_log("[RX] CMD_PANEL_DEFAULT → no panel default saved (rc=%d)\r\n",
                               (int)lr);
                    break;
                }
                if (!LpSettings_Deserialize(panel_blob, panel_len)) {
                    DebugP_log("[RX] CMD_PANEL_DEFAULT → deserialize FAILED (%u B)\r\n",
                               (unsigned)panel_len);
                    break;
                }
                size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                if (blen > 0) {
                    LpNssResult sr = LpSettings_Save(save_blob, blen);
                    DebugP_log("[RX] CMD_PANEL_DEFAULT → restored & saved %u B rc=%d\r\n",
                               (unsigned)blen, (int)sr);
                } else {
                    DebugP_log("[RX] CMD_PANEL_DEFAULT → re-serialize failed\r\n");
                }
                break;
            }
            case 4: { /* CMD_FACTORY_DEFAULT — reset s_data to factory defaults, persist */
                LpSettings_DataInit();
                size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                if (blen > 0) {
                    LpNssResult sr = LpSettings_Save(save_blob, blen);
                    DebugP_log("[RX] CMD_FACTORY_DEFAULT → reset & saved rc=%d\r\n", (int)sr);
                } else {
                    DebugP_log("[RX] CMD_FACTORY_DEFAULT → serialize failed\r\n");
                }
                break;
            }
            case 5: { /* CMD_SYSTEM_STOP — home-page Start/Stop button
                       * (`remoteStop=Stop`).  Latch the system in
                       * UI_SYSTEM_REMOTEOFF (legacy convention: write 3
                       * = SysStop into RemoteOff[RO_FAN], which
                       * `nova_states.c::CheckSystemStatus` reads to
                       * pick `ST_SYSTEM_REMOTEOFF`).  Persist so the
                       * latch survives reboot until the operator hits
                       * Start (CMD_CLEAR_ALARM clears the slot back
                       * to AUTO above). */
                if (LpSettings_SetRemoteOff(EQ_FAN, 3U /* SYSSTOP */)) {
                    size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                    if (blen > 0) {
                        LpNssResult sr = LpSettings_Save(save_blob, blen);
                        DebugP_log("[RX] CMD_SYSTEM_STOP → SysStop latched, saved rc=%d\r\n",
                                   (int)sr);
                    }
                } else {
                    DebugP_log("[RX] CMD_SYSTEM_STOP → already latched (no-op)\r\n");
                }
                break;
            }
            case 6: { /* CMD_CLEAR_DIAG — DoorDiagRow / refrig "Clear" buttons */
                bool changed = LpSettings_ClearRefrigDiag();
                if (changed) {
                    size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                    if (blen > 0) {
                        LpNssResult sr = LpSettings_Save(save_blob, blen);
                        DebugP_log("[RX] CMD_CLEAR_DIAG → cleared, saved rc=%d\r\n", (int)sr);
                    }
                } else {
                    DebugP_log("[RX] CMD_CLEAR_DIAG → no-op (no latched diag)\r\n");
                }
                break;
            }
            case 8: { /* CMD_RESET_IO_CONFIG — Level 2 IO Config "Set To
                       * Defaults" button. Wipes operator port assignments,
                       * re-applies POWER→DI2 soft default, re-pins
                       * DI1=AUX_LOW_LIMIT and DI11=E-STOP, then persists.
                       * Operator equipment renames are preserved. The next
                       * periodic IoConfig emit (within ~5s) pushes the new
                       * map back to the UI; the page bypasses its dirty-
                       * state guard so the broadcast wins. */
                LpSettings_ResetIoConfig();
                size_t blen = LpSettings_Serialize(save_blob, sizeof(save_blob));
                if (blen > 0) {
                    LpNssResult sr = LpSettings_Save(save_blob, blen);
                    DebugP_log("[RX] CMD_RESET_IO_CONFIG → reset & saved rc=%d\r\n",
                               (int)sr);
                } else {
                    DebugP_log("[RX] CMD_RESET_IO_CONFIG → serialize failed\r\n");
                }
                break;
            }
            case 50: { /* CMD_REBOOT_SOC — operator/bridge requested SoC warm reset.
                       *
                       * Used by the OTA Activate primitive: bridge writes a
                       * new image to OSPI Bank B (or to 0x80000 in the
                       * stage-2-chooser hack path), then asks firmware to
                       * cycle so the SBL re-loads from the new bytes.
                       * Same Sciclient_pmDeviceReset path the JTAG
                       * auto-flasher uses (proven 2026-05-02), so DMSC
                       * orchestrates a real warm reset (ROM re-runs, SBL
                       * loads OSPI 0x80000, full DMSC banner reappears
                       * on UART). The bridge's NovaBridge re-handshake
                       * then surfaces the new image automatically.
                       *
                       * Ack is queued by `needs_ack=true` below; the bridge
                       * will see it on the wire only if it gets out before
                       * the reset takes effect (which is fine — the bridge
                       * also handles the disconnect/reconnect gracefully).
                       *
                       * SystemP_WAIT_FOREVER means this call does not
                       * return on success — execution continues from
                       * ROM after DMSC tears the SoC down. */
                DebugP_log("[RX] CMD_REBOOT_SOC → calling "
                           "Sciclient_pmDeviceReset(SystemP_WAIT_FOREVER)\r\n");
                /* Brief delay so the Ack frame and this DebugP line have
                 * time to drain out of the UART FIFO before DMSC kills
                 * the clock to the UART module. ~50 ms matches the
                 * delay already used in flasher_uart/main.c. */
                ClockP_usleep(50 * 1000);
                (void)Sciclient_pmDeviceReset(SystemP_WAIT_FOREVER);
                /* Should not reach here. If we do, fall through to the
                 * default handler so the operator sees a log line. */
                DebugP_log("[RX] CMD_REBOOT_SOC → Sciclient_pmDeviceReset returned (UNEXPECTED)\r\n");
                break;
            }
            default:
                DebugP_log("[RX] SystemCmd cmd_type=%u not handled in LP firmware\r\n",
                           (unsigned)cmd_type);
                break;
            }
            needs_ack = true;
            continue;
        }

        /* Unknown tag — skip and keep walking the envelope. */
        size_t sk = pb_skip_field(wire, payload + pos, len - pos);
        if (sk == 0U) goto done;
        pos += sk;
    }

done:
    /* The bridge's sendCommand path waits 3 s for an Ack envelope
     * referencing the command's seq. Send one whenever we processed at
     * least one known command field; parse-error early exits skip the
     * Ack so the bridge surfaces a timeout rather than a false success.
     *
     * The Ack is QUEUED here and flushed by bridge_uart_task on its next
     * 100 ms tick. Sending it from inside this callback would call
     * NovaProto_SendRaw → polled UART TX while we're still inside the
     * bridge_uart_task's RX-drain loop — the busy-wait on THRE while RX
     * bytes might still be arriving caused frame drops in early
     * bringup. The deferred-queue keeps RX and TX cleanly serialized.
     *
     * Ack envelope wire layout (built by bridge_uart_task):
     *   Field 1 (protocol_version): tag=0x08, varint=1
     *   Field 2 (seq):              tag=0x10, varint=our seq (reuse envelope_seq)
     *   Field 100 (ack):            tag=(100<<3)|2 = 802 → varint 0xA2 0x06
     *     Ack.ref_seq (field 1, varint): tag=0x08, value=envelope_seq
     *     Ack.status  (field 2, enum):   ACK_OK = 0, proto3-suppressed.
     */
    if (needs_ack && envelope_seq_valid) {
        s_pending_ack_seq   = envelope_seq;
        s_pending_ack_ready = true;
    }
}

/* ---- Forward declarations ---- */
static void nova_main_task(void *args);
static void bridge_uart_task(void *args);
static void equipment_output_sync_task(void *args);
static void equipment_ao_sync_task(void *args);



/* ---- Bit-bang UART0 marker (debug bisect) ----
 * Writes a single character directly to UART0 THR after polling LSR.
 * Used to verify code reachability without depending on the SDK's
 * DebugP_log machinery (which silently drops output if the UART driver
 * isn't open / the log writer isn't routed). */
static void bb_uart0_putc(char c)
{
    volatile uint32_t *u = (volatile uint32_t *)CSL_UART0_BASE;
    /* Wait THRE (LSR bit 5). Bound the spin so we can't deadlock if
     * UART0 isn't clocked yet — ~1M iterations ≈ 10 ms at 100 MHz. */
    for (uint32_t i = 0; i < 1000000U; i++) {
        if (u[UART_REG_LSR / 4] & UART_LSR_THRE) break;
    }
    u[UART_REG_THR / 4] = (uint32_t)(uint8_t)c;
}
void bb_uart0_puts(const char *s)
{
    while (*s) bb_uart0_putc(*s++);
}

/* ---- Entry point ---- */
int main(void)
{
    /* Earliest possible marker — no SDK dependencies. UART0 is left
     * configured by the SBL (115200 8N1) so a raw THR write should
     * appear on the App UART (COM7) before anything else. */
    bb_uart0_puts("\r\n[BB] main entered\r\n");

    System_init();
    bb_uart0_puts("[BB] after System_init\r\n");
    Board_init();
    bb_uart0_puts("[BB] after Board_init\r\n");

    Drivers_open();
    bb_uart0_puts("[BB] after Drivers_open\r\n");
    bb_uart0_puts("[BB] before Board_eepromOpen\r\n");
    {
        extern int32_t Board_eepromOpen(void);
        int32_t st = Board_eepromOpen();
        char buf[48];
        snprintf(buf, sizeof(buf), "[BB] after Board_eepromOpen st=%d\r\n", (int)st);
        bb_uart0_puts(buf);
    }
    bb_uart0_puts("[BB] before Board_flashOpen\r\n");
    {
        extern int32_t Board_flashOpen(void);
        int32_t flashStatus = Board_flashOpen();
        char buf[48];
        snprintf(buf, sizeof(buf), "[BB] after Board_flashOpen status=%d\r\n", (int)flashStatus);
        bb_uart0_puts(buf);
    }
    /* 0.A.91: chip software-reset (RSTEN+RST) immediately after the
     * Flash driver opens. Clears any wedged WIP/state from a prior
     * session — see Platform/hal_flash.c::hal_flash_chip_reset for the
     * full rationale (SDK's Flash_norOspiWaitReady has an infinite-loop
     * bug when cmdRead fails). Pre-scheduler safe: only OSPI register
     * writes + ClockP_usleep. */
    {
        extern void hal_flash_init(void);
        hal_flash_init();
        bb_uart0_puts("[BB] hal_flash_init (chip reset) ok\r\n");
    }

    /* UART2 register-level setup. The SDK's full UART driver was
     * rejected during bringup because its combined RX/TX ISR stalled
     * TX while RX bytes arrived (HARDWARE_BRINGUP.md §9). We use:
     *   - polled TX (bridge_uart_tx, IIR-driven THRE poll)
     *   - custom narrow RX-only ISR (uart2_rx_isr → ring buffer)
     * SDK Drivers_open() already configured baud rate (921600) and
     * LCR (8N1); we only need to disable its IER bits and re-program
     * FCR for our trigger level. */
    s_uart2_base = (volatile uint32_t *)CSL_UART2_BASE;
    s_uart2_base[UART_REG_IER / 4] = 0x00;
    bb_uart0_puts("[BB] U2: IER cleared\r\n");

    /* FCR layout (TI 16550-compatible):
     *   bit0    = FIFO_EN
     *   bit1    = RX_FIFO_RST
     *   bit2    = TX_FIFO_RST
     *   bit3    = DMA_MODE   (kept 0 so UDMA can't race the CPU)
     *   bits6-7 = RX_TRIG_LVL: 00=1, 01=4, 10=8, 11=14
     * Trigger level 8 (0x80) gives ~70 µs of headroom at 921600 baud
     * between IRQs while keeping ISR overhead low. The 64-byte HW FIFO
     * still has 56 bytes of slack against any ISR latency spike. */
    s_uart2_base[UART_REG_FCR / 4] = 0x87;
    bb_uart0_puts("[BB] U2: FCR set\r\n");

    /* Drain any stale RX bytes the SDK left in the FIFO before we
     * register the ISR, otherwise the first IRQ would fire on data we
     * never asked for and skew NovaProto's COBS state machine. */
    while (s_uart2_base[UART_REG_LSR / 4] & UART_LSR_DR) {
        (void)s_uart2_base[UART_REG_RHR / 4];
    }
    bb_uart0_puts("[BB] U2: drained\r\n");

    /* Register the custom UART2 RX ISR. Done BEFORE NovaProto_Init so
     * that the first byte received after we enable IER cannot reach the
     * ring before the COBS decoder is ready to consume it. */
    HwiP_Params hwiParams;
    HwiP_Params_init(&hwiParams);
    hwiParams.intNum   = UART2_IRQ_NUM;
    hwiParams.callback = uart2_rx_isr;
    hwiParams.args     = NULL;
    (void)HwiP_construct(&s_uart2_hwi, &hwiParams);
    bb_uart0_puts("[BB] U2: HwiP_construct ok\r\n");

    /* Initialize the COBS+CRC protocol layer */
    NovaProto_Init(bridge_uart_tx, bridge_rx_callback);
    bb_uart0_puts("[BB] NovaProto_Init ok\r\n");

    /* Enable Receive Data Available + Line Status interrupts. From this
     * point uart2_rx_isr drains the HW FIFO into s_rx_ring; the bridge
     * task pulls from the ring on its own cadence. */
    s_uart2_base[UART_REG_IER / 4] = UART_IER_ERBI | UART_IER_ELSI;
    bb_uart0_puts("[BB] U2: IER enabled\r\n");

    /* Seed the wall-clock from compile-time __DATE__/__TIME__ so the
     * DateTime envelope has sensible content from cold boot. The bridge
     * will overwrite this with the host clock via SettingsUpdate.field 26
     * shortly after the first DataLoadStatus(ready=true) reaches it. */
    LpRtc_Init();
    bb_uart0_puts("[BB] LpRtc_Init ok\r\n");

    /* Phase D — settings vault: scan RAM banks for a valid blob. On
     * cold boot both banks are 0xFF/zero so this is a no-op (active=-1).
     * Apparent power-cycle persistence comes from the bridge replaying
     * ~/.constellation/lp_settings.bin via envelope-91 once it sees
     * DataLoadStatus(ready=true) and protocol_version=1. */
    /* Phase E — Settings struct: zero defaults BEFORE LpSettings_Init
     * so a Restore() callback can populate it. The store calls back
     * into LpSettings_Deserialize() if a valid blob exists in either
     * RAM bank (typically empty on cold boot — the bridge replays its
     * file mirror via envelope-91 once the link comes up). */
    LpSettings_DataInit();
    bb_uart0_puts("[BB] LpSettings_DataInit ok\r\n");
    LpSettings_SetApplyCallback(
        (LpSettingsApplyFn)LpSettings_Deserialize);

    LpSettings_Init();
    bb_uart0_puts("[BB] LpSettings_Init ok\r\n");
    /* Cold-boot defaults push lives in bridge_uart_task — see the
     * "cold-boot defaults seeding" block. Pushing here would race the
     * bridge's envelope-91 replay and clobber operator changes. */

    /* Phase F — Per-board provisioning record. Reads role + IP from
     * compile-time defaults today (lp_device_config.c); Phase 2 will
     * back this with a dedicated OSPI region so a single nova_lp.bin
     * image can be assigned to any board post-flash. */
    LpDeviceConfig_Init();
    bb_uart0_puts("[BB] LpDeviceConfig_Init ok\r\n");

    /* Phase 1B OTA — bring up the firmware-update bookkeeping (reads
     * FwBankHeader / FwBootMeta from OSPI, decides active bank). The
     * `lp_ota_task` listener on :5503 needs this populated before it
     * answers any FwBeginUpdate / push_bank_info call. Init is read-
     * only against OSPI (no erase/write), so it's safe to run pre-
     * scheduler immediately after Board_flashOpen. */
    {
        extern void NovaFwUpdate_Init(void);
        NovaFwUpdate_Init();
        bb_uart0_puts("[BB] NovaFwUpdate_Init ok\r\n");
    }

    /* Phase 3.7 OTA broker — Pi5 install-orchestration. Reads envelope
     * tags 130..136, emits 140..142. NovaOtaBroker_Init() resets
     * state, creates the chunk ring + command queue + mutex + wake
     * semaphore, and statically allocates `ota_broker` task (created
     * here pre-scheduler, runs once vTaskStartScheduler() starts). All
     * leaf I/O (TCP push to remote LP via lwIP, local OSPI writes for
     * controller self-update) runs on the broker task so heartbeats
     * keep flowing from bridge_uart_task during a multi-minute install.
     * See docs/uart-airgap-architecture.md. */
    NovaOtaBroker_Init();
    bb_uart0_puts("[BB] NovaOtaBroker_Init ok (task created)\r\n");

    OrbitState_Init();
    bb_uart0_puts("[BB] OrbitState_Init ok\r\n");

    OrbitRole role = OrbitRole_Get();
    bb_uart0_puts("[BB] OrbitRole_Get ok\r\n");
    bb_uart0_puts("[BB] LP role = ");
    bb_uart0_puts(OrbitRole_Name(role));
    bb_uart0_puts("\r\n");
    bb_uart0_puts("[BB] role logged\r\n");

    /* Bring up lwIP's SYS_ARCH_PROTECT mutex BEFORE any task that may touch
     * lwIP. See include comment above. */
    sys_init();
    bb_uart0_puts("[BB] lwip sys_init ok\r\n");

    /* Drive all wired DOs low and announce DI count. SysConfig already
     * programmed pinmux+DIR via Drivers_open(); this is the post-boot
     * "known safe state" cycle. */
    LpOrbitIo_Init();
    bb_uart0_puts("[BB] orbit io init ok\r\n");

    /* nova_main_task is the universal banner/idle task — always spawned. */
    xTaskCreateStatic(nova_main_task, "nova_main",
        NOVA_TASK_STACK, NULL, NOVA_TASK_PRI,
        gNovaTaskStack, &gNovaTaskObj);
    bb_uart0_puts("[BB] nova_main task created\r\n");

    /* Bridge UART (envelope/COBS to the RPi5) only makes sense for the
     * CONTROLLER role; orbit boards have no bridge upstream. */
    if (role == ORBIT_ROLE_CONTROLLER) {
        xTaskCreateStatic(bridge_uart_task, "bridge_uart",
            BRIDGE_TASK_STACK, NULL, BRIDGE_TASK_PRI,
            gBridgeTaskStack, &gBridgeTaskObj);
    }

    /* lwIP / CPSW comes up in every role — orbit boards need it for
     * the Modbus TCP server, controller boards need it for orbit_client. */
    xTaskCreateStatic(enet_task_entry, "enet_lwip",
        ENET_TASK_STACK, NULL, ENET_TASK_PRI,
        gEnetTaskStack, &gEnetTaskObj);
    bb_uart0_puts("[BB] enet task created\r\n");

    if (role == ORBIT_ROLE_CONTROLLER) {
        /* Controller-only TCP smoke / orbit_client setup. */
        xTaskCreateStatic(lwip_smoke_task, "lwip_smoke",
            SMOKE_TASK_STACK, NULL, SMOKE_TASK_PRI,
            gSmokeTaskStack, &gSmokeTaskObj);
    } else if (OrbitRole_IsOrbit(role)) {
        /* Orbit roles host a Modbus TCP slave on :5502 plus a
         * comm-loss watchdog and sensor RTU master. */
        xTaskCreateStatic(orbit_modbus_tcp_task, "orbit_mbt",
            ORBIT_MBT_TASK_STACK, NULL, ORBIT_MBT_TASK_PRI,
            gOrbitMbtStack, &gOrbitMbtObj);
        xTaskCreateStatic(orbit_safemode_task, "orbit_sm",
            ORBIT_SM_TASK_STACK, NULL, ORBIT_SM_TASK_PRI,
            gOrbitSmStack, &gOrbitSmObj);
        xTaskCreateStatic(orbit_sensor_rtu_task, "orbit_sr",
            ORBIT_SR_TASK_STACK, NULL, ORBIT_SR_TASK_PRI,
            gOrbitSrStack, &gOrbitSrObj);
        /* LAUNCHXL gigabit-RGMII workaround. Controller path runs the
         * same fixup as part of lwip_smoke_task; orbit roles need this
         * standalone one-shot. */
        xTaskCreateStatic(lwip_phy_fixup_task, "phy_fixup",
            PHY_FIXUP_TASK_STACK, NULL, PHY_FIXUP_TASK_PRI,
            gPhyFixupStack, &gPhyFixupObj);
    }

    /* NET-WATCHDOG (0.A.216) — defense-in-depth against the post-Activate
     * MAC half-deaf wedge. Spawned for every role; harmless overhead
     * (15-s poll cadence, ~3 KB stack). */
    xTaskCreateStatic(lwip_net_watchdog_task, "net_wd",
        NET_WD_TASK_STACK, NULL, NET_WD_TASK_PRI,
        gNetWdStack, &gNetWdObj);

    if (role == ORBIT_ROLE_GDC) {
        bb_uart0_puts("[BB] GDC: pre OrbitGdc_Init\r\n");
        OrbitGdc_Init();
        bb_uart0_puts("[BB] GDC: OrbitGdc_Init ok\r\n");
        xTaskCreateStatic(orbit_gdc_task, "orbit_gdc",
            ORBIT_GDC_TASK_STACK, NULL, ORBIT_GDC_TASK_PRI,
            gOrbitGdcStack, &gOrbitGdcObj);
        bb_uart0_puts("[BB] GDC: task created\r\n");
    }

    if (role == ORBIT_ROLE_TRITON) {
        OrbitTriton_Init();
        xTaskCreateStatic(orbit_triton_task, "orbit_triton",
            ORBIT_TRITON_TASK_STACK, NULL, ORBIT_TRITON_TASK_PRI,
            gOrbitTritonStack, &gOrbitTritonObj);
    }

    /* OTA listener — Phase 1A. Runs on every role that has Ethernet so
     * the bridge can pull FwBankInfo from any board.  Phase 1B will
     * extend the same task to handle FwBeginUpdate / FwDataChunk and
     * write into Bank B (after the lp_device_config offset collision
     * is resolved — see docs/LP-AM2434-OTA-Update-Plan.md). */
    xTaskCreateStatic(lp_ota_task, "lp_ota",
        LP_OTA_TASK_STACK, NULL, LP_OTA_TASK_PRI,
        gLpOtaStack, &gLpOtaObj);

    /* Equipment-output sync — CONTROLLER only, since it talks to the
     * STORAGE orbit via OrbitClient (which itself is only spawned on
     * CONTROLLER via lwip_smoke_task). */
    if (role == ORBIT_ROLE_CONTROLLER) {
        xTaskCreateStatic(equipment_output_sync_task, "eq_out",
            EQ_OUT_TASK_STACK, NULL, EQ_OUT_TASK_PRI,
            gEqOutStack, &gEqOutObj);

        xTaskCreateStatic(equipment_ao_sync_task, "eq_ao",
            EQ_AO_TASK_STACK, NULL, EQ_AO_TASK_PRI,
            gEqAoStack, &gEqAoObj);

        /* AS2 equipment engine — Phase A1 (read-only, mode display
         * only; outputs still driven by equipment_output_sync_task). */
        xTaskCreateStatic(lp_engine_task, "lp_engine",
            LP_ENGINE_TASK_STACK, NULL, LP_ENGINE_TASK_PRI,
            gLpEngineStack, &gLpEngineObj);
    }

    /* Independent dual-core watchdog — heartbeat producer task on
     * the main core. Watchdog body runs on R5FSS1_0 (separate image
     * bundled into the same .mcelf.hs_fs by the build system). */
    LpWatchdogClient_Start();

    bb_uart0_puts("[BB] pre vTaskStartScheduler\r\n");
    vTaskStartScheduler();
    bb_uart0_puts("[BB] !! scheduler returned !!\r\n");

    Board_driversClose();
    Drivers_close();
    Board_deinit();
    System_deinit();
    return 0;
}

/* ---- Equipment-output sync task body (Phase E2, fw 0.A.15+) ----
 *
 * Diff-based FC05 dispatcher.  Reads `LpSettings.io_config.output_map[]`
 * (operator's IO Config assignments) crossed with `remote_off.state[]`
 * (operator's Equipment Control overrides), computes per-coil desired
 * state, and writes ONLY the slots that changed since the previous tick.
 *
 * Mapping rule (legacy AS2 SS_PORT_ID_MULTIPLIER scheme = 12):
 *   port_id 0          → unassigned (skip)
 *   port_id 1..11      → STORAGE board (orbit 0), coil = port_id - 1
 *   port_id 12+        → reserved for future expansion boards (skip)
 *
 * STORAGE has 10 DO coils (0..9 → DO1..DO10) per orbit_client.h.  We
 * cap at 11 because the legacy AS2 scheme reserved port id 12 as the
 * board-1 boundary; any port_id ≥ 12 today is an operator misassignment
 * and is silently ignored (the IO Config UI's port selector won't
 * surface those values anyway).
 *
 * Phase A2 (0.A.26+): output authority comes from the engine. Each
 * tick, for every populated equipment slot, we ask `CheckOutputs(eq)`
 * (defined in nova_serialshift.c — reads the engine-set bit in
 * `IoBoard[MAIN].OutputState`) for the desired state. As a fallback
 * for slots the engine doesn't drive (PULSEDOOR_*, REDLIGHT,
 * YELLOWLIGHT, POWER, REMOTE_STANDBY, etc. — see lp_engine_shim.c
 * `kRemoteOffMap`), the operator's REMOTE_MANUAL flag still wins so
 * those bench-test buttons keep working. Phase A3 brings the engine's
 * Ctrl* PID drivers online and the fallback shrinks to just the
 * Constellation-only slots. */
#define EQ_OUT_REMOTE_MANUAL   2U      /* must match RemoteOffState.MANUAL */
#define EQ_OUT_TICK_MS         250U    /* responsive but not chatty */

static uint8_t s_last_driven_coil [ORBIT_CLIENT_MAX_ORBITS][ORBIT_DO_COIL_COUNT];
static bool    s_last_driven_valid[ORBIT_CLIENT_MAX_ORBITS][ORBIT_DO_COIL_COUNT];

extern int CheckOutputs(unsigned int eqIndex);

static void equipment_output_sync_task(void *args)
{
    (void)args;

    /* Wait until OrbitClient_Init has finished spawning workers — it
     * runs from lwip_smoke_task which itself waits for netif up.  A
     * generous initial delay avoids hammering FC05 before the orbit
     * connections exist (would just log spurious connect failures). */
    vTaskDelay(pdMS_TO_TICKS(15000));

    DebugP_log("[EQOUT] starting (orbits=%u)\r\n",
               (unsigned)OrbitClient_Count());

    for (;;) {
        const LpSettingsData *s = LpSettings_DataGet();
        if (s == NULL) {
            vTaskDelay(pdMS_TO_TICKS(EQ_OUT_TICK_MS));
            continue;
        }

        /* Build per-coil desired bitmap fresh each tick. */
        uint8_t desired[ORBIT_CLIENT_MAX_ORBITS][ORBIT_DO_COIL_COUNT];
        memset(desired, 0, sizeof(desired));

        const uint32_t out_count = s->io_config.output_count;
        for (uint32_t eq = 0; eq < out_count && eq < LP_IO_ENTRIES_MAX; eq++) {
            const uint32_t port_id = s->io_config.output_map[eq];
            if (port_id == 0U) continue;          /* unassigned */
            if (port_id >= 12U) continue;         /* board ≥ 1: not yet wired */
            const uint8_t coil = (uint8_t)(port_id - 1U);
            if (coil >= ORBIT_DO_COIL_COUNT) continue;

            /* Engine-driven intent (mode logic + ApplyManualOverrides
             * inside SetMode). Falls back to operator MANUAL for slots
             * the engine doesn't track (Constellation-only outputs).
             *
             * E-Stop master gate: when E-Stop is asserted (open), the
             * engine is already in ST_SHUTDOWN and CheckOutputs(eq)
             * returns 0 for everything. The MANUAL fallback below
             * would otherwise still energize an operator's manually-
             * forced equipment — explicitly stomp it here so "even
             * MANUAL doesn't run in E-Stop" is enforced in firmware
             * (in addition to the hardware safety relay that
             * physically de-energizes the output rail). */
            const bool estop = (Nova_GetEStopActive() != 0U);
            bool want_on = !estop
                        && (   (CheckOutputs(eq) != 0)
                            || (s->remote_off.state[eq] == EQ_OUT_REMOTE_MANUAL));

            if (want_on) {
                desired[0][coil] = 1U;
            }
        }

        /* Diff-and-dispatch.  Only write coils that changed since last
         * tick OR have never been written yet.  The first iteration
         * after boot will burst-write whatever the cold state requires
         * (most likely all-zero, so a no-op on the orbit side). */
        const uint8_t orbits = (uint8_t)OrbitClient_Count();
        for (uint8_t orb = 0; orb < orbits && orb < ORBIT_CLIENT_MAX_ORBITS; orb++) {
            /* Only orbit 0 (STORAGE) has DO coils mapped today — see
             * the port_id rule above. Skip GDC/TRITON to save a needless
             * connect attempt that would always write 0. */
            if (orb != 0U) continue;

            for (uint8_t c = 0; c < ORBIT_DO_COIL_COUNT; c++) {
                const bool want = (desired[orb][c] != 0U);
                if (s_last_driven_valid[orb][c]
                    && (s_last_driven_coil[orb][c] != 0U) == want) {
                    continue;     /* unchanged — nothing to write */
                }

                int rc = OrbitClient_WriteCoil(orb, c, want);
                if (rc == 0) {
                    s_last_driven_coil [orb][c] = want ? 1U : 0U;
                    s_last_driven_valid[orb][c] = true;
                    DebugP_log("[EQOUT] orbit=%u coil=%u → %s\r\n",
                               (unsigned)orb, (unsigned)c,
                               want ? "ON" : "OFF");
                }
                /* On failure we DO NOT mark valid — next tick re-tries.
                 * This makes orbit-disconnect → reconnect self-heal. */
            }
        }

        vTaskDelay(pdMS_TO_TICKS(EQ_OUT_TICK_MS));
    }
}

/* ---- Equipment-AO sync task body (Phase A4 follow-up, fw 0.A.44+) ----
 *
 * The legacy engine computes `PwmChannel[PWM_DOORS|REFRIGERATION|FAN|
 * BURNER].Output` in raw timer counts (PWM_MIN_VALUE..PWM_MAX_VALUE,
 * 55..277 = 0..100 %). Operator picks per-(orbit, channel) which PWM
 * source drives the 4-20 mA AO via the Level 2 4-20mA / PWM Output
 * Setup page; that selection persists in `Settings.AoEquip[slot][ch]`
 * (`ao_equip_t` enum, 0=UNUSED / 1=FAN / 2=DOORS / 3=REFRIG / 4=BURNER).
 *
 * Each tick we walk every assigned (slot, channel) pair, convert the
 * source PwmChannel.Output → 0..100 %, and FC06-write to the orbit's
 * HR (HR0=channel 0, HR1=channel 1; range validated 0..100 in
 * orbit_storage.c::write_one_hr). Per-(slot, channel) last-written
 * cache means an idle PID generates zero Modbus traffic.
 *
 * E-Stop master gate mirrors the digital path: when E-Stop is open we
 * force every assigned AO to 0 % (de-energize all 4-20 mA actuators).
 * Unassigned channels (`equip == 0`) are intentionally skipped — the
 * AO holds whatever value the orbit last latched (typically 0 from
 * cold boot or safemode). */
#define AO_EQUIP_UNUSED   0U   /* mirror of nova_fan_output.h::ao_equip_t */
#define AO_EQUIP_FAN      1U
#define AO_EQUIP_DOORS    2U
#define AO_EQUIP_REFRIG   3U
#define AO_EQUIP_BURNER   4U

#define EQ_AO_ORBITS     LP_AO_EQUIP_SLOT_MAX
#define EQ_AO_CHANNELS   LP_AO_EQUIP_CH_MAX

static uint8_t s_last_ao_pct  [EQ_AO_ORBITS][EQ_AO_CHANNELS];
static bool    s_last_ao_valid[EQ_AO_ORBITS][EQ_AO_CHANNELS];

/* PwmChannel[].Output (raw counts 55..277) → 0..100 %. Out-of-range
 * (e.g. PWM_UNDEFINED=255 with MIN/MAX inverted, or stale init) maps
 * to 0 to fail safe. Same formula used by the diag emit in
 * build_system_status_envelope so the wire and the AO agree. */
static uint8_t pwm_raw_to_pct(unsigned int raw)
{
    if (raw < PWM_MIN_VALUE || raw > PWM_MAX_VALUE) return 0U;
    uint32_t pct = ((raw - PWM_MIN_VALUE) * 100U + (PWM_RANGE / 2U)) / PWM_RANGE;
    return (pct > 100U) ? 100U : (uint8_t)pct;
}

static uint8_t ao_pct_for_equip(uint8_t equip)
{
    switch (equip) {
        case AO_EQUIP_FAN:    return pwm_raw_to_pct(PwmChannel[PWM_FAN].Output);
        case AO_EQUIP_DOORS:  return pwm_raw_to_pct(PwmChannel[PWM_DOORS].Output);
        case AO_EQUIP_REFRIG: return pwm_raw_to_pct(PwmChannel[PWM_REFRIGERATION].Output);
        case AO_EQUIP_BURNER: return pwm_raw_to_pct(PwmChannel[PWM_BURNER].Output);
        default:              return 0U;
    }
}

static void equipment_ao_sync_task(void *args)
{
    (void)args;

    /* Match the digital path's settle window: orbit workers + engine
     * need ~15 s after netif up before any AO drive makes sense. */
    vTaskDelay(pdMS_TO_TICKS(15000));

    DebugP_log("[EQAO] starting (orbits=%u)\r\n",
               (unsigned)OrbitClient_Count());

    for (;;) {
        const bool estop = (Nova_GetEStopActive() != 0U);
        const uint8_t orbits = (uint8_t)OrbitClient_Count();

        for (uint8_t orb = 0; orb < orbits && orb < EQ_AO_ORBITS; orb++) {
            for (uint8_t ch = 0; ch < EQ_AO_CHANNELS; ch++) {
                uint8_t equip = LpSettings_GetAoEquip(orb, ch);
                if (equip == AO_EQUIP_UNUSED) continue;

                /* E-Stop wins: force every assigned AO to 0 %. */
                uint8_t want = estop ? 0U : ao_pct_for_equip(equip);

                if (s_last_ao_valid[orb][ch]
                    && s_last_ao_pct[orb][ch] == want) {
                    continue;     /* unchanged — nothing to write */
                }

                /* HR0 = channel 0, HR1 = channel 1 (orbit_storage.c
                 * HR_AO_BASE=0, ORBIT_AO_COUNT=2). Value validated
                 * 0..100 server-side; we send pct directly. */
                int rc = OrbitClient_WriteHoldingRegister(orb,
                            (uint16_t)ch, (uint16_t)want);
                if (rc == 0) {
                    s_last_ao_pct  [orb][ch] = want;
                    s_last_ao_valid[orb][ch] = true;
                    DebugP_log("[EQAO] orbit=%u ch=%u equip=%u → %u%%\r\n",
                               (unsigned)orb, (unsigned)ch,
                               (unsigned)equip, (unsigned)want);
                }
                /* On failure: leave valid=false so next tick re-tries
                 * (orbit reconnect self-heals, identical pattern to
                 * the FC05 path). */
            }
        }

        vTaskDelay(pdMS_TO_TICKS(EQ_AO_TICK_MS));
    }
}

/* ---- AS2 equipment-engine tick task (Phase A1, fw 0.A.20+) ----
 *
 * Boots the legacy engine once, then ticks it every second.  The
 * engine is read-only at this stage: its CurrentMode output flows
 * into SystemStatus.current_mode (proto field 16, AS2 UI_* numbering)
 * but its OutputOn/OutputOff calls go into the IoBoard[] shadow only
 * — no equipment is driven by the engine yet. Phase A2 hands real
 * coil control over from equipment_output_sync_task. */
static void lp_engine_task(void *args)
{
    (void)args;

    /* Wait long enough for OrbitClient + LpSettings to be ready. */
    vTaskDelay(pdMS_TO_TICKS(15000));

    DebugP_log("[ENGINE] init\r\n");
    lp_engine_init();

    char prevMode = -1;
    for (;;) {
        lp_engine_tick();
        LpWatchdog_Ping(LP_WD_ALIVE_ENGINE_TICK);
        unsigned char m = lp_engine_get_current_mode();
        if ((char)m != prevMode) {
            DebugP_log("[ENGINE] mode %u → %u\r\n",
                       (unsigned)prevMode, (unsigned)m);
            prevMode = (char)m;
        }
        vTaskDelay(pdMS_TO_TICKS(LP_ENGINE_TICK_MS));
    }
}

/* ---- Main task: one-time banner, then idle ---- */
static void nova_main_task(void *args)
{
    (void)args;
    DebugP_log("\r\n");
    DebugP_log("============================================\r\n");
    DebugP_log("  Constellation Nova on LP-AM2434\r\n");
    DebugP_log("  R5F @ 800 MHz | FreeRTOS | Phase 2 COBS\r\n");
    DebugP_log("============================================\r\n");
    DebugP_log("\r\n");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

/* ---- Protobuf varint encoder ---- */
static size_t pb_encode_varint(uint8_t *buf, uint32_t value)
{
    size_t n = 0;
    while (value > 0x7F) {
        buf[n++] = (uint8_t)(0x80 | (value & 0x7F));
        value >>= 7;
    }
    buf[n++] = (uint8_t)value;
    return n;
}

/*
 * Build: Envelope { protocol_version=1, seq=N, heartbeat={uptime_sec=U} }
 *
 * Wire layout:
 *   Field 1 (protocol_version): tag=0x08, varint=1
 *   Field 2 (seq):              tag=0x10, varint=N
 *   Field 101 (heartbeat):      tag=0xAA 0x06, len, sub-message
 *     Heartbeat.uptime_sec (field 1): tag=0x08, varint=U
 */
static size_t build_heartbeat_envelope(uint8_t *buf, size_t bufsize,
                                       uint32_t seq, uint32_t uptime_sec)
{
    uint8_t inner[16];
    size_t inner_len = 0;
    size_t pos = 0;

    /* Heartbeat.uptime_sec = U */
    inner[inner_len++] = 0x08;
    inner_len += pb_encode_varint(&inner[inner_len], uptime_sec);

    /* Envelope.protocol_version = 1 */
    buf[pos++] = 0x08;
    pos += pb_encode_varint(&buf[pos], NOVA_PROTOCOL_VERSION);

    /* Envelope.seq = N */
    buf[pos++] = 0x10;
    pos += pb_encode_varint(&buf[pos], seq);

    /* Envelope.heartbeat (field 101, length-delimited)
     * Tag = (101 << 3) | 2 = 810 → varint 0xAA 0x06 */
    buf[pos++] = 0xAA;
    buf[pos++] = 0x06;
    pos += pb_encode_varint(&buf[pos], (uint32_t)inner_len);
    memcpy(&buf[pos], inner, inner_len);
    pos += inner_len;

    (void)bufsize;
    return pos;
}

/*
 * Build: Envelope { protocol_version=1, seq=N, data_load_status={ready=true} }
 *
 * Wire layout:
 *   Field 1 (protocol_version): tag=0x08, varint=1
 *   Field 2 (seq):              tag=0x10, varint=N
 *   Field 17 (data_load_status): tag=0x8A 0x01, len, sub-message
 *     DataLoadStatus.ready (field 1): tag=0x08, varint=1
 *
 * The bridge flips its `connected` flag to true on receipt of
 * DataLoadStatus(ready=true). LP bringup firmware sends this once at
 * startup so the UI's nova.connected indicator reflects the real link.
 */
static size_t build_dls_envelope(uint8_t *buf, size_t bufsize, uint32_t seq)
{
    size_t pos = 0;
    uint8_t inner[4];
    size_t inner_len = 0;

    /* DataLoadStatus.ready = true */
    inner[inner_len++] = 0x08;
    inner[inner_len++] = 0x01;

    /* Envelope.protocol_version = 1 */
    buf[pos++] = 0x08;
    pos += pb_encode_varint(&buf[pos], NOVA_PROTOCOL_VERSION);

    /* Envelope.seq = N */
    buf[pos++] = 0x10;
    pos += pb_encode_varint(&buf[pos], seq);

    /* Envelope.data_load_status (field 17, length-delimited)
     * Tag = (17 << 3) | 2 = 138 → varint 0x8A 0x01 */
    buf[pos++] = 0x8A;
    buf[pos++] = 0x01;
    pos += pb_encode_varint(&buf[pos], (uint32_t)inner_len);
    memcpy(&buf[pos], inner, inner_len);
    pos += inner_len;

    (void)bufsize;
    return pos;
}

/*
 * Build: Envelope { protocol_version=1, seq=N,
 *                   version_info={arm_version, bootloader_version} }
 *
 * Wire layout:
 *   Field 1  (protocol_version): tag=0x08, varint=1
 *   Field 2  (seq):              tag=0x10, varint=N
 *   Field 22 (version_info):     tag=(22<<3)|2 = 178 → varint 0xB2 0x01,
 *                                len, sub-message
 *     VersionInfo.arm_version        (field 1, string): tag=0x0A, len, bytes
 *     VersionInfo.bootloader_version (field 2, string): tag=0x12, len, bytes
 *
 * The VersionInfo.boards repeated field (field 3) is intentionally not
 * emitted from the LP. Per-board version probing is an orbit-side
 * responsibility once the orbit-discovery envelope (tag 121) carries
 * board firmware strings; the bridge will merge the two on its way to
 * the UI.
 *
 * Strings are taken from compile-time constants in `lp_version.h`. Both
 * MUST be non-empty (the bridge UI hides the line when the string is
 * blank) and are bounded by the 24-char practical UI limit.
 *
 * Source: proto/agristar/system.proto :: VersionInfo
 *         constellation-ui/server/src/novaDataStore.ts :: decodeVersionInfo
 */
static size_t build_version_info_envelope(uint8_t *buf, size_t bufsize,
                                          uint32_t seq)
{
    const char *arm_ver  = LP_FW_VERSION;
    const char *boot_ver = LP_BOOTLOADER_VERSION;
    const size_t arm_len  = strlen(arm_ver);
    const size_t boot_len = strlen(boot_ver);

    /* Inner: VersionInfo body. Two short strings; 64B is generous. */
    uint8_t inner[64];
    size_t ilen = 0;

    /* arm_version (field 1, string) */
    inner[ilen++] = 0x0A;
    ilen += pb_encode_varint(&inner[ilen], (uint32_t)arm_len);
    if (ilen + arm_len > sizeof(inner)) return 0;
    memcpy(&inner[ilen], arm_ver, arm_len);
    ilen += arm_len;

    /* bootloader_version (field 2, string) */
    inner[ilen++] = 0x12;
    ilen += pb_encode_varint(&inner[ilen], (uint32_t)boot_len);
    if (ilen + boot_len > sizeof(inner)) return 0;
    memcpy(&inner[ilen], boot_ver, boot_len);
    ilen += boot_len;

    /* Wrap in Envelope. */
    size_t pos = 0;

    /* Envelope.protocol_version = 1 */
    buf[pos++] = 0x08;
    pos += pb_encode_varint(&buf[pos], NOVA_PROTOCOL_VERSION);

    /* Envelope.seq = N */
    buf[pos++] = 0x10;
    pos += pb_encode_varint(&buf[pos], seq);

    /* Envelope.version_info — field 22, length-delimited.
     * Tag = (22 << 3) | 2 = 178 → varint 0xB2 0x01. */
    buf[pos++] = 0xB2;
    buf[pos++] = 0x01;
    pos += pb_encode_varint(&buf[pos], (uint32_t)ilen);
    if (pos + ilen > bufsize) return 0;
    memcpy(&buf[pos], inner, ilen);
    pos += ilen;

    return pos;
}

/*
 * Build: Envelope { protocol_version=1, seq=N,
 *                   date_time={date_str, time_str, am_pm} }
 *
 * Wire layout:
 *   Field 1  (protocol_version): tag=0x08, varint=1
 *   Field 2  (seq):              tag=0x10, varint=N
 *   Field 21 (date_time):        tag=(21<<3)|2 = 170 → varint 0xAA 0x01,
 *                                len, sub-message
 *     DateTime.date_str (field 1, string): tag=0x0A, len, bytes
 *     DateTime.time_str (field 2, string): tag=0x12, len, bytes
 *     DateTime.am_pm    (field 3, varint): tag=0x18, value
 *
 * Returns 0 if the wall-clock has no baseline yet (skip TX) — the
 * bridge holds DateTime in cache and stale "01/01/1970" reads would
 * be worse than no read at all. Once LpRtc_SetFromStrings has run
 * (either from build-time fallback in LpRtc_Init or from a bridge
 * RTC sync), the strings are always non-empty.
 *
 * Source: proto/agristar/system.proto :: DateTime
 *         constellation-ui/server/src/novaDataStore.ts :: decodeDateTime
 */
static size_t build_date_time_envelope(uint8_t *buf, size_t bufsize,
                                       uint32_t seq)
{
    char date_str[16];
    char time_str[16];
    uint32_t am_pm = 0;
    LpRtc_GetStrings(date_str, time_str, &am_pm);

    size_t dlen = strlen(date_str);
    size_t tlen = strlen(time_str);
    if (dlen == 0 || tlen == 0) return 0;   /* no baseline yet */

    /* Inner: DateTime body. Two short strings + 1 varint; 64B is generous. */
    uint8_t inner[64];
    size_t ilen = 0;

    /* date_str (field 1, string) */
    inner[ilen++] = 0x0A;
    ilen += pb_encode_varint(&inner[ilen], (uint32_t)dlen);
    if (ilen + dlen > sizeof(inner)) return 0;
    memcpy(&inner[ilen], date_str, dlen);
    ilen += dlen;

    /* time_str (field 2, string) */
    inner[ilen++] = 0x12;
    ilen += pb_encode_varint(&inner[ilen], (uint32_t)tlen);
    if (ilen + tlen > sizeof(inner)) return 0;
    memcpy(&inner[ilen], time_str, tlen);
    ilen += tlen;

    /* am_pm (field 3, varint) — proto3 zero-suppression is correct here:
     * 0 = AM is the default and the decoder will treat absence as AM.
     * Skipping the field when am_pm == 0 keeps the wire compact. */
    if (am_pm != 0) {
        inner[ilen++] = 0x18;
        ilen += pb_encode_varint(&inner[ilen], am_pm);
    }

    /* Wrap in Envelope. */
    size_t pos = 0;

    /* Envelope.protocol_version = 1 */
    buf[pos++] = 0x08;
    pos += pb_encode_varint(&buf[pos], NOVA_PROTOCOL_VERSION);

    /* Envelope.seq = N */
    buf[pos++] = 0x10;
    pos += pb_encode_varint(&buf[pos], seq);

    /* Envelope.date_time — field 21, length-delimited.
     * Tag = (21 << 3) | 2 = 170 → varint 0xAA 0x01. */
    buf[pos++] = 0xAA;
    buf[pos++] = 0x01;
    pos += pb_encode_varint(&buf[pos], (uint32_t)ilen);
    if (pos + ilen > bufsize) return 0;
    memcpy(&buf[pos], inner, ilen);
    pos += ilen;

    return pos;
}

/*
 * Build: Envelope { protocol_version=1, seq=N, system_status={…} }
 *
 * Wire layout:
 *   Field 1   (protocol_version): tag=0x08, varint=1
 *   Field 2   (seq):              tag=0x10, varint=N
 *   Field 10  (system_status):    tag=(10<<3)|2 = 82 → 0x52, len, sub-msg
 *
 * SystemStatus is the per-tick "live values" envelope — UI pages gate on
 * it and re-render every push. It contains physical-quantity floats
 * (plenum_temp, outside_temp, …), formatted strings (fan_speed,
 * cool_output, …), the OperatingMode enum, and the burner cure state.
 *
 * Phase C population (May 2026) — sensor floats only.
 * --------------------------------------------------
 * The orbit at slot 0 (STORAGE per lwip_smoke.c::kOrbits[]) hosts the
 * analog-sensor RS-485 boards. Mirror the AS2 default layout from
 * `docs/legacy_AS2_reference/Application/Analog_Input.h`:
 *
 *   Board 0 (DEFAULT_TEMP_BOARD)  →  HR[200..203] = sensorHr[0..3]
 *      ch 0 = SENSOR_PLENUM_TEMP_1
 *      ch 1 = SENSOR_PLENUM_TEMP_2
 *      ch 2 = SENSOR_OUTSIDE_TEMP
 *      ch 3 = SENSOR_RETURN_TEMP
 *
 *   Board 1 (DEFAULT_HUMID_BOARD) →  HR[204..207] = sensorHr[4..7]
 *      ch 0 = SENSOR_OUTSIDE_HUMID
 *      ch 1 = SENSOR_PLENUM_HUMID
 *      ch 2 = SENSOR_RETURN_HUMID
 *      ch 3 = SENSOR_CO2
 *
 * Wire encoding (from orbit_server/adc_convert.c, mirrored by the
 * sensor-injector): TEMP/HUMID = round(eng × 10) int16; CO2 = round(ppm)
 * int16; UNDEF = 0x7FFF. The bridge is a transparent passthrough — all
 * scaling is done HERE so the UI never has to know HR units.
 *
 * Plenum temp = average of the two valid PLENUM_TEMP_1/2 readings, per
 * AS2 `Analog_Input.c::CalculatePlenumTemp()`. Undef sensors are simply
 * not emitted (proto3 zero-suppression renders the field as 0.0 → '--').
 *
 * Strings (fan_speed, cool_output, cool_label, burner_output),
 * current_mode, cure_state, and the calc_humid/start_temp/remote_*
 * floats remain empty/zero until the equipment-state machine + cure
 * controller are ported (next phases). Pages that need those fields
 * will keep rendering placeholders until then.
 *
 * Phase D will replace the fixed orbit-slot-0 + AS2-default mapping
 * with a lookup driven by `IoConfig` once the OSPI-persisted analog-
 * board topology lands. Do NOT promote this default mapping into a
 * runtime constant — the moment IoConfig is real, this whole helper
 * becomes a lookup loop.
 */

/* Encode a single fixed32 float field into `buf` (proto3 zero-suppress:
 * caller skips when value is undef). Returns bytes written. */
static size_t pb_encode_float_field(uint8_t *buf, uint32_t field_no, float v)
{
    /* Tag byte = (field_no << 3) | 5 (wire type 5 = fixed32). All
     * SystemStatus float fields are 1..15 so the tag fits in one byte. */
    buf[0] = (uint8_t)((field_no << 3) | 5U);
    uint32_t bits;
    memcpy(&bits, &v, sizeof(bits));
    buf[1] = (uint8_t)(bits & 0xFFU);
    buf[2] = (uint8_t)((bits >>  8) & 0xFFU);
    buf[3] = (uint8_t)((bits >> 16) & 0xFFU);
    buf[4] = (uint8_t)((bits >> 24) & 0xFFU);
    return 5U;
}

/* AS2 sensor-board layout constants (Analog_Input.h). Local to this
 * helper so they don't bleed into the rest of the firmware as a global
 * "topology table" — Phase D replaces them with an IoConfig lookup. */
#define LP_SS_STORAGE_ORBIT_SLOT  0U
#define LP_SS_TEMP_BOARD_HR_BASE  0U   /* sensorHr[0..3] = HR[200..203] */
#define LP_SS_HUMID_BOARD_HR_BASE 4U   /* sensorHr[4..7] = HR[204..207] */
#define LP_SS_HR_UNDEF            0x7FFFU

static bool ss_decode_temp_x10(uint16_t raw, float *out)
{
    if (raw == LP_SS_HR_UNDEF) return false;
    *out = (float)(int16_t)raw / 10.0f;
    return true;
}

static bool ss_decode_humid_x10(uint16_t raw, float *out)
{
    if (raw == LP_SS_HR_UNDEF) return false;
    *out = (float)(int16_t)raw / 10.0f;
    return true;
}

static bool ss_decode_co2_ppm(uint16_t raw, float *out)
{
    if (raw == LP_SS_HR_UNDEF) return false;
    *out = (float)(int16_t)raw;
    return true;
}

/* AS2-faithful temperature handoff: HR 200+ on the orbit's wire is
 * always tenths-of-°C (NTC LUT output). The user's display unit lives
 * in BasicSetup.tempType (0=Fahrenheit, 1=Celsius). Mirror what
 * Analog_Input.c::ConvertToTemp() did in legacy AS2 — return the
 * value in user units so every consumer (UI, graphs, logs, exports)
 * just reads a plain number with no per-site conversion. */
static inline float ss_temp_in_user_units(float celsius)
{
    const LpSettingsData *s = LpSettings_DataGet();
    if (s != NULL && s->basic.temp_type == 0U /* Fahrenheit */) {
        return celsius * 1.8f + 32.0f;
    }
    return celsius;
}

/* Slave-mode outside-air override: when MasterSlaveSettings.mode == 2
 * AND a fresh RemoteOutsideAir cache entry exists, substitute the
 * cached values into sensorHr[OUTSIDE_TEMP] / sensorHr[OUTSIDE_HUMID]
 * for the storage orbit slot. Both `build_system_status_envelope` and
 * `build_orbit_sensor_bank_envelope` call this so the engine, UI,
 * logs, and any orbit-consumer downstream see the same value.
 *
 * Wire units of the cache (tenths-of-°C / tenths-of-%RH) match the
 * orbit's HR encoding bit-for-bit, so no conversion happens here.
 *
 * Master mode and Standalone mode no-op (return without touching
 * the sample). Empty / stale cache also no-ops. */
static void ss_apply_remote_outside_override(OrbitSample *sample, uint8_t slot)
{
    if (slot != LP_SS_STORAGE_ORBIT_SLOT) return;
    const LpSettingsData *s = LpSettings_DataGet();
    if (s == NULL || s->master_slave.mode != 2U /* Slave */) return;

    int16_t t_x10  = INT16_MIN;
    int16_t h_x10  = INT16_MIN;
    if (!LpRemoteOutside_GetX10(&t_x10, &h_x10)) return;

    if (t_x10 != INT16_MIN) {
        sample->sensorHr[LP_SS_TEMP_BOARD_HR_BASE  + 2] = (uint16_t)t_x10;
    }
    if (h_x10 != INT16_MIN) {
        sample->sensorHr[LP_SS_HUMID_BOARD_HR_BASE + 0] = (uint16_t)h_x10;
    }
}

static size_t build_system_status_envelope(uint8_t *buf, size_t bufsize,
                                           uint32_t seq)
{
    /* Build the inner SystemStatus body first into a scratch buffer,
     * then wrap with the envelope header. Sized for 8 floats × 5 B,
     * one current_mode varint, one start_temp float, the diagnostic
     * fields 18-23 (~20 B), plus the cool_output / cool_label /
     * burner_output strings (~30 B). 128 B leaves comfortable headroom. */
    uint8_t inner[128];
    size_t  ilen = 0;

    OrbitSample sample;
    bool have_sample =
        OrbitClient_GetSample(LP_SS_STORAGE_ORBIT_SLOT, &sample);
    if (have_sample) {
        ss_apply_remote_outside_override(&sample, LP_SS_STORAGE_ORBIT_SLOT);
    }

    if (have_sample) {
        float v;
        /* --- Temp board (HR 200..203) --- */
        bool pt1_ok = ss_decode_temp_x10(sample.sensorHr[LP_SS_TEMP_BOARD_HR_BASE + 0], &v);
        float pt1 = pt1_ok ? v : 0.0f;
        bool pt2_ok = ss_decode_temp_x10(sample.sensorHr[LP_SS_TEMP_BOARD_HR_BASE + 1], &v);
        float pt2 = pt2_ok ? v : 0.0f;

        /* plenum_temp (field 1) — AS2 CalculatePlenumTemp(): average of
         * valid PT1/PT2; if only one valid use that one; if neither,
         * skip (proto3 default 0.0 → UI renders '--'). Emit in user
         * units so the SystemStatus path matches the AS2-style
         * Sensor.Value contract used everywhere else. */
        if (pt1_ok && pt2_ok) {
            ilen += pb_encode_float_field(&inner[ilen], 1U,
                ss_temp_in_user_units((pt1 + pt2) * 0.5f));
        } else if (pt1_ok) {
            ilen += pb_encode_float_field(&inner[ilen], 1U,
                ss_temp_in_user_units(pt1));
        } else if (pt2_ok) {
            ilen += pb_encode_float_field(&inner[ilen], 1U,
                ss_temp_in_user_units(pt2));
        }

        /* outside_temp (field 3) */
        if (ss_decode_temp_x10(sample.sensorHr[LP_SS_TEMP_BOARD_HR_BASE + 2], &v)) {
            ilen += pb_encode_float_field(&inner[ilen], 3U,
                ss_temp_in_user_units(v));
        }
        /* return_temp (field 9) */
        if (ss_decode_temp_x10(sample.sensorHr[LP_SS_TEMP_BOARD_HR_BASE + 3], &v)) {
            ilen += pb_encode_float_field(&inner[ilen], 9U,
                ss_temp_in_user_units(v));
        }

        /* --- Humid board (HR 204..207) --- */
        /* outside_humid (field 5) */
        if (ss_decode_humid_x10(sample.sensorHr[LP_SS_HUMID_BOARD_HR_BASE + 0], &v)) {
            ilen += pb_encode_float_field(&inner[ilen], 5U, v);
        }
        /* plenum_humid (field 2) */
        if (ss_decode_humid_x10(sample.sensorHr[LP_SS_HUMID_BOARD_HR_BASE + 1], &v)) {
            ilen += pb_encode_float_field(&inner[ilen], 2U, v);
        }
        /* return_humid (field 8) */
        if (ss_decode_humid_x10(sample.sensorHr[LP_SS_HUMID_BOARD_HR_BASE + 2], &v)) {
            ilen += pb_encode_float_field(&inner[ilen], 8U, v);
        }
        /* co2_level (field 10) */
        if (ss_decode_co2_ppm(sample.sensorHr[LP_SS_HUMID_BOARD_HR_BASE + 3], &v)) {
            ilen += pb_encode_float_field(&inner[ilen], 10U, v);
        }
    }

    /* current_mode (field 16) — AS2 UI_* numbering (UI_STANDBY=2,
     * UI_REFRIG=5, UI_HEATING=7, UI_FAILURE=21, …, see States.h). The
     * proto enum `OperatingMode` only covers 8 values and uses
     * different numbering — it's effectively wrong; the UI's
     * modeToColor table at +layout.svelte:72-99 uses AS2 UI_* and
     * goes up to 24. We emit the raw AS2 number on the wire as
     * varint(int32) which the UI reads as a plain number. The proto
     * enum will be replaced with a uint32 in a polish phase. */
    {
        unsigned char m = lp_engine_get_current_mode();
        /* field 16, wire type 0 (varint): tag = (16<<3)|0 = 0x80 0x01 */
        inner[ilen++] = 0x80;
        inner[ilen++] = 0x01;
        ilen += pb_encode_varint(&inner[ilen], m);
    }

    /* start_temp (field 7) — "Cooling Available Temperature" on the
     * home page. Computed in lp_engine_shim::lp_calc_cooling_available
     * (re-impl of legacy SetStartTemp + WetBulbDepression). Already in
     * user units (°F or °C) since OutsideTemp / RefTemp / Plenum.TempSet
     * all live in user units. Skip when undefined → UI shows '--'. */
    {
        extern float StartTemp;
        /* SENSOR_VAL_UNDEFINED = 0x7FFF (Analog_Input.h sentinel). */
        if (StartTemp != (float)0x7FFF) {
            ilen += pb_encode_float_field(&inner[ilen], 7U, StartTemp);
        }
    }

    /* ── Diagnostic fields (CPLD-elimination audit, May 2026) ──────
     * These let us see why the state machine picks one mode over another
     * without flashing diag-only firmware. Force-emit (varint key + value
     * with no zero-suppression) so a value of 0 (ST_NONE / RC_OFF / 0%)
     * is still present on the wire.
     *
     *   18 system_state    — raw SystemState enum (ST_*)
     *   19 run_clock_now   — RunClockMode() return (RC_*)
     *   20 pwm_doors_pct   — fresh-air damper PWM %
     *   21 pwm_refrig_pct  — refrigeration PWM %
     *   22 estop_active    — Orbit MAIN E-Stop input
     *   23 ro_bits         — packed RemoteOff state for the 4 equipment
     *                        whose CPLD switch we're about to delete:
     *                          [3:0]   RO_FAN
     *                          [7:4]   RO_REFRIGERATION
     *                          [11:8]  RO_CLIMACELL
     *                          [15:12] RO_HUMIDIFIER1
     *
     * All externs are satisfied by Platform .c TUs already linked into
     * the LP image (nova_states.obj, nova_pwm.obj, hal_orbit.obj). */
    {
        unsigned int doors_raw = PwmChannel[PWM_DOORS].Output;
        unsigned int refr_raw  = PwmChannel[PWM_REFRIGERATION].Output;
        uint32_t doors_pct = (doors_raw >= PWM_MIN_VALUE && doors_raw <= PWM_MAX_VALUE)
            ? (uint32_t)(((doors_raw - PWM_MIN_VALUE) * 100U + (PWM_RANGE / 2U)) / PWM_RANGE)
            : 0U;
        uint32_t refr_pct = (refr_raw >= PWM_MIN_VALUE && refr_raw <= PWM_MAX_VALUE)
            ? (uint32_t)(((refr_raw - PWM_MIN_VALUE) * 100U + (PWM_RANGE / 2U)) / PWM_RANGE)
            : 0U;

        uint32_t ro_bits = 0U;
        ro_bits |= ((uint32_t)Settings.RemoteOff[RO_FAN]           & 0x0F) <<  0;
        ro_bits |= ((uint32_t)Settings.RemoteOff[RO_REFRIGERATION] & 0x0F) <<  4;
        ro_bits |= ((uint32_t)Settings.RemoteOff[RO_CLIMACELL]     & 0x0F) <<  8;
        ro_bits |= ((uint32_t)Settings.RemoteOff[RO_HUMIDIFIER1]   & 0x0F) << 12;

        /* Inner buffer is 64 B; current usage ~43 B; we add ~20 B max.
         * Hard-cap each field with a bounds check; if any one would
         * overflow, skip the rest rather than corrupt the envelope. */
        #define DIAG_EMIT_VARINT(key0, key1, val) \
            do { \
                if (ilen + 11U > sizeof(inner)) goto diag_done; \
                inner[ilen++] = (key0); inner[ilen++] = (key1); \
                ilen += pb_encode_varint(&inner[ilen], (val)); \
            } while (0)

        DIAG_EMIT_VARINT(0x90, 0x01, (uint32_t)(unsigned char)SystemState);
        DIAG_EMIT_VARINT(0x98, 0x01, (uint32_t)RunClockMode());
        DIAG_EMIT_VARINT(0xA0, 0x01, doors_pct);
        DIAG_EMIT_VARINT(0xA8, 0x01, refr_pct);
        DIAG_EMIT_VARINT(0xB0, 0x01, (uint32_t)Nova_GetEStopActive()); /* field 22 */
        DIAG_EMIT_VARINT(0xB8, 0x01, ro_bits);
        #undef DIAG_EMIT_VARINT
diag_done: ;
    }

    /* cool_label / cool_output / burner_output (fields 13, 12, 14) —
     * legacy AS2 `UI_Messages.c::FormatCoolingOutput()` re-impl.
     *
     * The home page picks the output-row label from `cool_label`:
     *   "0" → "Cooling Output"        (fresh-air / damper)
     *   "1" → "Refrigeration Output"  (refrig active)
     *   "2" → "Burner Output"         (Onion cure path)
     *
     * `cool_output` is the % string for whichever PWM the state is
     * driving; `burner_output` only appears in *_DEHUMID states (the
     * burner runs concurrently for moisture removal).
     *
     * Onion + cure-auto burner-label path is intentionally omitted
     * until the cure controller is fully ported — we never enter that
     * state today on Storage builds, and the AS2 logic depends on
     * SW_CURE_AUTO / SW_FRESHAIR_* CPLD switches we're in the middle
     * of eliminating. Until then the label correctly reverts to "0".
     *
     * Buffer accounting: each string field = tag(1) + len(1) +
     * payload(<=8). Combined worst case <30 B; current `inner` usage
     * after diag block is ~63 B; sized at 96 B → 33 B headroom OK. */
    {
        unsigned int doors_raw = PwmChannel[PWM_DOORS].Output;
        unsigned int refr_raw  = PwmChannel[PWM_REFRIGERATION].Output;
        unsigned int burn_raw  = PwmChannel[PWM_BURNER].Output;
        uint8_t doors_pct = pwm_raw_to_pct(doors_raw);
        uint8_t refr_pct  = pwm_raw_to_pct(refr_raw);
        uint8_t burn_pct  = pwm_raw_to_pct(burn_raw);

        const unsigned char st = (unsigned char)SystemState;
        const bool is_refrig =
               (st == ST_REFRIG)
            || (st == ST_REFRIGDEHUMID)
            || (st == ST_DEFROST);
        const bool is_cooling =
               (st == ST_COOLING)
            || (st == ST_COOLDEHUMID);
        const bool emit_burner =
               (st == ST_REFRIGDEHUMID)
            || (st == ST_COOLDEHUMID);

        const char *cool_label = is_refrig ? "1" : "0";
        char        cool_buf[8];
        const char *cool_str   = "Off";
        size_t      cool_len   = 3U;
        if (is_refrig) {
            cool_len = (size_t)snprintf(cool_buf, sizeof(cool_buf),
                                        "%u", (unsigned)refr_pct);
            cool_str = cool_buf;
        } else if (is_cooling) {
            cool_len = (size_t)snprintf(cool_buf, sizeof(cool_buf),
                                        "%u", (unsigned)doors_pct);
            cool_str = cool_buf;
        }

        char        burn_buf[8];
        const char *burn_str   = NULL;
        size_t      burn_len   = 0U;
        if (emit_burner) {
            burn_len = (size_t)snprintf(burn_buf, sizeof(burn_buf),
                                        "%u", (unsigned)burn_pct);
            burn_str = burn_buf;
        }

        /* Hard-cap with bounds checks; if any field would overflow,
         * skip the rest rather than corrupt the envelope. */
        #define EMIT_STR(tag_byte, str, len) \
            do { \
                if (ilen + 2U + (len) > sizeof(inner)) goto str_done; \
                inner[ilen++] = (uint8_t)(tag_byte); \
                inner[ilen++] = (uint8_t)(len); \
                memcpy(&inner[ilen], (str), (len)); \
                ilen += (len); \
            } while (0)

        /* field 12 cool_output: tag = (12<<3)|2 = 0x62 */
        EMIT_STR(0x62, cool_str, cool_len);
        /* field 13 cool_label:  tag = (13<<3)|2 = 0x6A */
        EMIT_STR(0x6A, cool_label, 1U);
        /* field 14 burner_output: tag = (14<<3)|2 = 0x72 */
        if (burn_str != NULL) {
            EMIT_STR(0x72, burn_str, burn_len);
        }

        /* field 11 fan_speed: tag = (11<<3)|2 = 0x5A — 1:1 port of
         * legacy `UI_Messages.c::FormatFanSpeed()`. The PWM channel
         * routing (which physical 4-20 mA AO carries the fan signal)
         * is operator-configurable on the Level 2 PWM page; this
         * string is just the engine's commanded duty for PWM_FAN.
         * Reports "Manual" when operator override active, "Off" when
         * fan equipment is disabled or the system is in failure mode
         * (the legacy comment notes the failure case keeps the close-
         * pulse-door output asserted but the board cuts output power,
         * so the UI must say "Off"). */
        {
            char        fan_buf[8];
            const char *fan_str;
            size_t      fan_len;
            extern char CurrentMode;
            uint8_t fan_pct = pwm_raw_to_pct(PwmChannel[PWM_FAN].Output);

            if (CurrentMode == UI_FAILURE) {
                fan_str = "Off"; fan_len = 3U;
            } else if (CheckInputs(SW_FAN_AUTO) && CheckOutputs(EQ_FAN)) {
                fan_len = (size_t)snprintf(fan_buf, sizeof(fan_buf),
                                           "%u", (unsigned)fan_pct);
                fan_str = fan_buf;
            } else if (CheckInputs(SW_FAN_MANUAL) && CheckOutputs(EQ_FAN)) {
                fan_str = "Manual"; fan_len = 6U;
            } else {
                fan_str = "Off"; fan_len = 3U;
            }
            EMIT_STR(0x5A, fan_str, fan_len);
        }
        #undef EMIT_STR
str_done: ;
    }


    size_t pos = 0;

    /* Envelope.protocol_version = 1 */
    buf[pos++] = 0x08;
    pos += pb_encode_varint(&buf[pos], NOVA_PROTOCOL_VERSION);

    /* Envelope.seq */
    buf[pos++] = 0x10;
    pos += pb_encode_varint(&buf[pos], seq);

    /* Envelope.system_status (field 10, length-delimited).
     * SystemStatus fits in one tag byte AND its body fits in <128 B so
     * the inner-length varint is always one byte. */
    if (pos + 2U + ilen > bufsize) return 0;
    buf[pos++] = 0x52;
    buf[pos++] = (uint8_t)ilen;
    if (ilen > 0U) {
        memcpy(&buf[pos], inner, ilen);
        pos += ilen;
    }

    return pos;
}

/*
 * Build one SensorReading sub-message at *buf. Always emits index,
 * value, and valid (uses pb_uint32_force-equivalent for index since
 * 0 is a valid sensor index). Returns bytes written.
 *
 * Wire layout (SensorReading):
 *   Field 1 (index, uint32 varint): tag=0x08
 *   Field 2 (value, float fixed32):  tag=0x15, 4 LE bytes
 *   Field 3 (valid, bool varint):    tag=0x18
 */
static size_t build_sensor_reading(uint8_t *buf, uint32_t index, float value,
                                   bool valid)
{
    size_t pos = 0;

    /* index (field 1, varint) — force-emit, 0 is valid */
    buf[pos++] = 0x08;
    pos += pb_encode_varint(&buf[pos], index);

    /* value (field 2, float fixed32 LE) */
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    buf[pos++] = 0x15;
    buf[pos++] = (uint8_t)(bits & 0xFF);
    buf[pos++] = (uint8_t)((bits >> 8) & 0xFF);
    buf[pos++] = (uint8_t)((bits >> 16) & 0xFF);
    buf[pos++] = (uint8_t)((bits >> 24) & 0xFF);

    /* valid (field 3, bool varint) — only emit when true (proto3 default false) */
    if (valid) {
        buf[pos++] = 0x18;
        buf[pos++] = 0x01;
    }
    return pos;
}

/*
 * Build: Envelope { protocol_version=1, seq=N, sensor_data={…} }
 *
 * Wire layout:
 *   Field 1   (protocol_version): tag=0x08, varint=1
 *   Field 2   (seq):              tag=0x10, varint=N
 *   Field 13  (sensor_data):      tag=(13<<3)|2 = 106 → 0x6A, len, sub-msg
 *     SensorData.temperatures (field 1, repeated SensorReading)
 *     SensorData.humidities   (field 2, repeated SensorReading)
 *
 * Source: every orbit's cached HR[200..263] register window. Each
 * non-zero register becomes one SensorReading with:
 *   - index = orbit_slot * ORBIT_SENSOR_HR_COUNT + hr_offset (stable
 *     SID across reboots; legacy AS2 used board.Address*8+s, this
 *     scheme is the orbit-equivalent and matches the SID space the UI
 *     pile-temps page already understands once the IO config arrives).
 *   - value = raw_hr / 10.0f (orbit sim convention: temp/humid value
 *     ×10 as int16; see orbit-simulator/src/adcConversion.ts L112).
 *   - valid = true.
 *
 * Phase B caveat: without IoConfig (Phase C), we cannot tell temps
 * from humids. Every reading currently goes into the `temperatures`
 * repeated field. The UI's pile-temps page will show humid-sensor
 * registers mixed in. This is acceptable for the gate-clearing phase;
 * Phase C splits them once the per-sensor TYPE is known. Do NOT add
 * an "if (offset >= 32)" hack to fake the split — the sensor-board
 * does not partition HR space that way; the partition is per-sensor
 * configuration that lives in IoConfig.
 *
 * Skip readings where raw_hr == 0 (no sensor wired) or 0xFFFF (sensor
 * disconnected fault code from the sensor board).
 *
 * Returns 0 if no orbit has produced a sample yet OR all readings
 * filtered out (caller skips TX silently).
 */
static size_t build_sensor_data_envelope(uint8_t *buf, size_t bufsize,
                                         uint32_t seq)
{
    uint8_t inner[1024];   /* up to 5 orbits × 64 readings × ~10B each */
    size_t ilen = 0;
    size_t emitted = 0;

    size_t orbit_count = OrbitClient_Count();
    OrbitSample sample;

    for (size_t slot = 0; slot < orbit_count; slot++) {
        if (!OrbitClient_GetSample((uint8_t)slot, &sample)) continue;

        for (size_t off = 0; off < ORBIT_SENSOR_HR_COUNT; off++) {
            uint16_t raw = sample.sensorHr[off];
            if (raw == 0 || raw == 0xFFFFU) continue;   /* unwired / fault */

            uint32_t sid = (uint32_t)slot * ORBIT_SENSOR_HR_COUNT
                         + (uint32_t)off;
            float value = (float)(int16_t)raw / 10.0f;

            uint8_t reading[16];
            size_t rlen = build_sensor_reading(reading, sid, value, true);

            /* Wrap with field 1 (temperatures) length-delimited tag.
             * See header comment for why everything goes into field 1
             * until Phase C IO config lands. */
            if (ilen + 2 + rlen > sizeof(inner)) goto done;
            inner[ilen++] = 0x0A;
            ilen += pb_encode_varint(&inner[ilen], (uint32_t)rlen);
            memcpy(&inner[ilen], reading, rlen);
            ilen += rlen;
            emitted++;
        }
    }

done:
    if (emitted == 0) return 0;

    size_t pos = 0;

    /* Envelope.protocol_version = 1 */
    buf[pos++] = 0x08;
    pos += pb_encode_varint(&buf[pos], NOVA_PROTOCOL_VERSION);

    /* Envelope.seq */
    buf[pos++] = 0x10;
    pos += pb_encode_varint(&buf[pos], seq);

    /* Envelope.sensor_data (field 13, length-delimited) */
    buf[pos++] = 0x6A;
    pos += pb_encode_varint(&buf[pos], (uint32_t)ilen);
    if (pos + ilen > bufsize) return 0;
    memcpy(&buf[pos], inner, ilen);
    pos += ilen;

    return pos;
}

/*
 * Phase C — IoConfig / IoDefinition / AvailableIo skeleton emitters.
 *
 * Each of these envelopes carries the legacy AS2 equipment ↔ port
 * mapping that the UI's IO Config + Equipment pages render. Without
 * OSPI persistence (Phase D) this firmware has no Settings.IoEntry[]
 * or Settings.AnalogBoard[] to source from, so we emit EMPTY bodies:
 *
 *   IoConfig      → outputMap=[], inputMap=[], board_count=0
 *   IoDefinition  → entries=[]
 *   AvailableIo   → output_pins=[], input_pins=[]
 *
 * The bridge decoders set `this.ioConfig`, `this.ioDefinition`,
 * `this.availableIo` to non-null empty objects, which clears the UI's
 * "haven't received yet" gates while honestly reporting "no IO
 * configured here". Once Phase D lands and Settings.IoEntry[] exists,
 * each builder gets populated for-real from the persisted struct.
 *
 * AnalogBoard (env field 26) is intentionally NOT emitted: legacy
 * NovaMsg_SendAnalogBoard iterates only `Settings.AnalogBoard[b].Present`
 * boards. With zero present boards, the correct wire behavior is
 * silence, not an empty AnalogBoard{address=0, sensors=[]} which the
 * bridge would cache as a phantom board at address 0.
 *
 * Cadence: same as ServiceInfo (every 30s, one tick offset) — these
 * are config envelopes that don't change without user action.
 */
static size_t build_io_config_envelope(uint8_t *buf, size_t bufsize,
                                       uint32_t seq)
{
    /* IoConfig (envelope field 24, tag = (24<<3)|2 = 194 → 0xC2 0x01).
     * Body comes from LpSettings_BuildIoConfigBody — emits packed
     * output_map + input_map only when non-empty (proto3 zero-suppress).
     * Empty body → zero-length sub-msg, which still clears the UI's
     * "haven't received yet" gate without inventing any state. */
    static uint8_t inner[2560];
    size_t ilen = LpSettings_BuildIoConfigBody(inner, sizeof(inner));

    if (bufsize < 8 + ilen) return 0;
    size_t pos = 0;
    buf[pos++] = 0x08;
    pos += pb_encode_varint(&buf[pos], NOVA_PROTOCOL_VERSION);
    buf[pos++] = 0x10;
    pos += pb_encode_varint(&buf[pos], seq);

    buf[pos++] = 0xC2;
    buf[pos++] = 0x01;
    pos += pb_encode_varint(&buf[pos], (uint32_t)ilen);
    if (ilen > 0U) {
        memcpy(&buf[pos], inner, ilen);
        pos += ilen;
    }
    return pos;
}

/*
 * AccountSettings envelope (envelope field 29, tag = (29<<3)|2 = 234
 * → 0xEA 0x01). Body = repeated UserAccount + count + password_defined.
 * Inner can reach ~480 B (16 users × 30 B), so use a dedicated buffer
 * rather than the 384 B settings_envelope helper.
 */
static size_t build_account_envelope(uint8_t *buf, size_t bufsize,
                                     uint32_t seq)
{
    static uint8_t inner[768];
    size_t ilen = LpSettings_BuildAccountBody(inner, sizeof(inner));

    if (bufsize < 8 + ilen) return 0;
    size_t pos = 0;
    buf[pos++] = 0x08;
    pos += pb_encode_varint(&buf[pos], NOVA_PROTOCOL_VERSION);
    buf[pos++] = 0x10;
    pos += pb_encode_varint(&buf[pos], seq);

    buf[pos++] = 0xEA;
    buf[pos++] = 0x01;
    pos += pb_encode_varint(&buf[pos], (uint32_t)ilen);
    if (ilen > 0U) {
        memcpy(&buf[pos], inner, ilen);
        pos += ilen;
    }
    return pos;
}

static size_t build_io_definition_envelope(uint8_t *buf, size_t bufsize,
                                           uint32_t seq)
{
    /* IoDefinition (envelope field 25). Body = repeated IoEntry, one
     * sub-msg per equipment slot. Worst case 58 entries × ~41 B = ~2.4 KB
     * — give the inner buffer comfortable headroom. */
    static uint8_t inner[3072];
    size_t ilen = LpSettings_BuildIoDefinitionBody(inner, sizeof(inner));

    if (bufsize < 8U + 4U + ilen) return 0;
    size_t pos = 0;

    buf[pos++] = 0x08;
    pos += pb_encode_varint(&buf[pos], NOVA_PROTOCOL_VERSION);
    buf[pos++] = 0x10;
    pos += pb_encode_varint(&buf[pos], seq);

    /* Envelope.io_definition (field 25): tag = (25<<3)|2 = 202 → 0xCA 0x01 */
    buf[pos++] = 0xCA;
    buf[pos++] = 0x01;
    pos += pb_encode_varint(&buf[pos], (uint32_t)ilen);
    if (ilen > 0U) {
        memcpy(&buf[pos], inner, ilen);
        pos += ilen;
    }
    return pos;
}

static size_t build_available_io_envelope(uint8_t *buf, size_t bufsize,
                                          uint32_t seq)
{
    /* Empty AvailableIo sub-msg (Phase C — no GPIO map yet). */
    if (bufsize < 8) return 0;
    size_t pos = 0;

    buf[pos++] = 0x08;
    pos += pb_encode_varint(&buf[pos], NOVA_PROTOCOL_VERSION);
    buf[pos++] = 0x10;
    pos += pb_encode_varint(&buf[pos], seq);

    /* Envelope.available_io (field 27): tag = (27<<3)|2 = 218 → 0xDA 0x01 */
    buf[pos++] = 0xDA;
    buf[pos++] = 0x01;
    buf[pos++] = 0x00;
    return pos;
}

/*
 * Phase E1 — real EquipmentStatus emitter (envelope field 11).
 *
 * Wire shape (equipment.proto :: EquipmentStatus, EquipState):
 *   Envelope.equipment_status (field 11, length-delimited) =
 *     repeated EquipState items (field 1) where each EquipState is:
 *       1 (varint, force) eq_index   — EQUIPMENT_IO ordinal
 *       2 (bool)          output_on  — physical relay state
 *       3 (enum)          remote_off — RemoteOffState (0..3)
 *       4 (enum)          alarm      — AlarmSeverity (suppressed=0)
 *       5 (string)        label      — sent separately in IoDefinition
 *
 * What we emit today (0.A.17):
 *   - One entry per eq slot whose io_definition.entries[i].populated is
 *     set AND whose mode matches the current SystemMode (so Onion-only
 *     equipment doesn't crowd a Potato system's UI, and vice versa).
 *   - `output_on` (field 2) = real DO readback from the STORAGE orbit's
 *     cached FC01 bitmap when io_config.output_map[i] points to a
 *     wired coil (port_id 1..11 → bit 0..10).  Falls back to
 *     (remote_off == REMOTE_MANUAL) for unmapped slots OR while the
 *     orbit has not yet completed a successful FC01+FC02 cycle
 *     (sample.io_valid == false).
 *   - `remote_off` (field 3) = the LP's eq-indexed
 *     s_data.remote_off.state[i].
 *   - `alarm` (field 4) = always 0 (Phase F+ adds the alarm subsystem).
 *   - `input_on` (field 6, NEW in 0.A.17) = real DI readback from the
 *     STORAGE orbit's cached FC02 bitmap via io_config.input_map[i]
 *     (port_id 1..11 → DI 0..10).  False when unmapped or sample
 *     invalid — never falls back to remote_off (an unmapped equipment
 *     genuinely has no input contact).
 *
 * Performance note: ~30 populated equipment entries × ~10 B per
 * EquipState submsg ≈ 300 B body.  Inner buffer sized at 1 KB for
 * headroom — one full populated table is ~64 × 12 B = 768 B worst-
 * case (every slot non-zero).  Outer envelope adds ~10 B header. */
static size_t build_equipment_status_envelope(uint8_t *buf, size_t bufsize,
                                              uint32_t seq)
{
    const LpSettingsData *s = LpSettings_DataGet();
    if (s == NULL) return 0;

    /* Build the body (repeated EquipState items on field 1). */
    static uint8_t body[1024];
    size_t bpos = 0;

    /* Snapshot the STORAGE orbit's cached DI/DO bitmaps once per
     * envelope build (cheap — a single mutex take inside the
     * orbit-client API).  When the orbit has never completed a
     * successful FC01+FC02 cycle (sample.io_valid == false) we fall
     * back to the REMOTE_MANUAL-derived output_on (preserves the
     * pre-Phase-E3 behaviour for cold-boot before the first poll).
     *
     * port_id encoding (mirrors equipment_output_sync_task above):
     *   port_id  0          → unassigned (no real I/O)
     *   port_id  1..11      → STORAGE board (orbit 0), bit = port_id - 1
     *   port_id ≥ 12        → reserved future expansion (skip)
     *
     * input_map[]/output_map[] are eq-indexed; the ith slot belongs
     * to io_definition.entries[i]. */
    OrbitSample storage_sample;
    bool storage_io_valid = false;
    uint16_t storage_do_bitmap = 0U;
    uint16_t storage_di_bitmap = 0U;
    if (OrbitClient_GetSample(0U /* STORAGE */, &storage_sample)) {
        storage_io_valid  = storage_sample.io_valid;
        storage_do_bitmap = storage_sample.do_bitmap;
        storage_di_bitmap = storage_sample.di_bitmap;
    }

    /* Map LpBasicSetup.system_mode (0=Potato, 1=Onion) to the IoEntry
     * mode filter (M_NONE=0, M_POTATO=1, M_ONION=2, M_BEE=3, M_ALL=4 —
     * see s_io_defaults table in lp_settings.c).  M_NONE is never
     * visible. */
    enum { MODE_NONE = 0, MODE_POTATO = 1, MODE_ONION = 2, MODE_BEE = 3, MODE_ALL = 4 };
    uint8_t cur_mode_filter = (s->basic.system_mode == 1U) ? MODE_ONION
                            : (s->basic.system_mode == 2U) ? MODE_BEE
                                                            : MODE_POTATO;

    for (uint32_t i = 0; i < LP_IO_ENTRIES_MAX; i++) {
        const LpIoEntry *e = &s->io_definition.entries[i];
        if (!e->populated) continue;
        /* Skip entries that don't apply to the current crop mode.
         * MODE_ALL always applies; MODE_NONE never does. */
        if (e->mode == MODE_NONE) continue;
        if (e->mode != MODE_ALL && e->mode != cur_mode_filter) continue;

        uint8_t  ro = s->remote_off.state[i];

        /* output_on: real DO readback when wired + sample valid;
         * otherwise REMOTE_MANUAL-derived fallback. */
        bool out_on = false;
        bool out_mapped = false;
        if (i < s->io_config.output_count) {
            const uint32_t port_id = s->io_config.output_map[i];
            if (port_id >= 1U && port_id <= 11U) {
                const uint8_t bit = (uint8_t)(port_id - 1U);
                if (bit < ORBIT_DO_COIL_COUNT) {
                    out_mapped = true;
                    if (storage_io_valid) {
                        out_on = ((storage_do_bitmap >> bit) & 0x1U) != 0U;
                    }
                }
            }
        }
        if (!out_mapped || !storage_io_valid) {
            out_on = (ro == 2U /* REMOTE_MANUAL */);
        }

        /* input_on: real DI readback via input_map[i].  False when
         * unmapped or the orbit has never published a valid sample. */
        bool in_on = false;
        if (storage_io_valid && i < s->io_config.input_count) {
            const uint32_t port_id = s->io_config.input_map[i];
            if (port_id >= 1U && port_id <= 11U) {
                const uint8_t bit = (uint8_t)(port_id - 1U);
                if (bit < ORBIT_DI_DISC_COUNT) {
                    in_on = ((storage_di_bitmap >> bit) & 0x1U) != 0U;
                }
            }
        }

        /* Inner EquipState submsg.  Worst case:
         *   eq_index : 1+5 = 6   (force-encoded)
         *   output_on: 1+1 = 2   (bool only emitted when true)
         *   remote_off: 1+1 = 2  (enum only emitted when non-zero)
         *   alarm    : skipped today
         *   input_on : 1+1 = 2   (bool only emitted when true)
         *   total    ≤ 12 B. */
        uint8_t item[16];
        size_t  ipos = 0;

        /* field 1 (eq_index) — force-encoded so slot 0 (EQ_FAN) is
         * decodable.  tag = (1<<3)|0 = 0x08. */
        item[ipos++] = 0x08;
        ipos += pb_encode_varint(&item[ipos], i);

        /* field 2 (output_on, bool) — proto3-suppressed when false. */
        if (out_on) {
            item[ipos++] = 0x10;   /* tag = (2<<3)|0 = 0x10 */
            item[ipos++] = 0x01;
        }

        /* field 3 (remote_off, enum) — proto3-suppressed when 0=AUTO. */
        if (ro != 0U) {
            item[ipos++] = 0x18;   /* tag = (3<<3)|0 = 0x18 */
            ipos += pb_encode_varint(&item[ipos], (uint32_t)ro);
        }

        /* field 6 (input_on, bool) — proto3-suppressed when false. */
        if (in_on) {
            item[ipos++] = 0x30;   /* tag = (6<<3)|0 = 0x30 */
            item[ipos++] = 0x01;
        }

        /* Wrap as field 1 (repeated submsg) of EquipmentStatus body.
         * tag = (1<<3)|2 = 0x0A. */
        if (bpos + 1U + 5U + ipos > sizeof(body)) break;  /* truncate gracefully */
        body[bpos++] = 0x0A;
        bpos += pb_encode_varint(&body[bpos], (uint32_t)ipos);
        memcpy(&body[bpos], item, ipos);
        bpos += ipos;
    }

    /* Synthetic Cure entry — virtual slot 63 in remote_off carries
     * the operator's Cure tile state (AUTO/OFF only).  No IoEntry
     * is populated for slot 63, so the loop above won't emit it.
     * Cure is meaningful only in Onion mode; suppress in other
     * modes so the UI doesn't render the tile when it's irrelevant.
     *
     * Always emit the eq_index (force-encoded so 0 would be valid,
     * though 63 isn't 0); only emit remote_off when non-AUTO.  No
     * output_on / input_on (no wired equipment behind cure). */
    if (cur_mode_filter == MODE_ONION) {
        uint8_t  cure_ro = s->remote_off.state[63];
        uint8_t  item[8];
        size_t   ipos = 0;
        item[ipos++] = 0x08;                  /* field 1 (eq_index) */
        ipos += pb_encode_varint(&item[ipos], 63U);
        if (cure_ro != 0U) {
            item[ipos++] = 0x18;              /* field 3 (remote_off) */
            ipos += pb_encode_varint(&item[ipos], (uint32_t)cure_ro);
        }
        if (bpos + 1U + 5U + ipos <= sizeof(body)) {
            body[bpos++] = 0x0A;
            bpos += pb_encode_varint(&body[bpos], (uint32_t)ipos);
            memcpy(&body[bpos], item, ipos);
            bpos += ipos;
        }
    }

    /* Wrap into the outer envelope. */
    if (bufsize < 8U + 4U + bpos) return 0;
    size_t pos = 0;
    buf[pos++] = 0x08;
    pos += pb_encode_varint(&buf[pos], NOVA_PROTOCOL_VERSION);
    buf[pos++] = 0x10;
    pos += pb_encode_varint(&buf[pos], seq);
    /* Envelope.equipment_status (field 11): tag = (11<<3)|2 = 90 = 0x5A */
    buf[pos++] = 0x5A;
    pos += pb_encode_varint(&buf[pos], (uint32_t)bpos);
    if (bpos > 0U) {
        memcpy(&buf[pos], body, bpos);
        pos += bpos;
    }
    return pos;
}

/*
 * WarningReport emitter (envelope tag 12).
 *
 * Walks the legacy WARNING[] backing store via WarningStatus(idx) and
 * emits one Warning submsg per active index. The bridge `decodeWarningReport`
 * (`novaDataStore.decodeWarningReport`) maps `code` → key via the UI-side
 * `WARNING_KEYS[]` table (`constellation-ui/src/lib/business/warningKeys.ts`)
 * which is a direct mirror of `WARNING_ITEMS` in `Platform/include/legacy/Warnings.h`.
 *
 * Wire layout per Warning submsg (alarms.proto:16):
 *   field 1 (code, varint)     — WARNING_ITEMS index, force-encoded so 0 survives
 *   field 2 (severity, varint) — non-zero (UI filters out severity==0)
 *   field 3 (message, string)  — omitted; UI falls back to DEFAULT_WARNING_TEXT[key]
 *   field 4 (eq_index, varint) — proto3-suppressed when 0
 *   field 5 (timestamp,varint) — omitted today
 *
 * The UI further filters w.code >= 0x100 so we cap the loop at NUM_WARNINGS
 * (currently <100). Sized buffers comfortably hold all ~95 codes.
 */
/* ─── PidLogRecord (envelope tag 72) ─────────────────────────────────
 * Pulled from the SPSC ring populated by `PIDLogWrite` in
 * `lp_engine_shim.c` (which is itself called from
 * `nova_controls.c::Pid()` whenever `Settings.Log.PID.{Door,Refrig}`
 * is enabled). Returns 0 if the ring is empty so the caller can
 * skip the wire write. Date/time strings come from the LP RTC; if
 * the RTC is not authoritative yet, we still emit the record with
 * empty strings — the bridge timestamps the row on insert anyway,
 * and the `epoch_sec`-derived strings would otherwise be meaningless
 * during cold-boot bursts. */
static size_t build_pid_log_record_envelope(uint8_t *buf, size_t bufsize,
                                            uint32_t seq)
{
    uint32_t epoch_sec, loop_index, sequence;
    int32_t  output;
    float    p_term, i_term, d_term, error_v;
    if (!lp_pidlog_drain_one(&epoch_sec, &loop_index, &p_term, &i_term,
                             &d_term, &output, &error_v, &sequence)) {
        return 0;
    }

    char date_str[16] = "";
    char time_str[16] = "";
    if (epoch_sec != 0U) {
        time_t t = (time_t)epoch_sec;
        struct tm *tm = localtime(&t);
        if (tm != NULL) {
            /* MM/DD/YYYY + HH:MM:SS to match legacy SD-card PIDLog. */
            (void)strftime(date_str, sizeof(date_str), "%m/%d/%Y", tm);
            (void)strftime(time_str, sizeof(time_str), "%H:%M:%S", tm);
        }
    }
    size_t dlen = strlen(date_str);
    size_t tlen = strlen(time_str);

    /* Inner: PidLogRecord body (≤ 80 B with full strings + 4 floats). */
    uint8_t inner[96];
    size_t  ilen = 0;

    /* field 1 date (string) */
    if (dlen > 0U) {
        inner[ilen++] = 0x0A;
        ilen += pb_encode_varint(&inner[ilen], (uint32_t)dlen);
        memcpy(&inner[ilen], date_str, dlen);
        ilen += dlen;
    }
    /* field 2 time (string) */
    if (tlen > 0U) {
        inner[ilen++] = 0x12;
        ilen += pb_encode_varint(&inner[ilen], (uint32_t)tlen);
        memcpy(&inner[ilen], time_str, tlen);
        ilen += tlen;
    }
    /* field 3 loop_index (varint). Force-encoded — loop 0 (PID_DOOR) is
     * a meaningful value, not "missing". */
    inner[ilen++] = 0x18;
    ilen += pb_encode_varint(&inner[ilen], loop_index);

    /* field 4-7 floats (always emitted, even if 0.0 — operator sees the
     * actual term values during a tune session). */
    ilen += pb_encode_float_field(&inner[ilen], 4U, p_term);
    ilen += pb_encode_float_field(&inner[ilen], 5U, i_term);
    ilen += pb_encode_float_field(&inner[ilen], 6U, d_term);
    ilen += pb_encode_float_field(&inner[ilen], 7U, error_v);

    /* field 8 output (int32 wire type 0). Force-encoded; sign-extend
     * via zigzag is NOT used (proto int32 uses standard varint with
     * 10-byte encoding for negative values). */
    inner[ilen++] = 0x40;
    /* int32 → 64-bit two's complement varint per proto wire spec. */
    {
        uint64_t v64 = (uint64_t)(int64_t)output;
        for (int i = 0; i < 10; i++) {
            uint8_t byte = (uint8_t)(v64 & 0x7FU);
            v64 >>= 7;
            if (v64 == 0U) {
                inner[ilen++] = byte;
                break;
            }
            inner[ilen++] = byte | 0x80U;
        }
    }

    /* field 9 sequence (varint, force-encoded). */
    inner[ilen++] = 0x48;
    ilen += pb_encode_varint(&inner[ilen], sequence);

    /* Wrap in Envelope. */
    if (bufsize < 8U + 4U + ilen) return 0;
    size_t pos = 0;
    buf[pos++] = 0x08;
    pos += pb_encode_varint(&buf[pos], NOVA_PROTOCOL_VERSION);
    buf[pos++] = 0x10;
    pos += pb_encode_varint(&buf[pos], seq);
    /* Envelope.pid_log_record (field 72): tag = (72<<3)|2 = 578 →
     * 2-byte varint 0x82 0x04. */
    buf[pos++] = 0x82;
    buf[pos++] = 0x04;
    pos += pb_encode_varint(&buf[pos], (uint32_t)ilen);
    memcpy(&buf[pos], inner, ilen);
    pos += ilen;
    return pos;
}

static size_t build_warning_report_envelope(uint8_t *buf, size_t bufsize,
                                            uint32_t seq)
{
    static uint8_t body[1024];
    size_t bpos = 0;

    for (uint32_t i = 0; i < (uint32_t)NUM_WARNINGS; i++) {
        char st = WarningStatus((WARNING_ITEMS)i);
        if (st == 0) continue;

        /* Inner Warning submsg, worst case ~10 B per entry. */
        uint8_t item[16];
        size_t  ipos = 0;

        /* field 1 (code) — force-encoded; tag = 0x08 */
        item[ipos++] = 0x08;
        ipos += pb_encode_varint(&item[ipos], i);

        /* field 2 (severity) — st is the FM_* code; non-zero by construction. */
        item[ipos++] = 0x10;
        ipos += pb_encode_varint(&item[ipos], (uint32_t)(uint8_t)st);

        /* Wrap as field 1 (repeated submsg) of WarningReport.
         * tag = (1<<3)|2 = 0x0A. */
        if (bpos + 1U + 5U + ipos > sizeof(body)) break;
        body[bpos++] = 0x0A;
        bpos += pb_encode_varint(&body[bpos], (uint32_t)ipos);
        memcpy(&body[bpos], item, ipos);
        bpos += ipos;
    }

    /* Outer envelope. */
    if (bufsize < 8U + 4U + bpos) return 0;
    size_t pos = 0;
    buf[pos++] = 0x08;
    pos += pb_encode_varint(&buf[pos], NOVA_PROTOCOL_VERSION);
    buf[pos++] = 0x10;
    pos += pb_encode_varint(&buf[pos], seq);
    /* Envelope.warning_report (field 12): tag = (12<<3)|2 = 98 = 0x62 */
    buf[pos++] = 0x62;
    pos += pb_encode_varint(&buf[pos], (uint32_t)bpos);
    if (bpos > 0U) {
        memcpy(&buf[pos], body, bpos);
        pos += bpos;
    }
    return pos;
}

/*
 * Phase C.5 — remaining no-persistence skeleton emitters.
 *
 * Each emits an empty sub-message body (zero-length). All seven bridge
 * decoders gracefully handle empty bodies: repeated fields collapse
 * to [], scalar fields default to 0 (proto3). The result on the bridge
 * is a non-null entry that clears the UI's "haven't received yet" gate
 * without inventing any state. Real population for each lands when the
 * underlying data source exists:
 *
 *   - EquipmentStatus (11)  → real emitter (Phase E1, 0.A.14) — see
 *                              build_equipment_status_envelope() above
 *   - WarningReport   (12)  → needs alarm subsystem (Phase F+)
 *   - Runtimes        (14)  → needs runtime accumulator (Phase F+)
 *   - HumidModes      (15)  → needs humid head/pump state machine
 *   - AuxSwitches     (16)  → needs aux switch input scanning
 *   - FanRuntime      (18)  → needs daily/total accumulator
 *   - SensorLabels    (28)  → needs IoConfig sensor labels (Phase D)
 *
 * Single-byte sub-msg tag formula: tag = (field << 3) | wire_type.
 * For length-delimited (wire_type=2):
 *   field 11 → 90 = 0x5A     field 14 → 114 = 0x72
 *   field 12 → 98 = 0x62     field 15 → 122 = 0x7A
 * For fields ≥16, use 2-byte varint tag:
 *   field 16 → 130 → 0x82 0x01
 *   field 18 → 146 → 0x92 0x01
 *   field 28 → 226 → 0xE2 0x01
 */
static size_t build_skeleton_envelope(uint8_t *buf, size_t bufsize,
                                      uint32_t seq, const uint8_t *tag,
                                      size_t taglen)
{
    if (bufsize < 8 + taglen) return 0;
    size_t pos = 0;

    buf[pos++] = 0x08;
    pos += pb_encode_varint(&buf[pos], NOVA_PROTOCOL_VERSION);
    buf[pos++] = 0x10;
    pos += pb_encode_varint(&buf[pos], seq);

    memcpy(&buf[pos], tag, taglen);
    pos += taglen;
    buf[pos++] = 0x00;   /* zero-length sub-msg */
    return pos;
}

/*
 * Phase D — settings_blob envelope (LP→bridge, field 32, length-delimited).
 *
 * Wire layout:
 *   Field 1 (protocol_version): tag=0x08, varint=1
 *   Field 2 (seq):              tag=0x10, varint=N
 *   Field 32 (settings_blob):   tag=(32<<3)|2 = 258 → 0x82 0x02
 *     Body = LpBankHeader (24B) + blob bytes — exactly the bytes
 *            LpSettings_TakeBlob() copies out of the active bank.
 *
 * Bridge logic:
 *   - On every receipt, write payload verbatim to
 *     ~/.constellation/lp_settings.bin (atomic rename).
 *   - On firmware-ready (DataLoadStatus(ready=true) + protocol_version=1),
 *     read that file and POST it back as envelope-91 to seed bank A
 *     before any subsequent SettingsUpdate writes hit the Settings struct.
 *
 * Returns 0 if no blob is dirty (nothing to send) or buf is too small.
 */
static size_t build_settings_blob_envelope(uint8_t *buf, size_t bufsize,
                                           uint32_t seq)
{
    /* Pull the blob out of lp_settings_store. Caps at the per-bank
     * worst case so the local stack buffer is bounded. */
    static uint8_t blob_scratch[24 + LP_SETTINGS_MAX_BLOB_SIZE];
    size_t blob_len = LpSettings_TakeBlob(blob_scratch, sizeof(blob_scratch));
    if (blob_len == 0) return 0;

    size_t pos = 0;
    buf[pos++] = 0x08;
    pos += pb_encode_varint(&buf[pos], NOVA_PROTOCOL_VERSION);
    buf[pos++] = 0x10;
    pos += pb_encode_varint(&buf[pos], seq);

    /* Envelope.settings_blob (field 32): tag = (32<<3)|2 = 258 → 0x82 0x02 */
    if (pos + 2 > bufsize) return 0;
    buf[pos++] = 0x82;
    buf[pos++] = 0x02;
    pos += pb_encode_varint(&buf[pos], (uint32_t)blob_len);
    if (pos + blob_len > bufsize) return 0;
    memcpy(&buf[pos], blob_scratch, blob_len);
    pos += blob_len;
    return pos;
}

/*
 * Panel-default snapshot envelope (LP→bridge, field 33, length-delimited).
 *
 * Wire layout matches build_settings_blob_envelope (header + blob); the
 * only difference is the destination file on the bridge side
 * (lp_panel_defaults.bin instead of lp_settings.bin) and the bank it
 * round-trips into (panel bank instead of active A/B). Bridge mirrors
 * exactly the same atomic-write + replay-on-firmware-ready scheme via
 * envelope field 92 instead of 91.
 *
 *   Field 33 (panel_blob): tag = (33<<3)|2 = 266 → 0x8A 0x02
 *
 * Returns 0 if no panel snapshot is dirty (nothing to send).
 */
static size_t build_panel_blob_envelope(uint8_t *buf, size_t bufsize,
                                        uint32_t seq)
{
    static uint8_t blob_scratch[24 + LP_SETTINGS_MAX_BLOB_SIZE];
    size_t blob_len = LpSettings_TakePanelBlob(blob_scratch,
                                               sizeof(blob_scratch));
    if (blob_len == 0) return 0;

    size_t pos = 0;
    buf[pos++] = 0x08;
    pos += pb_encode_varint(&buf[pos], NOVA_PROTOCOL_VERSION);
    buf[pos++] = 0x10;
    pos += pb_encode_varint(&buf[pos], seq);

    if (pos + 2 > bufsize) return 0;
    buf[pos++] = 0x8A;
    buf[pos++] = 0x02;
    pos += pb_encode_varint(&buf[pos], (uint32_t)blob_len);
    if (pos + blob_len > bufsize) return 0;
    memcpy(&buf[pos], blob_scratch, blob_len);
    pos += blob_len;
    return pos;
}

/*
 * Phase E — BasicSetup envelope (LP→bridge, field 20, length-delimited).
 *
 * Body is built by lp_settings::LpSettings_BuildBasicSetupBody which
 * mirrors `system.proto::BasicSetup` field-for-field. Empty/zero
 * fields are suppressed per proto3 so a fresh-from-defaults LP emits
 * an empty body and the bridge decoder produces all-blank cache
 * fields (UI shows defaults).
 *
 * Returns 0 if the encoded body would overflow buf.
 *
 * Source: proto/agristar/system.proto :: BasicSetup
 *         constellation-ui/server/src/novaDataStore.ts :: decodeBasicSetup
 */
static size_t build_basic_setup_envelope(uint8_t *buf, size_t bufsize,
                                         uint32_t seq)
{
    uint8_t inner[256];
    size_t  ilen = LpSettings_BuildBasicSetupBody(inner, sizeof(inner));

    size_t pos = 0;
    buf[pos++] = 0x08;
    pos += pb_encode_varint(&buf[pos], NOVA_PROTOCOL_VERSION);
    buf[pos++] = 0x10;
    pos += pb_encode_varint(&buf[pos], seq);

    /* Envelope.basic_setup (field 20): tag = (20<<3)|2 = 162 → 0xA2 0x01 */
    if (pos + 2U + 5U + ilen > bufsize) return 0;
    buf[pos++] = 0xA2;
    buf[pos++] = 0x01;
    pos += pb_encode_varint(&buf[pos], (uint32_t)ilen);
    memcpy(&buf[pos], inner, ilen);
    pos += ilen;
    return pos;
}

/*
 * Phase E — Settings-page envelope, generic builder for any oneof tag
 * in the 40-67 range. Mirrors `build_basic_setup_envelope` but takes
 * the envelope field number and the inner-body builder as parameters
 * so we don't copy-paste 25 near-identical functions.
 *
 * `tag1`/`tag2` are the precomputed varint bytes for `(field<<3)|2`
 * (all fields 40-67 fit in two bytes).
 */
typedef size_t (*LpBodyBuilder)(uint8_t *buf, size_t bufsize);

static size_t build_settings_envelope(uint8_t *buf, size_t bufsize,
                                      uint32_t seq,
                                      uint8_t tag1, uint8_t tag2,
                                      LpBodyBuilder body_fn)
{
    /* 384 B inner: covers Refrig (8 stages × ~10 B + 13 flat × ~6 B
     * ≈ 160 B) and leaves headroom for other multi-array pages. */
    uint8_t inner[384];
    size_t  ilen = body_fn(inner, sizeof(inner));

    size_t pos = 0;
    buf[pos++] = 0x08;
    pos += pb_encode_varint(&buf[pos], NOVA_PROTOCOL_VERSION);
    buf[pos++] = 0x10;
    pos += pb_encode_varint(&buf[pos], seq);

    if (pos + 2U + 5U + ilen > bufsize) return 0;
    buf[pos++] = tag1;
    buf[pos++] = tag2;
    pos += pb_encode_varint(&buf[pos], (uint32_t)ilen);
    memcpy(&buf[pos], inner, ilen);
    pos += ilen;
    return pos;
}


/*
 * Build OrbitBoardStatus (sub-message) inline at *buf.
 *
 * Always emits slot, dipswitch_id, connected, comm_errors, cpu_temp,
 * uptime_secs even when the proto3 default would suppress them — keeps
 * a stale-zero board from collapsing to an empty submessage that the
 * decoder cannot distinguish from "no board". cpu_temp is fixed32 (LE
 * float bits). Returns bytes written.
 *
 * Source data is the cached `OrbitSample.ident[7]` from orbit_client.h:
 *   ident[0]=id  ident[1]=eStop  ident[2]=commLost  ident[3]=safeMode
 *   ident[4]=cpuTemp×10  ident[5]=uptime_lo16  ident[6]=uptime_hi16
 */
static size_t build_orbit_board(uint8_t *buf, uint32_t slot,
                                const OrbitSample *s)
{
    size_t pos = 0;

    /* slot (field 1, varint) */
    buf[pos++] = 0x08;
    pos += pb_encode_varint(&buf[pos], slot);

    /* dipswitch_id (field 2, varint) — orbit ID from HR[40000] */
    buf[pos++] = 0x10;
    pos += pb_encode_varint(&buf[pos], (uint32_t)s->ident[0]);

    /* connected (field 3, bool varint) */
    buf[pos++] = 0x18;
    buf[pos++] = s->online ? 0x01 : 0x00;

    /* comm_errors (field 4, varint) */
    buf[pos++] = 0x20;
    pos += pb_encode_varint(&buf[pos], s->errorCount);

    /* estop_active (field 5, bool varint) — only emit when true */
    if (s->ident[1] != 0) {
        buf[pos++] = 0x28;
        buf[pos++] = 0x01;
    }

    /* safe_mode (field 6, bool varint) — only emit when true */
    if (s->ident[3] != 0) {
        buf[pos++] = 0x30;
        buf[pos++] = 0x01;
    }

    /* cpu_temp (field 7, float fixed32 LE) — convert from ×10 °C */
    float cpu_temp = (float)s->ident[4] / 10.0f;
    uint32_t cpu_bits;
    memcpy(&cpu_bits, &cpu_temp, sizeof(cpu_bits));
    buf[pos++] = 0x3D;
    buf[pos++] = (uint8_t)(cpu_bits & 0xFF);
    buf[pos++] = (uint8_t)((cpu_bits >> 8) & 0xFF);
    buf[pos++] = (uint8_t)((cpu_bits >> 16) & 0xFF);
    buf[pos++] = (uint8_t)((cpu_bits >> 24) & 0xFF);

    /* uptime_secs (field 8, varint) — combine HR[40005..40006] */
    uint32_t uptime = ((uint32_t)s->ident[6] << 16) | (uint32_t)s->ident[5];
    buf[pos++] = 0x40;
    pos += pb_encode_varint(&buf[pos], uptime);

    /* role (field 10, varint) — operator-assigned via Level 2 IO Config.
     * Pulled from LpSettings (field 78 of the save blob).  Proto3 zero-
     * suppression is fine here: 0 = ORBIT_ROLE_UNASSIGNED is also the
     * proto default that the bridge decoder picks when the field is
     * absent. We DO however force-emit when populated so a freshly
     * cleared slot (UNASSIGNED but populated=1) is still visible.
     * zone_id (11) and refrig_stage (13) are emitted only when
     * non-zero. legacy_slot (12) is `int32` — negative values need a
     * 10-byte sign-extended varint that pb_encode_varint can't write,
     * so we omit it for now (the bridge UI doesn't display it; the
     * field is only meaningful to firmware-side AS2 IoBoard[] mapping
     * which the LP doesn't implement yet). */
    {
        const LpOrbitRoleEntry *e = LpSettings_GetOrbitRole(slot);
        if (e != NULL && e->populated) {
            /* role: tag (10<<3)|0 = 80 → 0x50 */
            buf[pos++] = 0x50;
            pos += pb_encode_varint(&buf[pos], (uint32_t)e->role);
            if (e->zone_id != 0U) {
                buf[pos++] = 0x58;  /* (11<<3)|0 */
                pos += pb_encode_varint(&buf[pos], (uint32_t)e->zone_id);
            }
            if (e->refrig_stage != 0U) {
                buf[pos++] = 0x68;  /* (13<<3)|0 */
                pos += pb_encode_varint(&buf[pos], (uint32_t)e->refrig_stage);
            }
        }
    }

    /* ao_equip (field 15, repeated uint32 PACKED). Tag (15<<3)|2 = 122
     * → 0x7A.  Length=2 (channels 0 and 1) — emit only when at least
     * one cell is non-zero so factory state stays off the wire. The
     * UI reads the array as { ch0, ch1 } so positional layout matters
     * even when ch0 is unused but ch1 is programmed. */
    {
        uint8_t ch0 = LpSettings_GetAoEquip(slot, 0);
        uint8_t ch1 = LpSettings_GetAoEquip(slot, 1);
        if (ch0 != 0U || ch1 != 0U) {
            uint8_t vbuf[2 * 5];
            size_t  vlen = 0;
            vlen += pb_encode_varint(&vbuf[vlen], (uint32_t)ch0);
            vlen += pb_encode_varint(&vbuf[vlen], (uint32_t)ch1);
            buf[pos++] = 0x7A;
            pos += pb_encode_varint(&buf[pos], (uint32_t)vlen);
            memcpy(&buf[pos], vbuf, vlen);
            pos += vlen;
        }
    }

    /* ─── April 2026 LP-I/O extension (proto fields 16..23) ─────────
     * The orbit-server side now tracks DI/DO/DC24V state, AO levels,
     * VFD/sensor activity timestamps, and per-channel labels (see
     * orbit_state.h + orbit_storage.c). The CONTROLLER cache
     * (`OrbitSample`) mirrors:
     *   - sensor block (HR 200..263)               — every poll
     *   - AO HR window (HR 0..3)                   — every poll
     *   - DO bitmap (FC01 coils 0..9)              — every poll (Apr 2026)
     *   - DI bitmap (FC02 discrete inputs 0..14)   — every poll (Apr 2026)
     * Per-channel labels and per-slot activity timestamps remain TODO.
     *
     * Field-by-field status:
     *   - digital_inputs (16)  : LIVE from `s->di_bitmap` when
     *                            `s->io_valid`. OMITTED otherwise so
     *                            the bridge can distinguish "never
     *                            polled" (proto3 default 0 / absent)
     *                            from a real all-zero read once the
     *                            bridge is taught about io_valid.
     *                            Encoded WITHOUT _force so a real
     *                            zero bitmap still suppresses the tag
     *                            on the wire — that matches the
     *                            "bitmaps not _force-encoded" decision
     *                            in proto-orbit-iostatus.md (semantics
     *                            are identical: bridge sees no field
     *                            → all bits clear).
     *   - digital_outputs (17) : LIVE from `s->do_bitmap` (same rules).
     *   - dc24v_outputs  (18)  : OMITTED. Proto field is for DC24V
     *                            *output* channels (0..3) and the LP
     *                            has no sense-back path for them
     *                            today — DC24V monitor *inputs* live
     *                            in di_bitmap bits 11..14. Bridge
     *                            already omits this per the migration
     *                            notes; matching that behaviour.
     *   - analog_outputs_x10 (19) : LIVE from `s->aoHr[0..1]`.
     *   - vfd_activity_secs   (20) : 24 × UINT32_MAX sentinel (TODO).
     *   - sensor_activity_secs(21) : 16 × UINT32_MAX sentinel (TODO).
     *   - output_labels (22) / input_labels (23) : OMITTED (TODO).
     *
     * NO FAKE VALUES anywhere — every field above is either real
     * polled data or an explicit sentinel documented in
     * /memories/repo/proto-orbit-iostatus.md.
     */

    /* digital_inputs (field 16, varint).  Tag = (16<<3)|0 = 128
     * → varint 0x80 0x01.  FORCE-encode whenever io_valid is true,
     * even when the bitmap is 0 — "all DI off" is a meaningful
     * state, distinct from "never polled". This is the
     * pb_uint32_force pattern from the bridge protocol invariants
     * (proto3 normally zero-suppresses, which would conflate the
     * two states and matters for safety inputs like the e-stop bit). */
    if (s->io_valid) {
        buf[pos++] = 0x80;
        buf[pos++] = 0x01;
        pos += pb_encode_varint(&buf[pos], (uint32_t)s->di_bitmap);
    }

    /* digital_outputs (field 17, varint). Tag = (17<<3)|0 = 136
     * → varint 0x88 0x01. Same force-encode rule — all-DOs-off
     * (everything de-energized) is a real state the controller
     * needs to be able to publish unambiguously. */
    if (s->io_valid) {
        buf[pos++] = 0x88;
        buf[pos++] = 0x01;
        pos += pb_encode_varint(&buf[pos], (uint32_t)s->do_bitmap);
    }

    /* analog_outputs_x10 (field 19, repeated uint32 PACKED).
     * Tag = (19<<3)|2 = 154 → varint 0x9A 0x01. Two entries today
     * (channels 0 and 1); each value already in percent×10. */
    {
        uint8_t vbuf[2 * 5];
        size_t vlen = 0;
        for (size_t i = 0; i < ORBIT_AO_HR_COUNT && i < 2u; i++) {
            vlen += pb_encode_varint(&vbuf[vlen], (uint32_t)s->aoHr[i]);
        }
        buf[pos++] = 0x9A;
        buf[pos++] = 0x01;
        pos += pb_encode_varint(&buf[pos], (uint32_t)vlen);
        memcpy(&buf[pos], vbuf, vlen);
        pos += vlen;
    }

    /* vfd_activity_secs (field 20, repeated uint32 PACKED).
     * Tag = (20<<3)|2 = 162 → 0xA2 0x01. 24 entries; all UINT32_MAX
     * until orbit_client polls remote VFD activity. UINT32_MAX
     * varint-encodes to 5 bytes (0xFFFFFFFF) so payload = 120 bytes. */
    {
        uint8_t vbuf[24 * 5];
        size_t vlen = 0;
        for (size_t i = 0; i < 24u; i++) {
            vlen += pb_encode_varint(&vbuf[vlen], 0xFFFFFFFFu);
        }
        buf[pos++] = 0xA2;
        buf[pos++] = 0x01;
        pos += pb_encode_varint(&buf[pos], (uint32_t)vlen);
        memcpy(&buf[pos], vbuf, vlen);
        pos += vlen;
    }

    /* sensor_activity_secs (field 21, repeated uint32 PACKED).
     * Tag = (21<<3)|2 = 170 → 0xAA 0x01. 16 entries; all UINT32_MAX
     * until orbit_client polls remote sensor activity. */
    {
        uint8_t vbuf[16 * 5];
        size_t vlen = 0;
        for (size_t i = 0; i < 16u; i++) {
            vlen += pb_encode_varint(&vbuf[vlen], 0xFFFFFFFFu);
        }
        buf[pos++] = 0xAA;
        buf[pos++] = 0x01;
        pos += pb_encode_varint(&buf[pos], (uint32_t)vlen);
        memcpy(&buf[pos], vbuf, vlen);
        pos += vlen;
    }

    /* output_labels / input_labels (fields 22, 23, repeated string):
     * intentionally omitted for now (count=0). Bridge treats absent
     * label arrays as "no labels set" and renders "DO N"/"DI N".
     * When the orbit-side label HR region is added (follow-up), the
     * controller will mirror them via orbit_client and we'll emit
     * 10 entries here. */

    return pos;
}

/*
 * Build: Envelope { protocol_version=1, seq=N, orbit_status={boards=[...]} }
 *
 * Wire layout:
 *   Field 1 (protocol_version): tag=0x08, varint=1
 *   Field 2 (seq):              tag=0x10, varint=N
 *   Field 120 (orbit_status):   tag=0xC2 0x07, len, sub-message
 *     OrbitStatus.boards (field 1, repeated): tag=0x0A, len, OrbitBoardStatus
 *
 * Returns 0 if no orbit ever polled successfully (skip TX).
 */
static size_t build_orbit_status_envelope(uint8_t *buf, size_t bufsize,
                                          uint32_t seq)
{
    /* Per-board worst case (Apr 2026 LP I/O extension):
     *   identity + flags  ≈  30 B
     *   digital_inputs / digital_outputs (each ≤ 4 B incl. 2-B tag)
     *                                          ≈   8 B
     *   analog_outputs_x10 (2 entries)        =   7 B
     *   vfd_activity_secs  (24 × UINT32_MAX)  = 124 B
     *   sensor_activity_secs (16 × UINT32_MAX)=  83 B
     *   labels (omitted today)                =   0 B
     *   total per board                       ≈ 252 B
     * 5 orbits × (4 B board-wrapper tag/len + 252) ≈ 1280 B → 2048 safe. */
    uint8_t inner[2048];
    size_t inner_len = 0;
    OrbitSample sample;
    size_t orbit_count = OrbitClient_Count();
    size_t included = 0;

    for (size_t i = 0; i < orbit_count; i++) {
        if (!OrbitClient_GetSample((uint8_t)i, &sample)) {
            continue;   /* never polled */
        }
        /* Build one OrbitBoardStatus into a scratch, then wrap with
         * field 1 length-delimited tag. */
        uint8_t board[400];
        size_t board_len = build_orbit_board(board, (uint32_t)i, &sample);
        if (inner_len + 2 + board_len > sizeof(inner)) break;

        inner[inner_len++] = 0x0A;   /* field 1, wire-type 2 */
        inner_len += pb_encode_varint(&inner[inner_len], (uint32_t)board_len);
        memcpy(&inner[inner_len], board, board_len);
        inner_len += board_len;
        included++;
    }

    /* Always emit OrbitStatus, even with empty boards[]. The bridge
     * cache (`dataCache.orbitBoards`) is wire-authoritative; if we
     * suppress empty pushes the cache freezes on the last non-empty
     * snapshot — leaving the UI showing phantom-connected orbits and
     * stale roles after a board goes offline (or after a CONTROLLER
     * reset where LpSettings hasn't loaded yet on the very first push).
     * (void)included keeps the variable for future debug logging. */
    (void)included;

    size_t pos = 0;

    /* Envelope.protocol_version = 1 */
    buf[pos++] = 0x08;
    pos += pb_encode_varint(&buf[pos], NOVA_PROTOCOL_VERSION);

    /* Envelope.seq = N */
    buf[pos++] = 0x10;
    pos += pb_encode_varint(&buf[pos], seq);

    /* Envelope.orbit_status (field 120, length-delimited)
     * Tag = (120 << 3) | 2 = 962 → varint 0xC2 0x07 */
    buf[pos++] = 0xC2;
    buf[pos++] = 0x07;
    pos += pb_encode_varint(&buf[pos], (uint32_t)inner_len);
    memcpy(&buf[pos], inner, inner_len);
    pos += inner_len;

    (void)bufsize;
    return pos;
}

/*
 * Build: Envelope { protocol_version=1, seq=N,
 *                   orbit_sensor_bank={slot, hr_base, values[count], seq} }
 *
 * Wire layout:
 *   Field 1   (protocol_version): tag=0x08, varint=1
 *   Field 2   (seq):              tag=0x10, varint=env_seq
 *   Field 124 (orbit_sensor_bank): tag=(124<<3)|2=994 → 0xE2 0x07
 *     OrbitSensorBank.slot     (field 1, varint, ALWAYS — slot 0 valid)
 *     OrbitSensorBank.hr_base  (field 2, varint, ALWAYS — hr_base 0 valid)
 *     OrbitSensorBank.values   (field 3, repeated uint32 packed)
 *     OrbitSensorBank.seq      (field 4, varint, per-bank seq)
 *
 * Phase 4b 2026-06-01: parameterised on (hr_base, values, count) so
 * the same emitter handles the STORAGE sensor bank (200..327, 128
 * regs) AND the per-role secondary windows (GDC 300..395, TRITON
 * 400..655). `values` is the source u16 array; caller provides
 * `count` and the matching `hr_base` so the bridge can route the
 * decode by (slot, hr_base) pair.
 *
 * Returns 0 if count==0 or the envelope wouldn't fit in `bufsize`.
 */
static size_t build_orbit_sensor_bank_envelope(uint8_t *buf, size_t bufsize,
                                               uint32_t env_seq,
                                               uint8_t orbit_index,
                                               uint16_t hr_base,
                                               const uint16_t *values,
                                               uint16_t count,
                                               uint32_t board_seq)
{
    if (count == 0U || values == NULL) {
        return 0;
    }

    /* Build OrbitSensorBank inner first. Inner buffer sized for the
     * worst case: 256 regs × 3 B max varint + ~16 B hdr/seq overhead. */
    uint8_t inner[ORBIT_ROLE_HR_MAX * 3 + 32];
    size_t ilen = 0;

    /* slot (field 1, varint) — always emit, slot 0 is valid */
    inner[ilen++] = 0x08;
    ilen += pb_encode_varint(&inner[ilen], (uint32_t)orbit_index);

    /* hr_base (field 2, varint) — always emit, hr_base 0 is valid */
    inner[ilen++] = 0x10;
    ilen += pb_encode_varint(&inner[ilen], (uint32_t)hr_base);

    /* values (field 3, repeated uint32 PACKED). Tag = (3<<3)|2 = 0x1A.
     * Packed encoding: tag, length, concatenated varints. */
    inner[ilen++] = 0x1A;
    /* Pre-encode the values into a scratch to learn the byte length. */
    uint8_t vbuf[ORBIT_ROLE_HR_MAX * 3];   /* uint16 → ≤3B varint */
    size_t vlen = 0;
    for (uint16_t k = 0; k < count; k++) {
        vlen += pb_encode_varint(&vbuf[vlen], (uint32_t)values[k]);
    }
    ilen += pb_encode_varint(&inner[ilen], (uint32_t)vlen);
    if (ilen + vlen > sizeof(inner)) return 0;
    memcpy(&inner[ilen], vbuf, vlen);
    ilen += vlen;

    /* seq (field 4, varint) — per-board monotonic */
    inner[ilen++] = 0x20;
    ilen += pb_encode_varint(&inner[ilen], board_seq);

    /* Wrap in Envelope. */
    size_t pos = 0;

    /* Envelope.protocol_version = 1 */
    buf[pos++] = 0x08;
    pos += pb_encode_varint(&buf[pos], NOVA_PROTOCOL_VERSION);

    /* Envelope.seq */
    buf[pos++] = 0x10;
    pos += pb_encode_varint(&buf[pos], env_seq);

    /* Envelope.orbit_sensor_bank — field 124, length-delimited.
     * Tag = (124 << 3) | 2 = 994 → varint 0xE2 0x07. (123 is reserved
     * for AoEquipAssign, bridge→firmware.) */
    buf[pos++] = 0xE2;
    buf[pos++] = 0x07;
    pos += pb_encode_varint(&buf[pos], (uint32_t)ilen);
    if (pos + ilen > bufsize) return 0;
    memcpy(&buf[pos], inner, ilen);
    pos += ilen;

    return pos;
}

/*
 * Build: Envelope { protocol_version=1, seq=N,
 *                   vfd_reg_bank={ unit_id, hr_base, values[], seq, online } }
 *
 * Phase 4b Sub-phase 3 (2026-06-01). Mirrors the orbit_sensor_bank
 * builder but at envelope tag 128 and with an `online` field. All
 * varint fields use FORCE-encoding because 0 is a meaningful value
 * (unit_id 0 valid, hr_base 0 valid, value 0 valid, online=0 means
 * "cached but stale"). Returns 0 when count==0 or the inner won't
 * fit.
 */
static size_t build_vfd_reg_bank_envelope(uint8_t *buf, size_t bufsize,
                                          uint32_t env_seq,
                                          uint8_t unit_id, uint16_t hr_base,
                                          const uint16_t *values, uint16_t count,
                                          uint32_t bank_seq, bool online)
{
    if (count == 0U || values == NULL) return 0;

    /* Worst case: 16-reg group × 3 B varint + ~24 B overhead. */
    uint8_t inner[NOVA_VFD_MAX_REGS_PER_BANK * 3 + 32];
    size_t  ilen = 0;

    /* unit_id (field 1, varint, force-encoded) */
    inner[ilen++] = 0x08;
    ilen += pb_encode_varint(&inner[ilen], (uint32_t)unit_id);

    /* hr_base (field 2, varint, force-encoded) */
    inner[ilen++] = 0x10;
    ilen += pb_encode_varint(&inner[ilen], (uint32_t)hr_base);

    /* values (field 3, repeated uint32 PACKED). Tag = (3<<3)|2 = 0x1A. */
    inner[ilen++] = 0x1A;
    uint8_t vbuf[NOVA_VFD_MAX_REGS_PER_BANK * 3];
    size_t  vlen = 0;
    for (uint16_t k = 0; k < count; k++) {
        vlen += pb_encode_varint(&vbuf[vlen], (uint32_t)values[k]);
    }
    ilen += pb_encode_varint(&inner[ilen], (uint32_t)vlen);
    if (ilen + vlen > sizeof(inner)) return 0;
    memcpy(&inner[ilen], vbuf, vlen);
    ilen += vlen;

    /* seq (field 4, varint) */
    inner[ilen++] = 0x20;
    ilen += pb_encode_varint(&inner[ilen], bank_seq);

    /* online (field 5, varint, force-encoded — 0 means "cached, stale") */
    inner[ilen++] = 0x28;
    ilen += pb_encode_varint(&inner[ilen], online ? 1U : 0U);

    /* Envelope wrap. */
    size_t pos = 0;
    buf[pos++] = 0x08;
    pos += pb_encode_varint(&buf[pos], NOVA_PROTOCOL_VERSION);
    buf[pos++] = 0x10;
    pos += pb_encode_varint(&buf[pos], env_seq);

    /* Envelope.vfd_reg_bank — field 128, length-delimited.
     * Tag = (128 << 3) | 2 = 1026 → varint 0x82 0x08. */
    buf[pos++] = 0x82;
    buf[pos++] = 0x08;
    pos += pb_encode_varint(&buf[pos], (uint32_t)ilen);
    if (pos + ilen > bufsize) return 0;
    memcpy(&buf[pos], inner, ilen);
    pos += ilen;
    return pos;
}

/*
 * bridge_uart_task — drives all firmware → bridge UART2 traffic.
 *
 * RX is interrupt-driven: uart2_rx_isr pushes bytes into s_rx_ring as
 * fast as the UART can deliver them. This task drains the ring into
 * NovaProto on every wake (10 ms tick), then runs the cadence emitters
 * (heartbeat, sensor, settings) from the same loop. The 10 ms wake
 * keeps NovaProto's COBS state machine moving without burning CPU
 * polling — at 4 KB ring + 921600 baud we have ~45 ms of slack before
 * the ring would fill, far longer than any cadence emit takes.
 *
 * Verified 2026-04-24: 30 frames / 60s with 0 CRC / 0 COBS errors at
 * 921600 baud. 2026-04-28: large SettingsUpdate bodies (96 B+) now ACK
 * cleanly — the prior 100 ms polled drain dropped HW-FIFO bytes
 * silently above ~48 B body (lp-am2434-uart-rx-fifo-trap memory).
 */
static void bridge_uart_task(void *args)
{
    uint8_t envelope_buf[5120];   /* SensorData with 5 orbits × 64 readings
                                   * fits comfortably; orbit_status,
                                   * sensor_bank, datetime all <512.
                                   * SettingsBlob = 24 B header +
                                   * full s_data serialised (up to
                                   * LP_SETTINGS_MAX_BLOB_SIZE = 4096) +
                                   * envelope tag/length overhead. */
    uint32_t tick = 0;
    uint32_t orbit_seq = 0;
    uint32_t bank_env_seq = 0;
    uint32_t bank_board_seq[ORBIT_CLIENT_MAX_ORBITS] = {0};
    uint32_t version_seq = 0;
    uint32_t datetime_seq = 0;
    uint32_t sysstatus_seq = 0;
    uint32_t sensordata_seq = 0;
    uint32_t ioconfig_seq = 0;
    uint32_t iodef_seq = 0;
    uint32_t availio_seq = 0;
    uint32_t account_seq = 0;
    uint32_t equip_seq = 0;
    uint32_t warn_seq = 0;
    uint32_t pidlog_seq = 0;
    uint32_t humidmodes_seq = 0;
    uint32_t auxsw_seq = 0;
    uint32_t fanrun_seq = 0;
    uint32_t senslab_seq = 0;
    uint32_t blob_seq = 0;
    uint32_t panel_blob_seq = 0;
    uint32_t basicsetup_seq = 0;
    uint32_t plenum_seq = 0;
    uint32_t fanspeed_seq = 0;
    uint32_t fanboost_seq = 0;
    uint32_t ramprate_seq = 0;
    uint32_t refrig_seq = 0;
    uint32_t burner_seq = 0;
    uint32_t co2_seq = 0;
    uint32_t cure_seq = 0;
    uint32_t climacell_seq = 0;
    uint32_t climacell_times_seq = 0;
    uint32_t humid_ctrl_seq = 0;
    uint32_t outside_air_seq = 0;
    uint32_t misc_seq = 0;
    uint32_t failure_seq = 0;
    uint32_t failure2_seq = 0;
    uint32_t temp_alarm_seq = 0;
    uint32_t door_seq = 0;
    uint32_t cure_limit_seq = 0;
    uint32_t user_log_seq = 0;
    uint32_t pid_seq = 0;
    uint32_t master_slave_seq = 0;
    uint32_t http_port_seq = 0;
    uint32_t public_address_seq = 0;
    uint32_t sys_mode_seq = 0;
    uint32_t pid_log_seq = 0;
    uint32_t service_info_seq = 0;
    uint32_t email_seq = 0;
    uint32_t graph_favorites_seq = 0;
    uint32_t alert_seq = 0;
    uint32_t bay_name_seq = 0;
    uint32_t load_monitor_seq = 0;
    uint32_t runtime_seq = 0;
    uint32_t aux_program_seq = 0;
    uint32_t analog_board_seq = 0;
    uint32_t pwm_channel_seq = 0;
    bool sent_dls = false;

    if (s_uart2_base == NULL) {
        vTaskSuspend(NULL);
    }

    while (1)
    {
        /* Drain the RX ring fed by uart2_rx_isr. NovaProto_FeedByte is
         * single-threaded by design — only this task ever calls it, so
         * the COBS state machine never races the ISR. */
        uint8_t rxbyte;
        while (uart2_rx_ring_pop(&rxbyte)) {
            NovaProto_FeedByte(rxbyte);
        }

        /* Flush any deferred Ack queued by bridge_rx_callback during the
         * RX drain above. See bridge_rx_callback for the wire layout. */
        if (s_pending_ack_ready) {
            uint32_t seq = s_pending_ack_seq;
            s_pending_ack_ready = false;
            uint8_t ackbuf[16];
            size_t apos = 0;
            ackbuf[apos++] = 0x08;
            apos += pb_encode_varint(&ackbuf[apos], NOVA_PROTOCOL_VERSION);
            ackbuf[apos++] = 0x10;
            apos += pb_encode_varint(&ackbuf[apos], seq);
            uint8_t inner[8];
            size_t ilen = 0;
            inner[ilen++] = 0x08;
            ilen += pb_encode_varint(&inner[ilen], seq);
            ackbuf[apos++] = 0xA2;
            ackbuf[apos++] = 0x06;
            apos += pb_encode_varint(&ackbuf[apos], (uint32_t)ilen);
            memcpy(&ackbuf[apos], inner, ilen);
            apos += ilen;
            NovaProto_SendRaw(ackbuf, apos);
        }

        tick++;

        /* Send DataLoadStatus(ready=true) once at 1s of uptime, then
         * repeat every 30s. The repeat lets a restarted bridge re-establish
         * the `connected` flag without needing to power-cycle the LP. */
        (void)sent_dls;
        if (tick == 10 || (tick % 300) == 0) {
            size_t pb_len = build_dls_envelope(envelope_buf,
                                               sizeof(envelope_buf), 0);
            NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* Send VersionInfo envelope once at 2s of uptime (well after
         * DataLoadStatus latches `connected`), then repeat every 30s.
         * Cadence matches DataLoadStatus and the legacy AS2 SysVersions
         * push so a freshly-attached bridge / UI sees the firmware
         * identity within 30 s of boot or reconnect. */
        if (tick == 20 || (tick % 300) == 15) {
            size_t pb_len = build_version_info_envelope(
                envelope_buf, sizeof(envelope_buf), ++version_seq);
            if (pb_len > 0) {
                NovaProto_SendRaw(envelope_buf, pb_len);
            }
        }

        /* Phase C standalone ServiceInfo removed — the settings
         * scheduler now emits ServiceInfo on field 23 with the persisted
         * s_data.service_info struct (dealer/tech strings). Having two
         * senders on field 23 with independent seq counters caused the
         * bridge's per-msg reset detector to false-fire continuously. */

        /* Phase C — IoConfig / IoDefinition / AvailableIo on the same
         * 30s cadence as VersionInfo, staggered by ticks 17/18/19 so
         * they fan out across the second instead of bursting. All three
         * emit empty-body skeletons until OSPI persistence (Phase D)
         * lands; this clears the UI's "config envelope not received"
         * gates without lying about equipment topology. AnalogBoard
         * (field 26) is intentionally NOT emitted — see
         * build_io_config_envelope() comment. */
        if (tick == 22 || (tick % 300) == 17) {
            size_t pb_len = build_io_config_envelope(
                envelope_buf, sizeof(envelope_buf), ++ioconfig_seq);
            if (pb_len > 0) {
                NovaProto_SendRaw(envelope_buf, pb_len);
            }
        }
        if (tick == 23 || (tick % 300) == 18) {
            size_t pb_len = build_io_definition_envelope(
                envelope_buf, sizeof(envelope_buf), ++iodef_seq);
            if (pb_len > 0) {
                NovaProto_SendRaw(envelope_buf, pb_len);
            }
        }
        if (tick == 24 || (tick % 300) == 19) {
            size_t pb_len = build_available_io_envelope(
                envelope_buf, sizeof(envelope_buf), ++availio_seq);
            if (pb_len > 0) {
                NovaProto_SendRaw(envelope_buf, pb_len);
            }
        }
        if (tick == 25 || (tick % 300) == 20) {
            /* AccountSettings — same 30s cadence as other config pages.
             * Emits empty body when no users configured (clears the UI's
             * "haven't received yet" gate without inventing accounts). */
            size_t pb_len = build_account_envelope(
                envelope_buf, sizeof(envelope_buf), ++account_seq);
            if (pb_len > 0) {
                NovaProto_SendRaw(envelope_buf, pb_len);
            }
        }

        /* Phase C.5 skeleton envelopes — empty bodies clear UI gates.
         *
         * The five "live values" broadcasts (EquipmentStatus,
         * SystemStatus, SensorData, OrbitStatus, OrbitSensorBank) are
         * gated on !NovaOtaBroker_IsInInstallMode() so a Pi5 OTA push
         * has the UART back-direction to itself for FwInstallProgress
         * envelopes. Heartbeat stays unguarded (the bridge needs the
         * keep-alive during install), and the per-tag settings-
         * response scheduler below stays unguarded too (on-demand
         * responses must keep working). Per the Phase-3 task brief
         * and docs/uart-airgap-architecture.md. */
        if (((tick % 20) == 15 || s_equip_status_dirty) &&
            !NovaOtaBroker_IsInInstallMode()) {
            /* EquipmentStatus (envelope tag 11) every 2 s, plus an
             * extra immediate push when bridge_rx_callback flipped a
             * remote_off slot.  The dirty flag is consumed here so the
             * UI sees the change within ~10 ms instead of waiting up
             * to 2 s. */
            s_equip_status_dirty = false;
            size_t pb_len = build_equipment_status_envelope(
                envelope_buf, sizeof(envelope_buf), ++equip_seq);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }
        if ((tick % 50) == 35) {           /* WarningReport every 5s */
            size_t pb_len = build_warning_report_envelope(
                envelope_buf, sizeof(envelope_buf), ++warn_seq);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* PidLogRecord drain — event-driven. Drain at most one record
         * per data-exchange tick (100 ms) so a refrig U=15 / door U=2
         * burst can't monopolize the UART. The ring is sized for ~3 s
         * of buffering at full burst. */
        {
            size_t pb_len = build_pid_log_record_envelope(
                envelope_buf, sizeof(envelope_buf), pidlog_seq + 1U);
            if (pb_len > 0) {
                NovaProto_SendRaw(envelope_buf, pb_len);
                pidlog_seq++;
            }
        }
        if ((tick % 50) == 40) {           /* AuxSwitches every 5s */
            static const uint8_t TAG_AUXSW[] = { 0x82, 0x01 };
            size_t pb_len = build_skeleton_envelope(
                envelope_buf, sizeof(envelope_buf), ++auxsw_seq,
                TAG_AUXSW, sizeof(TAG_AUXSW));
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }
        if (tick == 26 || (tick % 300) == 21) {   /* HumidModes / 30s */
            static const uint8_t TAG_HUMIDMODES[] = { 0x7A };
            size_t pb_len = build_skeleton_envelope(
                envelope_buf, sizeof(envelope_buf), ++humidmodes_seq,
                TAG_HUMIDMODES, sizeof(TAG_HUMIDMODES));
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }
        if (tick == 27 || (tick % 300) == 22) {   /* FanRuntime / 30s */
            static const uint8_t TAG_FANRUN[] = { 0x92, 0x01 };
            size_t pb_len = build_skeleton_envelope(
                envelope_buf, sizeof(envelope_buf), ++fanrun_seq,
                TAG_FANRUN, sizeof(TAG_FANRUN));
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }
        if (tick == 28 || (tick % 300) == 23) {   /* SensorLabels / 30s */
            static const uint8_t TAG_SENSLAB[] = { 0xE2, 0x01 };
            size_t pb_len = build_skeleton_envelope(
                envelope_buf, sizeof(envelope_buf), ++senslab_seq,
                TAG_SENSLAB, sizeof(TAG_SENSLAB));
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* Phase E — BasicSetup envelope (tag 20). Cadence: every 5 s,
         * tick%50==45 (offset from DateTime/SystemStatus/SensorData
         * windows so we never bunch four envelopes onto one tick). The
         * inner body comes from the in-RAM Settings struct populated
         * by LpSettings_DataInit + LpSettings_Deserialize; with no
         * configured fields it serializes to a 0-byte body and the
         * bridge cache shows all defaults. */
        if ((tick % 50) == 45) {
            size_t pb_len = build_basic_setup_envelope(
                envelope_buf, sizeof(envelope_buf), ++basicsetup_seq);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* Plenum (tag 40): every 5 s, tick%50==46.
         * (40<<3)|2 = 322 → varint 0xC2 0x02. */
        if ((tick % 50) == 46) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++plenum_seq,
                0xC2, 0x02, LpSettings_BuildPlenumBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* FanSpeed (tag 41): every 5 s, tick%50==47.
         * (41<<3)|2 = 330 → varint 0xCA 0x02. */
        if ((tick % 50) == 47) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++fanspeed_seq,
                0xCA, 0x02, LpSettings_BuildFanSpeedBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* FanBoost (tag 42): every 5 s, tick%50==48.
         * (42<<3)|2 = 338 → varint 0xD2 0x02. */
        if ((tick % 50) == 48) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++fanboost_seq,
                0xD2, 0x02, LpSettings_BuildFanBoostBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* RampRate (tag 43): every 5 s, tick%50==49.
         * (43<<3)|2 = 346 → varint 0xDA 0x02. */
        if ((tick % 50) == 49) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++ramprate_seq,
                0xDA, 0x02, LpSettings_BuildRampRateBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* Refrig (tag 44): every 5 s, tick%50==0 (wraps to next 5-s
         * window). (44<<3)|2 = 354 → varint 0xE2 0x02. */
        if ((tick % 50) == 0 && tick != 0) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++refrig_seq,
                0xE2, 0x02, LpSettings_BuildRefrigBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* Burner (tag 45): every 5 s, tick%50==1.
         * (45<<3)|2 = 362 → varint 0xEA 0x02. */
        if ((tick % 50) == 1) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++burner_seq,
                0xEA, 0x02, LpSettings_BuildBurnerBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* Co2 (tag 46): every 5 s, tick%50==2.
         * (46<<3)|2 = 370 → varint 0xF2 0x02. */
        if ((tick % 50) == 2) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++co2_seq,
                0xF2, 0x02, LpSettings_BuildCo2Body);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* Cure (tag 47): every 5 s, tick%50==3.
         * (47<<3)|2 = 378 → varint 0xFA 0x02. */
        if ((tick % 50) == 3) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++cure_seq,
                0xFA, 0x02, LpSettings_BuildCureBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* Climacell (tag 48): every 5 s, tick%50==4.
         * (48<<3)|2 = 386 → varint 0x82 0x03. */
        if ((tick % 50) == 4) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++climacell_seq,
                0x82, 0x03, LpSettings_BuildClimacellBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* ClimacellTimes (tag 49): every 5 s, tick%50==5.
         * (49<<3)|2 = 394 → varint 0x8A 0x03. Body up to ~245 B. */
        if ((tick % 50) == 5) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++climacell_times_seq,
                0x8A, 0x03, LpSettings_BuildClimacellTimesBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* HumidCtrl (tag 50): every 5 s, tick%50==6.
         * (50<<3)|2 = 402 → varint 0x92 0x03. */
        if ((tick % 50) == 6) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++humid_ctrl_seq,
                0x92, 0x03, LpSettings_BuildHumidCtrlBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* OutsideAir (tag 51): every 5 s, tick%50==7.
         * (51<<3)|2 = 410 → varint 0x9A 0x03. */
        if ((tick % 50) == 7) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++outside_air_seq,
                0x9A, 0x03, LpSettings_BuildOutsideAirBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* Misc (tag 52): every 5 s, tick%50==8.
         * (52<<3)|2 = 418 → varint 0xA2 0x03. */
        if ((tick % 50) == 8) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++misc_seq,
                0xA2, 0x03, LpSettings_BuildMiscBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* Failure (tag 53): every 5 s, tick%50==9.
         * (53<<3)|2 = 426 → varint 0xAA 0x03. */
        if ((tick % 50) == 9) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++failure_seq,
                0xAA, 0x03, LpSettings_BuildFailureBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* Failure2 (tag 54): every 5 s, tick%50==10.
         * (54<<3)|2 = 434 → varint 0xB2 0x03. */
        if ((tick % 50) == 10) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++failure2_seq,
                0xB2, 0x03, LpSettings_BuildFailure2Body);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* TempAlarm (tag 55): every 5 s, tick%50==11.
         * (55<<3)|2 = 442 → varint 0xBA 0x03. */
        if ((tick % 50) == 11) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++temp_alarm_seq,
                0xBA, 0x03, LpSettings_BuildTempAlarmBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* Door (tag 56): every 5 s, tick%50==12.
         * (56<<3)|2 = 450 → varint 0xC2 0x03. */
        if ((tick % 50) == 12) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++door_seq,
                0xC2, 0x03, LpSettings_BuildDoorBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* CureLimit (envelope tag 68): every 5 s, tick%50==13.
         * (68<<3)|2 = 546 → varint 0xA2 0x04. */
        if ((tick % 50) == 13) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++cure_limit_seq,
                0xA2, 0x04, LpSettings_BuildCureLimitBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* UserLog (envelope tag 59): every 5 s, tick%50==14.
         * (59<<3)|2 = 474 → varint 0xDA 0x03. */
        if ((tick % 50) == 14) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++user_log_seq,
                0xDA, 0x03, LpSettings_BuildUserLogBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* Pid (envelope tag 60): every 5 s, tick%50==15.
         * (60<<3)|2 = 482 → varint 0xE2 0x03. */
        if ((tick % 50) == 15) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++pid_seq,
                0xE2, 0x03, LpSettings_BuildPidBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* MasterSlave (envelope tag 66): every 5 s, tick%50==16.
         * (66<<3)|2 = 530 → varint 0x92 0x04. */
        if ((tick % 50) == 16) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++master_slave_seq,
                0x92, 0x04, LpSettings_BuildMasterSlaveBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* HttpPort (envelope tag 69): every 5 s, tick%50==17.
         * (69<<3)|2 = 554 → varint 0xAA 0x04. */
        if ((tick % 50) == 17) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++http_port_seq,
                0xAA, 0x04, LpSettings_BuildHttpPortBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* PublicAddress (envelope tag 74): every 5 s, tick%50==18.
         * (74<<3)|2 = 594 → varint 0xD2 0x04. */
        if ((tick % 50) == 18) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++public_address_seq,
                0xD2, 0x04, LpSettings_BuildPublicAddressBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* SysMode (envelope tag 75): every 5 s, tick%50==19.
         * (75<<3)|2 = 602 → varint 0xDA 0x04. */
        if ((tick % 50) == 19) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++sys_mode_seq,
                0xDA, 0x04, LpSettings_BuildSysModeBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* PidLog (envelope tag 67): every 5 s, tick%50==20.
         * (67<<3)|2 = 538 → varint 0x9A 0x04. */
        if ((tick % 50) == 20) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++pid_log_seq,
                0x9A, 0x04, LpSettings_BuildPidLogBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* ServiceInfo (envelope tag 23): every 5 s, tick%50==21.
         * (23<<3)|2 = 186 → varint 0xBA 0x01. */
        if ((tick % 50) == 21) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++service_info_seq,
                0xBA, 0x01, LpSettings_BuildServiceInfoBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* Email (envelope tag 62): every 5 s, tick%50==22.
         * (62<<3)|2 = 498 → varint 0xF2 0x03. */
        if ((tick % 50) == 22) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++email_seq,
                0xF2, 0x03, LpSettings_BuildEmailBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* GraphFavorites (envelope tag 61): every 5 s, tick%50==23.
         * (61<<3)|2 = 490 → varint 0xEA 0x03. */
        if ((tick % 50) == 23) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++graph_favorites_seq,
                0xEA, 0x03, LpSettings_BuildGraphFavoritesBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* Alert (envelope tag 63): every 5 s, tick%50==24.
         * (63<<3)|2 = 506 → varint 0xFA 0x03. */
        if ((tick % 50) == 24) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++alert_seq,
                0xFA, 0x03, LpSettings_BuildAlertBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* BayName (envelope tag 76): every 5 s, tick%50==26.
         * Slot 25 is reserved for DateTime cadence below.
         * (76<<3)|2 = 610 → varint 0xE2 0x04. */
        if ((tick % 50) == 26) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++bay_name_seq,
                0xE2, 0x04, LpSettings_BuildBayNameBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* LoadMonitor (envelope tag 57): every 5 s, tick%50==27.
         * (57<<3)|2 = 458 → varint 0xCA 0x03. */
        if ((tick % 50) == 27) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++load_monitor_seq,
                0xCA, 0x03, LpSettings_BuildLoadMonitorBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* Runtime (envelope tag 77): every 5 s, tick%50==28.
         * (77<<3)|2 = 618 → varint 0xEA 0x04. */
        if ((tick % 50) == 28) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++runtime_seq,
                0xEA, 0x04, LpSettings_BuildRuntimeBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* AuxProgram (envelope tag 58): every 5 s, tick%50==29.
         * (58<<3)|2 = 466 → varint 0xD2 0x03. */
        if ((tick % 50) == 29) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++aux_program_seq,
                0xD2, 0x03, LpSettings_BuildAuxProgramBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* AnalogBoard (envelope tag 26): every 5 s, tick%50==30.
         * (26<<3)|2 = 210 → varint 0xD2 0x01. */
        if ((tick % 50) == 30) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++analog_board_seq,
                0xD2, 0x01, LpSettings_BuildAnalogBoardBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* PwmChannel (envelope tag 64): every 5 s, tick%50==31.
         * (64<<3)|2 = 514 → varint 0x82 0x04. */
        if ((tick % 50) == 31) {
            size_t pb_len = build_settings_envelope(
                envelope_buf, sizeof(envelope_buf), ++pwm_channel_seq,
                0x82, 0x04, LpSettings_BuildPwmChannelBody);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* Phase D — settings blob: emit on every tick the dirty flag
         * is set. LpSettings_TakeBlob() clears the flag and copies the
         * active bank header+blob; build_settings_blob_envelope returns
         * 0 when nothing is dirty so steady-state cost is one branch. */
        {
            size_t pb_len = build_settings_blob_envelope(
                envelope_buf, sizeof(envelope_buf), ++blob_seq);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* Panel-default snapshot: same dirty-flag pattern as the active
         * blob above. Triggered by the operator pressing "Save as Panel
         * Default" on the Level 2 Save Settings page (CMD_SET_DEFAULT). */
        {
            size_t pb_len = build_panel_blob_envelope(
                envelope_buf, sizeof(envelope_buf), ++panel_blob_seq);
            if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* Cold-boot defaults seeding (race-free).
         *
         * Runs ONCE at tick==40 (~4 s after task start, ~3 s after the
         * t=10 DLS push that flips bridge.connected and triggers
         * envelope-91 replay). If by this point the bridge replayed a
         * saved blob, LpSettings_Deserialize() will have flipped
         * s_active_bank to 0 or 1 — we skip and the operator's data is
         * preserved. If active_bank is still < 0, no replay arrived
         * (fresh board / wiped file / schema mismatch) so we serialize
         * the just-initialised defaults and Save() — which dirties the
         * blob, so the next iteration of this same task emits
         * envelope-32 and the bridge mirrors it to disk. From then on
         * subsequent reboots replay normally. */
        if (tick == 40 && LpSettings_GetActiveBank() < 0) {
            static uint8_t seed_blob[LP_SETTINGS_MAX_BLOB_SIZE];
            size_t sl = LpSettings_Serialize(seed_blob,
                                             sizeof(seed_blob));
            if (sl > 0) {
                (void)LpSettings_Save(seed_blob, sl);
            }
        }

        /* Send DateTime envelope every ~5 s (50 × 100 ms). The bridge
         * UI Header polls DateTime continuously; the 5-s cadence is
         * dense enough that a manual time edit propagates within a
         * second or two of save-button press, but sparse enough not to
         * flood the wire. Skipped silently before LpRtc_Init populates
         * a baseline (it always does, so this gates only on the very
         * first 100 ms of boot). */
        if ((tick % 50) == 25) {
            size_t pb_len = build_date_time_envelope(
                envelope_buf, sizeof(envelope_buf), ++datetime_seq);
            if (pb_len > 0) {
                NovaProto_SendRaw(envelope_buf, pb_len);
            }
        }

        /* Send SystemStatus envelope every ~2s (20 × 100ms), offset
         * +5 ticks from Heartbeat so the two don't collide on the wire.
         * Phase B emits an empty SystemStatus body — see
         * build_system_status_envelope() for the Phase C dependency.
         * Gated on broker install-mode per Phase 3. */
        if ((tick % 20) == 5 && !NovaOtaBroker_IsInInstallMode()) {
            size_t pb_len = build_system_status_envelope(
                envelope_buf, sizeof(envelope_buf), ++sysstatus_seq);
            if (pb_len > 0) {
                NovaProto_SendRaw(envelope_buf, pb_len);
                LpWatchdog_Ping(LP_WD_ALIVE_SYSTEMSTATUS);
            }
        }

        /* Send SensorData envelope every ~2s (20 × 100ms), offset
         * +10 ticks so it lands halfway between Heartbeat and the next
         * SystemStatus. Skipped silently before any orbit produces a
         * sample. Gated on broker install-mode per Phase 3. */
        if ((tick % 20) == 10 && !NovaOtaBroker_IsInInstallMode()) {
            size_t pb_len = build_sensor_data_envelope(
                envelope_buf, sizeof(envelope_buf), ++sensordata_seq);
            if (pb_len > 0) {
                NovaProto_SendRaw(envelope_buf, pb_len);
            }
        }

        /* Send Heartbeat envelope every ~2s (20 × 100ms). Intentionally
         * NOT gated on install mode — bridge needs the keep-alive
         * during a long OTA push, and the heartbeat is the cheapest
         * envelope on the wire. */
        if ((tick % 20) == 0)
        {
            uint32_t uptime = (uint32_t)(ClockP_getTimeUsec() / 1000000ULL);
            size_t pb_len = build_heartbeat_envelope(
                envelope_buf, sizeof(envelope_buf),
                tick / 20, uptime);

            NovaProto_SendRaw(envelope_buf, pb_len);
        }

        /* Send OrbitStatus envelope every ~5s (50 × 100ms). Skipped
         * silently before any orbit has produced a sample. Gated on
         * broker install-mode per Phase 3. */
        if ((tick % 50) == 0 && !NovaOtaBroker_IsInInstallMode())
        {
            size_t pb_len = build_orbit_status_envelope(
                envelope_buf, sizeof(envelope_buf), ++orbit_seq);
            if (pb_len > 0) {
                NovaProto_SendRaw(envelope_buf, pb_len);
            }
        }

        /* Send OrbitSensorBank(s) per 100ms tick, cycling through all
         * configured orbits. With 5 orbits this covers every board every
         * 500ms (~2 Hz per board, well under the 1 Hz orbit polling rate
         * so the bridge never sees stale duplicates).
         *
         * Phase 4b 2026-06-01: each cycle emits up to TWO banks for the
         * selected orbit — the always-present STORAGE-style sensor bank
         * (hr_base=ORBIT_SENSOR_HR_BASE) PLUS the per-role secondary
         * window (hr_base=ORBIT_ROLE_HR_GDC/TRITON_BASE) when populated.
         * Bridge routes by (slot, hr_base) so it can hold both banks
         * concurrently. Gated on broker install-mode per Phase 3. */
        if (!NovaOtaBroker_IsInInstallMode()) {
            uint8_t orbit_count = (uint8_t)OrbitClient_Count();
            if (orbit_count > 0) {
                uint8_t idx = (uint8_t)(tick % orbit_count);
                OrbitSample sample;
                if (OrbitClient_GetSample(idx, &sample)) {
                    ss_apply_remote_outside_override(&sample, idx);
                    uint32_t this_seq = ++bank_board_seq[idx];

                    /* Bank 1: STORAGE-style sensor bank (always emit). */
                    size_t pb_len = build_orbit_sensor_bank_envelope(
                        envelope_buf, sizeof(envelope_buf),
                        ++bank_env_seq, idx,
                        ORBIT_SENSOR_HR_BASE,
                        sample.sensorHr, ORBIT_SENSOR_HR_COUNT,
                        this_seq);
                    if (pb_len > 0) {
                        NovaProto_SendRaw(envelope_buf, pb_len);
                    }

                    /* Bank 2: per-role secondary window (GDC/TRITON
                     * only; STORAGE / UNASSIGNED leave roleHrCount=0).
                     * Skip when the role read has never succeeded so
                     * the bridge doesn't see all-zero placeholder
                     * data on first boot. */
                    if (sample.roleHrValid && sample.roleHrCount > 0U) {
                        pb_len = build_orbit_sensor_bank_envelope(
                            envelope_buf, sizeof(envelope_buf),
                            ++bank_env_seq, idx,
                            sample.roleHrBase,
                            sample.roleHr, sample.roleHrCount,
                            this_seq);
                        if (pb_len > 0) {
                            NovaProto_SendRaw(envelope_buf, pb_len);
                        }
                    }
                }
            }
        }

        /* Phase 4b Sub-phase 3 (2026-06-01): emit VfdRegBank envelopes
         * for one drive's full snapshot per 100 ms tick, cycling through
         * all discovered drives. With up to 8 drives, each gets visited
         * every 800 ms — plenty fresh for the Level 2 Fans page (which
         * polls /iot/fans at ~5 s today). Each drive emits up to 6
         * banks (process / live / fault / limits / ramp / nameplate);
         * banks for register groups the polling task hasn't seen yet
         * are silently skipped (count==0 path inside the builder).
         *
         * Gated on broker install-mode to avoid contending with the OTA
         * UART burst — matches the OrbitSensorBank cadence above. */
        if (!NovaOtaBroker_IsInInstallMode()) {
            const size_t drv_count = NovaVfdClient_DriveCount();
            if (drv_count > 0U) {
                const uint8_t drv_idx =
                    (uint8_t)((tick / 1U) % drv_count);  /* one drive per tick */
                NovaVfdSnapshot vsnap;
                /* Find the Nth `known` drive by index. */
                uint8_t cur = 0;
                NovaVfdSnapshot pick;
                bool have_pick = false;
                for (uint8_t i = 0; i < 8U; i++) {  /* NOVA_VFD_MAX_DRIVES */
                    if (NovaVfdClient_GetSnapshot(i, &vsnap)) {
                        if (cur == drv_idx) { pick = vsnap; have_pick = true; break; }
                        cur++;
                    }
                }
                if (have_pick) {
                    static uint32_t s_vfd_bank_seq = 0;
                    const uint8_t  uid = pick.unit_id;
                    const uint32_t this_seq = ++s_vfd_bank_seq;
                    const bool online = pick.online;
                    size_t pb_len;
                    /* Process data (0..3) */
                    pb_len = build_vfd_reg_bank_envelope(
                        envelope_buf, sizeof(envelope_buf), ++bank_env_seq,
                        uid, 0U, pick.process, 4U, this_seq, online);
                    if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
                    /* Live (100..108) */
                    pb_len = build_vfd_reg_bank_envelope(
                        envelope_buf, sizeof(envelope_buf), ++bank_env_seq,
                        uid, 100U, pick.live, 9U, this_seq, online);
                    if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
                    /* Fault (400) */
                    pb_len = build_vfd_reg_bank_envelope(
                        envelope_buf, sizeof(envelope_buf), ++bank_env_seq,
                        uid, 400U, pick.fault, 1U, this_seq, online);
                    if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
                    /* Limits (2001..2002) */
                    pb_len = build_vfd_reg_bank_envelope(
                        envelope_buf, sizeof(envelope_buf), ++bank_env_seq,
                        uid, 2001U, pick.limits, 2U, this_seq, online);
                    if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
                    /* Ramp (2202..2203) */
                    pb_len = build_vfd_reg_bank_envelope(
                        envelope_buf, sizeof(envelope_buf), ++bank_env_seq,
                        uid, 2202U, pick.ramp, 2U, this_seq, online);
                    if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
                    /* Nameplate (9901..9905) */
                    pb_len = build_vfd_reg_bank_envelope(
                        envelope_buf, sizeof(envelope_buf), ++bank_env_seq,
                        uid, 9901U, pick.nameplate, 5U, this_seq, online);
                    if (pb_len > 0) NovaProto_SendRaw(envelope_buf, pb_len);
                }
            }
        }

        /* Phase 3.7: broker now runs on its own task (`ota_broker`)
         * and ticks itself on a 100 ms xSemaphoreTake timeout. The
         * NovaOtaBroker_Tick() entry point is retained as a no-op for
         * back-compat but no longer needs to be called from here. */

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
