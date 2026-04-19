/**
 * nova_messages.h — Protobuf message builders for Constellation
 *
 * Replaces UI_Messages.c from Mini_IO. Each function reads from the
 * firmware's Settings/State globals and builds a nanopb-encoded
 * Envelope message ready for NovaProto_SendRaw().
 *
 * NOTE: Until nanopb codegen is run, this file provides the interface
 * contract. The actual nanopb struct types will come from generated headers.
 * For now we use a raw-byte encoding approach that can be replaced with
 * nanopb calls once the generated .pb.h files exist.
 */
#ifndef NOVA_MESSAGES_H
#define NOVA_MESSAGES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ─── Message IDs matching Envelope oneof field numbers ───────────────── */
/* These correspond to the field numbers in envelope.proto */

/* Firmware → Bridge: push/periodic */
#define MSG_SYSTEM_STATUS       10
#define MSG_EQUIPMENT_STATUS    11
#define MSG_WARNING_REPORT      12
#define MSG_SENSOR_DATA         13
#define MSG_RUNTIMES            14
#define MSG_HUMID_MODES         15
#define MSG_AUX_SWITCHES        16
#define MSG_DATA_LOAD_STATUS    17
#define MSG_FAN_RUNTIME         18

/* Firmware → Bridge: settings/config data */
#define MSG_BASIC_SETUP         20
#define MSG_DATE_TIME           21
#define MSG_VERSION_INFO        22
#define MSG_SERVICE_INFO        23
#define MSG_IO_CONFIG           24
#define MSG_IO_DEFINITION       25
#define MSG_ANALOG_BOARD        26
#define MSG_AVAILABLE_IO        27
#define MSG_SENSOR_LABELS       28
#define MSG_ACCOUNT_SETTINGS    29

/* Firmware → Bridge: responses */
#define MSG_LOG_CHUNK           30
#define MSG_PASSWORD_RESPONSE   31

/* Firmware → Bridge: settings pages */
#define MSG_PLENUM_SETTINGS     40
#define MSG_FAN_SPEED_SETTINGS  41
#define MSG_FAN_BOOST_SETTINGS  42
#define MSG_RAMP_RATE_SETTINGS  43
#define MSG_REFRIG_SETTINGS     44
#define MSG_BURNER_SETTINGS     45
#define MSG_CO2_SETTINGS        46
#define MSG_CURE_SETTINGS       47
#define MSG_CLIMACELL_SETTINGS  48
#define MSG_CLIMACELL_TIMES     49
#define MSG_HUMID_CTRL_SETTINGS 50
#define MSG_OUTSIDE_AIR_SETTINGS 51
#define MSG_MISC_SETTINGS       52
#define MSG_FAILURE_SETTINGS    53
#define MSG_FAILURE_SETTINGS2   54
#define MSG_TEMP_ALARM_SETTINGS 55
#define MSG_DOOR_SETTINGS       56
#define MSG_LOAD_MONITOR        57
#define MSG_AUX_PROGRAM         58
#define MSG_USER_LOG_SETTINGS   59
#define MSG_PID_SETTINGS        60
#define MSG_GRAPH_FAVORITES     61
#define MSG_EMAIL_SETTINGS      62
#define MSG_ALERT_SETTINGS      63
#define MSG_PWM_SETTINGS        64
#define MSG_NETWORK_NODES       65

/* Firmware → Bridge: log data push */
#define MSG_LOG_RECORD          70
#define MSG_ACTIVITY_EVENT      71

/* Bridge → Firmware: commands */
#define MSG_EQUIPMENT_CMD       80
#define MSG_REFRIG_DIAG_CMD     81
#define MSG_SYSTEM_CMD          82
#define MSG_LOG_QUERY           83
#define MSG_PASSWORD_AUTH       84
#define MSG_NETWORK_NODE_CMD    85
#define MSG_IO_NAME_UPDATE      86

/* Bridge → Firmware: settings updates */
#define MSG_SETTINGS_UPDATE     90

/* Protocol control */
#define MSG_ACK                100
#define MSG_HEARTBEAT          101
#define MSG_DATA_REQUEST       102

