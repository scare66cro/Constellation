import { TableContainer, Table, TableBody, TableCell, TableHead, TableRow, Toolbar, Typography, Switch } from "@material-ui/core"
import { ToggleOffOutlined, ToggleOn } from "@material-ui/icons"
import { FormattedMessage, useIntl } from "react-intl"
import { useDispatch, useSelector } from "react-redux"
import { agristar2EquipmentStatusColumnPanelSwitchAuto, agristar2EquipmentStatusColumnPanelSwitchManual, agristar2EquipmentStatusColumnStatusDiagOff, agristar2EquipmentStatusColumnStatusDiagOn, agristar2EquipmentStatusColumnStatusOff, agristar2EquipmentStatusColumnStatusOn, agristar2EquipmentStatusColumnStatusRemoteOff, agristar2Refrigeration } from "../../../utilities/translationObjects"
import { postAgristar2Action } from "../actions"
import {
    extractAgristarEquipmentControlFromIoTClient, extractPermissionsFromIoTClient,
    selectSelectedIoTClient, selectSaving, extractAgristar2PayloadFromIoTClient,
    extractBoardTypeFromAgristarPayload, extractOnionModeFromAgristar2Payload, extractBeeModeFromAgristar2Payload,
} from "../selectors"
import SaveComponent from './SaveComponent';
import ButtonSave from '../../../common/ButtonSave';

const EquipmentRow = (props) => {
    // ---------------PROPS---------------------------------
    const iotClient = props.iotClient
    const isAuthorized = props.isAuthorized
    const exists = props.exists
    const equipmentNameTranslation = props.equipmentNameTranslation
    const equipmentRenamedAs = props.equipmentRenamedAs
    const equipmentStatus = props.equipmentStatus()
    const panelSwitchStatus = props.panelSwitchStatus() || '--'
    const remoteOff = props.remoteOff

    const remSwitchName = props.remSwitchName

    const outputColor = props.outputColor || 'lightgrey'
    const statusColor = props.statusColor || 'lightgrey'
    const panelSwitchColor = props.panelSwitchColor || 'lightgrey'

    // ---------------HOOKS--------------------------------
    const dispatch = useDispatch()

    // --------------REMOTE SWITCH HANDLER-------------------
    const handleRemoteSwitchAction = () => {
        dispatch(postAgristar2Action(
            iotClient, 
            {
                "tag":"EquipCtrlAction",
                "action":remSwitchName,
                "value": (remoteOff ? 'On' : 'Off') 
                    // remoteOff is true means current position is 'Off' so send the opposite to toggle it
            }
        ))
    }

    return(
        <>
            {
                exists ?

                <TableRow >
                    <TableCell padding='none' align="center" 
                        style={{padding:'2px', backgroundColor:(outputColor)}} 
                    >
                        {equipmentRenamedAs ? equipmentRenamedAs : equipmentNameTranslation}
                    </TableCell>
                    <TableCell padding='none' align="center" 
                        style={{padding:'2px', backgroundColor:statusColor}}
                    >
                        {equipmentStatus}
                    </TableCell>
                    <TableCell padding='none' align="center"
                        style={{padding:'2px', backgroundColor:panelSwitchColor}}
                    >
                        {panelSwitchStatus}
                    </TableCell>
                    <TableCell padding='none' align="center">
                        {
                            remSwitchName ?
                            <Switch color='primary' 
                                // if 'remoteOff' is false this means the remoteSwitch is set to equipment 'On'
                                checked={!remoteOff}
                                onChange={handleRemoteSwitchAction}
                                disabled={!isAuthorized}
                            />
                            : 
                            <></>
                        }
                    </TableCell>
                </TableRow>

                : null

            }
        </>
    )
}

