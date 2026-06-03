export enum SYSTEM_MODE {
    POTATO_MODE = "0",
    ONION_MODE = "1",
    BEE_MODE = "2",
    PECAN_MODE = "3",
};

export const SWITCH_OFF = '0';
export const SWITCH_AUTO = '1';
export const SWITCH_MANUAL = '2';

// DI proving convention — flipped 2026-06-03 from the legacy AS2
// normally-closed semantics ('0' = closed = good) to the modern
// active-high convention. On Constellation, an unmapped DI emits
// `input_on = false` from firmware (proto3-suppressed) and renders
// as '0' here — which now correctly means "no proving signal" and
// suppresses the home-page running graphic until the operator
// either maps a proving DI that reads HIGH or disables proving via
// Level 2 Failures 1 ClimacellMode = None. The legacy '0=good'
// inversion was a hidden gotcha for anyone reading the home page
// with no AS2 background.
export const INPUT_GOOD = '1';
export const INPUT_BAD = '0';

export const OUTPUT_ON = '1';
export const OUTPUT_OFF = '0';

export const EQUIP_NOT_DEFINED = '-1';
