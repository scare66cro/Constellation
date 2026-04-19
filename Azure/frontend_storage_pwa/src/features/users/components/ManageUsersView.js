import {
  Button, List, CircularProgress, Paper, Typography, Toolbar,
  Dialog, DialogContent, DialogContentText, DialogActions,
} from "@material-ui/core"
import { People } from "@material-ui/icons"
import { useIntl, FormattedMessage } from "react-intl"
import { useDispatch, useSelector } from "react-redux"
import { bottomNavManageUsers } from "../../../utilities/translationObjects"
import { loadOrganizations, loadUsers, saveUsers } from "../actions";
import { selectOrganizations, selectUsers } from "../selectors";
import { selectAssignedList, selectSitesList } from "../../sites/selectors";
import { selectUser } from "../../account/selectors";
import { useEffect, useRef, useState } from "react"
import UserItem from "./UserItem";
import NewUserDialog from './NewUserDialog';
import { v4 as Uuid4 } from 'uuid';
import { useCallback } from "react"
import IsDirtySave from "../../../common/IsDirtySave"
import { setDirtyBit } from "../../../common/actions"
import { loadAssignedSites, saveAssignedSites } from "../../sites/actions"
import AssignedSitesDialog from "./AssignedSitesDialog"

const ManageUsersView = (props) => {
    // ------------HOOKS--------------
    const intl = useIntl();
    const dispatch = useDispatch();

    // -----------SELECTORS------------
    const users = useSelector((state) => selectUsers(state));
    const sites = useSelector((state) => selectSitesList(state));
    const assigned = useSelector((state) => selectAssignedList(state));
    const organizations = useSelector((state) => selectOrganizations(state));
    const user = useSelector((state) => selectUser(state));
    const defaultUser = {
      id: 'newuser',
      username: '',
      password: '',
      first_name: '',
      last_name: '',
      email: '',
      isActive: true,
      permissions: [],
      organization: user.organization,
    };
    const [userList, setUserList] = useState([]);
    let saveList = useRef([]);
    const [showUserList, setShowUserList] = useState(false);
    const [organizationList, setOrganizationList] = useState([]);
    const [showOrgList, setShowOrgList] = useState(false);
    const [open, setOpen] = useState(false);
    const [openAlert, setOpenAlert] = useState(false);
    const [alertError, setAlertError] = useState('');
    const [selectedUser, setSelectedUser] = useState(undefined);
    const [showAssignSites, setShowAssignSites] = useState(false);
    const [assignedSites, setAssignedSites] = useState(undefined);
    const [columnAssigned, setColumnAssigned] = useState(1);

    const handleAssignSites = useCallback((user) => {
      setSelectedUser(user);
      setShowAssignSites(true);
    }, []);

    const handleAssignClose = (myAssignedSites) => {
      setShowAssignSites(false);
      setSelectedUser(undefined);
      if (myAssignedSites) {
        setAssignedSites(myAssignedSites);
        dispatch(saveAssignedSites(selectedUser.id, myAssignedSites));
      }
    }

    useEffect(() => {
      setColumnAssigned(Math.ceil(assignedSites?.length / 3));
    }, [assignedSites])

    useEffect(() => {
      if (selectedUser) {
        dispatch(loadAssignedSites(selectedUser));
      }
    }, [selectedUser, dispatch])

    useEffect(() => {
      setShowOrgList(false);
      setShowUserList(false);
      saveList.current = [];
      setUserList([]);
      setOrganizationList([]);
      dispatch(loadUsers(user.id));
      dispatch(loadOrganizations());
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [user]);

    useEffect(() => {
      setUserList([...users]);
      saveList.current = [];
      setShowUserList(true);
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [users])

    useEffect(() => {
      const list = { };
      if (organizations.length > 0) {
        organizations.forEach((org) => list[org.id] = `${org.ctype.split(' ')[0]}: ${org.name}`);
        setOrganizationList(list);
        setShowOrgList(true);
      }
    }, [organizations]);

    useEffect(() => {
      if (Array.isArray(assigned) && assigned.length > 0 && showAssignSites && sites) {
        setAssignedSites(sites.map(
          (site) => assigned.findIndex((item) => item.id === site.id) > -1
            ? { id: site.id, checked: true, name: site.name }
            : { id: site.id, checked: false, name: site.name }
        ));
      } else if (showAssignSites && sites) {
        setAssignedSites([...sites.map((site) => ({id: site.id, checked: false, name: site.name }))]);
      }
    }, [showAssignSites, assigned, sites])

    const AddNewUser = () => {
      setOpen(true);
    };

    const handleClose = (newUser) => {
      setOpen(false);
      if (userList.find((item) => item.username === newUser.username)) {
        setAlertError(`Duplicate username ${newUser.username}`);
        setOpenAlert(true);
      } else {
        newUser.id = Uuid4();
        setUserList([...userList, newUser]);
        saveList.current.push(newUser);
        dispatch(setDirtyBit("ManageUsersView", true));
      }
    }

    const SaveUsers = () => {
      dispatch(saveUsers(user.id, saveList.current));
      dispatch(setDirtyBit("ManageUsersView", false));
    };

    const onChange = useCallback((newUser) => {
      const index = userList.findIndex((item) => item.id === newUser.id);
      if (index > -1) {
        const updatedUser = UpdateUser(newUser, userList[index]);
        const savedIndex = saveList.current.findIndex((item) => item.id === newUser.id);
        if (savedIndex > -1) {
          saveList.current[savedIndex] = updatedUser;
        } else {
          saveList.current.push(updatedUser);
        }
      }
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [userList]);

    const handleAlertClose = () => {
      setOpenAlert(false);
    }

    return(
        users.length === 0
        ?
          <div style={{ width: '100%', display: 'flex', flexDirection: 'row', justifyContent: 'center', marginTop: '20px'}}>
            <CircularProgress />
          </div>
        : 
          <>
            <Paper style={{height: 'calc(100vh - 120px)'}}>
              <div style={{ width: '100%', display: 'flex', flexDirection: 'row', justifyContent: 'space-between'}}>
                <Toolbar>
                  <div style={{marginRight:'15px'}}>
                    <People />
                  </div>
                  <Typography variant={'h6'}>
                    {intl.formatMessage(bottomNavManageUsers)}
                  </Typography>
                </Toolbar>
                <div>
                  <Button color="primary" onClick={() => AddNewUser()}>
                    <FormattedMessage
                      id="manage-users.add"
                      defaultMessage="Add User"
                    />
                  </Button>
                  <IsDirtySave id="ManageUsersView" onClick={() => SaveUsers()} />
                </div>
              </div>
              <List style={{height: 'calc(100vh - 190px)', overflowY: 'auto'}}>
              {
                  showUserList && showOrgList && userList.map((item) =>
                    <li key={item.id}>
                      <UserItem 
                        user={item}
                        organizations={organizationList}
                        isMe={user.id === item.id}
                        onChange={onChange}
                        isCustomer={user.user_type === 'customer'}
                        assignSites={handleAssignSites}
                      />
                    </li>
                  )
              }
              </List>
            </Paper>
            <NewUserDialog
              open={open}
              setOpen={setOpen}
              user={user}
              organizationList={organizationList}
              onClose={handleClose}
              default={defaultUser}
              isCustomer={user.user_type === 'customer'}
            />
            <Dialog open={openAlert} onClose={handleAlertClose}>
              <DialogContent>
                <DialogContentText>{alertError}</DialogContentText>
              </DialogContent>
              <DialogActions>
                <Button onClick={handleAlertClose}>Close</Button>
              </DialogActions>
            </Dialog>
            <AssignedSitesDialog
              showAssignSites={showAssignSites}
              assigned={assigned}
              user={props.user}
              columnAssigned={columnAssigned}
              assignedSites={assignedSites}
              handleAssignClose={handleAssignClose}
            />
          </>
    )
}

export function ManagePermission(user, add, permission) {
  const index = user.permissions.indexOf(permission);
  if (index > -1 && !add) {
    user.permissions.splice(index, 1);
  } else if (index === -1 && add) {
    user.permissions.push(permission);
  }
}

export function UpdateUser(permissions, found) {
  const user = { ...found, permissions: [...found.permissions] };
  if (user) {
    user.organization = permissions.organizationId;
    user.is_active = permissions.isActive;
    user.first_name = permissions.first
    user.last_name = permissions.last
    user.email = permissions.email
    ManagePermission(user, permissions.level1, 'api.access_level1_IoTClient');
    ManagePermission(user, permissions.level2, 'api.access_level2_IoTClient');
    ManagePermission(user, permissions.upgrade, 'api.view_upgrade');
    ManagePermission(user, permissions.manageUser, 'user_account_app.view_useraccount');
    ManagePermission(user, permissions.manageUser, 'user_account_app.add_useraccount');
    ManagePermission(user, permissions.manageUser, 'user_account_app.change_useraccount');
    ManagePermission(user, permissions.manageUser, 'api.view_usesites');
    ManagePermission(user, permissions.manageUser, 'api.add_usersites');
    ManagePermission(user, permissions.manageUser, 'api.change_usersites');
    ManagePermission(user, permissions.manageSite, 'api.view_customeraccount');
    ManagePermission(user, permissions.manageSite, 'api.add_customeraccount');
    ManagePermission(user, permissions.manageSite, 'api.change_customeraccount');
    ManagePermission(user, permissions.manageSite, 'api.view_site');
    ManagePermission(user, permissions.manageSite, 'api.add_site');
    ManagePermission(user, permissions.manageSite, 'api.change_site');
    ManagePermission(user, permissions.manageSite, 'api.view_iotclient');
    ManagePermission(user, permissions.manageSite, 'api.add_iotclient');
    ManagePermission(user, permissions.manageSite, 'api.change_iotclient');
    return user;
  }
  return undefined;
}

export default ManageUsersView;