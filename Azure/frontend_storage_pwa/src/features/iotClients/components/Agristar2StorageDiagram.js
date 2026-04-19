import { Button, IconButton, makeStyles, Typography } from '@material-ui/core'
import { AccessTime, AcUnit, FireplaceRounded, FormatColorReset, InvertColors, Toys, WbIncandescent, WbSunny } from '@material-ui/icons'
import { useIntl } from 'react-intl'
import storageDiagram from '../../../assets/images/agristar2-storage-diagram.png'
import humidifierHeadOn from '../../../assets/images/humidifier-head.png'
import humidifierPumpOn from '../../../assets/images/humidifier-pump-on.gif'
import flame from '../../../assets/images/heat.gif'

import { AgristarMode } from '../../../common/Icons'
import { ModeTranslationText } from '../../../utilities/dataMapAgristar2'
import {
    agristar2Plenum, agristar2ReturnAir, agristar2OutsideAir, agristar2CoolingTempAvailable, 
    agristar2StartButton, agristar2StopButton, agristar2BurnerOutput, agristar2OutsideBlend,
} from '../../../utilities/translationObjects'
import Agristar2DataLineDisplay from './Agristar2DataLineDisplay'
import { useEffect, useState } from 'react'
import { UnitsComponent, useMeasurementSystem } from '../../../utilities/appUnitsOfMeasurements'
import {
    extractAgristar2DiagramDataFromIoTClient, extractAgristar2IONamesFromIoTClient, extractAgristar2PayloadFromIoTClient,
    extractAgristar2PwmOutputFromIoTClient,
    extractAgristarEquipmentControlFromIoTClient, extractBeeModeFromAgristar2Payload, extractOnionModeFromAgristar2Payload,
    extractPermissionsFromIoTClient,
    interpretAgristar2BaylightsIsOn, selectSaving, extractIoTClientVersion, isVersionAtLeast,
} from '../selectors'
import { modeToCategoryMap, categoryToColorMap } from '../../../utilities/dataMapAgristar2'
import AlarmsPopupAndIconButton from './AlarmsPopupAndIconButton'
import { agristar2BaylightToggle, agristar2RemoteStart, agristar2RemoteStop, getAgristar2Data, resetSavingRefresh } from '../actions'
import { useDispatch, useSelector } from 'react-redux'
import TimePassedLabel from '../../../common/TimePassLabel'
import CoolingModeIcon from '../../../assets/icons/mode-cooling.svg'
import equipmentClimacellSvg from '../../../assets/icons/equipment-climacell.svg'
import RefreshIcon from '@material-ui/icons/Refresh';
import { useValidateData } from '../../../common/Hooks';
import useLineItemCustomColor from '../hooks/LineItemCustomColor'
import { isValidSensor } from '../../../utilities/dataValidations'

const ModeBanner = (props) => {
    const currentMode = props.currentMode

    return(
        <div style={{display:'flex', width:'60%', overflow:'hidden'}}>
            <div style={{padding:'5px', width:'35px', height:'35px'}}>
                <AgristarMode mode={(currentMode)}/>
            </div>
            <div style={{padding:'8px 0px 8px 5px'}}>
                <Typography className='marquee' noWrap variant='body1' color='primary' style={{width:'50%'}}> 
                    <span >
                        <ModeTranslationText mode={currentMode} />
                    </span>
                </Typography>
                <Typography className='marquee marquee2' noWrap variant='body1' color='primary' style={{width:'50%'}}> 
                    <span >
                        <ModeTranslationText mode={currentMode} />
                    </span>
                </Typography>
                <Typography className='marquee marquee3' noWrap variant='body1' color='primary' style={{width:'50%'}}> 
                    <span >
                        <ModeTranslationText mode={currentMode} />
                    </span>
                </Typography>
            </div>
        </div>
    )
}

