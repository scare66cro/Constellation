// these are objects that declare translations
import { defineMessage } from 'react-intl'

export const unitsTemperatureFahrenheit = defineMessage({
    id:'units.temperature-fahrenheit',
    defaultMessage:'\u00B0F',
    description:'standard unit of measurement for temperature Fahrenheit'
})

export const unitsTemperatureCelsius = defineMessage({
    id:'units.temperature-Celsius',
    defaultMessage:'\u00B0C',
    description:'metric unit of measurement for temperature Celsius'
})

export const unitsPPM = defineMessage({
    id:'units.parts-per-million',
    defaultMessage:'PPM',
    description:'PPM is the abreviation for parts per million. please keep abreviated '
})

export const unitsRelativeHumidity = defineMessage({
    id:'units.relative-humidity',
    defaultMessage:'%RH',
    description:'RH is the abreviation for relative humidity. please keep abreviated '
})

export const unitsCalculated = defineMessage({
    id:'units.calculated-humidity',
    defaultMessage: '% (calculated)',
    description: 'calculated humidity'
})

export const unitsMI = defineMessage({
    id: 'units.moisture-loss',
    defaultMessage: 'mi',
    description: 'moisture loss index',
});

// --------------------------TIME-------------------------------------------
export const unitsMinuteShort = defineMessage({
    id:'units.time-minutes-short',
    defaultMessage:'m',
    description:'This is short hand or acronym for minute, it must be 2 letters or less'
})

export const unitsHoursShort = defineMessage({
    id:'units.time-hourss-short',
    defaultMessage:'h',
    description:'This is short hand or acronym for hours, it must be 2 letters or less'
})

export const unitsDayShort = defineMessage({
    id:'units.time-days-short',
    defaultMessage:'d',
    description:'This is short hand or acronym for Day, it must be 2 letters or less'
})

export const unitsTimeNowShort = defineMessage({
    id:'units.time-now-short',
    defaultMessage:'now',
    description:'A consice and short way express that the data was retrieved now and is fresh.'
})

export const networkErrorMessage = defineMessage({
    id: 'app.error-popup.network-connection',
    description: 'alert pop up about the network connnection', // Description should be a string literal
    defaultMessage: 'Network error, please check your internet connection.', // Message should be a string literal
})

export const serverErrorMessage = defineMessage({
    id: 'app.error-popup.internal-server-error',
    description: 'alert pop up for 500 response', 
    defaultMessage: 'Something went wrong...', 
})

export const authorizationFailed403 = defineMessage({
    id: 'app.error-popup.authorization-failed',
    description: 'alert pop up for 403 response', 
    defaultMessage: 'You do not have permission to perform this action.', 
})

export const loginUsername = defineMessage({
    id: 'login.input-username',
    description: 'input label for username on login page', 
    defaultMessage: 'Username', 
})

export const loginPassword = defineMessage({
    id: 'login.input-password',
    description: 'input label for Password on login page', 
    defaultMessage: 'Password', 
})

export const loginCredentialsFailed = defineMessage({
    id: 'login.login-credentials-failed',
    description: 'tells user that their login attempt failed, because either their username or password was incorrect', 
    defaultMessage: 'Username or Password is incorrect', 
})

export const loginWithGoogle = defineMessage({
    id: 'login.login-with-google',
    description: 'button to click to login using google account', 
    defaultMessage: 'Login with Google', 
})

export const loginWithMicrosoft = defineMessage({
    id: 'login.login-with-microsoft',
    description: 'button to click to login using Microsoft account', 
    defaultMessage: 'Login with Microsoft', 
})

export const bottomNavDashboard = defineMessage({
    id:'bottomNav.dashboard-tab-label',
    description:'label for the button in the main app bottom navigation bar belonging to dashboard',
    defaultMessage:'Dashboard'
})

export const bottomNavStorages = defineMessage({
    id:'bottomNav.storages-tab-label',
    description:'label for the button in the main app bottom navigation bar belonging to Storages',
    defaultMessage:'Storages'
})

export const bottomNavEvents = defineMessage({
    id:'bottomNav.events-tab-label',
    description:'label for the button in the main app bottom navigation bar belonging to Events',
    defaultMessage:'Events'
})

export const bottomNavDevices = defineMessage({
    id:'bottomNav.devices-tab-label',
    description:'label for the button in the main app bottom navigation bar belonging to Devices',
    defaultMessage:'Devices'
})

export const bottomNavMore = defineMessage({
    id:'bottomNav.more-tab-label',
    description:'label for the button in the main app bottom navigation bar belonging to More',
    defaultMessage:'More'
})

export const bottomNavBusinessAccount = defineMessage({
    id:'bottomNav.BusinessAccount-tab-label',
    description:'label for the button in the main app bottom navigation bar belonging to BusinessAccount',
    defaultMessage:'Business Account'
})

