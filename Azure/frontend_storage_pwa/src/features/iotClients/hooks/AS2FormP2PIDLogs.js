import { useEffect, useState } from "react"
import { useIntl, defineMessage } from "react-intl"
import { useDispatch, useSelector } from "react-redux"
import { postAgristar2Action } from "../actions"
import {
    extractAgristar2FreshAirFromIoTClient,
    extractAgristar2RefrigerationSetupFromIoTClient,
    extractPermissionsFromIoTClient,
    extractAgristar2LogSettingsFromIoTClient,
    selectSelectedIoTClient,
    selectSaving
} from "../selectors"

const useAS2FormP2PIDLogs = () => {
    // ----------------HOOKS---------------------------
    const dispatch = useDispatch();
    const intl = useIntl();

    // ----------------SELECTORS----------------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state));
    const saving = useSelector((state) => selectSaving(state));
    const { btnPIDDoorLog } = extractAgristar2FreshAirFromIoTClient(iotClient);
    const { btnPIDRefrigLog } = extractAgristar2RefrigerationSetupFromIoTClient(iotClient);
    const { pidWrap } = extractAgristar2LogSettingsFromIoTClient(iotClient);
    const obj_permissions = extractPermissionsFromIoTClient(iotClient)

    // ----------------AUTHORIZATION-----------------
    const isAuthorized = obj_permissions?.['agristar2_action_level2']

    // ----------------FORM DATA---------------------
    const [payloadToSend, setPayloadToSend] = useState({
        "tag":"p2FreshAirSetup",
        pidWrap,
    });

    const [pidLogs, setPidLogs] = useState({
        btnPIDDoorLog,
        btnPIDRefrigLog,
    });

    const mapValueToLabel = {
        pidWrap:{
            '0': intl.formatMessage(defineMessage({
                id:'getp2PIDLogDataTranslatedText[2].no',
                defaultMessage:'No',
            })),
            '1': intl.formatMessage(defineMessage({
                id: 'getp2PIDLogDataTranslatedText[3].yes',
                defaultMessage: 'Yes',
            })),
        },
    }
    

    // --------------UPDATE IF THERE IS A CHANGE---------
    useEffect(()=>{
        setPayloadToSend({
            "tag":"p2PIDLogs",
            pidWrap,
        })
    }, [pidWrap]);

    useEffect(() => {
        setPidLogs({
            btnPIDDoorLog,
            btnPIDRefrigLog,
        });
    }, [btnPIDDoorLog, btnPIDRefrigLog])

    // ---------CHANGE HANDLERS-----------------------
    const handlePayloadToSend = (key, val) => {
        setPayloadToSend({
            ...payloadToSend,
            [key]:val
        });
    }

    const toggleLog = (key, val) => {
        setPidLogs({
            ...pidLogs,
            [key]: val ? '1' : '0',
        });
        dispatch(postAgristar2Action(iotClient, {
            'tag': 'button2',
            [key]: val ? 'Turn On' : 'Turn Off',
        }));
    }

    const clearPIDLog = () => {
        dispatch(postAgristar2Action(iotClient, {
            'tag': 'button2',
            'PIDClearLog': 'Clear',
        }));
    };

    // ---------SUBMIT--------------------------------
    const submitPayloadToSend = () => {
        dispatch(postAgristar2Action(iotClient, payloadToSend))
    }
    
    return{
        isAuthorized,
        payloadToSend,
        mapValueToLabel,
        pidLogs,
        saving,
        clearPIDLog,
        toggleLog,
        handlePayloadToSend,
        submitPayloadToSend,
    }
}
export default useAS2FormP2PIDLogs;