const ButtonStartStop = (props) => {
    const systemRemoteOff = props.systemRemoteOff
    const iotclient = props.iotclient
    const disabled = props.disabled ?? false
    // --------------HOOKS---------------------------
    const intl = useIntl()
    const dispatch = useDispatch()

    // --------------COMPONENT LOGIC-----------------
    const startColor = '#26a65b'
    const stopColor = '#c0392b' // '#cf142b'
    const disabledColor = '#555555'
    const getColor = () => {
        if (disabled) {
            return disabledColor;
        }
        if(systemRemoteOff) {
            return startColor
        }
        return stopColor
    }

    const getButtonText = () => {
        if(systemRemoteOff) {
            return intl.formatMessage(agristar2StartButton)
        }
        return intl.formatMessage(agristar2StopButton)
    }

    // ---------------START/STOP----------------------
    // {"tag":"RemoteStop","remoteStop":"Start"}
    // {"tag":"RemoteStop","remoteStop":"Stop"}
    const onClickHandler = () => {
        if(systemRemoteOff){
            dispatch(agristar2RemoteStart(iotclient))
        }
        else{
            dispatch(agristar2RemoteStop(iotclient))
        }
    }

    return(
        <Button 
            variant='contained'
            size='small'
            style={{
                marginBottom:'5px', 
                marginLeft:'7px', 
                padding:'1px 3px', 
                backgroundColor:getColor(),
                color:'white',
                border:'none',
                boxShadow:'1px 1px 2px #3b3b3b'
            }}
            disableElevation={true}
            onClick={onClickHandler}
            disabled={disabled}

        >   
            {getButtonText()}
        </Button>
    )
}


// -------------STYLES------------------------
const useStyles = makeStyles((theme)=>({
    bayLightsButton:{
        backgroundColor:'rgb(108 108 108 / 40%)', 
        "&.MuiIconButton-sizeSmall":{
            padding:'5px'
        }
    },
    bayLightsOn:{
        color:'yellow'
    },
    bayLightsOff:{
        color:'grey'
    }
}))
const ButtonBaylight = (props) => {
    const label = props.label
    const iotclient = props.iotclient
    const bayNumber = props.bayNumber
    const baylightValue = props.baylightValue

    // --------------HOOKS-------------------
    const classes = useStyles()
    const dispatch = useDispatch()

    // --------------TOGGLE------------------
    const toggleBaylight = () => {
        dispatch(agristar2BaylightToggle(iotclient, bayNumber))
    }

    return(
        <IconButton size='small' 
            style={{marginTop:'-3px',color:'yellow', backgroundColor:'rgb(108 108 108 / 40%)'}}
            className={classes.bayLightsButton}
            onClick={toggleBaylight}
        >
                <Typography variant='caption' style={{position:'absolute', color:'black'}}>
                    {label}
                </Typography>
            <WbIncandescent 
                className={
                    interpretAgristar2BaylightsIsOn(baylightValue) ? 
                    classes.bayLightsOn 
                    : 
                    classes.bayLightsOff
                } 
            />
        </IconButton>
    )
}

const EquipmentAnimationStorageDoor = (props) => {
    const getDoorOutput = (val) => {
        switch (val) {
            default:
                return val
        }
    }
    // MULTIPLY BY .9 - this puts it onto the degrees scale of 0 is closed 90 is open 100%
    const door_value = getDoorOutput(props.doorOutput)*.9

    return(
        <div style={{height:'30px'}}>
            <div
                style={{
                    position:'absolute',
                    width: '5px',
                    height: '30px',
                    marginLeft:'-2px',
                    background: 'rgb(220 220 220)',
                    transform: `rotate(${360 - (door_value || 0)}deg)`,
                    transformOrigin: 'top left',
                    border: 'rgb(66, 66, 66) solid 1px',
                }}
            >
                <div style={{
                        marginTop:'-3px',
                        marginLeft:'-2px',
                        backgroundColor:'rgb(220 220 220)', 
                        border: 'black solid 1px',
                        borderRadius:'10px', 
                        width:'5px', 
                        height:'5px'
                    }}
                />
            </div>
            <div
                style={{
                    position:'absolute',
                    width:'3px',
                    height:'30px',
                    backgroundColor:'rgb(220 220 220)',
                    border: 'rgb(66, 66, 66) solid 1px',
                    zIndex:'2'
                }}
            />
            <img src={CoolingModeIcon} alt={`Cooling`} 
                hidden={
                    door_value === 0 || 
                    door_value === '--' || 
                    isNaN(door_value) ||
                    door_value === undefined ? true : false}
                // hidden={true}
                className='Cooling-Mode-Icon'
                style={{
                    position:'relative',
                    maxWidth:'30px', 
                    maxHeight:'30px', 
                    height:'20px',
                    marginTop:'20px',
                    marginLeft:'-20px',
                    zIndex:'1'
                }} 
            />
        </div>
    )
}