export const bottomNavManageUsers = defineMessage({
    id:'bottomNav.ManageUsers-tab-label',
    description:'label for the button in the main app bottom navigation bar belonging to ManageUsers',
    defaultMessage:'Manage Users'
})

export const bottomNavManageSites = defineMessage({
    id:'bottomNav.ManageSites-tab-label',
    description:'label for the button in the main app bottom navigation bar belonging to ManageSites',
    defaultMessage:'Manage Storages'
})

export const bottomNavMyProfile = defineMessage({
    id:'bottomNav.MyProfile-tab-label',
    description:'label for the button in the main app bottom navigation bar belonging to MyProfile',
    defaultMessage:'My Profile'
})

export const bottomNavContactDealer = defineMessage({
    id:'bottomNav.ContactDealer-tab-label',
    description:'label for the button in the main app bottom navigation bar belonging to ContactDealer',
    defaultMessage:'Contact Dealer'
})

export const bottomNavAppSettings = defineMessage({
    id:'bottomNav.AppSettings-tab-label',
    description:'label for the button in the main app bottom navigation bar belonging to AppSettings',
    defaultMessage:'App Settings'
})

export const bottomNavLogout = defineMessage({
    id:'bottomNav.Logout-tab-label',
    description:'label for the button in the main app bottom navigation bar belonging to Logout',
    defaultMessage:'Logout'
})

export const sitesListViewNoSites = defineMessage({
    id:"sites.no-sites-to-view",
    defaultMessage:"No sites to view",
    description:"this message displays if there are no sites to view"
})

export const appSettingsLanguage = defineMessage({
    id:"appSettings.languages-header",
    defaultMessage:"Language",
    description:"indicates option to select a supported language to use in app"
})

export const appSettingsDisplayUnits = defineMessage({
    id:"appSettings.display-units-header",
    defaultMessage:"Measurement System",
    description:"indicates option to select a metric or standard of measurement"
})

export const appSettingsFontSize = defineMessage({
    id:"appSettings.font-size-header",
    defaultMessage:"Font Size",
    description:"indicates option to select an app wide font size"
})

export const eventsListNoEvents = defineMessage({
    id:"events.no-events-to-view",
    defaultMessage:'No events to view',
    description:"this displays if there are no sites to view"
})

export const formSiteEventSiteField = defineMessage({
    id:"events.event-form-site-field",
    defaultMessage:'Storage',
    description:"lives on the site event form, it is the label for the select storage field"
})

export const formSiteEventCategoryField = defineMessage({
    id:"events.event-form-category-field",
    defaultMessage:'Category',
    description:"lives on the site event form, it is the label for the event category field"
})

export const formSiteEventTitleField = defineMessage({
    id:"events.event-form-title-field",
    defaultMessage:'Title',
    description:"lives on the site event form, it is the label for the event Title field"
})

export const formSiteEventStartedAtField = defineMessage({
    id:"events.event-form-StartedAt-field",
    defaultMessage:'Started At',
    description:"lives on the site event form, it is the label for the event Started At field"
})

export const formSiteEventNotesField = defineMessage({
    id:"events.event-form-notes-field",
    defaultMessage:'Notes',
    description:"lives on the site event form, it is the label for the event Notes field"
})

export const formSiteEventFinishedAtField = defineMessage({
    id:"events.event-form-FinishedAt-field",
    defaultMessage:'Finished At',
    description:"lives on the site event form, it is the label for the event Finished At field"
})

export const formSiteEventTitleNewSiteEvent = defineMessage({
    id:"events.event-form-title-new-site-event",
    defaultMessage:'New Storage Event',
    description:"lives on the site event form, it is the label for the event Finished At field"
})

export const formSiteEventSaveButton = defineMessage({
    id:"events.event-form-save-button",
    defaultMessage:'Save Event',
    description:"lives on the site event form, it is the label for the event Save Event button"
})

export const buttonCancel = defineMessage({
    id:"button.Cancel",
    defaultMessage:'Cancel',
    description:"generic button used throughout the app to cancel things"
})

export const buttonAdd = defineMessage({
    id: "button.Add",
    defaultMessage:'Add',
    description:"generic button to add items"
})

export const buttonDismissAll = defineMessage({
    id:"button.dismissAll",
    defaultMessage:'Dismiss All',
    description:"generic button used throughout the app to Dismiss a list of items."
})

export const buttonSave = defineMessage({
    id:"buttonsTranslatedText[19].save",
    defaultMessage:'Save',
    description:"Save button label"
})

export const alarmPopupTitle = defineMessage({
    id:"title.iotAlarms",
    defaultMessage:'Alarms',
    description:"plural of alarm. Alarms. Alarms as in something happened at a site and the client should know about it."
})

