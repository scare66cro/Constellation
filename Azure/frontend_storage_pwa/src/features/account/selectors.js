import { createSelector } from '@reduxjs/toolkit'

import {loginCredentialsFailed} from '../../utilities/translationObjects'

// export const count = createSelector(state => state.account.count, num => num)
const selectAccount = state => state.account?.account
export const _appError = createSelector(state => state.account._appError, id => id)
export const _loggedIn = createSelector(state => state.account._loggedIn, isLoggedIn => isLoggedIn);

const _status = state=> state.account._status
export const _statusSelector = createSelector(state=> _status(state), status => status)

const _msg = state => state.account._msg
export const _msgSelector = createSelector(state => _msg(state), message_id => message_id)

// checks if the user entered the wrong credentials
//_status can be failed for multiple reasons, _msg always says why. If _status is not failed, _msg doesn't matter
export const _loginFailedMSG = createSelector(state => {   
    const status = _status(state)
    const msg = _msg(state)
    if( status === 'failed' && msg === loginCredentialsFailed.id){
        return msg
    }
    return
}, loginMSG => loginMSG) // this format is useless oops... need to change so createSelector can tell if inputs changed

// --------------SELECT _ACTIONS QUEUE---------------   
export const selectAccountActionsQueue = (state) => state.account?._actions

// --------------SELECT USER DATA--------------------
export const selectUserId = state => selectAccount(state)?.id;
export const selectUser = state => selectAccount(state);

// --------------EXTRACT USER PROFILE-----------------
export const selectUserProfile = (state) => {
    const account = selectAccount(state)
    return{
        username: account?.username,
        email: account?.email,
        first_name: account?.first_name,
        last_name: account?.last_name,
        organization_id: account?.organization_id
    }
}