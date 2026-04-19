import { useState, useEffect } from "react"
import { defineMessage, useIntl } from "react-intl";
import { useDispatch, useSelector } from "react-redux"
import { agristar2Refrigeration } from "../../../utilities/translationObjects";
import { postAgristar2Action } from "../actions"
import {
    extractPermissionsFromIoTClient,
    selectSelectedIoTClient,
    extractAgristar1Failures1FromIoTClient,
    selectSaving,
    extractAgristar2PayloadFromIoTClient,
    extractOnionModeFromAgristar2Payload,
} from "../selectors"

const useAS1FormP2Failures1 = () => {
    // ----------------HOOKS---------------------------
    const dispatch = useDispatch()

    // ----------------SELECTORS----------------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state))
    const saving = useSelector((state) => selectSaving(state))
    const obj_permissions = extractPermissionsFromIoTClient(iotClient)
    const failures1 = extractAgristar1Failures1FromIoTClient(iotClient);
    const payload = extractAgristar2PayloadFromIoTClient(iotClient);
    const onionMode = extractOnionModeFromAgristar2Payload(payload);

    const intl = useIntl();

    const failureModes = {
        0: intl.formatMessage(defineMessage({
            id: 'p2FailuresSetup[7].none',
            defaultMessage: 'None',
        })),
        1: intl.formatMessage(defineMessage({
            id: 'p2FailuresSetup[8].alarm',
            defaultMessage: 'Alarm',
        })),
        2: intl.formatMessage(defineMessage({
            id: 'p2FailuresSetup[3].fail',
            defaultMessage: 'Fail',
        })),
    };

    const LightsUnits = {
        1: intl.formatMessage(defineMessage({
            id: 'p2FailuresSetup[5].minutes',
            defaultMessage: 'Minutes',
        })),
        60: intl.formatMessage(defineMessage({
            id: 'p2FailuresSetup[17].hours',
            defaultMessage: 'Hours',
        })),
    };

    const RefridgeRun = {
        255: ' ',
        0: intl.formatMessage(defineMessage({
            id: 'p2FailuresSelectTranslatedText[0].Recirculate',
            defaultMessage: 'Recirculate',
        })),
        1: intl.formatMessage(defineMessage({
            id: 'p2FailuresSelectTranslatedText[1].Standby',
            defaultMessage: 'Standby',
        })),
        2: intl.formatMessage(agristar2Refrigeration),
    };

    // ----------------AUTHORIZATION-----------------
    const isAuthorized = obj_permissions?.['agristar2_action_level2']

    // ----------------FORM DATA---------------------
    const [payloadToSend, setPayloadToSend] = useState({
        tag: 'p2FailuresSetup',
        ...failures1,
    });

    useEffect(() => {
        setPayloadToSend({
            tag: 'p2FailuresSetup',
            ...failures1,
        });
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
        setErrors({
            FanMode: saving.p2FailuresSetup?.errors?.FanMode,
            FanTimer: saving.p2FailuresSetup?.errors?.FanTimer,
            ClimacellMode: saving.p2FailuresSetup?.errors?.ClimacellMode,
            ClimacellTimer: saving.p2FailuresSetup?.errors?.ClimacellTimer,
            RefridgeMode: saving.p2FailuresSetup?.errors?.RefridgeMode,
            RefridgeTimer: saving.p2FailuresSetup?.errors?.RefridgeTimer,
            Humidifier1Mode: saving.p2FailuresSetup?.errors?.Humidifier1Mode,
            Humidifier1Timer: saving.p2FailuresSetup?.errors?.Humidifier1Timer,
            Humidifier2Mode: saving.p2FailuresSetup?.errors?.Humidifier2Mode,
            Humidifier2Timer: saving.p2FailuresSetup?.errors?.Humidifier2Timer,
            Aux1Mode: saving.p2FailuresSetup?.errors?.Humidifier1Mode,
            Aux1Timer: saving.p2FailuresSetup?.errors?.Humidifier1Timer,
            Aux2Mode: saving.p2FailuresSetup?.errors?.Humidifier2Mode,
            Aux2Timer: saving.p2FailuresSetup?.errors?.Humidifier2Timer,
            AuxMode: saving.p2FailuresSetup?.errors?.AuxMode,
            AuxTimer: saving.p2FailuresSetup?.errors?.AuxTimer,
            HeatMode: saving.p2FailuresSetup?.errors?.HeatMode,
            HeatTimer: saving.p2FailuresSetup?.errors?.HeatTimer,
            CavityHeatMode: saving.p2FailuresSetup?.errors?.CavityHeatMode,
            CavityHeatTimer: saving.p2FailuresSetup?.errors?.CavityHeatTimer,
            LightsMode: saving.p2FailuresSetup?.errors?.LightsMode,
            LightsTimer: saving.p2FailuresSetup?.errors?.LightsTimer,
            LightsUnit: saving.p2FailuresSetup?.errors?.LightsUnit,
        });
    }, [saving]);

    return{
        isAuthorized,
        payloadToSend,
        failureModes,
        LightsUnits,
        RefridgeRun,
        saving,
        errors,
        onionMode,
        handlePayloadToSend,
        submitPayloadToSend,
    }
}
export default useAS1FormP2Failures1;