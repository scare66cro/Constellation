import { createReducer } from '@reduxjs/toolkit'
import { createSiteEventFailed, createSiteEventReset, createSiteEventStart, createSiteEventSuccess, loadEventsFailed, loadEventsStart, loadEventsSuccess } from './actions'

const initialState = {
    _status: "pending", // STARTS as 'pending' because it gets loaded on app start options: [idle, pending, success, failed]
    _msg: undefined, //put messages that relate to site list as a whole here
    events: undefined,

    _loadEventsStatus: undefined,
    _loadEventsMSG: undefined, // 'idle', 'pending', 'success', 'failed'
    
    _createSiteEventStatus: undefined,
    _createSiteEventMSG: undefined,
    _createSiteEventFailedPayload: undefined, //meant to contain all of the validations returned from the server
    
    // eventDraft: undefined // this is for storing the value of the draft, will be 'nav away proof'
}

const eventsReducer = createReducer(initialState, (builder) => {
    builder
        .addCase(loadEventsStart, (state, action) => {
            return {
                ...state,
                _status: 'pending'
            }
        })
        .addCase(loadEventsSuccess, (state, action) => {
            return {
                ...state,
                _status: "idle",
                events: action.payload 
            }
        })
        .addCase(loadEventsFailed, (state, action) => {
            return {
                ...state,
                _status: 'failed',
                _msg: action.payload
            }
        })

        .addCase(createSiteEventStart, (state,action) => {
            return {
                ...state,
                _createSiteEventStatus:'pending',
                _createSiteEventFailedPayload: undefined, //deletes error messages from previous post
            }
        })
        .addCase(createSiteEventSuccess, (state,action) => {
            return {
                ...state,
                events:[action.payload, ...state.events],
                _createSiteEventStatus:'success'
            }
        })
        .addCase(createSiteEventFailed, (state, action) => {
            return {
                ...state,
                _createSiteEventStatus:'failed',
                _createSiteEventMSG: action.payload
            }
        })
        // this only fires to clear loading/success/failure popup screen
        .addCase(createSiteEventReset, (state, action) => {
            let newState = state
            if(state._createSiteEventStatus !== 'pending'){
                newState = {
                    ...state,
                    _createSiteEventStatus:'idle',
                    _createSiteEventMSG: undefined
                }
            }
            return newState
        })
})

export default eventsReducer