import { useDispatch, useSelector } from "react-redux";
import { Box, CircularProgress, Toolbar, Typography } from "@material-ui/core";
import { SystemUpdateAlt } from "@material-ui/icons";
import { FormattedMessage } from "react-intl";
import {
    extractAgristar2PayloadFromIoTClient,
    extractIoTClientVersion,
    selectSelectedIoTClient,
    selectUpgrade,
} from "../selectors";
import Agristar2InputField from "./Agristar2InputFields";
import ButtonSave from "../../../common/ButtonSave";
import { postUpgradeAction  } from "../actions";

const Agristar2PageViewUpgradeClient = (props) => {
    // -----------------------SELECTORS---------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state));
    const payload = extractAgristar2PayloadFromIoTClient(iotClient);
    const iotClientVersion = extractIoTClientVersion(payload);
    const upgrade = useSelector((state)=>selectUpgrade(state));

    const dispatch = useDispatch();

    const upgradeClient = () => {
        dispatch(postUpgradeAction(iotClient.id, upgrade.version));
    }

    return (
        iotClient.inUpgrade
        ?
        <div style={{width:'100%', display:'flex', justifyContent:'center', alignItems:'center', height:'100%', paddingTop:'16px', paddingBottom: '16px'}}>
            <CircularProgress />
        </div>
        :
        <>
        <Toolbar variant='dense' style={{display:'flex', justifyContent:'space-between'}}>
            <SystemUpdateAlt />
            <Typography variant='h5' align={'center'} >
                <FormattedMessage 
                        id='agristar2.pageview-Title-upgrade'
                        defaultMessage='Upgrade Panel IoT Client'
                        description='Title for upgrading iot client on panel'
                />
            </Typography>
            <div></div>
        </Toolbar>
        <Box zIndex={'tooltip'}>
            <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle', padding:'0px 5%'}}>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'center'}}>
                    <Typography align='justify' component='div' >
                        <FormattedMessage 
                            id='agristar2.upgrade-current'
                            defaultMessage='Current Version'
                            description='Display Current Version'
                        />
                        <Agristar2InputField 
                            type={'text'}
                            readonly
                            value={iotClientVersion || ''}
                        />
                    </Typography>
                </div>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'center'}}>
                    <Typography align='justify' component='div' >
                        <FormattedMessage 
                            id='agristar2.upgrade-new'
                            defaultMessage='New Version'
                            description='Display Newest Version'
                        />
                        <Agristar2InputField 
                            type={'text'}
                            readonly
                            value={upgrade.version || ''}
                        />
                    </Typography>
                </div>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'center'}}>
                    <Typography align='justify' component='div'>
                        <i>{upgrade.description}</i>
                    </Typography>
                </div>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'center'}}>
                    <ButtonSave
                        style={{margin:'8px 0px', minWidth:'51%'}}
                        onClick={()=>upgradeClient()}
                        label={
                            <FormattedMessage 
                                id='agristar2.upgrade-client'
                                defaultMessage='Upgrade Client'
                                description='Upgrade Client Button label'
                            />
                        }
                    />
                </div>
            </div>
        </Box>
        </>
    );
}

export default Agristar2PageViewUpgradeClient;
