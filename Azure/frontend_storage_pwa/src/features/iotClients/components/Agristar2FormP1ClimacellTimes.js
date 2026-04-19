import { Button, Typography } from "@material-ui/core"
import { FormattedMessage } from "react-intl"
import ButtonSave from "../../../common/ButtonSave"
import { clockFormatOptions } from "../../../utilities/appTime"
import AS2FormP1ClimacellTimes from "../hooks/AS2FormP1ClimacellTimes"
import SaveComponent from "./SaveComponent"

const TimeNode = props => {
    const usersPreferredClockFormat = props.usersPreferredClockFormat
    const index = props.index
    const mode = props.mode
    const onClick = props.onClick

    // --------HOOKS--------------
    const {mapModeToInfo} = AS2FormP1ClimacellTimes()

    return(
        <div
            onClick={()=>onClick()}
            style={{
                width:`8%`,
                margin:'1px 0px',
                backgroundColor: mapModeToInfo[mode].color,
                marginTop: ((index+1 >= 25) && (index+1 < 37) ? '5px' : '1px'),
                padding: '.4rem 0px',
                // border: selected ? 'grey solid 2px' : 'transparent solid 2px'
            }}
        >   
            <Typography 
                style={{fontSize:'.5rem', overflow:'clip'}}
                align='center'
                
            >
                { //-----------------HOURS-------------
                    usersPreferredClockFormat === clockFormatOptions.h24 ? 
                        Math.floor((index)/2)
                        :
                        ((index) < 24 ? Math.floor((index)/2) : Math.floor((index-24)/2)) === 0 ? 
                            12 //if 0:00 set it to 12:00
                            : 
                            (index) < 24 ? Math.floor((index)/2) : Math.floor((index-24)/2)
                }
                { //----------------MINUTES------------
                    `:${((index)%2) === 1 ? '30' : '00'}`
                }
            </Typography>
        </div>
    )
}

const Agristar2FormP1ClimacellTimes = (props) => {
    const {
        isAuthorized,
        payloadToSend,
        saving,
        
        mapModeToInfo,

        usersPreferredClockFormat,

        timeRange,
        selectedMode,

        handleSelectMode,
        handleTimeNodeClick,
        handleAlwaysOn,
        handleSubmit,

    } = AS2FormP1ClimacellTimes()

    const isSelected = (index) => timeRange.includes(index)

    return(
        <SaveComponent saving={saving?.p1ClimacellTimes?.status}>
            <div
                style={{width:'100%', display:'grid', justifyContent:'center'}}
            >  
                <div
                    style={{
                        width:'100%', 
                        display:'flex', 
                        justifyContent:'space-evenly',
                        flexWrap:'wrap',
                    }}
                >

                    {
                        payloadToSend.climacellTimes?.map((mode,index) => 
                            <TimeNode 
                                key={index}
                                usersPreferredClockFormat={usersPreferredClockFormat}
                                index={index}
                                mode={mode}
                                selected={isSelected(index)}
                                onClick={()=> { if (isAuthorized) handleTimeNodeClick(index)}}
                            />
                        )
                    }
                                
                </div>
                {/* <div
                    style={{width:'100%', padding:'15px'}}
                >
                    <Slider 
                        value={timeRange}
                        onChange={handleTimeRange}
                        valueLabelDisplay="auto"
                        min={0}
                        max={47}
                        disabled={!isAuthorized}
                        valueLabelFormat={
                            (number)=>`${usersPreferredClockFormat === clockFormatOptions.h24 ? 
                                Math.floor((number)/2)
                                :
                                ((number) < 24 ? Math.floor((number)/2) : Math.floor((number-24)/2)) === 0 ? 
                                    12 //if 0:00 set it to 12:00
                                    : 
                                    (number) < 24 ? Math.floor((number)/2) : Math.floor((number-24)/2)
                            }`+`:${((number)%2) === 1 ? '30' : '00'}`
                        }
                    />
                </div> */}
                <Button 
                    variant={'contained'}
                    size={'small'}
                    style={{margin:'auto',marginTop:'20px', maxWidth:'94%', backgroundColor: mapModeToInfo[selectedMode].color}}
                    fullWidth
                    onClick={()=>handleAlwaysOn()}
                    disabled={!isAuthorized}
                >
                    <FormattedMessage
                        id='buttonsTranslatedText[21].alwayson'
                        defaultMessage='Always On'
                    />
                </Button>
                <div
                    style={{width:'100%', display:'flex', justifyContent:'space-evenly', flexWrap:'wrap', marginTop:'15px'}}
                >
                    {
                        Object.keys(mapModeToInfo).map(b => 
                            <Button 
                                key={b}
                                variant={'contained'}
                                size={'small'}
                                style={{
                                    width:'45%', 
                                    margin:'5px',
                                    backgroundColor:mapModeToInfo[b].color,
                                    border:(selectedMode === b*1 ? 'grey 3px solid' : 'none')
                                }}
                                
                                onClick={()=>handleSelectMode(b*1)}
                                disabled={!isAuthorized}
                            >
                                {mapModeToInfo[b].label}
                            </Button>    
                        )

                    }
                </div>
                <div
                    style={{width:'100%', padding:'15px', display:'flex', justifyContent:'center'}}
                >
                    <ButtonSave
                        style={{marginLeft:'0px', width:'30%'}}
                        onClick={handleSubmit}
                        disabled={!isAuthorized}
                    />
                </div>
            </div>
        </SaveComponent>
    )
}
export default Agristar2FormP1ClimacellTimes