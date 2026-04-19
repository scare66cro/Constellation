/*
 * system.h — System globals for Constellation Nova
 *
 * Drop-in replacement for Mini_IO/Platform/system.h.
 * Application code includes this for uptime, clock speed, etc.
 *
 * Copyright (c) 2026 Agristar
 */

#ifndef SYSTEM_H
#define SYSTEM_H

#include "hal.h"
#include "debug.h"

/* Already declared in hal.h:
 *   system_clock_speed, system_mac, uptime_sec, uptime_ms,
 *   reset_cause, net_up
 */

#endif
