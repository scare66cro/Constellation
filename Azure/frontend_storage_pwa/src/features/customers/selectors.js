export const selectCustomersSlice = state => state.customers
export const selectCustomersStatus = state => selectCustomersSlice(state)?._status

export const selectCustomers = state => selectCustomersSlice(state).customers