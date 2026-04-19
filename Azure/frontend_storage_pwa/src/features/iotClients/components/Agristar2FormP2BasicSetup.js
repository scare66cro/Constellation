import Agristar2InputField, { Agristar2InputFieldSelect } from './Agristar2InputFields'
import ButtonSave from '../../../common/ButtonSave'
import useAS2FormP2Basic from '../hooks/AS2FormP2Basic';
import {
    Typography, TableContainer, Table, TableBody, TableRow, TableCell,
    FormControlLabel, Checkbox
} from "@material-ui/core"
import { FormattedMessage } from "react-intl"
import SaveComponent from './SaveComponent';

const Agristar2FormP2BasicSetup = (props) => {
    const {
        payloadToSend,
        isAuthorized,
        mapValueToLabel,
        handlePayloadToSend,
        submitPayloadToSend,
        saving,
        errors,
        beeMode,
        homePage,
        systemModeLabel,
    } = useAS2FormP2Basic()

    const saveEnabled = () => {
        if (isAuthorized === false){
            return true
        }
    }

    return(
        <SaveComponent saving={saving?.p2BasicSetup?.status}>
            <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle'}}>
                <TableContainer>
                    <Table>
                        <TableBody>
                            <TableRow>
                                <TableCell align="right">
                                    <Typography >
                                        <FormattedMessage 
                                            id='p2BasicSetup[1].storage.name'
                                            defaultMessage='Storage Name'
                                        />
                                    </Typography>
                                </TableCell>
                                <TableCell align="left">
                                    <Agristar2InputField 
                                        type={'text'}
                                        value={payloadToSend.StorageName || ''}
                                        error={errors.StorageName}
                                        onChange={(e)=>handlePayloadToSend('StorageName', e.target.value)}
                                        disabled={!isAuthorized}
                                        style={{maxWidth: '150px'}}
                                    />
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell align="right">
                                    <Typography component='div'>
                                        <FormattedMessage 
                                            id='p2BasicSetup[2].home.page'
                                            defaultMessage='Home Page'
                                        />
                                    </Typography>
                                </TableCell>
                                <TableCell align="left">
                                    <Agristar2InputFieldSelect 
                                        value={payloadToSend.HomePage}
                                        options={homePage}
                                        error={errors.HomePage}
                                        onChange={(e)=>handlePayloadToSend('HomePage', e.target.value)}
                                        disabled={!isAuthorized}
                                        style={{marginTop:'-.2rem', marginLeft:'10px', paddingLeft:'5px'}}
                                    />
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell align="right">
                                    <Typography >
                                        <FormattedMessage 
                                            id='p2BasicSetup[13].remote.login.password'
                                            defaultMessage='Remote Login Password'
                                        />
                                    </Typography>
                                </TableCell>
                                <TableCell align="left">
                                    <Agristar2InputField 
                                        type={'text'}
                                        value={payloadToSend.dlr0 || ''}
                                        error={errors.dlr0}
                                        onChange={(e)=>handlePayloadToSend('dlr0', e.target.value)}
                                        disabled={!isAuthorized}
                                    />
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell align="right">
                                    <Typography component='div'>
                                        <FormattedMessage 
                                            id='p2BasicSetup[14].require.remote.login'
                                            defaultMessage='Require Remote Login'
                                        />
                                    </Typography>
                                </TableCell>
                                <TableCell align="left">
                                    <Agristar2InputFieldSelect 
                                        value={payloadToSend.loginSecure}
                                        options={mapValueToLabel.loginSecure}
                                        error={errors.loginSecure}
                                        onChange={(e)=>handlePayloadToSend('loginSecure', e.target.value)}
                                        disabled={!isAuthorized}
                                        style={{marginTop:'-.2rem', marginLeft:'10px', paddingLeft:'5px'}}
                                    />
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell align="right">
                                    <Typography component='div'>
                                        <FormattedMessage 
                                            id='p2BasicSetup[17].temperature.display'
                                            defaultMessage='Temperature Display'
                                        />
                                    </Typography>
                                </TableCell>
                                <TableCell align="left">
                                    <Agristar2InputFieldSelect 
                                        value={payloadToSend.TempType}
                                        options={mapValueToLabel.TempType}
                                        error={errors.TempType}
                                        onChange={(e)=>handlePayloadToSend('TempType', e.target.value)}
                                        disabled={!isAuthorized}
                                        style={{marginTop:'-.2rem', marginLeft:'10px', paddingLeft:'5px'}}
                                    />
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell align="right">
                                    <Typography component='div'>
                                        <FormattedMessage 
                                            id='p2BasicSetup[20].system.mode'
                                            defaultMessage='System Mode'
                                        />
                                    </Typography>
                                </TableCell>
                                <TableCell align="left">
                                    <Agristar2InputFieldSelect 
                                        value={payloadToSend.SystemMode}
                                        options={systemModeLabel}
                                        error={errors.SystemMode}
                                        onChange={(e)=>handlePayloadToSend('SystemMode', e.target.value)}
                                        disabled={!isAuthorized}
                                        style={{marginTop:'-.2rem', marginLeft:'10px', paddingLeft:'5px'}}
                                    />
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell align="right">
                                    <Typography >
                                        <FormattedMessage 
                                            id='p2BasicSetup[23].multi.view'
                                            defaultMessage='Number of Systems to Multi-View'
                                        />
                                    </Typography>
                                </TableCell>
                                <TableCell align="left">
                                    <Agristar2InputField 
                                        type={'number'}
                                        value={payloadToSend.MultiView || ''}
                                        error={errors.MultiView}
                                        onChange={(e)=>handlePayloadToSend('MultiView', e.target.value)}
                                        disabled={!isAuthorized}
                                    />
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell align="right">
                                    <Typography component='div'>
                                        <FormattedMessage 
                                            id='p2BasicSetup[24].animations'
                                            defaultMessage='Display Equipment Animations'
                                        />
                                    </Typography>
                                </TableCell>
                                <TableCell align="left">
                                    <Agristar2InputFieldSelect 
                                        value={payloadToSend.Animations}
                                        options={mapValueToLabel.Animations}
                                        error={errors.Animations}
                                        onChange={(e)=>handlePayloadToSend('Animations', e.target.value)}
                                        disabled={!isAuthorized}
                                        style={{marginTop:'-.2rem', marginLeft:'10px', paddingLeft:'5px'}}
                                    />
                                </TableCell>
                            </TableRow>
                            {
                                beeMode &&
                                <TableRow>
                                    <TableCell align="right">
                                        <Typography component="div">
                                            <FormattedMessage
                                                id='p2BasicSetup[25].co2sensor'
                                                defaultMessage='CO2 Sensor'
                                            />
                                        </Typography>
                                    </TableCell>
                                    <TableCell align="left">
                                        <FormControlLabel
                                            control={
                                                <Checkbox
                                                    checked={payloadToSend.CO2_50K === '1' ? true : false}
                                                    onChange={(e) => handlePayloadToSend('CO2_50K', e.target.checked)}
                                                    color='primary'
                                                    disabled={!isAuthorized}
                                                />
                                            }
                                            label={
                                                <FormattedMessage
                                                    id='p2BasicSetup[26].use50K'
                                                    defaultMessage='Use 50K Sensor'
                                                />
                                            }
                                        />
                                    </TableCell>
                                </TableRow>
                            }
                        </TableBody>
                    </Table>
                </TableContainer>
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
export default Agristar2FormP2BasicSetup;