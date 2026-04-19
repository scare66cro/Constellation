import { IconButton } from "@material-ui/core"
import ReportProblemRoundedIcon from '@material-ui/icons/ReportProblemRounded';

import { useState } from "react"
import { useIntl } from "react-intl"
import { useDispatch } from "react-redux"
import { as2AlarmTranslations } from "../../../utilities/translationObjects"
import { agristar2PayloadClearAlarm, getAgristar2FrontMatter, postAgristar2Action } from "../actions"
import AlarmsPopup from "./AlarmsPopup"

const AlarmsPopupAndIconButton = (props) => {
    // ---------------------PROPS-----------------------------------------
    const iotclient = props.iotclient
    const alarm_data = props.alarmData
    const style = props.style
    const isAuthorized = props.isAuthorized;

    // ---------------------HOOKS-----------------------------------------
    const intl = useIntl()
    const dispatch = useDispatch()
    
    // ---------------------GET FRESH AGRISTAR2 DATA-----------------------
    const getFreshAs2data = () => {
        dispatch(getAgristar2FrontMatter(iotclient))
    }

    // -------------------Open Alarm Popup-------------------------------
    const [alarmsPopupOpen, setAlarmsPopupOpen] = useState(false)
    const toggleAlarmsPopup = () => {
        setAlarmsPopupOpen(!alarmsPopupOpen)
    }

    // -------------Dismiss Alarms Action--------------------
    const dismissAllAlarms = () => {
        const actionPayload = agristar2PayloadClearAlarm(iotclient.id)
        dispatch(postAgristar2Action(iotclient, actionPayload))
        setAlarmsPopupOpen(false)
        // WAIT a couple seconds and get the data again
        // ...this will ensure the mode data catches up with the alarmDismissal
        // ...this has to do with the way the ClearAlarm post works between the iotClient and LightTPD server
        setTimeout(() => {
            getFreshAs2data()
        }, 5000);
    }

    return(
        <div 
            hidden={alarm_data && alarm_data.length > 0 ? false : true}
            style={{...style}}
        >   
            <IconButton style={{position:'relative', marginRight:'5px', padding:'0px'}}
                onClick={()=>toggleAlarmsPopup()}
            >
                {/* <Badge className={classes.badge} color={'error'} variant='dot' badgeContent={'2'}> */}
                    <ReportProblemRoundedIcon 
                        style={{fontWeight:'bolder', color:'#d90000'}} 
                        className='blinker'
                    />
                {/* </Badge> */}
            </IconButton>
            <AlarmsPopup 
                open={alarmsPopupOpen} 
                onClose={() => toggleAlarmsPopup()}
                lineItems={
                    // TRANSLATE ALARMS
                    alarm_data?.map(a => 
                        as2AlarmTranslations[a.split('=')[0]] ? intl.formatMessage(as2AlarmTranslations[a.split('=')[0]]) : a
                    )
                }
                onClickActionButton={()=>dismissAllAlarms(iotclient.id)}
                isAuthorized={isAuthorized}
            />
        </div>
    )
}
export default AlarmsPopupAndIconButton