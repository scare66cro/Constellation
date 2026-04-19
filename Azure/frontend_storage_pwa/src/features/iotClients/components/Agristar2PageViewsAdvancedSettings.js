import { Accordion, AccordionDetails, AccordionSummary, Box, Toolbar, Typography } from "@material-ui/core"
import { Settings } from "@material-ui/icons"
import { FormattedMessage, useIntl } from "react-intl"
import { agristar2AdvancedSettingsTitle } from '../../../utilities/translationObjects'
import Agristar2FormP2BasicSetup from "./Agristar2FormP2BasicSetup";
import Agristar2FormP2FreshAirSetup from "./Agristar2FormP2FreshAirSetup";
import Agristar2FormP2ClimaCellSetup from "./Agristar2FormP2ClimaCell";
import AgristarFormP2Analog from "./Agristar2FormP2Analog";
import AgristarFormP2IOConfig from "./Agristar2FormP2IOConfig";
import Agristar2FormP2Failures1 from "./Agristar2FormP2Failures1";
import Agristar2FormP2Failures2 from "./Agristar2FormP2Failures2";
import Agristar2FormP2LogSettings from './Agristar2FormP2LogSettings';
import Agristar2FormP2RefrigerationSetup from './Agristar2FormP2RefrigerationSetup';
import Agristar2FormP2PwmOutputsAS2 from './Agristar2FormP2PwmOutputsAS2';
import Agristar2FormP2AuxProg from './Agristar2FormP2AuxProg';
import Agristar2FormP2PIDLogs from "./Agristar2FormP2PIDLogs";
import { useSelector } from "react-redux";
import {
    extractAgristar2PayloadFromIoTClient, extractBoardTypeFromAgristarPayload,
    extractOnionModeFromAgristar2Payload, extractPecanModeFromAgristar2Payload,
    selectSelectedIoTClient,
} from "../selectors";
import Agristar1FormP2Failures1 from "./Agristar1FormP2Failures1";
import Agristar1FormP2PwmOutputs from "./Agristar1FormP2PWMOutputs";
import useAS2FormP2Failures1 from "../hooks/AS2FormP2Failures1";
import Agristar2FormP2Burner from "./Agristar2FormP2Burner";

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


