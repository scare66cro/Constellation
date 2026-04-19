import { createAction } from "@reduxjs/toolkit";
import axiosCatchDefaults from "../../utilities/axiosCatchDefaults";
import axiosInstance from "../../utilities/axiosConfig";

export const loadCustomersStart = createAction('loadCustomers/start')
export const loadCustomersSuccess = createAction('loadCustomers/success')
export const loadCustomersFailed = createAction('loadCustomers/failed')

export const loadCustomers = () => {

    return (dispatch)=>{
        dispatch(loadCustomersStart())
        axiosInstance.get('/api/customer-accounts/my_customers/')
            .then(resp => {
                dispatch(loadCustomersSuccess(resp.data))
            })
            .catch(err => {
                console.log('as2 action error', err)
                axiosCatchDefaults(err, dispatch)
                dispatch(loadCustomersFailed())
            })
    }
}

export const saveNewCustomerSuccess = createAction('saveNewCustomer/Success');
export const saveNewCustomerFailed = createAction('saveNewCustomer/Failed');

export const saveNewCustomer = (customer) => {
    return (dispatch) => {
        axiosInstance.post('/api/customer-accounts/new_customer/', { customer })
            .then((resp) => {
                dispatch(saveNewCustomerSuccess(resp.data));
            })
            .catch((err) => {
                dispatch(saveNewCustomerFailed(err.message));
            });
    }
}

export const updateCustomerSiteSuccess = createAction('updateCustomerSite/Success');

export const updateCustomerSite = (payload) => {
    return (dispatch) => {
        dispatch(updateCustomerSiteSuccess(payload));
    };
}
