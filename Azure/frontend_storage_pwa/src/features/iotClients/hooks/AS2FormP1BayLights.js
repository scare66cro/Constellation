// GENERIC AS2 HOOKS
import { useEffect, useState } from "react";
import { useIntl } from "react-intl";
import { useDispatch, useSelector } from "react-redux";
import { postAgristar2Action } from "../actions";
import { agristar2EquipmentStatusColumnStatusOff, agristar2EquipmentStatusColumnStatusOn } from "../../../utilities/translationObjects";
import {
    extractAgristar2PayloadFromIoTClient,
    extractPermissionsFromIoTClient,
    selectSaving,
    selectSelectedIoTClient,
} from "../selectors";

// FORM HOOKS

// PLENUM SETTINGS FORM TEMP & HUMID
function useAS2FormP1BayLights () {
    const intl = useIntl();
    // -------------HOOKS-------------------------------

    // -------------SELECTORS---------------------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state));
    const saving = useSelector((state)=>selectSaving(state));
    const payload = extractAgristar2PayloadFromIoTClient(iotClient);
    const baylights = payload?.['LoadMonitorData'];

    const obj_permissions = extractPermissionsFromIoTClient(iotClient)

    // -------------AUTHORIZATION-----------------------
    const isAuthorized = obj_permissions?.['agristar2_action_level1']

    // -------------INITIALIZING FORM AND AS2 PAYLOAD DATA------

    // PAYLOAD TO SUBMIT TO CLOUD
    const [payloadToSend, setPayloadToSend] = useState({
        tag: 'p1BaylightNames',
        lightsBay1Label: baylights?.[0],
        lightsBay2Label: baylights?.[1],
    });

    const [status, setStatus] = useState({
        bay1: iotClient.front_matter.main[19] === '1' ? intl.formatMessage(agristar2EquipmentStatusColumnStatusOff) : intl.formatMessage(agristar2EquipmentStatusColumnStatusOn),
        bay2: iotClient.front_matter.main[21] === '1' ? intl.formatMessage(agristar2EquipmentStatusColumnStatusOff) : intl.formatMessage(agristar2EquipmentStatusColumnStatusOn),
    });

    // -------------------------SUBMIT-----------------------
    const dispatch = useDispatch()
    const submitPayloadToSend = () => {
        dispatch(postAgristar2Action(iotClient, {
            tag: "p1BaylightNames",
            lightsBay1Label: payloadToSend.lightsBay1Label,
            lightsBay2Label: payloadToSend.lightsBay2Label,
        }));
    }

    const toggle = (light) => {
        dispatch(postAgristar2Action(iotClient, {
            tag: 'button2',
            [light]: 'Toggle',
        }))
    }

    useEffect(() => {
        setPayloadToSend({
            tag: "p1BaylightNames",
            lightsBay1Label: baylights?.[0],
            lightsBay2Label: baylights?.[1],
        })
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [JSON.stringify(baylights)]);

    useEffect(() => {
        setStatus({
            bay1: iotClient.front_matter.main[19] === '1' ? intl.formatMessage(agristar2EquipmentStatusColumnStatusOff) : intl.formatMessage(agristar2EquipmentStatusColumnStatusOn),
            bay2: iotClient.front_matter.main[21] === '1' ? intl.formatMessage(agristar2EquipmentStatusColumnStatusOff) : intl.formatMessage(agristar2EquipmentStatusColumnStatusOn),
        });
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [iotClient.front_matter]);

    const handlePayloadToSend = (key, value) => {
        setPayloadToSend({...payloadToSend, [key]: value});
    }

    // -----------ERRORS---------------------------
    const [errors, setErrors] = useState({});
    useEffect(() => {
        setErrors({
            lightsBay1Label: saving.p1BaylightNames?.errors?.lightsBay1Label,
            lightsBay2Label: saving.p1BaylightNames?.errors?.lightsBay2Label,
        })
    }, [saving]);

    return {isAuthorized, payloadToSend, submitPayloadToSend, saving, handlePayloadToSend, status, errors, toggle, iotClient}
}

export default useAS2FormP1BayLights;
