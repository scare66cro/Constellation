import { Typography } from "@material-ui/core"
import { useEffect, useState } from "react"
import { useIntl } from "react-intl"
import { unitsDayShort, unitsHoursShort, unitsMinuteShort, unitsTimeNowShort } from "../utilities/translationObjects"

const TimePassedLabel = (props) => {
    let [dateTime, setDateTime] = useState(props.dateTime);
    let variant = props.variant || 'body2'
    let style = props.style

    useEffect(() => {
        if (dateTime < props.dateTime) {
            setDateTime(props.dateTime);
        }
    }, [props.dateTime, dateTime]);

    const intl = useIntl()
    
    const timePassed = (time) => {
        const timeObj = new Date(time)
        const elapsed = Date.now()-timeObj
        const minutes = Math.round(elapsed/60000)
        const hours = Math.round(minutes/60)
        const days = Math.round(hours/24)
        
        if(!dateTime || isNaN(minutes)){
            return '--'
        }
        if(minutes <= 0) {
            return intl.formatMessage(unitsTimeNowShort)
        }
        if(minutes < 60) {
            return `${minutes}`+intl.formatMessage(unitsMinuteShort)
        }
        if(hours < 24) {
            return `${hours}`+intl.formatMessage(unitsHoursShort)
        }        
        return `${days}`+intl.formatMessage(unitsDayShort)
    }

    return(
        <Typography variant={variant} style={style}>
            {timePassed(dateTime)}
        </Typography>
    )
}
export default TimePassedLabel