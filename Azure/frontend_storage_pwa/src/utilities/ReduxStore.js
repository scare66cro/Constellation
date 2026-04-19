import { configureStore, combineReducers } from '@reduxjs/toolkit'
import accountReducer from '../features/account/reducer'
import customerAccountsReducer from '../features/customers/reducer'
import eventsReducer from '../features/events/reducer'
import iotClientsReducer from '../features/iotClients/reducer'
import sitesReducer from '../features/sites/reducer'
import userReducer from '../features/users/reducer'
import commonReducer from '../common/reducer';

// import authMiddleware from '../middleware/authMiddleware'
// import logger from 'redux-logger'
// import thunk from 'redux-thunk'

// https://redux-toolkit.js.org/api/configureStore#basic-example

const reducer = combineReducers({
    account: accountReducer,
    sites: sitesReducer,
    events: eventsReducer,
    iotClients: iotClientsReducer,
    customers: customerAccountsReducer,
    users: userReducer,
    common: commonReducer,
})

// const rootReducer = (state, action) => {
//     // when a logout action is dispatched it will reset redux state
//     if (action.type === 'USER_LOGGED_OUT') {
//         state = undefined;
//     }

//     return reducer(state, action);
// };

const initialState = {
    // myAccount:{
    //     username: "",
    //     locale: ""
    // }
}

const store = configureStore({
    reducer,
    // middleware: (getDefaultMiddleware) => getDefaultMiddleware().concat(authMiddleware),
    // middleware: [thunk],
    // devTools: process.env.NODE_ENV !== 'production',
    initialState,
    // enhancers: [offline]
})

export default store