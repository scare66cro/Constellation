import { useState, useEffect } from 'react';
import {
  Button, Paper, makeStyles, Toolbar, Typography, List,
  Dialog, DialogTitle, DialogContent, DialogActions, CircularProgress, Switch,
} from "@material-ui/core"

import { House } from '@material-ui/icons';
import { FormattedMessage, useIntl } from 'react-intl';
import { useSelector, useDispatch } from 'react-redux';
import { bottomNavManageSites } from "../../../utilities/translationObjects";
import { selectCustomers } from '../../customers/selectors';
import { selectIoTClientList, selectActiveToken, selectTokenRetrieved } from "../../iotClients/selectors";
import { selectSitesList } from "../selectors";
import { selectOrganizations } from '../../users/selectors';
import { selectUser } from '../../account/selectors';
import { loadOrganizations } from "../../users/actions";
import { v4 as Uuid4 } from 'uuid';
import ManageCustomers from './ManageCustomers';
import { resetClientToken, saveNewPanel, setPanelActiveState } from '../../iotClients/actions';
import { saveNewCustomer } from '../../customers/actions';
import { useCallback } from 'react';
import NewCustomerDialog from '../../../common/NewCustomerDialog';
import { saveNewSite } from '../actions';

const useStyles = makeStyles((theme)=>({
  root:{
      minHeight:`calc(100vh - 120px)`
  },
  header:{
    backgroundColor:theme.palette.primary.light,
    color:theme.palette.primary.contrastText,
    position: 'sticky',
    marginBottom:'50px',
    zIndex:'1',
  },
  list:{
    '&.MuiList-padding':{
        padding:'0px 4px 8px'
    }
  },
  contrastButton: {
    backgroundColor:theme.palette.primary.light,
    color: theme.palette.primary.contrastText,
    border: 0,
    marginRight: '6px',
    marginTop: '2px',
    padding: 0,
  }
}));

const ManageSitesView = (props) => {
  const intl = useIntl();
  const classes = useStyles();
  const dispatch = useDispatch();
  const organizations = useSelector((state) => selectOrganizations(state));
  const user = useSelector((state) => selectUser(state));
  const customers = useSelector((state)=>selectCustomers(state))
  const sites = useSelector((state) => selectSitesList(state));
  const panels = useSelector((state) => selectIoTClientList(state));
  const activeToken = useSelector((state) => selectActiveToken(state));
  const tokenRetrieved = useSelector((state) => selectTokenRetrieved(state));

  const [newCustomerOpen, setNewCustomerOpen] = useState(false);
  const [customerList, setCustomerList] = useState([]);
  const [organizationList, setOrganizationList] = useState([]);
  const [allSites, setAllSites] = useState([]);
  const [allPanels, setAllPanels] = useState([]);
  const [panelDialog, setPanelDialog] = useState(false);
  const [activeClient, setActiveClient] = useState(undefined);
  const [activeState, setActiveState] = useState(true);

  useEffect(() => {
    if (activeClient) {
      setActiveState(activeClient.is_active);
    }
  }, [activeClient]);

  useEffect(() => {
    setOrganizationList([]);
    dispatch(loadOrganizations());
  }, [user, dispatch]);

  useEffect(() => {
    const list = { };
    if (organizations.length > 0) {
      organizations.filter((org) => org.ctype === 'Dealer Account').forEach((org) => list[org.id] = org.name);
      setOrganizationList(list);
    }
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [organizations]);

  useEffect(() => {
    if (customers.length > 0) {
      // add a writable site list
      setCustomerList(customers.map((cust) => ({ ...cust, siteList: [...cust.sites] })));
    }
  }, [customers]);

  useEffect(() => {
    if (sites && sites.length > 0) {
      setAllSites([...sites.map((site) => ({ ...site, iotlist: [...site.iot_devices]}))]);
    }
  }, [sites]);

  useEffect(() => {
    if (panels && panels.length > 0) {
      setAllPanels([...panels]);
    }
  }, [panels]);

  const AddNewCustomer = useCallback((customer) => {
    if (customer) {
      customer.id = Uuid4();
      dispatch(saveNewCustomer(customer));
    }
    setNewCustomerOpen(false);
  }, [dispatch]);

  const AddNewSite = useCallback((site) => {
    if (site) {
      dispatch(saveNewSite(site));
    }
  }, [dispatch]);

  const AddNewPanel = useCallback((panel) => {
    if (panel) {
      dispatch(saveNewPanel(panel));
    }
  }, [dispatch]);

  const closePanelDialog = () => {
    if (activeState !== activeClient.is_active) {
      dispatch(setPanelActiveState(activeClient, activeState));
    }
    setPanelDialog(false);
    setActiveClient(undefined);
  };

  const resetToken = (id) => {
    dispatch(resetClientToken(id));
  };

  const handleActiveSwitchAction = () => {
    setActiveState(!activeState);
  };

  return (
    <>
      <Paper className={classes.root}>
        <div style={{ width: '100%', display: 'flex', flexDirection: 'row', justifyContent: 'space-between'}}>
          <Toolbar>
            <div style={{marginRight:'5px', marginTop: '4px'}}>
              <House />
            </div>
            <Typography variant={'h6'}>
              {intl.formatMessage(bottomNavManageSites)}
            </Typography>
          </Toolbar>
          <Button color="primary" onClick={() => setNewCustomerOpen(true)}>
            <FormattedMessage
              id="manage-sites.addcustomer"
              defaultMessage="Add Customer"
            />
          </Button>
        </div>
        <List style={{height: 'calc(100vh - 190px)', overflowY: 'auto'}}>
        {
          <ManageCustomers
            customers={customerList}
            addSite={AddNewSite}
            addPanel={AddNewPanel}
            organizations={organizations}
            allSites={allSites}
            allPanels={allPanels}
            setActiveClient={setActiveClient}
            setPanelDialog={setPanelDialog}
          />
        }
        </List>
      </Paper>

      <NewCustomerDialog
        dialog={newCustomerOpen}
        organizations={organizationList}
        closeDialog={AddNewCustomer}
      />

      <Dialog open={panelDialog}>
        <DialogTitle variant="h6">
          {activeClient ? activeClient.name : ''}
        </DialogTitle>
        <DialogContent>
          <FormattedMessage
            id="manage-sites.active"
            defaultMessage="Active:"
          />
          &nbsp;
          <Switch color='primary' 
            checked={activeState}
            onChange={handleActiveSwitchAction}
          />
          { tokenRetrieved.value && tokenRetrieved.error === ''
            ?
              ( activeToken.token_spent
              ?
                <DialogActions>
                  <Button color="primary" onClick={() => resetToken(activeClient.id)}>Reset Token</Button>
                  <Button color="primary" onClick={closePanelDialog}>Close</Button>
                </DialogActions>
              :
                <>
                  <Typography variant="body1">Token: {activeToken.token}</Typography>
                  <DialogActions>
                    <Button color="primary" onClick={closePanelDialog}>Close</Button>
                  </DialogActions>
                </>
              )
            :
              <div style={{width:'100%', display:'flex', justifyContent:'center'}}>
                {
                  tokenRetrieved.error === ''
                  ?
                    <CircularProgress />
                  :
                    <Typography>Unable to retrieve token</Typography>
                }
                <DialogActions>
                  <Button color="primary" onClick={closePanelDialog}>Close</Button>
                </DialogActions>
              </div>
          }
          </DialogContent>
      </Dialog>
    </>
  );
}

export default ManageSitesView;