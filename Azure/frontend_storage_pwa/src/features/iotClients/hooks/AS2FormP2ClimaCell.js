import { useEffect, useState } from "react"
import { defineMessage, useIntl } from "react-intl"
import { useDispatch, useSelector } from "react-redux"
import { postAgristar2Action } from "../actions"
import {
    extractAgristar2ClimaCellFromIoTClient,
    extractPermissionsFromIoTClient,
    selectSelectedIoTClient,
    selectSaving,
} from "../selectors"

const useAS2FormP2ClimaCell = () => {
    // ----------------HOOKS---------------------------
    const dispatch = useDispatch()
    const intl = useIntl()

    // ----------------SELECTORS----------------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state));
    const saving = useSelector((state)=>selectSaving(state));
    const climaCell = extractAgristar2ClimaCellFromIoTClient(iotClient);
    const obj_permissions = extractPermissionsFromIoTClient(iotClient)

    // ----------------AUTHORIZATION-----------------
    const isAuthorized = obj_permissions?.['agristar2_action_level2']

    // ----------------FORM DATA---------------------
    const [payloadToSend, setPayloadToSend] = useState({
        "tag":"p2Climacell",
        ...climaCell,
    });

    // --------------UPDATE IF THERE IS A CHANGE---------
    useEffect(()=>{
        setPayloadToSend({
            "tag":"p2Climacell",
            ...climaCell,
        })
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [JSON.stringify(climaCell)]);

    // ----------VALUE LABEL MAP-------------------------
    const mapValueToLabel = {
        AltType:{
            0: intl.formatMessage(defineMessage({
                id:'p2Climacell[3].feet',
                defaultMessage:'Feet',
            })),
            1: intl.formatMessage(defineMessage({
                id: 'p2Climacell[4].meters',
                defaultMessage: 'Meters',
            })),
        },
    }

    // ---------CHANGE HANDLERS-----------------------
    const handlePayloadToSend = (key, val) => {
        let newKey;
        switch (key) {
            case 'P': newKey = 'PClimacellValue'; break;
            case 'I': newKey = 'IClimacellValue'; break;
            case 'D': newKey = 'DClimacellValue'; break;
            case 'U': newKey = 'UClimacellValue'; break;
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
    
    // ---------ERRORS--------------------------------
    const [errors, setErrors] = useState({});
    useEffect(()=>{
        setErrors({
            ClimacellEff: saving.p2Climacell?.errors?.ClimacellEff,
            Altitude: saving.p2Climacell?.errors?.Altitude,
            AltType: saving.p2Climacell?.errors?.AltType,
            PClimacellValue: saving.p2Climacell?.errors?.PClimacellValue,
            IClimacellValue: saving.p2Climacell?.errors?.IClimacellValue,
            DClimacellValue: saving.p2Climacell?.errors?.DClimacellValue,
            UClimacellValue: saving.p2Climacell?.errors?.UClimacellValue,
        });
    }, [saving])

    return{
        isAuthorized,
        payloadToSend,
        mapValueToLabel,
        saving,
        errors,
        handlePayloadToSend,
        submitPayloadToSend,
    }
}
export default useAS2FormP2ClimaCell;
