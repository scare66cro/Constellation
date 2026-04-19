/**
 * nova_dataexc.h — Constellation data exchange layer
 *
 * Replaces DataExc.c from Mini_IO. This is the main serial communication
 * orchestrator — it decides WHEN to send messages and handles incoming
 * commands from the bridge.
 *
 * Architecture (replaces the RTS/ACK/REPOST state machine):
 *   TX: Firmware pushes data automatically (no polling from bridge)
 *     - SystemStatus: every 1s or on change
 *     - EquipmentStatus: every 1s or on change
 *     - Warnings: on change only
 *     - Heartbeat: every 5s
 *     - Settings pages: on connect (all) or on change (individual)
 *
 *   RX: Bridge sends commands, firmware replies with ACK
 *     - EquipmentCmd → ACK
 *     - SettingsUpdate → ACK + updated settings page
 *     - SystemCmd → ACK + action
 *     - DataRequest → requested data
 */
#ifndef NOVA_DATAEXC_H
#define NOVA_DATAEXC_H

#include <stdint.h>
#include <stdbool.h>

/* ─── Connection state ────────────────────────────────────────────────── */
typedef enum {
    NOVA_STATE_DISCONNECTED,   /* Waiting for bridge DataRequest */
    NOVA_STATE_CONNECTED,      /* Settings sent, pushing live data */
} NovaConnState;

/* ─── Init / tick ─────────────────────────────────────────────────────── */

/**
 * One-shot wiring of NovaProto + NovaDataExc to UART1 (RPi5 bridge).
 * Defined in nova_thread_overrides.c (it owns the UART tx callback).
 * Call from main.c after Usart_Init().
 */
void NovaBridge_Init(void);

/**
 * Initialize the data exchange layer.
 * Call once at startup after NovaProto_Init().
 */
void NovaDataExc_Init(void);

/**
 * Periodic tick — call from the main control loop (typically every 50ms).
 * Handles:
 *   - Push scheduling (send status messages at configured intervals)
 *   - Heartbeat generation
 *   - Connection timeout detection
 */
void NovaDataExc_Tick(void);

/**
 * Get current connection state.
 */
NovaConnState NovaDataExc_GetState(void);

/* ─── Change notification flags ───────────────────────────────────────── */
/* Set these from application code when data changes. The tick function
 * will send the appropriate messages. This replaces the UI_xxxxUpdate
 * flag pattern from the original firmware. */

extern volatile bool NovaDataExc_SettingsChanged;
extern volatile bool NovaDataExc_WarningsChanged;
extern volatile bool NovaDataExc_EquipChanged;
extern volatile bool NovaDataExc_IoConfigChanged;

#endif /* NOVA_DATAEXC_H */
