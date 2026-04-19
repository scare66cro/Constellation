import { AppBar, Slide, Toolbar, useScrollTrigger, makeStyles } from "@material-ui/core"

const useStyles = makeStyles((theme)=>({
    slide:{
        margin: "60px 3px 0px 3px",
        // margin: "56px 0px 0px",
        width: `calc(100% - 6px)`,
        zIndex:1
    },
    appBar:{
        '&.MuiAppBar-colorPrimary':{
            backgroundColor:'green',
        }
    },
    toolbar:{
        maxHeight:'40px',
        justifyContent:'space-between',
        '&.MuiToolbar-root':{
            backgroundColor: theme.palette.primary.main,
            margin:'5px 5px',
        },
        '&.MuiToolbar-regular': {
            height: '35px',
            minHeight:'35px'
        }
    }
}))

const HideOnScroll = (props) => {
    const classes = useStyles()

    const trigger = useScrollTrigger()

    return(
        <Slide 
            appear={false} 
            direction="down" 
            in={!trigger} 
            className={classes.slide}
        >
            <AppBar variant={'outlined'} className={classes.appBar}>
                <Toolbar  
                    // position={'sticky'}
                    disableGutters={props.disableGutters || true}
                    className={classes.toolbar}
                >
                    {props.children}
                </Toolbar>
            </AppBar>
        </Slide>
    )
}
export default HideOnScroll