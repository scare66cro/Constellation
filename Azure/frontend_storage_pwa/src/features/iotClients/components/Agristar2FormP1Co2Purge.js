import { Typography } from "@material-ui/core"
import { FormattedMessage } from "react-intl"
import Agristar2InputField, { Agristar2InputFieldSelect } from "./Agristar2InputFields"
import useAS2FormP1Co2Purge from "../hooks/AS2FormP1Co2Purge"
import ButtonSave from "../../../common/ButtonSave"
import { UnitsComponent } from "../../../utilities/appUnitsOfMeasurements"
import SaveComponent from "./SaveComponent"


const Agristar2FormP1Co2Purge = (props) => {

    // -------------HOOKS--------------------
    const {
        isAuthorized,

        displayData,
        mapValueToLabel,
        saving,
        errors,

        handleSelPurgeMode,
        handlePurgeHours,
        handleCo2SetPoint,
        handleCo2Target,
        handleMinTemp,
        handleMaxTemp,
        handleTime,
        handleFanOutput,
        handleDoorOutput,
        submit,

    } = useAS2FormP1Co2Purge()

    const inputDisabled = !isAuthorized || displayData.selPurgeMode === '0'
    const saveEnabled = isAuthorized // && !errors === false

    return(
        <SaveComponent saving={saving?.p1Co2Purge?.status}>
            <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle', padding:'0px 5%'}}>
                <div style={{margin:'0px auto', width:'100%', display:'flex', justifyContent:'space-between', maxWidth:'350px'}}>
                    <Typography style={{fontWeight:'bolder'}} >
                        CO<sub>2</sub>&nbsp;
                        <FormattedMessage 
                            id='p1Co2Purge[2].mode'
                            defaultMessage='Purge Control Mode:'
                        />
                    </Typography>
                    <Agristar2InputFieldSelect
                        value={displayData.selPurgeMode || ''}
                        options={mapValueToLabel.selPurgeMode}
                        onChange={(e)=>handleSelPurgeMode(e.target.value)}
                        error={errors.selPurgeMode}
                        disabled={!isAuthorized}
                        style={{marginTop:'-.2rem'}}
                    />
                </div>
                    {
                        displayData.selPurgeMode === '3'
                        ? 
                        <>
                            <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between'}}>
                                <Typography style={{maxWidth:'100%', color:(inputDisabled ? 'grey' : 'black')}} component={'div'}>
                                    <FormattedMessage 
                                        id='p1Co2Purge[18].target'
                                        defaultMessage='Target'
                                    />
                                    &nbsp;CO<sub>2</sub>&nbsp;
                                    <FormattedMessage 
                                        id='p1Co2Purge[19].is'
                                        defaultMessage='is'
                                    />
                                    <Agristar2InputField 
                                        type={'number'}
                                        value={displayData.co2Target}
                                        onChange={(e)=>handleCo2Target(e.target.value)}
                                        error={errors.co2Target}
                                        endAdornment={<UnitsComponent typeOfData={'ppm'} />}
                                        disabled={inputDisabled}
                                        style={{width:'100px'}}
                                    />
                                </Typography>
                            </div>
                            <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between'}}>
                                <Typography style={{maxWidth:'100%', color:(inputDisabled ? 'grey' : 'black')}} component={'div'}>
                                    <FormattedMessage
                                        id='p1Co2Purge[20].freshair'
                                        defaultMessage='The Fresh Air doors will adjust based on current'
                                    />
                                    &nbsp;CO<sub>2</sub>&nbsp;
                                    <FormattedMessage
                                        id='p1Co2Purge[21].freshair'
                                        defaultMessage='levels.'
                                    />
                                </Typography>
                            </div>
                        </>
                        :
                        <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between'}}>
                            <Typography style={{maxWidth:'100%', color:(inputDisabled ? 'grey' : 'black')}} align='justify' component={'div'}>
                                {   
                                    displayData.selPurgeMode === '1' ?
                                        // {/* IF PURGE MODE IS MANUAL */}
                                        <>
                                            <FormattedMessage 
                                                id='p1Co2Purge[6].msg1'
                                                defaultMessage='A'
                                            />
                                            &nbsp;CO<sub>2</sub>&nbsp;
                                            <FormattedMessage 
                                                id='p1Co2Purge[7].msg2'
                                                defaultMessage='purge will occur if '
                                            />
                                            &nbsp;
                                            <FormattedMessage 
                                                id='p1Co2PurgeDynTranslatedText[3].purge'
                                                defaultMessage='it has been'
                                            />
                                            &nbsp;
                                            <Agristar2InputField 
                                                type={'number'}
                                                value={displayData.PurgeHours}
                                                onChange={(e)=>handlePurgeHours(e.target.value)}
                                                error={errors.PurgeHours}
                                                // endAdornment={<UnitsComponent typeOfData={'relativeHumidity'} />}
                                                disabled={inputDisabled}
                                                style={{width:'100px'}}
                                            />
                                            &nbsp;
                                            <FormattedMessage 
                                                id='p1Co2PurgeDynTranslatedText[4].last'
                                                defaultMessage='hours since the last purge'
                                            />
                                        </>
                                        :
                                        // {/* IF PURGE MODE IS AUTOMATIC */}
                                        <>
                                            <FormattedMessage 
                                                id='p1Co2Purge[6].msg1'
                                                defaultMessage='A'
                                            />
                                            &nbsp;CO<sub>2</sub>&nbsp;
                                            <FormattedMessage 
                                                id='p1Co2Purge[7].msg2'
                                                defaultMessage='purge will occur if'
                                            />
                                            &nbsp;
                                            <FormattedMessage 
                                                id='p1Co2PurgeDynTranslatedText[0].co2'
                                                defaultMessage='the CO'
                                            />
                                            <sub>2</sub>&nbsp;
                                            <FormattedMessage 
                                                id='p1Co2PurgeDynTranslatedText[1].level'
                                                defaultMessage='level is above'
                                            />
                                            &nbsp;
                                            <Agristar2InputField 
                                                type={'number'}
                                                value={displayData.co2SetPoint}
                                                onChange={(e)=>handleCo2SetPoint(e.target.value)}
                                                error={errors.co2SetPoint}
                                                endAdornment={<UnitsComponent typeOfData={'ppm'} />}
                                                disabled={inputDisabled}
                                                style={{width:'100px'}}
                                            />
                                        </>
                                }
                                &nbsp;
                                <FormattedMessage 
                                    id='p1Co2Purge[8].between'
                                    defaultMessage='and the outside air temperature is between a minimum of'
                                />
                                &nbsp;
                                <Agristar2InputField 
                                    type={'number'}
                                    value={displayData.minTemp}
                                    onChange={(e)=>handleMinTemp(e.target.value)}
                                    error={errors.minTemp}
                                    endAdornment={<UnitsComponent typeOfData={'temperature'} />}
                                    disabled={inputDisabled}
                                    style={{width:'100px'}}
                                />
                                &nbsp;
                                <FormattedMessage 
                                    id='p1Co2Purge[9].maximum'
                                    defaultMessage='and a maximum of'
                                />
                                &nbsp;
                                <Agristar2InputField 
                                    type={'number'}
                                    value={displayData.maxTemp}
                                    onChange={(e)=>handleMaxTemp(e.target.value)}
                                    error={errors.maxTemp}
                                    endAdornment={<UnitsComponent typeOfData={'temperature'} />}
                                    disabled={inputDisabled}
                                    style={{width:'100px'}}
                                />
                                &nbsp;
                                <FormattedMessage 
                                    id='p1Co2Purge[16].chinese1'
                                    defaultMessage=' '
                                />
                                <FormattedMessage 
                                    id='p1Co2Purge[10].last'
                                    defaultMessage='The purge will last for'
                                />
                                &nbsp;
                                <Agristar2InputField 
                                    type={'number'}
                                    value={displayData.time}
                                    onChange={(e)=>handleTime(e.target.value)}
                                    error={errors.time}
                                    disabled={inputDisabled}
                                    style={{width:'100px'}}
                                />
                                &nbsp;
                                <FormattedMessage 
                                    id='p1Co2Purge[11].minutes'
                                    defaultMessage='minutes with'
                                />
                                &nbsp;
                                <FormattedMessage 
                                    id='p1Co2Purge[12].fan'
                                    defaultMessage='a fan output of'
                                />
                                &nbsp;
                                <Agristar2InputField 
                                    type={'number'}
                                    value={displayData.fanOutput}
                                    onChange={(e)=>handleFanOutput(e.target.value)}
                                    error={errors.fanOutput}
                                    endAdornment={'%'}
                                    disabled={inputDisabled}
                                    style={{width:'100px'}}
                                />
                                &nbsp;
                                <FormattedMessage 
                                    id='p1Co2Purge[13].door'
                                    defaultMessage='and a door output of'
                                />
                                &nbsp;
                                <Agristar2InputField 
                                    type={'number'}
                                    value={displayData.doorOutput}
                                    onChange={(e)=>handleDoorOutput(e.target.value)}
                                    error={errors.doorOutput}
                                    endAdornment={'%'}
                                    disabled={inputDisabled}
                                    style={{width:'100px'}}
                                />
                                <FormattedMessage 
                                    id='p1Co2Purge[17].chinese2'
                                    defaultMessage=' '
                                />
                            </Typography>
                        </div>
                    }
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'center'}}>
                    <ButtonSave 
                        onClick={()=>submit()}
                        disabled={!saveEnabled}
                        style={{margin:'5px auto'}}
                    />
                </div>
            </div>
        </SaveComponent>
    )
}
export default Agristar2FormP1Co2Purge