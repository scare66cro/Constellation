import { IconButton } from "@material-ui/core"
import { Lock, NoEncryption } from "@material-ui/icons"

const Agristar2ButtonUnsecuredIp = (props) => {
    const unsecured_ip = props.unsecured_ip
    const style = props.style

    const onClickUnsecuredIp = () => {
        window.open(unsecured_ip)
    }

    return(
        <>
            {
                unsecured_ip ?
                <IconButton 
                    style={{padding:'0px', top:'-3px', ...style}}  
                    onClick={()=>onClickUnsecuredIp()} 
                >                                      
                    <NoEncryption fontSize='small' color='secondary' 
                        style={{color:'#d90000', margin:'2px 4px', fontSize:'1rem',  ...props.style}}
                    />
                </IconButton>
                : 
                <IconButton 
                    style={{padding:'0px', top:'-3px', ...style}} 
                >  
                    <Lock fontSize='small' color='primary' 
                        style={{margin:'2px 4px', fontSize:'1rem',  ...props.style}}
                    />
                </IconButton>
            }
        </>
    )
}
export default Agristar2ButtonUnsecuredIp