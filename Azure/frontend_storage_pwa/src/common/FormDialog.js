import { Dialog, DialogActions, DialogContent, DialogTitle, makeStyles, Typography } from "@material-ui/core"

const useStyles = makeStyles((theme)=>({
    dialog: {
        '& .MuiDialog-paper':{
            margin:'15px',
            padding:'10px',
            borderRadius:'3px'
        },
        '& .MuiDialog-paperFullWidth':{
            width:'100%'
        },
        '& .MuiDialogContent-dividers':{
            padding: '16px 10px'
        },
    }
}))

const FormDialog = (props) => {
    // props:
    // -> dialogtitle
    // -> dialogaction
    
    const classes = useStyles()

    return(
        <Dialog
            fullWidth
            maxWidth={'md'}
            className={classes.dialog}
            {...props}
        >
            <DialogTitle disableTypography hidden={!props.hasOwnProperty('dialogtitle')}>
                <Typography variant='h5' align='center'>
                    {props.dialogtitle || ''}
                </Typography>
            </DialogTitle>
            <DialogContent dividers >
                {props.children}
            </DialogContent>

            {
                !props.hasOwnProperty('dialogactions') ?
                <></> :
                <DialogActions >
                    {props.dialogactions}
                </DialogActions>
            }

        </Dialog>
    )
}
export default FormDialog