import DateFnsUtils from '@date-io/date-fns';
import { MuiPickersUtilsProvider, TimePicker, DatePicker } from '@material-ui/pickers';
import { FormattedMessage } from 'react-intl';
import ButtonSave from '../../../common/ButtonSave'
import useAS2FormP1DateTime from '../hooks/AS2FormP1DateTime';
import SaveComponent from './SaveComponent';

const Agristar2FormP1DateTime = (props) => {
    const {
        payloadToSend,
        isAuthorized,
        handleDateSet,
        handleTimeSet,
        submitPayloadToSend,
        saving,
    } = useAS2FormP1DateTime();

    return (
        <SaveComponent saving={saving?.p1DateTime?.status}>
            <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle'}}>
                <MuiPickersUtilsProvider utils={DateFnsUtils}>
                    <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between', maxWidth:'250px'}}>
                        <DatePicker
                            margin="normal"
                            label={
                                <FormattedMessage
                                    id='p1DateTime[1].date'
                                    defaultMessage='Date'
                                />
                            }
                            format="MM/dd/yyyy"
                            value={payloadToSend.Date}
                            onChange={(val) => handleDateSet(val)}
                            disabled={!isAuthorized}
                        />
                    </div>
                    <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between', maxWidth:'250px'}}>
                        <TimePicker
                            margin="normal"
                            label={
                                <FormattedMessage
                                    id='p1DateTime[2].time'
                                    defaultMessage='Time'
                                />
                            }
                            format="h:mm a"
                            value={payloadToSend.Time}
                            onChange={(val) => handleTimeSet(val)}
                            disabled={!isAuthorized}
                        />
                    </div>
                </MuiPickersUtilsProvider>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'center'}}>
                    <ButtonSave 
                        onClick={()=>submitPayloadToSend()}
                        disabled={!isAuthorized}
                        style={{margin:'5px auto'}}
                    />
                </div>
            </div>
        </SaveComponent>
    );
}

export default Agristar2FormP1DateTime;