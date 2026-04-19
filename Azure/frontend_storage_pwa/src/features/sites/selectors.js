import { createSelector } from '@reduxjs/toolkit'

/*
    Notes:
    selectors need to enable extreme flexibility in components so that if the shape of the data changes
    ...it only needs to be changed here and the componenents will follow suit without error.
    ...That being said, here are the types of tools every selector should provide:
    https://redux.js.org/usage/deriving-data-selectors

    1) selector: a selector is a function that returns a top level variable from state. Example:
        sites-state-object: 
            {
                currentSite: '213kldf-334-93284',
                sites: [
                    {
                        id:'213kldf-334-93284',
                        name: 'site1'
                    }
                ]
            }
        a function named 'selectCurrentSite' would return the object from 'sites' array who's id matches 'currentSite'.
        ...this function did not require an id parameter from the react-component when it called currentSiteSelector

    2) extractor: (still is a selector, just named differently to differentiate) an extractor is a function that is
            ...given an object as a parameter and is designed to be able to extract something
            ...specific from within it because it knows the shape of that object. Extractors can also be used
            ...to extract things from arrays and such by allowing them to recieve more parameters along with the object
            ...specifying what to extract from it and return.
            ...ultimately extractors prevent code very specific to data structures being sprinkled throughout ui-components
            Example: convention extractFromSitesById(sites, currentSite) returns { id:'213kldf-334-93284', name: 'site1' }
*/

const _status = state => state.sites._status
const siteList = state => state.sites?.sites
const _selectedSite = state => state.sites._selectedSite
// export const selectSiteById = (state, id) => siteList(state)?.find(s => s.id === id)
const _selectedDevice = state => state.sites._selectedDevice

// these are meant to extract data from redux objects
// creates one failure point incase of schema change
export const selectSitesList = (state) => state.sites?.sites;
export const selectAssignedList = (state) => state.sites?.assignedSites;
export const extractSiteById = (sites, site_id) => sites?.find(s => s.id === site_id)

export const getDeviceById = (sites, device_id) => {
    let device
    sites?.forEach(s => s.iot_devices?.forEach(d => {
        if(d.id === device_id){
            return device = d
        }
    }))
    return device 
}

export const getSiteName = siteObj => siteObj?.name

export const getModeFromDeviceObject = device => device?.last_log?.payload?.Mode
export const getLastLogFromDeviceObject = device => device?.last_log?.payload

export const getDeviceLastLog = siteObj => getSiteAgristarLastLog(siteObj)
export const getSiteAgristarLastLog = SiteObj => SiteObj?.iot_devices[0]?.last_log?.payload

// DEVICE 
export const extractDeviceLastSettingsLog = deviceObj => deviceObj?.last_log?.payload

export const getSiteDefaultDevice = SiteObj => SiteObj?.iot_devices[0]
export const getSiteAgristarDefault = siteObj => getSiteDefaultDevice(siteObj)

export const getSiteAgristarById = (SiteObj, DeviceId) => SiteObj?.iot_devices?.find(d => d.id === DeviceId)

const getSelectedSiteObj = state => extractSiteById(siteList(state), _selectedSite(state))

const getSelectedDeviceObj = state => getDeviceById(siteList(state), _selectedDevice(state))



// AWESOME EXAMPLE!
// https://medium.com/voobans-tech-stories/5-ways-to-stop-wasting-renders-in-react-redux-73b3c5d86f50
// // selectors.js

// const carsSelector = state => state.cars.carsById;
// export const getCarsSelector = createSelector(carsSelector, carsById => Object.values(carsById));

export const _statusSelector = createSelector(state => _status(state), status => status )
export const sitesListSelector = createSelector(state => siteList(state), sites => sites )

// feels dirty.....
export const _selectedSiteObjSelector = createSelector(
    state => getSelectedSiteObj(state), siteObj => siteObj
)

export const _selectedDeviceObjSelector = createSelector(
    state => getSelectedDeviceObj(state), deviceObj => deviceObj
)

// upgrade
export const getUpgrade = (state) => state.upgrade;
