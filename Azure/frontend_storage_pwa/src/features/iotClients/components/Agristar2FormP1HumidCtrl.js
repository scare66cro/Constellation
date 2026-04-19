import {
  Typography, Tabs, Tab, Box, Paper,
  TableContainer, Table, TableBody, TableCell, TableHead, TableRow,
} from "@material-ui/core"
import { FormattedMessage, useIntl } from "react-intl"
import ButtonSave from "../../../common/ButtonSave"
import useASFormP1HumidCtrl from "../hooks/AS2FormP1HumidCtrl"
import Agristar2InputField, { Agristar2InputFieldSelect } from "./Agristar2InputFields"
import { useState } from 'react';
import SaveComponent from "./SaveComponent"
import { agristar2Refrigeration } from "../../../utilities/translationObjects"

const Row = (props) => {
    const style = props.style

    return(
        <div style={{margin:'8px auto', width:'90%', display:'flex', justifyContent:'space-between', ...style}}>
            {props.children}
        </div>
    )
}

function TabPanel(props) {
  const { children, value, index, ...other } = props;

  return (
    <div
      hidden={value !== index}
      id={`tabpanel-${index}`}
      aria-labelledby={`tab-${index}`}
      {...other}
    >
      {
        value === index && (
          <Box p={3}>
            {children}
          </Box>
        )
      }
    </div>
  )
}

