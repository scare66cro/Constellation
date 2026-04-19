import { createAction } from '@reduxjs/toolkit'
import axiosInstance from '../../utilities/axiosConfig'

import {appError} from '../account/actions'

import { networkErrorMessage } from '../../utilities/translationObjects'
import { updateCustomerSite } from '../customers/actions'

// SITE Actions
export const setSelectedSite = createAction('setSelectedSite')
export const setSelectedDevice = createAction('setSelectedDevice')

export const loadUpgradesStart = createAction('loadUpgrades/start');
export const loadUpgradesSuccess = createAction('loadUpgrades/Success');
export const loadUpgradesFailed = createAction('loadUpgrades/Failed');

export const loadUpgrades = () => {
    return (dispatch) => {
        dispatch(loadUpgradesStart());
        axiosInstance.get('/api/upgrades/latest/upgrade_data/')
            .then((res) => {
                dispatch(loadUpgradesSuccess(res.data));
            })
            .catch((err) => {
                const resp = err.response;
                if (resp === undefined) {
                    dispatch(appError(networkErrorMessage.id))
                    dispatch(loadUpgradesFailed());
                } else if (resp.status === 401) {
                    dispatch(loadUpgradesFailed());
                } else {
                    console.log("Unabled to get upgrade information");
                }
            });
    }
}

export const loadSitesStart = createAction('loadSites/start')
export const loadSitesSuccess = createAction('loadSites/Success')
export const loadSitesFailed = createAction('loadSites/Failed')

export const loadSites = () => {
    return(dispatch) => {
        dispatch(loadSitesStart())
        axiosInstance.get('/api/usersites/my_sites/')
        .then(res => {
            dispatch(loadSitesSuccess(res.data))
        })
        .catch(err => {
            const resp = err.response
            if(resp === undefined){
                dispatch(appError(networkErrorMessage.id))
                dispatch(loadSitesFailed())
            }
            else if(resp.status === 401){
                dispatch(loadSitesFailed())
            }
            else{
                console.log("Something went wrong")
            }
        })
    }
}

export const loadAssignedSitesStart = createAction('loadAssignedSites/start')
export const loadAssignedSitesSuccess = createAction('loadAssignedSites/Success')
export const loadAssignedSitesFailed = createAction('loadAssignedSites/Failed')

export const loadAssignedSites = (user) => {
    return (dispatch) => {
        dispatch(loadAssignedSitesStart())
        axiosInstance.get(`/api/usersites/${user.id}/assigned_sites/`)
            .then((res) => {
                if (res.status === 200) {
                    dispatch(loadAssignedSitesSuccess(res.data));
                }
            })
            .catch((err) => {
                const resp = err.response;
                if (resp === undefined) {
                    dispatch(appError(networkErrorMessage.id));
                    dispatch(loadAssignedSitesFailed());
                }
                else if (resp.status === 401) {
                    dispatch(loadAssignedSitesFailed());
                }
                else {
                    console.log('Load Assigned Sites Error');
                }
            });
    };
}

export const saveAssignedSites = (id, assignedSites) => {
    return (dispatch) => {
        axiosInstance.put(`/api/usersites/${id}/`, { sites: assignedSites.filter((i) => i.checked).map((i) => i.id) })
            .catch((err) => {
                const resp = err.response;
                if (resp === undefined) {
                    dispatch(appError(networkErrorMessage.id));
                }
            });
    }
}

export const saveSiteStart = createAction('saveSite/Start');
export const saveSiteSuccess = createAction('saveSite/Success');
export const saveSiteFailed = createAction('saveSite/Failed');

export const saveNewSite = (site) => {
    return (dispatch) => {
        dispatch(saveSiteStart())
        axiosInstance.post('/api/sites/new_site/', { site })
            .then((res) => {
                dispatch(saveSiteSuccess({ site: res.data } ));
                dispatch(updateCustomerSite({ id: res.data.owner, siteId: res.data.id }));
            })
            .catch((err) => {
                dispatch(saveSiteFailed(err.message));
            });
    };
}
