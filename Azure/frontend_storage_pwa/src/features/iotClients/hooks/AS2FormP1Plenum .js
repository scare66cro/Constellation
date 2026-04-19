// GENERIC AS2 HOOKS

import { useEffect, useState } from "react";
import { useDispatch, useSelector } from "react-redux";
import { defineMessage, useIntl } from "react-intl"
import { changeTempType, tempOptions, useMeasurementSystem } from "../../../utilities/appUnitsOfMeasurements";
import { postAgristar2Action } from "../actions";
import {
    extractAgristar2PayloadFromIoTClient,
    extractAgristar2PlenumSettingsFromIoTClient,
    extractAgristar2TempDevFromIoTClient,
    extractPermissionsFromIoTClient,
    selectSelectedIoTClient,
    selectSaving,
    extractBeeModeFromAgristar2Payload,
    extractOnionModeFromAgristar2Payload,
} from "../selectors";

// FORM HOOKS

// PLENUM SETTINGS FORM TEMP & HUMID
export function useAS2FormP1Plenum (form_data) {
    // This hook is for handling the p1Plenum form submission.
    // ...form_data is the fields + values that have been passed in by user input

    const intl = useIntl()

    // -------------HOOKS-------------------------------
    const preferredSystem = useMeasurementSystem()
    
    // -------------SELECTORS---------------------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state))
    const saving = useSelector((state)=>selectSaving(state));
    const as2_plenum_settings = extractAgristar2PlenumSettingsFromIoTClient(iotClient)
    const as2_tempdev_settings = extractAgristar2TempDevFromIoTClient(iotClient)
    const obj_permissions = extractPermissionsFromIoTClient(iotClient)
    const payload = extractAgristar2PayloadFromIoTClient(iotClient)
    const cureSwitchOn = payload?.['EquipStatusData']?.[8] === '1';
    const notRemoteOff = payload?.['EquipStatusData']?.[52] === '0';
    const burnerMode = payload?.['P2BurnerData']?.[6];
    const beeMode = extractBeeModeFromAgristar2Payload(payload);
    const onionMode = extractOnionModeFromAgristar2Payload(payload);

    // -------------AUTHORIZATION-----------------------
    const isAuthorized = obj_permissions?.['agristar2_action_level1']

    // -------------TEMPERATURE CONVERTERS--------------
    const changeTempToAs2Pref = (temp) => changeTempType(temp, as2_plenum_settings.temp_type, tempOptions.celsius)
    const changeTempToUserPref = (temp) => changeTempType(temp, as2_plenum_settings.temp_type, preferredSystem, 1)

    // -------------INITIALIZING FORM AND AS2 PAYLOAD DATA------
    const mapValueToLabel = {
        AuxSwitchForDehumidifier: {
            "0": intl.formatMessage(defineMessage({
                    id:'getp1Plenum[0].none',
                    defaultMessage:'None',
                })),
            "56": intl.formatMessage(defineMessage({
                    id:'getp1Plenum[1].aux1',
                    defaultMessage:'Auxiliary Switch 1',
                })),
            "58": intl.formatMessage(defineMessage({
                id:'getp1Plenum[2].aux2',
                defaultMessage:'Auxiliary Switch 2',
            })),
        },
        PlenumHumidRef: {
            '0': intl.formatMessage(defineMessage({
                id: 'p1PlenumDynTranslatedText[7].plenum',
                defaultMessage: 'Plenum',
            })),
            '1': intl.formatMessage(defineMessage({
                id: 'p1PlenumDynTranslatedText[8].return',
                defaultMessage: 'Return Air',
            })),
        },
    }

    let below_temp = as2_tempdev_settings?.below_temp;
    let above_temp = as2_tempdev_settings?.above_temp;
    let below_time = as2_tempdev_settings?.below_time;
    let above_time = as2_tempdev_settings?.above_time;
    let cure_temp_low = as2_tempdev_settings?.cure_temp_low;
    let cure_temp_high = as2_tempdev_settings?.cure_temp_high;

    // PAYLOAD TO SUBMIT TO CLOUD
    const [payloadToSend, setPayloadToSend] = useState({
        tag: "p1Plenum",
        PlenumTempSet: changeTempToAs2Pref(as2_plenum_settings?.temp_set),
        PlenumHumidSet: as2_plenum_settings?.humid_set,
        PlenumHumidRef: as2_plenum_settings?.humid_ref,
        PlenumTempSet2: changeTempToAs2Pref(as2_plenum_settings?.temp_set2),
        UseRefrigerationForDehumidifier: as2_plenum_settings?.use_refrig,
        AuxSwitchForDehumidifier: as2_plenum_settings?.aux_switch,
        BurnerTempSet: changeTempToAs2Pref(as2_plenum_settings?.burner_set),
        BurnerThreshold: as2_plenum_settings?.burner_thresh,
        BurnerManualMax: changeTempToAs2Pref(as2_plenum_settings?.burner_max),
        BurnerManualRestart: changeTempToAs2Pref(as2_plenum_settings?.burner_restart),
        AlarmTempLow: below_temp,
        AlarmMinLow: below_time,
        AlarmTempHigh: above_temp,
        AlarmMinHigh: above_time,
        CureTempLowLimit: changeTempToAs2Pref(cure_temp_low),
        CureTempHighLimit: changeTempToAs2Pref(cure_temp_high),
    })

    // DISPLAY DATA - Form data to be displayed for user
    const [displayData, setDisplayData] = useState({
        PlenumTempSet:changeTempToUserPref(as2_plenum_settings?.temp_set),
        PlenumHumidSet:as2_plenum_settings?.humid_set,
        PlenumHumidRef: as2_plenum_settings?.humid_ref,
        PlenumTempSet2: changeTempToUserPref(as2_plenum_settings?.temp_set2),
        UseRefrigerationForDehumidifier: as2_plenum_settings?.use_refrig,
        AuxSwitchForDehumidifier: as2_plenum_settings?.aux_switch,
        BurnerTempSet: changeTempToUserPref(as2_plenum_settings?.burner_set),
        BurnerThreshold: as2_plenum_settings?.burner_thresh,
        BurnerManualMax: changeTempToUserPref(as2_plenum_settings?.burner_max),
        BurnerManualRestart: changeTempToUserPref(as2_plenum_settings?.burner_restart),
    })

    // -----------UPDATE IF SETTINGS CHANGED----------------
    useEffect(()=>{
        setPayloadToSend({
            tag: "p1Plenum",
            PlenumTempSet: changeTempToAs2Pref(as2_plenum_settings?.temp_set),
            PlenumHumidSet: as2_plenum_settings?.humid_set,
            PlenumHumidRef: as2_plenum_settings?.humid_ref,
            PlenumTempSet2: changeTempToAs2Pref(as2_plenum_settings?.temp_set2),
            UseRefrigerationForDehumidifier: as2_plenum_settings?.use_refrig,
            AuxSwitchForDehumidifier: as2_plenum_settings?.aux_switch,
            BurnerTempSet: changeTempToAs2Pref(as2_plenum_settings?.burner_set),
            BurnerThreshold: as2_plenum_settings?.burner_thresh,
            BurnerManualMax: changeTempToAs2Pref(as2_plenum_settings?.burner_max),
            BurnerManualRestart: changeTempToAs2Pref(as2_plenum_settings?.burner_restart),
            AlarmTempLow: below_temp,
            AlarmMinLow: below_time,
            AlarmTempHigh: above_temp,
            AlarmMinHigh: above_time,
            CureTempLowLimit: changeTempToAs2Pref(cure_temp_low),
            CureTempHighLimit: changeTempToAs2Pref(cure_temp_high),
        })

        setDisplayData({
            PlenumTempSet: changeTempToUserPref(as2_plenum_settings?.temp_set),
            PlenumHumidSet: as2_plenum_settings?.humid_set,
            PlenumHumidRef: as2_plenum_settings?.humid_ref,
            PlenumTempSet2: changeTempToUserPref(as2_plenum_settings?.temp_set2),
            UseRefrigerationForDehumidifier: as2_plenum_settings?.use_refrig,
            AuxSwitchForDehumidifier: as2_plenum_settings?.aux_switch,
            BurnerTempSet: changeTempToUserPref(as2_plenum_settings?.burner_set),
            BurnerThreshold: as2_plenum_settings?.burner_thresh,
            BurnerManualMax: changeTempToUserPref(as2_plenum_settings?.burner_max),
            BurnerManualRestart: changeTempToUserPref(as2_plenum_settings?.burner_restart),
        })
    // eslint-disable-next-line react-hooks/exhaustive-deps
    },[JSON.stringify(as2_plenum_settings)])

    // --------------------HANDLE FIELD CHANGES-------------------
    const handlePayloadToSend = (key, val) => {
        if (key === 'PlenumTempSet' || key === 'PlenumTempSet2' || key === 'BurnerTempSet'
            || key === "BurnerManualMax" || key === "BurnerManualRestart") {
            setPayloadToSend({...payloadToSend, [key]:changeTempToAs2Pref(val)})
            setDisplayData({...displayData, [key]:val})
        } else if (key === 'UseRefrigerationForDehumidifier') {
            setPayloadToSend({...payloadToSend, [key]: val ? '1': '0'})
            setDisplayData({...displayData, [key]: val ? '1' : '0'})
        } else {
            setPayloadToSend({...payloadToSend, [key]: val});
            setDisplayData({...displayData, [key]:val})
        }
    }

    // ---------------------------ERRORS--------------------------
    const [errors, setErrors] = useState({ })
    useEffect(()=>{
        setErrors({
            PlenumTempSet: saving.p1Plenum?.errors?.PlenumTempSet,
            PlenumHumidSet: saving.p1Plenum?.errors?.PlenumHumidSet,
            PlenumHumidRef: saving.p1Plenum?.errors?.PlenumHumidRef,
            PlenumTempSet2: saving.p1Plenum?.errors?.PlenumTempSet2,
            UseRefrigerationForDehumidifier: saving.p1Plenum?.errors?.UseRefrigerationForDehumidifier,
            AuxSwitchForDehumidifier: saving.p1Plenum?.errors?.AuxSwitchForDehumidifier,
            BurnerTempSet: saving.p1Plenum?.errors?.BurnerTempSet,
            BurnerThreshold: saving.p1Plenum?.errors?.BurnerThreshold,
            BurnerManualMax: saving.p1Plenum?.errors?.BurnerManualMax,
            BurnerManualRestart: saving.p1Plenum?.errors?.BurnerManualRestart,
        })
    },[saving])

    // -------------------------SUBMIT-----------------------
    const dispatch = useDispatch()
    const submit = () => {
        dispatch(postAgristar2Action(iotClient, payloadToSend))
    }

    // ------------------------ACTION STATUS-----------------

    return {
        displayData, isAuthorized, handlePayloadToSend, errors,
        submit, saving, beeMode, mapValueToLabel, onionMode,
        cureSwitchOn, notRemoteOff, burnerMode
    }
}
export default useAS2FormP1Plenum