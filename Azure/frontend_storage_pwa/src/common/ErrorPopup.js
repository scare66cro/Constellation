import React from 'react'
import { _appError }  from '../features/account/selectors'
import { useDispatch, useSelector } from 'react-redux'
import { appErrorDismiss } from '../features/account/actions'

import { useIntl } from 'react-intl'
// import { getTranslationObjById } from '../../../utilities/translationObjects'
import { getTranslationObjById } from '../utilities/translationObjects'


    // to use this error popup create a translatable object in the /utilities/translationObjects.js
    // then pass its 'id' to the appError action in /account/actions.js

const ErrorPopup = () => {
    const errorTranslationID = useSelector(_appError)
    const dispatch = useDispatch()
    const intl = useIntl()

    React.useEffect(()=>{
        if(errorTranslationID !== undefined){
            window.alert(
                intl.formatMessage(getTranslationObjById(errorTranslationID) || 'missing translation id in translationIdMap') 
            )
            dispatch(appErrorDismiss())
        }
    }, [errorTranslationID, dispatch, intl])
    
    return(
        <></>
    )
}
export default ErrorPopup

