import { FormControl, FormHelperText, InputAdornment, makeStyles, MenuItem, Select, TextField } from "@material-ui/core"

const useStyles = makeStyles((theme)=>({
    textCenter:{
        '& .MuiInputBase-input':{
            textAlign:'center'
        }
    }
}))

const Agristar2InputField = (props) => {
    let { endAdornment, label, disabled, readOnly, value, onChange } = props;
    let { type, error } = props;
    const style = props.style
    const hasError = error !== undefined && error !== '';

    // Ensure value is never undefined to prevent uncontrolled -> controlled warning
    const safeValue = value === undefined || value === null ? '' : value;

    const classes = useStyles()

    return(
        <TextField 
            type={type}
            value={safeValue}
            onChange={onChange}
            error={hasError}
            helperText={error}
            disabled={disabled}
            label={label}
            size='small'
            style={{maxWidth:'100px', textAlign:'center', ...style}}
            className={classes.textCenter}
            InputProps={
                endAdornment ? 
                    { 
                        endAdornment: <InputAdornment style={{marginTop:'-.3rem'}} position='end'>{endAdornment}</InputAdornment>,
                        readOnly,
                    } 
                    : 
                    { readOnly }
            }
        />
    )
}
export default Agristar2InputField

export const Agristar2InputFieldSelect = (props) => {
    // normalize to string so Select finds matching option regardless of numeric/string mix
    const value = props.value === undefined || props.value === null ? '' : String(props.value)
    let options = props.options  // {'value/key':'label with meaning'}

    let disabled = props.disabled
    let onChange = props.onChange

    let error = props.error
    const style = props.style

    const classes = useStyles()

    return(
        <FormControl disabled={disabled} style={{...props.controlStyle}}>
            <Select 
                onChange={onChange}
                value={value}
                className={classes.textCenter}
                style={{paddingLeft: '5px', ...style}}
            >
                {
                    Object.entries(options)?.map(([key, value]) =>
                        value === '' ? null :
                        <MenuItem value={String(key)} key={key} >
                            {value}
                        </MenuItem>    
                    )
                }
            </Select>
            {
                props.label &&
                <FormHelperText>{props.label}</FormHelperText>
            }
            {
                error &&
                <FormHelperText error>{error}</FormHelperText>
            }
        </FormControl>
    )
}