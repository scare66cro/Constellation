import { useEffect, useState } from "react"
import { defineMessage, useIntl } from "react-intl"
import { useDispatch, useSelector } from "react-redux"
import { postAgristar2Action } from "../actions"
import { changeTempType, tempOptions, useMeasurementSystem } from "../../../utilities/appUnitsOfMeasurements";
import { 
    selectSaving,
    extractAgristar2OutsideAirSettingsFromIoTClient,
    extractPermissionsFromIoTClient,
    selectSelectedIoTClient,
    extractAgristar2PayloadFromIoTClient,
    extractAgristar2PlenumSettingsFromIoTClient,
    extractAgristarEquipmentControlFromIoTClient,
    extractOnionModeFromAgristar2Payload,
} from "../selectors"

const useAS2FormP1OutsideAir = () => {
    // ----------------HOOKS---------------
    const dispatch = useDispatch()
    const intl = useIntl()
    const preferredSystem = useMeasurementSystem()

    // ----------------SELECTORS-----------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state))
    const saving = useSelector((state)=>selectSaving(state));
    const outside_air_settings = extractAgristar2OutsideAirSettingsFromIoTClient(iotClient)
    const as2_plenum_settings = extractAgristar2PlenumSettingsFromIoTClient(iotClient)
    const obj_permissions = extractPermissionsFromIoTClient(iotClient)
    const payload = extractAgristar2PayloadFromIoTClient(iotClient);
    const equipStatus = extractAgristarEquipmentControlFromIoTClient(iotClient);
    const onionMode = extractOnionModeFromAgristar2Payload(payload);
    const cureMode =  onionMode && equipStatus.cure.outputStatus === 'on' && !equipStatus.cure.remoteOff;

    // ----------------AUTHORIZATION--------
    const isAuthorized = obj_permissions?.['agristar2_action_level1']

    // -------------TEMPERATURE CONVERTERS--------------
    const changeTempToAs2Pref = (temp) => changeTempType(temp, as2_plenum_settings.temp_type, tempOptions.celsius)
    const changeTempToUserPref = (temp) => changeTempType(temp, as2_plenum_settings.temp_type, preferredSystem, 1)
    
    // ----------------UTILITIES------------
    // const changeScaleToAs2Pref = (val) => changeTempScale(
    //     val, // degrees to be rescaled
    //     usersPreferredSystem, // scale the user prefers
    //     outside_air_settings.tempType // scale the Agristar requires
    // )

    // -------------INITIALIZE FORM AND PAYLOAD DATA---------
    const [payloadToSend, setPayloadToSend] = useState({            
        tag: "p1OutsideAir",
        ctrlMode: outside_air_settings.ctrlMode,
        OutsideAirSet: outside_air_settings.OutsideAirSet,
        selAboveBelow: outside_air_settings.selAboveBelow,
        selTempRef: outside_air_settings.selTempRef,
        CureStartTemp: changeTempToAs2Pref(outside_air_settings.CureStartTemp),
        selHumidRef: outside_air_settings.selHumidRef,
        CureStartHumid: outside_air_settings.CureStartHumid,
        CureHumidHighLimit: outside_air_settings.CureHumidHighLimit,
    }) // PAYLOAD FOR AS2

    const [displayData, setDisplayData] = useState({            
        ctrlMode: outside_air_settings.ctrlMode,
        OutsideAirSet: outside_air_settings.OutsideAirSet,
        selAboveBelow: outside_air_settings.selAboveBelow,
        selTempRef: outside_air_settings.selTempRef,
        CureStartTemp: changeTempToUserPref(outside_air_settings.CureStartTemp),
        selHumidRef: outside_air_settings.selHumidRef,
        CureStartHumid: outside_air_settings.CureStartHumid,
        CureHumidHighLimit: outside_air_settings.CureHumidHighLimit,
        CureTempLowLimit: outside_air_settings.CureTempLowLimit,
    }) // VISUAL FOR USER

    // -----------UPDATE IF SETTINGS CHANGED----------------
    useEffect(()=>{
        setPayloadToSend({
            tag: "p1OutsideAir",
            ctrlMode: outside_air_settings.ctrlMode,
            OutsideAirSet: outside_air_settings.OutsideAirSet,
            selAboveBelow: outside_air_settings.selAboveBelow,
            selTempRef: outside_air_settings.selTempRef,
            CureStartTemp: changeTempToAs2Pref(outside_air_settings.CureStartTemp),
            selHumidRef: outside_air_settings.selHumidRef,
            CureStartHumid: outside_air_settings.CureStartHumid,
            CureHumidHighLimit: outside_air_settings.CureHumidHighLimit,
        })
        setDisplayData({
            ctrlMode: outside_air_settings.ctrlMode,
            OutsideAirSet: parseFloat(outside_air_settings.OutsideAirSet).toFixed(1),
            selAboveBelow: outside_air_settings.selAboveBelow,
            selTempRef: outside_air_settings.selTempRef,
            CureStartTemp: changeTempToUserPref(outside_air_settings.CureStartTemp),
            selHumidRef: outside_air_settings.selHumidRef,
            CureStartHumid: outside_air_settings.CureStartHumid,
            CureHumidHighLimit: outside_air_settings.CureHumidHighLimit,
            CureTempLowLimit: outside_air_settings.CureTempLowLimit,
        })
    // eslint-disable-next-line react-hooks/exhaustive-deps
    },[JSON.stringify(outside_air_settings)])

    // -----------VALUE LABEL MAP-----------------------------
    const mapValueToLabel = {
        selTempRef1:{
            255: intl.formatMessage(defineMessage({
                id:'getp1OutsideAirDataTranslatedText[8].plenumSet',
                defaultMessage:'Plenum Setpoint',
            })),
        },
        selTempRef2:{
            ...outside_air_settings.tempsRef,
            254: intl.formatMessage(defineMessage({
                id:'getp1OutsideAirDataTranslatedText[9].returnAir',
                defaultMessage:'Return Air Temperature',
            })),
        },
        ctrlMode: {
            '0': intl.formatMessage(defineMessage({
                id:'getp1OutsideAirDataTranslatedText[12].outsideair',
                defaultMessage:'Outside Air',
            })),
            '1': intl.formatMessage(defineMessage({
                id:'getp1OutsideAirDataTranslatedText[13].plenum',
                defaultMessage:'Plenum',
            }))
        },
        selAboveBelow: {
            "0":intl.formatMessage(defineMessage({
                id:'getp1OutsideAirDataTranslatedText[6].above',
                defaultMessage:'Above',
            })),
            "1": intl.formatMessage(defineMessage({
                id:'getp1OutsideAirDataTranslatedText[7].below',
                defaultMessage:'Below',
            })),
        },
        selHumidRef: {
            "0": intl.formatMessage(defineMessage({
                id: 'p1OutsideAirDynTranslatedText[3].plenum',
                defaultMessage: 'Plenum Humidity',
            })),
            "1": intl.formatMessage(defineMessage({
                id: 'p1OutsideAirDynTranslatedText[4].calculated',
                defaultMessage: 'Calculated Humidity',
            })),
        },
    }

    // ---------------FORM LOGIC------------------------------
    // When in ctrlMode 1 you can only use 'Plenum Setpoint' for selTempRef
    const optionsSelTempRef = (
        displayData.ctrlMode === '1' ? 
            mapValueToLabel.selTempRef1 
            : 
            {...mapValueToLabel.selTempRef1, ...mapValueToLabel.selTempRef2}
    )

    // ---------------CHANGE HANDLERS-------------------------
    const handlePayload = (key, val) => {
        if (key === 'ctrlMode') {
            const newVal = {}
            if (val === '1') {
                newVal.selTempRef = '255';
            }
            setPayloadToSend({...payloadToSend, ...newVal, ctrlMode:val})
            setDisplayData({...displayData, ...newVal, ctrlMode:val})
        } else if (key === 'CureStartTemp') {
            setPayloadToSend({...payloadToSend, [key]:changeTempToAs2Pref(val)})
            setDisplayData({ ...displayData, [key]:changeTempToUserPref(val)})
        } else {
            setPayloadToSend({
                ...payloadToSend,
                [key]: val,
            });
            setDisplayData({
                ...displayData,
                [key]: val,
            });
        }
    }
    // --------------SUBMIT-------------------------
    const submit = () => {
        dispatch(postAgristar2Action(iotClient, payloadToSend))
    }

    // --------------ERRORS-------------------------
    const [errors, setErrors] = useState({ });
    useEffect(() => {
        setErrors({
            ctrlMode: saving.p1OutsideAir?.errors?.ctrlMode,
            OutsideAirSet: saving.p1OutsideAir?.errors?.OutsideAirSet,
            selAboveBelow: saving.p1OutsideAir?.errors?.selAboveBelow,
            selTempRef: saving.p1OutsideAir?.errors?.selTempRef,
            CureStartTemp: saving.p1OutsideAir?.errors?.CureStartTemp,
            selHumidRef: saving.p1OutsideAir?.errors?.selHumidRef,
            CureStartHumid: saving.p1OutsideAir?.errors?.CureStartHumid,
            CureHumidHighLimit: saving.p1OutsideAir?.errors?.CureHumidHighLimit,
        })
    }, [saving]);

    // --------------ACTION STATUS------------------


    return{
        isAuthorized,

        mapValueToLabel,
        optionsSelTempRef,

        displayData,
        handlePayload,
        saving,
        errors,
        submit,
        cureMode,
    }
}
export default useAS2FormP1OutsideAir