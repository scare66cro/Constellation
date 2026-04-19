/* driverlib/rom_map.h — TivaWare ROM mapping shim — all calls go direct */
#ifndef DRIVERLIB_ROM_MAP_H_SHIM
#define DRIVERLIB_ROM_MAP_H_SHIM

/* MAP_xxx macros redirect to the non-MAP version (which we've already shimmed) */
#define MAP_GPIOPinWrite            GPIOPinWrite
#define MAP_GPIOPinRead             GPIOPinRead
#define MAP_GPIODirModeSet          GPIODirModeSet
#define MAP_GPIOPadConfigSet        GPIOPadConfigSet
#define MAP_GPIOPinTypeGPIOInput    GPIOPinTypeGPIOInput
#define MAP_GPIOPinTypeGPIOOutput   GPIOPinTypeGPIOOutput
#define MAP_GPIOPinTypeUART         GPIOPinTypeUART
#define MAP_GPIOPinTypeSSI          GPIOPinTypeSSI
#define MAP_GPIOPinConfigure        GPIOPinConfigure
#define MAP_SysCtlPeripheralEnable  SysCtlPeripheralEnable
#define MAP_SysCtlPeripheralReady   SysCtlPeripheralReady
#define MAP_SysCtlClockGet          SysCtlClockGet
#define MAP_SysCtlDelay             SysCtlDelay
#define MAP_UARTCharPut             UARTCharPut
#define MAP_UARTCharGet             UARTCharGet
#define MAP_UARTCharsAvail          UARTCharsAvail
#define MAP_IntEnable               IntEnable
#define MAP_IntDisable              IntDisable

/* ROM_ prefix — same thing */
#define ROM_SysCtlPeripheralEnable  SysCtlPeripheralEnable
#define ROM_GPIOPinConfigure        GPIOPinConfigure
#define ROM_GPIOPinTypeUART         GPIOPinTypeUART
#define ROM_GPIOPinTypeSSI          GPIOPinTypeSSI
#define ROM_GPIODirModeSet          GPIODirModeSet
#define ROM_GPIOPinRead             GPIOPinRead
#define ROM_GPIOPinWrite            GPIOPinWrite
#define ROM_GPIOPadConfigSet        GPIOPadConfigSet

#endif
