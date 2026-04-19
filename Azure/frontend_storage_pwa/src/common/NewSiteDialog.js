import { Button, Dialog, DialogTitle, DialogActions, DialogContent, TextField } from "@material-ui/core";
import { useEffect } from "react";
import { useState } from "react";
import { FormattedMessage, useIntl } from "react-intl";
import { v4 as Uuid4 } from 'uuid';
import { buttonAdd, buttonCancel } from "../utilities/translationObjects";

const NewSiteDialog = (props) => {
  const intl = useIntl()
  const [newSite, setNewSite] = useState({name: ''});

  useEffect(() => {
    if (props.dialog) {
      setNewSite({
        id: Uuid4(),
        category: 'Potato',
        zones: [],
        iot_devices: [],
        iotlist: [],
        is_active: true,
        owner_id: props.customer?.id,
        name: ''
      });
    }
  }, [props.dialog, props.customer?.id])

  return (
    <Dialog open={props.dialog}>
      <DialogTitle>
        <FormattedMessage
          id='manage-site.newsite'
          defaultMessage="Add New Site"
        />
      </DialogTitle>
      <DialogContent>
        <div style={{display: 'flex', flexDirection: 'column', width: '100%'}}>
          <TextField
            type='text'
            value={newSite.name}
            onChange={(e) => setNewSite({ ...newSite, name: e.target.value })}
            variant='outlined'
            label={
              <FormattedMessage
                id='manage-sites.sitename'
                defaultMessage="Site Name"
              />
            }
            style={{marginBottom: '4px', marginRight: '4px'}}
          />
        </div>
      </DialogContent>
      <DialogActions>
        <Button onClick={() => props.closeDialog(newSite)} color="primary" disabled={newSite.name === ''}>
          {intl.formatMessage(buttonAdd)}
        </Button>
        <Button onClick={() => props.closeDialog(undefined)} color="primary">
          {intl.formatMessage(buttonCancel)}
        </Button>
      </DialogActions>
    </Dialog>
  );
}

export default NewSiteDialog;