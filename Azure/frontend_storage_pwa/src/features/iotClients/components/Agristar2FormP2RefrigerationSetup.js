import { FormattedMessage } from "react-intl"
import {
    Paper, TableContainer, Table, TableBody, TableCell, TableRow, Typography,
} from "@material-ui/core"
import ButtonSave from "../../../common/ButtonSave"
import useASFormP2RefrigerationSetup from "../hooks/AS2FormP2RefrigerationSetup"
import Agristar2InputField, { Agristar2InputFieldSelect } from "./Agristar2InputFields"
import Pidu from './Pidu';
import SaveComponent from "./SaveComponent"

const Refrigeration = (props) => {
    return (
        <TableRow>
            {
                props.boardType === 'AS1' && props.index >= 4
                ?
                <TableCell>
                    {
                    props.index === 4 &&
                    <Agristar2InputFieldSelect
                        value={props.stage5}
                        options={props.stageOptions}
                        onChange={(e)=>props.handleStageToSend(props.index, 'Defrost', e.target.value)}
                        disabled={!props.isAuthorized}
                        style={{marginTop:'-.2rem'}}
                    />
                    }
                    {
                    props.index === 5 &&
                    <Agristar2InputFieldSelect
                        value={props.stage6}
                        options={props.stageOptions}
                        onChange={(e)=>props.handleStageToSend(props.index, 'Defrost', e.target.value)}
                        disabled={!props.isAuthorized}
                        style={{marginTop:'-.2rem'}}
                    />
                    }
                </TableCell>
                : props.index >= 8
                    ? <TableCell>
                        <FormattedMessage 
                            id='p2Refrigeration[4].defrost'
                            defaultMessage='Defrost {defrost}'
                            description='Defrost'
                            values={
                                {
                                    defrost: props.index - 7,
                                }
                            }
                        />
                    </TableCell>
                    : <TableCell>
                        <FormattedMessage 
                            id='p2Refrigeration[1].stage'
                            defaultMessage='Stage {stage}'
                            description='Stage'
                            values={
                                {
                                    stage: props.index + 1,
                                }
                            }
                        />
                    </TableCell>
            }
            {
                props.index >= 8 || ((props.boardType === 'AS1' &&
                ((props.index === 4 && props.stage5 === 1) ||
                (props.index === 5 && props.stage6 === 1))))
                ?
                    <TableCell colSpan={2}>
                        <FormattedMessage 
                            id='p2RefrigerationAS2[12].enabled'
                            defaultMessage='Enabled'
                        />
                    </TableCell>
                :
                    <>
                        <TableCell>
                            <FormattedMessage 
                                id='p2Refrigeration[2].on'
                                defaultMessage='On:'
                            />
                            <Agristar2InputField
                                type={'number'}
                                value={props.stages[props.index].On || ''}
                                error={props.error[`Stage${props.index + 1}On`]}
                                onChange={(e)=>props.handleStageToSend(props.index, 'On', e.target.value)}
                                disabled={!props.isAuthorized}
                                style={{maxWidth:'100px', marginLeft: '10px'}}
                                endAdornment='%'
                            />
                        </TableCell>
                        <TableCell>
                            <FormattedMessage 
                                id='p2Refrigeration[3].off'
                                defaultMessage='Off:'
                            />
                            <Agristar2InputField
                                type={'number'}
                                value={props.stages[props.index].Off || ''}
                                error={props.error[`Stage${props.index + 1}Off`]}
                                onChange={(e)=>props.handleStageToSend(props.index, 'Off', e.target.value)}
                                disabled={!props.isAuthorized}
                                style={{maxWidth:'100px', marginLeft: '10px'}}
                                endAdornment='%'
                            />
                        </TableCell>
                    </>
            }
        </TableRow>
    );
}

const AgristarFormP2RefrigerationSetup = (props) => {
    const {
        isAuthorized,
        payloadToSend,
        mapValueToLabel,
        boardType,
        stage5,
        stage6,
        OutputConfig,
        saving,
        errors,
        handleStageToSend,
        handlePayloadToSend,
        submitPayloadToSend,
    } = useASFormP2RefrigerationSetup()
    
    const saveEnabled = isAuthorized
    const outputIndex = [13, 14, 15, 16, 17, 18, 19, 20, 21, 22];

    const refrigeration = boardType === 'AS2' ? 'p2RefrigerationAS2' : 'p2Refrigeration';
    return(
        <SaveComponent saving={saving?.[refrigeration]?.status}>
            <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle', padding:'0px'}}>
                <TableContainer component={Paper}>
                    <Table width="100%">
                        <TableBody>
                            {
                                outputIndex.map((item, index) => (
                                    ((boardType === 'AS1' && item < 19) || boardType === 'AS2') &&
                                    OutputConfig[item] !== '-1' &&
                                    <Refrigeration
                                        key={index}
                                        index={index}
                                        error={errors}
                                        stages={payloadToSend.Stages}
                                        isAuthorized={isAuthorized}
                                        handleStageToSend={handleStageToSend}
                                        stageOptions={index === 4 ? mapValueToLabel.Stage5 : mapValueToLabel.Stage6}
                                        stage5={stage5}
                                        stage6={stage6}
                                        boardType={boardType}
                                    />
                                ))
                            }
                        </TableBody>
                    </Table>
                </TableContainer>
                <Pidu
                    label={
                        <FormattedMessage
                            id='p2Refrigeration[5].pidu'
                            defaultMessage='PIDU Values'
                        />
                    }
                    P={payloadToSend.PRefrValue}
                    I={payloadToSend.IRefrValue}
                    D={payloadToSend.DRefrValue}
                    U={payloadToSend.URefrValue}
                    error={{ errors, p: 'PRefrValue', i: 'IRefrValue', d: 'DRefrValue', u: 'URefrValue'}}
                    isAuthorized={isAuthorized}
                    handlePayloadToSend={handlePayloadToSend}/>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between'}}>
                    <Typography align='justify' component='div' >
                        <FormattedMessage 
                            id='p2Refrigeration[6].purge1'
                            defaultMessage='Refrigeration purge mode is'
                        />
                        &nbsp;
                        <Agristar2InputFieldSelect
                            value={payloadToSend.RefrigerationPurge || ''}
                            options={mapValueToLabel.RefrigerationPurge}
                            error={errors.RefrigerationPurge}
                            onChange={(e)=>handlePayloadToSend('RefrigerationPurge', e.target.value)}
                            disabled={!isAuthorized}
                            style={{marginTop:'-.2rem'}}
                        />
                        &nbsp;
                        <FormattedMessage 
                            id='p2Refrigeration[9].purge2'
                            defaultMessage='and output must be below'
                        />
                        &nbsp;
                        <Agristar2InputField
                            type={'number'}
                            value={payloadToSend.PurgeThreshold || ''}
                            error={errors.PurgeThreshold}
                            onChange={(e)=>handlePayloadToSend('PurgeThreshold', e.target.value)}
                            disabled={!isAuthorized}
                            style={{maxWidth:'100px'}}
                            endAdornment='%'
                        />
                        &nbsp;
                        <FormattedMessage 
                            id='p2Refrigeration[10].purge3'
                            defaultMessage='to purge.'
                        />
                    </Typography>
                </div>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'center'}}>
                    <ButtonSave 
                        onClick={()=>submitPayloadToSend()}
                        disabled={!saveEnabled}
                        style={{margin:'5px auto'}}
                    />
                </div>
            </div>
        </SaveComponent>
    )
}
export default AgristarFormP2RefrigerationSetup;