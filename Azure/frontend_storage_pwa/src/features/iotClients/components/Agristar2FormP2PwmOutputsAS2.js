import { Agristar2InputFieldSelect } from './Agristar2InputFields'
import ButtonSave from '../../../common/ButtonSave'
import useAS2FormP2PwmOutput from '../hooks/AS2FormP2PwmOutput';
import { TableContainer, Table, TableHead, TableBody, TableRow, TableCell } from "@material-ui/core"
import { FormattedMessage } from "react-intl"
import SaveComponent from './SaveComponent';

const Agristar2FormP2PwmOutputsAS2 = (props) => {
    const {
        pwmConfig,
        pwmChannels,
        ioInfo,
        isAuthorized,
        handlePwmChannels,
        submitPayloadToSend,
        saving,
        potatoMode,
        onionMode,
        pecanMode,
    } = useAS2FormP2PwmOutput();

    const saveEnabled = () => {
        if (isAuthorized === false){
            return true
        }
    }

    let availPwmList = { '-1': 'None' };
    pwmConfig
        .filter((item, index) => (
            index !== 2 && // skip fan
            (((potatoMode || pecanMode) && item[1] === '1')
            || (onionMode && item[1] === '2')
            || item[1] === '4'
            || item[1] === '6'
            || item[1] === '7')))
        .forEach((item) => (availPwmList[pwmConfig[item[3]][3]] = pwmConfig[item[3]][0]));

    return(
        <SaveComponent saving={saving?.p2PwmFrameAS2?.status}>
            <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle'}}>
                {
                    ioInfo.map((io, i) => 
                        io[0].indexOf('none') === -1 && (
                            <TableContainer key={i}>
                                <Table>
                                    <TableHead>
                                        <TableRow>
                                            <TableCell>{io[0]}</TableCell>
                                            <TableCell>
                                                <FormattedMessage 
                                                    id='p2PwmOutputs[2].4_20'
                                                    defaultMessage='4-20 Output'
                                                />
                                            </TableCell>
                                        </TableRow>
                                    </TableHead>
                                    <TableBody>
                                        {
                                            [...Array(io[3]*1)].map((_, j) => 
                                                <TableRow key={i*100 + j}>
                                                    <TableCell># {j + 1}</TableCell>
                                                    <TableCell>
                                                    {
                                                        i === 0 && j === 1
                                                        ? `${pwmConfig[2][0]} *`
                                                        : (
                                                            <Agristar2InputFieldSelect
                                                                value={pwmChannels.indexOf((i*2+j).toString())}
                                                                options={availPwmList}
                                                                onChange={(e)=>handlePwmChannels(i, j, e.target.value)}
                                                                disabled={!isAuthorized}
                                                                style={{marginTop:'-.2rem'}}
                                                            />
                                                        )
                                                    }
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
                        disabled={saveEnabled()}
                        style={{margin:'5px auto'}}
                    />
                </div>
            </div>
        </SaveComponent>
    )
}
export default Agristar2FormP2PwmOutputsAS2;