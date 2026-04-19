import ButtonSave from '../../../common/ButtonSave'
import { useAS2FormP1Alert } from '../hooks/AS2FormP1Alert'
import { Checkbox, FormControlLabel, Typography } from "@material-ui/core"
import { FormattedMessage, useIntl } from "react-intl"
import SaveComponent from './SaveComponent'

const Alarm = (props) => {
    return (
        <>
            {
                (props.item === 'WARN_PLENTEMP1' || props.item === 'WARN_NEWBOARD') &&
                <div style={{margin:'8px auto', width:'100%', display:'flex'}}>
                    <Typography>
                        <FormattedMessage 
                            id='p1AlertSetupDynTranslatedText[0].enable'
                            defaultMessage='Enable'
                        />
                    </Typography>
                    <Typography style={{marginLeft:'100px'}}>
                        {
                            props.item === 'WARN_PLENTEMP1' &&
                            <FormattedMessage 
                                id='p1AlertSetupDynTranslatedText[1].primary'
                                defaultMessage='Primary System Alerts'
                            />
                        }
                        {
                            props.item === 'WARN_NEWBOARD' &&
                            <FormattedMessage 
                                id='p1AlertSetupDynTranslatedText[2].secondary'
                                defaultMessage='Secondary System Alerts'
                            />
                        }
                    </Typography>
                </div>
            }
            {
                <div style={{margin:'8px auto', width:'100%', display:'flex'}}>
                <FormControlLabel
                    control={
                        <Checkbox
                            checked={props.payloadToSend[props.item] === 'on' ? true : false}
                            onChange={(e) => props.handlePayloadToSend(props.item, e.target.checked)}
                            color='primary'
                            disabled={!props.isAuthorized}
                        />
                    }
                    label={props.alarm}
                />
                </div>
            }
        </>
    ); 
}

const Agristar2FormAlert = (props) => {
    const intl = useIntl();

    const {
        payloadToSend, isAuthorized,
        as2AlarmTranslations,
        keys,
        saving,
        handlePayloadToSend,
        submitPayloadToSend,
        boardType,
    } = useAS2FormP1Alert()

    return(
        <SaveComponent saving={saving?.p1AlertFrame?.status}>
            <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle', padding:'0px 5%'}}>
                {
                    keys.map((item, index) => 
                        <Alarm
                            key={index}
                            alarm={intl.formatMessage(as2AlarmTranslations[item])}
                            item={item}
                            payloadToSend={payloadToSend}
                            boardType={boardType}
                            isAuthorized={isAuthorized}
                            handlePayloadToSend={(item, val) => handlePayloadToSend(item, val)}/>
                    )
                }
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
export default Agristar2FormAlert;