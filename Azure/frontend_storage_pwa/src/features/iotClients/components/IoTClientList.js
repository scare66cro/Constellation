import { Typography } from "@material-ui/core";
import { useEffect, useState } from 'react';
import { useIntl } from "react-intl";
import { iotClientsListEmptyMSG, iotClientTypeUnknown } from "../../../utilities/translationObjects";
import IoTClientListItemAgristar2 from "./IoTClientListItemAgristar2";

const IoTClientList = (props) => {

    const [iotClients, setIoTClients] = useState(props.iotClients.filter((i) => i.is_active));

    useEffect(() => {
        setIoTClients(props.iotClients.filter((i) => i.is_active));
    }, [props.iotClients]);
    const intl = useIntl()
    // -------------------- _status LISTENER---------------
    // const iotClientStatus = useSelector(selectStatus)
    // const dispatch = useDispatch()

    return(
        <>  
            {/* <StatusPopup 
                status={iotClientStatus} 
                open={iotClientStatus !== 'idle' && iotClientStatus !== undefined} 
                onClick={()=>dispatch(resetStatus())}
                successTimeout={1500}
            /> */}
            {
                iotClients.length > 0 ?
                    iotClients.map(iotClient => {
                        switch (iotClient.client_type) {
                            case 'agristar1':
                            case 'nova':
                                return(
                                    <IoTClientListItemAgristar2 
                                        key={iotClient.id}
                                        iotClient={iotClient}
                                    />    
                                )
                            case 'agristar2':
                                return(
                                    <IoTClientListItemAgristar2 
                                        key={iotClient.id}
                                        iotClient={iotClient}
                                    />    
                                )
                            default: 
                                return(
                                    <Typography>
                                        {intl.formatMessage(iotClientTypeUnknown)}
                                    </Typography>
                                )
                        }
                    })
                    :
                    <div style={{width:'100%', display:'flex', marginBottom:'5px'}}>
                        <Typography align='center' style={{margin:'auto'}}>
                            <i>
                                {intl.formatMessage(iotClientsListEmptyMSG)}
                            </i>
                        </Typography>
                    </div>
            }
        </>
    );
}
export default IoTClientList