const EquipmentContainer = (props) => {
    const style = props.style
    return(
        <div 
            style={{ 
                position:'absolute', 
                width:'40px', 
                marginTop:'1px',
                height:'138px', 
                backgroundColor:'lightgrey',
                border:'black solid 1px',
                left:'11%',
                display: 'flex',
                alignItems:'center',
                flexDirection: 'column',
                justifyContent: 'space-evenly',
                ...style,
            }}
        >
            {props.children}
        </div>
    )
}

const EquipmentAnimationRefrigeration = (props) => {
    const isOn = props.isOn // true or false

    return(
        <> 
            {
                isOn ?
                <AcUnit 
                    style={{
                        width:'100%',
                        margin:'10px 0px',
                        color:'#34a7ff',
                    }}
                    className={'spin-slow'}
                    fontSize={'large'}
                />
                :
                <AcUnit 
                style={{
                    width:'100%',
                    margin:'10px 0px',
                    color:'grey'
                }}
                fontSize={'large'}
                />
            }
        </>
    )
}

const EquipmentAnimationFan = (props) => {
    const isOn = props.isOn

    const Fan = (props) => {
        return(
            <Toys 
                fontSize='large' 
                className={props.className}
                style={{
                    width:'100%',
                    margin:'10px 0px',
                    color:'#3b3b3b',
                    ...props.style
                }}
            />
        )
    }
    return(
        <>
            {
                isOn ?
                <Fan className='spin-fan' style={{color:'#525252'}} />
                :
                <Fan style={{color:'grey'}} />
            }
        </>
    )
}

const EquipmentAnimationClimacell = (props) => {
    const isOn = props.isOn

    const WaterDrop = (props) => {
        return(
            <InvertColors
                style={{ 
                    position:'relative',
                    width:'100%', 
                    height:'10px', 
                    color:'#029be8',
                    ...props.style
                }}
                className={props.className}
            />
        )
    }
    const Climacell = (props) => {
        return(
            <div
                style={{
                    position:'relative',
                    width:'100%',
                    height:'40%',
                    overflow:'clip'
                }}
            >   
                <img 
                    src={equipmentClimacellSvg}
                    alt={'Climacell'} 
                    style={{position:'absolute', width:'100%', height:'100%', backgroundColor:'rgb(219, 211, 205)', borderTop:'solid 1px black', borderBottom:'solid 1px black'}} 
                />
                {   isOn ?
                    <>
                        <div 
                            style={{
                                position:'absolute',
                                width:'100%'
                            }}
                        >
                            <WaterDrop className='climacell-water' />
                            <WaterDrop className='climacell-water'/>
                            <WaterDrop className='climacell-water'/>
                        </div>

                        <div 
                            style={{
                                position:'absolute',
                                width:'100%'
                            }}
                        >
                            <WaterDrop className='climacell-water2' />
                            <WaterDrop className='climacell-water2' />
                        </div>
                    </>
                    :
                    <></>
                }

            </div>
        )
    }

    return(
        <Climacell />
    )
}

const EquipmentAnimationPlenumHeater = (props) => {
    const hidden = props.hidden
    const style = props.style

    return(
        <>
            {
                props.isOn && !hidden ?
                    <>
                        {/* <Whatshot fontSize='large' style={{position:'absolute', color:'#ffdc6e'}}/>
                        <Whatshot 
                            fontSize='small' 
                            style={{
                                color:'red',
                                position:'relative',
                                top:'4px'
                            }}
                            className={'flickering'}
                        /> */}
                        <div style={{...style}}>
                            {/* <ModeHeating style={{width:'28px', height:'28px'}} /> */}
                            <img src={flame} alt={'Heating'} style={{borderRadius:'3px',marginTop:'4px',width:'28px', height:'28px', border:'grey 3px solid'}}/>

                        </div>
                    </>
                    :
                    <FireplaceRounded fontSize='large' style={hidden ? {color:'transparent'}:{color:'grey', width:'33px', ...style}}/>
            }
        </>
    )
}

