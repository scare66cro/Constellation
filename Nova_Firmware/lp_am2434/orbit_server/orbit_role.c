/*
 * orbit_role.c — OrbitRole accessor backed by lp_device_config.
 */

#include "orbit_role.h"
#include "../lp_device_config.h"

OrbitRole OrbitRole_Get(void)
{
    const LpDeviceConfig *c = LpDeviceConfig_Get();
    switch (c->role) {
        case ORBIT_ROLE_STORAGE: return ORBIT_ROLE_STORAGE;
        case ORBIT_ROLE_GDC:     return ORBIT_ROLE_GDC;
        case ORBIT_ROLE_TRITON:  return ORBIT_ROLE_TRITON;
        case ORBIT_ROLE_PULSAR:  return ORBIT_ROLE_PULSAR;
        case ORBIT_ROLE_CONTROLLER:
        default:                 return ORBIT_ROLE_CONTROLLER;
    }
}

const char *OrbitRole_Name(OrbitRole r)
{
    switch (r) {
        case ORBIT_ROLE_CONTROLLER: return "CONTROLLER";
        case ORBIT_ROLE_STORAGE:    return "STORAGE";
        case ORBIT_ROLE_GDC:        return "GDC";
        case ORBIT_ROLE_TRITON:     return "TRITON";
        case ORBIT_ROLE_PULSAR:     return "PULSAR";
        default:                    return "?";
    }
}
