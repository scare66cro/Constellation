import { useEffect, useState } from "react"
import { defineMessage, useIntl } from "react-intl"
import { useDispatch, useSelector } from "react-redux"
import { postAgristar2Action } from "../actions"
import {
  extractAgristarHumidCtrlFromIoTClient,
  extractPermissionsFromIoTClient,
  extractPlenumHumidSetFromAgristar2Payload,
  extractAgristar2PayloadFromIoTClient,
  selectSelectedIoTClient,
  selectSaving,
} from "../selectors"


const useASFormP1HumidCtrl = () => {
    // ----------------HOOKS---------------------------
    const dispatch = useDispatch()
    const intl = useIntl()

    // ----------------SELECTORS----------------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state))
    const saving = useSelector((state)=>selectSaving(state))
    const humidCtrl = extractAgristarHumidCtrlFromIoTClient(iotClient)
    const obj_permissions = extractPermissionsFromIoTClient(iotClient)
    const payload = extractAgristar2PayloadFromIoTClient(iotClient)
    const humidSetpoint = extractPlenumHumidSetFromAgristar2Payload(payload);
    const [humidCtrlsAvailable, setHumidCtrlsAvailable] = useState(humidCtrl[0].humidHead.exists
      || humidCtrl[1].humidHead.exists
      || humidCtrl[2].humidHead.exists);

    useEffect(() => {
      setHumidCtrlsAvailable(humidCtrl[0].humidHead.exists
        || humidCtrl[1].humidHead.exists
        || humidCtrl[2].humidHead.exists)
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [JSON.stringify(humidCtrl)])

    // ----------------AUTHORIZATION-----------------
    const isAuthorized = obj_permissions?.['agristar2_action_level1']

    // ----------------FORM DATA---------------------
    const [payloadToSend, setPayloadToSend] = useState({
        tag:"p1HumidCtrl",
        humid: [...humidCtrl],
    });

    useEffect(() => {
      setPayloadToSend({
        tag: "p1HumidCtrl",
        humid: [...humidCtrl],
      });
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [JSON.stringify(humidCtrl)])

    // --------------UPDATE IF THERE IS A CHANGE---------
    const mapValueToLabel = {
      mode :{
          0: intl.formatMessage(defineMessage({
              id:'p1HumidCtrlDynTranslatedText[4].manual',
              defaultMessage:'Manual',
          })),
          1: intl.formatMessage(defineMessage({
              id:'p1HumidCtrlDynTranslatedText[5].timer',
              defaultMessage:'Timer (default)',
          })),
          2: intl.formatMessage(defineMessage({
            id:'p1HumidCtrlDynTranslatedText[6].automatic',
            defaultMessage:'Automatic',
          })),
      },
    }

    // ---------CHANGE HANDLERS-----------------------
    const handlePayloadToSend = (key, val) => {
      const newHumid = [...payloadToSend.humid];
      newHumid[key] = val;
      setPayloadToSend({
        tag: "p1HumidCtrl",
        humid: newHumid
      });
    }

    function updateDuration(index, systemMode, val) {
      let prop = {};
      prop[systemMode] = val;
      const newHumid = [...payloadToSend.humid];
      newHumid[index] = {...newHumid[index], ...prop};
      setPayloadToSend({
        tag: "p1HumidCtrl",
        humid: newHumid
      });
    }

    // ---------SUBMIT--------------------------------
    const submitPayloadToSend = (index) => {
      const humid = payloadToSend.humid[index];
      const newPayload = {
        tag: payloadToSend.tag,
        selHumidType: index.toString(),
        selHumidMode: humid.mode,
        coolOn: humid.coolOn,
        coolOff: humid.coolOff,
        recircOn: humid.recircOn,
        recircOff: humid.recircOff,
        refrigOn: humid.refrigOn,
        refrigOff: humid.refrigOff,
      };
      dispatch(postAgristar2Action(iotClient, newPayload));
    }
    
    // ---------ERRORS--------------------------------
    const [errors, setErrors] = useState({})
    useEffect(() => {
      setErrors({
        selHumidType: saving.p1HumidCtrl?.errors?.selHumidType,
        selHumidMode: saving.p1HumidCtrl?.errors?.selHumidMode,
        coolOn: saving.p1HumidCtrl?.errors?.coolOn,
        coolOff: saving.p1HumidCtrl?.errors?.coolOff,
        recircOn: saving.p1HumidCtrl?.errors?.recircOn,
        recircOff: saving.p1HumidCtrl?.errors?.recircOff,
        refrigOn: saving.p1HumidCtrl?.errors?.refrigOn,
        refrigOff: saving.p1HumidCtrl?.errors?.refrigOff,
      });
    }, [saving]);

    return{
        isAuthorized,
        payloadToSend,
        mapValueToLabel,
        humidSetpoint,
        humidCtrlsAvailable,
        saving,
        errors,
        handlePayloadToSend,
        updateDuration,
        submitPayloadToSend,
    }
}
export default useASFormP1HumidCtrl;