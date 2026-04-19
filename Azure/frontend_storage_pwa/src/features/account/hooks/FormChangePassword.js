import { useEffect, useState } from "react"
import { useDispatch, useSelector } from "react-redux"
import { changeActionStatus, changePassword } from "../actions"
import { selectAccountActionsQueue, selectUserId } from "../selectors"

const useFormChangePassword = (props) => {
    // -------------HOOKS----------------
    const dispatch = useDispatch()

    // -------------SELECTOR--------------
    const userId = useSelector(state=>selectUserId(state))
    const formStatus = useSelector(state=>selectAccountActionsQueue(state))['changePassword']?._status
    const responseError = useSelector(state=>selectAccountActionsQueue(state))['changePassword']?.responseError

    // ------------DISMISS FORM STATUS-----------
    const dismissStatus = () => {
        dispatch(changeActionStatus({
            name: 'changePassword',
            status: 'idle'
        }))
    }

    // ------------PAYLOAD TO SEND---------
    const [form, setForm] = useState({ 
        "current_password": '', 
        "new_password": '', 
        "new_password_confirm": '' 
    })

    // ------------HANDLE CHANGES-----------
    const handleCurrentPassword = (val) => {
        setForm({...form, current_password:val})
    }
    const handleNewPassword = (val) => {
        setForm({...form, new_password:val})
    }
    const handleNewPasswordConfirm = (val) => {
        setForm({...form, new_password_confirm:val})
    }

    // ---------------SUBMIT----------------
    const handleSubmit = () => {
        dispatch(changePassword(userId, form))
    }

    // ------------ERROR CHECK-------------
    const [errors, setErrors] = useState({})

    const errorNewPassword = () => {
        if(form.new_password?.length < 9 || !isNaN(form.new_password)){
            setErrors({...errors, new_password:'must be minimum length of 9 and not a number'})
        }
        else{
            setErrors({...errors, new_password: undefined})
        }
    }
    useEffect(()=>{
        errorNewPassword()
    // eslint-disable-next-line react-hooks/exhaustive-deps
    },[form.new_password])

    const errorConfirmPassword = () => {
        if(form.new_password !== form.new_password_confirm){
            setErrors({...errors, new_password_confirm: 'passwords do not match'})
        }else{
            setErrors({...errors, new_password_confirm: undefined})
        }
    }
    useEffect(()=>{
        errorConfirmPassword()
    // eslint-disable-next-line react-hooks/exhaustive-deps
    },[form.new_password_confirm])

    return{
        formStatus,
        dismissStatus,
        form,
        handleCurrentPassword,
        handleNewPassword,
        handleNewPasswordConfirm,
        handleSubmit,
        responseError,
        errors
    }
}
export default useFormChangePassword
