import { Box, Toolbar, Typography } from "@material-ui/core"
import { Schedule } from "@material-ui/icons"
import { FormattedMessage } from "react-intl"
import Agristar2FormP1RunTimes from "./Agristar2FormP1RunClock"


const Agristar2PageViewSystemRunClock = (props) => {
    
    return(
        <> 
            <Toolbar variant='dense' style={{display:'flex', justifyContent:'space-between'}}>
                <Schedule />
                <Typography variant='h5' align={'center'} >
                    <FormattedMessage 
                            id='mnRunTimes[0].title'
                            defaultMessage='System Run Clock'
                    />
                </Typography>
                <div></div>
            </Toolbar>
            <Box zIndex={'tooltip'}>
                <Agristar2FormP1RunTimes />
            </Box>
        </>
    )
}
export default Agristar2PageViewSystemRunClock