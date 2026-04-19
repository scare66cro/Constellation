import Agristar2InputField from './Agristar2InputFields'
import ButtonSave from '../../../common/ButtonSave';
import useAS2FormP2FreshAir from '../hooks/AS2FormP2FreshAir';
import { Typography } from "@material-ui/core";
import { FormattedMessage } from "react-intl";
import Pidu from './Pidu';
import SaveComponent from './SaveComponent';

const Agristar2FormP2FreshAirSetup = (props) => {
    const {
        payloadToSend,
        isAuthorized,
        saving,
        errors,
        handlePayloadToSend,
        submitPayloadToSend,
    } = useAS2FormP2FreshAir();

    const saveEnabled = () => {
        if (isAuthorized === false){
            return true
        }
    }

    return(
        <SaveComponent saving={saving?.p2FreshAirSetup?.status}>
            <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle'}}>
                <Pidu
                    label={
                        <FormattedMessage
                            id='p2FreshAirSetup[1].pidu'
                            defaultMessage='PIDU Values'
                        />
                    }
                    P={payloadToSend.PAirValue}
                    I={payloadToSend.IAirValue}
                    D={payloadToSend.DAirValue}
                    U={payloadToSend.UAirValue}
                    isAuthorized={isAuthorized}
                    error={{errors, p: 'PAirValue', i: 'IAirValue', d: 'DAirValue', u: 'UAirValue'}}
                    handlePayloadToSend={handlePayloadToSend}/>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between', maxWidth:'350px'}}>
                    <Typography >
                        <FormattedMessage 
                            id='p2FreshAirSetup[2].actuator.time'
                            defaultMessage='Total Open Time For All Actuator Stages'
                        />
                    </Typography>
                    <Agristar2InputField 
                        type={'number'}
                        value={payloadToSend.ActuatorTimes || ''}
                        error={errors.ActuatorTimes}
                        onChange={(e)=>handlePayloadToSend('ActuatorTimes', e.target.value)}
                        disabled={!isAuthorized}
                    />
                    <Typography>
                        <FormattedMessage 
                            id='p2FreshAirSetup[3].seconds'
                            defaultMessage='Seconds'
                        />
                    </Typography>
                </div>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between', maxWidth:'350px'}}>
                    <Typography >
                        <FormattedMessage 
                            id='p2FreshAirSetup[4].cooling.short.cycle'
                            defaultMessage='Cooling Air Short Cycle Timer'
                        />
                    </Typography>
                    <Agristar2InputField 
                        type={'number'}
                        value={payloadToSend.CoolAirCycle || ''}
                        error={errors.CoolAirCycle}
                        onChange={(e)=>handlePayloadToSend('CoolAirCycle', e.target.value)}
                        disabled={!isAuthorized}
                    />
                    <Typography>
                        <FormattedMessage 
                            id='p2FreshAirSetup[5].minutes'
                            defaultMessage='Minutes'
                        />
                    </Typography>
                </div>
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
export default Agristar2FormP2FreshAirSetup;