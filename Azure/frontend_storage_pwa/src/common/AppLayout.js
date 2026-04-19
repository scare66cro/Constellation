import { AppBar, BottomNavigation, BottomNavigationAction, Icon, List, ListItem, ListItemIcon, ListItemText, makeStyles, SwipeableDrawer, Toolbar, Typography } from "@material-ui/core"
import MoreVert from '@material-ui/icons/MoreHoriz'
import AccountBoxIcon from '@material-ui/icons/AccountBox';
import PhonelinkSetupIcon from '@material-ui/icons/PhonelinkSetup';
import ExitToAppIcon from '@material-ui/icons/ExitToApp';
import RefreshIcon from '@material-ui/icons/Refresh';
import PeopleIcon from '@material-ui/icons/People';
import HouseIcon from '@material-ui/icons/House';

import storagesIcon from '../assets/images/storage-site-icon.svg'
import gellert_logo from '../assets/images/gellert_logo_full_en.png'

import { useIntl } from 'react-intl'
// import { bottomNavDashboard } from '../utilities/translationObjects'
import { bottomNavMore, bottomNavStorages, bottomNavMyProfile, 
    bottomNavAppSettings, bottomNavLogout, bottomNavManageUsers, bottomNavManageSites,
} from '../utilities/translationObjects'

import { useState, useEffect } from "react"
import { Link, useHistory, useLocation } from "react-router-dom"
import { useDispatch, useSelector } from "react-redux";
import { logout } from "../features/account/actions";
import { appURLs } from "../utilities/appNavigation";

import { selectUser } from "../features/account/selectors";
import { loadIotClients } from '../features/iotClients/actions';
import { selectIoTStatus } from "../features/iotClients/selectors";

const useStyles = makeStyles((theme)=>({
    body:{
        '&::-webkit-scrollbar': {
            [theme.breakpoints.up('sm')]: {
                width: '10px'
            },
            width: '5px'
        },
        '&::-webkit-scrollbar-track': {
            background: '#f1f1f1'
        },
        '&::-webkit-scrollbar-thumb': {
            background: '#888' 
        },
        '&::-webkit-scrollbar-thumb:hover': {
            background: '#555'
        },
    },
    // root:{
    //     backgroundColor: theme.palette.background.main,
    // },
    appBar:{
        backgroundColor: theme.palette.background.main,
        '& .MuiToolbar-gutters':{
            paddingLeft: "8px",
        }
    },
    toolBar:{
        display:'flex',
        justifyContent:'space-between'
    },
    spacerTop: theme.mixins.toolbar,
    // WARNING!!, for this content window to work in iOS, aka safari,
    // ....<body> element must have scroll disabled (<body style="overflow: hidden;" >)
    // ....otherwise it will compete with nested scrollable elements like content windows 
    // ....making it appear stuck
    content:{
        width: '100%',
        height: 'calc(100% - 55px)',
    },
    spacerBottom: {
        height: '56px',
        width:'100%'
    },
    bottomNavigation: {
        left:'0',
        backgroundColor: theme.palette.background.main,
        '&.MuiBottomNavigation-root':{
            boxShadow: '0px -2px 4px -1px rgb(0 0 0 / 20%),'
            +' 0px -4px 5px 0px rgb(0 0 0 / 14%),'
            +' 0px -1px 10px 0px rgb(0 0 0 / 12%)',
        }
    },
    bottomNavigation_iPhone: {
        left:'0',
        '&.MuiBottomNavigation-root':{
            height:'75px'
        },
        '& .MuiBottomNavigationAction-label':{
            fontSize:'1rem'
        }
    }
}))

