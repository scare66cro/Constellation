import { Dialog, DialogActions, DialogContent, DialogTitle, List, Button, ListItem, ListItemText, Divider, makeStyles } from "@material-ui/core"
import { EventAvailable, Notifications, Warning } from "@material-ui/icons"
import { useIntl } from "react-intl"
import { buttonCancel } from "../../../utilities/translationObjects"

const useStyles = makeStyles((theme)=>({
    buttonContainer:{
        '&.MuiDialogActions-root':{
            justifyContent:'center'
        }
    }
}))

const AlertsPopup = (props) => {
    const intl = useIntl();
    const classes = useStyles()
    return(
        <Dialog
            open={props.open}
            onClose={props.onClose}
            scroll={'paper'}
            fullWidth
        >
            <DialogTitle >
                <div style={{display:'flex', justifyContent:'space-between'}}>
                    <Notifications />
                    Notifications
                    <div></div>
                </div>
            </DialogTitle>
            <DialogContent>
                {/* <DialogContentText> */}

                    <List>
                        <Divider ></Divider>
                        <ListItem button >
                            <ListItemText>
                                <Warning style={{color:'orange', margin:'10px 5px -5px 0px'}}/>
                                Burns Storage 
                                <br/>
                                <div style={{marginLeft:'10px'}}>
                                    - Low Plenum Temp
                                </div>
                            </ListItemText>
                            
                        </ListItem>
                        <Divider ></Divider>
                        
                        <ListItem button>
                            <ListItemText>
                                <EventAvailable color='primary' style={{ margin:'10px 5px -5px 0px'}}/>
                                Rupert NW 
                                <br/>
                                <div style={{marginLeft:'10px'}}>
                                    - Sproutnip Treatment
                                </div>
                            </ListItemText>
                        </ListItem>
                        <Divider ></Divider>

                        <ListItem button>
                            <ListItemText>
                                <Warning style={{color:'orange', margin:'10px 5px -5px 0px'}}/>
                                Hansen Brite 
                                <br/>
                                <div style={{marginLeft:'10px'}}>
                                    - Low Plenum Temp
                                </div>
                            </ListItemText>
                        </ListItem>

                    </List>

                {/* </DialogContentText> */}
            </DialogContent>
            <DialogActions className={classes.buttonContainer} >
                <Button variant={'contained'} size={'small'} color={'secondary'}>Dismiss All</Button>
                <Button variant={'contained'} size={'small'} onClick={props.onClose} >
                    {intl.formatMessage(buttonCancel)}
                </Button>
            </DialogActions>
        </Dialog>
    )
}
export default AlertsPopup