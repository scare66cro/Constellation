/*
 * adc_convert.c — NTC LUT + bridge math (port of adcConversion.ts).
 *
 * NaN sentinel: the TS code uses JS `NaN`. We use a large negative
 * float constant ADC_NAN below INT16_MIN so callers can use a single
 * isnan-style check without pulling in <math.h>'s NAN macro (and so
 * the conversion is bit-exact deterministic without needing the
 * fenv quiet-NaN behaviour).
 */

#include "adc_convert.h"
#include <stddef.h>

#define ADC_NAN  (-1.0e30f)

static inline int adc_is_nan(float v) { return v <= -1.0e29f; }

/* NTC table: { degC, rt(Ω) }. Copied verbatim from adcConversion.ts. */
typedef struct { int16_t degC; float rt; } NtcRow;

static const NtcRow TEMP_TABLE[] = {
    {-40, 75777.0f}, {-39, 70918.0f}, {-38, 66401.0f}, {-37, 62200.0f},
    {-36, 58292.0f}, {-35, 54653.0f}, {-34, 51264.0f}, {-33, 48106.0f},
    {-32, 45162.0f}, {-31, 42416.0f}, {-30, 39855.0f}, {-29, 37463.0f},
    {-28, 35230.0f}, {-27, 33144.0f}, {-26, 31194.0f}, {-25, 29371.0f},
    {-24, 27665.0f}, {-23, 26069.0f}, {-22, 24574.0f}, {-21, 23174.0f},
    {-20, 21862.0f}, {-19, 20633.0f}, {-18, 19480.0f}, {-17, 18398.0f},
    {-16, 17383.0f}, {-15, 16430.0f}, {-14, 15535.0f}, {-13, 14694.0f},
    {-12, 13903.0f}, {-11, 13160.0f}, {-10, 12461.0f}, { -9, 11803.0f},
    { -8, 11183.0f}, { -7, 10600.0f}, { -6, 10051.0f}, { -5,  9533.0f},
    { -4,  9045.0f}, { -3,  8585.0f}, { -2,  8151.0f}, { -1,  7741.0f},
    {  0,  7353.0f}, {  1,  6988.0f}, {  2,  6643.0f}, {  3,  6318.0f},
    {  4,  6010.0f}, {  5,  5719.0f}, {  6,  5444.0f}, {  7,  5183.0f},
    {  8,  4937.0f}, {  9,  4703.0f}, { 10,  4482.0f}, { 11,  4273.0f},
    { 12,  4075.0f}, { 13,  3886.0f}, { 14,  3708.0f}, { 15,  3539.0f},
    { 16,  3378.0f}, { 17,  3226.0f}, { 18,  3081.0f}, { 19,  2944.0f},
    { 20,  2814.0f}, { 21,  2690.0f}, { 22,  2572.0f}, { 23,  2460.0f},
    { 24,  2353.0f}, { 25,  2252.0f}, { 26,  2156.0f}, { 27,  2064.0f},
    { 28,  1977.0f}, { 29,  1894.0f}, { 30,  1814.0f}, { 31,  1739.0f},
    { 32,  1667.0f}, { 33,  1598.0f}, { 34,  1533.0f}, { 35,  1471.0f},
    { 36,  1411.0f}, { 37,  1355.0f}, { 38,  1300.0f}, { 39,  1249.0f},
    { 40,  1199.0f}, { 41,  1152.0f}, { 42,  1107.0f}, { 43,  1064.0f},
    { 44,  1023.0f}, { 45,   983.6f}, { 46,   946.0f}, { 47,   910.0f},
    { 48,   875.6f}, { 49,   842.6f}, { 50,   811.1f}, { 51,   780.9f},
    { 52,   752.0f}, { 53,   724.3f}, { 54,   697.8f}, { 55,   672.4f},
    { 56,   648.0f}, { 57,   624.7f}, { 58,   602.3f}, { 59,   580.8f},
    { 60,   560.2f}, { 61,   540.2f}, { 62,   521.5f}, { 63,   503.3f},
    { 64,   485.8f}, { 65,   469.0f}, { 66,   452.9f}, { 67,   437.4f},
    { 68,   422.6f}, { 69,   408.3f}, { 70,   394.6f}, { 71,   381.3f},
    { 72,   368.6f}, { 73,   356.4f}, { 74,   344.7f}, { 75,   333.4f},
    { 76,   322.5f}, { 77,   312.1f}, { 78,   302.0f}, { 79,   292.3f},
    { 80,   282.9f}, { 81,   273.9f}, { 82,   265.3f}, { 83,   256.9f},
};
#define TEMP_TABLE_LEN (sizeof(TEMP_TABLE) / sizeof(TEMP_TABLE[0]))

/* Bridge constants — match Mini_IO Analog_Input.c. */
#define ADC_RG1   27400.0f
#define ADC_RG2   10000.0f
#define ADC_RTH1  10000.0f
#define ADC_RTH2   1000.0f
#define ADC_R1REF 30100.0f
#define ADC_R2REF 15000.0f
/* BRIDGE_NUMERATOR = 1024 * (1 + Rg1/Rg2) * Rth2 * (R1ref + R2ref) */
#define ADC_BRIDGE_NUMERATOR \
    (1024.0f * (1.0f + (ADC_RG1 / ADC_RG2)) * ADC_RTH2 * (ADC_R1REF + ADC_R2REF))

