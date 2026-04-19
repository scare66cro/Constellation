// GENERIC AS2 HOOKS

import { useEffect, useState } from "react";
import { useDispatch, useSelector } from "react-redux";
import { postAgristar2Action } from "../actions";
import { changeTempType, tempOptions, useMeasurementSystem } from "../../../utilities/appUnitsOfMeasurements";
import {
    extractAgristar2TempDevFromIoTClient,
    extractPermissionsFromIoTClient,
    selectSelectedIoTClient,
    selectSaving,
    extractAgristar2PayloadFromIoTClient,
    extractAgristar2PlenumSettingsFromIoTClient,
    extractAgristarEquipmentControlFromIoTClient,
    extractOnionModeFromAgristar2Payload,
} from "../selectors";


// FORM HOOKS

// PLENUM SETTINGS FORM TEMP & HUMID
export function useAS2FormP1PlenumTempDev (form_data) {
    // This hook is for handling the p1Plenum form submission.
    // ...form_data is the fields + values that have been passed in by user input

    // -------------HOOKS-------------------------------
    const preferredSystem = useMeasurementSystem()

    // -------------SELECTORS---------------------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state))
    const saving = useSelector((state) => selectSaving(state))
    const as2_tempdev_settings = extractAgristar2TempDevFromIoTClient(iotClient)
    const as2_plenum_settings = extractAgristar2PlenumSettingsFromIoTClient(iotClient)
    const obj_permissions = extractPermissionsFromIoTClient(iotClient)
    const payload = extractAgristar2PayloadFromIoTClient(iotClient);
    const equipStatus = extractAgristarEquipmentControlFromIoTClient(iotClient);
    const onionMode = extractOnionModeFromAgristar2Payload(payload);
    const cureMode =  onionMode && equipStatus.cure.outputStatus === 'on' && !equipStatus.cure.remoteOff;

    // -------------AUTHORIZATION-----------------------
    const isAuthorized = obj_permissions?.['agristar2_action_level1']

    // -------------TEMPERATURE CONVERTERS--------------
    const changeTempToAs2Pref = (temp) => changeTempType(temp, as2_plenum_settings.temp_type, tempOptions.celsius)
    const changeTempToUserPref = (temp) => changeTempType(temp, as2_plenum_settings.temp_type, preferredSystem, 1)


    // -------------INITIALIZING FORM AND AS2 PAYLOAD DATA------
    // aliased variables
    let below_temp = as2_tempdev_settings?.below_temp;
    let above_temp = as2_tempdev_settings?.above_temp;
    let below_time = as2_tempdev_settings?.below_time;
    let above_time = as2_tempdev_settings?.above_time;
    let cure_temp_low = as2_tempdev_settings?.cure_temp_low;
    let cure_temp_high = as2_tempdev_settings?.cure_temp_high;

    // PAYLOAD TO SUBMIT TO CLOUD
    const [payloadToSend, setPayloadToSend] = useState({
        tag: "p1PlenTempDev",
        AlarmTempLow: below_temp,
        AlarmMinLow: below_time,
        AlarmTempHigh: above_temp,
        AlarmMinHigh: above_time,
        CureTempLowLimit: changeTempToAs2Pref(cure_temp_low),
        CureTempHighLimit: changeTempToAs2Pref(cure_temp_high),
    });

    // DISPLAY DATA - Form data to be displayed for user
    const [displayData, setDisplayData] = useState({
        AlarmTempLow: below_temp,
        AlarmMinLow: below_time,
        AlarmTempHigh: above_temp,
        AlarmMinHigh: above_time,
        CureTempLowLimit: changeTempToUserPref(cure_temp_low),
        CureTempHighLimit: changeTempToUserPref(cure_temp_high),
    });

    // -----------UPDATE IF SETTINGS CHANGED----------------
    useEffect(()=>{
        setPayloadToSend({        
            tag: "p1PlenTempDev",
            AlarmTempLow: below_temp,
            AlarmMinLow: below_time,
            AlarmTempHigh: above_temp,
            AlarmMinHigh: above_time,
            CureTempLowLimit: changeTempToAs2Pref(cure_temp_low),
            CureTempHighLimit: changeTempToAs2Pref(cure_temp_high),
            })

        setDisplayData({
            AlarmTempLow: below_temp,
            AlarmMinLow: below_time,
            AlarmTempHigh: above_temp,
            AlarmMinHigh: above_time,
            CureTempLowLimit: changeTempToUserPref(cure_temp_low),
            CureTempHighLimit: changeTempToUserPref(cure_temp_high),
        })
    // eslint-disable-next-line react-hooks/exhaustive-deps
    },[below_temp, below_time, above_temp, above_time, cure_temp_low, cure_temp_high])

    // --------------------HANDLE FIELD CHANGES-------------------
    const handlePayload = (key, val) => {
        if (key === 'CureTempLowLimit' || key === 'CureTempHighLimit') {
            setPayloadToSend({...payloadToSend, [key]: changeTempToAs2Pref(val)});
            setDisplayData({...displayData, [key]: changeTempToUserPref(val)});
        } else {
            setPayloadToSend({ ...payloadToSend, [key]: val});
            setDisplayData({...displayData, [key]: val});
        }
    }

    // ---------------------------ERRORS--------------------------
    const [errors, setErrors] = useState({})
    useEffect(() => {
        setErrors({
            AlarmTempLow: saving.p1PlenTempDev?.errors?.AlarmTempLow,
            AlarmMinLow: saving.p1PlenTempDev?.errors?.AlarmMinLow,
            AlarmTempHigh: saving.p1PlenTempDev?.errors?.AlarmTempHigh,
            AlarmMinHigh: saving.p1PlenTempDev?.errors?.AlarmMinHigh,
            CureTempLowLimit: saving.p1PlenTempDev?.errors?.CureTempLowLimit,
            CureTempHighLimit: saving.p1PlenTempDev?.errors?.CureTempHighLimit,
        })
    }, [saving])

    // -------------------------SUBMIT-----------------------
    const dispatch = useDispatch()
    const submit = () => {
        dispatch(postAgristar2Action(iotClient, payloadToSend))
    }

    // ------------------------ACTION STATUS-----------------

    return {displayData, isAuthorized, handlePayload, errors, saving, submit, cureMode}
}
export default useAS2FormP1PlenumTempDev