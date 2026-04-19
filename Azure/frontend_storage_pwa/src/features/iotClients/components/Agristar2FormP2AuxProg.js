import {
    Paper, TableContainer, Table, TableBody, TableCell, TableHead, TableRow,
} from "@material-ui/core";
import { FormattedMessage } from 'react-intl';
import ButtonSave from "../../../common/ButtonSave";
import useASFormP2AuxProg from "../hooks/AS2FormP2AuxProg";
import Agristar2InputField, { Agristar2InputFieldSelect } from "./Agristar2InputFields";
import { Fragment } from "react";
import SaveComponent from "./SaveComponent";

const Rule2 = (props) => {
    return (
        <>
        <TableCell colSpan={3}>
            <div style={{display: 'flex', flexFlow: 'row'}}>
                <Agristar2InputFieldSelect
                    value={props.rule.op}
                    options={props.mapValueToLabel.op}
                    onChange={(e) => props.handleRuleToSend(props.index, 'op', e.target.value)}
                    disabled={!props.isAuthorized}
                    style={{marginTop:'-.2rem', marginRight: '10px'}}
                />
                { props.rule.sensorOption === '0' &&
                    <Agristar2InputField
                        type={'number'}
                        value={props.rule.sen}
                        onChange={(e) => props.handleRuleToSend(props.index, 'sen', e.target.value)}
                        error={props.error[`sen${props.index+1}`]}
                        disabled={!props.isAuthorized}
                        style={{marginTop:'-.2rem', marginRight: '10px'}}
                    />
                }
                { props.rule.sensorOption === '1' &&
                    <>
                        <Agristar2InputFieldSelect
                            value={props.rule.ref}
                            onChange={(e) => props.handleRuleToSend(props.index, 'ref', e.target.value)}
                            options={props.availSensors(true)}
                            disable={!props.isAuthorized}
                            style={{marginTop:'-.2rem', marginRight: '10px'}}
                        />
                        <Agristar2InputField
                            type={'number'}
                            value={props.rule.diff}
                            onChange={(e) => props.handleRuleToSend(props.index, 'diff', e.target.value)}
                            error={props.error[`diff${props.index+1}`]}
                            disabled={!props.isAuthorized}
                            style={{width: '100px'}}
                        />
                    </>
                }
            </div>
        </TableCell>
        </>
    );
};

