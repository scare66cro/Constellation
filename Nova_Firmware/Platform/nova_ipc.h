/*
 * nova_ipc.h — Inter-core communication for AM2434 AMP architecture
 *
 * Shared memory layout for 4× Cortex-R5F cores:
 *   R5F-0: Control engine (all PID, state machines, Modbus TCP, equipment)
 *   R5F-1: Communications bridge (RPi5 UART, packet framing, OTA receiver)
 *   R5F-2: Safety watchdog (heartbeat monitor, safe-state enforcement)
 *   R5F-3: Spare (sleep)
 *
 * IPC uses a fixed shared-memory region visible to all cores.
 * No hardware mailbox interrupts yet — cores poll the shared region.
 * The AM2434 mailbox peripheral can be added later for interrupt-driven IPC.
 *
 * QEMU layout (flat):
 *   0x00000000 – 0x0017FFFF  R5F-0 (1536 KB)
 *   0x00180000 – 0x001BFFFF  R5F-1 (256 KB)
 *   0x001C0000 – 0x001DFFFF  R5F-2 (128 KB)
 *   0x001E0000 – 0x001FFFFF  IPC shared (128 KB)
 *
 * Production layout (real silicon):
 *   Each core has its own ATCM (64KB) + BTCM (64KB)
 *   MSRAM (2MB) partitioned: R5F-0 1536KB, R5F-1 256KB, R5F-2 128KB, IPC 128KB
 *   Details in per-core linker scripts.
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */
#ifndef NOVA_IPC_H
#define NOVA_IPC_H

#include <stdint.h>

/* ─── QEMU flat-memory addresses ──────────────────────────────────────── */
#define IPC_BASE_ADDR           0x001E0000
#define IPC_REGION_SIZE         0x00020000   /* 128 KB */

/* ─── IPC Magic + Version ─────────────────────────────────────────────── */
#define IPC_MAGIC               0x49504321   /* "IPC!" */
#define IPC_VERSION             1

/* ─── Core IDs ────────────────────────────────────────────────────────── */
#define CORE_ID_CONTROL         0   /* R5F-0: control engine */
#define CORE_ID_COMMS           1   /* R5F-1: communications bridge */
#define CORE_ID_WATCHDOG        2   /* R5F-2: safety watchdog */
#define CORE_ID_SPARE           3   /* R5F-3: spare */
#define CORE_COUNT              4

/* ─── Core States ─────────────────────────────────────────────────────── */
#define CORE_STATE_OFF          0   /* Not booted */
#define CORE_STATE_BOOTING      1   /* In startup */
#define CORE_STATE_RUNNING      2   /* Normal operation */
#define CORE_STATE_ERROR        3   /* Fault detected */
#define CORE_STATE_SAFE         4   /* Forced safe-state by watchdog */

/* ─── Watchdog Configuration ──────────────────────────────────────────── */
#define WDG_HEARTBEAT_HZ        10      /* R5F-0 updates heartbeat 10×/sec */
#define WDG_TIMEOUT_MS          500     /* Watchdog trips after 500ms silence */
#define WDG_MAX_STRIKES         3       /* Consecutive failures → safe-state hold */

/* ─── Message Ring (core-to-core) ─────────────────────────────────────── */
/* Each directed channel (e.g. R5F-0 → R5F-1) has a small ring buffer
 * in shared memory for passing commands/status without interrupts.
 * 16 slots × 64 bytes = 1 KB per channel. */
#define IPC_MSG_SLOTS           16
#define IPC_MSG_MAX_SIZE        60      /* 60 bytes payload + 4 byte header */

typedef struct __attribute__((packed)) {
    uint16_t type;          /* Message type (application-defined) */
    uint16_t length;        /* Payload length (0..IPC_MSG_MAX_SIZE) */
    uint8_t  payload[IPC_MSG_MAX_SIZE];
} IpcMessage;

typedef struct __attribute__((packed)) {
    volatile uint32_t write_idx;    /* Written by sender */
    volatile uint32_t read_idx;     /* Written by receiver */
    IpcMessage        slots[IPC_MSG_SLOTS];
} IpcRing;

/* ─── Per-Core Heartbeat Block ────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    volatile uint32_t counter;      /* Monotonic counter, incremented by owning core */
    volatile uint32_t state;        /* CORE_STATE_xxx */
    volatile uint32_t uptime_ms;    /* Milliseconds since boot */
    volatile uint32_t error_code;   /* Last error (0 = none) */
    char              version[32];  /* Firmware version string */
    uint8_t           reserved[12]; /* Pad to 64 bytes */
} IpcHeartbeat;

/* ─── Watchdog Control Block (written by R5F-2 only) ──────────────────── */
typedef struct __attribute__((packed)) {
    volatile uint32_t safe_state_active;    /* 1 = watchdog has forced safe-state */
    volatile uint32_t trip_count;           /* How many times watchdog has tripped */
    volatile uint32_t last_trip_time_ms;    /* Uptime when last trip occurred */
    volatile uint32_t strike_count;         /* Consecutive strikes (cleared on recovery) */
    volatile uint32_t monitored_cores;      /* Bitmask: which cores watchdog monitors */
    uint8_t           reserved[44];         /* Pad to 64 bytes */
} IpcWatchdogCtrl;

