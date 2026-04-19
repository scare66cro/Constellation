import {
    Box, Toolbar, Typography,
    TableContainer, Table, TableBody, TableRow, TableCell,
} from "@material-ui/core";
import { Landscape } from "@material-ui/icons";
import { FormattedMessage } from "react-intl";
import { useSelector } from "react-redux";
import { extractAgristar2PayloadFromIoTClient, selectSelectedIoTClient } from "../selectors";

const Agristar2PagePileView = (props) => {
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state));
    const payload = extractAgristar2PayloadFromIoTClient(iotClient);
    const tempLabels = payload?.['PileTempsLabels'];
    const humidLabels = payload?.['PileHumidsLabels'];
    const tempData = payload?.['PileTempsData'];
    const humidData = payload?.['PileHumidsData'];

    return (
        <> 
            <Toolbar variant='dense' style={{display:'flex', justifyContent:'space-between'}}>
                <Landscape />
                <Typography variant='h5' align={'center'} >
                    <FormattedMessage
                        id='agristar2.pageview-Title-pile-sensors'
                        defaultMessage='Pile Sensors'
                        description='Pile Sensor Page title for agristar panel'
                    />
                </Typography>
                <div></div>
            </Toolbar>
            <Box zIndex={'tooltip'}>
                {
                    tempData.length > 1 &&
                    <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle'}}>
                        <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between', maxWidth:'300px'}}>
                            <Typography variant='h6' align={'center'}>
                                <FormattedMessage
                                    id='mnPileTemps[0].title'
                                    defaultMessage='Pile Temperature Sensors'
                                />
                            </Typography>
                        </div>
                        <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between', maxWidth:'300px'}}>
                            <TableContainer>
                                <Table>
                                    <TableBody>
                                        {
                                            tempLabels.map((label, index) => (
                                                (index % 2) === 0 &&
                                                <TableRow key={index}>
                                                    <TableCell>{label}</TableCell>
                                                    <TableCell>{tempData[index]}</TableCell>
                                                </TableRow>
                                            ))
                                        }
                                    </TableBody>
                                </Table>
                            </TableContainer>
                        </div>
                    </div>
                }
                {
                    humidData.length > 1 &&
                    <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle'}}>
                        <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between', maxWidth:'300px'}}>
                            <Typography variant='h6' align={'center'}>
                                <FormattedMessage
                                    id='mnPileHumids[0].humidity'
                                    defaultMessage='Pile Humidity'
                                    description='Pile Humidity Sensor Title'
                                />
                                &nbsp; &amp; CO<sub>2</sub>
                                <FormattedMessage
                                    id='mnPileHumids[1].sensor'
                                    defaultMessage='Sensors'
                                />
                            </Typography>
                        </div>
                        <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between', maxWidth:'300px'}}>
                            <TableContainer>
                                <Table>
                                    <TableBody>
                                        {
                                            humidLabels.map((label, index) => (
                                                index % 2 === 0 && 
                                                <TableRow key={index}>
                                                    <TableCell>{label}</TableCell>
                                                    <TableCell>{humidData[index]}</TableCell>
                                                </TableRow>
                                            ))
                                        }
                                    </TableBody>
                                </Table>
                            </TableContainer>
                        </div>
                    </div>
                }
            </Box>
        </>
    );
};

export default Agristar2PagePileView;