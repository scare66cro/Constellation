import { UnitsComponent } from "../../../utilities/appUnitsOfMeasurements"
import Agristar2InputField, { Agristar2InputFieldSelect } from './Agristar2InputFields'
import ButtonSave from '../../../common/ButtonSave'
import { useAS2FormP1Plenum } from "../hooks/AS2FormP1Plenum "
import { Typography, FormControl, FormControlLabel, FormHelperText, Checkbox } from "@material-ui/core"
import { FormattedMessage } from "react-intl"
import SaveComponent from "./SaveComponent"

const Agristar2FormPlenumSetpoints = (props) => {

    const {
        displayData, isAuthorized, handlePayloadToSend, onionMode,
        beeMode, errors, submit, saving, mapValueToLabel,
        cureSwitchOn, notRemoteOff, burnerMode,
    } = useAS2FormP1Plenum()

    return(
        <SaveComponent saving={saving?.p1Plenum?.status}>
            <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle'}}>
                {
                onionMode && cureSwitchOn && notRemoteOff
                ?
                    burnerMode !== '1'
                    ?
                        <>
                            <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between', maxWidth:'350px'}}>
                                <Typography>
                                    <FormattedMessage
                                        id='p1PlenumDynTranslatedText[0].burnerSetPoint'
                                        defaultMessage='Plenum Temperature Setpoint for Burner Cure'
                                    />
                                </Typography>
                                <Agristar2InputField
                                    type={'number'}
                                    value={displayData.BurnerTempSet}
                                    onChange={(e) => handlePayloadToSend("BurnerTempSet", e.target.value)}
                                    error={errors.BurnerTempSet}
                                    endAdornment={<UnitsComponent typeOfData={'temperature'} />}
                                    disabled={!isAuthorized || burnerMode === '0'}
                                />
                            </div>
                            <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between', maxWidth:'350px'}}>
                                <Typography>
                                    <FormattedMessage
                                        id='p1PlenumDynTranslatedText[1].burnerThreshold'
                                        defaultMessage='Burner Output Threshold for Maximum Cure'
                                    />
                                </Typography>
                                <Agristar2InputField
                                    type={'number'}
                                    value={displayData.BurnerThreshold}
                                    onChange={(e) => handlePayloadToSend("BurnerThreshold", e.target.value)}
                                    error={errors.BurnerThreshold}
                                    endAdornment='%'
                                    disabled={!isAuthorized || burnerMode !== '3'}
                                />
                            </div>
                        </>
                    :
                        <>
                            <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between'}}>
                                <Typography>
                                    <FormattedMessage
                                        id='p1PlenumDynTranslatedText[2].manualmode'
                                        defaultMessage='In manual mode burner will turn off if'
                                    />
                                    &nbsp;
                                    <FormattedMessage
                                        id='p1PlenumDynTranslatedText[3].plenumincreases'
                                        defaultMessage='plenum temperature increases to'
                                    />
                                </Typography>
                                &nbsp;
                                <Agristar2InputField
                                    type={'number'}
                                    value={displayData.BurnerManualMax}
                                    onChange={(e) => handlePayloadToSend("BurnerManualMax", e.target.value)}
                                    error={errors.BurnerManualMax}
                                    endAdornment={<UnitsComponent typeOfData={'temperature'} />}
                                    disabled={!isAuthorized}
                                />
                            </div>
                            <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between'}}>
                                <Typography>
                                    <FormattedMessage
                                        id='p1PlenumDynTranslatedText[4].burnerRestart'
                                        defaultMessage='and the burner will restart when'
                                    />
                                    &nbsp;
                                    <FormattedMessage
                                        id='p1PlenumDynTranslatedText[5].plenumdecreses'
                                        defaultMessage='plenum temperature decreases to'
                                    />
                                </Typography>
                                &nbsp;
                                <Agristar2InputField
                                    type={'number'}
                                    value={displayData.BurnerManualRestart}
                                    onChange={(e) => handlePayloadToSend("BurnerManualRestart", e.target.value)}
                                    error={errors.BurnerManualRestart}
                                    endAdornment={<UnitsComponent typeOfData={'temperature'} />}
                                    disabled={!isAuthorized}
                                />
                            </div>
                        </>
                : 
                    <>
                        <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between', maxWidth:'350px'}}>
                            <Typography >
                                <FormattedMessage 
                                    id='p1Plenum[1].temperature'
                                    defaultMessage='Temperature'
                                    description='Plenum Temperature Setpoint:'
                                />
                            </Typography>
                            <Agristar2InputField 
                                type={'number'}
                                value={displayData.PlenumTempSet || ''}
                                onChange={(e)=>handlePayloadToSend("PlenumTempSet", e.target.value)}
                                error={errors.PlenumTempSet}
                                endAdornment={<UnitsComponent typeOfData={'temperature'} />}
                                disabled={!isAuthorized}
                            />
                        </div>
                        <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between', maxWidth:'350px'}}>
                            { (beeMode || onionMode) && 
                                <Agristar2InputFieldSelect
                                    value={displayData.PlenumHumidRef || '0'}
                                    options={mapValueToLabel.PlenumHumidRef}
                                    onChange={(e)=>handlePayloadToSend("PlenumHumidRef", e.target.value)}
                                    error={errors.PlenumHumidRef}
                                    disabled={!isAuthorized}
                                    style={{marginTop:'-.2rem'}}
                                />          
                            }
                            <Typography>
                                <FormattedMessage 
                                    id='p1Plenum[2].humidity'
                                    defaultMessage='Humidity'
                                    description='Plenum Humidity Setpoint:'
                                />
                            </Typography>
                            <Agristar2InputField 
                                type={'number'}
                                value={displayData.PlenumHumidSet || ''}
                                onChange={(e)=>handlePayloadToSend("PlenumHumidSet", e.target.value)}
                                error={errors.PlenumHumidSet}
                                endAdornment={<UnitsComponent typeOfData={'relativeHumidity'} />}
                                disabled={!isAuthorized}
                            />
                        </div>
                        {
                            beeMode && 
                            <>
                                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between', maxWidth:'350px'}}>
                                    <Typography>
                                        <FormattedMessage 
                                            id='p1Plenum[3].temperature2'
                                            defaultMessage='Refrigeration Temperature'
                                            description='Plenum Refrigeration Temperature Setpoint:'
                                        />
                                    </Typography>
                                    <Agristar2InputField 
                                        type={'number'}
                                        value={displayData.PlenumTempSet2 || ''}
                                        onChange={(e)=>handlePayloadToSend("PlenumTempSet2", e.target.value)}
                                        error={errors.PlenumTempSet2}
                                        endAdornment={<UnitsComponent typeOfData={'temperature'} />}
                                        disabled={!isAuthorized}
                                    />
                                </div>
                                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'left', maxWidth:'250px'}}>
                                    <Typography variant='h6'>
                                        <FormattedMessage
                                            id='p1Plenum[4].dehumidification'
                                            defaultMessage='Dehumidification'
                                        />
                                    </Typography>
                                </div>
                                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between', maxWidth:'350px'}}>
                                    <FormControl
                                        error={errors.UseRefrigerationForDehumidifier !== undefined && errors.UseRefrigerationForDehumidifier !== ''}
                                    >
                                        <FormHelperText>{errors.UseRefrigerationForDehumidifier}</FormHelperText>
                                        <FormControlLabel
                                            control={
                                                <Checkbox
                                                    checked={displayData.UseRefrigerationForDehumidifier === '1' ? true : false}
                                                    onChange={(e) => handlePayloadToSend("UseRefrigerationForDehumidifier", e.target.checked)}
                                                    color='primary'
                                                    disabled={!isAuthorized}
                                                />
                                            }
                                            label={
                                                <FormattedMessage
                                                    id='p1Plenum[5].useRefrig'
                                                    defaultMessage='Use Refrigeration with Dehumidification.'
                                                />
                                            }
                                        />
                                    </FormControl>
                                </div>
                                <div style={{margin:'0px auto', width:'100%', display:'flex', justifyContent:'space-between', maxWidth:'350px'}}>
                                    <Typography style={{fontWeight:'bolder'}} >
                                        <FormattedMessage 
                                            id='p1Plenum[6].auxSwitch'
                                            defaultMessage='Dehumidifier Switch:'
                                        />
                                    </Typography>
                                    <Agristar2InputFieldSelect
                                        value={displayData.AuxSwitchForDehumidifier || ''}
                                        options={mapValueToLabel.AuxSwitchForDehumidifier}
                                        onChange={(e)=>handlePayloadToSend("AuxSwitchForDehumidifier", e.target.value)}
                                        error={errors.AuxSwitchForDehumidifier}
                                        disabled={!isAuthorized}
                                        style={{marginTop:'-.2rem'}}
                                    />          
                                </div>
                            </>
                        }
                    </>
                }
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'center'}}>
                    <ButtonSave 
                        onClick={()=>submit()}
                        disabled={!isAuthorized}
                        style={{margin:'5px auto'}}
                    />
                </div>
            </div>
        </SaveComponent>            
    )
}
export default Agristar2FormPlenumSetpoints