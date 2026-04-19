import { useEffect, useState } from "react"
import { defineMessage, useIntl } from "react-intl"
import { useDispatch, useSelector } from "react-redux"
import { agristar2Refrigeration } from "../../../utilities/translationObjects"
import { postAgristar2Action } from "../actions"
import {
    extractAgristar2AuxProgFromIoTClient,
    extractAgristar2IONamesFromIoTClient,
    extractAgristar2PayloadFromIoTClient,
    extractBeeModeFromAgristar2Payload,
    extractOnionModeFromAgristar2Payload,
    extractPecanModeFromAgristar2Payload,
    extractPermissionsFromIoTClient,
    extractSystemModeFromAgristar2Payload,
    selectSaving,
    selectSelectedIoTClient,
} from "../selectors"

const useAS2FormP2Analog = () => {
    // ----------------HOOKS---------------------------
    const dispatch = useDispatch()
    const intl = useIntl()

    // ----------------SELECTORS----------------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state));
    const saving = useSelector((state) => selectSaving(state));
    const auxProg = extractAgristar2AuxProgFromIoTClient(iotClient);
    const { IoNames, OutputConfig, InputConfig } = extractAgristar2IONamesFromIoTClient(iotClient);
    const obj_permissions = extractPermissionsFromIoTClient(iotClient)
    const payload = extractAgristar2PayloadFromIoTClient(iotClient);
    const systemMode = extractSystemModeFromAgristar2Payload(payload);
    const potatoMode = systemMode === '0';
    const onionMode = extractOnionModeFromAgristar2Payload(payload);
    const beeMode = extractBeeModeFromAgristar2Payload(payload);
    const pecanMode = extractPecanModeFromAgristar2Payload(payload);

    // ----------------AUTHORIZATION-----------------
    const isAuthorized = obj_permissions?.['agristar2_action_level2']

    // ----------------FORM DATA---------------------
    const [payloadToSend, setPayloadToSend] = useState({
        ...auxProg,
    });

    // --------------UPDATE IF THERE IS A CHANGE---------
    useEffect(()=>{
        setPayloadToSend({
            ...auxProg,
        })
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [JSON.stringify(auxProg)]);

    const moveAux = (dir) => {
        let direction = { NextAux: payloadToSend.AuxProgram };
        if (dir === 'Back') {
            direction = { PrevAux: payloadToSend.AuxProgram }
        }
        dispatch(postAgristar2Action(iotClient, {
            tag: 'button2',
            ...direction,
        }));
    }

    // ----------VALUE LABEL MAP-------------------------
    const mapValueToLabel = {
        units: {
            '0': intl.formatMessage(defineMessage({
                id: 'p2AuxProg[23].minutes',
                defaultMessage: 'Minutes',
            })),
            '1': intl.formatMessage(defineMessage({
                id: 'p2AuxProg[24].hours',
                defaultMessage: 'Hours',
            })),
        },
        onOff: {
            '0': intl.formatMessage(defineMessage({
                id:'p2AuxProg[16].off',
                defaultMessage:'Off',
            })),
            '1': intl.formatMessage(defineMessage({
                id:'p2AuxProg[15].on',
                defaultMessage:'On',
            })),
        },
        type:{
            '255': '',
            '0': intl.formatMessage(defineMessage({
                id:'p2AuxProg[8].manual',
                defaultMessage:'Manual',
            })),
            '1': intl.formatMessage(defineMessage({
                id: 'p2AuxProg[9].output',
                defaultMessage: 'Output',
            })),
            '2': intl.formatMessage(defineMessage({
                id: 'p2AuxProg[10].input',
                defaultMessage: 'Input',
            })),
            '3': intl.formatMessage(defineMessage({
                id: 'p2AuxProg[11].switch',
                defaultMessage: 'Switch',
            })),
            '4': intl.formatMessage(defineMessage({
                id: 'p2AuxProg[12].sensor',
                defaultMessage: 'Sensor',
            })),
            '5': intl.formatMessage(defineMessage({
                id: 'p2AuxProg[13].mode',
                defaultMessage: 'Mode',
            })),
        },
        op:{
            '0': intl.formatMessage(defineMessage({
                id:'p2AuxProg[17].equal',
                defaultMessage:'EQ',
            })),
            '1': intl.formatMessage(defineMessage({
                id: 'p2AuxProg[18].gt',
                defaultMessage: 'GT',
            })),
            '2': intl.formatMessage(defineMessage({
                id: 'p2AuxProg[19].lt',
                defaultMessage: 'LT',
            })),
        },
        andOr:{
            '0': intl.formatMessage(defineMessage({
                id:'p2AuxProg[6].and',
                defaultMessage:'AND',
            })),
            '1': intl.formatMessage(defineMessage({
                id: 'p2AuxProg[7].or',
                defaultMessage: 'OR',
            })),
            '255': intl.formatMessage(defineMessage({
                id: 'p2AuxProg[5].end',
                defaultMessage: 'END',
            })),
        },
        option: {
            '255': intl.formatMessage(defineMessage({
                id: 'p2AuxProg[20].option',
                defaultMessage: 'Option',
            })),
            '0': intl.formatMessage(defineMessage({
                id: 'p2AuxProg[21].value',
                defaultMessage: 'Value',
            })),
            '1': intl.formatMessage(defineMessage({
                id: 'p2AuxProg[22].reference',
                defaultMessage: 'Reference',
            })),
        },
        mode:{
            '0': intl.formatMessage(defineMessage({
                id:'p2AuxProgModeList[0].cooling',
                defaultMessage:'Cooling',
            })),
            '1': intl.formatMessage(agristar2Refrigeration),
            '2': intl.formatMessage(defineMessage({
                id: 'p2AuxProgModeList[2].recirc',
                defaultMessage: 'Recirculation',
            })),
            '3': intl.formatMessage(defineMessage({
                id: 'p2AuxProgModeList[3].heating',
                defaultMessage: 'Heating',
            })),
            '4': intl.formatMessage(defineMessage({
                id: 'p2AuxProgModeList[4].purge',
                defaultMessage: 'CO2 Purge',
            })),
            '5': intl.formatMessage(defineMessage({
                id: 'p2AuxProgModeList[5].defrost',
                defaultMessage: 'Defrost',
            })),
            '6': intl.formatMessage(defineMessage({
                id: 'p2AuxProgModeList[6].standby',
                defaultMessage: 'Standby',
            })),
            '7': intl.formatMessage(defineMessage({
                id: 'p2AuxProgModeList[7].shutdown',
                defaultMessage: 'Shutdown',
            })),
        },
    }

    const cureLabel = intl.formatMessage(defineMessage({
        id: 'p2AuxProgModeList[8].cure',
        defaultMessage: 'Cure',
    }));

    const [cure, setCure] = useState(onionMode ? { '8': cureLabel } : undefined);

    const [mode, setMode] = useState({ ...mapValueToLabel.mode, ...cure });

    useEffect(() => {
        setCure(onionMode ? { '8': cureLabel } : undefined);
    }, [onionMode, cureLabel]);

    useEffect(() => {
        if (onionMode) {
            setMode({...mapValueToLabel.mode, ...cure });
        } else {
            setMode({...mapValueToLabel.mode});
        }
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [cure])

    // ---------CHANGE HANDLERS-----------------------
    const handlePayloadToSend = (key, val) => {
        setPayloadToSend({
            ...payloadToSend,
            [key]:val
        })
    }

    const handleRuleToSend = (index, key, val) => {
        const newPayload = {...payloadToSend};
        newPayload.rules[index][key] = val;
        // if we update the last one then the next becomes the end
        if (key === 'andOr' && index < 5 && val !== '255'
            && newPayload.rules[index + 1]['andOr'] === '256') {
            newPayload.rules[index + 1]['andOr'] = '255';
        }
        // turn off all other rules
        if (key === 'andOr' && val === '255') {
            newPayload.rules[index]['first'] = true;
            for (let i = index + 1; i < 6; i += 1) {
                newPayload.rules[i]['andOr'] = '256';
            }
        }
        if (key === 'type') {
            if (val === '3') {
                newPayload.rules[index]['io'] = '44';
            } else if (val === '4') {
                newPayload.rules[index]['io'] = '-1';
            } else {
                newPayload.rules[index]['io'] = '0';
            }

            newPayload.rules[index]['first'] = true;
            if (val === '4') {
                newPayload.rules[index]['st'] = '255';
                newPayload.rules[index]['rt'] = '255';
                newPayload.rules[index]['sen'] = '';
                newPayload.rules[index]['diff'] = '';
            } else {
                newPayload.rules[index]['op'] = '255';
                newPayload.rules[index]['sen'] = '255';
                newPayload.rules[index]['diff'] = '255';
                newPayload.rules[index]['rt'] = '255';
                newPayload.rules[index]['ref'] = '255';
                if (val !== '5') {
                    newPayload.rules[index]['st'] = '1';
                } else {
                    newPayload.rules[index]['st'] = '255';
                }
            }
        }
        if (key === 'sensorOption') {
            newPayload.rules[index]['first'] = false;
            newPayload.rules[index]['op'] = '0';
            newPayload.rules[index]['diff'] = '';
            newPayload.rules[index]['ref'] = val === '0' ? '255' : '-1';
        }
        setPayloadToSend({
            ...newPayload,
        });
    }

    // ---------SUBMIT--------------------------------
    const submitPayloadToSend = () => {
        const newPayload = {
            tag: 'p2AuxProgFrame',
            AuxProgram: payloadToSend.AuxProgram,
            dutyCycle: payloadToSend.dutyCycle,
            period: payloadToSend.period,
            units: payloadToSend.units
        };
        payloadToSend.rules.forEach((item, index) => {
            if (index > 0) {
                newPayload[`andOr${index+1}`] = item.andOr === '256' ? '255' : item.andOr;
            }
            newPayload[`type${index+1}`] = item.type;
            newPayload[`io${index+1}`] = item.io;
            newPayload[`st${index+1}`] = item.st;
            newPayload[`op${index+1}`] = item.op;
            newPayload[`ref${index+1}`] = item.ref;
            newPayload[`sen${index+1}`] = item.sen;
            newPayload[`diff${index+1}`] = item.diff;
        });
        dispatch(postAgristar2Action(iotClient, newPayload))
    }

    const availSensors = (ref) => {
        const sensors = {};

        if (ref) {
            sensors['-2'] = 'Temp Setpoint';
            sensors['-8'] = 'Temp 2 Setpoint';
        }

        sensors['-1'] = 'Plenum Temp';
        sensors['2'] = 'Outside Temp';
        sensors['3'] = 'Return Temp';
        if (ref) {
            sensors['-3'] = 'Humidity Setpoint';
        }
        sensors['5'] = 'Plenum Humidity';
        sensors['4'] = 'Outside Humidity';
        sensors['6'] = 'Return Humidity';
        if (!ref) {
            sensors['-4'] = 'Fan Speed';
            sensors['-5'] = 'Refrig Output';
            sensors['-6'] = 'Cooling Output';
        } else {
            sensors['-7'] = 'Cooling Available';
            sensors['-9'] = 'CO2 Setpoint'
        }
        sensors['7'] = 'CO2 Level';
        return sensors;
    }

    const availEquip = (type) => {
        const equip = {};
        const ioConfig = type === 'input' ? InputConfig : OutputConfig;

        let listInfo;

        for (let i = 0; i < ioConfig.length; i += 1) {
            if (ioConfig[i] !== '-1') {
                listInfo = IoNames[i].split(':');
                if (((potatoMode || pecanMode) && listInfo[1] === '1')
                    || (onionMode && listInfo[1] === '2')
                    || (beeMode && (listInfo[1] === '4' || listInfo[1] === '5'))
                    || listInfo[1] === '4' || listInfo[1] === '7') {
                    if ((type === 'output' && listInfo[2] === '0')
                        || (type === 'input' && listInfo[2] === '1')
                        || listInfo[2] === '2') {
                        equip[listInfo[4]] = listInfo[0];
                    }
                }
            }
        }
        if (type === 'output' && ioConfig[40] === '1') {
            listInfo = IoNames[41].split(':');
            equip[listInfo[4]] = listInfo[0];
            listInfo = IoNames[42].split(':');
            equip[listInfo[4]] = listInfo[0];
        }

        return equip;
    }

    const availSwitch = () => {
        const switches = {};
        for (let i = 0; i < IoNames.length; i += 1) {
            var listInfo = IoNames[i].split(':');
            if ((listInfo[2] === '3') && (
                ((potatoMode || pecanMode) && listInfo[1] === '1')
                || (onionMode && listInfo[1] === '2')
                || (beeMode && (listInfo[1] === '4' || listInfo[1] === '5'))
                || listInfo[1] === '4' || listInfo[1] === '7')
            ) {
                switches[listInfo[4]] = listInfo[0];
            }
        }
        return switches;
    }

    const [errors, setErrors] = useState({});
    useEffect(() => {
        setErrors({
            sen1: saving.p2AuxProgFrame?.errors?.sen1,
            sen2: saving.p2AuxProgFrame?.errors?.sen2,
            sen3: saving.p2AuxProgFrame?.errors?.sen3,
            sen4: saving.p2AuxProgFrame?.errors?.sen4,
            sen5: saving.p2AuxProgFrame?.errors?.sen5,
            sen6: saving.p2AuxProgFrame?.errors?.sen6,
            diff1: saving.p2AuxProgFrame?.errors?.diff1,
            diff2: saving.p2AuxProgFrame?.errors?.diff2,
            diff3: saving.p2AuxProgFrame?.errors?.diff3,
            diff4: saving.p2AuxProgFrame?.errors?.diff4,
            diff5: saving.p2AuxProgFrame?.errors?.diff5,
            diff6: saving.p2AuxProgFrame?.errors?.diff6,
            dutyCycle: saving.p2AuxProgFrame?.errors?.dutyCycle,
            period: saving.p2AuxProgFrame?.errors?.period,
        });
    }, [saving]);

    return {
        isAuthorized,
        payloadToSend,
        mapValueToLabel,
        availSensors,
        availEquip,
        availSwitch,
        moveAux,
        saving,
        errors,
        handlePayloadToSend,
        handleRuleToSend,
        submitPayloadToSend,
        mode,
    };
}
export default useAS2FormP2Analog;
