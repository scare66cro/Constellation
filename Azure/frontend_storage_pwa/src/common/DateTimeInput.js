import { AccessTime, Today } from "@material-ui/icons";
import { DatePicker, TimePicker } from "@material-ui/pickers";
import { useCookies } from "react-cookie";

const DateInput = (props) => {

    const [cookies] = useCookies()

    const changeHandler = (date) => {
        // this gives back date as ISO string
        let newDate = new Date(Date.parse(date)).toISOString()
        props.onChange?.(newDate)
    }

    return(

        // https://material-ui-pickers.dev/localization/date-fns

        <div style={{display:'flex', width:'100%', justifyContent:'space-between'}}>

            <div style={{width:'calc(60% - 5px)'}} >
                <DatePicker 
                    fullWidth
                    label={props.label}
                    value={props.value}
                    onChange={changeHandler}
                    inputVariant={props.inputVariant || 'outlined'}
                    margin={props.margin || 'dense'}
                    format={props.format || 'MMM-d-yyyy'}
                    InputProps={{
                        endAdornment: (
                            <Today />
                        ),
                    }}
                />
            </div>
            
            <div style={{width:'40%'}} >
                <TimePicker 
                    fullWidth
                    value={props.value}
                    onChange={changeHandler}
                    inputVariant={props.inputVariant || 'outlined'}
                    margin={props.margin || 'dense'}
                    ampm={cookies['time-format'] === 'h24' ? false : true}
                    InputProps={{
                        endAdornment: (
                            <AccessTime />
                        ),
                    }}
                />
            </div>

        </div>

    )
}
export default DateInput