import { useEffect, useState } from "react"
import { useDispatch, useSelector } from "react-redux"
import { postAgristar2Action } from "../actions"
import {
    extractAgristar2AvailableIoDataFromIoTClient,
    extractAgristar1PwmOutputFromIoTClient,
    extractPermissionsFromIoTClient,
    selectSelectedIoTClient,
    selectSaving,
} from "../selectors"

const useAS1FormP2PwmOutput = () => {
    // ----------------HOOKS---------------------------
    const dispatch = useDispatch()

    // ----------------SELECTORS----------------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state));
    const saving = useSelector((state) => selectSaving(state));
    const pwmConfig = extractAgristar1PwmOutputFromIoTClient(iotClient);
    const ioInfo = extractAgristar2AvailableIoDataFromIoTClient(iotClient);
    const obj_permissions = extractPermissionsFromIoTClient(iotClient)

    let channelOptions = { '-1': 'None' };
    let row = 1;
    ioInfo
        .forEach((io) => {
            if (io[0].indexOf('none') === -1) {
                for (var j = 1; j <= io[3]*1; j += 1) {
                    channelOptions[row] = `${io[0]}-${j}`;
                    row += 1;
                }
            }
        });

    // ----------------AUTHORIZATION-----------------
    const isAuthorized = obj_permissions?.['agristar2_action_level2']

    // ----------------FORM DATA---------------------
    const [payloadToSend, setPayloadToSend] = useState(pwmConfig);

    // --------------UPDATE IF THERE IS A CHANGE---------
    useEffect(()=>{
        setPayloadToSend(pwmConfig);
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [JSON.stringify(pwmConfig)]);

    // ---------CHANGE HANDLERS-----------------------
    const handlePayloadToSend = (key, val) => {
        setPayloadToSend({
            ...payloadToSend,
            [key]: val,
        });
    };

    // ---------SUBMIT--------------------------------
    const submitPayloadToSend = () => {
        const payload = { 
            tag: "p2PwmFrame",
            p2PwmOutputs: "Agri-Star",
            ...payloadToSend,
        };
        dispatch(postAgristar2Action(iotClient, payload))
    }
    
    // ---------ERRORS--------------------------------


    return{
        isAuthorized,
        payloadToSend,
        channelOptions,
        saving,
        handlePayloadToSend,
        submitPayloadToSend,
    }
}
export default useAS1FormP2PwmOutput;
