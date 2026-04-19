import { Accordion, AccordionDetails, AccordionSummary, Box, Toolbar, Typography } from "@material-ui/core"
import { Settings } from "@material-ui/icons"
import { FormattedMessage, useIntl } from "react-intl"
import { agristar2GeneralSettingsTitle } from '../../../utilities/translationObjects'
import Agristar2FormPlenumSetpoints from "./Agristar2FormP1Plenum"
import Agristar2FormP1Co2Purge from "./Agristar2FormP1Co2Purge"
import Agristar2FormP1OutsideAir from "./Agristar2FormP1OutsideAir"
import AgristarFormP1FanSpeed from "./Agristar2FormP1FanSpeed"
import Agristar2FormP1FanBoost from "./Agristar2FormP1FanBoost"
import Agristar2FormPlenumTempDev from "./Agristar2FormP1TempDev";
import Agristar2FormRampRate from "./Agristar2FormP1RampRate";
import Agristar2FormHumidCtrl from "./Agristar2FormP1HumidCtrl";
import Agristar2FormP1Misc from "./Agristar2FormP1Misc";
import Agristar2FormP1Email from "./Agristar2FormP1Email";
import Agristar2FormAlert from "./Agristar2FormP1Alert";
import Agristar2FormP1RunTimes from "./Agristar2FormP1RunTimes"
import {
    extractAgristar2PayloadFromIoTClient, extractBeeModeFromAgristar2Payload,
    extractIoTClientVersion, extractOnionModeFromAgristar2Payload, isVersionAtLeast,
    selectSelectedIoTClient, extractPecanModeFromAgristar2Payload,
} from "../selectors"
import { useSelector } from "react-redux"
import Agristar2FormP1DateTime from "./Agristar2FormP1DateTime"
import Agristar2FormP1BayLights from "./Agristar2FormP1BayLights"

const CustomAccordion = (props) => {
    let summaryTitle = props.summaryTitle
    let summaryData = props.summaryData
    let expanded = props.expanded
    let children = props.children

    return(
        <Accordion 
            expanded={expanded}
            // className={classes.accordionRoot}
        >
            <AccordionSummary 
                style={{display:'flex', justifyContent:'space-between'}} 
            >
                <Typography style={{display:'flex', justifyContent:'space-between', width:'100%'}}>
                    {summaryTitle}
                    <span>
                        {summaryData}
                    </span>
                </Typography>
            </AccordionSummary>
            <AccordionDetails
                // className={classes.accordionDetails}
            >
                {children}
            </AccordionDetails>
        </Accordion>
    )
}


