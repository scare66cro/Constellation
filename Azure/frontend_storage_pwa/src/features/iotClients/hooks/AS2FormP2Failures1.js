import { useState, useEffect } from "react"
import { defineMessage, useIntl } from "react-intl";
import { useDispatch, useSelector } from "react-redux"
import { agristar2Refrigeration } from "../../../utilities/translationObjects";
import { postAgristar2Action } from "../actions"
import {
    extractPermissionsFromIoTClient,
    selectSelectedIoTClient,
    extractAgristar2Failures1FromIoTClient,
    extractAgristar2PayloadFromIoTClient,
    selectSaving,
    extractBoardTypeFromAgristarPayload,
    extractOnionModeFromAgristar2Payload,
    extractControllerVersion,
    extractIoTClientVersion,
    isVersionAtLeast,
} from "../selectors"

const useAS2FormP2Failures1 = () => {
    // ----------------HOOKS---------------------------
    const dispatch = useDispatch()

    // ----------------SELECTORS----------------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state));
    const saving = useSelector((state) => selectSaving(state));
    const obj_permissions = extractPermissionsFromIoTClient(iotClient)
    const intl = useIntl();
    const payload = extractAgristar2PayloadFromIoTClient(iotClient);
    const outputConfig = payload?.['OutputConfigData'];
    const inputConfig = payload?.['InputConfigData'];
    const pwmConfig = payload?.['P2PwmChannelData']
    const onionMode = extractOnionModeFromAgristar2Payload(payload);
    const boardType = extractBoardTypeFromAgristarPayload(payload);
    const failures1 = extractAgristar2Failures1FromIoTClient(iotClient);
    const controller = extractControllerVersion(payload);
    const IoTVersion = extractIoTClientVersion(payload);
    const is200plus = isVersionAtLeast(IoTVersion, 2, 0, 0);

    const lightsOffMessage = intl.formatMessage(defineMessage({
        id: 'p2FailuresSetupAS2.lights-off',
        defaultMessage: 'Lights Off',
    }));

    const failureModes = {
        0: intl.formatMessage(defineMessage({
            id: 'p2FailuresSetupAS2[7].none',
            defaultMessage: 'None',
        })),
        1: intl.formatMessage(defineMessage({
            id: 'p2FailuresSetupAS2[8].alarm',
            defaultMessage: 'Alarm',
        })),
        2: intl.formatMessage(defineMessage({
            id: 'p2FailuresSetupAS2[3].fail',
            defaultMessage: 'Fail',
        })),
    };

    const lightModes = {
        0: intl.formatMessage(defineMessage({
            id: 'p2FailuresSetupAS2[7].none',
            defaultMessage: 'None',
        })),
        1: intl.formatMessage(defineMessage({
            id: 'p2FailuresSetupAS2[8].alarm',
            defaultMessage: 'Alarm',
        })),
    };

    if (is200plus) {
        lightModes[3] = lightsOffMessage;
    }

    const LightsUnits = {
        1: intl.formatMessage(defineMessage({
            id: 'p2FailuresSetupAS2[5].minutes',
            defaultMessage: 'Minutes',
        })),
        60: intl.formatMessage(defineMessage({
            id: 'p2FailuresSetupAS2[17].hours',
            defaultMessage: 'Hours',
        })),
    };

    const RefridgeRun = {
        255: ' ',
        0: intl.formatMessage(defineMessage({
            id: 'p2FailuresSelectTranslatedText[0].recirculate',
            defaultMessage: 'Recirculate',
        })),
        1: intl.formatMessage(defineMessage({
            id: 'p2FailuresSelectTranslatedText[1].standby',
            defaultMessage: 'Standby',
        })),
    };

    if (boardType === 'AS1' || (boardType === 'AS2' && (controller === '1.05' || controller * 1 > 1.05))) {
        RefridgeRun['2'] = intl.formatMessage(agristar2Refrigeration);
    }

    // ----------------AUTHORIZATION-----------------
    const isAuthorized = obj_permissions?.['agristar2_action_level2']

    // ----------------FORM DATA---------------------
    const [payloadToSend, setPayloadToSend] = useState({
        tag:'p2FailuresSetupAS2',
        ...failures1
    });

    useEffect(() => {
        setPayloadToSend({
            tag: 'p2FailuresSetupAS2',
            ...failures1,
        })
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [JSON.stringify(failures1)]);

    // ---------CHANGE HANDLERS-----------------------
    const handlePayloadToSend = (key, val) => {
        setPayloadToSend({
            ...payloadToSend,
            [key]:val,
        });
    };

    // ---------SUBMIT--------------------------------
    const submitPayloadToSend = () => {
        dispatch(postAgristar2Action(iotClient, payloadToSend));
    }

    // ---------ERRORS--------------------------------
    const [errors, setErrors] = useState({});
    useEffect(() => {
        const climaCellBurner = onionMode
            ? {
                BurnerMode: saving.p2FailuresSetup?.errors?.BurnerMode,
                BurnerTimer: saving.p2FailuresSetup?.errors?.BurnerTimer,
            }
            : {
                ClimacellMode: saving.p2FailuresSetup?.errors?.ClimacellMode,
                ClimacellTimer: saving.p2FailuresSetup?.errors?.ClimacellTimer,
            };
        const humidifierAuxiliary = onionMode
            ? {
                Aux1Mode: saving.p2FailuresSetup?.errors?.Aux1Mode,
                Aux1Timer: saving.p2FailuresSetup?.errors?.Aux1Timer,
                Aux2Mode: saving.p2FailuresSetup?.errors?.Aux2Mode,
                Aux2Timer: saving.p2FailuresSetup?.errors?.Aux2Timer,
            }
            : {
                Humidifier1Mode: saving.p2FailuresSetup?.errors?.Humidifier1Mode,
                Humidifier1Timer: saving.p2FailuresSetup?.errors?.Humidifier1Timer,
                Humidifier2Mode: saving.p2FailuresSetup?.errors?.Humidifier2Mode,
                Humidifier2Timer: saving.p2FailuresSetup?.errors?.Humidifier2Timer,
            };

        boardType === 'AS2'
        ? setErrors({ // AS2
            FanMode: saving.p2FailuresSetupAS2?.errors?.FanMode,
            FanTimer: saving.p2FailuresSetupAS2?.errors?.FanTimer,
            ...climaCellBurner,
            RefridgeMode: saving.p2FailuresSetupAS2?.errors?.RefridgeMode,
            RefridgeTimer: saving.p2FailuresSetupAS2?.errors?.RefridgeTimer,
            RefrStagesMode: saving.p2FailuresSetupAS2?.errors?.RefrStagesMode,
            RefrStagesTimer: saving.p2FailuresSetupAS2?.errors?.RefrStagesTimer,
            HumidifiersMode: saving.p2FailuresSetupAS2?.errors?.HumidifiersMode,
            HumidifiersTimer: saving.p2FailuresSetupAS2?.errors?.HumidifiersTimer,
            AuxMode: saving.p2FailuresSetupAS2?.errors?.AuxMode,
            AuxTimer: saving.p2FailuresSetupAS2?.errors?.AuxTimer,
            HeatMode: saving.p2FailuresSetupAS2?.errors?.HeatMode,
            HeatTimer: saving.p2FailuresSetupAS2?.errors?.HeatTimer,
            CavityHeatMode: saving.p2FailuresSetupAS2?.errors?.CavityHeatMode,
            CavityHeatTimer: saving.p2FailuresSetupAS2?.errors?.CavityHeatTimer,
            LightsMode: saving.p2FailuresSetupAS2?.errors?.LightsMode,
            LightsTimer: saving.p2FailuresSetupAS2?.errors?.LightsTimer,
            LightsUnit: saving.p2FailuresSetupAS2?.errors?.LightsUnit,
            })
        : setErrors({ // AS1
            FanMode: saving.p2FailuresSetup?.errors?.FanMode,
            FanTimer: saving.p2FailuresSetup?.errors?.FanTimer,
            ...climaCellBurner,
            RefridgeMode: saving.p2FailuresSetup?.errors?.RefridgeMode,
            RefridgeTimer: saving.p2FailuresSetup?.errors?.RefridgeTimer,
            RefridgeRun: saving.p2FailuresSetup?.errors?.RefridgeRun,
            ...humidifierAuxiliary,
            CavityHeatMode: saving.p2FailuresSetup?.errors?.CavityHeatMode,
            CavityHeatTimer: saving.p2FailuresSetup?.errors?.CavityHeatTimer,
            LightsMode: saving.p2FailuresSetup?.errors?.LightsMode,
            LightsTimer: saving.p2FailuresSetup?.errors?.LightsTimer,
            LightsUnit: saving.p2FailuresSetup?.errors?.LightsUnit,
        });
    }, [saving, boardType, onionMode]);

    return{
        isAuthorized,
        payloadToSend,
        failureModes,
        lightModes,
        LightsUnits,
        RefridgeRun,
        noFan: inputConfig[0] === '-1' && pwmConfig[2]?.split(':')[2] === '-1',
        noRefrig: pwmConfig[1]?.split(':')[2] === '-1' && outputConfig[13] === '-1',
        noRefrigStage: inputConfig[13] === '-1' && inputConfig[21] === '-1',
        noCavity: inputConfig[5] === '-1',
        noLights: inputConfig[23] === '-1' && inputConfig[24] === '-1',
        noClimacell: inputConfig[3] === '-1',
        noHumid: inputConfig[7] === '-1',
        noHeat: inputConfig[4] === '-1',
        noAux: inputConfig.reduce((acc, curr, index) => (index >= 25 && index <= 32 && curr !== '-1') ? acc + 1 : acc, 0) === 0,
        noBurner: inputConfig[6] === '-1' && pwmConfig[3].split(':')[2] === '-1',
        onionMode,
        boardType,
        saving,
        errors,
        handlePayloadToSend,
        submitPayloadToSend,
    }
}
export default useAS2FormP2Failures1;