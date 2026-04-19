import { CircularProgress, Typography } from "@material-ui/core";
import { AccessTime, SystemUpdateAlt } from "@material-ui/icons";
import { memo, useEffect, useState } from 'react';
import { useIntl } from "react-intl";
import { useHistory } from "react-router-dom"
import { AgristarMode } from "../../../common/Icons";
import { appURLs } from '../../../utilities/appNavigation';
import { UnitsComponent, useMeasurementSystem } from "../../../utilities/appUnitsOfMeasurements";
import { categoryToColorMap, modeToCategoryMap } from "../../../utilities/dataMapAgristar2";
import { agristar2Burner, agristar2Cooling, agristar2Fan, agristar2OutsideAir, agristar2Plenum, agristar2Refrigeration, agristar2ReturnAir, siteListDeviceHasNoLastLogMSG } from '../../../utilities/translationObjects';
import Agristar2DataLineDisplay from "./Agristar2DataLineDisplay";
import {
    extractAgristar2ListItemDataFromIoTClient,
    selectUpgrade,
    extractPermissionsFromIoTClient,
    extractAgristar2PwmOutputFromIoTClient,
    extractAgristar2IONamesFromIoTClient,
    isVersionAtLeast,
} from "../selectors";
import '../../../app/App.css'
import TimePassedLabel from "../../../common/TimePassLabel";
import AlarmsPopupAndIconButton from "./AlarmsPopupAndIconButton";
import Agristar2ButtonUnsecuredIp from "./Agristar2ButtonUnsecuredIp";
import { useSelector } from "react-redux";
import { useValidateData } from "../../../common/Hooks";
import { FormattedMessage } from 'react-intl';
import useLineItemCustomColor from "../hooks/LineItemCustomColor";
import isUpgradeable from "../../../common/version";
import { isValidSensor } from "../../../utilities/dataValidations";

