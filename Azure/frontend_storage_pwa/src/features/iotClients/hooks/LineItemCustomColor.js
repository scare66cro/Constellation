import { useState, useEffect } from "react";
import { extractOnionModeFromFrontMatter } from "../selectors";

const useLineItemCustomColor = (frontMatter) => {
    const potatoMode = frontMatter.system_mode === '0';
    const onionMode = extractOnionModeFromFrontMatter(frontMatter);
    const cureMode = onionMode && frontMatter.cure_output === '1' && frontMatter.cure_remote !== '1';
    const notCureMode = onionMode && frontMatter.cure_output === '0' && frontMatter.cure_remote !== '1';

    const [onionColor, setOnionColor] = useState(undefined);
    useEffect(() => {
        if (cureMode) {
            if (frontMatter.plenum_temp > frontMatter.cure_temp_low
                && frontMatter.plenum_temp < frontMatter.cure_temp_high) {
                setOnionColor('green');
            } else {
                setOnionColor('red');
            }
        } else {
            setOnionColor(undefined);
        }
    }, [cureMode, frontMatter.cure_temp_high, frontMatter.cure_temp_low, frontMatter.plenum_temp]);

    const [humidColor, setHumidColor] = useState(undefined);
    useEffect(() => {
        if (cureMode) { // not remote off
            if (frontMatter.plenum_humid < frontMatter.cure_humid_high * 1) {
                setHumidColor('green');
            } else {
                setHumidColor('red');
            }
        }
        else if ((potatoMode
            || (onionMode
                && frontMatter.cure_output === '0'      // not cure mode
                && frontMatter.cure_remote !== '1') )  // not remote off
                && frontMatter.humid_set_reference === '0') // plenum referenced
        {
            if (frontMatter.plenum_humid > frontMatter.plenum_humid_set - 4) {
                setHumidColor('green');
            } else {
                setHumidColor('red');
            }
        } else {
            setHumidColor('black');
        }
    }, [cureMode, frontMatter, onionMode, potatoMode]);

    const [outsideColor, setOutsideColor] = useState('black');
    useEffect(() => {
        if (cureMode) { // not remote off
            if (frontMatter.outside_temp >= frontMatter.cure_start_temp) {
                setOutsideColor('green');
            } else {
                setOutsideColor('red');
            }
        } else {
            if (frontMatter.outside_temp > frontMatter.cooling_available * 1) {
                setOutsideColor('red');
            } else {
                setOutsideColor('green');
            }
        }
    }, [cureMode, frontMatter]);

    const [returnHumidColor, setReturnHumidColor] = useState('black');
    useEffect(() => {
        if (notCureMode && frontMatter.humid_set_reference === '1') {
            if (frontMatter.return_humid > frontMatter.plenum_humid_set) {
                setReturnHumidColor('green');
            } else {
                setReturnHumidColor('red');
            }
        } else {
            setReturnHumidColor('black');
        }
    }, [frontMatter, notCureMode]);

    const [calcHumidColor, setCalcHumidColor] = useState('black');
    useEffect(() => {
        if (cureMode && frontMatter.air_cure_humid_reference === '1') {
            if (frontMatter.calc_humid < frontMatter.cure_start_humid * 1) {
                setCalcHumidColor('green');
            } else {
                setCalcHumidColor('red');
            }
        } else {
            setCalcHumidColor('black');
        }
    }, [cureMode, frontMatter]);

    const [co2Color, setCo2Color] = useState('black');
    useEffect(() => {
        if (frontMatter.co2_purge_mode === '2' || frontMatter.co2_purge_mode === '3') { // auto or continuous
            if (frontMatter.co2 <= frontMatter.co2_set_point) {
                setCo2Color('green');
            } else {
                setCo2Color('red');
            }
        } else {
            setCo2Color('black');
        }
    }, [frontMatter]);

    const [co2Color2, setCo2Color2] = useState('black');
    useEffect(() => {
        if (frontMatter.co2_purge_mode === '2' || frontMatter.co2_purge_mode === '3') { // auto or continuous
            if (frontMatter.co2_2 <= frontMatter.co2_set_point) {
                setCo2Color2('green');
            } else {
                setCo2Color2('red');
            }
        } else {
            setCo2Color2('black');
        }
    }, [frontMatter]);

    return {
        onionColor, humidColor, outsideColor, returnHumidColor, calcHumidColor, co2Color, co2Color2
    };
};

export default useLineItemCustomColor;