import { CircularProgress, Typography } from "@material-ui/core"
import { useEffect, useState } from "react"
import { useIntl } from "react-intl"
import { useDispatch, useSelector } from "react-redux"
import { useRouteMatch } from "react-router"
import { iotClientTypeUnknown } from "../../../utilities/translationObjects"
import { getAgristar2Data, setSelectedIotClient } from "../actions"
import { selectSelectedIoTClient, selectStatus } from "../selectors"
import IoTClientViewAgristar2 from "./IoTClientViewAgristar2"

const DeviceView = (props) => {

    const dispatch = useDispatch()
    const intl = useIntl()

    const [selected, setSelected] = useState(false);

    const [latch, setLatch] = useState(false);

    const [ready, setReady] = useState(false);

    // 1. set the selected iotClient
    let {params} = useRouteMatch()
    let iotClientID = params.device_id

    useEffect(()=>{
        dispatch(setSelectedIotClient(iotClientID))
    },[iotClientID, dispatch])

    // 2. get the iotclient from the redux store
    const iotClient = useSelector((state) => selectSelectedIoTClient(state))
    const iotClientStatus = useSelector((s)=>selectStatus(s))

    useEffect(() => {
        if (iotClient && !selected && iotClientID === iotClient.id) {
            dispatch(getAgristar2Data(iotClient));
            setSelected(true);
        }
    }, [iotClient, selected, iotClientID, dispatch]);

    useEffect(() => {
        if (!latch && iotClient && iotClient.status === 'pending') {
            setLatch(true);
        }
    }, [iotClient, latch]);

    useEffect(() => {
        if (latch && iotClient && iotClient.status === 'idle') {
            setReady(true);
        }
    }, [iotClient, latch, ready]);


    // 3. determine the IoTClient's type and return correct IoTClientViewComponent
    const getIoTClientComponent = () => {
        switch (iotClient?.client_type) {
            case 'agristar1':
            case 'agristar2':
            case 'nova':
                return(
                    // AGRISTAR2
                    // This is a fully complete view with its own redux-store hooks & routing included. 
                    // Perhaps this is what will get pulled out and used for the Panel UI as a stand alone component.
                    ready
                    ?
                        <IoTClientViewAgristar2 
                            iotClient={iotClient}
                        />
                    :
                        <div 
                            style={{ marginTop:'15px', width:'100%', display:'flex', justifyContent:'center'}}
                        >
                            <CircularProgress />
                        </div>
                )
            default:
                return(
                    <>
                        { iotClientStatus === 'idle' ? 
                            <Typography>
                                {intl.formatMessage(iotClientTypeUnknown)}
                            </Typography>
                            :
                            <div 
                                style={{ marginTop:'15px', width:'100%', display:'flex', justifyContent:'center'}}
                            >
                                <CircularProgress />
                            </div>
                        }
                    </>
                )
            }
    }

    return(
        <>
            { getIoTClientComponent() }
        </>
    )
}
export default DeviceView