const EquipmentAnimationHumidifier = (props) => {
    const headIsOn = props.headIsOn
    const pumpIsOn = props.pumpIsOn

    const style={
        borderRadius:'100%',
        width:'85%',
        ...props.style
    }

    return(
        <>
            {
                headIsOn ? 
                    <>
                        {
                            pumpIsOn ?
                            <>
                                <img src={humidifierPumpOn} alt={'On'} style={style} />
                            </>
                            :
                            <>
                                <img src={humidifierHeadOn} alt={'Ready'} style={style} />
                            </>
                        }
                    </>
                    :
                    <>
                        {/* maybe a silohette? */}
                    </>
            }
        </>
    )
}

const Agristar2StorageDiagram = (props) => {
    // --------------------PROPS---------------------------
    let width = props.width
    let height = props.height
    let iotClient = props.iotClient
    
    // --------------------HOOKS---------------------------
    const intl = useIntl()
    const systemOfMeasurement = useMeasurementSystem()
    const dispatch = useDispatch()

    // --------------------SELECTORS/EXTRACTORS------------
    const agristar2_data = extractAgristar2DiagramDataFromIoTClient(iotClient, systemOfMeasurement)
    const equipStatus = extractAgristarEquipmentControlFromIoTClient(iotClient)
    const saving = useSelector((state) => selectSaving(state));
    const payload = extractAgristar2PayloadFromIoTClient(iotClient);
    const IoTVersion = extractIoTClientVersion(payload);
    const is200plus = isVersionAtLeast(IoTVersion, 2, 0, 0);

    const { onionColor, humidColor, outsideColor, returnHumidColor, calcHumidColor, co2Color } = useLineItemCustomColor(agristar2_data);

    const onionMode = extractOnionModeFromAgristar2Payload(payload);
    const beeMode = extractBeeModeFromAgristar2Payload(payload);
    const cureMode = onionMode && equipStatus.cure.outputStatus === 'on' && !equipStatus.cure.remoteOff;
    const obj_permissions = extractPermissionsFromIoTClient(iotClient);
    const isAuthorized = obj_permissions?.['agristar2_action_level1']
    const pwmOutput = extractAgristar2PwmOutputFromIoTClient(iotClient);
    const io = extractAgristar2IONamesFromIoTClient(iotClient);

    const [refrigerationDefined, setRefrigerationDefined] = useState(false);

    const [protocol, setProtocol] = useState(iotClient.last_log?.payload.payload?.['Protocol'])

    const [count, setCount] = useState(0);

    const [staleData, networkError] = useValidateData(iotClient.id, 16);
    
    const [errorBanner, setErrorBanner] = useState('')

    const [isClimacellOn, setIsClimacellOn] = useState(false);

    const [isHumidifier1On, setIsHumidifier1On] = useState(false);

    const [isHumidifier2On, setIsHumidifier2On] = useState(false);

    const [isHumidifier3On, setIsHumidifier3On] = useState(false);

    useEffect(() => {
        setRefrigerationDefined(pwmOutput?.[1]?.[2] !== '-1' || io?.OutputConfig?.[13] !== '-1');
    }, [io, pwmOutput])

    useEffect(() => {
        if (networkError) {
            setErrorBanner('NETWORK ERROR');
        } else if (staleData) {
            setErrorBanner('STALE DATA');
        } else {
            setErrorBanner('');
        }
    }, [staleData, networkError]);

    // --------------------REFRESH AGRISTAR2 DATA----------
    // refresh on save
    useEffect(() => {
        if (saving.refresh) {
            dispatch(resetSavingRefresh());
        }
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [saving]);

    // refesh on start
    useEffect(()=>{
        setProtocol(iotClient.last_log.payload.payload['Protocol'])
    // eslint-disable-next-line react-hooks/exhaustive-deps
    },[iotClient.id])

    // refresh on interval
    useEffect(()=> {
        let id = null;
        if (count < 8 && (protocol === 'mqtt' || protocol === 'bridge-direct' || is200plus)) {
            // query device every 15 seconds for 2 minutes
            id = setTimeout(() => {
                dispatch(getAgristar2Data(iotClient));
                setCount(count + 1);
            }, (is200plus && protocol === 'http') ? 18000 : 15000);
        } else if (count < 2 && protocol === 'http') {
            id = setTimeout(() => {
                dispatch(getAgristar2Data(iotClient));
                setCount(count + 1);
            }, 60000);
        }
        return () => clearTimeout(id);
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [iotClient.id, count]);

    useEffect(() => {
        setIsClimacellOn(
            equipStatus?.climacell?.input === 'on'
            && ((equipStatus?.climacell?.panel_switch === 'auto'
                && equipStatus?.climacell?.outputStatus === 'on')
                || equipStatus?.climacell?.panel_switch === 'manual')
        );
    }, [equipStatus?.climacell])

    useEffect(() => {
        setIsHumidifier1On(
            equipStatus?.humidifier1Head?.input === 'on'
            && ((equipStatus?.humidifier1Head?.panel_switch === 'auto'
                && equipStatus?.humidifier1Head.outputStatus === 'on')
                || equipStatus?.humidifier1Head?.panel_switch === 'manual')
        );
        setIsHumidifier2On(
            equipStatus?.humidifier2Head?.input === 'on'
            && ((equipStatus?.humidifier2Head?.panel_switch === 'auto'
                && equipStatus?.humidifier2Head.outputStatus === 'on')
                || equipStatus?.humidifier2Head?.panel_switch === 'manual')
        );
        setIsHumidifier3On(
            equipStatus?.humidifier3Head?.input === 'on'
            && ((equipStatus?.humidifier3Head?.panel_switch === 'auto'
                && equipStatus?.humidifier3Head.outputStatus === 'on')
                || equipStatus?.humidifier3Head?.panel_switch === 'manual')
        );
    }, [equipStatus?.humidifier1Head, equipStatus?.humidifier2Head, equipStatus?.humidifier3Head])

    const refreshData = () => {
        dispatch(getAgristar2Data(iotClient));
        setCount(1);
    }

    // const bayLightsOn = agristar2_data.bay_light1 === '0' || agristar2_data.bay_light2 === '0'
    // const handleBayLightToggle = () => {} //setBayLights(!bayLights)

    // ---------------------AS2 MODE FUNCTIONS-----------------------
    const getModeCategoryColor = (mode) => categoryToColorMap[modeToCategoryMap[mode]]

    return(
        <div >
        <div id='agristar-storage-diagram' 
            style={{
                '--color-1': getModeCategoryColor(agristar2_data?.current_mode, agristar2_data?.time_stamp) || 'lightgrey',
                '--color-2': (staleData | networkError) ? '#ff9f9f' : getModeCategoryColor(agristar2_data?.current_mode, agristar2_data?.time_stamp) || 'lightgrey',
                width:width , 
                height: height,
                minHeight:'200px',
                background: `linear-gradient(315deg, var(--color-1), var(--color-2) 80%)`,
                display:'flex',
                position:'relative'
            }}
        >
            <div style={{width:'100%', maxWidth:'100%'}}>
                <div style={{display:'flex', width:'100%', overflow:'hidden', justifyContent:'space-between', verticalAlign:'center'}}>

        {/* CURRENT MODE */}
                    <ModeBanner currentMode={(staleData || networkError) ? errorBanner : agristar2_data.current_mode}/>

                    <div style={{display:'flex', justifyContent:'right', margin:'8px 8px 0px 0px'}}>
                        <AlarmsPopupAndIconButton
                            iotclient={iotClient}
                            alarmData={agristar2_data.alarm_data}
                            style={{paddingLeft:'5px',}}
                            isAuthorized={isAuthorized}
                        />

                        <ButtonStartStop 
                            systemRemoteOff={agristar2_data.system_remote_off}
                            iotclient={iotClient}
                            disabled={!isAuthorized}
                        />

                    </div>
                </div>

                <div style={{display:'flex',margin:'-5px 0px 3px 5px', maxHeight:'16px'}}>
                    <AccessTime fontSize='small' style={{fontSize:'1rem', marginTop:'3px', color: (staleData || networkError) ? 'DarkRed' : 'Black'}}/>
                    <TimePassedLabel 
                        dateTime={agristar2_data.time_stamp}
                        style={{margin:'1px 3px 5px 3px', fontStyle:'italic', color: (staleData || networkError) ? 'DarkRed' : 'Black'}}
                        />
                    { (count >= ((protocol === 'mqtt' || protocol === 'bridge-direct' || is200plus)  ? 8 : 2)) &&
                        <IconButton onClick={() => refreshData()}>
                            <RefreshIcon fontSize='small' style={{fontSize:'1rem', marginBottom:'3px', color: 'DarkRed'}}/>
                            <Typography variant="caption" display="block" style={{color: 'DarkRed'}}>Refresh Data</Typography>
                        </IconButton>
                    }
                </div>

                <div style={{width:'100%'}}>
        {/* EQUIPMENT DIAGRAM */}
                    <img alt='storage-diagram' src={storageDiagram} style={{position:'absolute', width:'100%', height:'140px', margin:'auto'}}/>
            {/* DOORS */}
                    <div
                        style={{position:'absolute', overflow:'clip', marginTop:'1px', height:'138px', borderLeft:'black solid 1px'}}
                    >   
                        <div style={{height:'20px', width:'0px'}}></div>
                        <EquipmentAnimationStorageDoor 
                            doorOutput={agristar2_data.door_value}
                        />
                        <div style={{height:'40px', width:'auto', paddingTop:'7px', paddingLeft:'1.5px'}}>
                            <Typography variant='caption'>
                                {agristar2_data.door_value === '--' ? '0' : agristar2_data.door_value}%
                            </Typography>
                        </div>
                        <EquipmentAnimationStorageDoor 
                            doorOutput={agristar2_data.door_value}
                        />
                    </div>
            {/* REFRIGERATION */}
                    <EquipmentContainer style={{left:'10%', width:'12%', maxWidth:'48px'}} >
                        <EquipmentAnimationRefrigeration
                            isOn={agristar2_data.refrig_value !== '0' && agristar2_data.refrig_value !== 0}
                        />
                        <div style={{height:'20px', width:'100%', marginTop:'-3px', textAlign:'center', paddingLeft:'1.5px'}}>
                            <Typography variant='caption' >
                                {agristar2_data.refrig_value}%
                            </Typography>
                        </div>
                        <EquipmentAnimationRefrigeration
                            isOn={agristar2_data.refrig_value !== '0' && agristar2_data.refrig_value !== 0}
                        />
                    </EquipmentContainer>
            {/* FANS */}
                    <EquipmentContainer style={{left:'24%', width:'12%', maxWidth:'48px'}}>
                        <EquipmentAnimationFan 
                            isOn={!isNaN(agristar2_data.fan_speed) || agristar2_data.fan_speed === 'Manual'}
                        />
                        <Typography variant='caption' >
                            {
                                !isNaN(agristar2_data.fan_speed) ? 
                                    agristar2_data.fan_speed // show speed if number
                                    :
                                    agristar2_data.fan_speed === 'Manual' ? 
                                    '100' // show 100% if in 'Manual'
                                    : '0' // show '0%' if 'Off' or anything else
                            }%
                        </Typography>
                        <EquipmentAnimationFan 
                            isOn={!isNaN(agristar2_data.fan_speed) || agristar2_data.fan_speed === 'Manual'}
                        />
                    </EquipmentContainer>
            {/* CLIMACELL */}
                    <EquipmentContainer style={{left:'38%', width:'12%', maxWidth:'48px', justifyContent:'space-between'}}>
                        <EquipmentAnimationClimacell 
                            isOn={isClimacellOn}
                        />
                        {
                            isClimacellOn ?
                            <InvertColors style={{color:'#029be8'}} />
                            :
                            <FormatColorReset style={{color:'grey'}} />
                        }
                        <EquipmentAnimationClimacell
                            isOn={isClimacellOn}
                        />
                    </EquipmentContainer>
            {/* HEATERS */}
                    <EquipmentContainer 
                        style={{
                            border:'none',
                            backgroundColor:'transparent',
                            left:'54%'
                        }}
                    >   
                    {/* cavity heater */}
                        <EquipmentAnimationPlenumHeater 
                            isOn={equipStatus?.cavity_heat?.outputStatus === 'on'}
                            hidden={!(equipStatus?.cavity_heat?.outputStatus === 'on')}
                            style={{position:'relative', top:'-13px', left:'10px'}}
                        />
                    {/* plenum heater */}
                        <EquipmentAnimationPlenumHeater 
                            isOn={equipStatus?.heat?.outputStatus === 'on'}
                            hidden={!(equipStatus?.heat?.outputStatus === 'on')}
                        />
                    {/* hidden spacer */}
                        <EquipmentAnimationPlenumHeater 
                            isOn={false}
                            hidden={true}
                        />
                    </EquipmentContainer>
            {/* BURNER */}
                    <EquipmentContainer
                        style={{
                            border: 'none',
                            backgroundColor: 'transparent',
                            left: '65%'
                        }}
                    >
                        <EquipmentAnimationPlenumHeater
                            isOn={equipStatus?.burner?.outputStatus === 'on'}
                            hidden={!(equipStatus?.burner?.outputStatus === 'on')}
                        />
                    </EquipmentContainer>
            {/* HUMIDIFIER */}
                    <EquipmentContainer
                        style={{
                            border:'none',
                            backgroundColor:'transparent',
                            left:'65%'
                        }}
                    >
                        <EquipmentAnimationHumidifier 
                            headIsOn={isHumidifier1On}
                            pumpIsOn={equipStatus?.humidifier1Pump?.outputStatus === 'on'}
                        />
                    </EquipmentContainer>
                    <EquipmentContainer
                        style={{
                            border:'none',
                            backgroundColor:'transparent',
                            left:'76%'
                        }}
                    >
                        <EquipmentAnimationHumidifier 
                            headIsOn={isHumidifier2On}
                            pumpIsOn={equipStatus?.humidifier2Pump?.outputStatus === 'on'}
                        />
                    </EquipmentContainer>
                    <EquipmentContainer
                        style={{
                            border:'none',
                            backgroundColor:'transparent',
                            left:'87%'
                        }}
                    >
                        <EquipmentAnimationHumidifier 
                            headIsOn={isHumidifier3On}
                            pumpIsOn={equipStatus?.humidifier3Pump?.outputStatus === 'on'}
                        />
                    </EquipmentContainer>
        {/*------------ BAYLIGHTS------------------ */}
                    <>
                        <EquipmentContainer
                            style={{
                                border:'none',
                                backgroundColor:'transparent',
                                left:'72%',
                                justifyContent:'start'
                            }}
                        >
                            <ButtonBaylight 
                                iotclient={iotClient}
                                bayNumber='1'
                                label={agristar2_data.bay_light1_short}
                                baylightValue={agristar2_data.bay_light1}
                            />
                        </EquipmentContainer>
                        <EquipmentContainer
                            style={{
                                border:'none',
                                backgroundColor:'transparent',
                                left:'86%',
                                justifyContent:'start',
                            }}
                        >
                            <ButtonBaylight 
                                iotclient={iotClient}
                                bayNumber='2'
                                label={agristar2_data.bay_light2_short}
                                baylightValue={agristar2_data.bay_light2}
                            />
                        </EquipmentContainer>
                    </>
        {/* -------END BAYLIGHTS-------- */}
                    <div style={{width:'100%', height:'140px', margin:'auto'}}></div>
        {/* CO2 data */}
                    { !onionMode
                        ?
                        <div 
                            style={{display:'flex', justifyContent:'left', height:'auto', width:'100%',
                                overflow:'none', position:'absolute', top:'150px'}}
                        >
                            <Typography variant={'body2'} style={{width:'95%', textAlign:'right', marginTop:'10px', color: {co2Color}}}>
                                CO<sub>2</sub>&nbsp; <b>{agristar2_data.co2 === 'dis' ? '--' : agristar2_data.co2}</b> <UnitsComponent typeOfData={'ppm'}/>
                                {
                                    isValidSensor(agristar2_data.co2_2) &&
                                    <span>|<b>{agristar2_data.co2_2}</b> <UnitsComponent typeOfData="ppm" /></span>
                                }
                            </Typography>
                        </div>
                        :
                        <div 
                            style={{display:'flex', justifyContent:'left', height:'auto', width:'98%',
                                overflow:'none', position:'absolute', top:'150px'}}
                        >
                            <Typography variant={'body2'} style={{width:'95%', textAlign:'right', marginTop:'10px'}}>
                                {intl.formatMessage(agristar2BurnerOutput)}&nbsp;<b>{agristar2_data.burner_output}&nbsp;%</b>
                            </Typography>
                        </div>
                    }
                </div>
                <div style={{padding:'5px'}}>
                <div style={{
                    width:'100%', display:'grid', justifyContent:'left', padding:'5px',
                    gridTemplateColumns: '1fr 2fr 1fr',
                }}>
                    {/* PLENUM */}
                    <Typography variant="body1" >
                        {intl.formatMessage(agristar2Plenum)}
                    </Typography>
                    <Agristar2DataLineDisplay 
                        dataUnit={
                            <UnitsComponent typeOfData={'temperature'}/>
                        }
                        actual={agristar2_data.plenum_temp}
                        setPoint={cureMode ? agristar2_data.cure_start_temp : agristar2_data.plenum_temp_set}
                        setPoint2={agristar2_data.plenum_temp_set2}
                        style={{textAlign: 'center'}}
                        customColor={onionColor}
                        customColor2={'darkblue'}
                        showSetPoint2={beeMode && refrigerationDefined}
                    />
                    <Agristar2DataLineDisplay 
                        dataUnit={<UnitsComponent typeOfData={'relativeHumidity'}/>}
                        actual={agristar2_data.plenum_humid}
                        setPoint={cureMode ? agristar2_data.cure_start_humid : agristar2_data.plenum_humid_set}
                        style={{textAlign: 'center'}}
                        customColor={humidColor}
                    />
                    {/* RETURN AIR*/}
                    <Typography variant="body1" >
                        {intl.formatMessage(agristar2ReturnAir)}
                    </Typography>
                    <Agristar2DataLineDisplay 
                        dataUnit={<UnitsComponent typeOfData={'temperature'}/>}
                        actual={agristar2_data.return_temp}
                        actual2={agristar2_data.return_temp2}
                        setPointHidden={true}
                        customColor={'black'}
                        style={{textAlign: 'center'}}
                    />
                    <Agristar2DataLineDisplay 
                        dataUnit={<UnitsComponent typeOfData={'relativeHumidity'}/>}
                        actual={agristar2_data.return_humid}
                        actual2={agristar2_data.return_humid2}
                        setPointHidden={true}
                        customColor={returnHumidColor}
                        style={{textAlign: 'center'}}
                    />
                </div>
                </div>
            </div>
        </div>
            <div style={{padding:'5px'}}>
                <div  style={{
                    width:'100%', display:'grid', justifyContent:'left', padding:'5px',
                    gridTemplateColumns: '4fr 3fr 3fr',
                }}>
                    {/* COOLING AVAILABLE */}
                    <Typography variant="body1">
                        {intl.formatMessage(onionMode ? agristar2OutsideBlend : agristar2CoolingTempAvailable)}
                    </Typography>
                    <Agristar2DataLineDisplay 
                        dataUnit={onionMode ? '%' : <UnitsComponent typeOfData={'temperature'}/>}
                        column={2}
                        actual={agristar2_data.cooling_available}
                        setPointHidden={true}
                        customColor={'blue'}
                        style={{textAlign: 'center'}}
                    />
                    { onionMode &&
                        <Agristar2DataLineDisplay 
                            dataUnit={<UnitsComponent typeOfData={'calculatedHumidity'}/>}
                            column={6}
                            actual={agristar2_data.calc_humid}
                            setPointHidden={true}
                            customColor={calcHumidColor}
                            style={{textAlign: 'center', gridColumnEnd: 9}}
                        />
                    }
                    {/* OUTSIDE AIR*/}
                    <Typography variant="body1" style={{gridColumnStart: 1}}>
                        {intl.formatMessage(agristar2OutsideAir)} <WbSunny fontSize={'small'} style={{color:'orange', padding:'0px'}}/>
                    </Typography>
                    <Agristar2DataLineDisplay 
                        dataUnit={<UnitsComponent typeOfData={'temperature'}/>}
                        column={2}
                        actual={agristar2_data.outside_temp}
                        setPointHidden={true}
                        customColor={outsideColor}
                        style={{textAlign: 'center'}}
                    />
                    <Agristar2DataLineDisplay 
                        dataUnit={<UnitsComponent typeOfData={'relativeHumidity'}/>}
                        column={6}
                        actual={agristar2_data.outside_humid}
                        setPointHidden={true}
                        customColor={'black'}
                        style={{textAlign: 'center'}}
                    />
                </div>
            </div>
        </div>
    )
}
export default Agristar2StorageDiagram