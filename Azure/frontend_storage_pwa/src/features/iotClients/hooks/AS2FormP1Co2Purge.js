import { useEffect, useState } from "react"
import { defineMessage, useIntl } from "react-intl"
import { useDispatch, useSelector } from "react-redux"
import { changeTempType, tempOptions, useMeasurementSystem } from "../../../utilities/appUnitsOfMeasurements"
import { postAgristar2Action } from "../actions"
import { extractAgristar2Co2PurgeSettingsFromIoTClient, extractAgristar2PayloadFromIoTClient, extractPermissionsFromIoTClient, extractSystemModeFromAgristar2Payload, selectSaving, selectSelectedIoTClient } from "../selectors"


const useAS2FormP1Co2Purge = () => {

    // -------------HOOKS----------------
    const preferredSystem = useMeasurementSystem()
    const dispatch = useDispatch()
    const intl = useIntl()
 
    // -------------SELECTORS------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state))
    const saving = useSelector((state)=>selectSaving(state));
    const co2_purge_settings = extractAgristar2Co2PurgeSettingsFromIoTClient(iotClient, preferredSystem)
    const obj_permissions = extractPermissionsFromIoTClient(iotClient)
    const payload = extractAgristar2PayloadFromIoTClient(iotClient);
    const beeMode = extractSystemModeFromAgristar2Payload(payload) === '2';

    // -------------AUTHORIZATION--------
    const isAuthorized = obj_permissions?.['agristar2_action_level1']

    // -------------TEMPERATURE CONVERTER------
    const changeTempToAs2Pref = (temp) => changeTempType(temp, co2_purge_settings.temp_type, tempOptions.celsius)
    const changeTempToUserPref = (temp) => changeTempType(temp, co2_purge_settings.temp_type, preferredSystem, 1)

    // -------------INITIALIZE FORM AND PAYLOAD DATA---------
    const [payloadToSend, setPayloadToSend] = useState({
        "tag":"p1Co2Purge",
        "selPurgeMode":co2_purge_settings.selPurgeMode,
        "PurgeHours":co2_purge_settings.PurgeHours,
        "co2SetPoint":co2_purge_settings.co2SetPoint,
        "co2Target": co2_purge_settings.co2Target,
        "minTemp":changeTempToAs2Pref(co2_purge_settings.minTemp),
        "maxTemp":changeTempToAs2Pref(co2_purge_settings.maxTemp),
        "time":co2_purge_settings.time,
        "fanOutput":co2_purge_settings.fanOutput,
        "doorOutput":co2_purge_settings.doorOutput
    })

    const [displayData, setDisplayData] = useState({
        "selPurgeMode":co2_purge_settings.selPurgeMode,
        "PurgeHours":co2_purge_settings.PurgeHours,
        "co2SetPoint":co2_purge_settings.co2SetPoint,
        "co2Target": co2_purge_settings.co2Target,
        "minTemp":changeTempToUserPref(co2_purge_settings.minTemp),
        "maxTemp":changeTempToUserPref(co2_purge_settings.maxTemp),
        "time":co2_purge_settings.time,
        "fanOutput":co2_purge_settings.fanOutput,
        "doorOutput":co2_purge_settings.doorOutput
    })

    // -----------UPDATE IF SETTINGS CHANGED----------------
    useEffect(()=>{
        setPayloadToSend({
            "tag":"p1Co2Purge",
            "selPurgeMode":co2_purge_settings.selPurgeMode,
            "PurgeHours":co2_purge_settings.PurgeHours,
            "co2SetPoint":co2_purge_settings.co2SetPoint,
            "co2Target":co2_purge_settings.co2Target,
            "minTemp":changeTempToAs2Pref(co2_purge_settings.minTemp),
            "maxTemp":changeTempToAs2Pref(co2_purge_settings.maxTemp),
            "time":co2_purge_settings.time,
            "fanOutput":co2_purge_settings.fanOutput,
            "doorOutput":co2_purge_settings.doorOutput
        })
        setDisplayData({
            "selPurgeMode":co2_purge_settings.selPurgeMode,
            "PurgeHours":co2_purge_settings.PurgeHours,
            "co2SetPoint":co2_purge_settings.co2SetPoint,
            "co2Target":co2_purge_settings.co2Target,
            "minTemp":changeTempToUserPref(co2_purge_settings.minTemp),
            "maxTemp":changeTempToUserPref(co2_purge_settings.maxTemp),
            "time":co2_purge_settings.time,
            "fanOutput":co2_purge_settings.fanOutput,
            "doorOutput":co2_purge_settings.doorOutput
        })
    // eslint-disable-next-line react-hooks/exhaustive-deps
    },[JSON.stringify(co2_purge_settings)])

    // -----------VALUE LABEL MAP-----------------------------
    const mapValueToLabel = {
        selPurgeMode: {
            "0": intl.formatMessage(defineMessage({
                    id:'getp1Co2PurgeDataTranslatedText[3].none',
                    defaultMessage:'None',
                })),
            "1": intl.formatMessage(defineMessage({
                    id:'getp1Co2PurgeDataTranslatedText[2].manual',
                    defaultMessage:'Manual',
                })),
            "2": intl.formatMessage(defineMessage({
                id:'getp1Co2PurgeDataTranslatedText[1].auto',
                defaultMessage:'Automatic',
                })),
        }
    }

    if (beeMode) {
        mapValueToLabel.selPurgeMode["3"] = intl.formatMessage(defineMessage({
            id:'getp1Co2PurgeDataTranslatedText[5].continuous',
            defaultMessage:'Continuous',
            }));
    }

    // ----------CHANGE HANDLERS---------------------------
    const handleSelPurgeMode = (val) => {
        setPayloadToSend({...payloadToSend, "selPurgeMode":val})
        setDisplayData({...displayData, "selPurgeMode":val})
    }

    const handlePurgeHours = (val) => {
        setPayloadToSend({...payloadToSend, "PurgeHours":val})
        setDisplayData({...displayData, "PurgeHours":val})
    }

    const handleCo2SetPoint = (val) => {
        setPayloadToSend({...payloadToSend, "co2SetPoint":val})
        setDisplayData({...displayData, "co2SetPoint":val})
    }

    const handleCo2Target = (val) => {
        setPayloadToSend({...payloadToSend, "co2Target":val})
        setDisplayData({...displayData, "co2Target":val})
    }

    const handleMinTemp = (val) => {
        setPayloadToSend({...payloadToSend, "minTemp":changeTempToAs2Pref(val)})
        setDisplayData({...displayData, "minTemp":val})
    }

    const handleMaxTemp = (val) => {
        setPayloadToSend({...payloadToSend, "maxTemp":changeTempToAs2Pref(val)})
        setDisplayData({...displayData, "maxTemp":val})
    }

    const handleTime = (val) => {
        setPayloadToSend({...payloadToSend, "time":val})
        setDisplayData({...displayData, "time":val})
    }

    const handleFanOutput = (val) => {
        setPayloadToSend({...payloadToSend, "fanOutput":val})
        setDisplayData({...displayData, "fanOutput":val})
    }

    const handleDoorOutput = (val) => {
        setPayloadToSend({...payloadToSend, "doorOutput":val})
        setDisplayData({...displayData, "doorOutput":val})
    }

    // ------------SUBMIT HANDLER-------------------
    const submit = () => {
        const prepared_payload = () => {
            if(payloadToSend.selPurgeMode === '0'){
                return{"tag":payloadToSend.tag, selPurgeMode:"0"}
            }
            if(payloadToSend.selPurgeMode === '1'){
                const copy = {...payloadToSend}
                delete copy.co2SetPoint
                return copy
            }
            else if (payloadToSend.selPurgeMode === '2') {
                const copy = {...payloadToSend}
                delete copy.PurgeHours
                return copy
            } else if (payloadToSend.selPurgeMode === '3') {
                // continuous
                const copy = {...payloadToSend}
                delete copy.co2SetPoint;
                return copy;
            }
        }
        dispatch(postAgristar2Action(iotClient, prepared_payload()))
    }

    // -----------ERRORS---------------------------
    const [errors, setErrors] = useState({});
    useEffect(() => {
        setErrors({
            selPurgeMode: saving.p1Co2Purge?.errors?.selPurgeMode,
            PurgeHours: saving.p1Co2Purge?.errors?.PurgeHours,
            co2SetPoint: saving.p1Co2Purge?.errors?.co2SetPoint,
            co2Target: saving.p1Co2Purge?.errors?.co2Target,
            minTemp: saving.p1Co2Purge?.errors?.minTemp,
            maxTemp: saving.p1Co2Purge?.errors?.maxTemp,
            time: saving.p1Co2Purge?.errors?.time,
            fanOutput: saving.p1Co2Purge?.errors?.fanOutput,
            doorOutput: saving.p1Co2Purge?.errors?.doorOutput,
        });
    }, [saving]);

    // -----------ACTION STATUS--------------------


    return{
        isAuthorized,

        displayData,
        mapValueToLabel,
        saving,
        errors,

        handleSelPurgeMode,
        handlePurgeHours,
        handleCo2SetPoint,
        handleCo2Target,
        handleMinTemp,
        handleMaxTemp,
        handleTime,
        handleFanOutput,
        handleDoorOutput,

        submit,
    }
}
export default useAS2FormP1Co2Purge