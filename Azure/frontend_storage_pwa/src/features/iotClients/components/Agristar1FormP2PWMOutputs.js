import { Agristar2InputFieldSelect } from './Agristar2InputFields'
import ButtonSave from '../../../common/ButtonSave'
import useAS1FormP2PwmOutput from '../hooks/AS1FormP2PwmOutput';
import { TableContainer, Table, TableHead, TableBody, TableRow, TableCell } from "@material-ui/core"
import { FormattedMessage, useIntl } from "react-intl"
import SaveComponent from './SaveComponent';
import { agristar2Refrigeration } from '../../../utilities/translationObjects';

const Agristar1FormP2PwmOutputs = (props) => {
    const intl = useIntl();
    const { payloadToSend, isAuthorized, channelOptions, saving, handlePayloadToSend, submitPayloadToSend } = useAS1FormP2PwmOutput()

    return(
        <SaveComponent saving={saving?.p2PwmFrame?.status}>
            <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle'}}>
                <TableContainer>
                    <Table>
                        <TableHead>
                            <TableRow>
                                <TableCell>
                                    <FormattedMessage
                                        id='p2PwmOutputs[1].equipment'
                                        defaultMessage='Equipment'
                                    />
                                </TableCell>
                                <TableCell>
                                    <FormattedMessage 
                                        id='p2PwmOutputs[2].4_20'
                                        defaultMessage='4-20 Output'
                                    />
                                </TableCell>
                            </TableRow>
                        </TableHead>
                        <TableBody>
                            <TableRow>
                                <TableCell>
                                    <FormattedMessage
                                        id='p2PwmOutputs[3].doors'
                                        defaultMessage='Fresh Air Doors'
                                    />
                                </TableCell>
                                <TableCell>
                                <Agristar2InputFieldSelect
                                    value={payloadToSend.selDoors}
                                    options={channelOptions}
                                    onChange={(e)=>handlePayloadToSend('selDoors', e.target.value)}
                                    disabled={!isAuthorized}
                                    style={{marginTop:'-.2rem'}}
                                />
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell>
                                    {intl.formatMessage(agristar2Refrigeration)}
                                </TableCell>
                                <TableCell>
                                <Agristar2InputFieldSelect
                                    value={payloadToSend.selRefrig}
                                    options={channelOptions}
                                    onChange={(e)=>handlePayloadToSend('selRefrig', e.target.value)}
                                    disabled={!isAuthorized}
                                    style={{marginTop:'-.2rem'}}
                                />          
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell>
                                    <FormattedMessage
                                        id='p2PwmOutputs[5].fan'
                                        defaultMessage='Fan'
                                    />
                                </TableCell>
                                <TableCell>
                                <Agristar2InputFieldSelect
                                    value={payloadToSend.selFan}
                                    options={channelOptions}
                                    onChange={(e)=>handlePayloadToSend('selFan', e.target.value)}
                                    disabled={!isAuthorized}
                                    style={{marginTop:'-.2rem'}}
                                />
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell>
                                    <FormattedMessage
                                        id='p2PwmOutputs[6].burner'
                                        defaultMessage='Burner'
                                    />
                                </TableCell>
                                <TableCell>
                                <Agristar2InputFieldSelect
                                    value={payloadToSend.selBurner}
                                    options={channelOptions}
                                    onChange={(e)=>handlePayloadToSend('selBurner', e.target.value)}
                                    disabled={!isAuthorized}
                                    style={{marginTop:'-.2rem'}}
                                />
                                </TableCell>
                            </TableRow>
                        </TableBody>
                    </Table>
                </TableContainer>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'center'}}>
                    <ButtonSave 
                        onClick={()=>submitPayloadToSend()}
                        disabled={!isAuthorized}
                        style={{margin:'5px auto'}}
                    />
                </div>
            </div>
        </SaveComponent>
    )
}
export default Agristar1FormP2PwmOutputs;
