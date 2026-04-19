import { useEffect, useState } from "react"
import { useDispatch, useSelector } from "react-redux"
import { postAgristar2Action } from "../actions"
import {
    extractAgristar2PayloadFromIoTClient,
    extractAgristar2RunTimesFromIoTClient,
    extractOnionModeFromAgristar2Payload,
    extractPermissionsFromIoTClient,
    selectSaving,
    selectSelectedIoTClient,
} from "../selectors"

function AS2FormP1RunTimes(props){
    // -------------------------PROPS-----------------
    // -------------------------HOOKS-----------------
    const dispatch = useDispatch()

    // -----------------------SELECTORS---------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state));
    const saving = useSelector((state) => selectSaving(state));
    const runtimes = extractAgristar2RunTimesFromIoTClient(iotClient);
    const obj_permissions = extractPermissionsFromIoTClient(iotClient);
    const pl = extractAgristar2PayloadFromIoTClient(iotClient);
    const onionMode = extractOnionModeFromAgristar2Payload(pl);

    const [payload, setPayload] = useState(runtimes);

    // -----------------------AUTHORIZATION-----------
    const isAuthorized = obj_permissions?.['agristar2_action_level1']

    useEffect(()=>{
        setPayload({
            ...runtimes,
        });
    // eslint-disable-next-line react-hooks/exhaustive-deps
    },[JSON.stringify(runtimes)])

    // -------------------USER CONTROLS DATA-------------------

    // -------------------CHANGE HANDLERS-----------------------

    // -------------------SUBMIT HANDLER------------------------
    const resetDaily = () => {
        dispatch(postAgristar2Action(iotClient, {
            tag: 'gellert',
            DailyFanRuntime: 0,
        }));
    };

    const resetTotal = () => {
        dispatch(postAgristar2Action(iotClient, {
            tag: 'gellert',
            TotalFanRuntime: 0,
        }));
    };

    return{
        isAuthorized,
        payload,
        saving,
        resetDaily,
        resetTotal,
        onionMode,
    }
}

export default AS2FormP1RunTimes;