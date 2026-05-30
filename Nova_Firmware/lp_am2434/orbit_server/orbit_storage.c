/*
 * orbit_storage.c — STORAGE-role Modbus register-map implementation.
 *
 * Mirrors orbit-simulator/src/orbitSimulator.ts STORAGE handlers.
 * See orbit_storage.h for the address map and concurrency contract.
 */

#include "orbit_storage.h"
#include "orbit_state.h"

#include <FreeRTOS.h>
#include <task.h>
#include <string.h>

/* --- Address ranges. --- */
#define HR_AO_BASE        0
#define HR_AO_MODE_BASE   2
#define HR_VFD_BASE       100
#define HR_VFD_END        (HR_VFD_BASE + ORBIT_VFD_BLOCK_SIZE)   /* 148 */
#define HR_SENSOR_BASE    200
#define HR_SENSOR_END     (HR_SENSOR_BASE + ORBIT_SENSOR_BLOCK_SIZE) /* 264 */
#define HR_STATUS_BASE    40000
#define HR_STATUS_COUNT   7

/* Bench DI-injection window. Until real DI hardware is wired, the
 * sensor-injector panel pokes synthetic DI states here. Read-back
 * round-trips whatever was last written or whatever
 * LpOrbitIo_LatchInputs swept in (live hardware wins automatically
 * because the sweep happens after these writes settle). Layout:
 *   41000..41009  DI 0..9   (0/1 each)
 *   41010         E-stop    (0/1)
 *   41011..41014  DC24V 0..3 (0/1 each)
 * Total 15 regs. Production builds need not strip this — when real
 * GPIO drives s->di[] every tick, injected values are overwritten
 * before the next FC02 read. */
#define HR_DI_INJECT_BASE   41000
#define HR_DI_INJECT_COUNT  (ORBIT_DI_COUNT + 1 + ORBIT_DC24V_COUNT)

#define DI_DIGITAL_END    ORBIT_DI_COUNT          /* 10 */
#define DI_ESTOP          (DI_DIGITAL_END)        /* 10 */
#define DI_DC24V_BASE     (DI_ESTOP + 1)          /* 11 */
#define DI_DC24V_END      (DI_DC24V_BASE + ORBIT_DC24V_COUNT) /* 15 */

/* Stamp `vfd_last_ok_secs[slot]` for every VFD-window slot covered by
 * an HR access [start, start+count). Activity-from-bridge: any read
 * or write that touches HR 100..147 counts as "the upstream master
 * cares about this drive, so it is alive". When orbit_vfd_rtu lands
 * the RTU master will overwrite with real-success times — same field,
 * monotonically increasing.
 *
 * Caller MUST hold OrbitState_Lock. Slots out of [0..ORBIT_VFD_DRIVES)
 * are never touched (no hardware drive backing them); the encoder
 * leaves those at 0 ("never seen") → UINT32_MAX on the wire. */
static void stamp_vfd_activity(OrbitStateData *s, uint16_t start, uint16_t count)
{
    if (count == 0u) return;
    uint16_t end = (uint16_t)(start + count);   /* exclusive */
    if (end <= HR_VFD_BASE || start >= HR_VFD_END) return;
    uint16_t lo = start < HR_VFD_BASE ? HR_VFD_BASE : start;
    uint16_t hi = end   > HR_VFD_END  ? HR_VFD_END  : end;
    uint16_t first_slot = (uint16_t)((lo - HR_VFD_BASE) / ORBIT_VFD_REGS_PER_DRIVE);
    uint16_t last_slot  = (uint16_t)(((hi - 1u) - HR_VFD_BASE) / ORBIT_VFD_REGS_PER_DRIVE);
    uint32_t now = s->uptime_sec;
    for (uint16_t slot = first_slot; slot <= last_slot; slot++) {
        if (slot < ORBIT_VFD_DRIVES) {
            s->vfd_last_ok_secs[slot] = now;
        }
    }
}

static uint16_t read_one_hr(const OrbitStateData *s, uint16_t addr,
                            uint8_t *exc)
{
    *exc = MB_EX_NONE;
    if (addr <= 1) {
        return s->ao_pct[addr];
    }
    if (addr >= HR_AO_MODE_BASE && addr < HR_AO_MODE_BASE + ORBIT_AO_COUNT) {
        return s->ao_mode[addr - HR_AO_MODE_BASE];
    }
    if (addr >= HR_VFD_BASE && addr < HR_VFD_END) {
        return s->vfd_regs[addr - HR_VFD_BASE];
    }
    if (addr >= HR_SENSOR_BASE && addr < HR_SENSOR_END) {
        return (uint16_t)s->sensor_block[addr - HR_SENSOR_BASE];
    }
    if (addr >= HR_STATUS_BASE && addr < HR_STATUS_BASE + HR_STATUS_COUNT) {
        switch (addr - HR_STATUS_BASE) {
            case 0: return s->board_id;
            case 1: return s->e_stop ? 1 : 0;
            case 2: return s->comm_lost ? 1 : 0;
            case 3: return s->safe_mode ? 1 : 0;
            case 4: return s->cpu_temp_x10;
            case 5: return (uint16_t)(s->uptime_sec & 0xFFFFu);
            case 6: return (uint16_t)((s->uptime_sec >> 16) & 0xFFFFu);
        }
    }
    if (addr >= HR_DI_INJECT_BASE && addr < HR_DI_INJECT_BASE + HR_DI_INJECT_COUNT) {
        uint16_t i = (uint16_t)(addr - HR_DI_INJECT_BASE);
        if (i < DI_DIGITAL_END) return s->di[i] ? 1 : 0;
        if (i == DI_ESTOP)      return s->e_stop ? 1 : 0;
        return s->dc24v[i - DI_DC24V_BASE] ? 1 : 0;
    }
    *exc = MB_EX_ILLEGAL_DATA_ADDRESS;
    return 0;
}

