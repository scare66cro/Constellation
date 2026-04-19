import Agristar2InputField from './Agristar2InputFields'
import ButtonSave from '../../../common/ButtonSave'
import { Typography } from "@material-ui/core"
import useAS2FormP1PlenumTempDev from "../hooks/AS2FormP1TempDev"
import SaveComponent from './SaveComponent'
import { FormattedMessage } from 'react-intl';

const Agristar2FormPlenumTempDev = (props) => {

    const {displayData, isAuthorized, handlePayload, errors, saving, submit, cureMode} = useAS2FormP1PlenumTempDev()

    const saveEnabled = () => {
        if(errors.PlenumTempSet === true || isAuthorized === false){
            return true
        }
    }

    return(
        <SaveComponent saving={saving?.p1PlenTempDev?.status}>
            <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle', padding:'0px 5%'}}>
                { cureMode
                ?
                    <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between'}}>
                        <Typography align='justify' component='div' >
                            <FormattedMessage 
                                id='p1PlenTempDevDynTranslatdText[0].msg1'
                                defaultMessage='System will alarm if'
                            />
                            &nbsp;
                            <FormattedMessage 
                                id='p1PlenTempDevDynTranslatdText[1].msg2'
                                defaultMessage='plenum temperature is below'
                            />
                            &nbsp;
                            <Agristar2InputField 
                                type={'number'}
                                value={displayData.CureTempLowLimit || ''}
                                onChange={(e)=>handlePayload('CureTempLowLimit', e.target.value)}
                                error={errors.CureTempLowLimit}
                                endAdornment={'\u00B0'}
                                disabled={!isAuthorized}
                            />
                            &nbsp;
                            <FormattedMessage 
                                id='p1PlenTempDevDynTranslatdText[2].msg3'
                                defaultMessage='for'
                            />
                            &nbsp;
                            <Agristar2InputField 
                                type={'number'}
                                value={displayData.AlarmMinLow || ''}
                                onChange={(e)=>handlePayload('AlarmMinLow', e.target.value)}
                                error={errors.AlarmMinLow}
                                disabled={!isAuthorized}
                            />
                            &nbsp;
                            <FormattedMessage 
                                id='p1PlenTempDevDynTranslatdText[3].msg4'
                                defaultMessage='minutes or'
                            />
                            &nbsp;
                            <FormattedMessage 
                                id='p1PlenTempDevDynTranslatdText[4].msg5'
                                defaultMessage='plenum temperature is above'
                            />
                            &nbsp;
                            <Agristar2InputField 
                                type={'number'}
                                value={displayData.CureTempHighLimit || ''}
                                onChange={(e)=>handlePayload('CureTempHighLimit', e.target.value)}
                                error={errors.CureTempHighLimit}
                                endAdornment={'\u00B0'}
                                disabled={!isAuthorized}
                            />
                            &nbsp;
                            <FormattedMessage 
                                id='p1PlenTempDevDynTranslatdText[5].msg6'
                                defaultMessage='for'
                            />
                            &nbsp;
                            <Agristar2InputField 
                                type={'number'}
                                value={displayData.AlarmMinHigh || ''}
                                onChange={(e)=>handlePayload('AlarmMinHigh', e.target.value)}
                                error={errors.AlarmMinHigh}
                                disabled={!isAuthorized}
                            />
                            &nbsp;
                            <FormattedMessage 
                                id='p1PlenTempDevDynTranslatdText[6].msg7'
                                defaultMessage='minutes.'
                            />
                        </Typography>
                    </div>
                :
                    <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between'}}>
                        <Typography align='justify' component='div' >
                            <FormattedMessage 
                                id='p1PlenTempDev[1].msg1'
                                defaultMessage='System will alarm if'
                            />
                            &nbsp;
                            <FormattedMessage 
                                id='p1PlenTempDev[2].msg2'
                                defaultMessage='plenum temperature is'
                            />
                            &nbsp;
                            <Agristar2InputField 
                                type={'number'}
                                value={displayData.AlarmTempLow || ''}
                                onChange={(e)=>handlePayload('AlarmTempLow', e.target.value)}
                                error={errors.AlarmTempLow}
                                endAdornment={'\u00B0'}
                                disabled={!isAuthorized}
                            />
                            &nbsp;
                            <FormattedMessage 
                                id='p1PlenTempDev[3].msg3'
                                defaultMessage='below plenum setpoint for'
                            />
                            &nbsp;
                            <Agristar2InputField 
                                type={'number'}
                                value={displayData.AlarmMinLow || ''}
                                onChange={(e)=>handlePayload('AlarmMinLow', e.target.value)}
                                error={errors.AlarmMinLow}
                                disabled={!isAuthorized}
                            />
                            &nbsp;
                            <FormattedMessage 
                                id='p1PlenTempDev[4].msg4'
                                defaultMessage='minutes or'
                            />
                            &nbsp;
                            <FormattedMessage 
                                id='p1PlenTempDev[5].msg5'
                                defaultMessage='plenum temperature is'
                            />
                            &nbsp;
                            <Agristar2InputField 
                                type={'number'}
                                value={displayData.AlarmTempHigh || ''}
                                onChange={(e)=>handlePayload('AlarmTempHigh', e.target.value)}
                                error={errors.AlarmTempHigh}
                                endAdornment={'\u00B0'}
                                disabled={!isAuthorized}
                            />
                            &nbsp;
                            <FormattedMessage 
                                id='p1PlenTempDev[6].msg6'
                                defaultMessage='above plenum setpoint for'
                            />
                            &nbsp;
                            <Agristar2InputField 
                                type={'number'}
                                value={displayData.AlarmMinHigh || ''}
                                onChange={(e)=>handlePayload('AlarmMinHigh', e.target.value)}
                                error={errors.AlarmMinHigh}
                                disabled={!isAuthorized}
                            />
                            &nbsp;
                            <FormattedMessage 
                                id='p1PlenTempDev[7].msg7'
                                defaultMessage='minutes.'
                            />
                        </Typography>
                    </div>
                }
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'center'}}>
                    <ButtonSave 
                        onClick={()=>submit()}
                        disabled={saveEnabled()}
                        style={{margin:'5px auto'}}
                    />
                </div>
            </div>
        </SaveComponent>        
    )
}
export default Agristar2FormPlenumTempDev;