import { useState, useEffect } from 'react'
import { Card, CardHeader, IconButton, makeStyles } from "@material-ui/core"
import { getSiteName } from '../selectors';
import { useDispatch, useSelector } from 'react-redux';
import { selectIoTClientsBySite } from '../../iotClients/selectors';
import { getAgristar2FrontMatter } from '../../iotClients/actions';
import IoTClientList from '../../iotClients/components/IoTClientList';
import RefreshIcon from '@material-ui/icons/Refresh';
// import { FormattedMessage, useIntl } from 'react-intl';
// import { sitesListViewNoSites } from '../../../utilities/translationObjects';
// import { getSiteName } from '../selectors';

const useStyles = makeStyles((theme)=>({
    card:{
        // backgroundColor: theme.palette.warning.main,
        padding: "10px 10px 3px",
        marginBottom:'5px',
        '&.MuiPaper-rounded':{
            borderRadius: '2px'
        }
    },
    cardHeader:{
        '& .MuiCardHeader-content':{
            maxWidth:'72%',
        },
        '&.MuiCardHeader-root':{
            padding:'0px',
            justifyContent:'space-between'
        }
    },
    badge:{
        '& .MuiBadge-badge':{
            zIndex:0
        }
    }   
}))


const SitesListItem = (props) => {

    const classes = useStyles()

    const site = props.site

    const dispatch = useDispatch()

    const [loading, setLoading] = useState(0);

    // -----------IOTCLIENTS-------------
    const state = useSelector(state => state);

    const [iotClients, setIoTClients] = useState(selectIoTClientsBySite(state, site?.id));

    useEffect(() => {
        setIoTClients(selectIoTClientsBySite(state, site?.id));
    }, [state, site?.id])

    const refreshIoTClients = () => {
        const refreshClients = iotClients.filter(iotclient => iotclient.is_active);
        setLoading(refreshClients.length);
        refreshClients.forEach(iotclient => {
            dispatch(getAgristar2FrontMatter(iotclient, updateLoading));
        });
    }

    function updateLoading() {
        setLoading((prev) => prev - 1);
    }

    return(
        <Card className={classes.card} >
            <CardHeader 
                className={classes.cardHeader}
                title={getSiteName(site) || '--'}
                titleTypographyProps={{
                    variant:'h6',
                    noWrap:true,
                    // onClick:()=>navHandler(appURLs.siteURLs.siteView(site.id))
                }}
                action={
                    <div style={{padding:'5px 5px 0px 0px', display:'flex', justifyContent:'right'}}>
                        <IconButton
                            disabled={loading !== 0}
                            onClick={()=>refreshIoTClients()}
                        >
                            <RefreshIcon style={{color: loading ? 'grey' : 'black'}} />
                        </IconButton>
                        {/* <IconButton 
                            style={{paddingLeft:'2px'}}
                            onClick={()=>console.log('go to site events')}
                        >
                            <Badge className={classes.badge}  color={'primary'} variant='dot' badgeContent={'10'} >
                                <EventsIcon />
                            </Badge>
                        </IconButton> */}

                    </div>
                }
                />

{/* SITE - DEFAULT DEVICE */}
            <IoTClientList 
                iotClients={iotClients}
            />
        </Card>
    )
}
export default SitesListItem