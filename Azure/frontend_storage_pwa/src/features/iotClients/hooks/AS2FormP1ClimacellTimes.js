import { useEffect, useState } from "react"
import { defineMessage, useIntl } from "react-intl"
import { useDispatch, useSelector } from "react-redux"
import { useClockFormat } from "../../../utilities/appTime"
import { categoryToColorMap } from "../../../utilities/dataMapAgristar2"
import { postAgristar2Action } from "../actions"
import { extractAgristar2PayloadFromIoTClient, extractPermissionsFromIoTClient, selectSaving, selectSelectedIoTClient } from "../selectors"

function AS2FormP1ClimacellTimes(props){
    // -------------------------PROPS-----------------
    // -------------------------HOOKS-----------------
    const usersPreferredClockFormat = useClockFormat()
    const dispatch = useDispatch()
    const intl = useIntl()

    // -----------------------SELECTORS---------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state));
    const saving = useSelector((state) => selectSaving(state));
    const climacellTimesData = extractAgristar2PayloadFromIoTClient(iotClient)?.['ClimacellTimesData']
    const obj_permissions = extractPermissionsFromIoTClient(iotClient)

    // -----------------------AUTHORIZATION-----------
    const isAuthorized = obj_permissions?.['agristar2_action_level1']

    // ---------------------- PAYLOAD TO SENND TO AGRISTAR--------
    const [payloadToSend, setPayloadToSend] = useState({
        'tag':'p1ClimacellTimes',
        'climacellTimes': climacellTimesData,
    })

    useEffect(()=>{
        setPayloadToSend({
            ...payloadToSend,
            climacellTimes:climacellTimesData
        })
    // eslint-disable-next-line react-hooks/exhaustive-deps
    },[JSON.stringify(climacellTimesData)])

    // -----------------------MAP INFO----------------
    const mapModeToInfo = {
        1:{
            color: categoryToColorMap.off,
            label:intl.formatMessage(defineMessage({
                id:'buttonsTranslatedText[13].off',
                defaultMessage:'Off',
            }))
        },
        2:{
            color: categoryToColorMap.on,
            label:intl.formatMessage(defineMessage({
                id:'buttonsTranslatedText[14].on',
                defaultMessage:'On',
            }))
        },
        3:{
            color: categoryToColorMap.coolingOn,
            label:intl.formatMessage(defineMessage({
                id:'buttonsTranslatedText[16].coolingOnly',
                defaultMessage:'Cooling Only',
            }))
        },
        4:{
            color: categoryToColorMap.auto,
            label:intl.formatMessage(defineMessage({
                id:'buttonsTranslatedText[15].auto',
                defaultMessage:'Auto',
            }))
        },
    }
    
    // -------------------USER CONTROLS DATA-------------------
    const [timeRange, setTimeRange] = useState([0,0]) 
        // ...Range is inclusive. [0,1] includes 1 and both would be set to selected mode
    const [selectedMode, setSelectedMode] = useState(1)

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
        setPayloadToSend({...payloadToSend, climacellTimes:formatted})
    }

    const handleTimeNodeClick = (val) => {
        let runTimes = [...payloadToSend.climacellTimes]
        runTimes[val] = selectedMode
        setPayloadToSend({...payloadToSend, climacellTimes:runTimes})
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

        handleSelectMode,
        handleTimeRange,
        handleAlwaysOn,
        handleTimeNodeClick,
        handleSubmit,
    }
}

export default AS2FormP1ClimacellTimes