float adc_to_temp(uint16_t adc_x16)
{
    if (adc_x16 == 0 || adc_x16 == (uint16_t)ORBIT_SENSOR_VAL_UNDEF) return ADC_NAN;
    float adc_div16 = (float)adc_x16 / 16.0f;
    float rt = ADC_BRIDGE_NUMERATOR / (adc_div16 * ADC_R2REF) - (ADC_RTH1 + ADC_RTH2);

    if (rt > TEMP_TABLE[0].rt) return ADC_NAN;
    if (rt < TEMP_TABLE[TEMP_TABLE_LEN - 1].rt) return ADC_NAN;

    size_t i = 0;
    while (i < TEMP_TABLE_LEN - 1 && TEMP_TABLE[i].rt > rt) i++;
    if (i == 0) return (float)TEMP_TABLE[0].degC;

    float xa = TEMP_TABLE[i - 1].rt, ya = (float)TEMP_TABLE[i - 1].degC;
    float xb = TEMP_TABLE[i].rt,     yb = (float)TEMP_TABLE[i].degC;
    return ya + ((rt - xa) * (yb - ya)) / (xb - xa);
}

float adc_to_humid(uint16_t adc_x16)
{
    if (adc_x16 == 0 || adc_x16 == (uint16_t)ORBIT_SENSOR_VAL_UNDEF) return ADC_NAN;
    return (((float)adc_x16 / 16.0f) - 180.0f) / 720.0f * 100.0f;
}

float adc_to_co2(uint16_t adc_x16)
{
    if (adc_x16 == 0 || adc_x16 == (uint16_t)ORBIT_SENSOR_VAL_UNDEF) return ADC_NAN;
    float scaled = (float)adc_x16 / 16.0f;
    if (scaled > 895.0f) return ADC_NAN;
    return ((scaled - 180.0f) / 720.0f) * 10000.0f;
}

/* 1:1 port of Mini_IO 2.0.1.b Analog_Input.c:177-185 ConvertToStaticPressure.
 * 4-20 mA → 0-2.5"wc, linear. The valid mA range maps to a scaled-ADC
 * range of ~180-900 (the A/D count arrives ×16 to add resolution, hence
 * the /16). Below 4 mA (ScaledADC < 180) the reading is invalid → undef;
 * above 20 mA (> 900) it clamps to the 2.5"wc full-scale. */
float adc_to_static_pressure(uint16_t adc_x16)
{
    if (adc_x16 == 0 || adc_x16 == (uint16_t)ORBIT_SENSOR_VAL_UNDEF) return ADC_NAN;
    float scaled = (float)adc_x16 / 16.0f;
    if (scaled < 180.0f) return ADC_NAN;          /* below 4 mA → undefined */
    if (scaled > 900.0f) return 2.5f;             /* above 20 mA → clamp 2.5"wc */
    return ((scaled - 180.0f) / 720.0f) * 2.5f;
}

static int16_t round_to_int16(float v)
{
    if (v >= 0.0f) return (int16_t)(v + 0.5f);
    return (int16_t)(v - 0.5f);
}

int16_t adc_to_orbit_register(uint16_t adc_x16, uint8_t sensor_type)
{
    if (adc_x16 == 0 || adc_x16 == (uint16_t)ORBIT_SENSOR_VAL_UNDEF) {
        return ORBIT_SENSOR_VAL_UNDEF;
    }
    float eng;
    switch (sensor_type) {
        case ADC_SENSOR_TYPE_IR_TEMP:
        case ADC_SENSOR_TYPE_TEMP:
            eng = adc_to_temp(adc_x16);
            if (adc_is_nan(eng)) return ORBIT_SENSOR_VAL_UNDEF;
            return round_to_int16(eng * 10.0f);
        case ADC_SENSOR_TYPE_HUMID:
            eng = adc_to_humid(adc_x16);
            if (adc_is_nan(eng)) return ORBIT_SENSOR_VAL_UNDEF;
            return round_to_int16(eng * 10.0f);
        case ADC_SENSOR_TYPE_CO2:
            eng = adc_to_co2(adc_x16);
            if (adc_is_nan(eng)) return ORBIT_SENSOR_VAL_UNDEF;
            return round_to_int16(eng);
        case ADC_SENSOR_TYPE_STATIC_PRESS:
            /* int16 "wc × 100 (0.00-2.50"wc → 0-250). No AS2 ancestor
             * for the ×100 wire scaling — see the header doc-comment.
             * Below-range (< 4 mA) returns ADC_NAN → undef, matching
             * ConvertToStaticPressure's SENSOR_VAL_UNDEFINED path. */
            eng = adc_to_static_pressure(adc_x16);
            if (adc_is_nan(eng)) return ORBIT_SENSOR_VAL_UNDEF;
            return round_to_int16(eng * 100.0f);
        default:
            return ORBIT_SENSOR_VAL_UNDEF;
    }
}
