import { Typography } from "@material-ui/core"
import { FormattedMessage } from "react-intl"
import ButtonSave from "../../../common/ButtonSave"
import useAS2FormP1FanSpeed from "../hooks/AS2FormP1FanSpeed"
import Agristar2InputField, { Agristar2InputFieldSelect } from "./Agristar2InputFields"
import SaveComponent from "./SaveComponent"

const Row = (props) => {
    const style = props.style

    return(
        <div style={{margin:'8px auto', width:'90%', display:'flex', justifyContent:'space-between', ...style}}>
            {props.children}
        </div>
    )
}

const AgristarFormP1FanSpeed = (props) => {

    const {
        isAuthorized,

        payloadToSend1,
        payloadToSend2,

        mapValueToLabel,

        handlePayloadToSend1,
        handlePayloadToSend2,

        submitPayloadToSend1,
        submitPayloadToSend2,
        errors,
        saving,
        onionMode,
        fanData,
        selDiff2,
    } = useAS2FormP1FanSpeed()

    const saveEnabled = isAuthorized
    const inputDisabled = !isAuthorized

    return(
        <SaveComponent saving={saving?.p1FreqCtrl?.status || saving?.p1SetFanSpeed?.status}>
            <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle', padding:'0px'}}>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between'}}>
                    <Typography align='justify' component='div'>
                        <FormattedMessage 
                            id='p1FreqCtrl[1].currentSpeed'
                            defaultMessage='Current Cooling Fan Speed'
                        />
                        <Agristar2InputField
                            type={'number'}
                            value={payloadToSend1.setFanSpeed}
                            onChange={(e)=>handlePayloadToSend1(e.target.value)}
                            disabled={inputDisabled}
                            error={errors.setFanSpeed}
                            endAdornment={'%'}
                            style={{maxWidth:'100px', marginLeft:'10px'}}
                        />
                    </Typography>
                    <ButtonSave 
                        label={
                            <FormattedMessage 
                                id='buttonsTranslatedText[22].setNewCooling'
                                defaultMessage='Set'
                                description='Set New Cooling Speed'
                            />
                        }
                        onClick={()=>submitPayloadToSend1()}
                        disabled={inputDisabled}
                    />
                </div>
                <Row>
                    <Typography component='div'>
                        <FormattedMessage 
                            id='p1FreqCtrl[2].coolingMaximum'
                            defaultMessage='Cooling Mode Maximum'
                        />
                    </Typography>
                    <Agristar2InputField
                        type={'number'}
                        value={payloadToSend2.maxFanSpeed}
                        onChange={(e)=>handlePayloadToSend2('maxFanSpeed', e.target.value)}
                        disabled={inputDisabled}
                        error={errors.maxFanSpeed}
                        endAdornment={'%'}
                        style={{maxWidth:'100px', marginLeft:'10px'}}
                    />
                </Row>
                <Row>
                    <Typography component='div'>
                        <FormattedMessage 
                            id='p1FreqCtrl[3].coolingMinimum'
                            defaultMessage='Cooling Mode Minimum'
                        />
                    </Typography>
                    <Agristar2InputField
                        type={'number'}
                        value={payloadToSend2.minFanSpeed}
                        onChange={(e)=>handlePayloadToSend2('minFanSpeed', e.target.value)}
                        disabled={inputDisabled}
                        error={errors.minFanSpeed}
                        endAdornment={'%'}
                        style={{maxWidth:'100px', marginLeft:'10px'}}
                    />
                </Row>
                <Row>
                    <Typography component='div'>
                        <FormattedMessage 
                            id='p1FreqCtrl[4].refrigMode'
                            defaultMessage='Refrigeration Mode'
                        />
                    </Typography>
                    <Agristar2InputField
                        type={'number'}
                        value={payloadToSend2.refrFanSpeed}
                        onChange={(e)=>handlePayloadToSend2('refrFanSpeed', e.target.value)}
                        disabled={inputDisabled}
                        error={errors.refrFanSpeed}
                        endAdornment={'%'}
                        style={{maxWidth:'100px', marginLeft:'10px'}}
                    />
                </Row>
                <Row>
                    <Typography component='div'>
                        <FormattedMessage 
                            id='p1FreqCtrl[5].recircMode'
                            defaultMessage='Recirculation Mode'
                        />
                    </Typography>
                    <Agristar2InputField
                        type={'number'}
                        value={payloadToSend2.recircFanSpeed}
                        onChange={(e)=>handlePayloadToSend2('recircFanSpeed', e.target.value)}
                        disabled={inputDisabled}
                        error={errors.recircFanSpeed}
                        endAdornment={'%'}
                        style={{maxWidth:'100px', marginLeft:'10px'}}
                    />
                </Row>
                <Row style={{width:'100%'}}>
                    <Typography align='center' component='div'>
                        <FormattedMessage 
                            id='p1FreqCtrl[6].msg1'
                            defaultMessage='In cooling mode, fan speed will update every'
                        />
                        &nbsp;
                        <Agristar2InputField
                            type={'number'}
                            value={payloadToSend2.updFanSpeed}
                            onChange={(e)=>handlePayloadToSend2('updFanSpeed', e.target.value)}
                            disabled={inputDisabled}
                            error={errors.updFanSpeed}
                            // endAdornment={'%'}
                            style={{maxWidth:'100px', marginLeft:'10px'}}
                        />
                        &nbsp;
                        <FormattedMessage 
                            id='p1FreqCtrl[7].msg2'
                            defaultMessage='hours'
                        />
                        &nbsp;
                        <FormattedMessage 
                            id='p1FreqCtrl[8].msg3'
                            defaultMessage='to maintain'
                        />
                        &nbsp;
                        {
                        ((onionMode && fanData && fanData[9] === '0') || !onionMode)
                        ?
                            <FormattedMessage 
                                id='p1FreqCtrl[9].msg4'
                                defaultMessage='a temperature differential of'
                            />
                        :
                            <>
                                { payloadToSend2.selCoolingType === '0'
                                ? 
                                    <FormattedMessage 
                                        id='p1FreqCtrlModeChangeTranslatedText[0].a'
                                        defaultMessage='a'
                                    />
                                :
                                    <FormattedMessage 
                                        id='p1FreqCtrlModeChangeTranslatedText[1].the'
                                        defaultMessage='the'
                                    />
                                }
                                &nbsp;
                                <Agristar2InputFieldSelect 
                                    value={payloadToSend2.selCoolingType}
                                    // options={mapValueToLabel.selTempRef}
                                    options={mapValueToLabel.selCoolingType}
                                    onChange={(e)=>handlePayloadToSend2('selCoolingType',e.target.value)}
                                    disabled={inputDisabled}
                                    error={errors.selDiff1}
                                    style={{marginTop:'-.2rem', marginLeft:'10px', paddingLeft:'5px'}}
                                />
                                &nbsp;
                                { payloadToSend2.selCoolingType === '0'
                                    ?
                                    <FormattedMessage 
                                        id='p1FreqCtrlModeChangeTranslatedText[5].of'
                                        defaultMessage='of'
                                    />
                                    :
                                    <FormattedMessage
                                        id='p1FreqCtrlModeChangeTranslatedText[11].plenumHumidity'
                                        defaultMessage='in the plenum below the plenum humidity setpoint'
                                    />
                                }
                            </>
                        }
                        {
                            (!onionMode || payloadToSend2.selCoolingType === '0') &&
                            <>
                                &nbsp;
                                <Agristar2InputField
                                    type={'number'}
                                    value={payloadToSend2.tempDiff}
                                    onChange={(e)=>handlePayloadToSend2('tempDiff', e.target.value)}
                                    disabled={inputDisabled}
                                    error={errors.tempDiff}
                                    endAdornment={'\u00B0'}
                                    style={{maxWidth:'100px', marginLeft:'10px'}}
                                />
                                <FormattedMessage
                                    id='p1FreqCtrl[14].chinese1'
                                    defaultMessage=' '
                                />
                                &nbsp;
                                <FormattedMessage 
                                    id='p1FreqCtrl[10].msg5'
                                    defaultMessage='between'
                                />
                                &nbsp;
                                <Agristar2InputFieldSelect 
                                    value={payloadToSend2.selDiff1}
                                    // options={mapValueToLabel.selTempRef}
                                    options={mapValueToLabel.selDiff1}
                                    onChange={(e)=>handlePayloadToSend2('selDiff1',e.target.value)}
                                    disabled={inputDisabled}
                                    error={errors.selDiff1}
                                    style={{marginTop:'-.2rem', marginLeft:'10px', paddingLeft:'5px'}}
                                />
                                &nbsp;
                                <FormattedMessage 
                                    id='p1FreqCtrl[11].msg6'
                                    defaultMessage='and'
                                />
                                &nbsp;
                                <Agristar2InputFieldSelect 
                                    value={payloadToSend2.selDiff2}
                                    // options={mapValueToLabel.selTempRef}
                                    options={selDiff2}
                                    onChange={(e)=>handlePayloadToSend2('selDiff2',e.target.value)}
                                    disabled={inputDisabled}
                                    error={errors.selDiff2}
                                    style={{marginLeft:'10px', paddingLeft:'5px'}}
                                />
                                <FormattedMessage
                                    id='p1FreqCtrl[15].chinese2'
                                    defaultMessage=' '
                                />
                            </>
                        }
                    </Typography>
                </Row>
                <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'center'}}>
                    <ButtonSave 
                        onClick={()=>submitPayloadToSend2()}
                        disabled={!saveEnabled}
                        style={{margin:'5px auto'}}
                    />
                </div>
            </div>
        </SaveComponent>
    )
}
export default AgristarFormP1FanSpeed;