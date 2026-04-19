import Agristar2InputField, { Agristar2InputFieldSelect } from './Agristar2InputFields'
import ButtonSave from '../../../common/ButtonSave';
import useAS2FormP2ClimaCell from '../hooks/AS2FormP2ClimaCell';
import { Typography } from "@material-ui/core";
import { FormattedMessage } from "react-intl";
import Pidu from './Pidu';
import SaveComponent from './SaveComponent';

const Agristar2FormP2ClimaCellSetup = (props) => {
    const {
        payloadToSend,
        isAuthorized,
        mapValueToLabel,
        handlePayloadToSend,
        submitPayloadToSend,
        saving,
        errors,
    } = useAS2FormP2ClimaCell()

    const saveEnabled = () => {
        if (isAuthorized === false){
            return true
        }
    }

    return(
        <SaveComponent saving={saving?.p2Climacell?.status}>
            <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle'}}>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between', maxWidth:'250px'}}>
                    <Typography >
                        <FormattedMessage 
                            id='p2Climacell[1].efficiency'
                            defaultMessage='ClimaCell Efficiency'
                        />
                    </Typography>
                    <Agristar2InputField 
                        type={'number'}
                        value={payloadToSend.ClimacellEff || ''}
                        error={errors.ClimacellEff}
                        onChange={(e)=>handlePayloadToSend('ClimacellEff', e.target.value)}
                        disabled={!isAuthorized}
                        endAdornment='%'
                    />
                </div>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between', maxWidth:'250px'}}>
                    <Typography >
                        <FormattedMessage 
                            id='p2Climacell[2].altitude'
                            defaultMessage='Altitude'
                        />
                    </Typography>
                    <Agristar2InputField 
                        type={'number'}
                        value={payloadToSend.Altitude || ''}
                        error={errors.Altitude}
                        onChange={(e)=>handlePayloadToSend('Altitude', e.target.value)}
                        disabled={!isAuthorized}
                    />
                    <Agristar2InputFieldSelect
                        value={payloadToSend.AltType || ''}
                        options={mapValueToLabel.AltType}
                        onChange={(e)=>handlePayloadToSend('AltType', e.target.value)}
                        // error={errors.PlenumTempSet}
                        disabled={!isAuthorized}
                        style={{marginTop:'-.2rem'}}
                    />          
                </div>
                <Pidu
                    label={
                        <>
                            <FormattedMessage
                                id='p2Climacell[5].climacell.humidifiers'
                                defaultMessage='ClimaCell/Humidifier(s)'
                            />
                            <FormattedMessage
                                id='p2Climacell[6].pidu'
                                defaultMessage='PIDU Values'
                            />
                        </>
                    }
                    P={payloadToSend.PClimacellValue}
                    I={payloadToSend.IClimacellValue}
                    D={payloadToSend.DClimacellValue}
                    U={payloadToSend.UClimacellValue}
                    error={{errors, p: 'PClimacellValue', i: 'IClimacellValue', d: 'DClimacellValue', u: 'UClimacellValue'}}
                    isAuthorized={isAuthorized}
                    handlePayloadToSend={handlePayloadToSend}/>
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
export default Agristar2FormP2ClimaCellSetup;