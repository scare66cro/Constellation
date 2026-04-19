import { createAction } from '@reduxjs/toolkit'
import { axiosCatchDefaults } from '../../utilities/axiosCatchDefaults'
import axiosInstance from '../../utilities/axiosConfig'
import getCookie from '../../utilities/cookieGetter' 

import { loginCredentialsFailed } from '../../utilities/translationObjects'
import { loadCustomers } from '../customers/actions'
import { loadEvents } from '../events/actions'
import { loadIotClients } from '../iotClients/actions'
import { loadSites, loadUpgrades } from '../sites/actions'

export const increment = createAction('counter/increment')

export const appError = createAction('app/error')
export const appErrorDismiss = createAction('app/error-dismiss')

export const msLogin = createAction('app/msLogin');
export const msLogout = createAction('app/msLogout');

// ---------------REMOVE ACTION FROM _ACTIONS-------------
export const removeAction = createAction('actions/removeAction')
// ---------------CHANGE ACTION STATUS IN QUEUE
export const changeActionStatus = createAction('actions/changeActionStatus')
// ....set _status from 'error' to 'idle'

export const loadAccountFailed = createAction('loadAccount/failed')

function loadAppState (dispatch, userId) {
    // loadCorporations
    // loadDealers
    dispatch(loadCustomers())
    dispatch(loadSites())
    // loadIotClients
    dispatch(loadIotClients())
    dispatch(loadEvents())
    dispatch(loadUpgrades())
}

export const loadAccount = (done) => {
    return (dispatch) => {
        const headers = {};
        dispatch(loginStart());
        axiosInstance.get(`/login/`, { headers }) // GET request to /api/login/ returns logged in user
        .then(res => {
            dispatch(loginSuccess(res.data))
            // calls all other actions needed to get the app data downloaded and set up
            loadAppState(dispatch, res.data.id)
            const csrfToken = getCookie('csrftoken')
            axiosInstance.defaults.headers.common['X-XSRF-TOKEN'] = csrfToken;
            done();
        })
        .catch(err => {
            axiosCatchDefaults(err, dispatch)

            const resp = err.response
            if(resp === undefined){
                dispatch(loginFailed())
            }
            else if(resp.status === 401){
                dispatch(loginFailed())
            }
            done();
        });
    }
}

// ----------------LOGIN----------------------
export const loginStart = createAction('login/started')
export const loginSuccess = createAction('login/success')
export const loginFailed = createAction('login/failed')

export const login = (form) => {
    return function(dispatch){
        let headers = undefined;
        dispatch(loginStart())
        axiosInstance.post(`/login/`, form, { headers })
            .then(res => {
                dispatch(loginSuccess(res.data))
                loadAppState(dispatch, res.data.id)
                const csrfToken = getCookie('csrftoken')
                axiosInstance.defaults.headers.common['X-XSRF-TOKEN'] = csrfToken;
            })
            .catch(err => {
                // Default error handlers
                axiosCatchDefaults(err, dispatch)
                
                // actions specific to this api call
                const resp = err.response
                if(resp === undefined){
                    // dispatch(appError(networkErrorMessage.id))
                    dispatch(loginFailed())
                }
                // if 401 display message to user credentials failed
                else  if(resp.status === 401){
                    dispatch(loginFailed(loginCredentialsFailed.id))
                }
            }) 
    }
}

// ---------------LOGOUT-------------------------
export const logoutStart = createAction('logout/started')
export const logoutSuccess = createAction('logout/success')
export const logoutFailed = createAction('logout/failed')

export const logout = () => {
    return function(dispatch){
        dispatch(logoutStart())
        axiosInstance.get(`/logout/`)
        .then(async res => {
            try {
                window.location.href = '/saml/complete-logout/';
            } finally {
                dispatch(logoutSuccess())
            }
        })
        .catch(err => {
            axiosCatchDefaults(err, dispatch)
            console.log("error....", err)
            dispatch(logoutFailed(err))
        }) 
    }
}

// ---------------CHANGE PASSWORD------------------
export const changePasswordStart = createAction('changePassword/started')
export const changePasswordSuccess = createAction('changePassword/success')
export const changePasswordFailed = createAction('changePassword/failed')

export const changePassword = (id,form) => {
    console.log("Changing password!")
    return function(dispatch){
        dispatch(changePasswordStart(form))
        // reset csrf token since it is changing
        const csrfToken = getCookie('csrftoken')
        axiosInstance.defaults.headers.common['X-XSRF-TOKEN'] = csrfToken;
        axiosInstance.post(`/api/user-accounts/${id}/change_password/`, form)
        .then(res => {
            // console.log(res.data)
            dispatch(changePasswordSuccess())
            setTimeout(()=>{
                window.location.replace('/storage-app/')
            },1000)
        })
        .catch(err => {
            // axiosCatchDefaults(err, dispatch)
            console.log("error....", err.response.data)
            if (err.response.data.detail !== undefined){
                dispatch(changePasswordFailed({ new_password_confirm: [err.response.data.detail] }));
            } else {
                dispatch(changePasswordFailed(err.response.data))
            }
        }) 
    }
}
