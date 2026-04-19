import { createSelector } from '@reduxjs/toolkit'

export const _statusSelector = createSelector(state => state.events._status, status => status)

export const _createSiteEventStatusSelector = createSelector(
    state => state.events._createSiteEventStatus, 
    status => status
)

export const eventsListSelector = createSelector(state => state.events.events, events => events)


// gets a list of categories that have been used in past events loaded in app at current moment
export const eventCategoryListSelector = createSelector(
    state => state.events.events, 
    events => events.reduce((unique, e) => {
        if(unique.includes(e.category) || e.category.toLowerCase() === 'warning'){
            return unique
        }
        return [...unique, e.category]
    },[])
)