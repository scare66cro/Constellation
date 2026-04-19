import { useIntl } from 'react-intl';
import { Button, Typography } from '@material-ui/core';
import { iotClientsListEmptyMSG } from '../../../utilities/translationObjects';
import { useDispatch } from 'react-redux';
import { getClientToken } from '../../iotClients/actions';

const ManagePanels = (props) => {
  const panels = props.panels;
  const intl = useIntl();
  const dispatch = useDispatch();

  const showPanelDialog = (iotclient) => {
    dispatch(getClientToken(iotclient.id))
    props.setActiveClient(iotclient);
    props.setPanelDialog(true);
  }

  return(
    <>
    { panels.length > 0
    ?
      panels.map((iotclient) =>
        <div align='center' key={iotclient.id}>
          <Button style={{width: '50%', marginBottom: '4px'}} onClick={() => showPanelDialog(iotclient)}>
            <Typography variant="h6" style={{color: iotclient.is_active ? 'black' : 'lightgrey'}}>{iotclient.name}</Typography>
          </Button>
        </div>
      )
    :
      <div style={{width:'100%', display:'flex', marginBottom:'5px'}}>
        <Typography align='center' style={{margin: 'auto'}}>
          <i>{intl.formatMessage(iotClientsListEmptyMSG)}</i>
        </Typography>
      </div>
    }
    </>
  );
}

export default ManagePanels;
