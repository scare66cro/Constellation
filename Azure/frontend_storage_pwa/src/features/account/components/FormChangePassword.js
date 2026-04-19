import { Typography } from "@material-ui/core"
import { FormattedMessage } from "react-intl"
import ButtonSave from "../../../common/ButtonSave"
import StatusPopup from "../../../common/StatusPopup"
import TextFieldBase from "../../../common/TextFieldBase"
import useFormChangePassword from "../hooks/FormChangePassword"

const FormChangePassword = (props) => {
    const {
        formStatus,
        dismissStatus,
        form,
        handleCurrentPassword,
        handleNewPassword,
        handleNewPasswordConfirm,
        handleSubmit,
        responseError,
        errors
    } = useFormChangePassword()

    return(
        <>
            <StatusPopup 
                status={formStatus}
                message={responseError}
                onClick={()=>dismissStatus()}
            />

            <Typography variant='caption' style={{color:'darkred', marginTop:'-15px'}}>
            - <FormattedMessage 
                id='form-changepassword-instruction1'
                defaultMessage='Password must be minimum of 9 characters'
            /> <br/>
            - <FormattedMessage 
                id='form-changepassword-instruction2'
                defaultMessage='Do not use attributes of your profile'
            /> <br/>
            - <FormattedMessage 
                id='form-changepassword-instruction3'
                defaultMessage='Do not use common or numeric only passwords'
            />
            </Typography>
            <TextFieldBase 
                style={{marginTop:'8px', minWidth: '51%'}}
                type={'password'}
                label={
                    <FormattedMessage
                        id='form-changePassword-field-currentpassword'
                        defaultMessage='Current Password'
                    />
                }
                value={form.current_password}
                onChange={(e)=>handleCurrentPassword(e.target.value)}
                error={false}
            />
            <TextFieldBase 
                style={{marginTop:'8px', minWidth: '51%'}}
                type={'password'}
                label={
                    <FormattedMessage
                        id='form-changePassword-field-newpassword'
                        defaultMessage='New Password'
                    />
                }
                value={form.new_password}
                onChange={(e)=>handleNewPassword(e.target.value)}
                error={errors.new_password_confirm !== undefined}
            />
            <TextFieldBase 
                style={{marginTop:'8px', minWidth: '51%'}}
                type={'password'}
                label={
                    <FormattedMessage
                        id='form-changePassword-field-newpasswordConfirm'
                        defaultMessage='Confirm New Password'
                    />
                }
                value={form.new_password_confirm}
                onChange={(e)=>handleNewPasswordConfirm(e.target.value)}
                error={errors.new_password_confirm !== undefined}
            />
            <ButtonSave 
                style={{margin:'8px 0px', minWidth:'51%'}}
                onClick={()=>handleSubmit()}
            />
        </>
    )
}
export default FormChangePassword