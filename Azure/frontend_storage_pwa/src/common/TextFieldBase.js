import { TextField } from '@material-ui/core'

const TextFieldBase = (props) => {


    return(

        <TextField 
            error={props.error}
            helperText={props.helperText}
            variant={props.variant || 'outlined'}
            size={props.size || 'small'}
            label={props.label}
            fullWidth={props.fullWidth}
            type={props.type}
            inputProps={props.inputProps}
            InputLabelProps={props.InputLabelProps}
            placeholder={props.placeholder || ''}
            style={props.style}
            
            value={props.value}
            onChange={props.onChange}
            {...props}
        />
    )
}

export default TextFieldBase