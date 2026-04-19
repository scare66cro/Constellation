import ButtonSave from '../../../common/ButtonSave';
import useAS2FormP2Failures1 from '../hooks/AS2FormP2Failures1';
import {
    Paper, TableContainer, Table,
    TableBody, TableRow, TableCell,
} from "@material-ui/core";
import { FormattedMessage } from "react-intl";
import Agristar2FailureMode from './Agristar2FailureMode';
import { Agristar2InputFieldSelect } from './Agristar2InputFields';
import SaveComponent from './SaveComponent';

const Agristar2FormP2Failures1 = (props) => {
    const {
        payloadToSend, isAuthorized, handlePayloadToSend, LightsUnits, lightModes, failureModes, RefridgeRun,
        submitPayloadToSend, noFan, noRefrig, noRefrigStage, noCavity, noLights, noClimacell,
        noHumid, noHeat, noAux, noBurner, saving, errors, onionMode, boardType,
    } = useAS2FormP2Failures1()

    const saveEnabled = () => {
        if (isAuthorized === false){
            return true
        }
    }

    return(
        <SaveComponent saving={saving?.p2FailuresSetupAS2?.status}>
            <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle'}}>
                <TableContainer component={Paper}>
                    <Table>
                        <TableBody>
                            {
                                !noFan &&
                                <TableRow>
                                    <TableCell>
                                        <FormattedMessage 
                                            id='p2FailuresSetupAS2[1].fan'
                                            defaultMessage='Fan'
                                        />
                                    </TableCell>
                                    <TableCell colSpan="4">
                                        <Agristar2FailureMode 
                                            modes={{'2': failureModes[2]}}
                                            isEnabled={isAuthorized}
                                            error={errors}
                                            handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                            payloadToSend={payloadToSend}
                                            mode='FanMode'
                                            timer='FanTimer'
                                            showMinutes
                                        />
                                    </TableCell>
                                </TableRow>
                            }
                            {
                                onionMode && !noBurner &&
                                <TableRow>
                                    <TableCell>
                                        <FormattedMessage 
                                            id='p2FailuresDynTranslatedText[2].burner'
                                            defaultMessage='Burner'
                                        />
                                    </TableCell>
                                    <TableCell colSpan="4">
                                        <Agristar2FailureMode 
                                            modes={failureModes}
                                            isEnabled={isAuthorized}
                                            error={errors}
                                            handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                            payloadToSend={payloadToSend}
                                            mode='BurnerMode'
                                            timer='BurnerTimer'
                                            showMinutes
                                        />
                                    </TableCell>
                                </TableRow>
                            }
                            {
                                !noClimacell && !onionMode &&
                                <TableRow>
                                    <TableCell>
                                        <FormattedMessage 
                                            id='p2FailuresSetupAS2[6].climacell'
                                            defaultMessage='ClimaCell'
                                            description='Label for ClimaCell.'
                                        />
                                    </TableCell>
                                    <TableCell colSpan="4">
                                        <Agristar2FailureMode 
                                            modes={failureModes}
                                            isEnabled={isAuthorized}
                                            error={errors}
                                            handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                            payloadToSend={payloadToSend}
                                            mode='ClimacellMode'
                                            timer='ClimacellTimer'
                                            showMinutes
                                        />
                                    </TableCell>
                                </TableRow>
                            }
                            {
                                !noRefrig &&
                                <TableRow>
                                    <TableCell rowSpan={payloadToSend.RefridgeMode === '1' ? 2 : 1}>
                                        <FormattedMessage 
                                            id='p2FailuresSetupAS2[9].refrig.master'
                                            defaultMessage='Refrigeration Master'
                                        />
                                    </TableCell>
                                    <TableCell colSpan="4">
                                        <Agristar2FailureMode 
                                            modes={failureModes}
                                            isEnabled={isAuthorized}
                                            error={errors}
                                            handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                            payloadToSend={payloadToSend}
                                            mode='RefridgeMode'
                                            timer='RefridgeTimer'
                                            showMinutes
                                        />
                                    </TableCell>
                                </TableRow>
                            }
                            { onionMode && boardType === 'AS1' &&
                            <>
                                <TableRow>
                                    <TableCell>
                                        <FormattedMessage 
                                            id='p2FailuresDynTranslatedText[3].aux1'
                                            defaultMessage='Auxiliary #1'
                                        />
                                    </TableCell>
                                    <TableCell colSpan="4">
                                        <Agristar2FailureMode 
                                            modes={failureModes}
                                            isEnabled={isAuthorized}
                                            error={errors}
                                            handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                            payloadToSend={payloadToSend}
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
                                    <TableCell colSpan="4">
                                        <Agristar2FailureMode 
                                            modes={failureModes}
                                            isEnabled={isAuthorized}
                                            error={errors}
                                            handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                            payloadToSend={payloadToSend}
                                            mode='Aux2Mode'
                                            timer='Aux2Timer'
                                            showMinutes
                                        />
                                    </TableCell>
                                </TableRow>
                            </>
                            }
                            {
                                payloadToSend.RefridgeMode === '1' &&
                                <TableRow>
                                    <TableCell colSpan={2} align='right'>
                                        <FormattedMessage
                                            id='p2FailuresSetupAS2[10].run.in'
                                            defaultMessage='Run in:'
                                        />
                                    </TableCell>
                                    <TableCell colSpan={2}>
                                        <Agristar2InputFieldSelect
                                            value={payloadToSend.RefridgeRun}
                                            onChange={(e) => handlePayloadToSend('RefridgeRun', e.target.value)}
                                            error={errors.RefridgeRun}
                                            options={RefridgeRun}
                                            disable={!isAuthorized}
                                            style={{marginTop:'-.2rem'}}
                                        />
                                    </TableCell>
                                </TableRow>
                            }
                            {
                                !noRefrigStage &&
                                <TableRow>
                                    <TableCell>
                                        <FormattedMessage 
                                            id='p2FailuresSetupAS2[11].refrigeration.stages'
                                            defaultMessage='Refrigeration Stages'
                                        />
                                    </TableCell>
                                    <TableCell colSpan="4">
                                        <Agristar2FailureMode 
                                            modes={failureModes}
                                            isEnabled={isAuthorized}
                                            error={errors}
                                            handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                            payloadToSend={payloadToSend}
                                            mode='RefrStagesMode'
                                            timer='RefrStagesTimer'
                                            showMinutes
                                        />
                                    </TableCell>
                                </TableRow>
                            }
                            {
                                !noHumid &&
                                <TableRow>
                                    <TableCell>
                                        <FormattedMessage 
                                            id='p2FailuresSetupAS2[12].humidifiers'
                                            defaultMessage='Humidifiers'
                                        />
                                    </TableCell>
                                    <TableCell colSpan="4">
                                        <Agristar2FailureMode 
                                            modes={failureModes}
                                            isEnabled={isAuthorized}
                                            error={errors}
                                            handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                            payloadToSend={payloadToSend}
                                            mode='HumidifiersMode'
                                            timer='HumidifiersTimer'
                                            showMinutes
                                        />
                                    </TableCell>
                                </TableRow>
                            }
                            {
                                !noAux &&
                                <TableRow>
                                    <TableCell>
                                        <FormattedMessage 
                                            id='p2FailuresSetupAS2[13].auxiliary'
                                            defaultMessage='Auxiliary'
                                        />
                                    </TableCell>
                                    <TableCell colSpan="4">
                                        <Agristar2FailureMode 
                                            modes={failureModes}
                                            isEnabled={isAuthorized}
                                            error={errors}
                                            handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                            payloadToSend={payloadToSend}
                                            mode='AuxMode'
                                            timer='AuxTimer'
                                            showMinutes
                                        />
                                    </TableCell>
                                </TableRow>
                            }
                            {
                                !noHeat &&
                                <TableRow>
                                    <TableCell>
                                        <FormattedMessage 
                                            id='p2FailuresSetupAS2[14].heat'
                                            defaultMessage='Heat'
                                        />
                                    </TableCell>
                                    <TableCell colSpan="4">
                                        <Agristar2FailureMode 
                                            modes={failureModes}
                                            isEnabled={isAuthorized}
                                            error={errors}
                                            handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                            payloadToSend={payloadToSend}
                                            mode='HeatMode'
                                            timer='HeatTimer'
                                            showMinutes
                                        />
                                    </TableCell>
                                </TableRow>
                            }
                            {
                                !noCavity &&
                                <TableRow>
                                    <TableCell>
                                        <FormattedMessage 
                                            id='p2FailuresSetupAS2[15].cavity.heat'
                                            defaultMessage='Cavity Heater'
                                        />
                                    </TableCell>
                                    <TableCell colSpan="4">
                                        <Agristar2FailureMode 
                                            modes={failureModes}
                                            isEnabled={isAuthorized}
                                            error={errors}
                                            handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                            payloadToSend={payloadToSend}
                                            mode='CavityHeatMode'
                                            timer='CavityHeatTimer'
                                            showMinutes
                                        />
                                    </TableCell>
                                </TableRow>
                            }
                            {
                                !noLights &&
                                <TableRow>
                                    <TableCell>
                                        <FormattedMessage 
                                            id='p2FailuresSetupAS2[16].bay.lights'
                                            defaultMessage='Bay Lights Monitor'
                                        />
                                    </TableCell>
                                    <TableCell colSpan="4">
                                        <Agristar2FailureMode 
                                            modes={lightModes}
                                            isEnabled={isAuthorized}
                                            error={errors}
                                            handlePayloadToSend={(t, v) => handlePayloadToSend(t, v) }
                                            payloadToSend={payloadToSend}
                                            mode='LightsMode'
                                            timer='LightsTimer'
                                        >
                                            <Agristar2InputFieldSelect
                                                value={payloadToSend.LightsUnits}
                                                options={LightsUnits}
                                                onChange={(e)=>handlePayloadToSend('LightsUnits', e.target.value)}
                                                disabled={!isAuthorized}
                                                style={{marginTop:'-.2rem'}}
                                            />
                                        </Agristar2FailureMode>
                                    </TableCell>
                                </TableRow>
                            }
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
export default Agristar2FormP2Failures1;