const Agristar2PageViewEquipmentStatus = (props) => {

    // --------------HOOKS---------------------------
    const intl = useIntl()
    const dispatch = useDispatch()

    // --------------SELECTORS/EXTRACTORS---------------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state));
    const saving = useSelector((state) => selectSaving(state));
    const obj_permissions = extractPermissionsFromIoTClient(iotClient)
    const isAuthorized = obj_permissions?.['agristar2_action_level1']
    const equipmentStatuses = extractAgristarEquipmentControlFromIoTClient(iotClient);
    const payload = extractAgristar2PayloadFromIoTClient(iotClient);
    const onionMode = extractOnionModeFromAgristar2Payload(payload);
    const beeMode = extractBeeModeFromAgristar2Payload(payload);
    const boardType = extractBoardTypeFromAgristarPayload(payload);

    const fanObj = equipmentStatuses?.fan
    const climacellObj = equipmentStatuses?.climacell
    const heatObj = equipmentStatuses?.heat
    const cavityHeatObj = equipmentStatuses?.cavity_heat
    const burnerObj = equipmentStatuses?.burner;
    const cureObj = equipmentStatuses?.cure;
    const dehumidObj = equipmentStatuses?.dehumidifier;
    const humidifierObjs = [
        equipmentStatuses?.humidifier1Head,
        equipmentStatuses?.humidifier1Pump,
        equipmentStatuses?.humidifier2Head,
        equipmentStatuses?.humidifier2Pump,
        equipmentStatuses?.humidifier3Head,
        equipmentStatuses?.humidifier3Pump,
    ]
    const refrigObj = equipmentStatuses?.refrigeration
    const refrigStageObjs = [
        equipmentStatuses?.ref_stage1,
        equipmentStatuses?.ref_stage2,
        equipmentStatuses?.ref_stage3,
        equipmentStatuses?.ref_stage4,
        equipmentStatuses?.ref_stage5,
        equipmentStatuses?.ref_stage6,
        equipmentStatuses?.ref_stage7,
        equipmentStatuses?.ref_stage8,
    ]
    const defrostObjs = [
        equipmentStatuses?.defrost1,
        equipmentStatuses?.defrost2,
    ] 
    const auxiliaryObjs = [
        equipmentStatuses?.aux1,
        equipmentStatuses?.aux2,
        ...boardType === 'AS2'
            ? [
                equipmentStatuses?.aux3,
                equipmentStatuses?.aux4,
                equipmentStatuses?.aux5,
                equipmentStatuses?.aux6,
                equipmentStatuses?.aux7,
                equipmentStatuses?.aux8,
            ]
            : [],
    ]

    const getPanelSwitchStatus = (panel_switch) => {
        switch(panel_switch){
            case 'off':
                return intl.formatMessage(agristar2EquipmentStatusColumnStatusOff)
            case 'auto':
                return intl.formatMessage(agristar2EquipmentStatusColumnPanelSwitchAuto)
            case 'manual':
                return intl.formatMessage(agristar2EquipmentStatusColumnPanelSwitchManual)
            case undefined:
                    return '--'
            default:
                return '??'
        }
    }

    const getEquipmentStatusText = (status_text) => {
        switch(status_text){
            case 'on':
                return intl.formatMessage(agristar2EquipmentStatusColumnStatusOn)
            case 'off':
                return intl.formatMessage(agristar2EquipmentStatusColumnStatusOff)
            case 'remote off':
                return intl.formatMessage(agristar2EquipmentStatusColumnStatusRemoteOff)
            case 'diag on':
                return intl.formatMessage(agristar2EquipmentStatusColumnStatusDiagOn)
            case 'diag off':
                return intl.formatMessage(agristar2EquipmentStatusColumnStatusDiagOff)
            default:
                return '??'
        }
    }

    const clearDiagnostics = () => {
        dispatch(postAgristar2Action(iotClient, {
            tag: 'button2',
            ClearDiag: 'Clear',
        }));
    }

    return(
        <>
            <Toolbar variant='dense' style={{display:'flex', justifyContent:'space-between'}}>
                <ToggleOn style={{padding:'-5px', position:'absolute', top:'.5rem', marginTop:'-.4rem'}} />
                <ToggleOffOutlined style={{padding:'1px', position:'', marginTop:'1.1rem'}} />
                <Typography variant='h5' align={'center'} >
                    <FormattedMessage 
                        id='mnEquipStatus[0].title'
                        defaultMessage='Equipment Status'
                    />
                </Typography>
                <div></div>
            </Toolbar>

            <SaveComponent saving={saving?.EquipCtrlAction?.status}>
                <TableContainer style={{maxWidth:'100%'}}>
                    <Table >
                        <TableHead>
                            <TableRow>
                                <TableCell padding='none' align='center' style={{ padding:'5px', width:'45%'}}>
                                    <Typography variant='caption'>
                                        <FormattedMessage 
                                            id='mnEquipStatus[1].output'
                                            defaultMessage='Output'
                                        />
                                    </Typography>
                                </TableCell>
                                <TableCell padding='none' align="center" style={{ padding:'5px', width:'20%'}}>
                                    <Typography variant='caption'>
                                        <FormattedMessage 
                                            id='mnEquipStatus[2].status'
                                            defaultMessage='Status'
                                        />
                                    </Typography>
                                </TableCell>
                                <TableCell padding='none' align="center" style={{ padding:'5px', width:'15%'}}>
                                    <Typography variant='caption'>
                                        <FormattedMessage 
                                            id='agristar2.equipment-control-columnTitle-PanelSwitch'
                                            defaultMessage='Panel Switch'
                                            description='column title for Panel Switch on the equipment status/control page'
                                        />
                                    </Typography>
                                </TableCell>
                                <TableCell padding='none' align="center" style={{ padding:'5px', width:'20%'}}>
                                    <Typography variant='caption'>
                                        <FormattedMessage 
                                            id='agristar2.equipment-control-columnTitle-RemoteSwitch'
                                            defaultMessage='Remote Switch'
                                            description='column title for Remote Switch on the equipment status/control page'
                                        />
                                    </Typography>
                                </TableCell>
                            </TableRow>
                        </TableHead>
                        <TableBody>
                    {/* ---------------FAN------------ */}
                        { fanObj ? 
                            <EquipmentRow 
                                iotClient={iotClient}
                                isAuthorized={isAuthorized}
                                exists={fanObj?.exists}
                                equipmentNameTranslation={
                                    <FormattedMessage 
                                        id='mnEquipStatus[4].fan'
                                        defaultMessage='Fan'
                                    />
                                }
                                equipmentRenamedAs={fanObj?.renamed_as}
                                equipmentStatus={()=>getEquipmentStatusText(fanObj.statusText)}
                                panelSwitchStatus={()=>getPanelSwitchStatus(fanObj?.panel_switch)}
                                remoteOff={fanObj?.remoteOff}
                                remSwitchName={fanObj?.remSwitchName}
                                outputColor={fanObj?.outputColor}
                                statusColor={fanObj?.statusColor}
                                panelSwitchColor={fanObj?.panel_switch_color}
                            />
                            :
                            null
                        }
                    {/* ---------------climacell------------ */}
                        { (climacellObj && !onionMode) ? 
                            <EquipmentRow 
                                iotClient={iotClient}
                                isAuthorized={isAuthorized}
                                exists={climacellObj?.exists}
                                equipmentNameTranslation={
                                    <FormattedMessage 
                                        id='mnEquipStatus[6].climacell'
                                        defaultMessage='ClimaCell'
                                    />
                                }
                                equipmentRenamedAs={climacellObj?.renamed_as}
                                equipmentStatus={()=>getEquipmentStatusText(climacellObj?.statusText)}
                                panelSwitchStatus={()=>getPanelSwitchStatus(climacellObj?.panel_switch)}
                                remoteOff={climacellObj?.remoteOff}
                                remSwitchName={climacellObj?.remSwitchName}
                                outputColor={climacellObj?.outputColor}
                                statusColor={climacellObj?.statusColor}
                                panelSwitchColor={climacellObj?.panel_switch_color}
                            />
                            :
                            null
                        }
                    {/* ---------------heat------------ */}
                        { (heatObj && !onionMode) ?
                            <EquipmentRow 
                                iotClient={iotClient}
                                isAuthorized={isAuthorized}
                                exists={heatObj?.exists}
                                equipmentNameTranslation={
                                    <FormattedMessage 
                                        id='mnEquipStatus[12].heat'
                                        defaultMessage='Heat'
                                    />
                                }
                                equipmentRenamedAs={heatObj?.renamed_as}
                                equipmentStatus={()=>getEquipmentStatusText(heatObj?.statusText)}
                                panelSwitchStatus={()=>getPanelSwitchStatus(heatObj?.panel_switch)}
                                remoteOff={heatObj?.remoteOff}
                                remSwitchName={heatObj?.remSwitchName}
                                outputColor={heatObj?.outputColor}
                                statusColor={heatObj?.statusColor}
                                panelSwitchColor={heatObj?.panel_switch_color}
                            />
                            :
                            null
                        }
                    {/* ---------------cavityHeat------------ */}
                        { cavityHeatObj ?
                            <EquipmentRow 
                                iotClient={iotClient}
                                isAuthorized={isAuthorized}
                                exists={cavityHeatObj?.exists}
                                equipmentNameTranslation={
                                    <FormattedMessage 
                                        id='mnEquipStatus[13].cavityHeat'
                                        defaultMessage='Cavity Heater'
                                    />
                                }
                                equipmentRenamedAs={cavityHeatObj?.renamed_as}
                                equipmentStatus={()=>getEquipmentStatusText(cavityHeatObj?.statusText)}
                                panelSwitchStatus={()=>getPanelSwitchStatus(cavityHeatObj?.panel_switch)}
                                remoteOff={cavityHeatObj?.remoteOff}
                                remSwitchName={cavityHeatObj?.remSwitchName}
                                outputColor={cavityHeatObj?.outputColor}
                                statusColor={cavityHeatObj?.statusColor}
                                panelSwitchColor={cavityHeatObj?.panel_switch_color}
                            />
                            :
                            null
                        }
                    {/* ---------------HUMIDIFIERS------------ */}
                            { !onionMode && 
                                humidifierObjs?.map((h,index) => ( h ?
                                        <EquipmentRow 
                                            key={index}
                                            iotClient={iotClient}
                                            isAuthorized={isAuthorized}
                                            exists={h?.exists}
                                            equipmentNameTranslation={
                                                (
                                                    h.humidifierPart === 'head' ? 
                                                    <FormattedMessage 
                                                        id='mnEquipStatus[7].humidifier.head'
                                                        defaultMessage='Humidifier {number} - Head'
                                                        values={{
                                                            number: h.humidifierNumber
                                                        }}
                                                    />
                                                    :
                                                    <FormattedMessage 
                                                        id='mnEquipStatus[8].humidifier.pump'
                                                        defaultMessage='Humidifier {number} - Pump'
                                                        values={{
                                                            number: h.humidifierNumber
                                                        }}
                                                    />
                                                )
                                            }
                                            equipmentRenamedAs={h?.renamed_as}
                                            equipmentStatus={()=>getEquipmentStatusText(h?.statusText)}
                                            panelSwitchStatus={()=>getPanelSwitchStatus(h?.panel_switch)}
                                            remoteOff={h?.remoteOff}
                                            remSwitchName={h?.remSwitchName}
                                            outputColor={h?.outputColor}
                                            statusColor={h?.statusColor}
                                            panelSwitchColor={h?.panel_switch_color}
                                        />
                                        :
                                        null
                                    )
                                )
                            }
                    {/* ---------------CURE--------------------- */}
                    {
                        onionMode && cureObj ?
                        <EquipmentRow 
                            iotClient={iotClient}
                            isAuthorized={isAuthorized}
                            exists={cureObj?.exists}
                            equipmentNameTranslation={
                                <FormattedMessage 
                                    id='EquipStatusDynAS2TranslatedText[11].cure'
                                    defaultMessage='Cure'
                                />
                            }
                            equipmentStatus={()=>getEquipmentStatusText(cureObj?.statusText)}
                            panelSwitchStatus={()=>getPanelSwitchStatus(cureObj?.panel_switch)}
                            remoteOff={cureObj?.remoteOff}
                            remSwitchName={cureObj?.remSwitchName}
                            outputColor={cureObj?.outputColor}
                            statusColor={cureObj?.statusColor}
                            panelSwitchColor={cureObj?.panel_switch_color}
                        />
                        :
                        null
                    }
                    {/* ---------------BURNER------------------- */}
                    { onionMode && burnerObj ?
                        <EquipmentRow 
                            iotClient={iotClient}
                            isAuthorized={isAuthorized}
                            exists={burnerObj?.exists}
                            equipmentNameTranslation={
                                <FormattedMessage 
                                    id='EquipStatusDynAS2TranslatedText[15].burner'
                                    defaultMessage='Burner'
                                />
                            }
                            equipmentStatus={()=>getEquipmentStatusText(burnerObj?.statusText)}
                            panelSwitchStatus={()=>getPanelSwitchStatus(burnerObj?.panel_switch)}
                            remoteOff={burnerObj?.remoteOff}
                            remSwitchName={burnerObj?.remSwitchName}
                            outputColor={burnerObj?.outputColor}
                            statusColor={burnerObj?.statusColor}
                            panelSwitchColor={burnerObj?.panel_switch_color}
                        />
                        :
                        null
                    }
                    {/* ---------------REFRIGERATION------------ */}
                        { refrigObj ?
                            <EquipmentRow 
                                iotClient={iotClient}
                                isAuthorized={isAuthorized}
                                exists={refrigObj?.exists}
                                equipmentNameTranslation={intl.formatMessage(agristar2Refrigeration)}
                                equipmentRenamedAs={refrigObj?.renamed_as}
                                equipmentStatus={()=>getEquipmentStatusText(refrigObj?.statusText)}
                                panelSwitchStatus={()=>getPanelSwitchStatus(refrigObj?.panel_switch)}
                                remoteOff={refrigObj?.remoteOff}
                                remSwitchName={refrigObj?.remSwitchName}
                                outputColor={refrigObj?.outputColor}
                                statusColor={refrigObj?.statusColor}
                                panelSwitchColor={refrigObj?.panel_switch_color}
                            />
                            :
                            null
                        }

                    {/* ------------------REFRIG STAGES-------------------- */}
                    { refrigStageObjs?.map((rs,index) => (rs ?
                        <EquipmentRow 
                            key={index}
                            iotClient={iotClient}
                            isAuthorized={isAuthorized}
                            exists={rs?.exists}
                            equipmentNameTranslation={
                                <FormattedMessage 
                                    id='agristar2.equipment-control-outputname-refrigerationStageNumber'
                                    defaultMessage='Refrigeration stage {stageNumber}'
                                    description='name for the equipment type -> refrigeration stage <number>'
                                    values={{
                                        stageNumber: (rs?.stageNumber),
                                    }}
                                />
                            }
                            equipmentRenamedAs={rs?.renamed_as}
                            equipmentStatus={()=>getEquipmentStatusText(rs?.statusText)}
                            panelSwitchStatus={()=>getPanelSwitchStatus(rs?.panel_switch)}
                            remoteOff={rs?.remoteOff}
                            remSwitchName={rs?.remSwitchName}
                            outputColor={rs?.outputColor}
                            statusColor={rs?.statusColor}
                            panelSwitchColor={rs?.panel_switch_color}
                        />
                        : null))
                    }
                    <TableRow>
                        <TableCell>
                            <FormattedMessage
                                id='agristar2.equipment-control-diagnostics-clear'
                                defaultMessage='Diagnostics Mode'
                            />
                        </TableCell>
                        <TableCell>
                            <ButtonSave
                                onClick={clearDiagnostics}
                                style={{margin:'5px auto'}}
                                label={
                                    <FormattedMessage 
                                        id='buttonsTranslatedText[35].clear'
                                        defaultMessage='Clear'
                                    />
                                }
                            />
                        </TableCell>
                    </TableRow>
                    {/* ------------------DEFROST-------------------- */}
                        {   defrostObjs?.map((defrost,index) => ( defrost ?
                                    <EquipmentRow 
                                        key={index}
                                        iotClient={iotClient}
                                        isAuthorized={isAuthorized}
                                        exists={defrost?.exists}
                                        equipmentNameTranslation={
                                            <FormattedMessage 
                                                id='p2Refrigeration[4].defrost'
                                                defaultMessage='Defrost {defrost}'
                                                description='Defrost'
                                                values={{defrost: (defrost?.number)}}
                                            />
                                        }
                                        equipmentRenamedAs={defrost?.renamed_as}
                                        equipmentStatus={()=>getEquipmentStatusText(defrost?.statusText)}
                                        panelSwitchStatus={()=>getPanelSwitchStatus(defrost?.panel_switch)}
                                        remoteOff={defrost?.remoteOff}
                                        remSwitchName={defrost?.remSwitchName}
                                        outputColor={defrost?.outputColor}
                                        statusColor={defrost?.statusColor}
                                        panelSwitchColor={defrost?.panel_switch_color}
                                    />
                                    :
                                    null
                                )
                            )
                        }
                    {/* ------------------Dehumidifier------------------- */}
                    { beeMode &&
                        <EquipmentRow
                            iotClient={iotClient}
                            isAuthorized={isAuthorized}
                            exists={dehumidObj?.exists}
                            equipmentNameTranslation={
                                <FormattedMessage 
                                    id='EquipStatusDynAS2TranslatedText[17].dehumidifier'
                                    defaultMessage='Dehumidifier'
                                />
                            }
                            equipmentStatus={()=>getEquipmentStatusText(dehumidObj?.statusText)}
                            panelSwitchStatus={()=>getPanelSwitchStatus(dehumidObj?.panel_switch)}
                            remoteOff={dehumidObj?.remoteOff}
                            remSwitchName={dehumidObj?.remSwitchName}
                            outputColor={dehumidObj?.outputColor}
                            statusColor={dehumidObj?.statusColor}
                            panelSwitchColor={dehumidObj?.panel_switch_color}
                        />
                    }
                    {/* ------------------AUXILLARIES-------------------- */}
                        {   auxiliaryObjs?.map((aux, index) => ( aux ?
                                    <EquipmentRow 
                                        key={index}
                                        iotClient={iotClient}
                                        isAuthorized={isAuthorized}
                                        exists={aux?.exists}
                                        equipmentNameTranslation={
                                            <FormattedMessage
                                                id = 'mnEquipStatus[11].auxNumber'
                                                defaultMessage = 'Auxiliary {number}'
                                                values = {{
                                                    number: index + 1,
                                                }}
                                            />
                                        }
                                        equipmentStatus={()=>getEquipmentStatusText(aux?.statusText)}
                                        panelSwitchStatus={()=>getPanelSwitchStatus(aux?.panel_switch)}
                                        remoteOff={aux?.remoteOff}
                                        remSwitchName={aux?.remSwitchName}
                                        outputColor={aux?.outputColor}
                                        statusColor={aux?.statusColor}
                                        panelSwitchColor={aux?.panel_switch_color}
                                    />
                                    :
                                    null
                                )
                            )
                        }
                        </TableBody>
                    </Table>
                </TableContainer>
            </SaveComponent>
        </>
    )
}
export default Agristar2PageViewEquipmentStatus
// Var IoNames = 
// NOTE: 
// [A] indicates display always displayed in Equipment Control/status page view
// [x] indicates only show it when it is configured 

