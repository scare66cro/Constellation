import { useEffect, useState } from "react";
import { useIntl, defineMessage } from 'react-intl';
import { useDispatch, useSelector } from "react-redux";
import { postAgristar2Action } from "../actions";
import {
    extractPermissionsFromIoTClient,
    selectSelectedIoTClient,
    extractAgristar2LogSettingsFromIoTClient,
    selectSaving,
} from "../selectors"

const useASFormP2LogSettings = () => {
    // ----------------HOOKS---------------------------
    const dispatch = useDispatch();
    const intl = useIntl();

    // ----------------SELECTORS----------------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state))
    const saving = useSelector((state)=>selectSaving(state));
    const obj_permissions = extractPermissionsFromIoTClient(iotClient)
    const log = extractAgristar2LogSettingsFromIoTClient(iotClient);

    // ----------------AUTHORIZATION-----------------
    const isAuthorized = obj_permissions?.['agristar2_action_level2']

    // ----------------FORM DATA---------------------
    const [payloadToSend, setPayloadToSend] = useState({
        "tag":"p2LogSettings",
        ...log
    });

    useEffect(() => {
        setPayloadToSend({
            tag: 'p2LogSettings',
            ...log,
        });
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [JSON.stringify(log)])

    // --------------UPDATE IF THERE IS A CHANGE---------
    const mapValueToLabel = {
        sdWrap :{
            0: intl.formatMessage(defineMessage({
                id:'p2LogSettings[5].no',
                defaultMessage:'No',
            })),
            1: intl.formatMessage(defineMessage({
                id:'p2LogSettings[6].yes',
                defaultMessage:'Yes',
            })),
        },
    }

    const initializeSDCard = () => {
        dispatch(postAgristar2Action(iotClient, {
            tag: 'button2',
            SdCardInit: 'Initialize SD Card',
        }));
    };

    const clearHistoryLog = () => {
        dispatch(postAgristar2Action(iotClient, {
            tag: 'button2',
            ClearUserLog: 'Clear History Log',
        }));
    };

    const clearActivityLog = () => {
        dispatch(postAgristar2Action(iotClient, {
            tag: 'button2',
            ClearSystemLog: 'Clear Activity Log',
        }));
    };

    // ---------CHANGE HANDLERS-----------------------
    const handlePayloadToSend = (key, val) => {
        setPayloadToSend({
            ...payloadToSend,
            [key]:val,
        });
    };

    // ---------SUBMIT--------------------------------
    const submitPayloadToSend = () => {
        const { tag, sdWrap, recInterval } = {...payloadToSend};
        dispatch(postAgristar2Action(iotClient, { tag, sdWrap, recInterval }));
    }

    const [errors, setErrors] = useState({});
    useEffect(()=>{
        setErrors({
            recInterval: saving.p2LogSettings?.errors?.recInterval,
            sdWrap: saving.p2LogSettings?.errors?.sdWrap,
        });
    }, [saving]);

    return{
        isAuthorized,
        payloadToSend,
        mapValueToLabel,
        saving,
        errors,
        initializeSDCard,
        clearHistoryLog,
        clearActivityLog,
        handlePayloadToSend,
        submitPayloadToSend,
    }
}
export default useASFormP2LogSettings;