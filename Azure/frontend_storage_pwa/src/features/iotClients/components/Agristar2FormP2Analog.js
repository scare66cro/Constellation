import {
    Paper, TableContainer, Table, TableBody, TableCell, TableHead,
    TableRow, Checkbox, FormControlLabel,
    Typography,
} from "@material-ui/core"
import ButtonSave from "../../../common/ButtonSave"
import useASFormP2Analog from "../hooks/AS2FormP2Analog"
import Agristar2InputField, { Agristar2InputFieldSelect} from "./Agristar2InputFields"
import { Fragment } from 'react';
import SaveComponent from "./SaveComponent";
import { FormattedMessage } from 'react-intl';

const AgristarFormP2Analog = (props) => {
    const {
        isAuthorized,
        payloadToSend,
        mapValueToLabel,
        saving,
        errors,
        moveBoard,
        handlePayloadToSend,
        handleSensorToSend,
        submitPayloadToSend,
        is200plus,
    } = useASFormP2Analog()

    const saveEnabled = isAuthorized
    const inputDisabled = !isAuthorized

    return(
        <SaveComponent saving={saving?.p2AnalogBoardSetup?.status || saving?.button2?.status}>
            <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle', padding:'0px'}}>
                <TableContainer component={Paper}>
                    <Table width="100%" size="small">
                        <TableHead>
                            <TableRow>
                                <TableCell>
                                    <FormattedMessage 
                                        id='p2AnalogBoardSetup[1].board'
                                        defaultMessage='Board'
                                    />
                                </TableCell>
                                <TableCell>
                                    <FormattedMessage 
                                        id='p2AnalogBoardSetup[2].board.type'
                                        defaultMessage='Type'
                                    />
                                </TableCell>
                                <TableCell colSpan='2'></TableCell>
                            </TableRow>
                        </TableHead>
                        <TableBody>
                            <TableRow>
                                <TableCell rowSpan='2'>{payloadToSend.BAdd}</TableCell>
                                <TableCell rowSpan='2' colSpan='2'>{mapValueToLabel.SensorType[payloadToSend.BType]}</TableCell>
                                <TableCell colSpan='4'>
                                    <Agristar2InputField
                                        type={'text'}
                                        value={payloadToSend.BdLbl}
                                        error={errors.BdLbl}
                                        onChange={(e)=>handlePayloadToSend('BdLbl', e.target.value)}
                                        disabled={inputDisabled || payloadToSend.BDis === '1'}
                                        label={
                                            <FormattedMessage
                                                id='p2AnalogBoardSetup[3].board.label'
                                                defaultMessage='Label'
                                            />
                                        }
                                        style={{maxWidth:'300px'}}
                                    />
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell>v{payloadToSend.BVer}</TableCell>
                                <TableCell colSpan='2'>
                                    <FormControlLabel
                                        control={
                                            <Checkbox
                                                checked={payloadToSend.BDis === '1' ? true : false}
                                                inputProps={{ 'aria-label': 'Disable Board'}}
                                                onChange={(e) => handlePayloadToSend('BDis', e.target.checked)}
                                                color='primary'
                                                disabled={inputDisabled}
                                            />
                                        }
                                        label={
                                            <FormattedMessage
                                                id='p2AnalogBoardSetup[5].board.disabled'
                                                defaultMessage='Disabled'
                                            />
                                        }
                                        labelPlacement='start'
                                    />
                                </TableCell>
                            </TableRow>
                        </TableBody>
                    </Table>
                </TableContainer>
                <TableContainer component={Paper}>
                    <Table width="100%" size="small">
                        <TableHead>
                            <TableRow>
                                <TableCell>
                                    <FormattedMessage 
                                        id='p2AnalogBoardSetup[6].sensor'
                                        defaultMessage='Sensor'
                                    />
                                </TableCell>
                                <TableCell colSpan="4">
                                    <FormattedMessage 
                                        id='p2AnalogBoardSetup[7].sensor.type'
                                        defaultMessage='Type'
                                    />
                                </TableCell>
                                <TableCell colSpan="4"></TableCell>
                            </TableRow>
                        </TableHead>
                        <TableBody>
                            {
                                payloadToSend.sensors.map((sensor, index) => (
                                    <Fragment key={index}>
                                    <TableRow>
                                        <TableCell rowSpan='2'># {index + 1}</TableCell>
                                        <TableCell colSpan='4'>
                                            {
                                                payloadToSend.BType === '3' &&
                                                    (payloadToSend.BAdd === '1' 
                                                        ? <Typography style={{fontSize: '0.875rem'}}>{mapValueToLabel.SensorType[sensor.SenTyp]}</Typography>
                                                        : <Agristar2InputFieldSelect
                                                            value={sensor.SenTyp}
                                                            error={errors.SenTyp}
                                                            onChange={(e) => handleSensorToSend(index, 'SenTyp', e.target.value)}
                                                            options={mapValueToLabel.PileTempOptions}
                                                            disable={!isAuthorized}
                                                            style={{marginTop:'-.2rem'}}
                                                        />)
                                            }
                                            {
                                                ((payloadToSend.BType === '2' || payloadToSend.BType === '1') && payloadToSend.BAdd === '2') &&
                                                    ((index === 3 || (index === 2 && is200plus) )
                                                    ? <Agristar2InputFieldSelect
                                                        value={sensor.SenTyp}
                                                        error={errors.SenTyp}
                                                        onChange={(e) => handleSensorToSend(index, 'SenTyp', e.target.value)}
                                                        options={mapValueToLabel.HumidOptions}
                                                        disable={!isAuthorized}
                                                        style={{marginTop:'-.2rem'}}
                                                    />
                                                    : <Typography style={{fontSize: '0.875rem'}}>{mapValueToLabel.SensorType[1]}</Typography>)
                                            }
                                            {
                                                payloadToSend.BType === '1' && parseInt(payloadToSend.BAdd, 10) > 2 &&
                                                <Agristar2InputFieldSelect
                                                    value={sensor.SenTyp}
                                                    error={errors.SenTyp}
                                                    onChange={(e) => handleSensorToSend(index, 'SenTyp', e.target.value)}
                                                    options={mapValueToLabel.PileHumidOptions}
                                                    disable={!isAuthorized}
                                                    style={{marginTop:'-.2rem'}}
                                                />
                                            }
                                        </TableCell>
                                        <TableCell colSpan='4'>
                                            <Agristar2InputField
                                                type={'text'}
                                                value={sensor.SenLbl}
                                                error={errors[`Sen${index + 1}Lbl`]}
                                                onChange={(e)=>handleSensorToSend(index, 'SenLbl', e.target.value)}
                                                disabled={inputDisabled || sensor.SenDis === '1'}
                                                label={
                                                    <FormattedMessage
                                                        id='p2AnalogBoardSetup[8].sensor.label'
                                                        defaultMessage='Label'
                                                    />
                                                }
                                                style={{maxWidth:'300px'}}
                                            />
                                        </TableCell>
                                    </TableRow>
                                    <TableRow>
                                        <TableCell colSpan='2'>
                                            <Agristar2InputField
                                                type={'number'}
                                                value={sensor.SenOff}
                                                error={errors[`Sen${index + 1}Off`]}
                                                onChange={(e)=>handleSensorToSend(index, 'SenOff', e.target.value)}
                                                disabled={inputDisabled || sensor.SenDis === '1'}
                                                label={
                                                    <FormattedMessage
                                                        id='p2AnalogBoardSetup[9].offset'
                                                        defaultMessage='Offset'
                                                    />
                                                }
                                                maxWidth="150px"
                                            />
                                        </TableCell>
                                        <TableCell colSpan='2'>
                                            <Agristar2InputField
                                                type='text'
                                                value={sensor.SenVal}
                                                readOnly
                                                disabled={inputDisabled || sensor.SenDis === '1'}
                                                label={
                                                    <FormattedMessage
                                                        id='p2AnalogBoardSetup[10].sensor.value'
                                                        defaultMessage='Value'
                                                    />
                                                }
                                                maxWidth="150px"
                                            />
                                        </TableCell>
                                        <TableCell colSpan='4'>
                                            <FormControlLabel
                                                control={
                                                    <Checkbox
                                                        checked={sensor.SenDis === '1' ? true : false}
                                                        inputProps={{ 'aria-label': 'Disable Sensor'}}
                                                        onChange={(e) => handleSensorToSend(index, 'SenDis', e.target.checked)}
                                                        color='primary'
                                                        disabled={inputDisabled || payloadToSend.BDis === '1'}
                                                    />
                                                }
                                                label={
                                                    <FormattedMessage
                                                        id='p2AnalogBoardSetup[11].sensor.disabled'
                                                        defaultMessage='Disabled'
                                                    />
                                                }
                                                labelPlacement='start'
                                            />
                                        </TableCell>
                                    </TableRow>
                                    </Fragment>
                                ))
                            }
                        </TableBody>
                    </Table>
                </TableContainer>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'center'}}>
                    <ButtonSave 
                        onClick={()=>moveBoard('Back')}
                        style={{margin:'5px auto'}}
                        label={
                            <FormattedMessage 
                                id='buttonsTranslatedText[6].back'
                                defaultMessage='Back'
                            />
                        }
                    />
                    <ButtonSave 
                        onClick={()=>submitPayloadToSend()}
                        disabled={!saveEnabled}
                        style={{margin:'5px auto'}}
                    />
                    <ButtonSave 
                        onClick={()=>moveBoard('Next')}
                        style={{margin:'5px auto'}}
                        label={
                            <FormattedMessage 
                                id='buttonsTranslatedText[5].next'
                                defaultMessage='Next'
                            />
                        }
                    />
                </div>
            </div>
        </SaveComponent>
    )
}
export default AgristarFormP2Analog;