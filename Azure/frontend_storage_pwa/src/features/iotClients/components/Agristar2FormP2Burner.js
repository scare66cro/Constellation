import { TableContainer, Table, TableBody, TableRow, TableCell, Typography } from "@material-ui/core";
import { FormattedMessage } from "react-intl";
import ButtonSave from "../../../common/ButtonSave";
import useAS2Formp2Burner from "../hooks/AS2FormP2Burner";
import Agristar2InputField, { Agristar2InputFieldSelect } from "./Agristar2InputFields";
import Pidu from "./Pidu";
import SaveComponent from "./SaveComponent";


const Agristar2FormP2Burner = (props) => {
  const {
    saving,
    isAuthorized,
    handlePayloadToSend,
    submitPayloadToSend,
    errors,
    mapValueToLabel,
    payloadToSend,
  } = useAS2Formp2Burner();

  const saveEnabled = () => {
    if (isAuthorized === false){
        return true
    }
  }

  return (
    <SaveComponent saving={saving?.p2Burner?.status}>
      <div style={{width:'100%', display:'flex', flexDirection:'column', justifyContent:'middle'}}>
          <TableContainer>
            <Table>
              <TableBody>
                <TableRow>
                  <TableCell colSpan={2}>
                    <Typography align='justify' component='div' >
                      <FormattedMessage
                        id='p2Burner[1].control'
                        defaultMessage='Burner Control Mode:'
                      />
                      &nbsp;
                      <Agristar2InputFieldSelect
                        value={payloadToSend.selBurnerMode}
                        options={mapValueToLabel.BurnerMode}
                        error={errors.selBurnerMode}
                        onChange={(e)=>handlePayloadToSend('selBurnerMode', e.target.value)}
                        disabled={!isAuthorized}
                        style={{marginTop:'-.2rem', marginLeft:'10px', paddingLeft:'5px'}}
                      />
                    </Typography>
                  </TableCell>
                </TableRow>
                { payloadToSend.selBurnerMode === '1' &&
                  <TableRow>
                    <TableCell align='right'>
                      <Typography>
                        <FormattedMessage
                          id="p2BurnerDynTranslatedText[0].output"
                          defaultMessage="Burner Output"
                        />
                      </Typography>
                    </TableCell>
                    <TableCell align='left'>
                      <Agristar2InputField
                          type={'number'}
                          value={payloadToSend.burnerManual}
                          error={errors.burnerManual}
                          onChange={(e)=>handlePayloadToSend('burnerManual', e.target.value)}
                          disabled={!isAuthorized}
                      />
                    </TableCell>
                  </TableRow>
                }
                { payloadToSend.selBurnerMode !== '1' &&
                  <>
                    <TableRow>
                      <TableCell align='right'>
                        <Typography>
                          <FormattedMessage
                            id="p2Burner[6].ignite"
                            defaultMessage="Burner will ignite when output reaches"
                          />
                        </Typography>
                      </TableCell>
                      <TableCell align='left'>
                        <Agristar2InputField
                            type={'number'}
                            value={payloadToSend.burnerOn}
                            error={errors.burnerOn}
                            onChange={(e)=>handlePayloadToSend('burnerOn', e.target.value)}
                            disabled={!isAuthorized || payloadToSend.selBurnerMode === '0'}
                        />
                      </TableCell>
                    </TableRow>
                    <TableRow>
                      <TableCell align='right'>
                        <Typography>
                          <FormattedMessage
                            id="p2Burner[7].low"
                            defaultMessage="Low Burner Level"
                          />
                        </Typography>
                      </TableCell>
                      <TableCell align='left'>
                        <Agristar2InputField
                            type={'number'}
                            value={payloadToSend.burnerLow}
                            error={errors.burnerLow}
                            onChange={(e)=>handlePayloadToSend('burnerLow', e.target.value)}
                            disabled={!isAuthorized || payloadToSend.selBurnerMode === '0'}
                        />
                      </TableCell>
                    </TableRow>
                    <TableRow>
                      <TableCell colSpan={2}>
                        <Pidu
                          label={
                              <>
                                  <FormattedMessage
                                      id='p2Burner[8].PIDU'
                                      defaultMessage='PIDU Values'
                                  />
                              </>
                          }
                          P={payloadToSend.PBurnerValue}
                          I={payloadToSend.IBurnerValue}
                          D={payloadToSend.DBurnerValue}
                          U={payloadToSend.UBurnerValue}
                          error={{errors, p: 'PBurnerValue', i: 'IBurnerValue', d: 'DBurnerValue', u: 'UBurnerValue'}}
                          isAuthorized={isAuthorized && payloadToSend.selBurnerMode !== '0'}
                          handlePayloadToSend={handlePayloadToSend}
                        />
                      </TableCell>
                    </TableRow>
                  </>
                }
                <TableRow>
                  <TableCell align='right'>
                    <Typography>
                      <FormattedMessage
                        id="p2BurnerDynTranslatedText[7].altitude"
                        defaultMessage="Altitude"
                      />
                    </Typography>
                  </TableCell>
                  <TableCell align='left'>
                    <Agristar2InputField
                      type={'number'}
                      value={payloadToSend.Altitude}
                      error={errors.Altitude}
                      onChange={(e)=>handlePayloadToSend('Altitude', e.target.value)}
                      disabled={!isAuthorized}
                    />
                    <Agristar2InputFieldSelect
                      value={payloadToSend.AltType}
                      options={mapValueToLabel.AltType}
                      error={errors.AltType}
                      onChange={(e)=>handlePayloadToSend('AltType', e.target.value)}
                      disabled={!isAuthorized}
                      style={{marginTop:'-.2rem', marginLeft:'10px', paddingLeft:'5px'}}
                    />
                  </TableCell>
                </TableRow>
              </TableBody>
            </Table>
          </TableContainer>
          <div style={{margin:'8px auto', width:'100%', display:'flex', justifyContent:'center'}}>
              <ButtonSave
                  onClick={()=>submitPayloadToSend()}
                  disabled={saveEnabled()}
                  style={{margin:'5px auto'}}
              />
          </div>

      </div>

    </SaveComponent>
  );
}

export default Agristar2FormP2Burner;
