import { useSelector } from "react-redux"
import { Switch, useRouteMatch, Route } from "react-router"
import { eventsListSelector } from "../selectors"
import EventsCalendar from "./EventsCalendar"
import EventsList from "./EventsList"
import EventsListToolBar from "./EventsListToolBar"
import NewEventDialog from "./NewEventDialog"


const EventsView = (props) => {

    let {path} = useRouteMatch()

    const eventsList = useSelector(eventsListSelector)

    const getTimeStamp = (date) => new Date(date).getTime()

    const sortByMostRecent = (a,b) => {
        if(getTimeStamp(a.started_at) < getTimeStamp(b.started_at)){return 1}
        return -1
    }

    let sortedEvents = eventsList !== undefined ? 
        eventsList.slice().sort(sortByMostRecent) 
        : 
        eventsList

    return(
        <div>
            <EventsListToolBar />
            
            <NewEventDialog />

            <Switch >
                <Route exact path={path} >
                    <EventsList events={ sortedEvents } />
                </Route>

                <Route exact path={`${path}/calendar`} >
                    <EventsCalendar />
                </Route>
            </Switch> 

        </div>
    )
}
export default EventsView