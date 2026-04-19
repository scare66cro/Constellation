import { Button, Checkbox, Dialog, DialogActions, DialogContent, DialogTitle, FormControlLabel, Grid } from "@material-ui/core";
import { useEffect, useState } from "react";
import { useIntl, FormattedMessage } from "react-intl";
import { buttonCancel, buttonSave } from "../../../utilities/translationObjects";

const AssignedSitesDialog = (props) => {
  const [myAssignedSites, setMyAssignedSites] = useState([]);
  const intl = useIntl();
  function onSiteChanged(site, checked) {
    const index = myAssignedSites.findIndex((item) => item.id === site.id);
    if (index > -1) {
      const updateSite = {...myAssignedSites[index]};
      updateSite.checked = checked;
      setMyAssignedSites([...myAssignedSites.slice(0, index), updateSite, ...myAssignedSites.slice(index + 1)]);
    }
  }

  useEffect(() => {
    if (Array.isArray(props.assignedSites)) {
      setMyAssignedSites([...props.assignedSites]);
    }
  }, [props.assignedSites])

  return (
    <Dialog open={props.showAssignSites} onClose={props.handleAssignClose}>
      <DialogTitle>
        <FormattedMessage
          id='manageUserDialog.assignSites'
          defaultMessage='Assign Sites for {user}'
          values={{
            user: props.user?.name,
          }}
        />
      </DialogTitle>
      <DialogContent>
        <Grid container spacing={2}> {
          Array.isArray(myAssignedSites) && props.columnAssigned > 0 && [...Array(props.columnAssigned)].map((_, columnIndex) => (
            <Grid key={columnIndex} item xs={4}>
              {
                myAssignedSites.slice(columnIndex * props.columnAssigned, (columnIndex + 1) * props.columnAssigned)
                  .map((site) => (
                    <FormControlLabel
                      key={site.id}
                      control={
                        <Checkbox
                          checked={site.checked}
                          inputProps={{ 'aria-label': 'Disable Board'}}
                          onChange={(e) => { onSiteChanged(site, e.target.checked) }}
                          color='primary'
                        />
                      }
                      label={site.name}
                      labelPlacement='end'
                    />
                  )
                )
              }
            </Grid>
          ))
        }
        </Grid>
      </DialogContent>
      <DialogActions>
        <Button onClick={() => props.handleAssignClose(myAssignedSites)} color="primary">
          {intl.formatMessage(buttonSave)}
        </Button>
        <Button onClick={() => props.handleAssignClose(undefined)} color="primary">
          {intl.formatMessage(buttonCancel)}
        </Button>
      </DialogActions>
    </Dialog>
  );
};

export default AssignedSitesDialog;