import {
    Paper, TableContainer, Table, TableBody, TableCell, TableHead, TableRow,
} from "@material-ui/core"
import ButtonSave from "../../../common/ButtonSave"
import useASFormP2IOConfig from "../hooks/AS2FormP2IOConfig"
import { Agristar2InputFieldSelect } from "./Agristar2InputFields"
import SaveComponent from "./SaveComponent"
import { FormattedMessage } from 'react-intl';

const InputCell = (props) => {
    const i = props.i;
    const j = props.j;
    const inputConfig = props.ioConfig.InputConfig;
    const ioNames = props.ioConfig.IoNames;
    const pid = ((i * 12) + (j * 1));
    const ioInfo = props.ioInfo;
    const board = pid / 12;
    const io = pid % 12;

    if (j <= ioInfo[2]*1) {
        if (i === 0 && (j === 1 || j >= 9)) {
            return `${ioNames[inputConfig.indexOf(pid.toString())]?.split(':')[0]}*`;
        } else {
            let equip = board >= 1 && io >= 7 ? {...props.ioConfig.EquipListInput, ...props.ioConfig.Lights } : props.ioConfig.EquipListInput;
            return (
                <Agristar2InputFieldSelect
                    value={inputConfig.indexOf(pid.toString())}
                    options={equip}
                    error={
                        (props.errors?.Duplicates && props.errors.Duplicates.indexOf(`i${pid}`) > -1
                            ? <FormattedMessage 
                                id='validateIoConfigTranslatedText[1].inputs'
                                defaultMessage='Duplicate or invalid Inputs defined.'/>
                            : null)
                    }
                    onChange={(e)=>props.handlePayloadToSend(`i${pid}`, e.target.value)}
                    disabled={!props.isAuthorized}
                    style={{marginTop:'-.2rem'}}
                />
            );    
        }
    } else {
        return 'N/A';
    }
}

const OutputCell = (props) => {
    const i = props.i;
    const j = props.j;
    const outputConfig = props.ioConfig.OutputConfig;
    const ioNames = props.ioConfig.IoNames;
    const pid = ((i * 12) + (j * 1));
    const board = pid / 12;
    const io = pid % 12;

    if (i === 0 && (j <= 3 || j >= 9)) {
        if (j === 9) {
            return (
                <Agristar2InputFieldSelect
                    value={outputConfig[40] || ''}
                    options={props.mapValueToLabel.PulseDoor}
                    onChange={(e)=>props.handlePayloadToSend('pulseDoor', e.target.value)}
                    disabled={!props.isAuthorized}
                    style={{marginTop:'-.2rem'}}
                />          
            );
        } else {
            return `${ioNames[outputConfig.indexOf(pid.toString())]?.split(':')[0]}*`;
        }
    } else {
        let equip = board >= 1 && io >= 7 ? {...props.ioConfig.EquipListOutput, ...props.ioConfig.Lights } : props.ioConfig.EquipListOutput;
        return (
            <Agristar2InputFieldSelect
                value={outputConfig.indexOf(pid.toString())}
                options={equip}
                error={
                    (props.errors?.Duplicates && props.errors.Duplicates.indexOf(`o${pid}`) > -1
                        ? <FormattedMessage
                            id='validateIoConfigTranslatedText[0].outputs'
                            defaultMessage='Duplicate or invalid Outputs defined.' />
                        : null) ||
                    (props.errors?.BadOutputs && props.errors.BadOutputs.indexOf(`i${pid}`) > -1
                        ? <FormattedMessage
                            id='validateIoConfigTranslatedText[2].7and8'
                            defaultMessage='Only auxiliary outputs or lights can be defined on outputs 7 and 8.' />
                        : null)
                }
                onChange={(e)=>props.handlePayloadToSend(`o${pid}`, e.target.value)}
                disabled={!props.isAuthorized}
                style={{marginTop:'-.2rem'}}
            />
        );    
    }
}

const AgristarFormP2IOConfig = (props) => {
    const {
        isAuthorized,
        ioConfig,
        mapValueToLabel,
        setToDefault,
        saving,
        errors,
        handlePayloadToSend,
        submitPayloadToSend,
    } = useASFormP2IOConfig()
    
    const saveEnabled = isAuthorized

    return(
        <SaveComponent saving={saving?.p2IoConfigFrame?.status || saving?.button2?.status}>
            <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle', padding:'0px'}}>
            {
                ioConfig.AvailableIoData.map((ioInfo, i) => 
                    ioInfo[0].indexOf('none') === -1 && (
                        <TableContainer component={Paper} key={i * 100}>
                            <Table width="100%">
                                <TableHead style={{background: 'lightsteelblue'}}>
                                    <TableRow>
                                        <TableCell>{ioInfo[0]}</TableCell>
                                        <TableCell>
                                            <FormattedMessage 
                                                id='p2IoConfigDynTranslatedText[0].output'
                                                defaultMessage='Output'
                                            />
                                        </TableCell>
                                        <TableCell>
                                        <FormattedMessage 
                                                id='p2IoConfigDynTranslatedText[1].input'
                                                defaultMessage='Input'
                                            />
                                        </TableCell>
                                    </TableRow>
                                </TableHead>
                                <TableBody>
                                    {
                                        [...Array(ioInfo[1]*1)].map((_, j) => 
                                            <TableRow key={i * 100 + j + 1}>
                                                <TableCell># {j + 1}</TableCell>
                                                <TableCell>
                                                    <OutputCell
                                                        i={i}
                                                        j={j+1}
                                                        mapValueToLabel={mapValueToLabel}
                                                        ioConfig={ioConfig}
                                                        errors={errors}
                                                        handlePayloadToSend={handlePayloadToSend}
                                                        isAuthorized={isAuthorized}
                                                    />
                                                </TableCell>
                                                <TableCell>
                                                    <InputCell
                                                        i={i}
                                                        j={j+1}
                                                        ioInfo={ioInfo}
                                                        ioConfig={ioConfig}
                                                        errors={errors}
                                                        handlePayloadToSend={handlePayloadToSend}
                                                        isAuthorized={isAuthorized}
                                                    />
                                                </TableCell>
                                            </TableRow>
                                        )
                                    }
                                </TableBody>
                            </Table>
                        </TableContainer>
                    )
                )
            }
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'center'}}>
                    <ButtonSave 
                        onClick={()=>submitPayloadToSend()}
                        disabled={!saveEnabled}
                        style={{margin:'5px auto'}}
                    />
                    <ButtonSave
                        onClick={()=>setToDefault()}
                        style={{margin:'5px auto'}}
                        disabled={!saveEnabled}
                        label={
                            <FormattedMessage 
                                id='buttonsTranslatedText[63].set.default'
                                defaultMessage='Set to Default'
                            />
                        }
                    />
                </div>
            </div>
        </SaveComponent>
    )
}
export default AgristarFormP2IOConfig;