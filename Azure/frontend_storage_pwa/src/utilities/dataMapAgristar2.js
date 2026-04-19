import { FormattedMessage, useIntl } from "react-intl"
import { tempOptions } from "./appUnitsOfMeasurements"
import {agristar2ModeTranslationShutdown,
    agristar2ModeTranslationCooling,
    agristar2ModeTranslationStandby
} from "./translationObjects"

// mode categories:
// Shutdown -           darkgrey
// Standby -            yellow
// Cooling -            green
// Refrigeration -      blue
// Recirculation -      greyish purple
// Heating -            orangish yellow
// Curing - not supported -> would also be orangish yellow

export const tempTypeMap = {
    '0':tempOptions.fahrenheit, // imperial
    '1':tempOptions.celsius, // metric
}

export const agristar2NumberToModeMap = {
    1: 'SHUTDOWN',
    2: 'STANDBY',
    3: 'REMOTE STANDBY',
    4: 'COOLING',
    5: 'REFRIGERATION',
    6: 'RECIRCULATING',
    7: 'HEATING',
    8: 'DEFROSTING',
    9: 'PURGING CO2',
    10: 'COOLING (RAMPING)',
    11: 'REFRIG (RAMPING)',
    12: 'FAN MANUAL',
    13: 'FAN SWITCH OFF',
    14: 'FAN REMOTE OFF',
    15: 'REFRIG REMOTE OFF',
    16: 'CURE',
    17: 'CURE',
    18: 'COOLING (DEHUMID)',
    19: 'REFRIG (DEHUMID)',
    20: 'REMOTE OFF',
    21: 'FAILURE',
    22: 'FAN BOOST',
    23: 'HEATING (RAMPING)',
    undefined: 'MODE UNKNOWN'
}

export const categoryToColorMap = {
    shutdown:'darkgrey',
    standby:'#f5d069', // yellowish
    cooling:'#66f2b5', //greenish blue
    refrigeration:'#59dbff', //lightblue
    recirculating:'#d2b0ff', // light purple
    failure: '#ffb0b0',//'#ff8080' // pinkishred
    off: '#ffb0b0', // pinkish red climacell
    on: '#66f2b5', // greenish blue climacell
    auto: '#ccff66', // light green climacell
    coolingOn: '#59dbff', // light blue climacell
    heating: '#ffcf00', // orange heating
    cure: '#ffcf00', // orange cure
    none:'#ddddc8', //sand
    undefined:'#ddddc8',
}

export const modeToCategoryMap = {
    'SHUTDOWN':'shutdown',
    'FAN SWITCH OFF':'shutdown',
    'FAN REMOTE OFF':'standby',

    'FAILURE':'failure', //??
    
    'STANDBY':'standby',
    'REMOTE STANDBY':'standby',
    'REFRIG REMOTE OFF':'standby',
    'REFRIG (RAMPING)':'refrigeration',
    'REMOTE OFF':'standby', // shutdown or standby??
    
    'COOLING':'cooling',
    'COOLING (RAMPING)':'cooling',
    'COOLING (DEHUMID)':'cooling', // bee mode??
    'PURGING CO2':'cooling', //how to know what overarching mode its in??
    
    'RECIRCULATING':'recirculating',
    'FAN MANUAL':'recirculating',
    
    'REFRIGERATION':'refrigeration',
    'REFRIGERATION (RAMPING)':'refrigeration',
    'DEFROSTING':'refrigeration',
    'REFRIG (DEHUMID)':'refrigeration',
    
    'HEATING':'heating',
    'HEATING (RAMPING)': 'heating',
    
    'FAN BOOST':'recirculating',
    
    'MODE UNKNOWN':'none', //typically indicates lost connection to controller

    'CURE':'cure',
}

