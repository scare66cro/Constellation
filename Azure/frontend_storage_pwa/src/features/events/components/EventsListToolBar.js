import { Button, ButtonGroup, IconButton } from "@material-ui/core"
import { Toc, DateRange, Tune, Search } from "@material-ui/icons"
import { useHistory } from "react-router"
import ToolBarHideOnScroll from '../../../common/ToolBarHideOnScroll'
import ToolBarSpacerForViewContent from "../../../common/ToolBarSpacerForViewContent"
import {appURLs} from '../../../utilities/appNavigation'


const EventsListToolBar = (props) => {
    // const classes = useStyles()

    const history = useHistory()

    const navHandler = (destination) => {
        history.push(destination)
    }

    return(
        <div>
            {/* <div className={classes.spacerTop} > */}
                <ToolBarSpacerForViewContent />

                <ToolBarHideOnScroll >

                    <IconButton color={'inherit'}>
                        <Search />
                    </IconButton>

                    <ButtonGroup 
                        color={'inherit'} size={'small'}
                    >
                        <Button 
                            onClick={()=>navHandler(appURLs.eventUrls.index)}
                        >
                            <Toc fontSize='small' style={{margin:'auto 10px'}} />
                        </Button>
                        <Button 
                            onClick={()=>navHandler(appURLs.eventUrls.eventsCalendar)}
                        >
                            <DateRange  fontSize='small' style={{margin:'auto 10px'}} />
                        </Button>
                    </ButtonGroup>

                    <IconButton color={'inherit'}>
                        <Tune />
                    </IconButton>

                </ToolBarHideOnScroll>
            {/* </div> */}
        </div>
    )
}
export default EventsListToolBar