/* Firmware update (110-119) */
#define MSG_FW_BEGIN_UPDATE    110
#define MSG_FW_DATA_CHUNK      111
#define MSG_FW_FINALIZE_UPDATE 112
#define MSG_FW_ACTIVATE_BANK   113
#define MSG_FW_UPDATE_STATUS   114
#define MSG_FW_BANK_INFO       115

/* Orbit module management (120-129) */
#define MSG_ORBIT_STATUS       120
#define MSG_ORBIT_DISCOVERY    121
#define MSG_ORBIT_ROLE_ASSIGN  122

/* ─── Sequence number management ──────────────────────────────────────── */

/**
 * Get the next sequence number (monotonically increasing, wraps at 2^32).
 */
uint32_t NovaMsg_NextSeq(void);

/* ─── Message send functions ──────────────────────────────────────────── */
/* Each function reads from Settings/State globals, builds an Envelope,
 * and calls NovaProto_SendRaw(). Returns true if sent successfully.
 *
 * These replace the UI_Send* functions from Mini_IO/Application/UI_Messages.c
 */

bool NovaMsg_SendSystemStatus(void);      /* Replaces UI_SendMain */
bool NovaMsg_SendEquipmentStatus(void);   /* Replaces UI_SendEquipStatus */
bool NovaMsg_SendMode(void);              /* Replaces UI_SendMode (embedded in SystemStatus) */
bool NovaMsg_SendDateTime(void);          /* Replaces UI_SendDateTime */
bool NovaMsg_SendBasicSetup(void);        /* Replaces UI_SendBasicSetup */
bool NovaMsg_SendVersionInfo(void);       /* Replaces UI_SendVersions */
bool NovaMsg_SendServiceInfo(void);       /* Replaces UI_SendService */
bool NovaMsg_SendWarnings(void);          /* Replaces UI_SendWarnings */
bool NovaMsg_SendHeartbeat(void);         /* New: periodic keepalive */
bool NovaMsg_SendDataLoadStatus(bool ready, uint32_t session_id);

/* Settings pages */
bool NovaMsg_SendPlenumSettings(void);    /* Replaces UI_SendPlenSetPoints */
bool NovaMsg_SendFanSpeedSettings(void);  /* Replaces UI_SendFanSpeed */
bool NovaMsg_SendFanBoostSettings(void);  /* Replaces UI_SendFanBoost */
bool NovaMsg_SendRampRateSettings(void);  /* Replaces UI_SendRampRate */
bool NovaMsg_SendRefrigSettings(void);    /* Replaces UI_SendRefrig */
bool NovaMsg_SendBurnerSettings(void);    /* Replaces UI_SendBurner */
bool NovaMsg_SendCo2Settings(void);       /* Replaces UI_SendCo2 */
bool NovaMsg_SendCureSettings(void);      /* Replaces UI_SendAirCure */
bool NovaMsg_SendClimacellSettings(void); /* Replaces UI_SendClimacell */
bool NovaMsg_SendClimacellTimes(void);    /* Replaces UI_SendClimacellTimes */
bool NovaMsg_SendHumidCtrlSettings(void); /* Replaces UI_SendHumCtrl */
bool NovaMsg_SendOutsideAirSettings(void);/* Replaces UI_SendOutsideAir */
bool NovaMsg_SendMiscSettings(void);      /* Replaces UI_SendMisc */
bool NovaMsg_SendFailureSettings(void);   /* Replaces UI_SendFailures */
bool NovaMsg_SendFailureSettings2(void);  /* Replaces UI_SendFailures2 */
bool NovaMsg_SendTempAlarmSettings(void); /* Replaces UI_SendTempDevAlarms */
bool NovaMsg_SendDoorSettings(void);      /* Replaces UI_SendDoor */
bool NovaMsg_SendLoadMonitorSettings(void);/* Replaces UI_SendLoadMonitor */
bool NovaMsg_SendAuxProgram(void);        /* Replaces UI_SendAuxProgram */
bool NovaMsg_SendUserLogSettings(void);   /* Replaces UI_SendUserLogSettings */
bool NovaMsg_SendPidSettings(void);       /* Replaces UI_SendPIDSettings */
bool NovaMsg_SendGraphFavorites(void);    /* Replaces UI_SendGraphFavorites */
bool NovaMsg_SendEmailSettings(void);     /* Replaces UI_SendEmail */
bool NovaMsg_SendAlertSettings(void);     /* Replaces UI_SendEmailAlertFlags */
bool NovaMsg_SendPwmSettings(void);       /* Replaces UI_SendPWMChannels */

