export enum SYSTEM_MODE {
    POTATO_MODE = "0",
    ONION_MODE = "1",
    BEE_MODE = "2",
    PECAN_MODE = "3",
};

export const SWITCH_OFF = '0';
export const SWITCH_AUTO = '1';
export const SWITCH_MANUAL = '2';

// Normalized input "good" flag used by the home-page running graphics
// (Climacell/Humidifier === INPUT_GOOD lights the animation).
//
// IMPORTANT: this is a *normalized* good-flag, NOT the raw DI level.
// PROVING equipment (fan, climacell, humidifier) is active-high FAULT
// per AS2 (Failures.c:443) — a healthy/proving device reads DI LOW;
// asserting the DI signals a FAILURE. `frontMatterComposite.buildPanel`
// therefore normalizes those slots (`inProveGood`: DI low → INPUT_GOOD,
// DI high → INPUT_BAD) so the panel carries "good=1" regardless of the
// raw polarity. CURRENT_SENSE inputs (bay lights) are the opposite —
// DI high = current flowing = on — and pass raw `inOn` through, so for
// lights INPUT_GOOD ('1') still lines up with DI high.
//
// History: 2026-06-03 a global flip to '1' tried to make "DI high = good"
// for proving too, but that contradicted the firmware (DI high = fault)
// and would invert the lights; replaced 2026-06-09 with per-category
// normalization in buildPanel + the AS2 fix in interpretEquipmentInput.
export const INPUT_GOOD = '1';
export const INPUT_BAD = '0';

export const OUTPUT_ON = '1';
export const OUTPUT_OFF = '0';

export const EQUIP_NOT_DEFINED = '-1';
