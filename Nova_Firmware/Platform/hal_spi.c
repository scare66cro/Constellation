/*
 * hal_spi.c — SPI HAL for AM2434 Cortex-R5F
 *
 * Provides SPI operations used by the Application layer for:
 *   - External NOR flash (settings/firmware — now QSPI on AM2434)
 *   - Bit-banged shift registers (CPLD I/O on AS2 — will be DRV8908 SPI on Nova)
 *
 * For now, these are stubs that compile. Real implementation depends on
 * whether Nova keeps the CPLD shift registers (unlikely) or moves to
 * DRV8908 SPI relay drivers.
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#include "hal.h"

/* FreeRTOS includes for SPI mutex */
#include "FreeRTOS.h"
#include "semphr.h"

static xSemaphoreHandle spi_semaphore = NULL;

void CreateSPILocks(void)
{
    spi_semaphore = xSemaphoreCreateBinary();
    if (spi_semaphore != NULL) {
        xSemaphoreGive(spi_semaphore);
    }
}

int spi_lock(unsigned char block)
{
    if (spi_semaphore == NULL) return 0;
    return (xSemaphoreTake(spi_semaphore,
                           block ? portMAX_DELAY : 0) == pdTRUE) ? 1 : 0;
}

void spi_unlock(void)
{
    if (spi_semaphore != NULL) {
        xSemaphoreGive(spi_semaphore);
    }
}

void hal_spi_init(hal_spi_port_t port, uint32_t speed)
{
    /* TODO: Initialize AM243x McSPI or OSPI controller */
    (void)port;
    (void)speed;
}

uint8_t hal_spi_transfer(hal_spi_port_t port, uint8_t tx)
{
    /* TODO: Implement McSPI byte transfer */
    (void)port;
    (void)tx;
    return 0xFF;
}

void hal_spi_cs_assert(hal_spi_port_t port)
{
    (void)port;
}

void hal_spi_cs_deassert(hal_spi_port_t port)
{
    (void)port;
}
