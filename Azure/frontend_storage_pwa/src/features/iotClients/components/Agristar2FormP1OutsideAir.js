import { Typography } from "@material-ui/core"
import { FormattedMessage } from "react-intl"
import ButtonSave from "../../../common/ButtonSave"
import useAS2FormP1OutsideAir from "../hooks/AS2FormP1OutsideAir"
import Agristar2InputField,{ Agristar2InputFieldSelect } from "./Agristar2InputFields"
import SaveComponent from "./SaveComponent"

const Agristar2FormP1OutsideAir = (props) => {
    // -------------------HOOKS--------------------
    const {
        isAuthorized,

        mapValueToLabel,
        optionsSelTempRef,

        displayData,

        handlePayload,
        errors,
        saving,
        submit,
        cureMode,
    } = useAS2FormP1OutsideAir()

    const saveEnabled = isAuthorized
    const inputDisabled = !isAuthorized

    return(
        <SaveComponent saving={saving?.p1OutsideAir?.status}>
            <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle', padding:'0px 10px'}}>
                <div style={{margin:'8px auto', width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle'}}>
                    { !cureMode
                        ?
                        <Typography component='div'>
                            <FormattedMessage 
                                id='p1OutsideAir[1].msg1'
                                defaultMessage='System can run on outside air cooling when'
                            />
                            &nbsp;
                            <Agristar2InputFieldSelect 
                                value={displayData.ctrlMode}
                                options={mapValueToLabel.ctrlMode}
                                onChange={(e)=>handlePayload('ctrlMode', e.target.value)}
                                error={errors.ctrlMode}
                                disabled={inputDisabled}
                                style={{marginTop:'-.2rem', paddingLeft:'5px'}}
                            />
                            &nbsp;
                            <FormattedMessage 
                                id='p1OutsideAir[2].msg2'
                                defaultMessage='temperature is'
                            />
                            &nbsp;
                            <Agristar2InputField 
                                type={'number'}
                                value={displayData.OutsideAirSet}
                                error={errors.OutsideAirSet}
                                onChange={(e)=>handlePayload('OutsideAirSet', e.target.value)}
                                disabled={inputDisabled}
                                endAdornment={'\u00B0'}
                                style={{width:'55px', marginLeft:'5px'}}
                            />
                            <Agristar2InputFieldSelect 
                                value={displayData.selAboveBelow}
                                options={displayData.ctrlMode === '0' ? mapValueToLabel.selAboveBelow : {"0": mapValueToLabel.selAboveBelow["0"]}}
                                onChange={(e)=>handlePayload('selAboveBelow', e.target.value)}
                                disabled={inputDisabled}
                                error={errors.selAboveBelow}
                                style={{marginTop:'-.2rem', marginLeft:'5px', paddingLeft:'5px'}}
                            />
                            <Agristar2InputFieldSelect 
                                value={displayData.selTempRef}
                                // options={mapValueToLabel.selTempRef}
                                options={optionsSelTempRef}
                                onChange={(e)=>handlePayload('selTempRef', e.target.value)}
                                disabled={inputDisabled}
                                error={errors.selTempRef}
                                style={{marginTop:'-.2rem', marginLeft:'5px', paddingLeft:'5px'}}
                            />
                        </Typography>
                        :
                        <>
                        <Typography component='div'>
                            <FormattedMessage 
                                id='p1OutsideAirDynTranslatedText[0].curemode'
                                defaultMessage='System will enter outside air cure mode when'
                            />
                            &nbsp;
                            <FormattedMessage 
                                id='p1OutsideAirDynTranslatedText[1].outsideabove'
                                defaultMessage='outside air temperature is above'
                            />
                            &nbsp;
                            <Agristar2InputField 
                                type={'number'}
                                value={displayData.CureStartTemp}
                                error={errors.CureStartTemp}
                                onChange={(e)=>handlePayload('CureStartTemp', e.target.value)}
                                endAdornment={'\u00B0'}
                                style={{width:'55px', marginLeft:'5px'}}
                                disabled={inputDisabled}
                            />
                            &nbsp;
                            <FormattedMessage 
                                id='p1OutsideAirDynTranslatedText[2].and'
                                defaultMessage='and'
                            />
                            &nbsp;
                            <Agristar2InputFieldSelect 
                                value={displayData.selHumidRef}
                                options={mapValueToLabel.selHumidRef}
                                onChange={(e)=>handlePayload('selHumidRef', e.target.value)}
                                disabled={inputDisabled}
                                error={errors.selHumidRef}
                                style={{marginTop:'-.2rem', marginLeft:'5px', paddingLeft:'5px'}}
                            />
                            &nbsp;
                            <FormattedMessage 
                                id='p1OutsideAirDynTranslatedText[5].isbelow'
                                defaultMessage='is below'
                            />
                            &nbsp;
                            <Agristar2InputField 
                                type={'number'}
                                value={displayData.CureStartHumid}
                                error={errors.CureStartHumid}
                                onChange={(e)=>handlePayload('CureStartHumid', e.target.value)}
                                endAdornment={'%'}
                                style={{width:'55px', marginLeft:'5px'}}
                                disabled={inputDisabled}
                            />
                        </Typography>
                        <Typography component='div'>
                            <FormattedMessage 
                                id='p1OutsideAirDynTranslatedText[6].blend'
                                defaultMessage='In outside air and burner cure modes system will blend outside air while'
                            />
                            &nbsp;
                            <FormattedMessage 
                                id='p1OutsideAirDynTranslatedText[7].deviationalarm'
                                defaultMessage='plenum temperature remains above the low deviation alarm value of'
                            />
                            &nbsp;
                            <b>{displayData.CureTempLowLimit}&#x00B0;</b>
                            &nbsp;
                            <FormattedMessage 
                                id='p1OutsideAirDynTranslatedText[8].and'
                                defaultMessage='and'
                            />
                            &nbsp;
                            <b>
                            <FormattedMessage
                                id='p1OutsideAirDynTranslatedText[22].plenum'
                                defaultMessage='plenum'
                            />
                            </b>
                            &nbsp;
                            <FormattedMessage 
                                id='p1OutsideAirDynTranslatedText[9].humidity-below'
                                defaultMessage='humidity remains below'
                            />
                            &nbsp;
                            <Agristar2InputField 
                                type={'number'}
                                value={displayData.CureHumidHighLimit}
                                error={errors.CureHumidHighLimit}
                                onChange={(e)=>handlePayload('CureHumidHighLimit', e.target.value)}
                                endAdornment={'%'}
                                style={{width:'55px', marginLeft:'5px'}}
                                disabled={inputDisabled}
                            />
                        </Typography>
                        </>
                    }
                </div>
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
export default Agristar2FormP1OutsideAir