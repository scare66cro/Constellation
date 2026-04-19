import { Button } from "@material-ui/core"
import { useIntl } from "react-intl"
import { buttonSave } from "../utilities/translationObjects"


const ButtonSave = (props) => {
    const intl = useIntl()

    const onClick = props.onClick
    const disabled = props.disabled
    const style = props.style
    const label = props.label

    return(
        <Button
            size='small' color='primary' 
            variant={'contained'} 
            style={{ marginLeft:'10px', ...style}}

            disabled={disabled}
            onClick={onClick}
        >
            {
                label ? label :
                <>
                    {intl.formatMessage(buttonSave)}
                </>
            }
        </Button>
    )
}
export default ButtonSave