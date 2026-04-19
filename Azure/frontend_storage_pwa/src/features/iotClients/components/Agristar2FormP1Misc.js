import { Typography, TableContainer, Table, TableBody, TableRow, TableCell } from '@material-ui/core';
import { FormattedMessage } from 'react-intl';
import ButtonSave from '../../../common/ButtonSave';
import useAS2FormP1Misc from '../hooks/AS2FormP1Misc';
import Agristar2InputField,{ Agristar2InputFieldSelect } from './Agristar2InputFields';
import SaveComponent from './SaveComponent';

const Agristar2FormP1Misc = (props) => {
    // -------------------HOOKS--------------------
    const {
        isAuthorized,
        mapValueToLabel,
        payloadToSend,
        noRefrig,
        noHeat,
        noCavity,
        onionMode,
        beeMode,
        errors,
        saving,
        handlePayloadToSend,
        submitPayloadToSend,
        is200plus,
    } = useAS2FormP1Misc()

    const saveEnabled = isAuthorized
    const inputDisabled = !isAuthorized

    const onCavityControlChange = (val) => {
        handlePayloadToSend('selCavityCtrl', val);
    }

    return(
        <SaveComponent saving={saving?.p1Misc?.status}>
            <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle', padding:'0px 5%'}}>
                <TableContainer>
                    <Table>
                        <TableBody>
                            {
                            !noRefrig &&
                            <>
                                <TableRow>
                                    <TableCell align="right">
                                        <Typography>
                                            <FormattedMessage 
                                                id='p1Misc[1].refrigeration.mode'
                                                defaultMessage='Refrigeration Mode:'
                                            />
                                        </Typography>
                                    </TableCell>
                                    <TableCell align="left">
                                        <Agristar2InputFieldSelect 
                                            value={payloadToSend.selRefrMode}
                                            options={mapValueToLabel.selRefrMode}
                                            error={errors.selRefrMode}
                                            onChange={(e)=>handlePayloadToSend('selRefrMode', e.target.value)}
                                            disabled={inputDisabled}
                                            style={{marginTop:'-.2rem'}}
                                        />
                                    </TableCell>
                                </TableRow>
                                {
                                    (is200plus && payloadToSend.selRefrMode === '2') &&
                                    <TableRow>
                                        <TableCell colSpan="2">
                                            <Typography align='justify' component='div' >
                                                <FormattedMessage
                                                    id='level1.miscellaneous.enthalpy-cooling-will-turn-off-if-refrigeration-is'
                                                    defaultMessage='Enthalpy cooling will turn off if refrigeration is'
                                                />
                                                <Agristar2InputField 
                                                    type={'number'}
                                                    value={payloadToSend.enthTarget}
                                                    error={errors.enthTarget}
                                                    onChange={(e)=>handlePayloadToSend('enthTarget', e.target.value)}
                                                    style={{width:'90px', marginLeft:'10px', marginTop:'-.2rem'}}
                                                    disabled={inputDisabled}
                                                    endAdornment='%'
                                                />&nbsp;
                                                <FormattedMessage
                                                    id='level1.miscellaneous.or-greater'
                                                    defaultMessage="or greater"
                                                />
                                            </Typography>
                                        </TableCell>
                                    </TableRow>
                                }
                                <TableRow>
                                    <TableCell colSpan="2">
                                        <Typography align='justify' component='div' >
                                            <FormattedMessage 
                                                id='p1Misc[2].refrigeration.air.defrost'
                                                defaultMessage='Refrigeration will air defost every '
                                            />
                                            <Agristar2InputField 
                                                type={'number'}
                                                value={payloadToSend.defrostInterval}
                                                error={errors.defrostInterval}
                                                onChange={(e)=>handlePayloadToSend('defrostInterval', e.target.value)}
                                                style={{width:'90px', marginLeft:'10px', marginTop:'-.2rem'}}
                                                disabled={inputDisabled}
                                            />
                                            <FormattedMessage 
                                                id='p1Misc[3].refrigeration.hours'
                                                defaultMessage='hours for'
                                            />
                                            <Agristar2InputField 
                                                type={'number'}
                                                value={payloadToSend.defrostTime}
                                                error={errors.defrostTime}
                                                onChange={(e)=>handlePayloadToSend('defrostTime', e.target.value)}
                                                style={{width:'90px', marginLeft:'10px', marginTop:'-.2rem'}}
                                                disabled={inputDisabled}
                                            />
                                            <FormattedMessage 
                                                id='p1Misc[4].minutes'
                                                defaultMessage='minutes.'
                                            />
                                        </Typography>
                                    </TableCell>
                                </TableRow>
                                { beeMode &&
                                    <TableRow>
                                        <TableCell colSpan="2">
                                            <Typography align='justify' component='div' >
                                                <FormattedMessage 
                                                    id='getp1MiscDataTranslatedText[9].norefrig'
                                                    defaultMessage='Refrigeration will not run if outside air temperature is below'
                                                />
                                                &nbsp;
                                                <Agristar2InputField 
                                                    type={'number'}
                                                    value={payloadToSend.refrigThresh}
                                                    error={errors.refrigThresh}
                                                    onChange={(e)=>handlePayloadToSend('refrigThresh', e.target.value)}
                                                    style={{width:'90px', marginLeft:'10px', marginTop:'-.2rem'}}
                                                    endAdornment={'\u00B0'}
                                                    disabled={inputDisabled}
                                                />
                                            </Typography>
                                        </TableCell>
                                    </TableRow>
                                }
                            </>
                            }
                            {
                                !noHeat && !onionMode &&
                                <TableRow>
                                    <TableCell colSpan="2">
                                        <Typography align='justify' component='div' >
                                            <FormattedMessage 
                                                id='p1Misc[5].heater.on'
                                                defaultMessage='Heater will turn on if plenum temperature is'
                                            />
                                            <Agristar2InputField 
                                                type={'number'}
                                                value={payloadToSend.tempThresh}
                                                error={errors.tempThresh}
                                                onChange={(e)=>handlePayloadToSend('tempThresh', e.target.value)}
                                                style={{width:'90px', marginLeft:'10px', marginTop:'-.2rem'}}
                                                endAdornment={'\u00B0'}
                                                disabled={inputDisabled}
                                            />
                                            <FormattedMessage 
                                                id='p1Misc[6].below.plenum'
                                                defaultMessage='below plenum setpoint'
                                            />
                                        </Typography>
                                    </TableCell>
                                </TableRow>
                            }
                            {
                                !noCavity &&
                                <>
                                <TableRow>
                                    <TableCell align="right">
                                        <Agristar2InputFieldSelect 
                                            value={payloadToSend.selCtrlMode}
                                            options={mapValueToLabel.selCtrlMode}
                                            error={errors.selCtrlMode}
                                            onChange={(e)=>handlePayloadToSend('selCtrlMode', e.target.value)}
                                            disabled={inputDisabled}
                                        />
                                    </TableCell>
                                    <TableCell align="left">
                                        <Agristar2InputFieldSelect 
                                            value={payloadToSend.selCavityCtrl}
                                            options={mapValueToLabel.selCavityCtrl}
                                            error={errors.selCavityCtrl}
                                            onChange={(e) => onCavityControlChange(e.target.value)}
                                            disabled={inputDisabled}
                                        />
                                    </TableCell>
                                </TableRow>
                                <TableRow>
                                    <TableCell align="right">
                                        <Typography component='div' >
                                            <FormattedMessage 
                                                id='p1Misc[7].temp.diff'
                                                defaultMessage='Temperature Differential:'
                                            />
                                        </Typography>
                                    </TableCell>
                                    <TableCell align="left">
                                        <Agristar2InputField 
                                            type={'number'}
                                            value={payloadToSend.cavityDiff}
                                            error={errors.cavityDiff}
                                            onChange={(e)=>handlePayloadToSend('cavityDiff', e.target.value)}
                                            style={{width:'90px', marginLeft:'10px'}}
                                            endAdornment={'\u00B0'}
                                            disabled={inputDisabled || payloadToSend.selCavityCtrl === "1"}
                                        />
                                    </TableCell>
                                </TableRow>
                                <TableRow>
                                {
                                    payloadToSend.selCavityCtrl !== '3' ? (
                                    <>
                                        <TableCell align="right">
                                            <Typography component='div' >
                                                <FormattedMessage 
                                                    id='getP1MiscDataTranslatedText[8].duty.cycle'
                                                    defaultMessage='Duty Cycle'
                                                />
                                            </Typography>
                                        </TableCell>
                                        <TableCell align="left">
                                            <Agristar2InputField 
                                                type={'number'}
                                                value={payloadToSend.cavityDutyCycle}
                                                error={errors.cavityDutyCycle}
                                                onChange={(e)=>handlePayloadToSend('cavityDutyCycle', e.target.value)}
                                                style={{width:'90px', marginLeft:'10px'}}
                                                endAdornment={'%'}
                                                disabled={inputDisabled || payloadToSend.selCavityCtrl === "1"}
                                            />
                                        </TableCell>
                                    </>
                                    ) : (
                                    <>
                                        <TableCell align="right">
                                            <Typography component='div' >
                                                <FormattedMessage 
                                                    id='getp1MiscDataTranslatedText[7].ref.sensor'
                                                    defaultMessage='Reference Sensor'
                                                />
                                            </Typography>
                                        </TableCell>
                                        <TableCell align="left">
                                            {
                                                mapValueToLabel.pileTemps.length !== 0 &&
                                                    <Agristar2InputFieldSelect 
                                                        value={payloadToSend.selCavityCtrlSensor}
                                                        options={mapValueToLabel.pileTemps}
                                                        error={errors.selCavityCtrlSensor}
                                                        onChange={(e)=>handlePayloadToSend('selCavityCtrlSensor', e.target.value)}
                                                        disabled={inputDisabled || payloadToSend.selCavityCtrl === "1"}
                                                        style={{marginTop:'-.2rem'}}
                                                    />
                                            }
                                        </TableCell>
                                    </>
                                    )
                                }
                                </TableRow>
                            </>
                            }
                            <TableRow>
                                <TableCell align="right">
                                    <Typography component='div' >
                                        <FormattedMessage 
                                            id='p1Misc[8].keyboard.preference'
                                            defaultMessage='System Keyboard Preference:'
                                        />
                                    </Typography>
                                </TableCell>
                                <TableCell align="left">
                                    <Agristar2InputFieldSelect 
                                        value={payloadToSend.kbPref}
                                        options={mapValueToLabel.kbPref}
                                        error={errors.kbPref}
                                        onChange={(e)=>handlePayloadToSend('kbPref', e.target.value)}
                                        disabled={inputDisabled}
                                        style={{marginTop:'-.2rem'}}
                                    />
                                </TableCell>
                            </TableRow>
                        </TableBody>
                    </Table>
                </TableContainer>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'center'}}>
                    <ButtonSave 
                        onClick={()=>submitPayloadToSend()}
                        disabled={!saveEnabled}
                        style={{margin:'5px auto'}}
                    />
                </div>
            </div>
        </SaveComponent>
    )
}
export default Agristar2FormP1Misc;