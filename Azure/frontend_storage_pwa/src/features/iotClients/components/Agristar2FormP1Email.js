import {
    Table,
    TableBody,
    TableCell,
    TableContainer,
    TableRow,
    Typography
} from "@material-ui/core"
import { useEffect, useState } from "react"
import { FormattedMessage } from "react-intl"
import ButtonSave from "../../../common/ButtonSave"
import useASFormP1Email from "../hooks/AS2FormP1Email"
import Agristar2InputField, { Agristar2InputFieldSelect } from "./Agristar2InputFields"
import SaveComponent from "./SaveComponent"

const Agristar2FormP1Email = (props) => {

    const {
        isAuthorized,
        payloadToSend,
        mapValueToLabel,
        displayList,
        saving,
        errors,
        handlePayloadToSend,
        submitPayloadToSend,
        findNodes,
    } = useASFormP1Email()

    const saveEnabled = isAuthorized
    const [inputDisabled, setInputDisabled] = useState(!isAuthorized || payloadToSend.selEmailAlert === '1');

    useEffect(() => {
        setInputDisabled(!isAuthorized || payloadToSend.selEmailAlert === '1');
    }, [payloadToSend.selEmailAlert, isAuthorized]);

    return(
        <SaveComponent saving={saving?.p1Comm?.status || saving?.p1CommDisplay?.status}>
            <div style={{width:'100%', display:'flex', flexDirection:'column', padding:'0px'}}>
                <TableContainer>
                    <Table>
                        <TableBody>
                            <TableRow>
                                <TableCell align='right'>
                                    <Typography component='div'>
                                        <FormattedMessage 
                                            id='p1Comm[1].send.email'
                                            defaultMessage='Send Email Alerts:'
                                        />
                                    </Typography>
                                </TableCell>
                                <TableCell align="left" colSpan="3">
                                    <Agristar2InputFieldSelect 
                                        value={payloadToSend.selEmailAlert}
                                        options={mapValueToLabel.selEmailAlert}
                                        error={errors.selEmailAlert}
                                        onChange={(e)=>handlePayloadToSend('selEmailAlert', e.target.value)}
                                        disabled={!isAuthorized}
                                        style={{marginTop:'-.2rem', marginLeft:'10px', paddingLeft:'5px'}}
                                    />
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell align="right">
                                    <Typography component='div'>
                                        <FormattedMessage 
                                            id='p1Comm[4].to'
                                            defaultMessage='To:'
                                        />
                                    </Typography>
                                </TableCell>
                                <TableCell align="left" colSpan="3">
                                    <Agristar2InputField
                                        type={'text'}
                                        value={payloadToSend.emailTo}
                                        error={errors.emailTo}
                                        onChange={(e)=>handlePayloadToSend('emailTo', e.target.value)}
                                        disabled={inputDisabled}
                                        style={{minWidth:'250px', maxWidth:'400px'}}
                                    />
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell align="right">
                                    <Typography component='div'>
                                        <FormattedMessage 
                                            id='p1Comm[5].from'
                                            defaultMessage='From:'
                                        />
                                    </Typography>
                                </TableCell>
                                <TableCell align="left" colSpan="3">
                                    <Agristar2InputField
                                        type={'text'}
                                        value={payloadToSend.emailFrom}
                                        error={errors.emailFrom}
                                        onChange={(e)=>handlePayloadToSend('emailFrom', e.target.value)}
                                        disabled={inputDisabled}
                                        style={{minWidth:'250px', maxWidth:'400px'}}
                                    />
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell align="right">
                                    <Typography component='div'>
                                        <FormattedMessage 
                                            id='p1Comm[6].email.server'
                                            defaultMessage='Email Server:'
                                        />
                                    </Typography>
                                </TableCell>
                                <TableCell align="left" colSpan="3">
                                    <Agristar2InputField
                                        type={'text'}
                                        value={payloadToSend.emailServer}
                                        error={errors.emailServer}
                                        onChange={(e)=>handlePayloadToSend('emailServer', e.target.value)}
                                        disabled={inputDisabled}
                                        style={{minWidth:'200px', maxWidth:'400px'}}
                                    />
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell align="right">
                                    <Typography component='div'>
                                        <FormattedMessage 
                                            id='p1Comm[7].authentication.type'
                                            defaultMessage='Authentication Type:'
                                        />
                                    </Typography>
                                </TableCell>
                                <TableCell align="left">
                                    <Agristar2InputFieldSelect 
                                        value={payloadToSend.selEmailAuthType}
                                        options={mapValueToLabel.selEmailAuthType}
                                        error={errors.selEmailAuthType}
                                        onChange={(e)=>handlePayloadToSend('selEmailAuthType', e.target.value)}
                                        disabled={inputDisabled}
                                        style={{marginTop:'-.2rem', marginLeft:'10px', paddingLeft:'5px'}}
                                    />
                                </TableCell>
                                <TableCell align="right">
                                    <Typography component='div' style={{ marginLeft: '20px'}}>
                                        <FormattedMessage 
                                            id='p1Comm[11].port'
                                            defaultMessage='Port:'
                                        />
                                    </Typography>
                                </TableCell>
                                <TableCell align="left">
                                    <Agristar2InputField
                                        type={'number'}
                                        value={payloadToSend.emailPort}
                                        error={errors.emailPort}
                                        onChange={(e)=>handlePayloadToSend('emailPort', e.target.value)}
                                        disabled={inputDisabled}
                                        style={{maxWidth: '75px'}}
                                    />
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell align="right">
                                    <Typography component='div'>
                                        <FormattedMessage 
                                            id='p1Comm[12].account'
                                            defaultMessage='Account:'
                                        />
                                    </Typography>
                                </TableCell>
                                <TableCell align="left" colSpan="3">
                                    <Agristar2InputField
                                        type={'text'}
                                        value={payloadToSend.emailAccount}
                                        error={errors.emailAccount}
                                        onChange={(e)=>handlePayloadToSend('emailAccount', e.target.value)}
                                        disabled={inputDisabled}
                                        style={{minWidth:'250px', maxWidth:'400px'}}
                                    />
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell align="right">
                                    <Typography component='div'>
                                        <FormattedMessage 
                                            id='p1Comm[13].password'
                                            defaultMessage='Password:'
                                        />
                                    </Typography>
                                </TableCell>
                                <TableCell align="left" colSpan="3">
                                    <Agristar2InputField
                                        type={'password'}
                                        value={payloadToSend.emailPassword}
                                        error={errors.emailPassword}
                                        onChange={(e)=>handlePayloadToSend('emailPassword', e.target.value)}
                                        disabled={inputDisabled}
                                    />
                                </TableCell>
                            </TableRow>
                            <TableRow>
                                <TableCell align="right">
                                    <Typography component='div'>
                                        <FormattedMessage 
                                            id='p1Comm[14].send.display'
                                            defaultMessage='Send from Display:'
                                        />
                                    </Typography>
                                </TableCell>
                                <TableCell align="center" colSpan="3">
                                    <Agristar2InputFieldSelect 
                                        value={payloadToSend.selEmailDisplay}
                                        options={displayList}
                                        error={errors.selEmailDisplay}
                                        onChange={(e)=>handlePayloadToSend('selEmailDisplay', e.target.value)}
                                        disabled={inputDisabled}
                                        style={{marginTop:'-.2rem', marginLeft:'5px', paddingLeft:'5px', minWidth:'150px'}}
                                    />
                                    <ButtonSave 
                                        onClick={()=>findNodes()}
                                        label="Find"
                                        style={{margin:'5px auto'}}
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
    )
}
export default Agristar2FormP1Email;