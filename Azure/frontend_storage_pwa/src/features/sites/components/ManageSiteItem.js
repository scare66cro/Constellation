import { useState } from 'react';
import { v4 as Uuid4 } from 'uuid';
import {
  makeStyles, Card, CardHeader, Button, TextField,
  Dialog, DialogTitle, DialogContent, DialogActions, Typography,
} from '@material-ui/core';
import { getSiteName } from "../selectors";
import { FormattedMessage, useIntl } from 'react-intl';
import { Agristar2InputFieldSelect } from '../../iotClients/components/Agristar2InputFields';
import ManagePanels from './ManagePanels';
import { HomeWork } from '@material-ui/icons';
import { buttonAdd, buttonCancel } from '../../../utilities/translationObjects';

const useStyles = makeStyles((theme)=>({
  card:{
    // backgroundColor: theme.palette.warning.main,
    padding: "3px 3px 3px",
    marginBottom:'5px',
    '&.MuiPaper-rounded':{
        borderRadius: '2px'
    }
  },
  cardHeader:{
      '& .MuiCardHeader-content':{
          maxWidth:'72%',
      },
      '&.MuiCardHeader-root':{
          padding:'0px',
          justifyContent:'space-between'
      }
  },
}));

const ManageSiteItem = (props) => {
  const intl = useIntl();
  const classes = useStyles();
  const site = props.site;
  const sitePanels = props.panels.filter((panel) => panel.site === site?.id);
  const [addPanelDialog, setAddPanelDialog] = useState(false);
  const [newPanel, setNewPanel] = useState({});

  const ShowAddPanel = () => {
    setNewPanel({
      name: '',
      site: site.id,
      unsecured_ip: '',
      client_type: 'agristar2',
      is_active: true,
      id: Uuid4(),
      token_spent: false,
    });
    setAddPanelDialog(true);
  }

  const CloseAddPanel = (panel) => {
    if (panel) {
      props.addPanel(panel);
    }
    setAddPanelDialog(false);
  }

  return (
    <>
      <Card className={classes.card}>
        <CardHeader className={classes.cardHeader}
          title={
            <div style={{display: 'flex', flexDirection: 'row'}}>
              <HomeWork style={{marginTop: '2px', marginRight: '4px'}}/>
              <Typography variant="h6">{getSiteName(site) || '--'}</Typography>
            </div>
          }
          titleTypographyProps={{
            variant: 'h6',
            noWrap: true,
          }}
          action={
            <Button color="primary" onClick={() => ShowAddPanel()} style={{marginTop: '4px'}}>
              <FormattedMessage
                id="manage-sites.addpanel"
                defaultMessage="Add Panel"
              />
            </Button>
          }
        />
        <ManagePanels
          panels={sitePanels}
          setActiveClient={props.setActiveClient}
          setPanelDialog={props.setPanelDialog}
        />
      </Card>
      <Dialog open={addPanelDialog}>
        <DialogTitle>
          <FormattedMessage
            id="manage-site.newpanel"
            defaultMessage="Add New Panel"
          />
        </DialogTitle>
        <DialogContent>
          <div style={{display: 'flex', flexDirection: 'column', width: '100%'}}>
            <TextField
              type='text'
              value={newPanel.name}
              onChange={(e) => setNewPanel({ ...newPanel, name: e.target.value })}
              variant='outlined'
              label={
                <FormattedMessage
                  id='manage-sites.panelname'
                  defaultMessage="Panel Name"
                />
              }
              style={{marginBottom: '4px', marginRight: '4px'}}
            />
            <TextField
              type='text'
              value={newPanel.unsecured_ip}
              onChange={(e) => setNewPanel({ ...newPanel, unsecured_ip: e.target.value })}
              variant='outlined'
              label={
                <FormattedMessage
                  id='manage-sites.panelip'
                  defaultMessage="Unsecured IP"
                />
              }
              style={{marginBottom: '4px', marginRight: '4px'}}
            />
            <Agristar2InputFieldSelect
              value={newPanel.client_type}
              onChange={(e) => setNewPanel({...newPanel, client_type: e.target.value })}
              options={{ agristar1: 'agristar1', agristar2: 'agristar2', nova: 'nova'}}
              label={
                <FormattedMessage
                  id="manage-sites.panelclienttype"
                  defaultMessage="Client Type"
                />
              }
            />
          </div>
        </DialogContent>
        <DialogActions>
        <Button onClick={() => CloseAddPanel(newPanel)} color="primary" disabled={newPanel.name === ''}>
          {intl.formatMessage(buttonAdd)}
        </Button>
          <Button onClick={() => CloseAddPanel(undefined)} color="primary">
            {intl.formatMessage(buttonCancel)}
          </Button>
        </DialogActions>
      </Dialog>
    </>
  );
};

export default ManageSiteItem;