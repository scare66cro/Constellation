import { Icon } from "@material-ui/core"

import eventsIcon from '../assets/images/events-icon.svg'
import flame from '../assets/images/heat.gif'
import fan from '../assets/icons/agristar-fan.svg'
import CoolingModeIcon from '../assets/icons/mode-cooling.svg'
import Co2PurgeInner from '../assets/icons/mode-co2-purge-inner.svg'
import Co2PurgeOuter from '../assets/icons/mode-co2-purge-outer.svg'

import ModeRecirculationImg from '../assets/images/mode-recirculation.gif'
import ModeDefrostImg from '../assets/images/mode-defrost.gif'


import usa from '../assets/icons/flag-usa.svg'
import russia from '../assets/icons/flag-russia.svg'
import ukraine from '../assets/icons/flag-ukraine.svg'
import china from '../assets/icons/flag-china.svg';


import '../app/App.css'
import { useEffect, useState } from "react"
import { AcUnit, ErrorOutline, Help, PanTool, PowerSettingsNewOutlined, Schedule, Toys } from "@material-ui/icons"

export const EventsIcon = (props) => {
    return(
        <Icon {...props}>
            <img src={eventsIcon} alt={'EventsIcon'} />
        </Icon>
    )
}

export const CoolingIcon = (props) => {
    return(
        <img src={fan} alt={'fan'} style={{maxWidth:'30px', maxHeight:'30px'}} className='spin-fan' />
    )
}

export const AgristarMode = (props) => {

    const standbyOrange='#d0801e'
    const iconOffGrey='#3b3b3b'

    const maxWidthHeight = '35px'

    const muiIconStyle = {fontSize:'2rem', borderRadius:'100%', maxHeight:maxWidthHeight, maxWidth:maxWidthHeight}
    const muiIconStyleBG = {fontSize:'1.8rem', padding:'2px', borderRadius:'100%'}

    const modes = {
        cooling:<ModeCooling />,
        refrigeration: <AcUnit style={{...muiIconStyle, color:'navy'}} className={'spin-slow'} />
    }

    const modeIcon = (mode) => {
        switch(mode){
            case 'SHUTDOWN':
                return <PowerSettingsNewOutlined  style={{...muiIconStyleBG, backgroundColor:'black', color:'white'}}/>
            case 'STANDBY':
                return <Schedule 
                            style={{...muiIconStyleBG, color:'white', backgroundColor:standbyOrange}}
                        />
                        // NOTE: Indicates System Run Clock is programmed to STANDBY
            case 'REMOTE STANDBY':
                return <PanTool 
                            style={{...muiIconStyleBG, color:'white', padding:'6px', backgroundColor:standbyOrange}}
                        />
                        // NOTE: Triggered when something (via input) remotely tells it to wait/standby
            case 'COOLING':
                return modes.cooling
            case 'REFRIGERATION':
                return modes.refrigeration
            case 'RECIRCULATING':
                return <img src={ModeRecirculationImg} alt="Recirculating" style={{...muiIconStyle, padding:'0px 5px 0px 0px'}}/>
            case 'HEATING':
            case 'HEATING (RAMPING)':
                return <img src={flame} alt={'Heating'} style={{...muiIconStyle, padding:'0px 5px 10px 0px'}}/>
            case 'DEFROSTING':
                return  <img src={ModeDefrostImg} alt={'Defrosting'} style={{...muiIconStyle, padding:'0px 5px 0px 0px'}}/>
            case 'PURGING CO2':
                return <ModePurging />
            case 'COOLING (RAMPING)':
                return modes.cooling
            case 'REFRIG (RAMPING)':
                return modes.refrigeration
            case 'FAN MANUAL':
                return <Toys className={'spin-fan'} style={{color:iconOffGrey}}/>
            case 'FAN SWITCH OFF':
                return <Toys style={{color:iconOffGrey}}/>
            case 'FAN REMOTE OFF':
                return <Toys style={{color:iconOffGrey}}/>
            case 'REFRIG REMOTE OFF':
                return <AcUnit style={{...muiIconStyle, color:iconOffGrey}} />
            case 'CURE':
                return <img src={flame} alt={'Heating'} style={{...muiIconStyle, padding:'0px 5px 10px 0px'}}/>
            case 'COOLING (DEHUMID)':
                return modes.cooling
            case 'REFRIG (DEHUMID)':
                return modes.refrigeration
            case 'REMOTE OFF':
                return <PowerSettingsNewOutlined  
                        style={{...muiIconStyleBG, color:'white', backgroundColor:standbyOrange}}
                    />
            case 'FAILURE':
                return <ErrorOutline style={{...muiIconStyle, color:'darkred'}}
                            className={'blinker'}
                        />
            case 'FAN BOOST':
                return <Toys style={{color:iconOffGrey}} className={'spin-fan-fast'}/>
            default:
                return <Help style={muiIconStyle} />
        }
    }

    return(
        <>{ modeIcon(props.mode) }</>
    )
}

export const ModeCooling = (props) => {
    
    return(
        <div style={{maxHeight:'30px', marginTop:'4px'}}>
            <div className='Door-Inner'></div>
            <img src={CoolingModeIcon} alt={`Cooling`} style={{maxWidth:'30px', maxHeight:'30px', height:'20px'}} className='Cooling-Mode-Icon'/>
            <div className='Door-Outer'></div>
        </div>
    )
}

export const ModePurging = (props) => {
    return(
        <div style={{width:'30px', height:'30px', margin:'0px 5px 0px 0px'}}>
            <img src={Co2PurgeInner} alt={'C02 Purge'} style={{position:'relative', margin:'7px'}} />
            <img src={Co2PurgeOuter} alt={' '} style={{position:'relative', top:'-35px'}}  className='spin-co2' /> 
        </div>
    )
}

export const ModeHeating = (props) => {

    const [visible, setVisible] = useState(false)

    useEffect(()=>{
        setTimeout(function(){ setVisible(true) }, 100);
    })

    return(
        <div style={{width:'30px', height:'30px'}}>
            {   !visible ? "" : 
                <>
                    <img src={flame} alt={'Heating'} style={{width:'30px', height:'auto'}}/>
                    {/* <img src={Heating1} alt={'Heating'} style={{position:'relative'}} className='flame1'/>
                    <img src={Heating3} alt={''} style={{position:'relative', top:'-35px'}} className='flame3'/> */}
                </>
            }
        </div>
    )
}

export const IconContainer = (props) => {
    return(
        <div style={{...props.style}} hidden={props.hidden}>
            <div style={{width: '30px', height:'20px'}} >
                {props.children}
            </div>
        </div>
    )
}

export const FlagUSA = (props) => {
    return(
        <IconContainer style={{...props.style}} hidden={props.hidden}>
            <img src={usa} alt={'usa'} style={{width:'100%', height:'100%'}} />
        </IconContainer>
    )
}

export const FlagRussia = (props) => {
    return(
        <IconContainer style={{...props.style}} hidden={props.hidden}>
            <img src={russia} alt={'russia'} style={{width:'100%', height:'100%'}} />
        </IconContainer>
    )
}

export const FlagUkraine = (props) => {
    return(
        <IconContainer style={{...props.style}} hidden={props.hidden}>
            <img src={ukraine} alt={'ukraine'} style={{width:'100%', height:'100%'}} />
        </IconContainer>
    )
}

export const FlagChina = (props) => {
    return(
        <IconContainer style={{...props.style}} hidden={props.hidden}>
            <img src={china} alt={'china'} style={{width:'100%', height:'100%'}} />
        </IconContainer>
    )
}
