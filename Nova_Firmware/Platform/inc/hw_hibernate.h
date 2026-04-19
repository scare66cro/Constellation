/* inc/hw_hibernate.h — TivaWare hibernate register shim */
#ifndef HW_HIBERNATE_H_SHIM
#define HW_HIBERNATE_H_SHIM

/* Register offsets — not used on AM2434, but Application code references them */
#define HIB_RTCC            0x00000000
#define HIB_RTCM0           0x00000004
#define HIB_RTCLD           0x0000000C
#define HIB_CTL             0x00000010
#define HIB_IM              0x00000014
#define HIB_RIS             0x00000018
#define HIB_MIS             0x0000001C
#define HIB_IC              0x00000020
#define HIB_RTCT            0x00000024
#define HIB_RTCSS           0x00000028
#define HIB_DATA            0x00000030

#define HIB_CC              0x00000040
#define HIB_CC_SYSCLKEN     0x00000001
#define HIB_CC_RTCCLKEN     0x00000002

#endif
