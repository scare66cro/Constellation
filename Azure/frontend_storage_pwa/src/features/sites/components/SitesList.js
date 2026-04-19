import { List, makeStyles, Typography } from "@material-ui/core"
import { sitesListViewNoSites } from '../../../utilities/translationObjects';
import SitesListItem from './SitesListItem';
import { useIntl } from 'react-intl';
import { extractSiteById, selectSitesList } from '../selectors';
import { useSelector } from 'react-redux';

const useStyles = makeStyles((theme)=>({
    list:{
        '&.MuiList-padding':{
            padding:'0px 4px 8px'
        }
    },
    card:{
        // backgroundColor: theme.palette.warning.main,
        padding: "10px",
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


const SitesList = (props) => {

    const classes = useStyles()
    const intl = useIntl()

    const sitesList = props.sitesList
    const sites = useSelector((state)=>selectSitesList(state))
    const getSite = (site_id) => extractSiteById(sites,site_id)

    return(
        <List className={classes.list}>
            {
                sitesList === undefined || sitesList === 0 ? 

                <div>
                    <Typography style={{textAlign:'center', margin:'10px'}}>
                        {intl.formatMessage(sitesListViewNoSites)}
                    </Typography>
                </div>
                :
                sitesList.map(id => 
                    <SitesListItem 
                        key={id}
                        site={getSite(id)}
                    />
                )
            }
        </List>
    )
}

export default SitesList