export const ModeTranslationText = (props) => {
    let mode = props.mode

    const intl = useIntl()

    let translation = (device_mode) => {
        switch(device_mode){
            case 'SHUTDOWN':
                return intl.formatMessage(agristar2ModeTranslationShutdown)
            case 'COOLING':
                return intl.formatMessage(agristar2ModeTranslationCooling)
            // case 'REFRIGERATION':
            //     return intl.formatMessage(agristar2ModeTranslationCooling)
            case 'STANDBY':
                return intl.formatMessage(agristar2ModeTranslationStandby)
            case 'REMOTE STANDBY':
                return <FormattedMessage 
                            id='getCurrentModeTranslatedText[2].REMOTE-STANDBY'
                            defaultMessage='REMOTE STANDBY'
                        />
            case 'REFRIGERATION':
                return <FormattedMessage 
                            id='getCurrentModeTranslatedText[4].REFRIGERATION'
                            defaultMessage='REFRIGERATION'
                        />
            case 'RECIRCULATING':
                return <FormattedMessage 
                            id='getCurrentModeTranslatedText[5].RECIRCULATING'
                            defaultMessage='RECIRCULATING'
                        />
            case 'HEATING':
                return <FormattedMessage 
                            id='getCurrentModeTranslatedText[6].HEATING'
                            defaultMessage='HEATING'
                        />
            case 'DEFROSTING':
                return  <FormattedMessage 
                            id='getCurrentModeTranslatedText[7].DEFROSTING'
                            defaultMessage='DEFROSTING'
                        />
            case 'PURGING CO2':
                return  <FormattedMessage 
                            id='getCurrentModeTranslatedText[8].PURGING-CO2'
                            defaultMessage='PURGING CO2'
                        />
            case 'COOLING (RAMPING)':
                return  <FormattedMessage 
                            id='getCurrentModeTranslatedText[9].COOLING-RAMPING'
                            defaultMessage='COOLING (RAMPING)'
                        />
            case 'REFRIG (RAMPING)':
                return  <FormattedMessage 
                            id='getCurrentModeTranslatedText[10].REFRIGERATION-RAMPING'
                            defaultMessage='REFRIGERATION (RAMPING)'
                        />
            case 'FAN MANUAL':
                return  <FormattedMessage 
                            id='getCurrentModeTranslatedText[11].FAN-MANUAL'
                            defaultMessage='FAN MANUAL'
                        />
            case 'FAN SWITCH OFF':
                return  <FormattedMessage 
                            id='getCurrentModeTranslatedText[12].FAN-SWITCH-OFF'
                            defaultMessage='FAN SWITCH OFF'
                        />
            case 'FAN REMOTE OFF':
                return  <FormattedMessage 
                            id='getCurrentModeTranslatedText[13].FAN-REMOTE-OFF'
                            defaultMessage='FAN REMOTE OFF'
                        />
            case 'REFRIG REMOTE OFF':
                return <FormattedMessage 
                            id='getCurrentModeTranslatedText[14].REFRIG-REMOTE-OFF'
                            defaultMessage='REFRIGERATION REMOTE OFF'
                        />
            case 'CURE':
                return <FormattedMessage 
                    id='getCurrentModeTranslatedText[15].CURE'
                    defaultMessage='CURE'
                />
            case 'COOLING (DEHUMID)':
                return  <FormattedMessage 
                            id='getCurrentModeTranslatedText[17].COOLING-DEHUMID'
                            defaultMessage='COOLING (DEHUMID)'
                        />
            case 'REFRIG (DEHUMID)':
                return  <FormattedMessage 
                            id='getCurrentModeTranslatedText[18].REFRIG-DEHUMID'
                            defaultMessage='REFRIGERATION (DEHUMID)'
                        />
            case 'REMOTE OFF':
                return  <FormattedMessage 
                            id='getCurrentModeTranslatedText[19].REMOTE-OFF'
                            defaultMessage='REMOTE OFF'
                        />
            case 'FAILURE':
                return  <FormattedMessage 
                            id='getCurrentModeTranslatedText[20].FAILURE'
                            defaultMessage='FAILURE'
                        />
            case 'FAN BOOST':
                return  <FormattedMessage 
                            id='getCurrentModeTranslatedText[21].FAN-BOOST'
                            defaultMessage='FAN BOOST'
                        />
            case 'HEATING (RAMPING)':
                return <FormattedMessage
                            id='getCurrentModeTranslatedText[23].HEATING-RAMPING'
                            defaultMessage='HEATING (RAMPING)'
                        />
            case 'NETWORK ERROR':
                return <FormattedMessage
                            id='network-error'
                            defaultMessage='Network Error'
                        />
            case 'STALE DATA':
                return <FormattedMessage
                            id='stale-data'
                            defaultMessage='Stale Data'
                        />
            default:
                return  <FormattedMessage 
                            id='agristar2.mode-translation-UNKNOWN-MODE'
                            defaultMessage='UNKNOWN MODE'
                            description='Agristar2 mode that is not known.'            
                        />
        }
    }

    return translation(mode)
}

export const getFontColorForDifferential = (actual, setPoint, lowDifferential=0.2, highDifferential=0.5) => { 
    const num1 = parseFloat(actual) 
    const num2 = parseFloat(setPoint) 
    const lowDiff = parseFloat(lowDifferential)
    const highDiff = parseFloat(highDifferential)
    if(actual !== undefined && setPoint !== undefined){
        const diff = parseFloat(Math.abs(num1-num2).toFixed(1))
        if(diff > highDiff){
            return '#d90000'
        } 
        else if(diff > lowDiff){
            return 'rgb(166 105 31)'
        }
        return 'green'
    }
    return '#d90000'
}