import { UnitsComponent } from "../../../utilities/appUnitsOfMeasurements"
import { FormattedMessage } from 'react-intl';
import Agristar2InputField, { Agristar2InputFieldSelect } from './Agristar2InputFields'
import ButtonSave from '../../../common/ButtonSave'
import { useAS2FormP1RampRate } from '../hooks/AS2FormP1RampRate'
import { Checkbox, FormControlLabel, Typography } from "@material-ui/core"
import SaveComponent from "./SaveComponent"

const Agristar2FormRampRate = (props) => {

  const {
    payloadToSend, isAuthorized, errors, saving, submit,
    handleTargetTempSet, handleTempRefSet, handleTempDiffSet, handleUpdatePeriodSet,
    handleUpdateTempSet, handleUpdateAutomaticSet, options, cureMode,
  } = useAS2FormP1RampRate()

  return(
    <SaveComponent saving={saving?.p1RampRate?.status}>
      <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle', padding:'0px 5%'}}>
        <div style={{margin:'8px auto', width:'100%', display:'flex'}}>
          <FormControlLabel
            control={
              <Checkbox
                checked={payloadToSend.rampAutomatic ? true : false}
                inputProps={{ 'aria-label': 'Automatic Ramp'}}
                onChange={(e) => handleUpdateAutomaticSet(e.target.checked)}
                color='primary'
                disabled={!isAuthorized || cureMode}
              />
            }
            label="Automatic Ramp"
          />
        </div>
        <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'space-between'}}>
          <Typography component='div' >
            <FormattedMessage 
              id='p1RampRate[2].msg1'
              defaultMessage='Plenum setpoint will change '
            />
            <Agristar2InputField 
                type={'number'}
                value={payloadToSend.updTemp || ''}
                onChange={(e)=>handleUpdateTempSet(e.target.value)}
                error={errors.updTemp}
                endAdornment={'\u00B0'}
                disabled={!isAuthorized || cureMode}
                style={{marginRight: '5px'}}
            />
            {
              payloadToSend.rampAutomatic 
              ? (
              <>
                <FormattedMessage
                  id='btnEnter_onclickTranslatedText[2].automatically'
                  defaultMessage='Automatically'
                />
                &nbsp;
                <FormattedMessage 
                  id='rampModeDynTranslatedText[0].temp-differential'
                  defaultMessage=' as a temperature differential of '
                />
                <Agristar2InputField 
                  type={'number'}
                  value={payloadToSend.rampTempDiff || ''}
                  onChange={(e)=>handleTempDiffSet(e.target.value)}
                  error={errors.rampTempDiff}
                  endAdornment={'\u00B0'}
                  disabled={!isAuthorized || cureMode}
                  style={{marginRight: '5px'}}
                />
                <FormattedMessage 
                  id='rampModeDynTranslatedText[1].reached'
                  defaultMessage=' is reached'
                />
                &nbsp;
                <FormattedMessage 
                  id='rampModeDynTranslatedText[2].between'
                  defaultMessage='between plenum setpoint and '
                />
                <Agristar2InputFieldSelect
                  value={payloadToSend.selTemp || ''}
                  options={options}
                  onChange={(e)=>handleTempRefSet(e.target.value)}
                  error={errors.selTemp}
                  disabled={!isAuthorized || cureMode}
                  style={{marginLeft: '5px'}}
                />
              </>
              )
              : (
              <>
                <FormattedMessage 
                  id='rampModeDynTranslatedText[3].every'
                  defaultMessage=' every '
                />
                <Agristar2InputField 
                  type={'number'}
                  value={payloadToSend.rampUpdateHours || ''}
                  onChange={(e)=>handleUpdatePeriodSet(e.target.value)}
                  error={errors.rampUpdateHours}
                  disabled={!isAuthorized || cureMode}
                />
                <FormattedMessage 
                  id='rampModeDynTranslatedText[4].hours.cooling'
                  defaultMessage=' hours of cooling or refrigeration runtime'
                />
              </>
              )
            }
            &nbsp;
            <FormattedMessage 
              id='p1RampRate[3].msg2'
              defaultMessage='until plenum setpoint equals '
            />
            <Agristar2InputField 
              type={'number'}
              value={payloadToSend.targetTemp || ''}
              onChange={(e)=>handleTargetTempSet(e.target.value)}
              error={errors.targetTemp}
              endAdornment={<UnitsComponent typeOfData={'temperature'} />}
              disabled={!isAuthorized || cureMode}
            />
          </Typography>
        </div>
        <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'center'}}>
          <ButtonSave 
              onClick={()=>submit()}
              disabled={!isAuthorized || cureMode}
              style={{margin:'5px auto'}}
          />
        </div>
      </div>
    </SaveComponent>
  )
}
export default Agristar2FormRampRate;