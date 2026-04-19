import { useEffect, useState } from "react";
import { useIntl, defineMessage } from 'react-intl';

import { useDispatch, useSelector } from "react-redux"
import { postAgristar2Action } from "../actions"
import {
    extractAgristar2RefrigerationSetupFromIoTClient,
    extractPermissionsFromIoTClient,
    selectSelectedIoTClient,
    extractBoardTypeFromAgristarPayload,
    extractAgristar2PayloadFromIoTClient,
    extractAgristar2IONamesFromIoTClient,
    selectSaving,
} from "../selectors"

const useAS2FormP2RefrigerationSetup = () => {
    // ----------------HOOKS---------------------------
    const dispatch = useDispatch();
    const intl = useIntl();

    // ----------------SELECTORS----------------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state));
    const saving = useSelector((state) => selectSaving(state));
    const refrigeration = extractAgristar2RefrigerationSetupFromIoTClient(iotClient);
    const { OutputConfig } = extractAgristar2IONamesFromIoTClient(iotClient);
    const obj_permissions = extractPermissionsFromIoTClient(iotClient);
    const payload = extractAgristar2PayloadFromIoTClient(iotClient);
    const boardType = extractBoardTypeFromAgristarPayload(payload);

    // ----------------AUTHORIZATION-----------------
    const isAuthorized = obj_permissions?.['agristar2_action_level2']

    // ----------------FORM DATA---------------------
    const [stage5, setStage5] = useState(refrigeration.Stages[4].On === '255' ? 1 : 0);
    const [stage6, setStage6] = useState(refrigeration.Stages[5].On === '255' ? 1 : 0);

    refrigeration.Stages[4].On = '80';
    refrigeration.Stages[5].On = '95';

    const [payloadToSend, setPayloadToSend] = useState({
        tag: 'p2RefrigerationAS2',
        p2Refrigeration: 'AS2',
        ...refrigeration,
    });

    const mapValueToLabel = {
        RefrigerationPurge :{
            0: intl.formatMessage(defineMessage({
                id:'p2Refrigeration[7].normal',
                defaultMessage:'Normal',
            })),
            1: intl.formatMessage(defineMessage({
                id:'p2Refrigeration[8].pumpdown',
                defaultMessage:'Pump Down',
            })),
        },
        Stage5: {
            0: intl.formatMessage(defineMessage({
                id:'p2Refrigeration[12].stage5',
                defaultMessage:'Stage 5',
            })),
            1: intl.formatMessage(defineMessage({
                id:'p2Refrigeration[4].defrost5',
                defaultMessage:'Defrost',
            })),
        },
        Stage6: {
            0: intl.formatMessage(defineMessage({
                id:'p2Refrigeration[13].stage6',
                defaultMessage:'Stage 6',
            })),
            1: intl.formatMessage(defineMessage({
                id:'p2Refrigeration[4].defrost6',
                defaultMessage:'Defrost',
            })),
        }
    }

    // --------------UPDATE IF THERE IS A CHANGE---------
    useEffect(()=>{
        setPayloadToSend({
            tag: 'p2RefrigerationAS2',
            p2Refrigeration: 'AS2',
            ...refrigeration,
        });
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [JSON.stringify(refrigeration)]);

    // ---------CHANGE HANDLERS-----------------------
    const handlePayloadToSend = (key, val) => {
        let newKey;
        switch (key) {
            case 'P': newKey = 'PRefrValue'; break;
            case 'I': newKey = 'IRefrValue'; break;
            case 'D': newKey = 'DRefrValue'; break;
            case 'U': newKey = 'URefrValue'; break;
            default:
                newKey = key;
                break;
        }
        setPayloadToSend({
            ...payloadToSend,
            [newKey]:val
        });
    }

    const handleStageToSend = (index, key, val) => {
        var stage = {};
        if (key === 'Defrost') {
            if (val === '1') {
                stage = {On: '255', Off: '255'};
                if (index === 4) setStage5(1);
                if (index === 5) setStage6(1);
            } else if (val === '0') {
                if (index === 4) {
                    stage = { On: '80', Off: '70' };
                    setStage5(0);
                } else if (index === 5) {
                    stage = { On: '95', Off: '85' };
                    setStage6(0)
                }
            }
        } else {
            stage = {...payloadToSend.Stages[index], [key]:val};
        }

        setPayloadToSend({
            ...payloadToSend,
            Stages: [
                ...payloadToSend.Stages.slice(0, index),
                stage,
                ...payloadToSend.Stages.slice(index+1),
            ],
        });
    }

    // ---------SUBMIT--------------------------------
    const submitPayloadToSend = () => {
        const newStages = {};
        payloadToSend.Stages.forEach((item, index) => {
            newStages[`Stage${index + 1}On`] = item.On;
            newStages[`Stage${index + 1}Off`] = item.Off;
        });

        if (stage5 === 1) newStages['Stage5On'] = '255';
        if (stage6 === 1) newStages['Stage6On'] = '255';
        
        const newPayload = {
            tag: boardType === 'AS2' ? 'p2RefrigerationAS2' : 'p2Refrigeration',
            p2Refrigeration: boardType === 'AS2' ? 'AS2' : 'Agri-Star',
            ...newStages,
            PRefrValue: payloadToSend.PRefrValue,
            IRefrValue: payloadToSend.IRefrValue,
            DRefrValue: payloadToSend.DRefrValue,
            URefrValue: payloadToSend.URefrValue,
            RefrigerationPurge: payloadToSend.RefrigerationPurge,
            PurgeThreshold: payloadToSend.PurgeThreshold,
        }
        dispatch(postAgristar2Action(iotClient, newPayload))
    }

    const [errors, setErrors] = useState({});
    useEffect(() => {
        const refrigeration = boardType === 'AS2' ? 'p2RefrigerationAS2' : 'p2Refrigeration';
        setErrors({
            Stage1On: saving[refrigeration]?.errors?.Stage1On,
            Stage1Off: saving[refrigeration]?.errors?.Stage1Off,
            Stage2On: saving[refrigeration]?.errors?.Stage2On,
            Stage2Off: saving[refrigeration]?.errors?.Stage2Off,
            Stage3On: saving[refrigeration]?.errors?.Stage3On,
            Stage3Off: saving[refrigeration]?.errors?.Stage3Off,
            Stage4On: saving[refrigeration]?.errors?.Stage4On,
            Stage4Off: saving[refrigeration]?.errors?.Stage4Off,
            Stage5On: saving[refrigeration]?.errors?.Stage5On,
            Stage5Off: saving[refrigeration]?.errors?.Stage5Off,
            Stage6On: saving[refrigeration]?.errors?.Stage6On,
            Stage6Off: saving[refrigeration]?.errors?.Stage6Off,
            Stage7On: saving[refrigeration]?.errors?.Stage7On,
            Stage7Off: saving[refrigeration]?.errors?.Stage7Off,
            Stage8On: saving[refrigeration]?.errors?.Stage8On,
            Stage8Off: saving[refrigeration]?.errors?.Stage8Off,
            PRefrValue: saving[refrigeration]?.errors?.PRefrValue,
            IRefrValue: saving[refrigeration]?.errors?.IRefrValue,
            DRefrValue: saving[refrigeration]?.errors?.DRefrValue,
            URefrValue: saving[refrigeration]?.errors?.URefrValue,
            PurgeThreshold: saving[refrigeration]?.errors?.PurgeThreshold,
        });
    }, [saving, boardType]);

    return{
        isAuthorized,
        payloadToSend,
        mapValueToLabel,
        boardType,
        stage5,
        stage6,
        OutputConfig,
        saving,
        errors,
        handleStageToSend,
        handlePayloadToSend,
        submitPayloadToSend,
    };
}
export default useAS2FormP2RefrigerationSetup;
