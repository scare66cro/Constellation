import { Typography } from "@material-ui/core"
import { useIntl } from "react-intl"
import { eventsListNoEvents } from "../../../utilities/translationObjects"
import EventsListItem from "./EventsListItem"


const EventsList = (props) => {
    
    const intl = useIntl()

    let events = props.events 

    return(
        <>
            {
                events === undefined || events === [] ? 
                <div >
                    <Typography style={{textAlign:'center', margin:'10px'}} >
                        { intl.formatMessage(eventsListNoEvents) }
                    </Typography>
                </div> 
                :
                events.map(e => 
                    <EventsListItem event={e} key={e.id} />
                )
            }
        </>
    )
}
export default EventsList