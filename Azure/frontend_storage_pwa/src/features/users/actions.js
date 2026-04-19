import { createAction } from '@reduxjs/toolkit'
import axiosInstance from '../../utilities/axiosConfig'

import {appError} from '../account/actions'

import { networkErrorMessage } from '../../utilities/translationObjects'

// Organizations
export const loadOrganizationsStart = createAction('loadOrganizations/start');
export const loadOrganizationsSuccess = createAction('loadOrganizations/Success');
export const loadOrganizationsFailed = createAction('loadOrganizations/Failed');

export const loadOrganizations = () => {
    return (dispatch) => {
        dispatch(loadOrganizationsStart());
        axiosInstance.get(`/api/organizations/`)
            .then((res) => {
                dispatch(loadOrganizationsSuccess(res.data));
            })
            .catch((err) => {
                const resp = err.response;
                if (resp === undefined) {
                    dispatch(appError(networkErrorMessage.id));
                    dispatch(loadOrganizationsFailed());
                } else if (resp.status === 401) {
                    dispatch(loadOrganizationsFailed());
                } else {
                    console.log("Unabled to get organization list");
                }
            });
    }
}

// Save User
export const saveUsers = (id, users) => {
    return (dispatch) => {
        axiosInstance.post('/api/user-accounts/save_users/', { users })
            .then((resp) => {
                dispatch(loadUsers(id))
            })
            .catch((err) => {
                console.log('Unable to save users', err.message);
            });
    }
};

// Delete User
export const deleteUsers = (id, list) => {
    return (dispatch) => {
        if (list.length > 0) {
            axiosInstance.post('/api/user-accounts/delete_users/', {
                ids: list
            })
                .then((resp) => {
                    dispatch(loadUsers(id));
                })
                .catch((err) => {
                    console.log("Unabled to delete users", err.message);
                });
        }
    };
}

// Dealer Actions
export const loadDealersStart = createAction('loadDealers/start');
export const loadDealersSuccess = createAction('loadDealers/Success');
export const loadDealersFailed = createAction('loadDealers/Failed');

export const loadDealers = (id) => {
    return (dispatch) => {
        dispatch(loadDealersStart());
        axiosInstance.get(`/api/user-accounts/${id}/managed_dealers/`)
            .then((res) => {
                dispatch(loadDealersSuccess(res.data));
            })
            .catch((err) => {
                const resp = err.response;
                if (resp === undefined) {
                    dispatch(appError(networkErrorMessage.id))
                    dispatch(loadDealersFailed());
                } else if (resp.status === 401) {
                    dispatch(loadDealersFailed());
                } else {
                    console.log('Unable to get dealers');
                }
            });
    }
}

// User Actions
export const loadUsersStart = createAction('loadUsers/start');
export const loadUsersSuccess = createAction('loadUsers/Success');
export const loadUsersFailed = createAction('loadUsers/Failed');

export const loadUsers = (id) => {
    return (dispatch) => {
        dispatch(loadUsersStart());
        axiosInstance.get(`/api/user-accounts/${id}/managed_users/`)
            .then((res) => {
                dispatch(loadUsersSuccess(res.data));
            })
            .catch((err) => {
                const resp = err.response;
                if (resp === undefined) {
                    dispatch(appError(networkErrorMessage.id))
                    dispatch(loadUsersFailed());
                } else if (resp.status === 401) {
                    dispatch(loadUsersFailed());
                } else {
                    console.log("Unabled to get user information");
                }
            });
    }
}

