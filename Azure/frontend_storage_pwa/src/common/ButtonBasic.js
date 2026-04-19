import { Button } from '@material-ui/core'
// import { makeStyles } from '@material-ui/core/styles'

// https://material-ui.com/components/buttons/#contained-buttons
// const useStyles = makeStyles((theme)=>({
//     default:{

//     }
// }))

const ButtonBasic = (props) => {
    // button types:
    // 1. basic action
    // 2. 


    return(
        
        <Button
            fullwidth={props.fullwidth}
            color={props.color || 'default'}
            size={props.size || 'small'}
            variant={'contained'}
            onClick={props.onClick}
        >
            {props.children}
        </Button>
    )
}
export default ButtonBasic