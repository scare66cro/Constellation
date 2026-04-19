import {
    TableContainer, Table, TableBody, TableRow, TableCell,
    Paper
} from '@material-ui/core'
import ButtonSave from '../../../common/ButtonSave';
import { FormattedMessage, useIntl } from 'react-intl';
import Agristar2InputField from './Agristar2InputFields';
import AS2FormP1RunTimes from '../hooks/AS2FormP1RunTimes';
import SaveComponent from './SaveComponent';
import { agristar2Refrigeration } from '../../../utilities/translationObjects';

const Agristar2FormP1RunTimes = (props) => {
    const intl = useIntl();
    const {
        payload,
        isAuthorized,
        resetDaily,
        resetTotal,
        saving,
        onionMode,
    } = AS2FormP1RunTimes();

    return (
        <SaveComponent saving={saving?.gellert?.status}>
            <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle'}}>
                <TableContainer component={Paper}>
                    <Table>
                        <TableBody>
                            <TableRow>
                                <TableCell>
                                    <FormattedMessage 
                                        id='p1FanRuntimes[1].daily'
                                        defaultMessage='Daily Fan Runtime (since noon)'
                                    />
                                </TableCell>
                                <TableCell>
                                    <Agristar2InputField
                                        value={payload.dailyFan}
                                        disabled={!isAuthorized}
                                        style={{marginTop:'-.2rem'}}
                                        endAdornment='hrs'
                                    />
                                </TableCell>
                                <TableCell>
                                <ButtonSave 
                                    label={
                                        <FormattedMessage 
                                            id='buttonsTranslatedText[20].reset'
                                            defaultMessage='Reset'
                                        />
                                    }
                                    disabled={!isAuthorized}
                                    onClick={()=>resetDaily()}
                                />
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell>
                                    <FormattedMessage 
                                        id='p1FanRuntimes[2].runtime-total'
                                        defaultMessage='Total Fan Runtime'
                                    />
                                </TableCell>
                                <TableCell>
                                    <Agristar2InputField
                                        value={payload.totalFan}
                                        disabled={!isAuthorized}
                                        style={{marginTop:'-.2rem'}}
                                        endAdornment='hrs'
                                    />
                                </TableCell>
                                <TableCell>
                                <ButtonSave 
                                    label={
                                        <FormattedMessage 
                                            id='buttonsTranslatedText[20].reset'
                                            defaultMessage='Reset'
                                        />
                                    }
                                    disabled={!isAuthorized}
                                    onClick={()=>resetTotal()}
                                />
                                </TableCell>
                            </TableRow>
                        </TableBody>
                    </Table>
                </TableContainer>
                <TableContainer component={Paper}>
                    <Table>
                        <TableBody>
                            <TableRow>
                                <TableCell>
                                    {intl.formatMessage(agristar2Refrigeration)}
                                </TableCell>
                                <TableCell>
                                    <Agristar2InputField
                                        value={payload.totalRefrigeration}
                                        disabled={!isAuthorized}
                                        style={{marginTop:'-.2rem'}}
                                        endAdornment='hrs'
                                    />
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell>
                                    <FormattedMessage 
                                        id='p1FanRuntimes[4].cooling'
                                        defaultMessage='Cooling'
                                    />
                                </TableCell>
                                <TableCell>
                                    <Agristar2InputField
                                        value={payload.totalCooling}
                                        disabled={!isAuthorized}
                                        style={{marginTop:'-.2rem'}}
                                        endAdornment='hrs'
                                    />
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell>
                                    <FormattedMessage 
                                        id='p1FanRuntimes[5].recirculation'
                                        defaultMessage='Recirculation'
                                    />
                                </TableCell>
                                <TableCell>
                                    <Agristar2InputField
                                        value={payload.totalRecirculation}
                                        disabled={!isAuthorized}
                                        style={{marginTop:'-.2rem'}}
                                        endAdornment='hrs'
                                    />
                                </TableCell>
                            </TableRow>
                            {onionMode &&
                                <TableRow>
                                    <TableCell>
                                        <FormattedMessage 
                                            id='p1FanRuntimes[6].cure'
                                            defaultMessage='Cure'
                                        />
                                    </TableCell>
                                    <TableCell>
                                        <Agristar2InputField
                                            value={payload.totalCure}
                                            disabled={!isAuthorized}
                                            style={{marginTop:'-.2rem'}}
                                            endAdornment='hrs'
                                        />
                                    </TableCell>
                                </TableRow>
                            }
                            <TableRow>
                                <TableCell>
                                    <FormattedMessage 
                                        id='p1FanRuntimes[7].standby'
                                        defaultMessage='Standby'
                                    />
                                </TableCell>
                                <TableCell>
                                    <Agristar2InputField
                                        value={payload.totalStandby}
                                        disabled={!isAuthorized}
                                        style={{marginTop:'-.2rem'}}
                                        endAdornment='hrs'
                                    />
                                </TableCell>
                            </TableRow>
                        </TableBody>
                    </Table>
                </TableContainer>
            </div>
        </SaveComponent>
    );
}

export default Agristar2FormP1RunTimes;