const IoTClientListItemAgristar2 = memo((props) => {
    // ---------------------PROPS----------------------------------
    const [iotClient, setIoTClient] = useState(props.iotClient)

    // ---------------------HOOKS----------------------------------
    const intl = useIntl();

    const history = useHistory()
    const navHandler = (destination) => {
        history.push(destination)
    }

    // ---------------------SELECTORS/EXTRACTORS-------------------
    const prefSystemOfMeasurement = useMeasurementSystem()
    const [agristar2Data, setAgristar2Data] = useState(extractAgristar2ListItemDataFromIoTClient(props.iotClient, prefSystemOfMeasurement))
    const clientVersion = agristar2Data.iot_client_version;
    const is200plus = isVersionAtLeast(clientVersion, 2, 0, 0);
    const upgrade = useSelector((state)=>selectUpgrade(state));
    const [staleData, networkError] = useValidateData(iotClient.id, 20);
    const beeMode = agristar2Data.system_mode === '2' && isVersionAtLeast(clientVersion, 1, 0, 3);
    const potatoMode = agristar2Data.system_mode === '0';
    const cureMode = agristar2Data.system_mode === '1' &&
        agristar2Data.cure_output === '1' && agristar2Data.cure_remote !== '1';
    const obj_permissions = extractPermissionsFromIoTClient(iotClient);
    const isAuthorized = obj_permissions?.['agristar2_action_level1'];
    const { onionColor, humidColor, outsideColor, returnHumidColor, co2Color } = useLineItemCustomColor(agristar2Data);
    const [refrigerationDefined, setRefrigerationDefined] = useState(false);
    const pwmOutput = extractAgristar2PwmOutputFromIoTClient(iotClient);
    const io = extractAgristar2IONamesFromIoTClient(iotClient);

    useEffect(() => {
        setIoTClient(props.iotClient);
        setAgristar2Data(extractAgristar2ListItemDataFromIoTClient(props.iotClient, prefSystemOfMeasurement));
    }, [props.iotClient, prefSystemOfMeasurement])

    useEffect(() => {
        setRefrigerationDefined(pwmOutput?.[1][2] !== '-1' || io.OutputConfig[13] !== '-1');
    }, [io, pwmOutput])

    // ---------------------MODE COLOR-----------------------
    const getModeCategoryColor = (mode) => categoryToColorMap[modeToCategoryMap[mode]]

    // ---------------------NAVIGATION HANDLER---------------------
    const onClickHandler = () => {
        if (iotClient.front_matter !== null) {
            navHandler(appURLs.deviceURLs.deviceView(iotClient.id))
        }
    }

    return(
        iotClient.status === 'pending'
        ?
        <div style={{width:'100%', display:'flex', justifyContent:'center', marginBottom: '5px'}}>
            <CircularProgress />
        </div>
        :
        <div 
            style={{display:'flex', // justifyContent:'space-between', 
                '--color-1': getModeCategoryColor(agristar2Data?.current_mode, agristar2Data?.time_stamp) || 'lightgrey',
                '--color-2': (staleData || networkError) ? '#ff9f9f' : getModeCategoryColor(agristar2Data?.current_mode, agristar2Data?.time_stamp) || 'lightgrey',
                borderRadius:'3px', padding: '5px', alignItems:'center', 
                margin:'0px -6px 2px -6px',
                background: `linear-gradient(315deg, var(--color-2), var(--color-1))`
            }}
        >
                <>
                    <div
                        style={{width:'100%'}}
                    >
                        <div style={{display:'flex', justifyContent:'space-between'}}>
                            <div 
                                style={{width:'100%', display:'flex'}} 
                                onClick={()=>onClickHandler()}
                            >
                                <Agristar2ButtonUnsecuredIp 
                                    unsecured_ip={agristar2Data.unsecured_ip} 
                                />

                                <div style={{width:'100%', display:'flex', justifyContent:'space-between'}}>
                                    <Typography variant='body1' style={{marginLeft:'3px', fontWeight:'600'}}>
                                        <i>
                                            {agristar2Data.device_name}
                                        </i>
                                    </Typography>
                                    <div style={{ display:'flex', float: 'right'}}>
                                        {
                                            clientVersion &&
                                            <Typography variant='body2' style={{marginRight:'3px'}}>
                                                <i>v{clientVersion}</i>
                                            </Typography>
                                        }
                                        {
                                            upgrade && clientVersion && clientVersion !== '1.0.0' && clientVersion !== '1.0.1-rc1' &&
                                            isUpgradeable(clientVersion, upgrade.version) &&
                                            <SystemUpdateAlt fontSize='small' style={{fontSize:'1rem', marginTop:'3px', marginRight:'5px'}} />
                                        }
                                        {
                                            staleData && !networkError &&
                                            <Typography variant='body1' style={{color: 'darkred', marginRight:'3px', fontWeight:'600'}}>
                                                <FormattedMessage 
                                                    id='agristar2.client-list-item.stale'
                                                    defaultMessage='Stale Data'
                                                    description='Need to refresh data'
                                                />
                                            </Typography>
                                        }
                                        {
                                            networkError &&
                                            <Typography variant='body1' style={{color: 'darkred', marginRight:'3px', fontWeight:'600'}}>
                                                <FormattedMessage 
                                                    id='agristar2.client-list-item.network'
                                                    defaultMessage='Network Error'
                                                    description='Unable to get data after 30 minutes'
                                                />
                                            </Typography>
                                        }
                                        <AccessTime fontSize='small' style={{fontSize:'1rem', marginTop:'3px', color: (staleData || networkError) ? 'darkred' : 'black'}}/>
                                        <TimePassedLabel
                                            dateTime={agristar2Data.time_stamp}
                                            style={{margin:'1px 3px 5px 3px', fontStyle:'italic', color: (staleData || networkError) ? 'darkred' : 'black'}}
                                        />
                                    </div>
                                </div>
                            </div>

                            <AlarmsPopupAndIconButton 
                                iotclient={iotClient}
                                alarmData={agristar2Data.alarm_data}
                                isAuthorized={isAuthorized}
                            />

                        </div>
                        <div style={{ display:'flex', margin:'auto'}}>
                        {
                            !agristar2Data || iotClient.front_matter === null
                            ? 
                                <Typography noWrap={true} style={{margin:'auto'}} > 
                                    <i>{intl.formatMessage(siteListDeviceHasNoLastLogMSG)}</i> 
                                </Typography>
                            :
                                <div style={{display:'flex', flexFlow:'column', width:'100%'}} onClick={()=>onClickHandler()}
                                >
                                    <div
                                        style={{
                                            display:'grid',
                                            gridTemplateColumns: '4fr 4fr 22fr',
                                            padding:'0px 0px 5px'
                                        }}
                                    >
                                        <div style={{margin:'0px', maxHeight:'25px'}}>
                                            <AgristarMode mode={agristar2Data?.current_mode}/>
                                        </div>
                                        <Typography variant="body2" style={{marginTop:'5px',marginRight:'2px',textAlign:'right'}}>
                                            <span style={{fontWeight: 'bold'}}>{intl.formatMessage(agristar2Plenum)}:</span>
                                        </Typography>
                                        <div style={{ display: 'flex', flexFlow: 'row', justifyContent: 'space-evenly' }}>
                                            <Agristar2DataLineDisplay 
                                                dataUnit={<UnitsComponent typeOfData={'temperature'} />}
                                                actual={agristar2Data?.plenum_temp}
                                                setPoint={cureMode ? agristar2Data?.cure_start_temp : agristar2Data?.plenum_temp_set}
                                                style={{marginTop:'5px'}}
                                                customColor={onionColor}
                                                setPoint2={agristar2Data?.plenum_temp_set2}
                                                customColor2={'darkblue'}
                                                showSetPoint2={beeMode && refrigerationDefined}
                                            />
                                            <Agristar2DataLineDisplay 
                                                dataUnit={<UnitsComponent typeOfData={'relativeHumidity'}/>}
                                                actual={agristar2Data?.plenum_humid}
                                                setPoint={cureMode ? agristar2Data?.cure_start_humid : agristar2Data?.plenum_humid_set}
                                                style={{marginTop:'5px'}}
                                                customColor={humidColor}
                                            />
                                        </div>
                                    </div>
                                    <div
                                        style={{
                                            display:'grid',
                                            gridTemplateColumns: '4fr 4fr 20fr',
                                            padding:'0px 0px 5px'
                                        }}
                                    >
                                        <Typography variant="body2" style={{textAlign: 'right', marginRight: '2px'}}>
                                            <span style={{fontWeight: 'bold'}}>{intl.formatMessage(agristar2Fan)}:</span>
                                        </Typography>
                                        <Typography variant="body2">
                                            {
                                                !isNaN(agristar2Data?.fan_speed)
                                                ? agristar2Data?.fan_speed // show speed if number
                                                : agristar2Data?.fan_speed === 'Manual'
                                                    ? '100' // show 100% if in 'Manual'
                                                    : '0' // show '0%' if 'Off' or anything else
                                            }%
                                        </Typography>
                                        <div style={{ display: 'flex', flexFlow: 'row', width: '100%'}}>
                                            <div style={{display:'grid', gridTemplateColumns: '8fr 10fr 10fr', width: '100%'}}>
                                                <Typography variant="body2" style={{marginRight: '2px', textAlign: 'right' }}>
                                                    <span style={{fontWeight: 'bold'}}>{intl.formatMessage(agristar2ReturnAir)}:</span>
                                                </Typography>
                                                <Typography variant="body2" style={{textAlign: 'center'}}>
                                                    {agristar2Data?.return_temp} <UnitsComponent typeOfData="temperature"/>
                                                    {
                                                        isValidSensor(agristar2Data?.return_temp2) &&
                                                        <span> | {agristar2Data?.return_temp2} <UnitsComponent typeOfData="temperature" /></span>
                                                    }
                                                </Typography>
                                                <Typography variant="body2" style={{textAlign: 'center', color: {returnHumidColor}}}>
                                                    {agristar2Data?.return_humid} <UnitsComponent typeOfData="relativeHumidity"/>
                                                    {
                                                        isValidSensor(agristar2Data?.return_humid2) &&
                                                        <span> | {agristar2Data?.return_humid2} <UnitsComponent typeOfData="relativeHumidity" /></span>
                                                    }
                                                </Typography>
                                            </div>
                                        </div>
                                        <Typography variant="body2" style={{textAlign: 'right', marginRight: '2px'}}>
                                            <span style={{fontWeight: 'bold'}}>
                                            {
                                                cureMode
                                                ? `${intl.formatMessage(agristar2Burner)}:`
                                                : modeToCategoryMap[agristar2Data?.current_mode] === 'refrigeration'
                                                    ? `${intl.formatMessage(agristar2Refrigeration)}:`
                                                    : `${intl.formatMessage(agristar2Cooling)}:`
                                            }
                                            </span>
                                        </Typography>
                                        <Typography variant="body2">
                                            {
                                                cureMode
                                                ? `${agristar2Data?.burner_output}%`
                                                : modeToCategoryMap[agristar2Data?.current_mode] === 'refrigeration'
                                                    ? `${agristar2Data?.refrig_value}%`
                                                    : `${agristar2Data?.door_value}%`
                                            }
                                        </Typography>
                                        <div style={{ display: 'flex', flexFlow: 'row', width: '100%'}}>
                                            <div style={{display:'grid', gridTemplateColumns: '8fr 10fr 10fr', width: '100%'}}>
                                                <Typography variant="body2" style={{ marginRight: '2px', textAlign: 'right' }}>
                                                    <span style={{fontWeight: 'bold'}}>{intl.formatMessage(agristar2OutsideAir)}:</span>
                                                </Typography>
                                                <Typography variant="body2" style={{textAlign: 'center', color: outsideColor}}>
                                                    {agristar2Data?.outside_temp} <UnitsComponent typeOfData="temperature" />
                                                </Typography>
                                                <Typography variant="body2" style={{textAlign: 'center'}}>
                                                    {agristar2Data?.outside_humid} <UnitsComponent typeOfData="relativeHumidity" />
                                                </Typography>
                                            </div>
                                        </div>
                                    </div>
                                    <div style={{display: 'grid', gridTemplateColumns: (isValidSensor(agristar2Data?.co2_2) || isValidSensor(agristar2Data?.return_temp2)) ? '2fr 6fr 4fr 4fr' : '2fr 2fr 3fr 1fr'}}>
                                        <Typography variant="body2" style={{textAlign: 'right'}}>
                                            <span style={{fontWeight: 'bold'}}>
                                                <FormattedMessage id="agristar2.iotsite-co2" defaultMessage="CO2"/>:
                                            </span>
                                        </Typography>
                                        <Typography variant="body2" style={{textAlign: 'left', marginLeft: '4px', color: co2Color}}>
                                            {agristar2Data?.co2} <UnitsComponent typeOfData="ppm" />
                                            {
                                                isValidSensor(agristar2Data?.co2_2) &&
                                                <span> | {agristar2Data?.co2_2} <UnitsComponent typeOfData="ppm" /></span>
                                            }
                                        </Typography>
                                        <Typography variant="body2" style={{textAlign: 'right'}}>
                                            <span style={{fontWeight: 'bold'}}>
                                                <FormattedMessage id="agristar2.iotsite-returnvsplenum" defaultMessage="Return vs Plenum:"/>
                                            </span>
                                        </Typography>
                                        { !isNaN(agristar2Data?.return_temp - agristar2Data?.plenum_temp) &&
                                            <Typography variant="body2" style={{textAlign: 'left', marginLeft: '4px'}}>
                                                {(agristar2Data?.return_temp - agristar2Data?.plenum_temp).toFixed(1)}&nbsp;&deg;
                                                {
                                                    isValidSensor(agristar2Data?.return_temp2) &&
                                                    <span> | {(agristar2Data?.return_temp2 - agristar2Data?.plenum_temp).toFixed(1)}&nbsp;&deg;</span>
                                                }
                                            </Typography>
                                        }
                                        { agristar2Data?.pileAvg !== undefined && agristar2Data?.pileAvg*1 > 0 &&
                                            <>
                                                <Typography variant="body2" style={{textAlign: 'right'}}>
                                                    <span style={{fontWeight: 'bold'}}>
                                                        <FormattedMessage id="agristar2.iotsite-pileavg" defaultMessage="Pile Avg:"/>
                                                    </span>
                                                </Typography>
                                                <Typography variant="body2" style={{textAlign: 'left', marginLeft: '4px'}}>
                                                        {parseFloat(agristar2Data?.pileAvg).toFixed(1)} <UnitsComponent typeOfData="temperature" />
                                                </Typography>
                                            </>
                                        }
                                        {
                                            is200plus && potatoMode && (isValidSensor(agristar2Data?.mli1) || isValidSensor(agristar2Data?.mli2)) &&
                                            <>
                                                <Typography variant="body2" style={{textAlign: 'right'}}>
                                                    <span style={{fontWeight: 'bold'}}>
                                                        <FormattedMessage id="agristar2.moisture-loss" defaultMessage="Moisture Loss"/>
                                                    </span>
                                                </Typography>
                                                {
                                                    isValidSensor(agristar2Data?.mli1) &&
                                                    <Typography variant="body2" style={{textAlign: 'left', marginLeft: '4px'}}>
                                                        <span>{agristar2Data.mli1} <UnitsComponent typeOfData="mi" /></span>
                                                        {
                                                            isValidSensor(agristar2Data?.mli2) &&
                                                            <span> | {agristar2Data.mli2} <UnitsComponent typeOfData="mi" /></span>
                                                        }
                                                    </Typography>
                                                }
                                            </>
                                        }
                                    </div>
                                </div>
                        }
                        </div>
                    </div>
                </>
        </div>
    );
})

export default IoTClientListItemAgristar2;
