import { Dialog, DialogActions, DialogContent, DialogTitle, List, Button, ListItem, ListItemText, makeStyles, Typography, ListItemIcon } from "@material-ui/core"
import { ArrowRight } from "@material-ui/icons";
import ReportProblemRoundedIcon from '@material-ui/icons/ReportProblemRounded';
import { useIntl } from "react-intl";
import { agristar2AlarmsListEmpty, alarmPopupTitle, buttonCancel, buttonDismissAll } from "../../../utilities/translationObjects";

const useStyles = makeStyles((theme)=>({
    buttonContainer:{
        '&.MuiDialogActions-root':{
            justifyContent:'center'
        }
    }
}))

const AlarmsPopup = (props) => {
    const classes = useStyles()
    // ------------------------PROPS------------------------------
    const open = props.open
    const onClose = props.onClose
    const lineItems = props.lineItems
    const onClickActionButton = props.onClickActionButton
    const isAuthorized = props.isAuthorized;

    const intl = useIntl()

    return(
        <Dialog
            open={open}
            onClose={onClose}
            scroll={'paper'}
            fullWidth
        >
            <DialogTitle >
                <div style={{display:'flex', justifyContent:'space-between'}}>
                    <ReportProblemRoundedIcon 
                        style={{color:'#d90000', margin:'5px 0px 0px'}} 
                        // className='blinker'
                    />
                    <Typography variant={'h6'} align={'center'}>
                        {intl.formatMessage(alarmPopupTitle)}
                    </Typography>
                    <div></div>
                </div>
            </DialogTitle>
            <DialogContent dividers style={{padding:'8px'}}>
                {/* <DialogContentText> */}

                    <List >
                        {/* <Divider ></Divider> */}
                        {   lineItems ? 
                            lineItems?.map((item, index) => 
                                <ListItem key={index} >
                                    <ListItemIcon style={{minWidth:'30px'}}>
                                        <ArrowRight />
                                    </ListItemIcon>
                                    <ListItemText>
                                        <Typography variant={'body2'} >
                                            {item}
                                        </Typography>
                                    </ListItemText>
                                </ListItem>
                            )
                            :
                            <Typography align='center'>
                                {intl.formatMessage(agristar2AlarmsListEmpty)}
                            </Typography>
                        }

                    </List>

                {/* </DialogContentText> */}
            </DialogContent>
            <DialogActions className={classes.buttonContainer} >
                <Button 
                    onClick={onClickActionButton}
                    variant={'contained'} 
                    size={'small'} 
                    color={'secondary'}
                    disabled={!isAuthorized}
                >
                    {intl.formatMessage(buttonDismissAll)}
                </Button>
                <Button variant={'contained'} size={'small'} onClick={props.onClose} >
                    {intl.formatMessage(buttonCancel)}
                </Button>
            </DialogActions>
        </Dialog>
    )
}
export default AlarmsPopup