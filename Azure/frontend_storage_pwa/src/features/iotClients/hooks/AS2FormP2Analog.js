import { useEffect, useState } from "react"
import { defineMessage, useIntl } from "react-intl"
import { useDispatch, useSelector } from "react-redux"
import { useMeasurementSystem } from "../../../utilities/appUnitsOfMeasurements"
import { postAgristar2Action } from "../actions"
import { extractAgristar2AnalogBoardFromIoTClient, extractPermissionsFromIoTClient, selectSaving, selectSelectedIoTClient, extractAgristar2PayloadFromIoTClient, isVersionAtLeast, extractIoTClientVersion } from "../selectors";

const useAS2FormP2Analog = () => {
    // ----------------HOOKS---------------------------
    const dispatch = useDispatch()
    const intl = useIntl()
    const prefSystemOfMeasurement = useMeasurementSystem()

    // ----------------SELECTORS----------------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state));
    const saving = useSelector((state)=>selectSaving(state));
    const analogBoard = extractAgristar2AnalogBoardFromIoTClient(iotClient, prefSystemOfMeasurement);
    const obj_permissions = extractPermissionsFromIoTClient(iotClient)
    const payload = extractAgristar2PayloadFromIoTClient(iotClient);
    const IoTVersion = extractIoTClientVersion(payload);
    const is200plus = isVersionAtLeast(IoTVersion, 2, 0, 0);

    // ----------------AUTHORIZATION-----------------
    const isAuthorized = obj_permissions?.['agristar2_action_level2']

    // ----------------FORM DATA---------------------
    const [payloadToSend, setPayloadToSend] = useState({
        ...analogBoard,
    });

    // --------------UPDATE IF THERE IS A CHANGE---------
    useEffect(()=>{
        setPayloadToSend({
            ...analogBoard,
        })
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [JSON.stringify(analogBoard)]);

    const moveBoard = (dir) => {
        let direction = { SameBoard: payloadToSend.BAdd };
        if (dir === 'Back') {
            direction = { PrevBoard: payloadToSend.BAdd }
        } else if (dir === 'Next') {
            direction = { NextBoard: payloadToSend.BAdd };
        }
        dispatch(postAgristar2Action(iotClient, {
            tag: 'button2',
            ...direction,
        }));
    }

    const returnTemp = intl.formatMessage(defineMessage({
        id: 'level2.analog.return-temperature',
        defaultMessage: 'Return Temperature',
    }));

    const returnHumidity = intl.formatMessage(defineMessage({
        id:'level2.analog.return-humidity',
        defaultMessage: 'Return Humidity'
    }));

    const co2 = intl.formatMessage(defineMessage({
        id: 'p2AnalogHelperTranslatedText[1].co2',
        defaultMessage: 'CO2'
    }));

    const pileTemp = intl.formatMessage(defineMessage({
        id:'level2.analog.pile-temperature',
        defaultMessage:'Pile Temperature'
    }));

    const pileHumidity =intl.formatMessage(defineMessage({
        id:'level2.analog.pile-humidity',
        defaultMessage:'Pile Humidity'
    }));

    const staticPressure = intl.formatMessage(defineMessage({
        id: 'global.static-pressure',
        defaultMessage: 'Static Pressure'
    }));

    const temperature = intl.formatMessage(defineMessage({
        id: 'p2AnalogHelperTranslatedText[2].temperature',
        defaultMessage: 'Temperature',
    }));

    const humidity = intl.formatMessage(defineMessage({
        id:'p2AnalogHelperTranslatedText[0].humidity',
        defaultMessage:'Humidity',
    }));

    const mapValueOriginal = {
        SensorType: {
            1: humidity,
            2: co2,
            3: temperature,
            4: returnTemp,
            6: returnHumidity,
            9: pileTemp,
            10: pileHumidity,
        },
        PileTempOptions: {
            3: pileTemp,
        },
        PileHumidOptions: {
            1: pileHumidity,
            2: co2,
        },
        HumidOptions: {
            1: humidity,
            2: co2,
        },
    };
    const mapValueNew = {
        SensorType: {
            1: humidity,
            2: co2 + ' #1',
            3: temperature,
            4: returnTemp + ' #1',
            5: returnTemp + ' #2',
            6: returnHumidity + ' #1',
            7: returnHumidity + ' #2',
            8: co2 + ' #2',
            9: pileTemp,
            10: pileHumidity,
            11: staticPressure,
        },
        PileTempOptions: {
            9: pileTemp,
            5: returnTemp + ' #2',
        },
        PileHumidOptions: {
            2: 'CO2 #1',
            8: 'CO2 #2',
            10: pileHumidity,
            6: returnHumidity + ' #1',
            7: returnHumidity + ' #2',
            11: staticPressure,
        },
        HumidOptions: {
            1: humidity,
            2: 'CO2 #1',
            8: 'CO2 #2',
            6: returnHumidity + ' #1',
            7: returnHumidity + ' #2',
        },
    };

    // ----------VALUE LABEL MAP-------------------------
    const [mapValueToLabel, setMapValueToLabel] = useState(is200plus ? mapValueNew : mapValueOriginal);
    useEffect(() => {
        is200plus ? setMapValueToLabel(mapValueNew) : setMapValueToLabel(mapValueOriginal);
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [is200plus]);

    // ---------CHANGE HANDLERS-----------------------
    const handlePayloadToSend = (key, val) => {
        const newPayload = {...payloadToSend};
        let newVal = val;
        if (key === 'BDis') {
            newVal = val ? '1' : '0';
            newPayload.sensors.map((item) => item.SenDis = val ? '1' : '0');
        }
        setPayloadToSend({
            ...newPayload,
            [key]: newVal
        })
    }

    const handleSensorToSend = (index, key, val) => {
        let newVal = val;
        if (key === 'SenDis') {
            newVal = val ? '1' : '0';
        }
        const newPayload = {...payloadToSend};
        newPayload.sensors[index][key] = newVal;
        setPayloadToSend({
            ...newPayload,
        });
    }

    // ---------SUBMIT--------------------------------
    const submitPayloadToSend = () => {
        const newPayload = {
            tag: 'p2AnalogBoardSetup',
            BAdd: payloadToSend.BAdd,
            BType: payloadToSend.BType,
            BdLbl: payloadToSend.BdLbl,
            BVer: payloadToSend.BVer,
            BDis: payloadToSend.BDis,
            Sen1Typ: payloadToSend.sensors[0].SenTyp,
            Sen1Lbl: payloadToSend.sensors[0].SenLbl,
            Sen1Off: payloadToSend.sensors[0].SenOff,
            Sen1Dis: payloadToSend.sensors[0].SenDis,
            Sen2Typ: payloadToSend.sensors[1].SenTyp,
            Sen2Lbl: payloadToSend.sensors[1].SenLbl,
            Sen2Off: payloadToSend.sensors[1].SenOff,
            Sen2Dis: payloadToSend.sensors[1].SenDis,
            Sen3Typ: payloadToSend.sensors[2].SenTyp,
            Sen3Lbl: payloadToSend.sensors[2].SenLbl,
            Sen3Off: payloadToSend.sensors[2].SenOff,
            Sen3Dis: payloadToSend.sensors[2].SenDis,
            Sen4Typ: payloadToSend.sensors[3].SenTyp,
            Sen4Lbl: payloadToSend.sensors[3].SenLbl,
            Sen4Off: payloadToSend.sensors[3].SenOff,
            Sen4Dis: payloadToSend.sensors[3].SenDis,
        }
        dispatch(postAgristar2Action(iotClient, newPayload))
    }
    
    // ---------ERRORS--------------------------------
    const [errors, setErrors] = useState({});
    useEffect(() => {
        setErrors({
            BAdd: saving.p2AnalogBoardSetup?.errors?.BAdd,
            BTyp: saving.p2AnalogBoardSetup?.errors?.BTyp,
            BdLbl: saving.p2AnalogBoardSetup?.errors?.BdLbl,
            BVer: saving.p2AnalogBoardSetup?.errors?.BVer,
            BDis: saving.p2AnalogBoardSetup?.errors?.BDis,
            Sen1Typ: saving.p2AnalogBoardSetup?.errors?.Sen1Typ,
            Sen1Lbl: saving.p2AnalogBoardSetup?.errors?.Sen1Lbl,
            Sen1Off: saving.p2AnalogBoardSetup?.errors?.Sen1Off,
            Sen1Dis: saving.p2AnalogBoardSetup?.errors?.Sen1Dis,
            Sen2Typ: saving.p2AnalogBoardSetup?.errors?.Sen2Typ,
            Sen2Lbl: saving.p2AnalogBoardSetup?.errors?.Sen2Lbl,
            Sen2Off: saving.p2AnalogBoardSetup?.errors?.Sen2Off,
            Sen2Dis: saving.p2AnalogBoardSetup?.errors?.Sen2Dis,
            Sen3Typ: saving.p2AnalogBoardSetup?.errors?.Sen3Typ,
            Sen3Lbl: saving.p2AnalogBoardSetup?.errors?.Sen3Lbl,
            Sen3Off: saving.p2AnalogBoardSetup?.errors?.Sen3Off,
            Sen3Dis: saving.p2AnalogBoardSetup?.errors?.Sen3Dis,
            Sen4Typ: saving.p2AnalogBoardSetup?.errors?.Sen4Typ,
            Sen4Lbl: saving.p2AnalogBoardSetup?.errors?.Sen4Lbl,
            Sen4Off: saving.p2AnalogBoardSetup?.errors?.Sen4Off,
            Sen4Dis: saving.p2AnalogBoardSetup?.errors?.Sen4Dis,
        });
    }, [saving]);

    return{
        isAuthorized,
        payloadToSend,
        mapValueToLabel,
        saving,
        errors,
        moveBoard,
        handlePayloadToSend,
        handleSensorToSend,
        submitPayloadToSend,
        is200plus,
    }
}
export default useAS2FormP2Analog;
