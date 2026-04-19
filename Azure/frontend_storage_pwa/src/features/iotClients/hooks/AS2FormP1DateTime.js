// GENERIC AS2 HOOKS
import DateFnsAdapter from '@date-io/date-fns';
import { useEffect, useState } from "react";
import { useDispatch, useSelector } from "react-redux";
import { postAgristar2Action } from "../actions";
import {
    extractAgristar2PayloadFromIoTClient,
    extractPermissionsFromIoTClient,
    selectSaving,
    selectSelectedIoTClient,
} from "../selectors";

// FORM HOOKS

// PLENUM SETTINGS FORM TEMP & HUMID
export function useAS2FormP1DateTime () {
    // -------------HOOKS-------------------------------
    
    // -------------SELECTORS---------------------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state));
    const saving = useSelector((state)=>selectSaving(state));
    const payload = extractAgristar2PayloadFromIoTClient(iotClient);
    const datetime = payload?.['DateTimeData'];
    const datefns = new DateFnsAdapter();

    const obj_permissions = extractPermissionsFromIoTClient(iotClient)

    // -------------AUTHORIZATION-----------------------
    const isAuthorized = obj_permissions?.['agristar2_action_level1']

    // -------------INITIALIZING FORM AND AS2 PAYLOAD DATA------

    // PAYLOAD TO SUBMIT TO CLOUD
    const [payloadToSend, setPayloadToSend] = useState({
        Date: datefns.parse(datetime?.[0], 'MM/dd/yyyy'),
        Time: datefns.parse(`${datetime?.[1]} ${datetime?.[2] === '1' ? 'PM' : 'AM'}`, 'h:mm:ss a'),
    });

    // -----------UPDATE IF SETTINGS CHANGED----------------
    useEffect(()=>{
        setPayloadToSend({        
            Date: datefns.parse(datetime?.[0], 'MM/dd/yyyy'),
            Time: datefns.parse(`${datetime?.[1]} ${datetime?.[2] === '1' ? 'PM' : 'AM'}`, 'h:mm:ss a'),
        });
    // eslint-disable-next-line react-hooks/exhaustive-deps
    },[JSON.stringify(datetime)])

    // --------------------HANDLE FIELD CHANGES-------------------
    const handleDateSet = (val) => {
        setPayloadToSend({...payloadToSend, Date: val });
    }

    const handleTimeSet = (val) => {
        setPayloadToSend({...payloadToSend, Time: val });
    }

    // -------------------------SUBMIT-----------------------
    const dispatch = useDispatch()
    const submitPayloadToSend = () => {
        dispatch(postAgristar2Action(iotClient, {
            tag: "p1DateTime",
            Date: datefns.format(payloadToSend.Date, 'MM/dd/yyyy'),
            Time: datefns.format(payloadToSend.Time, 'h:mm:ss'),
            TimeType: payloadToSend.Time.getHours() >= 12 ? 1 : 0,
        }));
    }

    return {isAuthorized, payloadToSend, handleDateSet, handleTimeSet, submitPayloadToSend, saving}
}

export default useAS2FormP1DateTime;
