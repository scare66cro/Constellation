import React from 'react';
import ReactDOM from 'react-dom';
import './index.css';
import App from './app/App';
import reportWebVitals from './reportWebVitals';

import { CookiesProvider } from 'react-cookie';

import store from './utilities/ReduxStore'
import { Provider } from 'react-redux'

import TranslationProvider from './utilities/TranslationProvider'
import LoginPopup from './features/account/components/LoginPopup'
import ErrorPopup from './common/ErrorPopup';

import CustomThemeProvider from './utilities/appThemes'
import { CssBaseline } from '@material-ui/core';

ReactDOM.render(
  <React.StrictMode>
    <CssBaseline />
    
    {/* currently only used by the Translation Provider */}
    <CookiesProvider> 

      {/* Redux Provider />*/}
      <Provider store={store} >

        {/* handles Language Translations */}
        <TranslationProvider>

          {/* <style-provider /> */}
          <CustomThemeProvider  >
          {/* <ThemeProvider  > */}

            {/* popup to handle login if not authenticated */}
            {/* authorization provider */}
            <LoginPopup />   

            {/* <ErrorPopup /> */}
            <ErrorPopup />

            {/* hide app until user account is loaded */}
            {/* load app state */}
            <App />
          
          </CustomThemeProvider>

        </TranslationProvider>

      </Provider>
    
    </CookiesProvider>

  </React.StrictMode>,
  document.getElementById('root')
);

// If you want to start measuring performance in your app, pass a function
// to log results (for example: reportWebVitals(console.log))
// or send to an analytics endpoint. Learn more: https://bit.ly/CRA-vitals
reportWebVitals();
