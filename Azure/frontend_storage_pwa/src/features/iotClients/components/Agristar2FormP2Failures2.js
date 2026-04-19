import ButtonSave from '../../../common/ButtonSave';
import useAS2FormP2Failures1 from '../hooks/AS2FormP2Failures1';
import useAS2FormP2Failures2 from '../hooks/AS2FormP2Failures2';
import { Paper, TableContainer, Table, TableBody, TableRow, TableCell, Typography } from "@material-ui/core";
import { FormattedMessage } from "react-intl";
import Agristar2FailureMode from './Agristar2FailureMode';
import Agristar2InputField from './Agristar2InputFields';
import SaveComponent from './SaveComponent';

const Agristar2FormP2Failures2 = (props) => {
    const {payloadToSend, isAuthorized, saving, errors, handlePayloadToSend, submitPayloadToSend} = useAS2FormP2Failures2();
    const { failureModes } = useAS2FormP2Failures1();

    const saveEnabled = () => {
        if (isAuthorized === false){
            return true
        }
    }

    return(
        <SaveComponent saving={saving?.p2FailuresSetup2?.status}>
            <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle'}}>
                <TableContainer component={Paper}>
                    <Table>
                        <TableBody>
                            <TableRow>
                                <TableCell>
                                    <FormattedMessage 
                                        id='p2FailuresSetup2[1].out.temp'
                                        defaultMessage='Outside Temperature Sensor'
                                    />
                                </TableCell>
                                <TableCell>
                                    <Agristar2FailureMode 
                                        modes={{'1': failureModes[1], '2': failureModes[2]}}
                                        isEnabled={isAuthorized}
                                        error={errors}
                                        handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                        payloadToSend={payloadToSend}
                                        mode='OutAirMode'
                                        timer='OutAirTimer'
                                        showMinutes
                                    />
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell>
                                    <FormattedMessage 
                                        id='p2FailuresSetup2[5].out.humidity'
                                        defaultMessage='Outside Humidity Sensor'
                                    />
                                </TableCell>
                                <TableCell>
                                    <Agristar2FailureMode 
                                        modes={failureModes}
                                        isEnabled={isAuthorized}
                                        error={errors}
                                        handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                        payloadToSend={payloadToSend}
                                        mode='OutHumidMode'
                                        timer='OutHumidTimer'
                                        showMinutes
                                    />
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell rowSpan='2'>
                                    <FormattedMessage 
                                        id='p2FailuresSetup2[6].high'
                                        defaultMessage='High'
                                    />
                                    &nbsp;CO<sub>2</sub>&nbsp;
                                    <FormattedMessage 
                                        id='p2FailuresSetup2[7].level'
                                        defaultMessage='Level'
                                    />
                                </TableCell>
                                <TableCell>
                                    <Agristar2FailureMode 
                                        modes={failureModes}
                                        isEnabled={isAuthorized}
                                        error={errors}
                                        handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                        payloadToSend={payloadToSend}
                                        mode='HighCo2Mode'
                                        timer='HighCo2Timer'
                                        showMinutes
                                    />
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell>
                                    <Typography component='div'>
                                        CO<sub>2</sub>&nbsp;
                                        <FormattedMessage 
                                            id='p2FailuresSetup2[8].co2.setpoint'
                                            defaultMessage='Setpoint'
                                        />
                                        <Agristar2InputField 
                                            type={'number'}
                                            value={payloadToSend.Co2Setpt || ''}
                                            error={errors.Co2Setpt}
                                            onChange={(e)=>handlePayloadToSend('Co2Setpt', e.target.value)}
                                            disabled={!isAuthorized}
                                            endAdornment='ppm'
                                        />
                                    </Typography>
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell rowSpan='2'>
                                    <FormattedMessage 
                                        id='p2FailuresSetup2[9].low.humid'
                                        defaultMessage='Low Plenum Humidity'
                                    />
                                </TableCell>
                                <TableCell>
                                    <Agristar2FailureMode 
                                        modes={failureModes}
                                        isEnabled={isAuthorized}
                                        error={errors}
                                        handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                        payloadToSend={payloadToSend}
                                        mode='LowHumidMode'
                                        timer='LowHumidTimer'
                                        showMinutes
                                    />
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell>
                                    <Typography component='div'>
                                        <FormattedMessage 
                                            id='p2FailuresSetup2[10].humid.setpoint'
                                            defaultMessage='Low Humidity Setpoint'
                                        />
                                        <Agristar2InputField 
                                            type={'number'}
                                            value={payloadToSend.LowHumidSet || ''}
                                            error={errors.LowHumidSet}
                                            onChange={(e)=>handlePayloadToSend('LowHumidSet', e.target.value)}
                                            disabled={!isAuthorized}
                                            endAdornment='%'
                                        />
                                    </Typography>
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell rowSpan='2'>
                                    <FormattedMessage 
                                        id='p2FailuresSetup2[11].plenum.sensor'
                                        defaultMessage='Plenum Sensor'
                                    />
                                </TableCell>
                                <TableCell>
                                    <Agristar2FailureMode 
                                        modes={{'1': failureModes[1], '2': failureModes[2]}}
                                        isEnabled={isAuthorized}
                                        error={errors}
                                        handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                        payloadToSend={payloadToSend}
                                        mode='PlenSenMode'
                                        timer='PlenSenTimer'
                                        showMinutes
                                    />
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell>
                                    <Typography component='div'>
                                        <FormattedMessage 
                                            id='p2FailuresSetup2[12].plen.difference1'
                                            defaultMessage='Difference of'
                                        />
                                        &nbsp;
                                        <Agristar2InputField 
                                            type={'number'}
                                            value={payloadToSend.PlenSenDiff || ''}
                                            error={errors.PlenSenDiff}
                                            onChange={(e)=>handlePayloadToSend('PlenSenDiff', e.target.value)}
                                            disabled={!isAuthorized}
                                            endAdornment={'\u00B0'}
                                        />
                                        &nbsp;
                                        <FormattedMessage 
                                            id='p2FailuresSetup2[13].plen.difference2'
                                            defaultMessage='between'
                                        />
                                        &nbsp;
                                        <FormattedMessage 
                                            id='p2FailuresSetup2[14].plen.difference3'
                                            defaultMessage='plenum temperature sensor'
                                        />
                                        &nbsp;#1&nbsp;
                                        <FormattedMessage 
                                            id='p2FailuresSetup2[15].plen.difference4'
                                            defaultMessage='and'
                                        />
                                        &nbsp;
                                        <FormattedMessage 
                                            id='p2FailuresSetup2[16].plen.difference5'
                                            defaultMessage='plenum temperature sensor'
                                        />
                                        &nbsp;#2&nbsp;
                                        <FormattedMessage 
                                            id='p2FailuresSetup2[17].plen.difference6'
                                            defaultMessage='triggers alarm/failure.'
                                        />
                                    </Typography>
                                </TableCell>
                            </TableRow>
                        </TableBody>
                    </Table>
                </TableContainer>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'center'}}>
                    <ButtonSave 
                        onClick={()=>submitPayloadToSend()}
                        disabled={saveEnabled()}
                        style={{margin:'5px auto'}}
                    />
                </div>
            </div>
        </SaveComponent>
    )
}
export default Agristar2FormP2Failures2;