/*
 * orbit_modbus_tcp.h — Modbus TCP slave server for orbit roles
 *
 * Listens on port 5502 (matches orbit-simulator) and dispatches
 * supported function codes to the role-specific register-map module
 * (orbit_storage.c for STORAGE; GDC and TRITON modules added later).
 *
 * One server task accept()s connections; each accepted socket is
 * serviced inline (we keep at most ORBIT_MBT_MAX_CONNS concurrent
 * sockets via select(); no per-connection task spawn). This matches
 * the orbit-simulator's "single Node loop" model and is well within
 * the LP's lwIP socket budget.
 *
 * Supported FCs:
 *   01 Read Coils
 *   02 Read Discrete Inputs
 *   03 Read Holding Registers
 *   04 Read Input Registers (aliased to FC03)
 *   05 Write Single Coil
 *   06 Write Single Register
 *   15 Write Multiple Coils
 *   16 Write Multiple Registers
 *
 * Anything else returns MB_EX_ILLEGAL_FUNCTION.
 */
#ifndef ORBIT_MODBUS_TCP_H
#define ORBIT_MODBUS_TCP_H

#include <stdint.h>

#define ORBIT_MBT_PORT          5502
#define ORBIT_MBT_MAX_CONNS     4

/* FreeRTOS task entry. Spawn from main() only when role != CONTROLLER. */
void orbit_modbus_tcp_task(void *args);

#endif /* ORBIT_MODBUS_TCP_H */
