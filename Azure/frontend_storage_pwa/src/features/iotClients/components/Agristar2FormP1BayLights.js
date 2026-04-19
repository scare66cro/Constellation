import { FormattedMessage } from 'react-intl';
import ButtonSave from '../../../common/ButtonSave'
import useAS2FormP1BayLights from '../hooks/AS2FormP1BayLights';
import SaveComponent from './SaveComponent';
import { Typography, TableContainer, Table, TableBody, TableRow, TableCell } from '@material-ui/core';
import Agristar2InputField from './Agristar2InputFields';

const Agristar2FormP1BayLights = (props) => {
    const {
        payloadToSend,
        isAuthorized,
        submitPayloadToSend,
        handlePayloadToSend,
        saving,
        errors,
        status,
        toggle,
        iotClient,
    } = useAS2FormP1BayLights();

    const inputDisabled = !isAuthorized;
    const saveEnabled = isAuthorized;

    return (
        <SaveComponent saving={saving?.p1BaylightNames?.status}>
            <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle'}}>
                <TableContainer>
                    <Table>
                        <TableBody>
                            <TableRow>
                                <TableCell align="center">
                                    <Typography>
                                        <FormattedMessage 
                                            id='mnEquipStatus[1].output'
                                            defaultMessage='Output'
                                        />
                                    </Typography>
                                </TableCell>
                                <TableCell align="center">
                                    <FormattedMessage 
                                        id='mnEquipStatus[2].status'
                                        defaultMessage='Status'
                                    />
                                </TableCell>
                                <TableCell align="center">
                                    <FormattedMessage 
                                        id='baylight.control'
                                        defaultMessage='Control'
                                    />
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell>
                                    <Agristar2InputField
                                        type={'text'}
                                        value={payloadToSend.lightsBay1Label}
                                        onChange={(e)=>handlePayloadToSend('lightsBay1Label', e.target.value)}
                                        disabled={inputDisabled}
                                        error={errors.lightsBay1Label}
                                        style={{minWidth:'250px', maxWidth:'400px'}}
                                    />
                                </TableCell>
                                <TableCell>
                                    <Typography style={{ color: iotClient.front_matter.main[19] === '1' ? 'red' : 'green' }}>
                                        {status.bay1}
                                    </Typography>
                                </TableCell>
                                <TableCell>
                                    <ButtonSave 
                                        label={
                                            <FormattedMessage 
                                                id='toggle'
                                                defaultMessage='Toggle'
                                            />
                                        }
                                        onClick={()=>toggle('lights1Btn')}
                                        disabled={inputDisabled}
                                    />
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell>
                                    <Agristar2InputField
                                        type={'text'}
                                        value={payloadToSend.lightsBay2Label}
                                        onChange={(e)=>handlePayloadToSend('lightsBay2Label', e.target.value)}
                                        error={errors.lightsBay2Label}
                                        disabled={inputDisabled}
                                        style={{minWidth:'250px', maxWidth:'400px'}}
                                    />
                                </TableCell>
                                <TableCell>
                                    <Typography style={{ color: iotClient.front_matter.main[21] === '1' ? 'red' : 'green' }}>
                                        {status.bay2}
                                    </Typography>
                                </TableCell>
                                <TableCell>
                                    <ButtonSave 
                                        label={
                                            <FormattedMessage 
                                                id='toggle'
                                                defaultMessage='Toggle'
                                            />
                                        }
                                        onClick={()=>toggle('lights2Btn')}
                                        disabled={inputDisabled}
                                    />
                                </TableCell>
                            </TableRow>
                        </TableBody>
                    </Table>
                </TableContainer>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'center'}}>
                    <ButtonSave 
                        onClick={()=>submitPayloadToSend()}
                        disabled={!saveEnabled}
                        style={{margin:'5px auto'}}
                    />
                </div>
            </div>
        </SaveComponent>
    );
}

export default Agristar2FormP1BayLights;