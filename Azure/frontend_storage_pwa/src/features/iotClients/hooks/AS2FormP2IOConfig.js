import { useEffect, useState } from "react"
import { defineMessage, useIntl } from "react-intl"
import { useDispatch, useSelector } from "react-redux"
import { postAgristar2Action } from "../actions"
import {
    extractAgristar2IOConfigFromIoTClient,
    extractPermissionsFromIoTClient,
    selectSelectedIoTClient,
    extractAgristar2AvailableIoDataFromIoTClient,
    extractIOConfigPayload,
    selectSaving,
} from "../selectors"

const useAS2FormP2IOConfig = () => {
    // ----------------HOOKS---------------------------
    const dispatch = useDispatch()
    const intl = useIntl()

    // ----------------SELECTORS----------------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state));
    const saving = useSelector((state) => selectSaving(state));
    const ioConfigSettings = extractAgristar2IOConfigFromIoTClient(iotClient);
    const obj_permissions = extractPermissionsFromIoTClient(iotClient)
    const AvailableIoData = extractAgristar2AvailableIoDataFromIoTClient(iotClient);

    // ----------------AUTHORIZATION-----------------
    const isAuthorized = obj_permissions?.['agristar2_action_level2']

    // ----------------FORM DATA---------------------
    const [payloadToSend, setPayloadToSend] = useState({
        tag: 'p2IoConfigFrame',
        p2IoConfig: 'AS2',
        ...ioConfigSettings.PayloadToSend,
    });

    const [ioConfig, setIoConfig] = useState({
        ...ioConfigSettings,
    })

    // --------------UPDATE IF THERE IS A CHANGE---------
    useEffect(()=>{
        setIoConfig({
            ...ioConfigSettings,
        });
        setPayloadToSend({
            tag: 'p2IoConfigFrame',
            p2IoConfig: 'AS2',
            ...ioConfigSettings.PayloadToSend,
        });
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [JSON.stringify(ioConfigSettings)]);

    // ---------CHANGE HANDLERS-----------------------
    const handlePayloadToSend = (key, val) => {
        const io = key[0];
        const pid = key.substring(1);

        let newOutputConfig = [...ioConfig.OutputConfig];
        let newInputConfig = [...ioConfig.InputConfig];

        if (io === 'i') {
            // clear old value(s)
            let old = newInputConfig.indexOf(pid);
            if (old !== -1) {
                newInputConfig[old] = '-1';
            }
            old = newOutputConfig.indexOf(pid)
            if (old !== -1 && pid !== '3' && ioConfig.IoNames[old].split(':')[2] === '2') {
                newOutputConfig[old] = '-1';
            }

            // set new value
            if (val !== '-1') {
                newInputConfig[val] = pid;
                if (ioConfig.IoNames[val].split(':')[2] === '2') {
                    newOutputConfig[val] = pid;
                }
            }
            setIoConfig({
                ...ioConfig,
                InputConfig: [...newInputConfig],
                OutputConfig: [...newOutputConfig],
            });
        } else if (io === 'o') {
            // clear old value(s)
            let old = newOutputConfig.indexOf(pid);
            if (old !== -1) {
                newOutputConfig[old] = '-1';
            }
            old = newInputConfig.indexOf(pid);
            if (old !== -1 && ioConfig.IoNames[old].split(':')[2] === '2') {
                newInputConfig[old] = '-1';
            }
            // set new value(s)
            if (val !== '-1') {
                newOutputConfig[val] = pid;
                if (ioConfig.IoNames[val].split(':')[2] === '2') {
                    newInputConfig[val] = pid;
                }
            }
            setIoConfig({
                ...ioConfig,
                InputConfig: [...newInputConfig],
                OutputConfig: [...newOutputConfig],
            });
        } else if (io === 'pulseDoor') {
            newOutputConfig[40] = val;
            setIoConfig({
                ...ioConfig,
                OutputConfig: [...newOutputConfig],
            });
        }

        const Payload = extractIOConfigPayload(AvailableIoData, newOutputConfig, newInputConfig);
        setPayloadToSend({
            ...payloadToSend,
            ...Payload
        });
    }

    // ----------VALUE LABEL MAP-------------------------
    const mapValueToLabel = {
        PulseDoor:{
            0: intl.formatMessage(defineMessage({
                id:'p2IoConfigDynTranslatedText[3].disabled',
                defaultMessage:'Pulse Door - Disabled',
            })),
            1: intl.formatMessage(defineMessage({
                id:'p2IoConfigDynTranslatedText[4].enabled',
                defaultMessage:'Pulse Door - Enabled',
            }))
        },
    }

    // ---------SUBMIT--------------------------------
    const submitPayloadToSend = () => {
        dispatch(postAgristar2Action(iotClient, payloadToSend))
    }

    const setToDefault = () => {
        dispatch(postAgristar2Action(iotClient, {
            tag: 'button2',
            resetIoConfig: 'Set to Default',
        }));
    }
    
    // ---------ERRORS--------------------------------
    const [errors, setErrors] = useState({})
    useEffect(() => {
        setErrors({
            Duplicates: saving.p2IoConfigFrame?.errors?.Duplicates,
            BadOutputs: saving.p2IoConfigFrame?.errors?.BadOutputs,
        });
    }, [saving]);


    return{
        isAuthorized,
        ioConfig,
        mapValueToLabel,
        saving,
        errors,
        handlePayloadToSend,
        submitPayloadToSend,
        setToDefault,
    };
}
export default useAS2FormP2IOConfig;
