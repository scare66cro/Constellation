import { Button, Fab, IconButton, makeStyles, Paper, Toolbar, Typography } from "@material-ui/core"
import {
    ArrowForwardIos, PinDrop, Schedule, Settings,
    ToggleOffOutlined, ToggleOn, Waves, SystemUpdateAlt, Landscape,
} from "@material-ui/icons"
import { useSelector } from "react-redux"
import { useHistory, useRouteMatch } from "react-router"
import { extractSiteById, selectSitesList } from "../../sites/selectors"
import Agristar2StorageDiagram from "./Agristar2StorageDiagram"
import { modeToCategoryMap, categoryToColorMap } from '../../../utilities/dataMapAgristar2'
import Agristar2PageViews from "./Agristar2PageViews"
import {
    extractAgristar2ListItemDataFromIoTClient,
    extractAgristar2PayloadFromIoTClient,
    extractIoTClientVersion,
    extractPermissionsFromIoTClient,
    extractBoardTypeFromAgristarPayload,
    isVersionAtLeast,
    selectUpgrade,
    extractOnionModeFromAgristar2Payload,
    extractBeeModeFromAgristar2Payload,
    extractPecanModeFromAgristar2Payload,
} from "../selectors"
import { useMeasurementSystem } from "../../../utilities/appUnitsOfMeasurements"
import Agristar2ButtonUnsecuredIp from "./Agristar2ButtonUnsecuredIp"
import isUpgradeable from "../../../common/version";

const CustomFab = (props) => {
    let className = props.className

    return(
        <Fab 
            size='medium' color='primary' 
            className={className} 
            style={{marginLeft:'2.5px', marginRight:'2.5px', ...props.style}}
            children={props.children}
            {...props}
        />
    )
}

const useStyles = makeStyles((theme) => ({
    button:{
        '&.MuiButton-root':{
            textTransform:'none'
        },
    },
    fab:{
        backgroundColor: theme.palette.primary.main,
        '&.Mui-focusVisible':{
            backgroundColor: theme.palette.primary.main,
        }
    },
    fabIcon:{
        color:'white'
    },
    rotate90:{
        transform: 'rotate(90deg)',
        color:'white'
    }
}))

