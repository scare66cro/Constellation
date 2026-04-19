// https://redux-toolkit.js.org/api/configureStore#basic-example

import { createReducer } from '@reduxjs/toolkit'

import {
  appError, appErrorDismiss,
  loginStart, loginSuccess, loginFailed,
  logoutStart, logoutSuccess, logoutFailed,
  loadAccountFailed,
  changePasswordStart, changePasswordSuccess, changePasswordFailed,
  removeAction,
  changeActionStatus,
  msLogin, msLogout,
} from './actions'

const initialState = { 
  _loggedIn: false, //is the user authenticated (true, false)
  _status: "idle", //is there a change happening to the account? (idle, pending, success, failed)
  _msg: undefined,   //is there a message to be displayed about this account 
                //...(login failed, acct update failed, not authorized perform action on account...)
  _appError: undefined, //global errors can get loaded in here.  These must always be resolved. Things like network errors get thrown here
  _actions: {
    // 'id'{
    //    _status: 'pending','success','failed',
    //    requestPayload: {}
    //    responseError:{
    //        detail:[]
    //        field1:[]
    //    }
    // }
  }
}

const accountReducer = createReducer(initialState, (builder) => {
  builder
    // -----------APP ERROR-------------------
    .addCase(appError, (state, action) => {
      state._appError = action.payload
    })
    .addCase(appErrorDismiss, (state, action) => {
      state._appError = undefined
    })
    // -----------_ACTIONS QUEUE-------------
    .addCase(removeAction, (state, action) => {
      state._actions[action.payload.name] = undefined
    })
    .addCase(changeActionStatus, (state, action) => {
      state._actions[action.payload.name] = {
        ...state._actions[action.payload.name],
        _status:action.payload.status
      }
    })
    // -----------LOGIN----------------------------
    .addCase(msLogin, (state, action) => {
      return {
        ...state,
        _msLoggedIn: true,
        account: action.payload,
      };
    })
    .addCase(msLogout, (state, action) => {
      window.location.replace('/storage-app/');
    })
    .addCase(loginStart, (state, action) => {
      state._status = 'pending'
      // state._loggedIn = false
    })
    .addCase(loginSuccess, (state, action) => {
      return {...state, 
        _status: 'idle', //no need for success feedback besides displaying app
        _loggedIn: true,
        account: action.payload 
      }
    })
    .addCase(loginFailed, (state, action) => {
      console.log("login failed....")
      state._loggedIn = false
      state._status = "failed" //when failed check message
      state._msg = action.payload || undefined //should provide a translation message id
    })
    // ---------LOGOUT-------------------
    .addCase(logoutStart, (state, action) => {
      state._status = 'pending'
    })
    .addCase(logoutSuccess, (state, action) => {
      // this refreshes the browser & resets redux. 
      // ...DOES NOT delete stored data like cookies or localstorage
      window.location.replace('/storage-app/')
    })
    .addCase(logoutFailed, (state, action) => {
      console.log("logout failed....")
    })
    .addCase(loadAccountFailed, (state, action) => {
      state._status = 'idle'
    })
    // -------------CHANGE PASSWORD----------------
    .addCase(changePasswordStart, (state, action) => {
      state._actions['changePassword'] = {
        _status:'pending',
        requestPayload: action.payload
      }
    })
    .addCase(changePasswordSuccess, (state, action) => {
      state._actions['changePassword'] = {
        ...state._actions['changePassword'],
        _status:'success',
      }
    })
    .addCase(changePasswordFailed, (state, action) => {
      state._actions['changePassword'] = {
        ...state._actions['changePassword'],
        _status:'failed',
        responseError: action.payload?.new_password_confirm?.join(' ')
      }
    })
    
})

export default accountReducer