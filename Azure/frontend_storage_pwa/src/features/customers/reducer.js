import { createReducer } from "@reduxjs/toolkit"
import { loadCustomersFailed, loadCustomersStart, loadCustomersSuccess, saveNewCustomerFailed, saveNewCustomerSuccess, updateCustomerSiteSuccess } from "./actions"

export const customerStatus = {
    pending:"pending",
    idle:"idle",
    success:"success",
    failed:"failed"
}

const initialState = {
    _status: customerStatus.pending,
    _msg: undefined,
    customers:[]
}

const customerAccountsReducer = createReducer(initialState, (builder) => {
    builder
    .addCase(loadCustomersStart, (state, action) => {
        return {
            ...state,
            _status: customerStatus.pending,
        };
    })
    .addCase(loadCustomersSuccess, (state, action) => {
        return {
            ...state,
            _status: customerStatus.idle,
            customers: [...action.payload],
        };
    })
    .addCase(loadCustomersFailed, (state, action) => {
        return {
            ...state,
            _status: customerStatus.failed,
        };
    })
    .addCase(saveNewCustomerSuccess, (state, action) => {
        let index = -1;
        let customers = [];
        for (let i = 0; i < state.customers.length; i += 1) {
            if (state.customers[i].name.toUpperCase() > action.payload.name.toUpperCase()) {
                index = i;
                break;
            }
        }
        if (index > 0) {
            customers = [...state.customers.slice(0, index), {...action.payload}, ...state.customers.slice(index + 1)];
        } else if (index === 0) {
            customers = [{...action.payload}, ...state.customers];
        } else {
            customers = [...state.customers, {...action.payload}];
        }
        return {
            ...state,
            customers,
        };
    })
    .addCase(saveNewCustomerFailed, (state, action) => {
        return {
            ...state,
            _status: customerStatus.failed,
        };
    })
    .addCase(updateCustomerSiteSuccess, (state, action) => {
        const index = state.customers.findIndex((cust) => cust.id === action.payload.id);
        const found = state.customers[index];
        return {
            ...state,
            customers: [...state.customers.slice(0, index), { ...found, sites: [...found.sites, action.payload.siteId ] }, ...state.customers.slice(index + 1)],
        };
    })
})
export default customerAccountsReducer