const IoTClientViewAgristar2 = (props) => {
    // ------------------PROPS-----------------------------------
    const iotClient = props.iotClient
    
    // ------------------HOOKS-----------------------------------
    const classes = useStyles()
    const {url} = useRouteMatch()
    const prefSystemOfMeasurement = useMeasurementSystem()

    // ------------------Selectors/Extractors-------------------------------
    const obj_permissions = extractPermissionsFromIoTClient(iotClient)
    const agristar2_data = extractAgristar2ListItemDataFromIoTClient(iotClient, prefSystemOfMeasurement);
    const upgrade = useSelector((state)=>selectUpgrade(state));
    const payload = extractAgristar2PayloadFromIoTClient(iotClient);
    const onionMode = extractOnionModeFromAgristar2Payload(payload);
    const beeMode = extractBeeModeFromAgristar2Payload(payload);
    const pecanMode = extractPecanModeFromAgristar2Payload(payload);
    const clientVersion = extractIoTClientVersion(payload);
    const is102plus = isVersionAtLeast(clientVersion, 1, 0, 2);
    const tempData = payload?.['PileTempsData'];
    const humidData = payload?.['PileHumidsData'];
    const outputConfig = payload?.['OutputConfigData'];
    const boardType = extractBoardTypeFromAgristarPayload(payload);

    const sites = useSelector((state)=>selectSitesList(state))
    const site = extractSiteById(sites, iotClient.site)
    const getModeCategoryColor = (mode) => categoryToColorMap[modeToCategoryMap[mode]]

    // ------------------NAVIGATION----------------------------------------
    const history = useHistory()
    const navHandler = (destination, state) => {
        history.push(destination, state)
    }

    const sectionNavHandler = (destination, state={}) => {
        navHandler(destination, state)
        document.querySelector('#smooth-scroll-into-view').scrollIntoView({ 
            behavior: 'smooth' 
        });
    }

    return(
        < >
        {/* IOTCLIENTVIEW HEADER */}
            <Paper style={{width:'calc(100% - 10px)', margin:'5px', maxWidth:'800px'}} >
                <div style={{width:'100%', display:'flex', justifyContent:'space-between'}}>
                    <Button 
                        startIcon={<PinDrop/>} 
                        color='primary'
                        size='large'
                        className={classes.button}
                        // onClick={()=>navHandler(appURLs.siteURLs.siteView(iotClient.site))}
                    >
                        <u>{site?.name}</u>
                    </Button>

                    <Typography variant='h4' color="primary">-</Typography>
                    <div>
                        <Agristar2ButtonUnsecuredIp 
                            unsecured_ip={agristar2_data.unsecured_ip} 
                            style={{fontSize:'1.2rem', top:'-1px'}}
                        />
                        <Button 
                            color='primary'
                            className={classes.button}
                            style={{paddingLeft:'2px'}}
                            size='large'
                            disableRipple
                            // startIcon={<Lock fontSize='small' />}
                            // startIcon={<LockOpen color='secondary' fontSize='small' />}
                        >
                            {agristar2_data?.device_name}
                        </Button>
                    </div>
                </div>

                <Agristar2StorageDiagram 
                    modeColor={getModeCategoryColor(agristar2_data.current_mode)}
                    width='100%'
                    height='auto'
                    iotClient={iotClient}
                />
            </Paper>

    {/* ANCHOR FOR SCROLL-TO FUNCTION */}
            <div id="smooth-scroll-into-view" style={{position:'relative', height:'0px', top:'-60px'}}></div>

    {/* AGRISTAR PAGE NAV BAR */}
            <Toolbar disableGutters  style={{zIndex:1, display:'flex', position:'sticky', top:'60px', bottom:'65px', width:'100%'}}>
                <IconButton disabled style={{paddingLeft:'0px', paddingRight:'0px', transform: 'rotate(180deg)' }} >
                    <ArrowForwardIos />
                </IconButton>
                <div style={{width:'100%'}} >
                    <CustomFab selected className={classes.fab} 
                        onClick={()=>sectionNavHandler(`${url}/settings`)} 
                    >
                        <Settings className={classes.fabIcon} fontSize={'large'} />
                    </CustomFab>
                    {
                        is102plus && (humidData?.length > 1 || tempData?.length > 1) && !beeMode &&
                        <CustomFab
                            onClick={()=>sectionNavHandler(`${url}/pile`)}>
                            <Landscape className={classes.fabIcon} fontSize={'large'}/>
                        </CustomFab>
                    }
                    <CustomFab 
                        onClick={()=>sectionNavHandler(`${url}/system-run-clock`)}
                    >
                        <Schedule className={classes.fabIcon} fontSize={'large'} />
                    </CustomFab>
                    {
                        is102plus && (boardType !== 'AS2' || outputConfig?.[3] !== '-1') && !pecanMode && !onionMode &&
                        <CustomFab
                            onClick={()=>sectionNavHandler(`${url}/climacell-clock`)}
                        >
                            <Waves className={classes.rotate90} fontSize={'large'} />
                        </CustomFab>
                    }
                    <CustomFab
                        onClick={()=>sectionNavHandler(`${url}/equipment-status`)}
                    >
                        <ToggleOn style={{padding:'-5px', position:'absolute', top:'.5rem', marginTop:'-.4rem'}} />
                        <ToggleOffOutlined style={{padding:'1px', position:'', marginTop:'1.1rem'}} />
                    </CustomFab>
                    {
                        obj_permissions?.['agristar2_action_level2'] && is102plus &&
                        <CustomFab
                            onClick={()=>sectionNavHandler(`${url}/advanced`)}>
                            <Settings fontSize={'large'} style={{padding:'-5px', position:'absolute', top:'.5rem', marginTop:'-.4rem'}} />
                            <Settings color='secondary' style={{padding:'1px', position:'', marginTop:'1.0rem', marginLeft:'.75rem', zIndex:'2'}} />
                        </CustomFab>
                    }
                    {
                        upgrade && clientVersion && clientVersion !== '1.0.0' && clientVersion !== '1.0.1-rc1' &&
                        isUpgradeable(clientVersion, upgrade.version) &&
                        <CustomFab
                            onClick={()=>sectionNavHandler(`${url}/upgrade`)}>
                            <SystemUpdateAlt fontSize={'large'} className={classes.fabIcon} />
                        </CustomFab>
                    }
                </div>
                <IconButton disabled style={{paddingLeft:'0px', paddingRight:'0px'}} >
                    <ArrowForwardIos />
                </IconButton>
            </Toolbar>

            <Paper style={{margin:'5px', marginBottom:'64px', maxWidth:'800px'}}>
                <Agristar2PageViews />
            </Paper>
        </>
    )
}
export default IoTClientViewAgristar2