const InputOutputMode = (props) => {
    switch (props.rule.type) {
        default:
        case '0': // manual
            return null;
        case '1': // output
            return (
                <>
                <TableCell>
                    <Agristar2InputFieldSelect
                        value={props.rule.io}
                        options={props.availEquip('output')}
                        onChange={(e)=>props.handleRuleToSend(props.index, 'io', e.target.value)}
                        disabled={!props.isAuthorized}
                        style={{marginTop:'-.2rem'}}
                    />
                </TableCell> 
                <TableCell>
                    <Agristar2InputFieldSelect
                        value={props.rule.st}
                        options={props.mapValueToLabel.onOff}
                        onChange={(e)=>props.handleRuleToSend(props.index, 'st', e.target.value)}
                        disabled={!props.isAuthorized}
                        style={{marginTop:'-.2rem'}}
                    />          
                </TableCell>
                </>
            );
        case '2': // input
            return (
                <>
                <TableCell>
                    <Agristar2InputFieldSelect
                        value={props.rule.io}
                        options={props.availEquip('input')}
                        onChange={(e)=>props.handleRuleToSend(props.index, 'io', e.target.value)}
                        disabled={!props.isAuthorized}
                        style={{marginTop:'-.2rem'}}
                    />          
                </TableCell> 
                <TableCell>
                    <Agristar2InputFieldSelect
                        value={props.rule.st}
                        options={props.mapValueToLabel.onOff}
                        onChange={(e)=>props.handleRuleToSend(props.index, 'st', e.target.value)}
                        disabled={!props.isAuthorized}
                        style={{marginTop:'-.2rem'}}
                    />          
                </TableCell>
                </>
            );
        case '3': // switch
            return (
                <>
                <TableCell>
                    <Agristar2InputFieldSelect
                        value={props.rule.io}
                        options={props.availSwitch()}
                        onChange={(e)=>props.handleRuleToSend(props.index, 'io', e.target.value)}
                        disabled={!props.isAuthorized}
                        style={{marginTop:'-.2rem'}}
                    />
                </TableCell>
                <TableCell>
                    <Agristar2InputFieldSelect
                        value={props.rule.st}
                        options={props.mapValueToLabel.onOff}
                        onChange={(e)=>props.handleRuleToSend(props.index, 'st', e.target.value)}
                        disabled={!props.isAuthorized}
                        style={{marginTop:'-.2rem'}}
                    />          
                </TableCell>
                </>
            );
        case '4': // sensor
            return (
                <>
                    {
                    <TableCell>
                        <Agristar2InputFieldSelect
                            value={props.rule.io}
                            options={props.availSensors(false)}
                            onChange={(e)=>props.handleRuleToSend(props.index, 'io', e.target.value)}
                            disabled={!props.isAuthorized}
                            style={{marginTop:'-.2rem'}}
                        />          
                    </TableCell> 
                    }
                    {
                        props.rule.first &&
                        <TableCell>
                            <Agristar2InputFieldSelect
                                value='255'
                                options={props.mapValueToLabel.option}
                                onChange={(e) => props.handleRuleToSend(props.index, 'sensorOption', e.target.value)}
                                disabled={!props.isAuthorized}
                                style={{marginTop:'-.2rem'}}
                            />
                        </TableCell>
                    }
                </>
            );
        case '5': // mode
            return (
                <TableCell>
                    <Agristar2InputFieldSelect
                        value={props.rule.io}
                        options={props.mode}
                        onChange={(e)=>props.handleRuleToSend(props.index, 'io', e.target.value)}
                        disabled={!props.isAuthorized}
                        style={{marginTop:'-.2rem'}}
                    />          
                </TableCell> 
            );
    }
};

const Rule = (props) => {
    return (
        <>
            {
            props.index === 0
            ? <TableCell rowSpan={props.rule.first ? 1 : 2}>{props.name}</TableCell>
            : 
                <TableCell rowSpan={props.rule.first ? 1 : 2}>
                    <Agristar2InputFieldSelect
                        value={props.rule.andOr}
                        options={props.mapValueToLabel.andOr}
                        onChange={(e)=>props.handleRuleToSend(props.index, 'andOr', e.target.value)}
                        disabled={!props.isAuthorized}
                        style={{marginTop:'-.2rem'}}
                    />          
                </TableCell>
            }
            {
                (props.rule.andOr !== '255' || props.index === 0) &&
                <>
                    <TableCell>
                        <Agristar2InputFieldSelect
                            value={props.rule.type}
                            options={props.mapValueToLabel.type}
                            onChange={(e)=>props.handleRuleToSend(props.index, 'type', e.target.value)}
                            disabled={!props.isAuthorized}
                            style={{marginTop:'-.2rem'}}
                        />          
                    </TableCell>
                    <InputOutputMode
                        index={props.index}
                        rule={props.rule}
                        mapValueToLabel={props.mapValueToLabel}
                        mode={props.mode}
                        handleRuleToSend={props.handleRuleToSend}
                        isAuthorized={props.isAuthorized}
                        availEquip={props.availEquip}
                        availSensors={props.availSensors}
                        availSwitch={props.availSwitch}
                    />
                </>
            }
        </>
    );
};

