import { FormattedMessage } from "react-intl";
import Agristar2InputField, { Agristar2InputFieldSelect } from './Agristar2InputFields'

const ModeFailure = (props) => {
    return (
        <>
            <FormattedMessage 
                id='p2FailuresSetup[2].mode'
                defaultMessage='Mode:'
            />

            <Agristar2InputFieldSelect 
                value={props.payloadToSend[props.mode]}
                options={props.modes}
                error={props.error?.[props.mode]}
                onChange={(e)=>props.handlePayloadToSend(props.mode, e.target.value)}
                disabled={!props.isEnabled}
                style={{marginTop:'-.2rem', marginLeft:'10px', paddingLeft:'5px'}}
            />

            <FormattedMessage 
                id='p2FailuresSetup[4].timer'
                defaultMessage='Timer:'
            />

            <Agristar2InputField 
                type={'number'}
                value={props.payloadToSend[props.timer] || ''}
                error={props.error?.[props.timer]}
                onChange={(e)=>props.handlePayloadToSend(props.timer, e.target.value)}
                disabled={!props.isEnabled}
                style={{ maxWidth: '50px' }}
            />

            {
                props.showMinutes &&
                <FormattedMessage 
                    id='p2FailuresSetup[5].minutes'
                    defaultMessage='Minutes'
                />
            }
            {
                props.children
            }
        </>
    );
}

export default ModeFailure;