const Agristar2PageViewSettings = (props) => {
    const intl = useIntl()
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state))
    const payload = extractAgristar2PayloadFromIoTClient(iotClient);
    const IoTVersion = extractIoTClientVersion(payload);
    const is102plus = isVersionAtLeast(IoTVersion, 1, 0, 2);
    const is200plus = isVersionAtLeast(IoTVersion, 2, 0, 0);
    const onionMode = extractOnionModeFromAgristar2Payload(payload);
    const beeMode = extractBeeModeFromAgristar2Payload(payload);
    const pecanMode = extractPecanModeFromAgristar2Payload(payload);

    return(
        <>  
            <Toolbar variant='dense' style={{display:'flex', justifyContent:'space-between'}}>
                <Settings />
                <Typography variant='h5' align={'center'} >
                    {intl.formatMessage(agristar2GeneralSettingsTitle)}
                </Typography>
                <div></div>
            </Toolbar>
            <Box zIndex={'tooltip'}>

            {/* SUMMARY TITLE */}
                <CustomAccordion 
                    summaryTitle={
                        <FormattedMessage
                            id='p1Plenum[0].title'
                            defaultMessage='Plenum Setpoints'
                        />
                    }
                >
                    <Agristar2FormPlenumSetpoints />
                </CustomAccordion>

                {
                    is102plus &&
                    <CustomAccordion 
                        summaryTitle={
                            <FormattedMessage 
                                    id='p1FanRuntimes[0].title'
                                    defaultMessage='Fan Runtimes'
                                />
                        }
                    >
                        <Agristar2FormP1RunTimes />
                    </CustomAccordion>
                }

                { !pecanMode &&
                    <CustomAccordion 
                        summaryTitle={
                            <FormattedMessage 
                                    id='p1OutsideAir[0].title'
                                    defaultMessage='Outside Air Controls'
                                />
                        }
                    >
                        <Agristar2FormP1OutsideAir />
                    </CustomAccordion>
                }
                {
                    is102plus &&
                    <CustomAccordion
                        summaryTitle={
                            <FormattedMessage
                                id='p1PlenTempDev[0].title'
                                defaultMessage='Plenum Temperature Deviation Alarms'
                            />
                        }
                    >
                        <Agristar2FormPlenumTempDev />
                    </CustomAccordion>
                }

                <CustomAccordion
                    summaryTitle={
                        <FormattedMessage
                            id='p1FreqCtrl[0].title'
                            defaultMessage='Fan Speed Controls'
                        />
                    }
                >
                    <AgristarFormP1FanSpeed />
                </CustomAccordion>
                {
                    is200plus &&
                    <CustomAccordion
                        summaryTitle={
                            <FormattedMessage
                                id='level1.fanboost.fan-boost-control'
                                defaultMessage='Fan Boost Control'
                            />
                        }
                    >
                        <Agristar2FormP1FanBoost />
                    </CustomAccordion>
                }
                {
                    is102plus &&
                    <>
                    <CustomAccordion
                        summaryTitle={
                            <FormattedMessage
                                id='p1RampRate[0].title'
                                defaultMessage='Plenum Setpoint Ramp Rate'
                            />
                        }
                    >
                        <Agristar2FormRampRate />
                    </CustomAccordion>
                    {!onionMode && !beeMode &&
                        <CustomAccordion
                            summaryTitle={
                                <FormattedMessage
                                    id='p1HumidCtrl[0].title'
                                    defaultMessage='Humidifier Control'
                                />
                            }
                        >
                            <Agristar2FormHumidCtrl />
                        </CustomAccordion>
                    }
                    </>
                }
                <CustomAccordion 
                    summaryTitle={
                        <span>
                            <FormattedMessage 
                                id='p1Co2Purge[0].high'
                                defaultMessage='High'
                            />
                            &nbsp;
                            CO<sub>2</sub>
                            &nbsp;
                            <FormattedMessage 
                                id='p1Co2Purge[1].purge'
                                defaultMessage='Level Purge Control'
                            />
                        </span>
                    }
                >
                    <Agristar2FormP1Co2Purge />
                </CustomAccordion>
                {
                    is200plus &&
                    <CustomAccordion
                        summaryTitle={
                            <FormattedMessage
                                id="bay-light-control"
                                defaultMessage='Bay Light Control'
                            />
                        }
                    >
                        <Agristar2FormP1BayLights />
                    </CustomAccordion>
                }
                {
                    is102plus &&
                    <>
                    <CustomAccordion
                        summaryTitle={
                            <FormattedMessage
                                id='p1Misc[0].title'
                                defaultMessage='Miscellaneous Program Parameters'
                            />
                        }
                    >
                        <Agristar2FormP1Misc />
                    </CustomAccordion>
                    <CustomAccordion
                        summaryTitle={
                            <FormattedMessage
                                id='p1Comm[0].title'
                                defaultMessage='Email Alerts'
                            />
                        }
                    >
                        <Agristar2FormP1Email />
                    </CustomAccordion>
                    <CustomAccordion
                        summaryTitle={
                            <FormattedMessage
                                id='p1AlertSetup[0].title'
                                defaultMessage='Select Email Alerts'
                            />
                        }
                    >
                        <Agristar2FormAlert />
                    </CustomAccordion>
                    <CustomAccordion
                        summaryTitle={
                            <FormattedMessage
                                id='p1DateTime[0].title'
                                defaultMessage='Set Date/Time'
                            />
                        }
                    >
                        <Agristar2FormP1DateTime />
                    </CustomAccordion>
                    </>
                }
            </Box>
        </>
    )
}
export default Agristar2PageViewSettings