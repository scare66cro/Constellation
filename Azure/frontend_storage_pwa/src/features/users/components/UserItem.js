import { Button, Card, CardContent, CardHeader, Dialog, DialogActions, DialogContent, DialogContentText, FormControlLabel, makeStyles, Switch, TextField } from '@material-ui/core';
import { AccountCircle } from '@material-ui/icons';
import { memo } from 'react';
import { useEffect, useState } from 'react';
import { FormattedMessage } from 'react-intl';
import { useDispatch } from 'react-redux';
import { Agristar2InputFieldSelect } from '../../iotClients/components/Agristar2InputFields';
import { setDirtyBit } from '../../../common/actions';

const useStyles = makeStyles((theme)=>({
  card:{
      // backgroundColor: theme.palette.warning.main,
      padding: "10px 10px 3px",
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
      },
      backgroundColor:theme.palette.primary.light,
      color:theme.palette.primary.contrastText,
      position: 'sticky',
      zIndex:'1',
  },
}))


const UserItem = memo((props) => {
  const classes = useStyles();

  const dispatch = useDispatch();

  const handleClose = () => {
      setOpen(false);
      props.onDelete(props.user);
  }


  const [user] = useState(props.user);
  const [level1, setLevel1] = useState(user?.permissions?.findIndex(item => item === 'api.access_level1_IoTClient') > -1);
  const [level2, setLevel2] = useState(user?.permissions?.findIndex(item => item === 'api.access_level2_IoTClient') > -1);
  const [upgrade, setUpgrade] = useState(user?.permissions?.findIndex(item => item === 'api.view_upgrade') > -1);
  const [manageUser, setManageUser] = useState(user?.permissions?.findIndex(item => item === 'user_account_app.view_useraccount') > -1);
  const [manageSite, setManageSite] = useState(user?.permissions?.findIndex(item => item === 'api.view_customeraccount') > -1);
  const [organizationId, setOrganizationId] = useState(user?.organization);
  const [isActive, setIsActive] = useState(user?.is_active ?? true);
  const [first, setFirst] = useState(user?.first_name)
  const [last, setLast] = useState(user?.last_name)
  const [email, setEmail] = useState(user?.email)
  const [open, setOpen] = useState(false);
  const [ready, setReady] = useState(false);

  useEffect(() => {
    setReady(true);
  }, []);

  useEffect(() => {
    if (ready) {
      const newUser = {
        id: props.user.id,
        level1,
        level2,
        upgrade,
        manageUser,
        manageSite,
        organizationId,
        first,
        last,
        email,
        isActive
      };
      props.onChange(newUser);
    }
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [level1, level2, upgrade, manageUser, manageSite, organizationId, first, last, email, isActive, props.user.id])

  return (
    <>
      <Dialog open={open} onClose={handleClose}>
        <DialogContent>
          <DialogContentText>
            <FormattedMessage 
              id='user.areyousure'
              defaultMessage='Are you sure you want to delete {username}?'
              description='User Name Delete'
              values={{ username: props.user.username }}
            />
          </DialogContentText>
          <DialogActions>
            <Button onClick={handleClose} color="primary">
              <FormattedMessage
                id='user.yes'
                defaultMessage='Yes'
              />
            </Button>
            <Button onClick={() => setOpen(false)} color="primary" autoFocus>
              <FormattedMessage
                id='user.no'
                defaultMessage='No'
              />
            </Button>
          </DialogActions>
        </DialogContent>
      </Dialog>
      <Card className={classes.card}>
        <CardHeader
          className={classes.cardHeader}
          title={
            <div style={{display: 'flex', flexDirection: 'row'}}>
              <AccountCircle style={{float: 'left', margin: '4px 6px'}} />
              {props.user.username || '--'}
            </div>
          }
          titleTypographyProps={{
              variant:'h6',
              noWrap:true,
          }}
        />
        <CardContent style={{display: 'flex', flexDirection: 'column', width: '100%'}}>
          <div style={{display: 'flex', flexDirection: 'row', width: '100%'}}>
            <TextField 
              type='text'
              value={first}
              onChange={(e)=>{setFirst(e.target.value);dispatch(setDirtyBit("ManageUsersView", true));}}
              variant='outlined'
              label={
                <FormattedMessage
                  id='user.first'
                  defaultMessage='First Name'
                />
              }
              style={{marginBottom: '8px', marginRight: '4px'}}
            />
            <TextField 
              type='text'
              value={last}
              onChange={(e)=>{setLast(e.target.value);dispatch(setDirtyBit("ManageUsersView", true));}}
              variant='outlined'
              label={
                <FormattedMessage
                  id='user.last'
                  defaultMessage='Last Name'
                />
              }
              style={{marginRight: '4px'}}
            />
          </div>
          <div style={{display: 'flex', flexDirection: 'row', width: '100%'}}>
            <TextField 
              type='email'
              value={email}
              onChange={(e)=>{setEmail(e.target.value);dispatch(setDirtyBit("ManageUsersView", true));}}
              variant='outlined'
              label={
                <FormattedMessage
                  id='user.email'
                  defaultMessage='email'
                />
              }
              style={{marginBottom: '4px', marginRight: '8px'}}
            />
            <Agristar2InputFieldSelect
              value={organizationId ?? ''}
              onChange={(e) => {setOrganizationId(e.target.value);dispatch(setDirtyBit("ManageUsersView", true));}}
              options={props.organizations}
              style={{marginTop:'-.2rem'}}
              controlStyle={{marginLeft:'auto', width: '50%'}}
              label={
                <FormattedMessage
                  id='user.siteaccess'
                  defaultMessage="Site Access"
                />
              }
            />
          </div>
          <Card style={{display: 'flex', flexDirection: 'row', flexWrap: 'wrap', width: '100%', marginTop: '4px' }}>
            <CardHeader title="Permissions" />
            <CardContent style={{paddingTop: '0px'}}>
              <FormControlLabel
                control={
                  <Switch
                      checked={level1}
                      onChange={(e) => {setLevel1(e.target.checked);dispatch(setDirtyBit("ManageUsersView", true));}}
                      color='primary'
                  />
                }
                label={
                  <FormattedMessage
                      id='user.level1'
                      defaultMessage='Level 1'
                  />
                }
              />
              <FormControlLabel
                control={
                  <Switch
                      checked={level2}
                      onChange={(e) => {setLevel2(e.target.checked);dispatch(setDirtyBit("ManageUsersView", true));}}
                      color='primary'
                  />
                }
                label={
                  <FormattedMessage
                      id='user.level2'
                      defaultMessage='Level 2'
                  />
                }
              />
              {
                !props.isCustomer &&
                <FormControlLabel
                  control={
                    <Switch
                        checked={upgrade}
                        onChange={(e) => {setUpgrade(e.target.checked);dispatch(setDirtyBit("ManageUsersView", true));}}
                        color='primary'
                    />
                  }
                  label={
                    <FormattedMessage
                        id='user.upgrade'
                        defaultMessage='Upgrade'
                    />
                  }
                />
              }
              <FormControlLabel
                control={
                  <Switch
                      checked={manageUser}
                      onChange={(e) => {setManageUser(e.target.checked);dispatch(setDirtyBit("ManageUsersView", true));}}
                      color='primary'
                  />
                }
                label={
                  <FormattedMessage
                      id='user.view'
                      defaultMessage='Manage Users'
                  />
                }
              />
              <FormControlLabel
                control={
                  <Switch
                    checked={manageSite}
                    onChange={(e) => {setManageSite(e.target.checked);dispatch(setDirtyBit("ManageUsersView", true));}}
                    color='primary'
                  />
                }
                label={
                  <FormattedMessage
                    id='user.manageSite'
                    defaultMessage='Manage Storages'
                  />
                }
              />
              <FormControlLabel
                control={
                    <Switch
                      checked={isActive}
                      onChange={(e) => {setIsActive(e.target.checked);dispatch(setDirtyBit("ManageUsersView", true));}}
                      color='primary'
                    />
                }
                label={
                  <FormattedMessage
                    id='user.active'
                    defaultMessage='Active'
                  />
                }
              />
            </CardContent>
          </Card>
          <div style={{display: 'flex', flexDirection: 'row', width: '100%', marginTop: '4px'}}>
            <Button onClick={() => props.assignSites(props.user)} color="primary">
              <FormattedMessage
                id='user.assignSites'
                defaultMessage='Assign Additional Sites'
              />
            </Button>
          </div>
        </CardContent>
      </Card>
    </>
  )
});

export default memo(UserItem);
