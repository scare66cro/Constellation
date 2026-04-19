import { useSelector } from 'react-redux';
import { _statusSelector } from '../selectors';
import { selectIoTStatus } from '../../iotClients/selectors';
import SitesList from './SitesList';

import mapdemo from '../../../assets/images/map-demo.JPG'
import { Route, Switch, useRouteMatch } from 'react-router';
import { Card, CircularProgress, makeStyles, Typography } from '@material-ui/core';
import { selectCustomers, selectCustomersStatus } from '../../customers/selectors';
import { customerStatus } from '../../customers/reducer';
import { FormattedMessage } from 'react-intl';

const useStyle = makeStyles((theme)=>({
    header:{
        backgroundColor:theme.palette.primary.light,
        color:theme.palette.primary.contrastText,
        position: 'sticky',
        top:'56px',
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
}))

const CustomersListComp = props => {
    const classes = useStyle();

    // ---------SELECTORS-------------
    const _status = useSelector((state)=>selectCustomersStatus(state))
    const customers = useSelector((state)=>selectCustomers(state))

    return(
        <>
        {
            _status === customerStatus.pending ? 
                <div 
                    style={{ marginTop:'15px', width:'100%', display:'flex', justifyContent:'center'}}
                >
                    <CircularProgress />
                </div>
                :
                (
                    customers.length > 0 ?
                    customers.map(c => 
                        <div key={c.id}>
                            <div style={{margin:'2px 3px'}} className={classes.header}>
                                <Typography align="center">
                                    {c.name}
                                </Typography>
                            </div>
                            {
                                c.sites.length > 0 ?
                                <SitesListComp 
                                    sites={c?.sites}
                                />
                                :
                                <Card className={classes.card}>
                                    <FormattedMessage 
                                        id='customer-no-sites-to-view'
                                        defaultMessage='No sites to view'
                                    />
                                </Card>
                            }
                        </div>
                    )
                    : 
                    <div>
                        <Typography style={{textAlign:'center', margin:'10px'}}>
                            <FormattedMessage 
                                id='customers-list-no-items'
                                defaultMessage='No customer storages to view'
                            />
                        </Typography>
                    </div>
                )
        }
        </>
    )
}

const SitesListComp = props => {
    // ---------PROPS-------------
    const sites = props.sites

    // ---------SELECTORS----------
    const _status = useSelector(_statusSelector)
    const _iot = useSelector(selectIoTStatus);

    return(
        <>
        {
            _iot === 'pending' || _status === 'pending' ? 
                    <div 
                        style={{ marginTop:'15px', width:'100%', display:'flex', justifyContent:'center'}}
                    >
                        <CircularProgress />
                    </div>
                : 
                <SitesList sitesList={sites} />
        }
        </>
    )
}

const SiteListView = (props) => {
    let {path} = useRouteMatch()
    // status of site store
    // load filters

    // search parameters settings

    // 
    return(
        <div>
            {/* <SiteListToolBar /> */}

            <Switch>
                <Route exact path={path} >
                {/* <Route exact path={'/sites'} > */}
                    <CustomersListComp />
                </Route>
                
                {/* Map view of sites */} 
                <Route exact path={`${path}/map`}>
                {/* <Route exact path={`/sites/map`}> */}
                    <div style={{ overflow:'hidden'}}>
                        <img src={mapdemo} alt="map" style={{width:'100vw'}}/>
                    </div>
                </Route>


            </Switch>
        </div> 
    )
}
export default SiteListView