const Agristar2PageViewAdvancedSettings = (props) => {
    const intl = useIntl()
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state));
    const payload = extractAgristar2PayloadFromIoTClient(iotClient);
    const boardType = extractBoardTypeFromAgristarPayload(payload);
    const outputConfig = payload?.['OutputConfigData'];
    const { noFan, noRefrig, noRefrigStage, noCavity, noLights,
            noClimacell, noHumid, noHeat, noAux } = useAS2FormP2Failures1();
    const onionMode = extractOnionModeFromAgristar2Payload(payload);
    const pecanMode = extractPecanModeFromAgristar2Payload(payload);

    return(
        <>  
            <Toolbar variant='dense' style={{display:'flex', justifyContent:'space-between'}}>
                <div style={{block:'inline'}}>
                    <Settings/>
                    <Settings color='secondary' fontSize='small' style={{marginLeft:'-.5rem', zIndex:'2'}}/>
                </div>
                <Typography variant='h5' align={'center'}>
                    {intl.formatMessage(agristar2AdvancedSettingsTitle)}
                </Typography>
                <div></div>
            </Toolbar>
            <Box zIndex={'tooltip'}>

            {/* SUMMARY TITLE */}
                <CustomAccordion 
                    summaryTitle={
                        <FormattedMessage
                            id='p2BasicSetup[0].title'
                            defaultMessage='Basic Setup'
                        />
                    }
                >
                    <Agristar2FormP2BasicSetup />
                </CustomAccordion>
                <CustomAccordion
                    summaryTitle={
                        <FormattedMessage
                            id='p2LogSettings[0].title'
                            defaultMessage='Log Settings'
                        />
                    }
                >
                    <Agristar2FormP2LogSettings />
                </CustomAccordion>
                
                <CustomAccordion
                    summaryTitle={
                        <FormattedMessage
                            id='p2AnalogBoardSetup[0].title'
                            defaultMessage='Analog Board Setup'
                        />
                    }
                >
                    <AgristarFormP2Analog />
                </CustomAccordion>

                { !pecanMode &&
                    <CustomAccordion
                        summaryTitle={
                            <FormattedMessage
                                id='p2FreshAirSetup[0].title'
                                defaultMessage='Fresh Air Door Setup'
                            />
                        }
                    >
                        <Agristar2FormP2FreshAirSetup />
                    </CustomAccordion>
                }
                <CustomAccordion
                    summaryTitle={
                        <FormattedMessage
                            id='p2Refrigeration[0].title'
                            defaultMessage='Refrigeration Setup'
                        />
                    }
                >
                    <Agristar2FormP2RefrigerationSetup />
                </CustomAccordion>
                <CustomAccordion
                    summaryTitle={
                        <FormattedMessage
                            id='p2PIDLogs[0].title'
                            defaultMessage='PID Value Logging'
                        />
                    }
                >
                    <Agristar2FormP2PIDLogs />
                </CustomAccordion>
                {
                (boardType !== 'AS2' || outputConfig[3] !== '-1') && !pecanMode && !onionMode &&
                <CustomAccordion
                    summaryTitle={
                        <FormattedMessage
                            id='p2Climacell[0].title'
                            defaultMessage='ClimaCell Setup'
                        />
                    }
                >
                    <Agristar2FormP2ClimaCellSetup />
                </CustomAccordion>
                }
                {
                    onionMode &&
                    <CustomAccordion
                        summaryTitle={
                            <FormattedMessage
                                id='p2Burner[0].title'
                                defaultMessage='Burner Setup'
                            />
                        }
                    >
                        <Agristar2FormP2Burner />
                    </CustomAccordion>
                }
                {
                    boardType !== 'AS1' &&
                    <CustomAccordion
                        summaryTitle={
                            <FormattedMessage
                                id='p2IoConfig[0].title'
                                defaultMessage='System I/O Configuration'
                            />
                        }
                    >
                        <AgristarFormP2IOConfig />
                    </CustomAccordion>
                }
                <CustomAccordion
                    summaryTitle={
                        <FormattedMessage
                            id='p2PwmOutputs[0].title'
                            defaultMessage='PWM Output Setup'
                        />
                    }
                >
                    {
                    boardType === 'AS2'
                        ? <Agristar2FormP2PwmOutputsAS2 />
                        : <Agristar1FormP2PwmOutputs />
                    }
                </CustomAccordion>
                {
                    boardType !== 'AS1' &&
                    <CustomAccordion
                        summaryTitle={
                            <FormattedMessage
                                id='p2AuxProg[0].title'
                                defaultMessage='Auxiliary Output Programming'
                            />
                        }
                    >
                        <Agristar2FormP2AuxProg />
                    </CustomAccordion>
                }
                {
                (boardType !== 'AS1' || !noFan || !noRefrig || !noRefrigStage || !noCavity ||
                    !noLights || !noClimacell || !noHumid || !noHeat || !noAux) &&
                <CustomAccordion
                    summaryTitle={
                        <FormattedMessage
                            id='p2FailuresSetup[0].title'
                            defaultMessage='Failures Setup - 1'
                        />
                    }
                >
                    {
                        boardType !== 'AS1'
                        ? <Agristar2FormP2Failures1 />
                        : <Agristar1FormP2Failures1 />
                    }
                </CustomAccordion>
                }
                <CustomAccordion
                    summaryTitle={
                        <FormattedMessage
                            id='p2FailuresSetup2[0].title'
                            defaultMessage='Failures Setup - 2'
                        />
                    }
                >
                    <Agristar2FormP2Failures2 />
                </CustomAccordion>
            </Box>
        </>
    )
}
export default Agristar2PageViewAdvancedSettings;