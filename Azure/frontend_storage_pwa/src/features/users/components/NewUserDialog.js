import { useState } from 'react';
import { FormattedMessage, useIntl } from 'react-intl';
import { Dialog, DialogTitle, DialogContent, DialogActions, Button, TextField } from '@material-ui/core';
import UserItem from './UserItem';
import { UpdateUser } from './ManageUsersView';
import { buttonAdd, buttonCancel, loginPassword } from '../../../utilities/translationObjects';

const NewUserDialog = (props) => {
  const intl = useIntl()
  const [newUser, setNewUser] = useState({...props.default});

  const onChange = (newPermissions) => {
    const user = UpdateUser(newPermissions, newUser);
    setNewUser(user);
  }

  const onClose = (newUser) => {
    if (newUser) {
      props.onClose({...newUser});
    }
    props.setOpen(false);
    setNewUser({...props.default});
  }

  return (
    <Dialog open={props.open} onClose={props.onClose}>
      <DialogTitle>
        <FormattedMessage
          id='manageUserDialog.add'
          defaultMessage='Add New User'
        />
      </DialogTitle>
      <DialogContent>
        <div style={{display: 'flex', flexDirection: 'row', width: '100%'}}>
          <TextField 
            type='text'
            value={newUser.username}
            onChange={(e)=>setNewUser({...newUser, username: e.target.value})}
            variant='outlined'
            label={
              <FormattedMessage
                id='manageUserDialog.username'
                defaultMessage='User Name'
              />
            }
            style={{marginBottom: '4px', marginRight: '4px'}}
          />
          <TextField 
            type='password'
            value={newUser.password}
            onChange={(e)=>setNewUser({...newUser, password: e.target.value})}
            variant='outlined'
            label={intl.formatMessage(loginPassword)}
          />
        </div>
        <UserItem 
            key='newuser'
            user={newUser}
            organizations={props.organizationList}
            onDelete={undefined}
            isMe={true}
            onChange={onChange}
            isCustomer={props.isCustomer}
        />
      </DialogContent>
      <DialogActions>
        <Button onClick={() => onClose(newUser)} color="primary" disabled={newUser.username === ''}>
          {intl.formatMessage(buttonAdd)}
        </Button>
        <Button onClick={() => onClose(undefined)} color="primary" autoFocus>
          {intl.formatMessage(buttonCancel)}
        </Button>
      </DialogActions>

    </Dialog>
  );
}

export default NewUserDialog;