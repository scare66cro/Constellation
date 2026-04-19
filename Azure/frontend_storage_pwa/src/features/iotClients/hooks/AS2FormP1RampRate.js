// GENERIC AS2 HOOKS

import { useEffect, useState } from "react";
import { useDispatch, useSelector } from "react-redux";
import { postAgristar2Action } from "../actions";
import { extractAgristar2PayloadFromIoTClient, extractAgristar2RampRateFromIoTClient, extractPermissionsFromIoTClient, extractSystemModeFromAgristar2Payload, selectSaving, selectSelectedIoTClient } from "../selectors";
import { useIntl, defineMessage } from 'react-intl';

// FORM HOOKS

// PLENUM SETTINGS FORM TEMP & HUMID
export function useAS2FormP1RampRate (form_data) {
    // This hook is for handling the p1Plenum form submission.
    // ...form_data is the fields + values that have been passed in by user input

    // -------------HOOKS-------------------------------
    
    // -------------SELECTORS---------------------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state))
    const saving = useSelector((state) => selectSaving(state))
    const as2_ramprate_settings = extractAgristar2RampRateFromIoTClient(iotClient);
    const obj_permissions = extractPermissionsFromIoTClient(iotClient)
    const payload = extractAgristar2PayloadFromIoTClient(iotClient);
    const systemMode = extractSystemModeFromAgristar2Payload(payload);
    const cureMode = systemMode === '1' && payload?.EquipStatusData?.[8] === '1' &&
      payload?.EquipStatusData?.[52] === '0';

    const intl = useIntl();

    // -------------AUTHORIZATION-----------------------
    const isAuthorized = obj_permissions?.['agristar2_action_level1']

    // -------------INITIALIZING FORM AND AS2 PAYLOAD DATA------
    let pileLabels = as2_ramprate_settings?.pile_labels;

    let options = { 
      '255': intl.formatMessage(defineMessage({
        id:'rampModeDynTranslatedText[5].return',
        defaultMessage:'Return Air Temp',
      })),
    };

    Object.keys(pileLabels).forEach((label) => {
      options[label] = pileLabels[label];
    });

    // PAYLOAD TO SUBMIT TO CLOUD
    const [payloadToSend, setPayloadToSend] = useState({
        tag: "p1RampRate",
        updTemp: as2_ramprate_settings?.update_temp,
        rampUpdateHours: as2_ramprate_settings?.update_period === 'Automatically' ? 1 : as2_ramprate_settings.update_period,
        rampAutomatic: as2_ramprate_settings?.update_period === 'Automatically',
        rampTempDiff: as2_ramprate_settings?.temp_diff,
        selTemp: as2_ramprate_settings?.temp_ref,
        targetTemp: as2_ramprate_settings?.target_temp,
    })

    // -----------UPDATE IF SETTINGS CHANGED----------------
    useEffect(()=>{
        setPayloadToSend({        
          tag: "p1RampRate",
          updTemp: as2_ramprate_settings?.update_temp,
          rampUpdateHours: as2_ramprate_settings?.update_period === 'Automatically' ? 1 : as2_ramprate_settings.update_period,
          rampAutomatic: as2_ramprate_settings?.update_period === 'Automatically',
          rampTempDiff: as2_ramprate_settings?.temp_diff,
          selTemp: as2_ramprate_settings?.temp_ref,
          targetTemp: as2_ramprate_settings?.target_temp,
        });
    // eslint-disable-next-line react-hooks/exhaustive-deps
    },[JSON.stringify(as2_ramprate_settings)]);

    // --------------------HANDLE FIELD CHANGES-------------------
    const handleUpdateTempSet = (val) => {
        setPayloadToSend({...payloadToSend, updTemp: val });
    };

    const handleUpdatePeriodSet = (val) => {
        setPayloadToSend({...payloadToSend, rampUpdateHours: val});
    };

    const handleUpdateAutomaticSet = (val) => {
      setPayloadToSend({...payloadToSend, rampAutomatic: val, selTemp: '255'});
    };

    const handleTempDiffSet = (val) => {
      setPayloadToSend({...payloadToSend, rampTempDiff: val});
    };

    const handleTempRefSet = (val) => {
      setPayloadToSend({...payloadToSend, selTemp: val});
    };

    const handleTargetTempSet = (val) => {
      setPayloadToSend({...payloadToSend, targetTemp: val});
    };

    // ---------------------------ERRORS--------------------------
    const [errors, setErrors] = useState({})
    useEffect(() => {
      setErrors({
        updTemp: saving.p1RampRate?.errors?.updTemp,
        rampUpdateHours: saving.p1RampRate?.errors?.rampUpdateHours,
        targetTemp: saving.p1RampRate?.errors?.targetTemp,
        rampTempDiff: saving.p1RampRate?.errors?.rampTempDiff,
      })
    }, [saving])

    // -------------------------SUBMIT-----------------------
    const dispatch = useDispatch()
    const submit = () => {
        dispatch(postAgristar2Action(iotClient, payloadToSend))
    }

    // ------------------------ACTION STATUS-----------------

    // ---------------------MONITORING / TESTING--------------
    // useEffect(()=>{
    //     console.log('...display data', displayData)
    //     console.log('...payload for cloud', payloadToSend)
    // },[displayData, payloadToSend])
    
    // useEffect(()=>{
    //     console.log('...errors', errors)
    // },[errors])

    // useEffect(()=>{
    //     console.log('preferred', plenumTempSetPreferred)
    // },[plenumTempSetPreferred])

    return {
      payloadToSend, isAuthorized,
      handleTargetTempSet, handleTempDiffSet, handleTempRefSet, handleUpdatePeriodSet,
      handleUpdateAutomaticSet, handleUpdateTempSet,
      errors, saving, submit, options, cureMode,
    }
}

export default useAS2FormP1RampRate;
