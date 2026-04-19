import { makeStyles, Paper, Toolbar, Typography } from "@material-ui/core";

const useStyles = makeStyles((theme)=>({
    root:{
        minHeight:`calc(100vh - 56px)`
    },
}))

const PageView = (props) => {
    // ---------------PROPS---------------
    const pageIcon = props.pageIcon
    const pageTitle = props.pageTitle

    // ---------------HOOKS---------------
    const classes = useStyles()

    return(
        <Paper square
            className={classes.root}
        >
            <Toolbar >
                <div  style={{marginRight:'15px'}}>
                    {pageIcon}
                </div>
                <Typography variant={'h6'}>
                    {pageTitle}
                </Typography>
            </Toolbar>

                {props.children}

        </Paper>
    )
}
export default PageView