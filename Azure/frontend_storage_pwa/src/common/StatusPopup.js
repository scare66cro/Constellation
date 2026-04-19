import { Backdrop, CircularProgress, makeStyles, Paper, Typography } from "@material-ui/core"
import CheckCircleIcon from '@material-ui/icons/CheckCircle';
import ErrorIcon from '@material-ui/icons/Error';
import { useEffect } from "react";

const useStyles = makeStyles((theme) => ({
    backdrop: {
        zIndex: theme.zIndex.modal + 1,
    },
    paper: {
        minWidth:'65px',
        minHeight:'65px',
        maxWidth:'400px',
        maxHeight:'400px',
        display:'flex',
        flexDirection:'column',
        justifyContent:'center',
        alignItems:'center',
        padding:'15px'
    },
    statusIcon: {
        width:'60px',
        height:'60px',
        margin:'20px'
    },
    success: {
        color: theme.palette.success.main
    },
    failed: {
        color: theme.palette.error.main
    }
}));

const StatusComponent = (props) => {
    const classes = useStyles()

    switch(props.status){
        // case 'loading': // in the event that the state loading itself for example initially
        case 'pending':
            return <CircularProgress style={{color:'white'}} />
        case 'success':
            return <CheckCircleIcon className={`${classes.success} ${classes.statusIcon}`} />
        case 'failed':
            return <ErrorIcon className={`${classes.failed} ${classes.statusIcon}`} />
        case 'laoding':
            return 
        default: // 'idle'
            return  <></>
    }
}

const StatusPopup = (props) => {
    const classes = useStyles()

    const onClick = props.onClick 
    const successTimeout = props.successTimeout //fire on click after given timeout
    const errorTimeout = props.errorTimeout
    const status = props.status // idle -> displays nothing
    const message = props.message

    useEffect(()=>{
        if(successTimeout){
            setTimeout(function(){ 
                onClick()
            }, successTimeout);
        }
    // eslint-disable-next-line react-hooks/exhaustive-deps
    },[status==='success'])

    useEffect(()=>{
        if(errorTimeout){
            setTimeout(function(){ 
                onClick()
            }, errorTimeout);
        }
    // eslint-disable-next-line react-hooks/exhaustive-deps
    },[status==='failed'])

    return(
        <Backdrop
            open={status !== 'idle' && status !== undefined}
            className={classes.backdrop}
            onClick={onClick}
        >   
            { status === 'pending' ? 
                <StatusComponent status={status}/>
                :
                <Paper className={classes.paper} >
                    <StatusComponent status={status}/>

                    { 
                        message ? 
                        <Typography style={{margin:'10px'}} >
                                {message}
                            </Typography>
                            : ''
                    }

                </Paper>
            }

        </Backdrop>
    )
}
export default StatusPopup