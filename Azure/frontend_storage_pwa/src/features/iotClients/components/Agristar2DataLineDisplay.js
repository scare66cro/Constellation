import { Typography } from "@material-ui/core"
import { getFontColorForDifferential } from "../../../utilities/dataMapAgristar2"


const Agristar2DataLineDisplay = (props) => {
    let dataUnit = props.dataUnit
    let actual = props.actual
    let setPoint = props.setPoint
    let lowDiff = props.lowDifferential //this is the lower break point that cause font to turn orange
    let highDiff = props.highDifferential // this is the upper break point that causes text to turn red
    let setPointHidden = props.setPointHidden
    let customColor = props.customColor
    let customerColor2 = props.customColor2
    let setPoint2 = props.setPoint2
    let showSetPoint2 = props.showSetPoint2
    let actual2 = props.actual2

    let variantTypography = props.variantTypography || 'body2'

    return(
        <div style={{display: 'flex', flexFlow: 'row'}}>
            <Typography variant={variantTypography} style={{marginRight: '2px',...props.style}} >
                <span 
                    style={{fontWeight:'bolder',
                        color:
                        (   customColor ? customColor :
                            getFontColorForDifferential(actual, setPoint, lowDiff, highDiff)
                        )
                    }}
                >
                    {actual || '--'}&nbsp;{dataUnit}
                </span> 
            </Typography>
            {
                !setPointHidden  &&
                <>
                    <Typography variant={variantTypography} style={{marginRight: '2px',...props.style}}>|</Typography>
                    <Typography variant={variantTypography} style={{marginRight: '2px',...props.style}}>
                        {setPoint || '--'}&nbsp;{dataUnit}
                    </Typography>
                    {
                        showSetPoint2 &&
                        <>
                            <Typography variant={variantTypography} style={{marginRight: '2px',...props.style}}>/</Typography>
                            <Typography variant={variantTypography} style={{marginRight: '2px',...props.style}}>
                                <span style={{color: customerColor2}}>{setPoint2 || '--'}&nbsp;{dataUnit}</span>
                            </Typography>
                        </>
                    }
                </>
            }
            {
                actual2 && actual2 !== 'dis' && actual2 !== '--' &&
                <Typography variant={variantTypography} style={{marginRight: '2px',...props.style}} >
                    <span 
                        style={{fontWeight:'bolder',
                            color:
                            (   customColor ? customColor :
                                getFontColorForDifferential(actual2, setPoint, lowDiff, highDiff)
                            )
                        }}
                    >
                        |{actual2}&nbsp;{dataUnit}
                    </span> 
                </Typography>
            }
        </div>
    )
}
export default Agristar2DataLineDisplay