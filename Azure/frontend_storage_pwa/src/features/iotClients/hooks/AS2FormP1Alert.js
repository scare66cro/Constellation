// GENERIC AS2 HOOKS

import { useEffect, useState } from "react";
import { useDispatch, useSelector } from "react-redux";
import { postAgristar2Action } from "../actions";
import {
    extractPermissionsFromIoTClient,
    selectSelectedIoTClient,
    extractAgristar1AlertSetupDataFromIoTClient,
    extractAgristar2AlertSetupDataFromIoTClient,
    extractASBoardTypeFromIoTClient,
    selectSaving,
} from "../selectors";
import { as2AlarmTranslations } from "../../../utilities/translationObjects";
// FORM HOOKS

// PLENUM SETTINGS FORM Email Alerts
export function useAS2FormP1Alert (form_data) {
    // This hook is for handling the p1Alert form submission.
    // ...form_data is the fields + values that have been passed in by user input

    // -------------HOOKS-------------------------------
    
    // -------------SELECTORS---------------------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state));
    const saving = useSelector((state)=>selectSaving(state));
    const obj_permissions = extractPermissionsFromIoTClient(iotClient);
    const boardType = extractASBoardTypeFromIoTClient(iotClient);
    const [alertSetup, keys] = boardType === 'AS2'
        ? extractAgristar2AlertSetupDataFromIoTClient(iotClient)
        : extractAgristar1AlertSetupDataFromIoTClient(iotClient);

    // -------------AUTHORIZATION-----------------------
    const isAuthorized = obj_permissions?.['agristar2_action_level1']

    // -------------INITIALIZING FORM AND AS2 PAYLOAD DATA------
    // aliased variables

    // PAYLOAD TO SUBMIT TO CLOUD
    const [payloadToSend, setPayloadToSend] = useState({
        ...alertSetup,
    })

    useEffect(() => {
        setPayloadToSend({
            ...alertSetup,
        });
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [JSON.stringify(alertSetup)])

    // --------------------HANDLE FIELD CHANGES-------------------

    const handlePayloadToSend = (key, val) => {
        setPayloadToSend({
            ...payloadToSend,
            [key]: (val === true ? 'on' : 'off')
        });
    }


    // ---------------------------ERRORS--------------------------

    // -------------------------SUBMIT-----------------------
    const dispatch = useDispatch()
    const submitPayloadToSend = () => {
        const sendItems = {};

        keys.forEach((item, index) => {
            if (payloadToSend[item] === 'on') {
                sendItems[index] = 'on';
            }
        });

        dispatch(postAgristar2Action(iotClient, {
            "tag": "p1AlertFrame",
            ...sendItems,
        }));
    }

    // ------------------------ACTION STATUS-----------------

    // ---------------------MONITORING / TESTING--------------

    return {
        payloadToSend, isAuthorized,
        as2AlarmTranslations,
        boardType, keys, saving,
        submitPayloadToSend, handlePayloadToSend,
    }
}

export default useAS2FormP1Alert;
