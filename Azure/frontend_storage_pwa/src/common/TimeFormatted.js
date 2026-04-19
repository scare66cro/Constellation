import { useCookies } from "react-cookie"
import { useIntl } from "react-intl"

const DateTimeMessage = (props) => {

    const intl = useIntl()
    const [cookies] = useCookies()

    return(
        <>
            {intl.formatTime(props.date,
                {
                    year:props.year ,
                    month:props.month,
                    hours:props.hours,
                    minutes:props.minutes,
                    hourCycle: cookies['time-format'] || 'h12',
                }    
            )}
        </>
    )
}
export default DateTimeMessage