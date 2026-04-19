import { useEffect, useState } from "react"
import { defineMessage, useIntl } from "react-intl"
import { useDispatch, useSelector } from "react-redux"
import { postAgristar2Action } from "../actions"
import {
    extractAgristar2PayloadFromIoTClient, extractAgristarFanSpeedSettingsFromIoTClient,
    extractBeeModeFromAgristar2Payload, extractOnionModeFromAgristar2Payload,
    extractPermissionsFromIoTClient, selectSaving, selectSelectedIoTClient,
} from "../selectors"

const useAS2FormP1FanSpeed = () => {
    // ----------------HOOKS---------------------------
    const dispatch = useDispatch()
    const intl = useIntl()

    // ----------------SELECTORS----------------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state))
    const saving = useSelector((state) => selectSaving(state))
    const fan_speed_settings = extractAgristarFanSpeedSettingsFromIoTClient(iotClient)
    const obj_permissions = extractPermissionsFromIoTClient(iotClient)
    const payload = extractAgristar2PayloadFromIoTClient(iotClient);
    const onionMode = extractOnionModeFromAgristar2Payload(payload);
    const beeMode = extractBeeModeFromAgristar2Payload(payload);
    const fanData = payload?.['FreqCtrlData'];

    // ----------------AUTHORIZATION-----------------
    const isAuthorized = obj_permissions?.['agristar2_action_level1']

    // ----------------FORM DATA---------------------
    const [payloadToSend1, setPayloadToSend1] = useState({
        "tag":"p1SetFanSpeed",
        setFanSpeed: fan_speed_settings.setFanSpeed
    })

    const [payloadToSend2, setPayloadToSend2] = useState({
        "tag":"p1FreqCtrl",
        maxFanSpeed: fan_speed_settings.maxFanSpeed,
        minFanSpeed: fan_speed_settings.minFanSpeed,
        refrFanSpeed: fan_speed_settings.refrFanSpeed,
        recircFanSpeed: fan_speed_settings.recircFanSpeed,
        updFanSpeed: fan_speed_settings.updFanSpeed,
        selCoolingType: fan_speed_settings.selCoolingType,
        tempDiff: fan_speed_settings.tempDiff,
        selDiff1: fan_speed_settings.selDiff1,
        selDiff2: fan_speed_settings.selDiff2,
    })

    // --------------UPDATE IF THERE IS A CHANGE---------
    useEffect(()=>{
        setPayloadToSend1({
            "tag":"p1SetFanSpeed",
            setFanSpeed: fan_speed_settings.setFanSpeed
        })
        setPayloadToSend2({
            "tag":"p1FreqCtrl",
            maxFanSpeed: fan_speed_settings.maxFanSpeed,
            minFanSpeed: fan_speed_settings.minFanSpeed,
            refrFanSpeed: fan_speed_settings.refrFanSpeed,
            recircFanSpeed: fan_speed_settings.recircFanSpeed,
            updFanSpeed: fan_speed_settings.updFanSpeed,
            selCoolingType: fan_speed_settings.selCoolingType,
            tempDiff: fan_speed_settings.tempDiff,
            selDiff1: fan_speed_settings.selDiff1,
            selDiff2: fan_speed_settings.selDiff2,
        })
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [JSON.stringify(fan_speed_settings)])

    const plenumTempLabel = intl.formatMessage(defineMessage({
        id:'getp1FreqCtrlDataTranslatedText[1].plen.temp',
        defaultMessage:'Plenum Temperature',
    }));

    // ----------VALUE LABEL MAP-------------------------
    const mapValueToLabel = {
        selDiff1:{
            0: intl.formatMessage(defineMessage({
                id:'getp1FreqCtrlDataTranslatedText[0].plen.set',
                defaultMessage:'Plenum Setpoint (default)',
            })),
            1: plenumTempLabel,
        },
        selDiff2:{
            255: intl.formatMessage(defineMessage({
                id:'getp1FreqCtrlDataTranslatedText[5].return.temp',
                defaultMessage:'Return Air Temperature (default)',
            })),
            ...fan_speed_settings.tempsRef
        },
        selCoolingType: {
            0: intl.formatMessage(defineMessage({
                id:'getp1FreqCtrlDataTranslatedText[3].temperature.diff',
                defaultMessage:'Temperature Differential',
            })),
            1: intl.formatMessage(defineMessage({
                id:'getp1FreqCtrlDataTranslatedText[4].humidity',
                defaultMessage:'Humidity',
            })),
        },
    }

    // initialize the bee label
    const [beeDiff2, setBeeDiff2] = useState(beeMode ? { 254: plenumTempLabel } : undefined);

    // initialize the selDiff2 with beeDiff2
    const [selDiff2, setSelDiff2] = useState({...mapValueToLabel.selDiff2, ...beeDiff2})

    // if beeMode changes update beeDiff2
    useEffect(() => {
        setBeeDiff2(beeMode ? { 254: plenumTempLabel } : undefined);
    }, [beeMode, plenumTempLabel])

    // if beeDiff2 changes update the labels
    useEffect(() => {
        if (beeMode) {
            setSelDiff2({...mapValueToLabel.selDiff2, ...beeDiff2 });
        } else {
            setSelDiff2({...mapValueToLabel.selDiff2});
        }
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [beeDiff2])


    // ---------CHANGE HANDLERS-----------------------
    const handlePayloadToSend1 = (val) => {
        setPayloadToSend1({
            ...payloadToSend1,
            setFanSpeed: val
        })
    }

    const handlePayloadToSend2 = (key, val) => {
        setPayloadToSend2({
            ...payloadToSend2,
            [key]:val
        })
    }

    // ---------SUBMIT--------------------------------
    const submitPayloadToSend1 = () => {
        dispatch(postAgristar2Action(iotClient, payloadToSend1))
    }

    const submitPayloadToSend2 = () => {
        dispatch(postAgristar2Action(iotClient, payloadToSend2))
    }

    // ---------ERRORS--------------------------------
    const [errors, setErrors] = useState({ })
    useEffect(() => {
        setErrors({
            maxFanSpeed: saving.p1FreqCtrl?.errors?.maxFanSpeed,
            minFanSpeed: saving.p1FreqCtrl?.errors?.minFanSpeed,
            refrFanSpeed: saving.p1FreqCtrl?.errors?.refrFanSpeed,
            recircFanSpeed: saving.p1FreqCtrl?.errors?.recircFanSpeed,
            updFanSpeed: saving.p1FreqCtrl?.errors?.updFanSpeed,
            selCoolingType: saving.p1FreqCtrl?.errors?.selCoolingType,
            tempDiff: saving.p1FreqCtrl?.errors?.tempDiff,
            selDiff1: saving.p1FreqCtrl?.errors?.selDiff1,
            selDiff2: saving.p1FreqCtrl?.errors?.selDiff2,
            setFanSpeed: saving.p1SetFanSpeed?.errors?.setFanSpeed,
        })
    }, [saving])

    return{
        isAuthorized,

        payloadToSend1,
        payloadToSend2,

        mapValueToLabel,

        handlePayloadToSend1,
        handlePayloadToSend2,

        submitPayloadToSend1,
        submitPayloadToSend2,
        errors,
        saving,
        onionMode,
        fanData,
        selDiff2,
    }
}
export default useAS2FormP1FanSpeed;