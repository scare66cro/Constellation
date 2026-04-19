import { Grid, Typography } from "@material-ui/core"
import { ArrowBackIos, ArrowForwardIos } from "@material-ui/icons"


const EventsCalendar = (props) => {

    function dayList (){
        let days = []
        for(var x = 1; x <= 31; x++){
            days.push(x)
        }
        return days
    }

    const days = dayList()

    return(
        <Grid container >
            <Grid item xs={12} sm={12} md={12}
                style={{height:'30px', display:'flex', justifyContent:'space-around', margin:'4px', color:'#3d3d3d'}}
            >   
                <ArrowBackIos />
                <Typography variant='h6' style={{fontWeight:'bolder', textAlign:'center', color:'#3d3d3d'}}>
                    May 2019
                </Typography>
                <ArrowForwardIos />
            </Grid>
            <Grid item  xs={12} sm={12} md={12}
                style={{height:'17px', display:'flex', justifyContent:'space-around', margin:'0px'}}
            >
                <div>Sun</div>
                <div>Mon</div>
                <div>Tues</div>
                <div>Wed</div>
                <div>Thurs</div>
                <div>Fri</div>
                <div>Sat</div>
            </Grid>
            {
                days.map((day, index) => 
                <Grid item key={index}
                style={{padding:'5px', backgroundColor:'#FFFFFF' , borderRadius:'5px', margin: '.58%', width:'13%', height:'60px'}}
                >
                        <div style={{width:'100%', height:'100%'}}>
                                {day}
                        </div>
                    </Grid>
                )
            }
        </Grid>
    )
}

export default EventsCalendar