import { makeStyles } from "@material-ui/core"

const useStyles = makeStyles((theme)=>({
    spacerTop: theme.mixins.toolbar
}))

const ToolBarSpacerForViewContent = (props) => {
    const classes = useStyles()

    let id = props.id
    let style = props.style

    return (
        <div id={id} className={classes.spacerTop} style={{...style}}></div>
    )
}

export default ToolBarSpacerForViewContent