import { useEffect, useState } from "react"
import { useDispatch, useSelector } from "react-redux"
import { postAgristar2Action } from "../actions"
import {
    extractAgristar2FreshAirFromIoTClient,
    extractPermissionsFromIoTClient,
    selectSelectedIoTClient,
    selectSaving,
} from "../selectors"

const useAS2FormP2FreshAir = () => {
    // ----------------HOOKS---------------------------
    const dispatch = useDispatch()

    // ----------------SELECTORS----------------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state));
    const saving = useSelector((state)=>selectSaving(state));
    const freshAir = extractAgristar2FreshAirFromIoTClient(iotClient);
    const obj_permissions = extractPermissionsFromIoTClient(iotClient)

    // ----------------AUTHORIZATION-----------------
    const isAuthorized = obj_permissions?.['agristar2_action_level2']

    // ----------------FORM DATA---------------------
    const [payloadToSend, setPayloadToSend] = useState({
        tag: "p2FreshAirSetup",
        ...freshAir,
    });

    // --------------UPDATE IF THERE IS A CHANGE---------
    useEffect(()=>{
        setPayloadToSend({
            tag:"p2FreshAirSetup",
            ...freshAir,
        })
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [JSON.stringify(freshAir)]);

    // ---------CHANGE HANDLERS-----------------------
    const handlePayloadToSend = (key, val) => {
        let newKey;
        switch (key) {
            case 'P': newKey = 'PAirValue'; break;
            case 'I': newKey = 'IAirValue'; break;
            case 'D': newKey = 'DAirValue'; break;
            case 'U': newKey = 'UAirValue'; break;
            default:
                newKey = key;
                break;
        }
        setPayloadToSend({
            ...payloadToSend,
            [newKey]:val
        });
    }

    // ---------SUBMIT--------------------------------
    const submitPayloadToSend = () => {
        dispatch(postAgristar2Action(iotClient, payloadToSend))
    }

    const [errors, setErrors] = useState({});
    useEffect(() => {
        setErrors({
            PAirValue: saving.p2FreshAirSetup?.errors?.PAirValue,
            IAirValue: saving.p2FreshAirSetup?.errors?.IAirValue,
            DAirValue: saving.p2FreshAirSetup?.errors?.DAirValue,
            UAirValue: saving.p2FreshAirSetup?.errors?.UAirValue,
            ActuatorTimes: saving.p2FreshAirSetup?.errors?.ActuatorTimes,
            CoolAirCycle: saving.p2FreshAirSetup?.errors?.CoolAirCycle,
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
export default useAS2FormP2FreshAir;
