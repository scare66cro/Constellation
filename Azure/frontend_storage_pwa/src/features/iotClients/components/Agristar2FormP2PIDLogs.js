import { Agristar2InputFieldSelect } from './Agristar2InputFields'
import ButtonSave from '../../../common/ButtonSave';
import useAS2FormP2PIDLogs from '../hooks/AS2FormP2PIDLogs';
import { Switch, Typography, FormControlLabel } from "@material-ui/core";
import { FormattedMessage, useIntl } from "react-intl";
import SaveComponent from './SaveComponent';
import { agristar2Refrigeration } from '../../../utilities/translationObjects';

const Agristar2FormP2PIDLogs = (props) => {
    const intl = useIntl();
    const {
        payloadToSend,
        clearPIDLog,
        isAuthorized,
        toggleLog,
        pidLogs,
        mapValueToLabel,
        saving,
        handlePayloadToSend,
        submitPayloadToSend,
    } = useAS2FormP2PIDLogs();

    const saveEnabled = () => {
        if (isAuthorized === false){
            return true
        }
    }

    return(
        <SaveComponent saving={saving?.p2PIDLogs?.status || saving?.button2?.status}>
            <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle'}}>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between', maxWidth:'250px'}}>
                    <FormControlLabel
                        control={
                            <Switch
                                checked={pidLogs.btnPIDDoorLog === '1'}
                                onChange={(e) => toggleLog('btnPIDDoorLog', e.target.checked)}
                                color='primary'
                                disabled={!isAuthorized}
                            />
                        }
                        label={
                            <FormattedMessage 
                                id='p2PIDLogs[1].fresh.air'
                                defaultMessage='Fresh Air Doors'
                            />
                        }
                    />
                </div>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between', maxWidth:'250px'}}>
                    <FormControlLabel
                        control={
                            <Switch
                                checked={pidLogs.btnPIDRefrigLog === '1'}
                                onChange={(e) => toggleLog('btnPIDRefrigLog', e.target.checked)}
                                color='primary'
                                disabled={!isAuthorized}
                            />
                        }
                        label={intl.formatMessage(agristar2Refrigeration)}
                    />
                </div>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between', maxWidth:'250px'}}>
                    <Typography >
                        <FormattedMessage 
                            id='p2PIDLogs[3].clear'
                            defaultMessage='Clear PID Log'
                        />
                    </Typography>
                    <ButtonSave
                        onClick={(e) => clearPIDLog()}
                        disabled={!isAuthorized}
                        style={{marginTop:'-.2rem'}}
                        label={
                            <FormattedMessage 
                                id='buttonsTranslatedText[35].clear'
                                defaultMessage='Clear'
                            />
                        }
                    />
                </div>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between', maxWidth:'250px'}}>
                    <Typography >
                        <FormattedMessage 
                            id='p2PIDLogs[4].overwrite'
                            defaultMessage='Overwrite oldest records if necessary'
                        />
                    </Typography>
                    <Agristar2InputFieldSelect
                        value={payloadToSend.pidWrap}
                        options={mapValueToLabel.pidWrap}
                        onChange={(e)=>handlePayloadToSend('pidWrap', e.target.value)}
                        disabled={!isAuthorized}
                        style={{marginTop:'-.2rem', minWidth:'100px'}}
                    />          
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
export default Agristar2FormP2PIDLogs;
