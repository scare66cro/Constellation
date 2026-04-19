import { useEffect, useState } from "react"
import { useDispatch, useSelector } from "react-redux"
import { postAgristar2Action } from "../actions"
import {
    extractAgristar2AvailableIoDataFromIoTClient, extractAgristar2PayloadFromIoTClient,
    extractAgristar2PwmOutputFromIoTClient, extractOnionModeFromAgristar2Payload,
    extractPecanModeFromAgristar2Payload, extractPermissionsFromIoTClient,
    extractSystemModeFromAgristar2Payload, selectSaving, selectSelectedIoTClient,
} from "../selectors"

const useAS2FormP2PwmOutput = () => {
    // ----------------HOOKS---------------------------
    const dispatch = useDispatch()

    // ----------------SELECTORS----------------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state));
    const saving = useSelector((state) => selectSaving(state));
    const pwmConfig = extractAgristar2PwmOutputFromIoTClient(iotClient);
    const ioInfo = extractAgristar2AvailableIoDataFromIoTClient(iotClient);
    const obj_permissions = extractPermissionsFromIoTClient(iotClient)
    const payload = extractAgristar2PayloadFromIoTClient(iotClient);
    const systemMode = extractSystemModeFromAgristar2Payload(payload);
    const potatoMode = systemMode === '0';
    const onionMode = extractOnionModeFromAgristar2Payload(payload);
    const pecanMode = extractPecanModeFromAgristar2Payload(payload);

    // ----------------AUTHORIZATION-----------------
    const isAuthorized = obj_permissions?.['agristar2_action_level2']

    // ----------------FORM DATA---------------------
    const [pwmChannels, setPwmChannels] = useState(pwmConfig.map((item) => item[2]));

    // --------------UPDATE IF THERE IS A CHANGE---------
    useEffect(()=>{
        setPwmChannels(pwmConfig.map((item) => item[2]));
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [JSON.stringify(pwmConfig)]);

    // ---------CHANGE HANDLERS-----------------------
    const handlePwmChannels = (i, j, val) => {
        const newChannels = [...pwmChannels];
        const oldIndex = newChannels.indexOf((i+j).toString());
        if (oldIndex !== -1) {
            newChannels[oldIndex] = '-1';
        }
        if (val !== '-1') {
            newChannels[val] = (i + j).toString();
        }
        setPwmChannels(newChannels);
    };

    // ---------SUBMIT--------------------------------
    const submitPayloadToSend = () => {
        const payloadToSend = { 
            "tag":"p2PwmFrameAS2",
            "p2PwmOutputs": "AS2",
        };
        ioInfo.forEach((io, i) => 
            io[0].indexOf('none') === -1 && (
                [...Array(io[3]*1)].forEach((_, j) => {
                    if (i !== 0 || j !== 1) {
                        payloadToSend[(i * 2 + j).toString()] = pwmChannels.indexOf((i*2+j).toString()).toString();
                    }
                })));
        dispatch(postAgristar2Action(iotClient, payloadToSend))
    }
    
    // ---------ERRORS--------------------------------


    return{
        isAuthorized,
        pwmConfig,
        pwmChannels,
        ioInfo,
        saving,
        handlePwmChannels,
        submitPayloadToSend,
        potatoMode,
        onionMode,
        pecanMode,
    }
}
export default useAS2FormP2PwmOutput;
