import { useEffect, useState } from "react";
import { useDispatch, useSelector } from "react-redux";
import { defineMessage, useIntl } from "react-intl";
import { postAgristar2Action } from "../actions";
import { extractAgristar2BurnerFromIoTClient, extractPermissionsFromIoTClient, selectSaving, selectSelectedIoTClient } from "../selectors";

const useAS2Formp2Burner = () => {
  const dispatch = useDispatch();
  const iotClient = useSelector((state)=>selectSelectedIoTClient(state));
  const saving = useSelector((state) => selectSaving(state));
  const obj_permissions = extractPermissionsFromIoTClient(iotClient)
  const burner = extractAgristar2BurnerFromIoTClient(iotClient);
  const isAuthorized = obj_permissions?.['agristar2_action_level2'];
  const intl = useIntl();

  const [payloadToSend, setPayloadToSend] = useState({
    tag: "p2Burner",
    selBurnerMode: burner.selBurnerMode,
    Altitude: burner.Altitude,
    AltType: burner.AltType,
    PBurnerValue: burner.PBurnerValue,
    IBurnerValue: burner.IBurnerValue,
    DBurnerValue: burner.DBurnerValue,
    UBurnerValue: burner.UBurnerValue,
    burnerOn: burner.burnerOn,
    burnerLow: burner.burnerLow,
    burnerManual: burner.burnerManual,
  });

  const mapValueToLabel = {
    BurnerMode: {
      '0': intl.formatMessage(defineMessage({
        id:'p2Burner[2].none',
        defaultMessage:'None',
      })),
      '1': intl.formatMessage(defineMessage({
        id:'p2Burner[3].manual',
        defaultMessage:'Manual',
      })),
      '2': intl.formatMessage(defineMessage({
        id:'p2Burner[4].economy',
        defaultMessage:'Economy Cure',
      })),
      '3': intl.formatMessage(defineMessage({
        id:'p2Burner[5].maximum',
        defaultMessage:'Maximum Cure',
      })),
    },
    AltType: {
      '0': intl.formatMessage(defineMessage({
        id: 'p2BurnerDynTranslatedText[9].feet',
        defaultMessage: 'Feet',
      })),
      '1': intl.formatMessage(defineMessage({
        id: 'p2BurnerDynTranslatedText[10].meters',
        defaultMessage: 'Meters',
      })),
    },
  };

  useEffect(() => {
    setPayloadToSend({
      ...payloadToSend,
      ...burner,
    })
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [JSON.stringify(burner)]);

  const handlePayloadToSend = (key, val) => {
    let newKey;
    switch (key) {
      case 'P': newKey = 'PBurnerValue'; break;
      case 'I': newKey = 'IBurnerValue'; break;
      case 'D': newKey = 'DBurnerValue'; break;
      case 'U': newKey = 'UBurnerValue'; break;
      default: newKey = key; break;
    }
    setPayloadToSend({
      ...payloadToSend,
      [newKey]: val,
    });
  }

  const submitPayloadToSend = () => {
    dispatch(postAgristar2Action(iotClient, payloadToSend));
  };

  const [errors, setErrors] = useState({});
  useEffect(() => {
    setErrors({
      selBurnerMode: saving.p2Burner?.errors?.selBurnerMode,
      Altitude: saving.p2Burner?.errors?.Altitude,
      AltType: saving.p2Burner?.errors?.AltType,
      PBurnerValue: saving.p2Burner?.errors?.PBurnerValue,
      IBurnerValue: saving.p2Burner?.errors?.IBurnerValue,
      DBurnerValue: saving.p2Burner?.errors?.DBurnerValue,
      UBurnerValue: saving.p2Burner?.errors?.UBurnerValue,
      burnerOn: saving.p2Burner?.errors?.burnerOn,
      burnerLow: saving.p2Burner?.errors?.burnerLow,
      burnerManual: saving.p2Burner?.errors?.burnerManual,
    });
  }, [saving]);

  return {
    saving,
    isAuthorized,
    handlePayloadToSend,
    submitPayloadToSend,
    errors,
    mapValueToLabel,
    payloadToSend,
  };
}

export default useAS2Formp2Burner;