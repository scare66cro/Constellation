/*
 * lp_device_config.h — Per-board provisioning record (OSPI-backed)
 *
 * Holds the small set of values that distinguishes one LP-AM2434 board
 * from another in the field:
 *
 *   role     — OrbitRole (CONTROLLER, STORAGE, GDC, TRITON, …)
 *   ip       — IPv4 address (host order). 0 → fallback default per role
 *              (CONTROLLER: 10.1.2.210, orbit roles: 10.1.2.249).
 *   netmask  — IPv4 netmask. 0 → 255.255.255.0
 *   gateway  — IPv4 gateway. 0 → derive from IP (last octet → .1)
 *   board_id — Optional 1-based board ID for orbit roles (0 if unset).
 *              Mirrors the orbit-simulator's `state.id` so multi-orbit
 *              sites can address each board by Modbus unit ID = board_id.
 *
 * ─── OSPI persistence layout ─────────────────────────────────────────
 *
 * Two 64 KiB ping-pong banks at fixed offsets in the on-board OSPI
 * NOR flash (W25Q64JV, 8 MB total, mapped at 0x60000000):
 *
 *   Bank A : OSPI 0x00600000 .. 0x0060FFFF  (64 KiB)
 *   Bank B : OSPI 0x00610000 .. 0x0061FFFF  (64 KiB)
 *
 * These offsets sit at the start of the proto-defined "Settings vault"
 * region (`proto/agristar/firmware.proto`: 0x600000-0x7FFFFF, 2 MB).
 * The earlier choice of 0x200000/0x210000 collided with proto-defined
 * Bank B (0x200000-0x3FFFFF, the inactive firmware image slot) — Phase
 * 1B/Phase 3 OTA writes would clobber the device-config record.
 * Migration on first boot of this firmware: if both new banks read
 * blank, fall back to scanning the legacy 0x200000/0x210000 banks and
 * — if a valid record is found — re-save it forward to the new banks.
 * Legacy sectors are NOT erased here; the next OTA push to Bank B
 * will erase them as part of normal flash programming.
 *
 *   - The XIP firmware image lives at OSPI 0x00080000 .. ~0x000CC000
 *     (~315 KiB). The syscfg FLASH region reserves 0x00100000 (1 MB)
 *     ending at 0x00200000 — so 0x00200000 is the first guaranteed-
 *     free byte past the XIP image.
 *   - 64 KiB per bank = 16 × 4 KiB sector erases, well within the
 *     W25Q64JV's per-sector 100k erase-cycle endurance for an
 *     occasionally-written provisioning record.
 *   - Banks A and B sit on adjacent 64 KiB block-erase boundaries so
 *     `LpDeviceConfig_Erase()` can wipe each with a single 64 KiB
 *     block-erase command (or 16 sector-erases, whichever the SDK
 *     Flash driver picks).
 *
 * Bank-on-flash byte layout (only the first ~80 bytes are populated;
 * the remaining 64 KiB stays at the post-erase 0xFF):
 *
 *   off  size  field
 *   0    4     bank_magic = LP_DEVCFG_BANK_MAGIC ("LPDB")
 *   4    4     sequence    (monotonically increasing across saves)
 *   8    4     payload_size = sizeof(LpDeviceConfig)
 *   12   N     payload bytes (full LpDeviceConfig record)
 *   12+N 4     crc32 over bytes [0 .. 12+N) — Ethernet/zlib polynomial
 *
 * Boot picks the bank with the highest `sequence` whose CRC verifies.
 * If both banks are blank or corrupt, we fall back to the compile-time
 * defaults so the unit still boots and reaches the network. If OSPI
 * itself fails to init (bench unit with no flash, bus error), we log
 * and fall back identically — boot must NEVER hang on a flash error.
 *
 * Provisioning flow:
 *   1. Bench-flash a fresh LP with default firmware → comes up at
 *      10.1.2.249 (orbit fallback) or 10.1.2.210 (controller fallback)
 *      with role=CONTROLLER.
 *   2. Operator runs `POST /iot/lp/provision` against the bridge
 *      (controller LP forwards to the new board over LAN, OR — for the
 *      first connection — operator runs the script directly against
 *      the new board's bench IP).
 *   3. Provision request writes role + ip via lp_device_config and
 *      asks for reboot. New board comes up at its assigned IP+role.
 *
 * The header layout is intentionally simple (not protobuf) because this
 * is read at very early boot before the proto stack is up.
 */
#ifndef LP_DEVICE_CONFIG_H
#define LP_DEVICE_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "orbit_server/orbit_role.h"

/* Magic constant in the OSPI record header. Any other value → record is
 * absent / corrupt → we fall back to compile-time defaults. */
