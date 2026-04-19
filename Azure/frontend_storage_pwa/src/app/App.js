import './App.css';

import { BrowserRouter, Switch, Route, Redirect } from "react-router-dom"

import AppLayout from '../common/AppLayout';

import SiteListView from '../features/sites/components/SitesView'
import { useSelector } from 'react-redux';
import { _loggedIn } from '../features/account/selectors';
import AppSettingsView from '../features/account/components/ViewAppSettings';
import EventsView from '../features/events/components/EventsView';
import SiteView from '../features/sites/components/SiteView';
import DeviceView from '../features/iotClients/components/IoTClientView';
import { PatternUUID4 } from '../utilities/regexPatterns';
import ViewProfile from '../features/account/components/ViewProfile';
import ManageUsersView from '../features/users/components/ManageUsersView';
import ManageSitesView from '../features/sites/components/ManageSitesView';

function App() {

  const _isLoggedIn = useSelector(_loggedIn)

  document.querySelector('#app-loading-screen').style.display = 'none'

  return (
    <div style={{maxWidth:'500px', margin:'auto'}}>
      { !_isLoggedIn ? <></> :

        <BrowserRouter basename={'/storage-app'}>
              
            <AppLayout>
            
              <Switch>
                {/* DEFAULT */}
                <Route exact path={'/'} >
                  <Redirect to="/sites" />
                </Route>

                {/* SITE views */}
                <Route path={`/sites/:site_id(${PatternUUID4})`} >
                  <SiteView />
                </Route>

                <Route path={'/sites'} >
                  <SiteListView />
                </Route>

                {/* DEVICE view */}
                <Route path={`/devices/:device_id(${PatternUUID4})`} >
                  <DeviceView />
                </Route>

                {/* EVENT views */}
                <Route path={'/events'} >
                  <EventsView />
                </Route>

                <Route path={'/app-settings'} >
                  <AppSettingsView />
                </Route>

                <Route path={'/profile'} >
                  <ViewProfile />
                </Route>

                <Route path={'/manage-users'}>
                  <ManageUsersView />
                </Route>

                <Route path={'/manage-sites'}>
                  <ManageSitesView />
                </Route>
              
              </Switch>
            
            </AppLayout>
          
        </BrowserRouter>
        
      }
    </div>
  );
}

export default App;
