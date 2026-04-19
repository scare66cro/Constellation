import { createReducer } from '@reduxjs/toolkit'
import {
  loadUsersStart, loadUsersSuccess, loadUsersFailed,
  loadOrganizationsStart, loadOrganizationsSuccess, loadOrganizationsFailed,
  loadDealersStart, loadDealersSuccess, loadDealersFailed,
} from './actions'

const initialState = {
  users: [],
  organizations: [],
  dealers: [],
}

const usersReducer = createReducer(initialState, (builder) => {
  builder
    // -------------Load Users--------------------
    .addCase(loadUsersStart, (state, action) => {
      return { ...state, users: [] };
    })
    .addCase(loadUsersSuccess, (state, action) => {
      return {...state, users: [...action.payload] };
    })
    .addCase(loadUsersFailed, (state, action) => {
      console.log('Failed to get managed user list');
    })
    // -------------Load Dealers-------------------
    .addCase(loadDealersStart, (state, action) => {
      return { ...state, dealers: [] };
    })
    .addCase(loadDealersSuccess, (state, action) => {
      return { ...state, dealers: [...action.payload] };
    })
    .addCase(loadDealersFailed, (state, action) => {
      console.log('Failed to get dealers list');
    })
    // -------------Load Organizations-------------
    .addCase(loadOrganizationsStart, (state, action) => {
      return { ...state, organizations: [] };
    })
    .addCase(loadOrganizationsSuccess, (state, action) => {
      return { ...state, organizations: [...action.payload] };
    })
    .addCase(loadOrganizationsFailed, (state, action) => {
      console.log('Failed to get organization list');
    })
  })

export default usersReducer;
