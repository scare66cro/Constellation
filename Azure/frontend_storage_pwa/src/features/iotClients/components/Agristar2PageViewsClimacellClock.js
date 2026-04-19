import { Box, makeStyles, Toolbar, Typography } from "@material-ui/core"
import { Waves } from "@material-ui/icons"
import { FormattedMessage } from "react-intl"
import Agristar2FormP1ClimacellTimes from "./Agristar2FormP1ClimacellTimes"

const useStyles = makeStyles((theme) => ({
    rotate90:{
        transform: 'rotate(90deg)',
    }
}))

const Agristar2PageViewClimacellRunClock = (props) => {
    const classes = useStyles();

    return(
        <> 
            <Toolbar variant='dense' style={{display:'flex', justifyContent:'space-between'}}>
                <Waves className={classes.rotate90}/>
                <Typography variant='h5' align={'center'} >
                    <FormattedMessage 
                            id='p1ClimacellTimes[0].title'
                            defaultMessage='Climacell Control'
                    />
                </Typography>
                <div></div>
            </Toolbar>
            <Box zIndex={'tooltip'}>
                <Agristar2FormP1ClimacellTimes />
            </Box>
        </>
    )
}
export default Agristar2PageViewClimacellRunClock