import { createReducer } from '@reduxjs/toolkit'
import { setDirtyBitSuccess } from './actions';

const initialState = {
  dirtyBits: {}, // used to determine if save icon should be shown
}

const commonReducer = createReducer(initialState, (builder) => {
  builder.addCase(setDirtyBitSuccess, (state, action) => {
    return {
      ...state,
      dirtyBits: {
        ...state.dirtyBits,
        [action.payload.id]: action.payload.value,
      },
    };
  });
});

export default commonReducer;