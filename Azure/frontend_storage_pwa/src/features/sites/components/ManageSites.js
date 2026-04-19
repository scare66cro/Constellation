import { makeStyles, List, Typography, Card } from '@material-ui/core';
import { useIntl } from 'react-intl';
import { sitesListViewNoSites } from '../../../utilities/translationObjects';
import { extractSiteById } from "../selectors";
import ManageSiteItem from './ManageSiteItem';

const useStyles = makeStyles((theme)=>({
  list:{
    '&.MuiList-padding':{
        padding:'0px 4px 8px'
    }
  },
  header:{
    backgroundColor:theme.palette.primary.light,
    color:theme.palette.primary.contrastText,
    position: 'sticky',
    marginBottom:'50px',
    zIndex:'1',
  },
  card:{
    // backgroundColor: theme.palette.warning.main,
    padding: "10px 10px 3px",
    marginBottom:'5px',
    '&.MuiPaper-rounded':{
        borderRadius: '2px'
    }
  },
}));

const ManageSites = (props) => {
  const intl = useIntl();
  const classes = useStyles();
  const siteList = props.siteList;
  const getSite = (site_id) => extractSiteById(props.sites, site_id);
  
  return (
    <List className={classes.list}>
      {
        siteList === undefined || siteList.length === 0
        ?
          <Card className={classes.card}>
            <Typography style={{textAlign: 'center', margin:'10px'}}>
              {intl.formatMessage(sitesListViewNoSites)}
            </Typography>
          </Card>
        :
          siteList.map((id) => 
            <ManageSiteItem
              key={id}
              site={getSite(id)}
              panels={props.panels}
              addPanel={props.addPanel}
              setActiveClient={props.setActiveClient}
              setPanelDialog={props.setPanelDialog}
            />
          )
      }
    </List>
  );
};

export default ManageSites;