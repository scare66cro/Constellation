import { createAction } from '@reduxjs/toolkit'
import { axiosCatchDefaults } from '../../utilities/axiosCatchDefaults'
import axiosInstance from '../../utilities/axiosConfig'

export const loadEventsStart = createAction('loadEvents/Start')
export const loadEventsSuccess = createAction('loadEvents/Success')
export const loadEventsFailed = createAction('loadEvents/Failed')

export const loadEvents = () => {
    return (dispatch) => {
        dispatch(loadEventsStart())
        axiosInstance.get('/api/site-events/')
            .then(res => {
               dispatch(loadEventsSuccess(res.data))
            })
            .catch(err => {
                console.log('events failed....')
                axiosCatchDefaults(err, dispatch)
                dispatch(loadEventsFailed())
            })
    }
}

export const createSiteEventStart = createAction('createSiteEvent/Start')
export const createSiteEventSuccess = createAction('createSiteEvent/Success')
export const createSiteEventFailed = createAction('createSiteEvent/Failed')
export const createSiteEventReset = createAction('createSiteEvent/Reset')

export const createSiteEvent = (someVal) => {
    return (dispatch) => {
        dispatch(createSiteEventStart())
        axiosInstance.post('/api/site-events/', someVal)
            .then(res => {
                dispatch(createSiteEventSuccess(res.data))
            })
            .catch(err => {
                console.log('event post failed', err.response)
                axiosCatchDefaults(err, dispatch)
                dispatch(createSiteEventFailed())
            })
    }
}
