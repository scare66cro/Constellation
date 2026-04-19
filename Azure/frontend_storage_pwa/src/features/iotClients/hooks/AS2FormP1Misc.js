import { useEffect, useState } from "react"
import { defineMessage, useIntl } from "react-intl"
import { useDispatch, useSelector } from "react-redux"
import { postAgristar2Action } from "../actions"
import {
    extractAgristar2MiscSettingsFromIoTClient,
    extractPermissionsFromIoTClient,
    selectSelectedIoTClient, 
    extractAgristar2PayloadFromIoTClient,
    extractBoardTypeFromAgristarPayload,
    selectSaving,
    extractOnionModeFromAgristar2Payload,
    extractBeeModeFromAgristar2Payload,
    extractIoTClientVersion,
    isVersionAtLeast,
} from "../selectors"

const useAS2FormP1Misc = () => {
    // ----------------HOOKS---------------
    const dispatch = useDispatch()
    const intl = useIntl()

    // ----------------SELECTORS-----------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state))
    const saving = useSelector((state)=>selectSaving(state));
    const misc_settings = extractAgristar2MiscSettingsFromIoTClient(iotClient)
    const payload = extractAgristar2PayloadFromIoTClient(iotClient);
    const pwmConfig = payload?.['P2PwmChannelData'];
    const outputConfig = payload?.['OutputConfigData'];
    const boardType = extractBoardTypeFromAgristarPayload(payload);
    const onionMode = extractOnionModeFromAgristar2Payload(payload);
    const beeMode = extractBeeModeFromAgristar2Payload(payload);
    const IoTClientVersion = extractIoTClientVersion(payload);
    const is200plus = isVersionAtLeast(IoTClientVersion, 2, 0, 0);

    const obj_permissions = extractPermissionsFromIoTClient(iotClient)

    // ----------------AUTHORIZATION--------
    const isAuthorized = obj_permissions?.['agristar2_action_level1']

    // ----------------UTILITIES------------
    // const changeScaleToAs2Pref = (val) => changeTempScale(
    //     val, // degrees to be rescaled
    //     usersPreferredSystem, // scale the user prefers
    //     outside_air_settings.tempType // scale the Agristar requires
    // )

    // -------------INITIALIZE FORM AND PAYLOAD DATA---------
    const [payloadToSend, setPayloadToSend] = useState({
        tag:'p1Misc',
        selRefrMode: misc_settings.selRefrMode,
        defrostInterval: misc_settings.defrostInterval,
        defrostTime: misc_settings.defrostTime,
        tempThresh: misc_settings.tempThresh,
        selCavityCtrl: misc_settings.selCavityCtrl,
        cavityDiff: misc_settings.cavityDiff,
        cavityDutyCycle: misc_settings.cavityDutyCycle,
        selCavityCtrlSensor: misc_settings.cavityDutyCycle,
        kbPref: misc_settings.kbPref,
        selCtrlMode: misc_settings.selCtrlMode,
        refrigThresh: misc_settings.refrigThresh,
        enthTarget: misc_settings.enthTarget,
    }) // PAYLOAD FOR AS2

    // -----------UPDATE IF SETTINGS CHANGED----------------
    useEffect(()=>{
        setPayloadToSend({
            tag:"p1Misc",
            selRefrMode: misc_settings.selRefrMode,
            defrostInterval: misc_settings.defrostInterval,
            defrostTime: misc_settings.defrostTime,
            tempThresh: misc_settings.tempThresh,
            selCavityCtrl: misc_settings.selCavityCtrl,
            cavityDiff: misc_settings.cavityDiff,
            cavityDutyCycle: misc_settings.cavityDutyCycle,
            selCavityCtrlSensor: misc_settings.cavityDutyCycle,
            kbPref: misc_settings.kbPref,
            selCtrlMode: misc_settings.selCtrlMode,
            refrigThresh: misc_settings.refrigThresh,
            enthTarget: misc_settings.enthTarget,
        })
    // eslint-disable-next-line react-hooks/exhaustive-deps
    },[JSON.stringify(misc_settings)])

    const enthalpyMessage = intl.formatMessage(defineMessage({
        id:'getp1MiscDataTranslatedText.enthalpy-cooling',
        defaultMessage: 'Enthalpy Cooling',
    }));

    // -----------VALUE LABEL MAP-----------------------------
    const mapValueToLabel = {
        selRefrMode:{
            0: intl.formatMessage(defineMessage({
                id:'getp1MiscDataTranslatedText[0].economizer',
                defaultMessage:'Economizer',
            })),
            1: intl.formatMessage(defineMessage({
                id:'getp1MiscDataTranslatedText[1].refrigeration.only',
                defaultMessage: 'Refrigeration Only',
            })),
        },
        selCtrlMode:{
            0: intl.formatMessage(defineMessage({
                id:'getp1MiscDataTranslatedText[2].cavity.heater',
                defaultMessage:'Cavity Heater Control',
            })),
            1: intl.formatMessage(defineMessage({
                id:'getp1MiscDataTranslatedText[3].pile.fan',
                defaultMessage: 'Pile Fan Control',
            }))
        },
        selCavityCtrl: {
            1: intl.formatMessage(defineMessage({
                id:'getp1MiscDataTranslatedText[4].off',
                defaultMessage:'Off',
            })),
            2: intl.formatMessage(defineMessage({
                id:'getp1MiscDataTranslatedText[5].manual',
                defaultMessage:'Manual',
            })),
            3: intl.formatMessage(defineMessage({
                id:'getp1MiscDataTranslatedText[6].automatic',
                defaultMessage:'Automatic',
            }))
        },
        kbPref:{
            0: intl.formatMessage(defineMessage({
                id:'getp1MiscDataTranslatedText[10].standard',
                defaultMessage:'Standard',
            })),
            1: intl.formatMessage(defineMessage({
                id:'getp1MiscDataTranslatedText[11].alphabetic',
                defaultMessage: 'Alphabetic',
            }))
        },
        pileTemps: misc_settings.pileTempsLabels,
    }

    if (is200plus) {
        mapValueToLabel.selRefrMode[2] = enthalpyMessage;
    }

    // ---------------CHANGE HANDLERS-------------------------
    const handlePayloadToSend = (key, val) => {
        setPayloadToSend({
            ...payloadToSend,
            [key]:val
        });
    }

    // --------------SUBMIT-------------------------
    const submitPayloadToSend = () => {
        dispatch(postAgristar2Action(iotClient, payloadToSend))
    }

    // --------------ERRORS-------------------------
    const [errors, setErrors] = useState({});
    useEffect(() => {
        setErrors({
            selRefrMode: saving.p1Misc?.errors?.selRefrMode,
            defrostInterval: saving.p1Misc?.errors?.defrostInterval,
            defrostTime: saving.p1Misc?.errors?.defrostTime,
            tempThresh: saving.p1Misc?.errors?.tempThresh,
            selCtrlMode: saving.p1Misc?.errors?.selCtrlMode,
            selCavityCtrl: saving.p1Misc?.errors?.selCavityCtrl,
            cavityCtrlType: saving.p1Misc?.errors?.cavityCtrlType,
            selCavityCtrlSensor: saving.p1Misc?.errors?.selCavityCtrlSensor,
            cavityDiff: saving.p1Misc?.errors?.cavityDiff,
            cavityDutyCycle: saving.p1Misc?.errors?.cavityDutyCycle,
            kbPref: saving.p1Misc?.errors?.kbPref,
            refrigThresh: saving.p1Misc?.errors?.refrigThresh,
            enthTarget: saving.p1Misc?.errors?.enthTarget,
        });
    }, [saving])

    // --------------ACTION STATUS------------------

    return{
        isAuthorized,
        mapValueToLabel,
        payloadToSend,
        pileTempsStart: misc_settings.pileTempsLabels[0],
        noRefrig: boardType === 'AS2' && outputConfig[13] === '-1' && pwmConfig[1].split(':')[2] === '-1',
        noHeat: boardType === 'AS2' && outputConfig[4] === '-1',
        noCavity: boardType === 'AS2' && outputConfig[5] === '-1',
        errors,
        saving,
        handlePayloadToSend,
        submitPayloadToSend,
        onionMode,
        beeMode,
        is200plus
    }
}
export default useAS2FormP1Misc