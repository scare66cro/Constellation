/*
 * rom_shims.c — Redirect ROM_* function calls to flash-resident driverlib.
 *
 * On real hardware, these functions live in the TI ROM at 0x01000000.
 * For QEMU builds (no ROM image), we provide thin wrappers that call
 * the regular driverlib implementations linked from flash.
 */

#include <stdint.h>
#include <stdbool.h>
#include "inc/hw_types.h"
#include "driverlib/gpio.h"
#include "driverlib/sysctl.h"
#include "driverlib/pin_map.h"

void ROM_GPIODirModeSet(uint32_t ui32Port, uint8_t ui8Pins, uint32_t ui32PinIO)
{
    GPIODirModeSet(ui32Port, ui8Pins, ui32PinIO);
}

void ROM_GPIOPinWrite(uint32_t ui32Port, uint8_t ui8Pins, uint8_t ui8Val)
{
    GPIOPinWrite(ui32Port, ui8Pins, ui8Val);
}

void ROM_GPIOPinConfigure(uint32_t ui32PinConfig)
{
    GPIOPinConfigure(ui32PinConfig);
}

void ROM_GPIOPinTypeUART(uint32_t ui32Port, uint8_t ui8Pins)
{
    GPIOPinTypeUART(ui32Port, ui8Pins);
}

void ROM_GPIOPinTypeSSI(uint32_t ui32Port, uint8_t ui8Pins)
{
    GPIOPinTypeSSI(ui32Port, ui8Pins);
}

void ROM_SysCtlPeripheralEnable(uint32_t ui32Peripheral)
{
    SysCtlPeripheralEnable(ui32Peripheral);
}
