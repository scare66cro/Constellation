import { Typography } from "@material-ui/core";
import Agristar2InputField from './Agristar2InputFields';

const Pidu = (props) => {
    return (
        <>
            <div style={{margin:'8px auto', width: '100%', display:'flex', textAlign: 'left'}}>
                <Typography style={{marginRight:'5px'}}>{props.label}:</Typography>
            </div>
            <div style={{margin: '8px 8px 16px 8px', display: 'flex'}}>
                <div style={{display:'flex'}}>
                    <Typography style={{marginLeft:'10px', marginRight:'10px'}}>P</Typography>
                    <Agristar2InputField
                        type={'number'}
                        value={props.P || ''}
                        error={props.error.errors[props.error.p]}
                        onChange={(e)=>props.handlePayloadToSend('P', e.target.value)}
                        disabled={!props.isAuthorized}
                    />
                    <Typography style={{marginLeft:'10px', marginRight:'10px'}}>I</Typography>
                    <Agristar2InputField
                        type={'number'}
                        value={props.I || ''}
                        error={props.error.errors[props.error.i]}
                        onChange={(e)=>props.handlePayloadToSend('I', e.target.value)}
                        disabled={!props.isAuthorized}
                    />
                    <Typography style={{marginLeft:'10px', marginRight:'10px'}}>D</Typography>
                    <Agristar2InputField
                        type={'number'}
                        value={props.D || ''}
                        error={props.error.errors[props.error.d]}
                        onChange={(e)=>props.handlePayloadToSend('D', e.target.value)}
                        disabled={!props.isAuthorized}
                    />
                    <Typography style={{marginLeft:'10px', marginRight:'10px'}}>U</Typography>
                    <Agristar2InputField
                        type={'number'}
                        value={props.U || ''}
                        error={props.error.errors[props.error.u]}
                        onChange={(e)=>props.handlePayloadToSend('U', e.target.value)}
                        disabled={!props.isAuthorized}
                    />
                </div>
            </div>
        </>
    );
}

export default Pidu;