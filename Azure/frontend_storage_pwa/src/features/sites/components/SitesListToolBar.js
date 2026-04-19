import React from 'react'
import { ButtonGroup, Button, IconButton } from "@material-ui/core"
import ToolBarHideOnScroll from '../../../common/ToolBarHideOnScroll'
import SearchIcon from '@material-ui/icons/Search';
import MapIcon from '@material-ui/icons/Map';
import TocIcon from '@material-ui/icons/Toc';
import { Tune } from "@material-ui/icons";
import { useHistory } from 'react-router';
import {appURLs} from '../../../utilities/appNavigation'
import ToolBarSpacerForViewContent from '../../../common/ToolBarSpacerForViewContent';

// const useStyles = makeStyles((theme)=>({
//     toolbar:{
//         '&.MuiToolbar-root':{
//             backgroundColor: theme.palette.primary.main,
//             margin:'5px 5px',
//         }
//     }
// }))

const SiteListToolBar = (props) => {
    // const classes = useStyles()
    const history = useHistory()

    const navHandler = (destination) => {
        history.push(destination)
    }

    return(
        <div>
            <ToolBarSpacerForViewContent />

            <ToolBarHideOnScroll >
                <IconButton color={'inherit'}>
                    <SearchIcon />
                </IconButton>

                <ButtonGroup color={'inherit'} size={'small'}>
                    <Button 
                        onClick={()=>navHandler(appURLs.siteURLs.index)}
                    >
                        <TocIcon fontSize='small' style={{margin:'auto 10px'}} />
                    </Button>
                    <Button 
                        onClick={()=>navHandler(appURLs.siteURLs.sitesMapView)} 
                    >
                        <MapIcon fontSize='small' style={{margin:'auto 10px'}}/>
                    </Button>
                </ButtonGroup>

                <IconButton color={'inherit'}>
                    <Tune />
                </IconButton>
            </ToolBarHideOnScroll>
    </div>
    )
}
export default SiteListToolBar