const AgristarFormP2AuxProg = (props) => {
    const {
        isAuthorized,
        payloadToSend,
        mapValueToLabel,
        saving,
        errors,
        moveAux,
        availEquip,
        availSensors,
        availSwitch,
        handlePayloadToSend,
        handleRuleToSend,
        submitPayloadToSend,
        mode,
    } = useASFormP2AuxProg()

    const saveEnabled = isAuthorized

    return(
        payloadToSend.rules.length === 0
        ?
        <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle', padding:'0px'}}>
            <FormattedMessage
                id='p2AuxProg[25].none'
                defaultMessage='No auxiliary outputs defined.'
            />
        </div>
        :
        <SaveComponent saving={saving?.p2AuxProgFrame?.status || saving?.button2?.status}>
            <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle', padding:'0px'}}>
                <TableContainer component={Paper}>
                    <Table>
                        <TableHead>
                            <TableRow>
                                <TableCell style={{width: '25%'}}>
                                    <FormattedMessage
                                        id='p2AuxProg[1].output'
                                        defaultMessage='Output'
                                    />
                                </TableCell>
                                <TableCell colSpan="3">
                                    <FormattedMessage
                                        id='p2AuxProg[2].rule'
                                        defaultMessage='Rule'
                                    />
                                </TableCell>
                            </TableRow>
                        </TableHead>
                        <TableBody>
                            {
                                payloadToSend.rules.map((rule, index) =>
                                    rule.andOr !== '256' &&
                                    <Fragment key={index}>
                                        <TableRow>
                                            <Rule
                                                index={index}
                                                name={`Auxiliary ${payloadToSend.AuxProgram - 24}`}
                                                rule={rule}
                                                mapValueToLabel={mapValueToLabel}
                                                mode={mode}
                                                handleRuleToSend={handleRuleToSend}
                                                isAuthorized={isAuthorized}
                                                availEquip={availEquip}
                                                availSensors={availSensors}
                                                availSwitch={availSwitch}
                                            />
                                        </TableRow>
                                        {
                                        rule.type === '4' && !rule.first &&
                                        <TableRow>
                                            <Rule2
                                                index={index}
                                                rule={rule}
                                                error={errors}
                                                mapValueToLabel={mapValueToLabel}
                                                handleRuleToSend={handleRuleToSend}
                                                isAuthorized={isAuthorized}
                                                availSensors={availSensors}
                                            />
                                        </TableRow>
                                        }
                                    </Fragment>
                                )
                            }
                            <TableRow>
                                <TableCell colSpan='4'>
                                    <div style={{display:'flex', justifyContent:'center'}}>
                                        <FormattedMessage 
                                            id='p2AuxProg[3].duty.cycle'
                                            defaultMessage='Duty Cycle:'
                                        />
                                        <Agristar2InputField 
                                            type={'text'}
                                            value={payloadToSend.dutyCycle || ''}
                                            error={errors.dutyCycle}
                                            onChange={(e)=>handlePayloadToSend('dutyCycle', e.target.value)}
                                            disabled={!isAuthorized}
                                            endAdornment='%'
                                            style={{width:'75px', marginLeft:'10px', marginTop:'-.2rem'}}
                                        />
                                        <FormattedMessage 
                                            id='p2AuxProg[4].period'
                                            defaultMessage='Period:'
                                        />
                                        <Agristar2InputField 
                                            type={'text'}
                                            value={payloadToSend.period || ''}
                                            error={errors.period}
                                            onChange={(e)=>handlePayloadToSend('period', e.target.value)}
                                            disabled={!isAuthorized}
                                            style={{width:'75px', marginLeft:'5px', marginTop:'-.2rem'}}
                                        />
                                        <Agristar2InputFieldSelect
                                            value={payloadToSend.units}
                                            options={mapValueToLabel.units}
                                            error={errors.units}
                                            onChange={(e)=>handlePayloadToSend('units', e.target.value)}
                                            disabled={!isAuthorized}
                                            style={{marginLeft: '5px', marginTop:'-.4rem'}}
                                        />          
                                    </div>
                                </TableCell>
                            </TableRow>
                        </TableBody>
                    </Table>
                </TableContainer>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'center'}}>
                    <ButtonSave 
                        onClick={()=>moveAux('Back')}
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
                        onClick={()=>moveAux('Next')}
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
export default AgristarFormP2AuxProg;