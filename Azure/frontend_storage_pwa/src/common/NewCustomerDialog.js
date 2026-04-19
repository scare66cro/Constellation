import { useState, useEffect } from "react";
import { Button, Checkbox, Dialog, DialogActions, DialogContent, DialogTitle, FormControlLabel, TextField } from "@material-ui/core";
import { FormattedMessage, useIntl } from "react-intl";
import { Agristar2InputFieldSelect } from "../features/iotClients/components/Agristar2InputFields";
import { buttonAdd, buttonCancel } from "../utilities/translationObjects";

const NewCustomerDialog = (props) => {
  const intl = useIntl();
  const [newCustomer, setNewCustomer] = useState({ id: '', sites: [], siteList: [], name: '', parent: '', isActive: true });

  useEffect(() => {
    if (props.dialog) {
      setNewCustomer({ id: '', name: '', parent: '', isActive: true, site: [], siteList: [] });
    }
  }, [props.dialog]);
  
  return (
    <Dialog open={props.dialog}>
      <DialogTitle>
        <FormattedMessage
          id="manage-sites.newcustomer"
          defaultMessage="Add New Customer"
        />
      </DialogTitle>
      <DialogContent>
        <div style={{display: 'flex', flexDirection: 'column', width: '100%'}}>
          <TextField
            type='text'
            value={newCustomer.name}
            onChange={(e) => setNewCustomer({ ...newCustomer, name: e.target.value })}
            variant='outlined'
            label={
              <FormattedMessage
                id='manage-sites.customername'
                defaultMessage="Customer Name"
              />
            }
            style={{marginBottom: '4px', marginRight: '4px'}}
          />
          <Agristar2InputFieldSelect
            value={newCustomer.parent}
            onChange={(e) => setNewCustomer({...newCustomer, parent: e.target.value })}
            options={props.organizations}
            label={
              <FormattedMessage
                id="manage-sites.customerparent"
                defaultMessage="Customer Parent"
              />
            }
          />
          <FormControlLabel
            control={
              <Checkbox
                checked={newCustomer.isActive}
                onChange={(e) => setNewCustomer({...newCustomer, isActive: e.target.checked})}
                color='primary'
              />
            }
            label={
              <FormattedMessage
                id="manage-sites.customeractive"
                defaultMessage="Is Active"
              />
            }
          />
        </div>
      </DialogContent>
      <DialogActions>
        <Button onClick={() => props.closeDialog(newCustomer)} color="primary" disabled={newCustomer.name === '' || newCustomer.parent === '' }>
          {intl.formatMessage(buttonAdd)}
        </Button>
        <Button onClick={() => props.closeDialog(undefined)} color="primary">
          {intl.formatMessage(buttonCancel)}
        </Button>
      </DialogActions>
    </Dialog>
  );
}

export default NewCustomerDialog;