const AgristarFormHumidCtrl = (props) => {
  const intl = useIntl();
    const {
        isAuthorized,
        payloadToSend,
        mapValueToLabel,
        saving,
        errors,
        humidSetpoint,
        humidCtrlsAvailable,
        handlePayloadToSend,
        updateDuration,
        submitPayloadToSend,
    } = useASFormP1HumidCtrl()

    const saveEnabled = isAuthorized
    const inputDisabled = !isAuthorized

    const [active, setActive] = useState(0)

    function handleTabChange(e, newValue) {
      setActive(newValue);
    }

    function updateMode(index, mode) {
      const humid = {
        ...payloadToSend.humid[index],
        mode
      };

      handlePayloadToSend(index, humid);
    }

    return(
      <SaveComponent saving={saving?.p1HumidCtrl?.status}>
        <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle', padding:'0px'}}>
          {
            humidCtrlsAvailable ? (
              <>
              <Tabs value={active} onChange={handleTabChange} indicatorColor="primary">
              {
                payloadToSend.humid.map((h, i) => {
                  if (h.humidHead?.exists) {
                    return (
                      <Tab key={i} label={`Humidifier ${i + 1}`}/>
                    )
                  } else {
                    return null;
                  }
                })
              }
              </Tabs>
              {
                payloadToSend.humid.map((h, i) => {
                  if (h.humidHead?.exists && payloadToSend.humid) {
                    return (
                      <TabPanel key={i} value={active} index={i}>
                        <Row>
                            <Typography component='div'>
                                <FormattedMessage 
                                    id='p1HumidCtrlModeDynTranslatedText[4].autoControlMode'
                                    defaultMessage='Auto Control Mode:'
                                />
                            </Typography>
                            <Agristar2InputFieldSelect 
                                value={payloadToSend.humid[i].mode}
                                // options={mapValueToLabel.selTempRef}
                                options={mapValueToLabel.mode}
                                error={errors.selHumidMode}
                                onChange={(e)=>updateMode(i, e.target.value)}
                                disabled={inputDisabled}
                                style={{marginTop:'-.2rem', marginLeft:'10px', paddingLeft:'5px'}}
                            />
                        </Row>
                        {
                          h.mode === '2' &&
                            <Typography>
                              <FormattedMessage 
                                id='p1HumidCtrlModeDynTranslatedText[0].maintain'
                                defaultMessage='The system will automatically maintain the'
                              />
                              &nbsp;
                              <FormattedMessage 
                                id='p1HumidCtrlModeDynTranslatedText[1].humidity'
                                defaultMessage='plenum humidity setpoint of {value}%.'
                                values={
                                  {
                                    value: humidSetpoint,
                                  }
                                }
                              />
                            </Typography>
                        }
                        {
                          (h.mode === '0' || h.mode === '1') &&
                            <TableContainer component={Paper}>
                              <Table width="100%">
                                <TableHead>
                                  <TableRow>
                                    <TableCell rowSpan="2">
                                      <FormattedMessage 
                                        id='p1HumidCtrlModeDynTranslatedText[2].systemmode'
                                        defaultMessage='System Mode'
                                      />
                                    </TableCell>
                                    <TableCell colSpan="2">
                                      <FormattedMessage 
                                        id='p1HumidCtrlModeDynTranslatedText[3].cycleduration'
                                        defaultMessage='Cycle Duration'
                                      />
                                    </TableCell>
                                  </TableRow>
                                  <TableRow>
                                    <TableCell>
                                      <FormattedMessage 
                                        id='p1HumidCtrlUnitsDynTranslatedText[0].seconds.on'
                                        defaultMessage='Seconds On'
                                      />
                                    </TableCell>
                                    <TableCell>
                                      <FormattedMessage 
                                        id='p1HumidCtrlUnitsDynTranslatedText[1].seconds.off'
                                        defaultMessage='Seconds Off'
                                      />
                                    </TableCell>
                                  </TableRow>
                                </TableHead>
                                <TableBody>
                                  <TableRow>
                                    <TableCell>
                                      <FormattedMessage 
                                        id='humidLabelsTranslatedText[0].cooling'
                                        defaultMessage='Cooling'
                                      />
                                    </TableCell>
                                    <TableCell>
                                      <Agristar2InputField
                                          type={'number'}
                                          value={h.coolOn}
                                          error={errors.coolOn}
                                          onChange={(e)=>updateDuration(i, 'coolOn', e.target.value)}
                                          disabled={inputDisabled || h.mode === '0'}
                                      />
                                    </TableCell>
                                    <TableCell>
                                      <Agristar2InputField
                                          type={'number'}
                                          value={h.coolOff}
                                          error={errors.coolOff}
                                          onChange={(e)=>updateDuration(i, 'coolOff', e.target.value)}
                                          disabled={inputDisabled || h.mode === '0'}
                                      />
                                    </TableCell>
                                  </TableRow>
                                  <TableRow>
                                    <TableCell>
                                      <FormattedMessage 
                                        id='humidLabelsTranslatedText[1].recirculation'
                                        defaultMessage='Recirculation'
                                      />
                                    </TableCell>
                                    <TableCell>
                                      <Agristar2InputField
                                          type={'number'}
                                          value={h.recircOn}
                                          error={errors.recircOn}
                                          onChange={(e)=>updateDuration(i, 'recircOn', e.target.value)}
                                          disabled={inputDisabled || h.mode === '0'}
                                      />
                                    </TableCell>
                                    <TableCell>
                                      <Agristar2InputField
                                          type={'number'}
                                          value={h.recircOff}
                                          error={errors.recircOff}
                                          onChange={(e)=>updateDuration(i, 'recircOff', e.target.value)}
                                          disabled={inputDisabled || h.mode === '0'}
                                      />
                                    </TableCell>
                                  </TableRow>
                                  <TableRow>
                                    <TableCell>
                                      {intl.formatMessage(agristar2Refrigeration)}
                                    </TableCell>
                                    <TableCell>
                                      <Agristar2InputField
                                          type={'number'}
                                          value={h.refrigOn}
                                          error={errors.refrigOn}
                                          onChange={(e)=>updateDuration(i, 'refrigOn', e.target.value)}
                                          disabled={inputDisabled || h.mode === '0'}
                                      />
                                    </TableCell>
                                    <TableCell>
                                      <Agristar2InputField
                                          type={'number'}
                                          value={h.refrigOff}
                                          error={errors.refrigOff}
                                          onChange={(e)=>updateDuration(i, 'refrigOff', e.target.value)}
                                          disabled={inputDisabled || h.mode === '0'}
                                      />
                                    </TableCell>
                                  </TableRow>
                                </TableBody>
                              </Table>
                            </TableContainer>
                        }
                      </TabPanel>
                    )
                  } else {
                    return null;
                  }
                })
              }
              </>
            ) : (
              <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'center'}}>
                <Typography>
                  <FormattedMessage 
                    id='p1HumidCtrlModeDynTranslatedText[5].output.not.defined'
                    defaultMessage='Humidifier output not defined.'
                  />
                </Typography>
              </div>
            )
          }
          <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'center'}}>
            <Typography variant='caption'>
              <FormattedMessage 
                id='p1HumidCtrl[1].note'
                defaultMessage='NOTE: Settings for each humidifier must be saved individually.'
              />
            </Typography>
          </div>
          <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'center'}}>
              <ButtonSave 
                  onClick={()=>submitPayloadToSend(active)}
                  disabled={!saveEnabled}
                  style={{margin:'5px auto'}}
              />
          </div>
        </div>
      </SaveComponent>
    )
}
export default AgristarFormHumidCtrl;