#define LP_DEVCFG_MAGIC   0x4C504443U   /* "LPDC" — payload struct magic */

/* Magic at offset 0 of each OSPI bank — distinct from the payload's
 * own LP_DEVCFG_MAGIC so a stray cross-load between the two cannot
 * pass validation. */
#define LP_DEVCFG_BANK_MAGIC  0x4C504442U   /* "LPDB" */

#define LP_DEVCFG_VERSION 1U

/* OSPI bank layout — see header doc-block above. */
#define LP_DEVCFG_BANK_SIZE     0x10000U     /* 64 KiB per bank */
#define LP_DEVCFG_BANK_A_OFF    0x00600000U  /* Settings vault region */
#define LP_DEVCFG_BANK_B_OFF    0x00610000U  /* adjacent 64 KiB block */

/* ─── Legacy bank offsets (pre-2026-05) ─────────────────────────────────
 * The original lp_device_config layout placed the ping-pong banks at
 * 0x00200000 / 0x00210000, which collides with the proto-defined
 * Bank B firmware image slot (0x200000-0x3FFFFF). LpDeviceConfig_Init
 * scans these offsets as a one-shot fallback when the new banks read
 * blank, so already-deployed boards migrate forward on the first boot
 * after this firmware update without losing their provisioned
 * role/IP/board_id. */
#define LP_DEVCFG_LEGACY_BANK_A_OFF  0x00200000U
#define LP_DEVCFG_LEGACY_BANK_B_OFF  0x00210000U

typedef struct {
    uint32_t  magic;        /* LP_DEVCFG_MAGIC */
    uint32_t  version;      /* LP_DEVCFG_VERSION */
    uint32_t  role;         /* OrbitRole */
    uint32_t  ip;           /* IPv4 host order (0 → role-default) */
    uint32_t  netmask;      /* 0 → 255.255.255.0 */
    uint32_t  gateway;      /* 0 → derive a.b.c.1 */
    uint32_t  board_id;     /* 0 → unassigned */
    uint32_t  reserved[8];  /* future: hostname, mac override, vlan, ... */
    uint32_t  crc32;        /* Legacy field — bank-level CRC supersedes
                             * this; kept zero on save to preserve struct
                             * layout for callers and forward compatibility. */
} LpDeviceConfig;

/* Compile-time fallbacks. Used when OSPI record is missing or corrupt.
 * Subnet: 10.47.27.0/24 — airgapped Constellation private LAN. No
 * gateway (boards are isolated; the rpi5 bridges to the office LAN on
 * its own NIC). Gateway = 0 → lwIP omits default route. */
#define LP_DEVCFG_DEFAULT_IP_CONTROLLER 0x0A2F1B01U   /* 10.47.27.1   */
#define LP_DEVCFG_DEFAULT_IP_ORBIT      0x0A2F1BF9U   /* 10.47.27.249 (unprov fallback) */
#define LP_DEVCFG_DEFAULT_NETMASK       0xFFFFFF00U   /* 255.255.255.0 */
#define LP_DEVCFG_DEFAULT_GATEWAY       0x00000000U   /* none — airgapped */

/* Read the OSPI record and populate the in-RAM cache. Idempotent;
 * called once from main() before the scheduler starts. */
void LpDeviceConfig_Init(void);

/* Read-only accessor. Always returns a valid populated struct (filled
 * with role-appropriate defaults if OSPI was empty/corrupt). */
const LpDeviceConfig *LpDeviceConfig_Get(void);

/* Convenience: applied IPv4 (with role default substitution). */
uint32_t LpDeviceConfig_GetIp(void);
uint32_t LpDeviceConfig_GetNetmask(void);
uint32_t LpDeviceConfig_GetGateway(void);

/* Persist a new record to OSPI. Writes to the *inactive* bank then
 * flips active. Caller is responsible for issuing a reboot afterwards
 * to apply the new role/IP. Returns true on durable success. */
bool LpDeviceConfig_Save(const LpDeviceConfig *cfg);

/* Wipe both OSPI banks back to 0xFF (factory reset). Gated behind an
 * explicit `confirm_magic` argument so a typo can't trigger it; pass
 * `LP_DEVCFG_ERASE_CONFIRM` to actually perform the erase. NOT exposed
 * over Modbus — only callable from C code (e.g. a hardware-jumper test
 * pattern at boot). Returns true if both banks erased cleanly. */
#define LP_DEVCFG_ERASE_CONFIRM  0xE7A5E700U   /* "ERASE" */
bool LpDeviceConfig_Erase(uint32_t confirm_magic);

#endif /* LP_DEVICE_CONFIG_H */