/* I/O and sensors */
bool NovaMsg_SendIoConfig(void);          /* Replaces UI_SendIoConfig */
bool NovaMsg_SendIoDefinition(void);      /* Replaces UI_SendIoDefinition (was MultiMsg) */
bool NovaMsg_SendAvailableIo(void);       /* Replaces UI_SendAvailableIo */
bool NovaMsg_SendAnalogBoard(uint32_t board_index); /* Replaces UI_SendAnalogBoard */
bool NovaMsg_SendSensorData(void);        /* Replaces UI_SendSensorData */
bool NovaMsg_SendSensorLabels(void);      /* Replaces UI_SendSensorLabels */

/* Equipment state */
bool NovaMsg_SendRuntimes(void);          /* Replaces UI_SendRuntimes */
bool NovaMsg_SendFanRuntime(void);        /* Replaces UI_SendFanDailyRun / FanTotalRun */
bool NovaMsg_SendHumidModes(void);        /* Replaces UI_SendHumidModes */
bool NovaMsg_SendAuxSwitches(void);       /* Replaces UI_SendAuxSwitches */

/* Accounts */
bool NovaMsg_SendAccountSettings(void);   /* Replaces UI_SendAccounts */
bool NovaMsg_SendNetworkNodes(void);      /* Replaces UI_SendNetworkNodes */

/* Response messages */
bool NovaMsg_SendAck(uint32_t ref_seq, uint8_t status);
bool NovaMsg_SendPasswordResponse(int8_t level);

/* Log data streaming */
bool NovaMsg_SendLogChunk(uint32_t log_type, uint32_t chunk_index,
                          uint32_t total_chunks,
                          const uint8_t *data, size_t data_len,
                          const char *metadata);

/* Log record push (replaces SD card UserLogWrite) */
bool NovaMsg_SendLogRecord(void);

/**
 * Encode a LogRecord's inner payload (without Envelope wrapper) into buf.
 * Returns encoded size, or 0 on overflow. The sequence number is embedded
 * in the payload so the bridge can detect gaps after ring-buffer replay.
 */
size_t NovaMsg_EncodeLogRecord(uint8_t *buf, size_t bufsize, uint32_t sequence);

/**
 * Send a pre-encoded LogRecord payload wrapped in an Envelope.
 * Used by the ring buffer drain loop in nova_dataexc.c.
 */
bool NovaMsg_SendLogRecordRaw(const uint8_t *inner, size_t inner_len);

/* Activity event push (replaces SD card SysLogWrite) */
bool NovaMsg_SendActivityEvent(uint32_t event_type, uint32_t eq_index,
                               const char *description, uint32_t new_state);

/* ─── Batch send: all settings after connection ───────────────────────── */
/**
 * Send all settings pages in sequence (replaces UI_SendAllSettings).
 * Called after successful handshake / DataRequest(REQ_ALL_SETTINGS).
 */
void NovaMsg_SendAllSettings(void);

/* ─── Firmware update messages ────────────────────────────────────────── */
bool NovaMsg_SendFwUpdateStatus(uint32_t state, uint32_t bytes_written,
                                 uint32_t total_size, uint32_t error_code,
                                 const char *error_message, uint32_t active_bank);
bool NovaMsg_SendFwBankInfo(void);

/* ─── Orbit module messages ───────────────────────────────────────────── */
/**
 * Send periodic orbit board health status (connected boards only).
 * Called every ~5s from NovaDataExc_Tick().
 */
bool NovaMsg_SendOrbitStatus(void);

/**
 * Send full orbit discovery results (all slots, connected or not).
 * Called on DataRequest(REQ_ORBIT_DISCOVERY) or startup.
 */
bool NovaMsg_SendOrbitDiscovery(void);

#endif /* NOVA_MESSAGES_H */