export const siteListDeviceHasNoLastLogMSG = defineMessage({
    id:'sites.no-device-log-history-in-cloud',
    defaultMessage:'No recorded log history',
    description:'indicates to user that there are no logs for the device saved in the cloud database'
})

export const iotClientsListEmptyMSG = defineMessage({
    id:'iotClient.no-devices',
    defaultMessage:'No devices to display',
    description:'indicates to user that there are no logs for the device saved in the cloud database'
})

export const iotClientTypeUnknown = defineMessage({
    id:'iotClient.type-unknown',
    defaultMessage:'Unknown device type',
    description:'used to indicate that the device cant be displayed because its type is not a known one.'
})

// AGRIST MODES
export const agristar2ModeTranslationShutdown = defineMessage({
    id:'getCurrentModeTranslatedText[0].shutdown',
    defaultMessage:'SHUTDOWN',
})
export const agristar2ModeTranslationStandby = defineMessage({
    id:'getCurrentModeTranslatedText[1].standby',
    defaultMessage:'STANDBY',
})
export const agristar2ModeTranslationCooling = defineMessage({
    id:'getCurrentModeTranslatedText[3].cooling',
    defaultMessage:'COOLING',
})

// -----------------------------AGRISTAR general vocubulary---------------------------
export const agristar2Plenum = defineMessage({
    id:'agristar2.general-plenum',
    defaultMessage:'Plenum',
    description:'a plenum is a passage for air, treated to specific climate requirements, to be pushed into the storage chamber where the food product is.'
})

export const agristar2Fan = defineMessage({
    id:'agristar2.iotsite-fan',
    defaultMessage: 'Fan',
    description:'Fan speed',
});

export const agristar2Burner = defineMessage({
    id:'agristar2.iotsite-burner',
    defaultMessage: 'Burner',
    description: 'Burner output',
});

export const agristar2Refrigeration = defineMessage({
    id:'agristar2.iotsite-refrigeration',
    defaultMessage: 'Refrigeration',
    description: 'Refrigeration output',
});

export const agristar2Cooling = defineMessage({
    id:'agristar2.iotsite-cooling',
    defaultMessage: 'Cooling',
    description: 'Cooling Output',
});

export const agristar2ReturnAir = defineMessage({
    id:'agristar2.general-ReturnAir',
    defaultMessage:'Return Air',
    description:'Return Air Temperature and Humiditiy'
})

export const agristar2OutsideAir = defineMessage({
    id:'updOutsideTempTranslatedText[0].OutsideAir',
    defaultMessage:'Outside Air',
    description:'Outside Air Temperature and Humidity'
})


export const agristar2CoolingTempAvailable = defineMessage({
    id:'mnMainData[5].cooling.available.temp',
    defaultMessage:'Cooling Available',
    description:'Cooling Available Temperature'
})

export const agristar2BurnerOutput = defineMessage({
    id:'updCoolingOutputTranslatedText[2].burner',
    defaultMessage:'Burner Output'
})

export const agristar2OutsideBlend = defineMessage({
    id: 'MainDynTranslatedText[2].blend',
    defaultMessage: 'Outside Air Blend'
})

export const agristar2StartButton = defineMessage({
    id:'getMainDataTranslatedText[6].start',
    defaultMessage:'Start',
})

export const agristar2StopButton = defineMessage({
    id:'getMainDataTranslatedText[7].stop',
    defaultMessage:'Stop',
})

export const agristar2AlarmsListEmpty = defineMessage({
    id:'agristar2.alarms-list-empty',
    defaultMessage:'No alarms to view',
    description:'Message to display when there are not any alarms items to list in the alarms popup view.'
})

export const agristar2GeneralSettingsTitle = defineMessage({
    id:'agristar2.general-settings-title',
    defaultMessage:'General Settings',
    description:'General Settings title for agristar panel'
})

export const agristar2AdvancedSettingsTitle = defineMessage({
    id:'agristar2.advanced-settings-title',
    defaultMessage:'Advanced Settings',
    description:'Advanced Settings (Level 2) title for agristar panel',
});

