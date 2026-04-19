import { createAction } from "@reduxjs/toolkit";

export const setDirtyBitSuccess = createAction('setDirtyBit/Success')

export const setDirtyBit = (id, value) => {
  return (dispatch) => {
    dispatch(setDirtyBitSuccess({ id, value }));
  }
}
