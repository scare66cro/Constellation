import { useEffect, useState } from "react"
import { useDispatch, useSelector } from "react-redux"
import { postAgristar2Action } from "../actions"
import {
    extractPermissionsFromIoTClient,
    selectSelectedIoTClient,
    extractAgristar2Failures2FromIoTClient,
    selectSaving,
} from "../selectors"

const useASFormP2Failures2 = () => {
    // ----------------HOOKS---------------------------
    const dispatch = useDispatch()

    // ----------------SELECTORS----------------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state));
    const saving = useSelector((state) => selectSaving(state));
    const obj_permissions = extractPermissionsFromIoTClient(iotClient)
    const failures2 = extractAgristar2Failures2FromIoTClient(iotClient);

    // ----------------AUTHORIZATION-----------------
    const isAuthorized = obj_permissions?.['agristar2_action_level2']

    // ----------------FORM DATA---------------------
    const [payloadToSend, setPayloadToSend] = useState({
        "tag":"p2FailuresSetup2",
        ...failures2
    });

    useEffect(() => {
        setPayloadToSend({
            tag: 'p2FailuresSetup2',
            ...failures2,
        });
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [JSON.stringify(failures2)]);


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
            OutAirMode: saving.p2FailuresSetup2?.errors?.OutAirMode,
            OutAirTimer: saving.p2FailuresSetup2?.errors?.OutAirTimer,
            OutHumidMode: saving.p2FailuresSetup2?.errors?.OutHumidMode,
            OutHumidTimer: saving.p2FailuresSetup2?.errors?.OutHumidTimer,
            HighCo2Mode: saving.p2FailuresSetup2?.errors?.HighCo2Mode,
            HighCo2Timer: saving.p2FailuresSetup2?.errors?.HighCo2Timer,
            Co2Setpt: saving.p2FailuresSetup2?.errors?.Co2Setpt,
            LowHumidMode: saving.p2FailuresSetup2?.errors?.LowHumidMode,
            LowHumidTimer: saving.p2FailuresSetup2?.errors?.LowHumidTimer,
            LowHumidSet: saving.p2FailuresSetup2?.errors?.LowHumidSet,
            PlenSenMode: saving.p2FailuresSetup2?.errors?.PlenSenMode,
            PlenSenTimer: saving.p2FailuresSetup2?.errors?.PlenSenTimer,
            PlenSenDiff: saving.p2FailuresSetup2?.errors?.PlenSenDiff,
        });
    }, [saving]);

    return{
        isAuthorized,
        payloadToSend,
        saving,
        errors,
        handlePayloadToSend,
        submitPayloadToSend,
    }
}
export default useASFormP2Failures2;