// ----------------------AS2 EQUIPMENT STATUS/CONTROL PAGE---------------------------------
export const agristar2EquipmentStatusColumnStatusOn = defineMessage({
    id:'eqStatusSetStatusTranslatedText[2].on',
    defaultMessage:'On',
    description:'indicates that a piece of equipments status is on'
})
export const agristar2EquipmentStatusColumnStatusOff = defineMessage({
    id:'eqStatusSetStatusTranslatedText[0].Off',
    defaultMessage:'Off',
    description:'indicates that a piece of equipments status is Off'
})
export const agristar2EquipmentStatusColumnStatusRemoteOff = defineMessage({
    id:'eqStatusSetStatusTranslatedText[1].remote.off',
    defaultMessage:'Remote Off',
    description:'indicates that a piece of equipments status is Remote Off'
})
export const agristar2EquipmentStatusColumnStatusDiagOn = defineMessage({
    id:'eqStatusSetRefrigStatusTranslatedText[2].diag.on',
    defaultMessage:'Diag On',
    description:'indicates that a piece of equipments status is Diag On'
})
export const agristar2EquipmentStatusColumnStatusDiagOff = defineMessage({
    id:'eqStatusSetRefrigStatusTranslatedText[3].diag-off',
    defaultMessage:'Diag Off',
    description:'indicates that a piece of equipments status is Diag Off'
})
export const agristar2EquipmentStatusColumnPanelSwitchAuto = defineMessage({
    id:'eqStatusSetSwitchTranslatedText[0].auto',
    defaultMessage:'Auto',
    description:'indicates that a piece of equipments panel switch is set to automatic'
})
export const agristar2EquipmentStatusColumnPanelSwitchManual = defineMessage({
    id:'eqStatusSetSwitchTranslatedText[1].manual',
    defaultMessage:'Manual',
    description:'indicates that a piece of equipments panel switch is set to Manual'
})
export const agristar2PostActionErrorHTTP503 = defineMessage({
    id:'agristar2.postAction-error-HTTP503',
    defaultMessage:"Agristar2 action failed. Verify the Agristar2's internet connection.",
    description:'indicates that the server failed to communicate with the Agristar2(iot) device. Users internet connection is ok.'
})

// ----------------------LOOK UP TRANSLATIONS----------------------------------------------
export const getTranslationObjById = (id) => {
    return translationIdMap[id]
}

// NOTE:
// this is used for components that must get passed a dynamic
// ....translation via redux store which can't handle react-components
export const translationIdMap = {
    "login.login-credentials-failed": loginCredentialsFailed,
    "app.error-popup.network-connection": networkErrorMessage,
    "app.error-popup.internal-server-error" : serverErrorMessage,
    "app.error-popup.authorization-failed":authorizationFailed403,
    "agristar2.postAction-error-HTTP503":agristar2PostActionErrorHTTP503,
}

