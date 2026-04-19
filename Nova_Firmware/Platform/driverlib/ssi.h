/* driverlib/ssi.h — TivaWare SSI (SPI) shim for AM2434 */
#ifndef DRIVERLIB_SSI_H_SHIM
#define DRIVERLIB_SSI_H_SHIM

#include <stdint.h>
#include "hal.h"

/* SSI bases — Application references SSI0_BASE for SD card */
#ifndef SSI0_BASE
#define SSI0_BASE   0
#define SSI1_BASE   1
#define SSI2_BASE   2
#define SSI3_BASE   3
#endif

/* Protocol modes */
#define SSI_FRF_MOTO_MODE_0     0x00000000
#define SSI_FRF_MOTO_MODE_1     0x00000002
#define SSI_FRF_MOTO_MODE_2     0x00000001
#define SSI_FRF_MOTO_MODE_3     0x00000003
#define SSI_MODE_MASTER         0x00000000
#define SSI_MODE_SLAVE          0x00000004

/* Functions map to HAL SPI */
static inline void SSIConfigSetExpClk(uint32_t b, uint32_t clk, uint32_t proto,
                                      uint32_t mode, uint32_t rate, uint32_t bits) {
    (void)b; (void)clk; (void)proto; (void)mode; (void)bits;
    hal_spi_init((hal_spi_port_t)b, rate);
}

static inline void SSIEnable(uint32_t b) { (void)b; }
static inline void SSIDisable(uint32_t b) { (void)b; }

static inline void SSIDataPut(uint32_t b, uint32_t data) {
    hal_spi_transfer((hal_spi_port_t)b, (uint8_t)data);
}

static inline int32_t SSIDataPutNonBlocking(uint32_t b, uint32_t data) {
    hal_spi_transfer((hal_spi_port_t)b, (uint8_t)data);
    return 1;
}

static inline void SSIDataGet(uint32_t b, uint32_t *data) {
    *data = hal_spi_transfer((hal_spi_port_t)b, 0xFF);
}

static inline int32_t SSIDataGetNonBlocking(uint32_t b, uint32_t *data) {
    *data = hal_spi_transfer((hal_spi_port_t)b, 0xFF);
    return 1;
}

static inline int SSIBusy(uint32_t b) { (void)b; return 0; }

#endif