// [x]Fan/Green Light:4:2:0:0, // 1) always displayed 2) display as 'Fan' 
// []Door:0:4:0:1, 
// [x]Refrigeration:4:1:0:2, // 1) always displayed
// [x]ClimaCell:1:2:0:3,
// [x]Heat:1:2:0:4,
// [x]Cavity Heat:4:2:0:5,
// []Burner:2:2:0:6,
// [x]Humidifier 1 - Head:1:2:0:7, // Display Head and Pump as one
// [x]Humidifier 1 - Pump:1:0:0:8,
// [x]Humidifier 2 - Head:1:2:0:9,
// [x]Humidifier 2 - Pump:1:0:0:10,
// [x]Humidifier 3 - Head:1:2:0:11,
// [x]Humidifier 3 - Pump:1:0:0:12,
// [x]Refrigeration Stage 1:4:2:1:13,
// [x]Refrigeration Stage 2:4:2:1:14,
// [x]Refrigeration Stage 3:4:2:1:15,
// [x]Refrigeration Stage 4:4:2:1:16,
// [x]Refrigeration Stage 5:4:2:1:17,
// [x]Refrigeration Stage 6:4:2:1:18,
// [x]Refrigeration Stage 7:4:2:1:19,
// [x]Refrigeration Stage 8:4:2:1:20,
// [x]Defrost 1:4:2:0:21,
// [x]Defrost 2:4:2:0:22,
// []Bay Lights 1:4:2:0:23,
// []Bay Lights 2:4:2:0:24,
// [x]Auxiliary z:4:2:1:25,
// [x]Auxiliary 2:4:2:1:26,
// [x]Auxiliary 3:4:2:1:27,
// [x]Auxiliary 4:4:2:1:28,
// [x]Auxiliary 5:4:2:1:29,
// [x]Auxiliary 6:4:2:1:30,
// [x]Auxiliary 7:4:2:1:31,
// [x]Auxiliary 8:4:2:1:32,
// []Power:0:1:0:33,
// []Remote Standby:4:1:0:34,
// []Refrigeration Standby:4:1:0:35,
// []Air Flow:0:1:0:36,
// []Low Temperature Limit:0:1:0:37,
// []Red Light:0:0:0:38,
// []Yellow Light:0:0:0:39,
// []Pulse Door - Power:0:0:0:40,
// []Pulse Door - Open:0:0:0:41,
// []Pulse Door - Close:0:0:0:42,
// []Start/Stop:4:3:0:43,
// []Fan - Auto:4:3:0:44,
// []Fan - Manual:4:3:0:45,
// []Door - Auto:4:3:0:46,
// []Door - Manual:4:3:0:47,
// []ClimaCell - Auto:1:3:0:48,
// []ClimaCell - Manual:1:3:0:49,
// []Humidifier - Auto:1:3:0:50,
// []Humidifier - Manual:1:3:0:51,
// []Refrigeration - Auto:4:3:0:52,
// []Cure - Auto:2:3:0:53,
// []Burner - Auto:2:3:0:54,
// []Auxiliary 1 - Auto:4:3:1:55,
// []Auxiliary 1 - Manual:4:3:1:56,
// []Auxiliary 2 - Auto:4:3:1:57,
// []Auxiliary 2 - Manual:4:3:1:58,