// -------------------------------AGRISTAR2 ALARMS-----------------------------------------
export const as2AlarmTranslations = {
    'WARN_PLENTEMP1':defineMessage({
        id:'WARN_PLENTEMP1',
        defaultMessage:'Invalid Plenum Temperature 1',
        description:'Warning message for invalid plenum temperature',
    }),
    'WARN_PLENTEMP2':defineMessage({
        id:'WARN_PLENTEMP2',
        defaultMessage:'Invalid Plenum Temperature 2',
        description:'Warning message for invalid plenum temperature',
    }),
    'WARN_NO_PLENTEMP':defineMessage({
        id:'WARN_NO_PLENTEMP',
        defaultMessage:'No valid Plenum Temperature is available',
        description:'Warning message for no valid plenum temperatures',
    }),
    'WARN_LOWPLENTEMP':defineMessage({
        id:'WARN_LOWPLENTEMP',
        defaultMessage:'Low Plenum Temperature Failure',
        description:'Warning message for low plenum temperature',
    }),
    'WARN_LOWPLENTEMP_BEE': defineMessage({
        id: 'WARN_LOWPLENTEMP_BEE',
        defaultMessage: 'Low Plenum Temperature Warning',
        description:'Warning message for low plenum temperature',
    }),
    'WARN_HIGHPLENTEMP':defineMessage({
        id:'WARN_HIGHPLENTEMP',
        defaultMessage:'High Plenum Temperature Failure',
        description:'Warning message for high plenum temperature',
    }),
    'WARN_HIGHPLENTEMP_BEE':defineMessage({
        id:'WARN_HIGHPLENTEMP_BEE',
        defaultMessage: 'High Plenum Temperature Warning',
        description: 'Warning message for high plenum temperature',
    }),
    'WARN_PLENSENSOR':defineMessage({
        id:'WARN_PLENSENSOR',
        defaultMessage:'Plenum Temperature Sensor Variance',
        description:'Warning message for plenum temperature variance',
    }),
    'WARN_RETURNAIRTEMP':defineMessage({
        id:'WARN_RETURNAIRTEMP',
        defaultMessage:'Invalid Return Air Temperature',
        description:'Warning message for invalid return air temperature',
    }),
    'WARN_AUXLOWPLENTEMP':defineMessage({
        id:'WARN_AUXLOWPLENTEMP',
        defaultMessage:'Auxiliary Low Plenum Temperature Failure',
        description:'Warning message for auxiliary low plenum temperature',
    }),
    'WARN_OUTTEMPSENSOR':defineMessage({
        id:'WARN_OUTTEMPSENSOR',
        defaultMessage:'Invalid Outside Air Temperature',
        description:'Warning message for invalid outside air temperature',
    }),
    'WARN_NO_STARTTEMP':defineMessage({
        id:"WARN_NO_STARTTEMP",
        defaultMessage:"The Cooling Available Temperature can't be calculated",
        description:'Warning message for cooling available temperature',
    }),
    'WARN_STARTTEMP':defineMessage({
        id:'WARN_STARTTEMP',
        defaultMessage:'Unable to calculate Start Temperature',
        description:'Warning message for no start temperature',
    }),
    'WARN_PLENHUMID':defineMessage({
        id:'WARN_PLENHUMID',
        defaultMessage:'Low Plenum Humidity',
        description:'Warning message for low plenum humidity',
    }),
    'WARN_INVALIDPLENHUMID':defineMessage({
        id:'WARN_INVALIDPLENHUMID',
        defaultMessage:'Invalid Plenum Humidity',
        description:'Warning message for invalid plenum humidity',
    }),
    'WARN_RETURNAIRHUMID':defineMessage({
        id:'WARN_RETURNAIRHUMID',
        defaultMessage:'Invalid Return Air Humidity',
        description:'Warning message for return air humidity',
    }),
    'WARN_OUTHUMIDSENSOR':defineMessage({
        id:'WARN_OUTHUMIDSENSOR',
        defaultMessage:'Invalid Outside Air Humidity',
        description:'Warning message for invalid outside air humidity',
    }),
    'WARN_OUTHUMIDVAR':defineMessage({
        id:'WARN_OUTHUMIDVAR',
        defaultMessage:'Outside Air Humidity Variance',
        description:'Warning message for outside air humidity variance',
    }),
    'WARN_INVALIDCO2':defineMessage({
        id:'WARN_INVALIDCO2',
        defaultMessage:'Invalid CO2 value',
        description:'Warning message for invalid co2 value',
    }),
    'WARN_AIRFLOW':defineMessage({
        id:'WARN_AIRFLOW',
        defaultMessage:'Air Flow Restriction Failure',
        description:'Warning message for air flow failure',
    }),
    'WARN_FAN':defineMessage({
        id:'WARN_FAN',
        defaultMessage:'Fan Failure',
        description:'Warning message for fan failure',
    }),
    'WARN_REFRIG_AS1':defineMessage({
        id:'WARN_REFRIG_AS1',
        defaultMessage:'Refrigeration',
        description:'Warning message for refrigeration',
    }),
    'WARN_REFRIG_PWM':defineMessage({
        id:'WARN_REFRIG_PWM',
        defaultMessage:'Refrigeration',
        description:'Warning message for refrigeration',
    }),
    'WARN_REFRIG_STAGE':defineMessage({
        id:'WARN_REFRIG_STAGE',
        defaultMessage:'Refrigeration Stage',
        description:'Warning message for regrigeration stage',
    }),
    'WARN_REFRIG_DEFROST':defineMessage({
        id:'WARN_REFRIG_DEFROST',
        defaultMessage:'Refrigeration Defrost',
        description:'Warning message for refrigeration defrost',
    }),
    'WARN_CLIMACELL':defineMessage({
        id:'WARN_CLIMACELL',
        defaultMessage:'ClimaCell',
        description:'Warning message for climacell',
    }),
    'WARN_HUMIDIFIER':defineMessage({
        id:'WARN_HUMIDIFIER',
        defaultMessage:'Humidifier',
        description:'Warning message for humidifier',
    }),
    'WARN_HUMID1_AS1':defineMessage({
        id:'WARN_HUMID1_AS1',
        defaultMessage:'Humidifier 1',
        description:'Warning message for humidifier 1',
    }),
    'WARN_HUMID2_AS1':defineMessage({
        id:'WARN_HUMID2_AS1',
        defaultMessage:'Humidifier 2',
        description:'Warning message for humidifier 2',
    }),
    'WARN_HIGHCO2':defineMessage({
        id:'WARN_HIGHCO2',
        defaultMessage:'High CO2 Level',
        description:'Warning message for high co2 level',
    }),
    'WARN_AUX':defineMessage({
        id:'WARN_AUX',
        defaultMessage:'Auxiliary',
        description:'Warning message for auxiliary',
    }),
    'WARN_AUX_AS1':defineMessage({
        id:'WARN_AUX_AS1',
        defaultMessage:'Auxiliary',
        description:'Warning message for auxiliary',
    }),
    'WARN_AUX1_AS1':defineMessage({
        id:'WARN_AUX1_AS1',
        defaultMessage:'Auxiliary 1',
        description:'Warning message for auxiliary 1',
    }),
    'WARN_AUX2_AS1':defineMessage({
        id:'WARN_AUX2_AS1',
        defaultMessage:'Auxiliary 2',
        description:'Warning message for auxiliary 2',
    }),
    'WARN_HEAT':defineMessage({
        id:'WARN_HEAT',
        defaultMessage:'Heat',
        description:'Warning message for heat',
    }),
    'WARN_CAVITYHEAT':defineMessage({
        id:'WARN_CAVITYHEAT',
        defaultMessage:'Cavity Heater / Pile Fan',
        description:'Warning message for cavity heater',
    }),
    'WARN_CAVHEATCALC':defineMessage({
        id:'WARN_CAVHEATCALC',
        defaultMessage:'Unable to calculate Cavity Heat / Pile Fan Differential - invalid reference temperature',
        description:'Warning message for no cavity heat calculation',
    }),
    'WARN_BURNER':defineMessage({
        id:'WARN_BURNER',
        defaultMessage:'Burner',
        description:'Warning message for burner',
    }),
    'WARN_POWER':defineMessage({
        id:'WARN_POWER',
        defaultMessage:'System power Failure',
        description:'Warning message for system power',
    }),
    'WARN_REMOTESTANDBY':defineMessage({
        id:'WARN_REMOTESTANDBY',
        defaultMessage:'System in Remote Standby',
        description:'Warning message for system in remote standby',
    }),
    'WARN_REFRIGSTANDBY':defineMessage({
        id:'WARN_REFRIGSTANDBY',
        defaultMessage:'System in Refrigeration Standby',
        description:'Warning message for system in refrigeration standby',
    }),
    'WARN_NOBROADCAST':defineMessage({
        id:'WARN_NOBROADCAST',
        defaultMessage:'This controller is configured as a Slave and has not received a Master broadcast in 10 minutes',
        description:'Warning message for no master broadcast',
    }),
    'WARN_SLAVENOBROADCAST':defineMessage({
        id:'WARN_SLAVENOBROADCAST',
        defaultMessage:'Slave using local Outside Temperature and Humidity until communication with Master restored',
        description:'Warning message for slave using local sensors',
    }),
    'WARN_TIMERESET':defineMessage({
        id:'WARN_TIMERESET',
        defaultMessage:'Invalid system Date/Time - system clock reset to 12:00pm (check system battery)',
        description:'Warning message for system clock reset',
    }),
    'WARN_DATETIME':defineMessage({
        id:'WARN_DATETIME',
        defaultMessage:'Invalid system Date/Time (check system battery)',
        description:'Warning message for check system battery',
    }),
    'WARN_UI':defineMessage({
        id:'WARN_UI',
        defaultMessage:'User interface did not respond (check Lantronix)',
        description:'Warning message for user interface did not respond',
    }),
    'WARN_ARMCOMM':defineMessage({
        id:'WARN_ARMCOMM',
        defaultMessage:'System controller is not responding',
        description:'Warning message for system controller not responding',
    }),
    'WARN_MODECHANGE':defineMessage({
        id:'WARN_MODECHANGE',
        defaultMessage:'System Mode changed to',
        description:'Warning message for system mode changed',
    }),
    'WARN_LOADLOG_CLEAR':defineMessage({
        id:'WARN_LOADLOG_CLEAR',
        defaultMessage:'Loading Monitor Data Log Cleared',
        description:'Warning message for loading monitor log cleared',
    }),
    'WARN_LOADMON_BAY1':defineMessage({
        id:'WARN_LOADMON_BAY1',
        defaultMessage:'Loading Monitor High Temperature',
        description:'Warning message for loading monitor high temperature',
    }),
    'WARN_LOADMON_BAY2':defineMessage({
        id:'WARN_LOADMON_BAY2',
        defaultMessage:'Loading Monitor Low Temperature',
        description:'Warning message for loading monitor low temperature',
    }),
    'WARN_SYSCONFIG_EQ':defineMessage({
        id:'WARN_SYSCONFIG_EQ',
        defaultMessage:'I/O configuration error',
        description:'Warning message for i/o configuration',
    }),
    'WARN_NO_OUTPUT':defineMessage({
        id:'WARN_NO_OUTPUT',
        defaultMessage:'Mode configuration error',
        description:'Warning message for mode configuration',
    }),
    'WARN_EXPANSIONBOARD':defineMessage({
        id:'WARN_EXPANSIONBOARD',
        defaultMessage:'I/O Expansion board removed',
        description:'Warning message for i/o expansion board removed',
    }),

    // secondary
    'WARN_NEWBOARD':defineMessage({
        id:'WARN_NEWBOARD',
        defaultMessage:'New Analog Board detected',
        description:'Warning message for new analog board detected',
    }),
    'WARN_BOARDREMOVED':defineMessage({
        id:'WARN_BOARDREMOVED',
        defaultMessage:'Analog Board removed',
        description:'Warning message for analog board removed',
    }),
    'WARN_COMMERR':defineMessage({
        id:'WARN_COMMERR',
        defaultMessage:'Analog Board communication error',
        description:'Warning message for analog board communication',
    }),
    'WARN_DEFAULTTEMP':defineMessage({
        id:'WARN_DEFAULTTEMP',
        defaultMessage:'Default Temperature Board missing',
        description:'Warning message for default temperature board missing',
    }),
    'WARN_BOARDNOTTEMP':defineMessage({
        id:'WARN_BOARDNOTTEMP',
        defaultMessage:'Analog Board 1 is not a temperature board',
        description:'Warning message for analog board 1 is not temperature board',
    }),
    'WARN_BOARDNOTHUMID':defineMessage({
        id:'WARN_BOARDNOTHUMID',
        defaultMessage:'Analog Board 2 is not a humidity board',
        description:'Warning message for analog board 2 is not humidity board',
    }),
    'WARN_DEFTEMPDIS':defineMessage({
        id:'WARN_DEFTEMPDIS',
        defaultMessage:'Default Temperature Board disabled',
        description:'Warning message for default temperature board is disabled',
    }),
    'WARN_DEFHUMDIS':defineMessage({
        id:'WARN_DEFHUMDIS',
        defaultMessage:'Default Humidity Board disabled',
        description:'Warning message for default humidity board disabled',
    }),
    'WARN_DATALOGWRITE':defineMessage({
        id:'WARN_DATALOGWRITE',
        defaultMessage:'Failure writing to History Log (check SD Card)',
        description:'Warning message for writing to history log',
    }),
    'WARN_DATALOGREAD':defineMessage({
        id:'WARN_DATALOGREAD',
        defaultMessage:'Failure reading from History Log (check SD Card)',
        description:'Warning message for reading from history log',
    }),
    'WARN_DATALOGFULL':defineMessage({
        id:'WARN_DATALOGFULL',
        defaultMessage:'History Log (SD Card) is full - set card to overwrite old records or insert new card',
        description:'Warning message for sd card full',
    }),
    'WARN_USERLOG_CLEAR':defineMessage({
        id:'WARN_USERLOG_CLEAR',
        defaultMessage:'History Log cleared',
        description:'Warning message for history log cleared',
    }),
    'WARN_ACTLOGWRITE':defineMessage({
        id:'WARN_ACTLOGWRITE',
        defaultMessage:'Failure writing to Activity Log (check SD Card)',
        description:'Warning message for writing to activity log',
    }),
    'WARN_ACTLOGREAD':defineMessage({
        id:'WARN_ACTLOGREAD',
        defaultMessage:'Failure reading from Activity Log (check SD Card)',
        description:'Warning message for reading from activity log',
    }),
    'WARN_ACTLOG_CLEAR':defineMessage({
        id:'WARN_ACTLOG_CLEAR',
        defaultMessage:'Activity Log cleared',
        description:'Warning message for activity log cleared',
    }),
    'WARN_SDCARD':defineMessage({
        id:'WARN_SDCARD',
        defaultMessage:'Unable to access SD Card',
        description:'Warning message for unable to access SD card',
    }),
    'WARN_SDCARD_DIFF':defineMessage({
        id:'WARN_SDCARD_DIFF',
        defaultMessage:'Different SD Card detected - continuing to use',
        description:'Warning message for different SD card',
    }),
    'WARN_SDCARD_INCOMPAT':defineMessage({
        id:'WARN_SDCARD_INCOMPAT',
        defaultMessage:'Incompatible SD Card detected - SD Card will be erased',
        description:'Warning message for incompatible sd card',
    }),
    'WARN_SDCARD_INIT':defineMessage({
        id:'WARN_SDCARD_INIT',
        defaultMessage:'SD Card initialized',
        description:'Warning message for sd card initialized',
    }),
    'WARN_SDCARD_UNINIT':defineMessage({
        id:'WARN_SDCARD_UNINIT',
        defaultMessage:'Unable to initialize SD Card',
        description:'Warning message for unable to initialize sd card',
    }),
    'WARN_SDCARD_LOCKED':defineMessage({
        id:'WARN_SDCARD_LOCKED',
        defaultMessage:'Unable to access SD Card - SD Card is locked (write protected)',
        description:'Warning message for unable to access sd card',
    }),
    'WARN_SDCARD_NONE':defineMessage({
        id:'WARN_SDCARD_NONE',
        defaultMessage:'SD Card is not inserted - please insert system SD Card',
        description:'Warning message for sd card not inserted',
    }),
    'WARN_SAVESETTINGS':defineMessage({
        id:'WARN_SAVESETTINGS',
        defaultMessage:'Unable to read or save System Settings',
        description:'Warning message for unable to read/write system settings',
    }),
    'WARN_FACTORYDEFAULT':defineMessage({
        id:'WARN_FACTORYDEFAULT',
        defaultMessage:'System Settings restored to Factory Default',
        description:'Warning message for factory default',
    }),
    'WARN_RTCACCESS':defineMessage({
        id:'WARN_RTCACCESS',
        defaultMessage:'Unable to access system clock',
        description:'Warning message for unable to access system clock',
    }),
    'WARN_EEPROMACCESS':defineMessage({
        id:'WARN_EEPROMACCESS',
        defaultMessage:'Unable to access system memory (EEPROM)',
        description:'Warning message for unable to access system memory',
    }),
    'WARN_MALLOC':defineMessage({
        id:'WARN_MALLOC',
        defaultMessage:'Unable to allocate memory for message (ARM)',
        description:'Warning message for unable to allocate memory',
    }),
    'WARN_INVALIDDATETIME':defineMessage({
        id:'WARN_INVALIDDATETIME',
        defaultMessage:'Clock could not be set due to an invalid date or time value',
        description:'Warning message for invalid date or time value',
    }),
    'WARN_DSTSTART':defineMessage({
        id:'WARN_DSTSTART',
        defaultMessage:'Clock has been adjusted for Daylight Saving Time',
        description:'Warning message for adjusted for daylight saving time',
    }),
    'WARN_DSTSTOP':defineMessage({
        id:'WARN_DSTSTOP',
        defaultMessage:'Clock has been adjusted for Standard Time',
        description:'Warning message for adjusted for standard time',
    }),
    'WARN_SETTINGSCONVERT':defineMessage({
        id:'WARN_SETTINGSCONVERT',
        defaultMessage:'System Settings converted to new format',
        description:'Warning message for system settings converted to new format',
    }),
    'WARN_SETTINGSCNVRTERR':defineMessage({
        id:'WARN_SETTINGSCNVRTERR',
        defaultMessage:'Unable to convert System Settings to new format',
        description:'Warning message for unable to convert system settings',
    }),
    'WARN_SETTINGSSIZE':defineMessage({
        id:'WARN_SETTINGSSIZE',
        defaultMessage:'System Settings size exceeds allocated EEPROM space',
        description:'Warning message for system settings exceeds space',
    }),
    'WARN_VERSION':defineMessage({
        id:'WARN_VERSION',
        defaultMessage:'The controller software version does not match the web server software version',
        description:'Warning message for controller software does not match on web server',
    }),
    'WARN_FILEACCESS':defineMessage({
        id:'WARN_FILEACCESS',
        defaultMessage:'File download error - Unable to access storage device',
        description:'Warning message for unable to access storage device',
    }),
    'WARN_FILENAME':defineMessage({
        id:'WARN_FILENAME',
        defaultMessage:'File download error - No file name or no IP address supplied',
        description:'Warning message for no file name',
    }),
    'WARN_FILEWRITE':defineMessage({
        id:'WARN_FILEWRITE',
        defaultMessage:'File download error - Unable to write to storage device (may be full)',
        description:'Warning message for unable to write storage device',
    }),
    'WARN_SOFTWAREUPDATE':defineMessage({
        id:'WARN_SOFTWAREUPDATE',
        defaultMessage:'Software upgrade in process',
        description:'Warning message for software upgrade in process',
    }),
    'WARN_CLEARALERTS':defineMessage({
        id:'WARN_CLEARALERTS',
        defaultMessage:'Alarms cleared',
        description:'Warning message for alarms cleared',
    }),
    'WARN_LIGHTS':defineMessage({
        id:'WARN_LIGHTS',
        defaultMessage:'Lights',
        description:'Warning message for lights',
    }),
    'WARN_LIGHTS1_AS1':defineMessage({
        id:'WARN_LIGHTS1_AS1',
        defaultMessage:'Lights 1',
        description:'Warning message for lights 1',
    }),
    'WARN_LIGHTS2_AS1':defineMessage({
        id:'WARN_LIGHTS2_AS1',
        defaultMessage:'Lights 2',
        description:'Warning message for lights 2',
    }),
    'WARN_LOADLOG_FULL':defineMessage({
        id:'WARN_LOADLOG_FULL',
        defaultMessage:'Loading Monitor Log Full - Terminating data acquisition',
        description:'Warning message for loading monitor log full',
    }),
    WARN_LIGHTS_OFF: defineMessage({
        id:'WARN_LIGHTS_OFF',
        defaultMessage: 'Lights Auto Off',
        description: 'Warning that the lights automatically turned off'
    }),
    'WARN_ALARMS_FILE': defineMessage({
        id: 'WARN_ALARMS_FILE',
        defaultMessage: 'No alarms file available (languageAlarms.txt)',
        description: 'Warning message for no language alarms file',
    }),
    'WARN_EQUIPDESC_FILE': defineMessage({
        id: 'WARN_EQUIPDESC_FILE',
        defaultMessage: 'No equipment descriptions file available (languageEquipDesc.txt)',
        description: 'Warning message for no language equipment description file',
    }),
}

// export equip