uint8_t storage_read_hr_block(uint16_t start, uint16_t count, uint16_t *out)
{
    if (count == 0 || count > 125) return MB_EX_ILLEGAL_DATA_VALUE;
    OrbitState_Lock();
    OrbitStateData *s = OrbitState_Get();
    /* Touch the watchdog: any successful read counts as the master
     * being alive. */
    OrbitState_TouchPoll(xTaskGetTickCount());
    /* Reads from the sensor block flag activity for the safemode
     * heuristic ("the master is actively polling sensor data"). */
    if (start >= HR_SENSOR_BASE && start < HR_SENSOR_END) {
        s->sensor_activity = true;
    }
    stamp_vfd_activity(s, start, count);
    uint8_t exc = MB_EX_NONE;
    for (uint16_t i = 0; i < count; i++) {
        out[i] = read_one_hr(s, start + i, &exc);
        if (exc != MB_EX_NONE) {
            OrbitState_Unlock();
            return exc;
        }
    }
    OrbitState_Unlock();
    return MB_EX_NONE;
}

static uint8_t write_one_hr(OrbitStateData *s, uint16_t addr, uint16_t value)
{
    if (addr <= 1) {
        if (value > 100) return MB_EX_ILLEGAL_DATA_VALUE;
        s->ao_pct[addr] = value;
        return MB_EX_NONE;
    }
    if (addr >= HR_AO_MODE_BASE && addr < HR_AO_MODE_BASE + ORBIT_AO_COUNT) {
        if (value > 1) return MB_EX_ILLEGAL_DATA_VALUE;
        s->ao_mode[addr - HR_AO_MODE_BASE] = (uint8_t)value;
        return MB_EX_NONE;
    }
    if (addr >= HR_VFD_BASE && addr < HR_VFD_END) {
        /* PHASE 2: forward the write to orbit_vfd_rtu so the real
         * drive sees it. For now, just cache locally so reads round-trip. */
        s->vfd_regs[addr - HR_VFD_BASE] = value;
        return MB_EX_NONE;
    }
    if (addr >= HR_SENSOR_BASE && addr < HR_SENSOR_END) {
        /* Sensor injection path. Until real RTU sensor boards arrive,
         * the bench/sim pushes engineering-format int16 values directly
         * into HR 200..263 over Modbus TCP. The controller LP then
         * reads them back via FC03 the same way it will read live
         * RTU data. When orbit_sensor_rtu binds a real transport, it
         * will overwrite these on the next sweep — which is the
         * intended behaviour: live data wins, injected stale values
         * get superseded. */
        s->sensor_block[addr - HR_SENSOR_BASE] = (int16_t)value;
        return MB_EX_NONE;
    }
    if (addr >= HR_STATUS_BASE && addr < HR_STATUS_BASE + HR_STATUS_COUNT) {
        /* Allow injecting a fake CPU temperature for thermal-alarm
         * testing; everything else in the status block is firmware-owned. */
        if ((addr - HR_STATUS_BASE) == 4) {
            s->cpu_temp_x10 = value;
            return MB_EX_NONE;
        }
        return MB_EX_ILLEGAL_DATA_ADDRESS;
    }
    if (addr >= HR_DI_INJECT_BASE && addr < HR_DI_INJECT_BASE + HR_DI_INJECT_COUNT) {
        uint16_t i = (uint16_t)(addr - HR_DI_INJECT_BASE);
        bool b = (value != 0u);
        if      (i < DI_DIGITAL_END) s->di[i] = b;
        else if (i == DI_ESTOP)      s->e_stop = b;
        else                         s->dc24v[i - DI_DC24V_BASE] = b;
        return MB_EX_NONE;
    }
    return MB_EX_ILLEGAL_DATA_ADDRESS;
}

uint8_t storage_write_hr_single(uint16_t addr, uint16_t value)
{
    OrbitState_Lock();
    OrbitStateData *s = OrbitState_Get();
    OrbitState_TouchPoll(xTaskGetTickCount());
    stamp_vfd_activity(s, addr, 1);
    uint8_t exc = write_one_hr(s, addr, value);
    OrbitState_Unlock();
    return exc;
}

