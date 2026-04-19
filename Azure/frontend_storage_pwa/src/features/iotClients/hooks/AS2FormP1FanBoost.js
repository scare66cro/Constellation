import { useEffect, useState } from "react"
import { defineMessage, useIntl } from "react-intl"
import { useDispatch, useSelector } from "react-redux"
import { postAgristar2Action } from "../actions"
import {
    extractAgristarFanBoostSettingsFromIoTClient, extractPermissionsFromIoTClient, selectSaving, selectSelectedIoTClient,
} from "../selectors"

const useAS2FormP1FanBoost = () => {
    // ----------------HOOKS---------------------------
    const dispatch = useDispatch()
    const intl = useIntl()

    // ----------------SELECTORS----------------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state))
    const saving = useSelector((state) => selectSaving(state))
    const fanboost = extractAgristarFanBoostSettingsFromIoTClient(iotClient)
    const obj_permissions = extractPermissionsFromIoTClient(iotClient)

    // ----------------AUTHORIZATION-----------------
    const isAuthorized = obj_permissions?.['agristar2_action_level1']

    // ----------------FORM DATA---------------------
    const [payloadToSend, setPayloadToSend] = useState({
        tag: "p1FanBoost",
        selBoostMode: fanboost.selBoostMode,
        speed: fanboost.speed,
        hours: fanboost.hours,
        time: fanboost.time,
        temp: fanboost.temp,
    });


    // --------------UPDATE IF THERE IS A CHANGE---------
    useEffect(()=>{
        setPayloadToSend({
            tag: "p1FanBoost",
            selBoostMode: fanboost.selBoostMode,
            speed: fanboost.speed,
            hours: fanboost.hours,
            time: fanboost.time,
            temp: fanboost.temp,
        });
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [JSON.stringify(fanboost)])

    // ----------VALUE LABEL MAP-------------------------
    const mapValueToLabel = {
        selBoostMode:{
            0: intl.formatMessage(defineMessage({
                id:'level1.fanboost.none-default',
                defaultMessage:'None (default)',
            })),
            1: intl.formatMessage(defineMessage({
              id: 'level1.fanboost.temperature-based',
              defaultMessage: 'Temperature Based',
            })),
            2: intl.formatMessage(defineMessage({
              id: 'level1.fanboost.runtime-based',
              defaultMessage: 'Runtime Based',
            }))
        },
    };

    // ---------CHANGE HANDLERS-----------------------
    const handlePayloadToSend = (key, val) => {
      setPayloadToSend({
        ...payloadToSend,
        [key]: val
      });
    }

    // ---------SUBMIT--------------------------------
    const submitPayloadToSend = () => {
        dispatch(postAgristar2Action(iotClient, payloadToSend))
    }

    // ---------ERRORS--------------------------------
    const [errors, setErrors] = useState({ })
    useEffect(() => {
        setErrors({
            selBoostMode: saving.p1FanBoost?.errors?.selBoostMode,
            speed: saving.p1FanBoost?.errors?.speed,
            hours: saving.p1FanBoost?.errors?.hours,
            time: saving.p1FanBoost?.errors?.time,
            temp: saving.p1FanBoost?.errors?.temp,
        })
    }, [saving])

    return{
        isAuthorized,
        payloadToSend,
        mapValueToLabel,
        handlePayloadToSend,
        submitPayloadToSend,
        errors,
        saving,
    }
}
export default useAS2FormP1FanBoost;