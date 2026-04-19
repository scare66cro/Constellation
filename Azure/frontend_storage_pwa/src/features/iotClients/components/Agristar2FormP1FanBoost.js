import { Typography } from "@material-ui/core"
import { FormattedMessage } from "react-intl"
import ButtonSave from "../../../common/ButtonSave"
import useAS2FormP1FanBoost from "../hooks/AS2FormP1FanBoost"
import Agristar2InputField, { Agristar2InputFieldSelect } from "./Agristar2InputFields"
import SaveComponent from "./SaveComponent"

const Agristar2FormP1FanBoost = (props) => {

    const {
        isAuthorized,
        payloadToSend,
        mapValueToLabel,
        handlePayloadToSend,
        submitPayloadToSend,
        errors,
        saving,
    } = useAS2FormP1FanBoost()

    const saveEnabled = isAuthorized
    const inputDisabled = !isAuthorized

    return(
        <SaveComponent saving={saving?.p1FanBoost?.status}>
          <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle', padding:'0px 5%'}}>
            <div style={{margin:'0px auto', width:'100%', display:'flex', justifyContent:'space-between', maxWidth:'350px'}}>
              <Typography component='div'>
                  <FormattedMessage 
                      id='level1.fanboost.fan-boost-control-mode'
                      defaultMessage='Fan Boost Control Mode'
                  />
              </Typography>
              <Agristar2InputFieldSelect
                value={payloadToSend.selBoostMode}
                options={mapValueToLabel.selBoostMode}
                onChange={(e)=>handlePayloadToSend('selBoostMode', e.target.value)}
                disabled={!isAuthorized}
                style={{marginTop:'-.2rem'}}
              />
            </div>
            {
              payloadToSend.selBoostMode !== '0' &&
              <>
                <div style={{margin:'8px auto', width:'100%', display:'flex'}}>
                  <Typography style={{maxWidth: '100%'}} component={'div'}>
                  <FormattedMessage
                    id='level1.fanboost.the-fan-speed-will-be-increased-to'
                    defaultMessage='The fan speed will be increased to'
                  />
                  <Agristar2InputField
                    type={'number'}
                    value={payloadToSend.speed}
                    onChange={(e)=>handlePayloadToSend(e.target.value)}
                    disabled={inputDisabled}
                    error={errors.speed}
                    endAdornment={'%'}
                    style={{maxWidth:'100px', marginLeft:'10px'}}
                  />
                  &nbsp;
                  <FormattedMessage
                    id='level1.fanboost.if'
                    defaultMessage='if'
                  />
                  &nbsp;
                  {
                    payloadToSend.selBoostMode === '1' &&
                    <>
                      <FormattedMessage
                        id='level1.fanboost.the-outside-temperature-is-below'
                        defaultMessage='the outside temperature is below'
                      />
                      <Agristar2InputField
                        type={'number'}
                        value={payloadToSend.temp}
                        onChange={(e)=>handlePayloadToSend(e.target.value)}
                        disabled={inputDisabled}
                        error={errors.temp}
                        endAdornment={'\u00B0'}
                        style={{maxWidth:'100px', marginLeft:'10px'}}
                      />
                      &nbsp;
                      <FormattedMessage
                        id='level1.fanboost.and-it'
                        defaultMessage='and it'
                      />
                      &nbsp;
                      <FormattedMessage
                        id='level1.fanboost.has-been'
                        defaultMessage='has been'
                      />
                      <Agristar2InputField
                        type={'number'}
                        value={payloadToSend.hours}
                        onChange={(e)=>handlePayloadToSend(e.target.value)}
                        disabled={inputDisabled}
                        error={errors.hours}
                        style={{maxWidth:'100px', marginLeft:'10px'}}
                      />
                      <FormattedMessage
                        id='level1.fanboost.hours-since-the-last-fan-boost-period'
                        defaultMessage='hours since the last fan boost period'
                      />
                    </>
                  }
                  {
                    payloadToSend.selBoostMode === '2' &&
                    <>
                      <FormattedMessage
                        id='level1.fanboost.the-continuous-Fan-Runtime-exceeds'
                        defaultMessage='the continuous Fan Runtime exceeds'
                      />
                      <Agristar2InputField
                        type={'number'}
                        value={payloadToSend.hours}
                        onChange={(e)=>handlePayloadToSend(e.target.value)}
                        disabled={inputDisabled}
                        error={errors.hours}
                        style={{maxWidth:'100px', marginLeft:'10px'}}
                      />
                      <FormattedMessage id='p1FreqCtrl[7].msg2' defaultMessage='hours'/>.
                    </>
                  }
                  </Typography>
                </div>
                <div style={{margin:'8px auto', width:'100%', display:'flex' }}>
                  <Typography style={{maxWidth: '100%'}} component={'div'}>
                  <FormattedMessage
                    id='level1.fanboost.the-fan-boost-period-will-last-for'
                    defaultMessage='The fan boost period will last for'
                  />
                  <Agristar2InputField
                    type={'number'}
                    value={payloadToSend.time}
                    onChange={(e)=>handlePayloadToSend(e.target.value)}
                    disabled={inputDisabled}
                    error={errors.time}
                    style={{maxWidth:'100px', marginLeft:'10px'}}
                  />
                  <FormattedMessage
                    id='p1Misc[4].minutes'
                    defaultMessage='minutes.'
                  />
                  </Typography>
                </div>
              </>
            }
            <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'center'}}>
                <ButtonSave 
                    onClick={()=>submitPayloadToSend()}
                    disabled={!saveEnabled}
                    style={{margin:'5px auto'}}
                />
            </div>
          </div>
        </SaveComponent>
    )
}
export default Agristar2FormP1FanBoost;