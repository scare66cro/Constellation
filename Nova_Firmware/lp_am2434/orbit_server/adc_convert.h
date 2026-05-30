/*
 * adc_convert.h — Sensor-board ADC → engineering register conversions
 *
 * 1:1 port of orbit-simulator/src/adcConversion.ts, which itself is a
 * direct port of legacy_AS2_reference Mini_IO/Application/Analog_Input.c
 * `ConvertToTemp / ConvertToHumid / ConvertToCO2`. Treat that file as
 * the single source of truth for the NTC LUT and bridge formula — when
 * Mini_IO changes, propagate to both the TS sim and this file together.
 *
 * Wire format produced by `adc_to_orbit_register()` matches the Modbus
 * HR 200..263 layout the controller LP firmware reads (see
 * nova_thread_overrides.c::ReadAnalogBoards() in the legacy port; on
 * Constellation LP this happens here):
 *   - TEMP / IR_TEMP : int16 °C × 10
 *   - HUMID          : int16 %RH × 10
 *   - CO2            : int16 raw ppm
 *   - failure / NONE : 0x7FFF (ORBIT_SENSOR_VAL_UNDEF)
 *
 * Pure functions, no globals — safe to call from any context.
 */
#ifndef ADC_CONVERT_H
#define ADC_CONVERT_H

#include <stdint.h>
#include "orbit_state.h"   /* ORBIT_SENSOR_VAL_UNDEF */

/* Sensor type codes (mirror the sim & the sensor-board firmware
 * `packedTypes` field). */
#define ADC_SENSOR_TYPE_IR_TEMP  0
#define ADC_SENSOR_TYPE_HUMID    1
#define ADC_SENSOR_TYPE_CO2      2
#define ADC_SENSOR_TYPE_TEMP     3
#define ADC_SENSOR_TYPE_NONE     0x0F

/* Forward conversions — return engineering unit, or NaN-equivalent
 * (negative INT32_MAX) on out-of-range. Use the `adc_to_orbit_register`
 * wrapper if you only care about the wire-formatted output. */
float adc_to_temp(uint16_t adc_x16);   /* °C  */
float adc_to_humid(uint16_t adc_x16);  /* %RH */
float adc_to_co2(uint16_t adc_x16);    /* ppm */

/* Single entry point used by orbit_sensor_rtu when it stuffs HR 200+.
 * Returns ORBIT_SENSOR_VAL_UNDEF on bad input or unknown sensor type. */
int16_t adc_to_orbit_register(uint16_t adc_x16, uint8_t sensor_type);

#endif /* ADC_CONVERT_H */
