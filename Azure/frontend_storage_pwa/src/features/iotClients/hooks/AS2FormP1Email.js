import { useState, useEffect } from "react"
import { defineMessage, useIntl } from "react-intl"
import { useDispatch, useSelector } from "react-redux"
import { postAgristar2Action } from "../actions"
import {
  extractPermissionsFromIoTClient,
  selectSelectedIoTClient,
  extractAgristarEmailFromIoTClient,
  extractAgristarDisplayListFromIoTClient,
  selectSaving,
} from "../selectors"


const useASFormP1Email = () => {
    // ----------------HOOKS---------------------------
    const dispatch = useDispatch()
    const intl = useIntl()

    // ----------------SELECTORS----------------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state))
    const saving = useSelector((state)=>selectSaving(state));
    const obj_permissions = extractPermissionsFromIoTClient(iotClient)
    const email = extractAgristarEmailFromIoTClient(iotClient);
    let displayList = extractAgristarDisplayListFromIoTClient(iotClient) ?? {};
    displayList['not selected'] = 'not selected';

    // ----------------AUTHORIZATION-----------------
    const isAuthorized = obj_permissions?.['agristar2_action_level1']

    // ----------------FORM DATA---------------------
    const [payloadToSend, setPayloadToSend] = useState({
        tag: 'p1Comm',
        ...email
    });

    useEffect(() => {
      setPayloadToSend({
        tag: 'p1Comm',
        ...email,
      });
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [JSON.stringify(email)])

    // --------------UPDATE IF THERE IS A CHANGE---------
    const mapValueToLabel = {
      selEmailAlert :{
          0: intl.formatMessage(defineMessage({
              id:'p1Comm[2].enabled',
              defaultMessage:'Enabled',
          })),
          1: intl.formatMessage(defineMessage({
              id:'p1Comm[3].disabled',
              defaultMessage:'Disabled',
          })),
      },
      selEmailAuthType: {
        0: intl.formatMessage(defineMessage({
          id: 'p1Comm[8].starttls',
          defaultMessage: 'StartTLS',
        })),
        1: intl.formatMessage(defineMessage({
          id: 'p1Comm[9].tls-ssl',
          defaultMessage: 'TLS-SSL',
        })),
        2: intl.formatMessage(defineMessage({
          id: 'p1Comm[10].none',
          defaultMessage: 'None',
        })),
      },
    }

    // ---------CHANGE HANDLERS-----------------------
    const handlePayloadToSend = (key, val) => {
      setPayloadToSend({
          ...payloadToSend,
          [key]:val,
      });
    };

    // ---------SUBMIT--------------------------------
    const submitPayloadToSend = () => {
      dispatch(postAgristar2Action(iotClient, payloadToSend));
    }

    const findNodes = () => {
      dispatch(postAgristar2Action(iotClient, { tag: 'p1CommDisplay'}));
    }
    
    // ---------ERRORS--------------------------------
    const [errors, setErrors] = useState({});
    useEffect(() => {
      setErrors({
        selEmailAlert: saving.p1Comm?.errors?.selEmailAlert,
        emailTo: saving.p1Comm?.errors?.emailTo,
        emailFrom: saving.p1Comm?.errors?.emailFrom,
        emailServer: saving.p1Comm?.errors?.emailServer,
        selEmailAuthType: saving.p1Comm?.errors?.selEmailAuthType,
        emailPort: saving.p1Comm?.errors?.emailPort,
        emailAccount: saving.p1Comm?.errors?.emailAccount,
        emailPassword: saving.p1Comm?.errors?.emailPassword,
        selEmailDisplay: saving.p1Comm?.errors?.selEmailDisplay,
      });
    }, [saving]);

    return{
        isAuthorized,
        payloadToSend,
        mapValueToLabel,
        selEmailDisplay: email.selEmailDisplay,
        displayList,
        saving,
        errors,
        handlePayloadToSend,
        submitPayloadToSend,
        findNodes,
    }
}
export default useASFormP1Email;