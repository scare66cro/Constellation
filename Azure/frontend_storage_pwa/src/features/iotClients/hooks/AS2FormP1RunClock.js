import { useEffect, useState } from "react"
import { defineMessage, useIntl } from "react-intl"
import { useDispatch, useSelector } from "react-redux"
import { useClockFormat } from "../../../utilities/appTime"
import { categoryToColorMap } from "../../../utilities/dataMapAgristar2"
import { agristar2Refrigeration } from "../../../utilities/translationObjects"
import { postAgristar2Action } from "../actions"
import {
    extractAgristar2PayloadFromIoTClient, extractOnionModeFromAgristar2Payload,
    extractPermissionsFromIoTClient, selectSaving, selectSelectedIoTClient,} from "../selectors"

function AS2FormP1RunClock(props){
    // -------------------------PROPS-----------------
    // -------------------------HOOKS-----------------
    const usersPreferredClockFormat = useClockFormat()
    const dispatch = useDispatch()
    const intl = useIntl()

    // -----------------------SELECTORS---------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state));
    const saving = useSelector((state) => selectSaving(state));
    const runTimesData = extractAgristar2PayloadFromIoTClient(iotClient)?.['RunTimesData']
    const obj_permissions = extractPermissionsFromIoTClient(iotClient)
    const payload = extractAgristar2PayloadFromIoTClient(iotClient);
    const onionMode = extractOnionModeFromAgristar2Payload(payload);
    const outputConfig = payload?.['OutputConfigData'];
    const pwmConfig = payload?.['P2PwmChannelData']

    // -----------------------AUTHORIZATION-----------
    const isAuthorized = obj_permissions?.['agristar2_action_level1']

    // ---------------------- PAYLOAD TO SENND TO AGRISTAR--------
    const [payloadToSend, setPayloadToSend] = useState({
        'tag':'p1RunTimes',
        'runTimes': runTimesData,
    })

    useEffect(()=>{
        setPayloadToSend({
            ...payloadToSend,
            runTimes:runTimesData
        })
    // eslint-disable-next-line react-hooks/exhaustive-deps
    },[JSON.stringify(runTimesData)])

    // -----------------------MAP INFO----------------
    const mapModeToInfo = onionMode
    ? {
        3: {
            color: categoryToColorMap.standby,
            label:intl.formatMessage(defineMessage({
                id:'RunTimesDynTranslatedText[7].standby',
                defaultMessage:'Standby',
            }))
        },
        5: {
            color: categoryToColorMap.cure,
            label: intl.formatMessage(defineMessage({
                id: 'RunTimesDynTranslatedText[1].cure',
                defaultMessage: 'Cure',
            }))
        },
    }
    : {
        1:{
            color: categoryToColorMap.cooling,
            label:intl.formatMessage(defineMessage({
                id:'RunTimesDynTranslatedText[3].cooling',
                defaultMessage:'Cooling',
            }))
        },
        2:{
            color: categoryToColorMap.recirculating,
            label:intl.formatMessage(defineMessage({
                id:'RunTimesDynTranslatedText[5].recirculation',
                defaultMessage:'Recirculation',
            }))
        },
        3:{
            color: categoryToColorMap.standby,
            label:intl.formatMessage(defineMessage({
                id:'RunTimesDynTranslatedText[7].standby',
                defaultMessage:'Standby',
            }))
        },
        4:{
            color: categoryToColorMap.refrigeration,
            label: intl.formatMessage(agristar2Refrigeration),
        },
    }
    
    // -------------------USER CONTROLS DATA-------------------
    const [timeRange, setTimeRange] = useState([0,0]) 
        // ...Range is inclusive. [0,1] includes 1 and both would be set to selected mode
    const [selectedMode, setSelectedMode] = useState(onionMode ? 3 : 1)

    // -------------------CHANGE HANDLERS-----------------------
    const handleSelectMode = (val) => {
        // console.log(val)
        // let newRuntimes = [...payloadToSend.runTimes]
        // for(let i = timeRange[0]*1; i <= timeRange[1]; i++){
        //     newRuntimes[i] = val
        // }
        // setPayloadToSend({
        //     ...payloadToSend,
        //     runTimes: newRuntimes
        // })
        setSelectedMode(val)
    }

    const handleTimeRange = (event, val) => {
        setTimeRange(val)
    }

    const handleAlwaysOn = () => {
        const formatted = (`${selectedMode},`.repeat(47)+`${selectedMode}`).split(',')
        setPayloadToSend({...payloadToSend, runTimes:formatted})
    }

    const handleTimeNodeClick = (val) => {
        let runTimes = [...payloadToSend.runTimes]
        runTimes[val] = selectedMode
        setPayloadToSend({...payloadToSend, runTimes:runTimes})
    }

    // -------------------SUBMIT HANDLER------------------------
    const handleSubmit = () => {
        dispatch(postAgristar2Action(iotClient, payloadToSend))
    }

    return{
        isAuthorized,
        payloadToSend,
        saving,
        
        mapModeToInfo,

        usersPreferredClockFormat,

        timeRange,
        selectedMode,
        noRefrigeration: pwmConfig[1].split(':')[2] === '-1' && outputConfig[13] === '-1',
        handleSelectMode,
        handleTimeRange,
        handleAlwaysOn,
        handleTimeNodeClick,
        handleSubmit,
        onionMode,
    }
}

export default AS2FormP1RunClock;