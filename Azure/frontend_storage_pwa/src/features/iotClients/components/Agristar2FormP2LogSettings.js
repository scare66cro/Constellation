import { Typography } from "@material-ui/core"
import { FormattedMessage } from "react-intl"
import ButtonSave from "../../../common/ButtonSave"
import useAS2FormP2LogSettings from "../hooks/AS2FormP2LogSettings"
import Agristar2InputField,{ Agristar2InputFieldSelect } from "./Agristar2InputFields"
import SaveComponent from "./SaveComponent"

const Agristar2FormP2LogSettings = (props) => {
    // -------------------HOOKS--------------------
    const {
        isAuthorized,
        payloadToSend,
        mapValueToLabel,
        saving,
        errors,
        initializeSDCard,
        clearHistoryLog,
        clearActivityLog,
        handlePayloadToSend,
        submitPayloadToSend,
    } = useAS2FormP2LogSettings()

    const saveEnabled = isAuthorized

    return(
        <SaveComponent saving={saving?.p2LogSettings?.status || saving?.button2?.status}>
            <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle', padding:'0px 5%'}}>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'center'}}>
                    <Typography align='justify' component='div' >
                        <FormattedMessage 
                            id='p2LogSettings[1].history.log'
                            defaultMessage='History Log Settings'
                        />
                    </Typography>
                </div>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between'}}>
                    <Typography align='justify' component='div' >
                        <FormattedMessage 
                            id='p2LogSettings[2].record.taken'
                            defaultMessage='A record will be taken every'
                        />
                        <Agristar2InputField 
                            type={'number'}
                            value={payloadToSend.recInterval || ''}
                            error={errors.recInterval}
                            onChange={(e)=>handlePayloadToSend('recInterval', e.target.value)}
                            disabled={!isAuthorized}
                        />
                        <FormattedMessage 
                            id='p2LogSettings[3].minutes'
                            defaultMessage='minutes'
                        />
                    </Typography>
                </div>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between'}}>
                    <Typography align='justify' component='div' >
                        <FormattedMessage 
                            id='p2LogSettings[4].overwrite'
                            defaultMessage='Overwrite old records if necessary:'
                        />

                        <Agristar2InputFieldSelect
                            value={payloadToSend.sdWrap || ''}
                            options={mapValueToLabel.sdWrap}
                            error={errors.sdWrap}
                            onChange={(e)=>handlePayloadToSend('sdWrap', e.target.value)}
                            // error={errors.PlenumTempSet}
                            disabled={!isAuthorized}
                            style={{marginTop:'-.2rem'}}
                        />
                    </Typography>
                </div>

                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'center'}}>
                    <ButtonSave 
                        onClick={()=>initializeSDCard()}
                        disabled={!saveEnabled}
                        style={{margin:'5px auto'}}
                        label={
                            <FormattedMessage 
                                id='buttonsTranslatedText[44].initialize'
                                defaultMessage='Initialize SD Card'
                            />
                        }
                    />
                </div>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'center'}}>
                    <ButtonSave 
                        onClick={()=>clearHistoryLog()}
                        disabled={!saveEnabled}
                        style={{margin:'5px auto'}}
                        label={
                            <FormattedMessage 
                                id='buttonsTranslatedText[45].clear.log'
                                defaultMessage='Clear History Log'
                            />
                        }
                    />
                </div>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'center'}}>
                    <ButtonSave 
                        onClick={()=>clearActivityLog()}
                        disabled={!saveEnabled}
                        style={{margin:'5px auto'}}
                        label={
                            <FormattedMessage 
                                id='buttonsTranslatedText[46].clear.activity'
                                defaultMessage='Clear Activity Log'
                            />
                        }
                    />
                </div>
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
export default Agristar2FormP2LogSettings;