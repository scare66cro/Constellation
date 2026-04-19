import { Card, Typography } from "@material-ui/core"
import { EventAvailable, Warning } from "@material-ui/icons";
import ScheduleIcon from '@material-ui/icons/Schedule';
import { useSelector } from "react-redux";
import TimeFormatted from "../../../common/TimeFormatted";
import DateFormatted from "../../../common/DateFormatted";
import { sitesListSelector } from "../../sites/selectors";

const EventsListItem = (props) => {

    let event = props.event

    let date = new Date(event.started_at)

    const sitesList = useSelector(sitesListSelector) || []

    const getSiteById = (id) => sitesList.filter(s => s.id === id)[0] 

    return(
        <>
        <div style={{display:'flex', margin:'3px 1px'}}>
            <div style={{width:'15%', maxWidth:'40px', margin:'5px'}}>
                <Typography variant={'subtitle2'} >
                    <DateFormatted date={date} month='short' />
                </Typography>
                <Typography variant={'h5'} >
                    {date.getDate() }
                </Typography>
                <Typography variant={'subtitle2'}>
                    {date.getFullYear()}
                </Typography>
            </div>
            <Card style={{padding:'10px', width:'80%'}}>
                <Typography >
                    {
                        event.model === 'iot_site_event' && event.category === 'Warning' ?
                        <Warning style={{color:'orange', position:'relative', top:'5px', marginRight:'10px'}}/>
                        :
                        <EventAvailable style={{position:'relative', top:'5px', marginRight:'10px'}} />
                    }
                    
                    <TimeFormatted date={date} hours='numeric' />

                    <ScheduleIcon fontSize={'small'} style={{position:'relative', top:'3px', left:'5px', color:'lightgray'}}/>
                </Typography>
                <Typography >
                    <span style={{fontWeight:'bolder'}}>{event.category || 'n/a'} </span> - {event.title || 'n/a'}
                </Typography>
                <Typography style={{fontStyle:'italic'}} >
                    {getSiteById(event.site) ? getSiteById(event.site).name : '-no site'}
                </Typography>
            </Card>
        </div>
        </>
    )
}
export default EventsListItem