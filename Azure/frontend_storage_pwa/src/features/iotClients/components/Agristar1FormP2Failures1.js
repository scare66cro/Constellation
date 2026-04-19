import ButtonSave from '../../../common/ButtonSave';
import useAS1FormP2Failures1 from '../hooks/AS1FormP2Failures1';
import { Paper, TableContainer, Table, TableBody, TableRow, TableCell } from "@material-ui/core";
import { FormattedMessage, useIntl } from "react-intl";
import Agristar2FailureMode from './Agristar2FailureMode';
import { Agristar2InputFieldSelect } from './Agristar2InputFields';
import SaveComponent from './SaveComponent';
import { agristar2Refrigeration } from '../../../utilities/translationObjects';

const Agristar1FormP2Failures1 = (props) => {
    const intl = useIntl();
    const {
        payloadToSend,
        isAuthorized,
        handlePayloadToSend,
        LightsUnits,
        RefridgeRun,
        failureModes,
        saving,
        errors,
        onionMode,
        submitPayloadToSend,
    } = useAS1FormP2Failures1()

    return(
        <SaveComponent saving={saving?.p2FailuresSetup?.status}>
            <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle'}}>
                <TableContainer component={Paper}>
                    <Table>
                        <TableBody>
                            <TableRow>
                                <TableCell>
                                    <FormattedMessage 
                                        id='p2FailuresSetup[1].fan'
                                        defaultMessage='Fan'
                                    />
                                </TableCell>
                                <TableCell>
                                    <Agristar2FailureMode 
                                        modes={{'2': failureModes[2]}}
                                        isEnabled={isAuthorized}
                                        handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                        payloadToSend={payloadToSend}
                                        error={errors}
                                        mode='FanMode'
                                        timer='FanTimer'
                                        showMinutes
                                    />
                                </TableCell>
                            </TableRow>
                            { !onionMode
                                ?
                                <TableRow>
                                    <TableCell>
                                        <FormattedMessage 
                                            id='p2FailuresSetup[6].climacell'
                                            defaultMessage='ClimaCell'
                                        />
                                    </TableCell>
                                    <TableCell>
                                        <Agristar2FailureMode 
                                            modes={failureModes}
                                            isEnabled={isAuthorized}
                                            handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                            payloadToSend={payloadToSend}
                                            error={errors}
                                            mode='ClimacellMode'
                                            timer='ClimacellTimer'
                                            showMinutes
                                        />
                                    </TableCell>
                                </TableRow>
                                :
                                <TableRow>
                                    <TableCell>
                                        <FormattedMessage 
                                            id='p2FailuresDynTranslatedText[2].burner'
                                            defaultMessage='Burner'
                                        />
                                    </TableCell>
                                    <TableCell>
                                        <Agristar2FailureMode 
                                            modes={failureModes}
                                            isEnabled={isAuthorized}
                                            handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                            payloadToSend={payloadToSend}
                                            error={errors}
                                            mode='BurnerMode'
                                            timer='BurnerTimer'
                                            showMinutes
                                        />
                                    </TableCell>
                                </TableRow>
                            }
                            <TableRow>
                                <TableCell rowSpan={payloadToSend.RefridgeMode === '1' ? 2 : 1}>
                                    {intl.formatMessage(agristar2Refrigeration)}
                                </TableCell>
                                <TableCell>
                                    <Agristar2FailureMode 
                                        modes={failureModes}
                                        isEnabled={isAuthorized}
                                        handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                        payloadToSend={payloadToSend}
                                        error={errors}
                                        mode='RefridgeMode'
                                        timer='RefridgeTimer'
                                        showMinutes
                                    />
                                </TableCell>
                            </TableRow>
                            {
                                payloadToSend.RefridgeMode === '1' &&
                                <TableRow>
                                    <TableCell>
                                        <FormattedMessage
                                            id='p2FailuresSetup[10].run.in'
                                            defaultMessage='Run in:'
                                        />
                                        <Agristar2InputFieldSelect
                                            value={payloadToSend.RefridgeRun}
                                            error={errors.RefridgeRun}
                                            onChange={(e) => handlePayloadToSend('RefridgeRun', e.target.value)}
                                            options={RefridgeRun}
                                            disable={!isAuthorized}
                                            style={{marginTop:'-.2rem'}}
                                        />
                                    </TableCell>
                                </TableRow>
                            }
                            { !onionMode &&
                            <>
                                <TableRow>
                                    <TableCell>
                                        <FormattedMessage 
                                            id='p2FailuresSetup[11].humidifier1'
                                            defaultMessage='Humidifier #1'
                                        />
                                    </TableCell>
                                    <TableCell>
                                        <Agristar2FailureMode 
                                            modes={failureModes}
                                            isEnabled={isAuthorized}
                                            handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                            payloadToSend={payloadToSend}
                                            error={errors}
                                            mode='Humidifier1Mode'
                                            timer='Humidifier1Timer'
                                            showMinutes
                                        />
                                    </TableCell>
                                </TableRow>
                                <TableRow>
                                    <TableCell>
                                        <FormattedMessage 
                                            id='p2FailuresSetup[12].humidifier2'
                                            defaultMessage='Humidifier #2'
                                        />
                                    </TableCell>
                                    <TableCell>
                                        <Agristar2FailureMode 
                                            modes={failureModes}
                                            isEnabled={isAuthorized}
                                            handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                            payloadToSend={payloadToSend}
                                            error={errors}
                                            mode='Humidifier2Mode'
                                            timer='Humidifier2Timer'
                                            showMinutes
                                        />
                                    </TableCell>
                                </TableRow>
                            </>
                            }
                            { onionMode &&
                            <>
                                <TableRow>
                                    <TableCell>
                                        <FormattedMessage 
                                            id='p2FailuresDynTranslatedText[3].aux1'
                                            defaultMessage='Auxiliary #1'
                                        />
                                    </TableCell>
                                    <TableCell>
                                        <Agristar2FailureMode 
                                            modes={failureModes}
                                            isEnabled={isAuthorized}
                                            handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                            payloadToSend={payloadToSend}
                                            error={errors}
                                            mode='Aux1Mode'
                                            timer='Aux1Timer'
                                            showMinutes
                                        />
                                    </TableCell>
                                </TableRow>
                                <TableRow>
                                    <TableCell>
                                        <FormattedMessage 
                                            id='p2FailuresDynTranslatedText[4].aux2'
                                            defaultMessage='Auxiliary #2'
                                        />
                                    </TableCell>
                                    <TableCell>
                                        <Agristar2FailureMode 
                                            modes={failureModes}
                                            isEnabled={isAuthorized}
                                            handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                            payloadToSend={payloadToSend}
                                            error={errors}
                                            mode='Aux2Mode'
                                            timer='Aux2Timer'
                                            showMinutes
                                        />
                                    </TableCell>
                                </TableRow>
                            </>
                            }
                            { !onionMode &&
                            <>
                                <TableRow>
                                    <TableCell>
                                        <FormattedMessage 
                                            id='p2FailuresSetup[13].auxiliary'
                                            defaultMessage='Auxiliary'
                                        />
                                    </TableCell>
                                    <TableCell>
                                        <Agristar2FailureMode 
                                            modes={failureModes}
                                            isEnabled={isAuthorized}
                                            handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                            payloadToSend={payloadToSend}
                                            error={errors}
                                            mode='AuxMode'
                                            timer='AuxTimer'
                                            showMinutes
                                        />
                                    </TableCell>
                                </TableRow>
                                <TableRow>
                                    <TableCell>
                                        <FormattedMessage 
                                            id='p2FailuresSetup[14].heat'
                                            defaultMessage='Heat'
                                        />
                                    </TableCell>
                                    <TableCell>
                                        <Agristar2FailureMode 
                                            modes={failureModes}
                                            isEnabled={isAuthorized}
                                            handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                            payloadToSend={payloadToSend}
                                            error={errors}
                                            mode='HeatMode'
                                            timer='HeatTimer'
                                            showMinutes
                                        />
                                    </TableCell>
                                </TableRow>
                            </>
                            }
                            <TableRow>
                                <TableCell>
                                    <FormattedMessage 
                                        id='p2FailuresSetup[15].cavity.heat'
                                        defaultMessage='Cavity Heater'
                                    />
                                </TableCell>
                                <TableCell>
                                    <Agristar2FailureMode 
                                        modes={failureModes}
                                        isEnabled={isAuthorized}
                                        handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                        payloadToSend={payloadToSend}
                                        error={errors}
                                        mode='CavityHeatMode'
                                        timer='CavityHeatTimer'
                                        showMinutes
                                    />
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell>
                                    <FormattedMessage 
                                        id='p2FailuresSetup[16].bay.lights'
                                        defaultMessage='Bay Lights Monitor'
                                    />
                                </TableCell>
                                <TableCell>
                                    <Agristar2FailureMode 
                                        modes={failureModes}
                                        isEnabled={isAuthorized}
                                        handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                        payloadToSend={payloadToSend}
                                        error={errors}
                                        mode='LightsMode'
                                        timer='LightsTimer'
                                    >
                                        <Agristar2InputFieldSelect
                                            value={payloadToSend.LightsUnits}
                                            error={errors.LightsUnits}
                                            options={LightsUnits}
                                            onChange={(e)=>handlePayloadToSend('LightsUnits', e.target.value)}
                                            disabled={!isAuthorized}
                                            style={{marginTop:'-.2rem'}}
                                        />
                                    </Agristar2FailureMode>
                                </TableCell>
                            </TableRow>
                        </TableBody>
                    </Table>
                </TableContainer>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'center'}}>
                    <ButtonSave 
                        onClick={()=>submitPayloadToSend()}
                        disabled={!isAuthorized}
                        style={{margin:'5px auto'}}
                    />
                </div>
            </div>
        </SaveComponent>
    )
}
export default Agristar1FormP2Failures1;