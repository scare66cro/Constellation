import { createReducer } from '@reduxjs/toolkit'

import {
    loadSitesStart, loadSitesSuccess, loadSitesFailed, setSelectedSite, setSelectedDevice,
    loadUpgradesStart, loadUpgradesSuccess, loadUpgradesFailed, saveSiteFailed, saveSiteSuccess,
    loadAssignedSitesStart, loadAssignedSitesSuccess, loadAssignedSitesFailed,
} from './actions'

const initialState = {
    _status: "pending", // idle, pending, success, failed
    _msg: undefined, //put messages that relate to site list as a whole here
    sites: undefined,
    assignedSites: undefined,
    _selectedSite: undefined, // id of site currently being viewed
    // _selectedDevice: undefined,

    // _deviceActions:{
        // ACTIONS that get sent to the agristar remain here until being resolved.
        // "the device UUID4": {
        //     "p1Plenum": {
        //         "status": "failed",
        //         "error_response": {
        //             "detail": "Device not an agristar"
        //         }
        //     },
        //     "p1OutsideAir": {
        //         "status": "failed",
        //         "error_response": {
        //             "detail": "Device not an agristar"
        //         }
        //     } 
        // },
        // "the device UUID4": {
        //     "p1OutsideAir": {
        //         "status": "failed",
        //         "error_response": {
        //             "detail": "Device not an agristar"
        //         }
        //     }
        // }
    // }
}

const sitesReducer = createReducer(initialState, (builder) => {
    builder
        .addCase(loadUpgradesStart, (state, action) => {
            return {
                ...state,
                _status: 'pending',
            };
        })
        .addCase(loadUpgradesSuccess, (state, action) => {
            return {
                ...state,
                _status: 'idle',
                upgrade: { ...action.payload.upgrade },
            };
        })
        .addCase(loadUpgradesFailed, (state, action) => {
            return {
                ...state,
                _status: 'failed'
            }
        })
        .addCase(loadSitesStart, (state, action) => {
            return{
                ...state,
                _status: 'pending'
            }
        })
        .addCase(loadSitesSuccess, (state, action) => {
            return{
                ...state,
                _status: "idle",
                sites: action.payload
            }
        })
        .addCase(loadSitesFailed, (state, action) => {
            return{
                ...state,
                _status: "failed",
                _msg: action.payload // can pass in an error message to be displayed regarding sites

                //decisions left to be made on this topic, 
                // ...if failed because of authorization, reset sites, 
                // ...if failed because of network or server error leave old ones to view
                // sites: [] 
            }
        })
        .addCase(loadAssignedSitesStart, (state, action) => {
            return {
                ...state,
                assignedSites: [],
                _status: 'pending',
            };
        })
        .addCase(loadAssignedSitesSuccess, (state, action) => {
            return {
                ...state,
                _status: "idle",
                assignedSites: action.payload,
            };
        })
        .addCase(loadAssignedSitesFailed, (state, action) => {
            return {
                ...state,
                _status: "failed",
                _msg: action.payload
            };
        })
        .addCase(saveSiteSuccess, (state, action) => {
            let index = -1;
            let sites = [];
            for (let i = 0; i < state.sites.length; i += 1) {
                if (state.sites[i].name.toUpperCase() > action.payload.site.name.toUpperCase()) {
                    index = i;
                    break;
                }
            }
            if (index > 0) {
                sites = [...state.sites.slice(0, index), {...action.payload.site}, ...state.sites.slice(index + 1)];
            } else if (index === 0) {
                sites = [{...action.payload.site}, ...state.sites];
            } else {
                sites = [...state.sites, {...action.payload.site}];
            }
            return {
                ...state,
                sites,
            }
        })
        .addCase(saveSiteFailed, (state, action) => {
            return {
                ...state,
                _status: 'failed',
                _msg: action.payload,
            }
        })
        .addCase(setSelectedSite, (state, action) => {
            return{
                ...state,
                _selectedSite: action.payload
            }
        })
        .addCase(setSelectedDevice, (state, action) => {
            return{
                ...state,
                _selectedDevice: action.payload
            }
        })
})

export default sitesReducer