const AppLayout = (props) => {
    const classes = useStyles()
    const intl = useIntl()
    const user = useSelector((state) => selectUser(state));
    const iotStatus = useSelector((state) => selectIoTStatus(state));

    const history = useHistory()
    const navHandler = (destination) => {
        history.push(destination)
        setMenuDrawerOpen(false)
    }

    const dispatch = useDispatch()
    const _logout = () => dispatch(logout())

    const _iPhone = () => navigator.platform.toLowerCase() === 'iphone';
    
    const [menuDrawerOpen, setMenuDrawerOpen] = useState(false)

    // set navMenu location
    const location = useLocation()
    const [value, setValue] = useState(`/${location.pathname.split('/')[1]}`);
    const [showUsers, setShowUsers] = useState(false);
    const [showSites, setShowSites] = useState(false);

    useEffect(()=>{
        setValue(`/${location.pathname.split('/')[1]}`) 
    }, [location])

    useEffect(() => {
        const perms = user?.permissions || [];
        setShowUsers(Array.isArray(perms) && perms.findIndex((item) => item === 'user_account_app.view_useraccount') > -1);
    }, [user])

    useEffect(() => {
        const perms = user?.permissions || [];
        setShowSites(Array.isArray(perms) && 
                    perms.findIndex((item) => item === 'api.view_site') > -1 &&
                    perms.findIndex((item) => item === 'api.view_customeraccount') > -1 &&
                    perms.findIndex((item) => item === 'api.view_iotclient') > -1);
    }, [user]);

    const refreshAllSites = () => {
        dispatch(loadIotClients())
    }

    return(
        <div 
            // className={classes.root}
        >
            <AppBar
                className={classes.appBar}
            >
                <Toolbar className={classes.toolBar}>
                    <img src={gellert_logo} alt={'gellert_logo'}
                        onClick={()=>window.open(`https://gellert.com/`)}
                        style={{height: "35px", borderRadius:'4px', margin:'5px'}}
                    />
                    
                    {/* <IconButton onClick={alertClick}>
                        <Badge color={'error'} variant='standard' badgeContent={'3'}>
                            <NotificationsIcon  style={{color:'black'}}/>
                        </Badge>
                    </IconButton>
                    <AlertsPopup open={alertOpen} onClose={alertClick}/> */}
                </Toolbar>
            </AppBar>

            {/* <div id='content-window' className={classes.content} > */}
                <div className={classes.spacerTop} />
                    {props.children}
                <div className={classes.spacerBottom}></div>
            {/* </div>             */}

            <BottomNavigation
                showLabels
                value={value} 
                // onChange={handleChange}
                style={{position:'fixed', bottom:'0', width:'100vw'}}
                className={ `${classes.bottomNavigation} 
                    ${_iPhone() ? classes.bottomNavigation_iPhone : ''}`
                }
            >
                {/* <BottomNavigationAction 
                    label={intl.formatMessage(bottomNavDashboard)} 
                    icon={
                        <DashboardIcon fontSize='large' style={{color:'black'}}/>
                        // <Icon fontSize={'large'}>
                        //     <img src={deviceIcon} alt={''}/>
                        // </Icon>
                    }
                /> */}

                <BottomNavigationAction 
                    component={Link}
                    to="/sites"
                    label={intl.formatMessage(bottomNavStorages)}
                    value="/sites"
                    icon={
                        <Icon >
                            <img src={storagesIcon} alt={''}/>
                        </Icon>
                    }
                />

                {/* <BottomNavigationAction 
                    component={Link}
                    to="/events"
                    value="/events"
                    label={intl.formatMessage(bottomNavEvents)}
                    icon={
                        <Icon >
                            <img src={eventsIcon} alt={''}/>
                        </Icon>
                    }
                /> */}
                <BottomNavigationAction
                    label="Refresh All"
                    to={'/doesnt-exist'}
                    icon={<RefreshIcon fontSize='large' style={{color: iotStatus === 'pending' ? 'grey' : 'black'}}/>}
                    onClick={()=>refreshAllSites()}
                    disabled={iotStatus === 'pending'}/>
                
                <BottomNavigationAction 
                    label={intl.formatMessage(bottomNavMore)}
                    to={'/doesnt-exist'}
                    // value={'/more'}
                    icon={<MoreVert fontSize='large' style={{color:'black'}}/>} 
                    onClick={()=>setMenuDrawerOpen(true)}
                />
            </BottomNavigation>
            <SwipeableDrawer 
                anchor={"bottom"} 
                open={menuDrawerOpen} 
                onOpen={()=>setMenuDrawerOpen(true)}
                onClose={()=>setMenuDrawerOpen(false)}
            >
                <List>
                    {/* <ListItem button
                        disabled
                    >
                        <ListItemIcon>
                            <BusinessCenterIcon />
                        </ListItemIcon>
                        <ListItemText>
                            {intl.formatMessage(bottomNavBusinessAccount)}
                        </ListItemText>
                    </ListItem> */}

                    {
                        showUsers && 
                        <ListItem button
                            onClick={()=>navHandler(appURLs.manageUsers.index)}
                        >
                            <ListItemIcon>
                                <PeopleIcon />
                            </ListItemIcon>
                            <ListItemText>
                                {intl.formatMessage(bottomNavManageUsers)}
                            </ListItemText>
                        </ListItem>
                    }
                    {
                        showSites &&
                        <ListItem button
                            onClick={() => navHandler(appURLs.manageSites.index)}
                        >
                            <ListItemIcon>
                                <HouseIcon />
                            </ListItemIcon>
                            <ListItemText>
                                {intl.formatMessage(bottomNavManageSites)}
                            </ListItemText>
                        </ListItem>
                    }

                    <ListItem button
                        onClick={()=>navHandler(appURLs.userProfile.index)}
                        // disabled
                    >
                        <ListItemIcon>
                            <AccountBoxIcon />
                        </ListItemIcon>
                        <ListItemText>
                            {intl.formatMessage(bottomNavMyProfile)}
                        </ListItemText>
                    </ListItem>

                    {/* <ListItem button 
                        disabled
                        // onClick={()=>navHandler(reRoutes.siteDigit)}
                    >
                        <ListItemIcon>
                            <CallIcon />
                        </ListItemIcon>
                        <ListItemText>
                            {intl.formatMessage(bottomNavContactDealer)}
                        </ListItemText>
                    </ListItem> */}

                    <ListItem button 
                        onClick={()=>navHandler(appURLs.appSettings.index)}
                    >
                        <ListItemIcon>
                            <PhonelinkSetupIcon />
                        </ListItemIcon>
                        <ListItemText>
                            {intl.formatMessage(bottomNavAppSettings)}
                        </ListItemText>
                    </ListItem>


                    <ListItem button 
                        onClick={_logout}
                        style={{width:'50vw'}}>
                        <ListItemIcon >
                            <ExitToAppIcon color={'secondary'}/>
                        </ListItemIcon>
                        <ListItemText 
                            primary={
                                <Typography color={'secondary'}>
                                    {intl.formatMessage(bottomNavLogout)}
                                </Typography>
                            }                        
                        />
                    </ListItem>

                </List>
            </SwipeableDrawer>
        </div>
    )
}
export default AppLayout