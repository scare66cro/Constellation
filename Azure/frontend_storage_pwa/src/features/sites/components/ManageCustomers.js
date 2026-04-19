import { useState } from 'react';
import { FormattedMessage } from 'react-intl';
import { makeStyles, Typography, Button } from '@material-ui/core';
import ManageSites from './ManageSites';
import { AccountCircle } from '@material-ui/icons';
import NewSiteDialog from '../../../common/NewSiteDialog';

const useStyles = makeStyles((theme)=>({
  header:{
    backgroundColor:theme.palette.primary.light,
    color:theme.palette.primary.contrastText,
    position: 'sticky',
    marginBottom:'50px',
    zIndex:'1',
  },
  contrastButton: {
    backgroundColor:theme.palette.primary.light,
    color: theme.palette.primary.contrastText,
    border: 0,
    marginRight: '6px',
    marginTop: '2px',
    padding: 0,
  },
}));

const ManageCustomers = (props) => {
  const classes = useStyles();
  const [showAddSite, setShowAddSite] = useState(false);
  const [currentCustomer, setCurrentCustomer] = useState(undefined);

  const ShowSiteDialog = (customer) => {
    setCurrentCustomer(customer);
    setShowAddSite(true);
  }

  const CloseSiteDialog = (site) => {
    props.addSite(site);
    setShowAddSite(false);
  }

  return (
    <>
    {
      props.customers.map((cust) => (
        <div key={cust.id}>
          <div style={{margin:'2px 3px'}} className={classes.header}>
            <Typography align="center" variant="h6">
              <AccountCircle style={{float: 'left', margin: '4px 2px'}}/>
              {cust.name}
              <Button className={classes.contrastButton} style={{float: 'right', marginTop: '4px'}} onClick={() => ShowSiteDialog(cust)}>
                <FormattedMessage
                  id="manage-sites.addsite"
                  defaultMessage="Add Site"
                />
              </Button>
            </Typography>
          </div>
          {
            <ManageSites
              customer={cust}
              siteList={cust?.siteList}
              sites={props.allSites}
              panels={props.allPanels}
              addPanel={props.addPanel}
              setActiveClient={props.setActiveClient}
              setPanelDialog={props.setPanelDialog}
            />
          }
        </div>
        )
      )
    }
    {
      <NewSiteDialog 
        dialog={showAddSite}
        closeDialog={CloseSiteDialog}
        customer={currentCustomer}
      />
    }
    </>
  );
}

export default ManageCustomers;