/* ─── Shared Data Block (R5F-0 → R5F-1) ──────────────────────────────── */
/* R5F-0 writes equipment status, sensor data, etc. here.
 * R5F-1 reads it to build UART packets for the RPi5.
 * This avoids passing large buffers through the ring — R5F-1 just
 * reads the latest snapshot directly. */
#define IPC_SHARED_DATA_SIZE    (16 * 1024)   /* 16 KB */

typedef struct __attribute__((packed)) {
    volatile uint32_t sequence;         /* Incremented on each update */
    volatile uint32_t data_ready;       /* Bitflags: which sections are fresh */
    uint8_t           data[IPC_SHARED_DATA_SIZE - 8];
} IpcSharedData;

/* ─── Master IPC Structure ────────────────────────────────────────────── */
/* This is the root structure at IPC_BASE_ADDR.
 * Total size: ~40 KB (well within 128 KB IPC region). */

typedef struct __attribute__((packed)) {
    /* Header (64 bytes) */
    uint32_t        magic;              /* Must be IPC_MAGIC */
    uint32_t        version;            /* IPC_VERSION */
    uint32_t        init_core;          /* Which core initialized the region */
    uint32_t        reserved_hdr[13];   /* Pad header to 64 bytes */

    /* Per-core heartbeats (4 × 64 = 256 bytes) */
    IpcHeartbeat    heartbeat[CORE_COUNT];

    /* Watchdog control (64 bytes) */
    IpcWatchdogCtrl watchdog;

    /* Message rings: directed channels (each ~1 KB) */
    /* [src][dst] — only a few channels are used: */
    IpcRing         ring_ctrl_to_comms;     /* R5F-0 → R5F-1 */
    IpcRing         ring_comms_to_ctrl;     /* R5F-1 → R5F-0 */
    IpcRing         ring_ctrl_to_wdg;       /* R5F-0 → R5F-2 (config/ack) */
    IpcRing         ring_wdg_to_ctrl;       /* R5F-2 → R5F-0 (trip events) */

    /* Shared data snapshot (R5F-0 → R5F-1, 16 KB) */
    IpcSharedData   shared_data;

} IpcRegion;

/* ─── Accessor ────────────────────────────────────────────────────────── */
#define IPC ((volatile IpcRegion *)IPC_BASE_ADDR)

/* ─── IPC Message Types ───────────────────────────────────────────────── */
/* Control → Comms */
#define IPC_MSG_UART_TX             1   /* Forward encoded packet to RPi5 UART */
#define IPC_MSG_STATUS_UPDATE       2   /* New equipment/sensor snapshot ready */

/* Comms → Control */
#define IPC_MSG_UART_RX             10  /* Received command from RPi5 */
#define IPC_MSG_FW_CHUNK            11  /* OTA firmware data chunk */
#define IPC_MSG_FW_BEGIN            12  /* OTA update start */
#define IPC_MSG_FW_FINALIZE         13  /* OTA update complete */

/* Control → Watchdog */
#define IPC_MSG_WDG_CONFIG          20  /* Watchdog configuration update */
#define IPC_MSG_WDG_PET             21  /* Explicit pet (beyond heartbeat) */

/* Watchdog → Control */
#define IPC_MSG_WDG_TRIP            30  /* Watchdog trip notification */
#define IPC_MSG_WDG_RECOVERED       31  /* Core recovered after trip */

/* ─── Ring Buffer Helpers ─────────────────────────────────────────────── */

static inline int ipc_ring_empty(volatile IpcRing *ring) {
    return ring->read_idx == ring->write_idx;
}

static inline int ipc_ring_full(volatile IpcRing *ring) {
    return ((ring->write_idx + 1) % IPC_MSG_SLOTS) == ring->read_idx;
}

static inline int ipc_ring_push(volatile IpcRing *ring, const IpcMessage *msg) {
    if (ipc_ring_full(ring)) return -1;
    uint32_t idx = ring->write_idx;
    /* Copy message to slot */
    volatile IpcMessage *slot = &ring->slots[idx];
    slot->type = msg->type;
    slot->length = msg->length;
    for (uint32_t i = 0; i < msg->length && i < IPC_MSG_MAX_SIZE; i++) {
        slot->payload[i] = msg->payload[i];
    }
    /* Memory barrier before publishing write index */
    __asm volatile("DMB" ::: "memory");
    ring->write_idx = (idx + 1) % IPC_MSG_SLOTS;
    return 0;
}

static inline int ipc_ring_pop(volatile IpcRing *ring, IpcMessage *msg) {
    if (ipc_ring_empty(ring)) return -1;
    uint32_t idx = ring->read_idx;
    volatile const IpcMessage *slot = &ring->slots[idx];
    msg->type = slot->type;
    msg->length = slot->length;
    for (uint32_t i = 0; i < slot->length && i < IPC_MSG_MAX_SIZE; i++) {
        msg->payload[i] = slot->payload[i];
    }
    /* Memory barrier before publishing read index */
    __asm volatile("DMB" ::: "memory");
    ring->read_idx = (idx + 1) % IPC_MSG_SLOTS;
    return 0;
}

#endif /* NOVA_IPC_H */