uint8_t storage_write_hr_block(uint16_t start, uint16_t count, const uint16_t *vals)
{
    if (count == 0 || count > 123) return MB_EX_ILLEGAL_DATA_VALUE;
    OrbitState_Lock();
    OrbitStateData *s = OrbitState_Get();
    OrbitState_TouchPoll(xTaskGetTickCount());
    stamp_vfd_activity(s, start, count);
    uint8_t exc = MB_EX_NONE;
    for (uint16_t i = 0; i < count; i++) {
        exc = write_one_hr(s, start + i, vals[i]);
        if (exc != MB_EX_NONE) break;
    }
    OrbitState_Unlock();
    return exc;
}

/* --- Coils --- */

static bool read_one_coil(const OrbitStateData *s, uint16_t addr, uint8_t *exc)
{
    *exc = MB_EX_NONE;
    if (addr < ORBIT_DO_COUNT) return s->do_[addr];
    *exc = MB_EX_ILLEGAL_DATA_ADDRESS;
    return false;
}

uint8_t storage_read_coil_block(uint16_t start, uint16_t count, uint8_t *out_bytes)
{
    if (count == 0 || count > 2000) return MB_EX_ILLEGAL_DATA_VALUE;
    uint16_t bytes = (uint16_t)((count + 7u) / 8u);
    memset(out_bytes, 0, bytes);
    OrbitState_Lock();
    OrbitStateData *s = OrbitState_Get();
    OrbitState_TouchPoll(xTaskGetTickCount());
    uint8_t exc = MB_EX_NONE;
    for (uint16_t i = 0; i < count; i++) {
        bool b = read_one_coil(s, start + i, &exc);
        if (exc != MB_EX_NONE) { OrbitState_Unlock(); return exc; }
        if (b) out_bytes[i >> 3] |= (uint8_t)(1u << (i & 7u));
    }
    OrbitState_Unlock();
    return MB_EX_NONE;
}

static uint8_t write_one_coil(OrbitStateData *s, uint16_t addr, bool value)
{
    if (addr < ORBIT_DO_COUNT) {
        s->do_[addr] = value;
        /* PHASE 2: forward to GPIO HAL. */
        return MB_EX_NONE;
    }
    return MB_EX_ILLEGAL_DATA_ADDRESS;
}

uint8_t storage_write_coil_single(uint16_t addr, uint16_t value)
{
    if (value != 0xFF00 && value != 0x0000) return MB_EX_ILLEGAL_DATA_VALUE;
    OrbitState_Lock();
    OrbitStateData *s = OrbitState_Get();
    OrbitState_TouchPoll(xTaskGetTickCount());
    uint8_t exc = write_one_coil(s, addr, value == 0xFF00);
    OrbitState_Unlock();
    return exc;
}

uint8_t storage_write_coil_block(uint16_t start, uint16_t count, const uint8_t *bits)
{
    if (count == 0 || count > 1968) return MB_EX_ILLEGAL_DATA_VALUE;
    OrbitState_Lock();
    OrbitStateData *s = OrbitState_Get();
    OrbitState_TouchPoll(xTaskGetTickCount());
    uint8_t exc = MB_EX_NONE;
    for (uint16_t i = 0; i < count; i++) {
        bool b = (bits[i >> 3] >> (i & 7u)) & 1u;
        exc = write_one_coil(s, start + i, b);
        if (exc != MB_EX_NONE) break;
    }
    OrbitState_Unlock();
    return exc;
}

/* --- Discrete inputs --- */

static bool read_one_discrete(const OrbitStateData *s, uint16_t addr,
                              uint8_t *exc)
{
    *exc = MB_EX_NONE;
    if (addr < DI_DIGITAL_END) return s->di[addr];
    if (addr == DI_ESTOP)      return s->e_stop;
    if (addr >= DI_DC24V_BASE && addr < DI_DC24V_END) {
        return s->dc24v[addr - DI_DC24V_BASE];
    }
    *exc = MB_EX_ILLEGAL_DATA_ADDRESS;
    return false;
}

uint8_t storage_read_discrete_block(uint16_t start, uint16_t count, uint8_t *out_bytes)
{
    if (count == 0 || count > 2000) return MB_EX_ILLEGAL_DATA_VALUE;
    uint16_t bytes = (uint16_t)((count + 7u) / 8u);
    memset(out_bytes, 0, bytes);
    OrbitState_Lock();
    OrbitStateData *s = OrbitState_Get();
    OrbitState_TouchPoll(xTaskGetTickCount());
    uint8_t exc = MB_EX_NONE;
    for (uint16_t i = 0; i < count; i++) {
        bool b = read_one_discrete(s, start + i, &exc);
        if (exc != MB_EX_NONE) { OrbitState_Unlock(); return exc; }
        if (b) out_bytes[i >> 3] |= (uint8_t)(1u << (i & 7u));
    }
    OrbitState_Unlock();
    return MB_EX_NONE;
}
