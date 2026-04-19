import { useEffect, useState } from "react"
import { defineMessage, useIntl } from "react-intl"
import { useDispatch, useSelector } from "react-redux"
import { postAgristar2Action } from "../actions"
import {
    extractAgristar2BasicSetupFromIoTClient, extractAgristar2PayloadFromIoTClient,
    extractBeeModeFromAgristar2Payload, extractOnionModeFromAgristar2Payload,
    extractPecanModeFromAgristar2Payload, extractPermissionsFromIoTClient,
    selectSaving, selectSelectedIoTClient,
} from "../selectors"

const useAS2FormP2Basic = () => {
    // ----------------HOOKS---------------------------
    const dispatch = useDispatch()
    const intl = useIntl()

    // ----------------SELECTORS----------------------
    const iotClient = useSelector((state)=>selectSelectedIoTClient(state));
    const saving = useSelector((state)=>selectSaving(state));
    const basicSetup = extractAgristar2BasicSetupFromIoTClient(iotClient);
    const obj_permissions = extractPermissionsFromIoTClient(iotClient)
    const payload = extractAgristar2PayloadFromIoTClient(iotClient);
    const pecanMode = extractPecanModeFromAgristar2Payload(payload);
    const beeMode = extractBeeModeFromAgristar2Payload(payload);
    const onionMode = extractOnionModeFromAgristar2Payload(payload);

    // ----------------AUTHORIZATION-----------------
    const isAuthorized = obj_permissions?.['agristar2_action_level2']

    // ----------------FORM DATA---------------------
    const [payloadToSend, setPayloadToSend] = useState({
        "tag":"p2BasicSetup",
        ...basicSetup,
    });

    // --------------UPDATE IF THERE IS A CHANGE---------
    useEffect(()=>{
        setPayloadToSend({
            "tag":"p2BasicSetup",
            ...basicSetup,
        })
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [JSON.stringify(basicSetup)]);

    // ----------VALUE LABEL MAP-------------------------
    const mapValueToLabel = {
        HomePage:{
            'mnMainData.htm': intl.formatMessage(defineMessage({
                id:'p2BasicSetup[3].mnMainData',
                defaultMessage:'System Monitor',
            })),
            'mnPileTemps.htm':intl.formatMessage(defineMessage({
                id: 'p2BasicSetup[4].mnPileTemps',
                defaultMessage: 'Pile Temperatures',
            })),
            'mnPileHumids.htm':intl.formatMessage(defineMessage({
                id: 'p2BasicSetup[5].mnPileHumids',
                defaultMessage: 'Pile Humidities',
            })),
            'mnRunTimes.htm':intl.formatMessage(defineMessage({
                id: 'p2BasicSetup[6].mnRunTimes',
                defaultMessage: 'System Run Clock',
            })),
            'mnFreqCtrl.htm':intl.formatMessage(defineMessage({
                id: 'p2BasicSetup[7].mnFreqCtrl',
                defaultMessage: 'Fan Speed Control',
            })),
            'mnRampRate.htm':intl.formatMessage(defineMessage({
                id: 'p2BasicSetup[8].mnRampRate',
                defaultMessage: 'Ramp Rate',
            })),
            'mnHumidCtrl.htm':intl.formatMessage(defineMessage({
                id: 'p2BasicSetup[9].mnHumidCtrl',
                defaultMessage: 'Plenum Humidity Control',
            })),
            'mnClimacellTimes.htm':intl.formatMessage(defineMessage({
                id: 'p2BasicSetup[10].mnClimacellTimes',
                defaultMessage: 'ClimaCell Control',
            })),
            'mnCo2Purge.htm':intl.formatMessage(defineMessage({
                id: 'p2BasicSetup[11].mnCo2Purge',
                defaultMessage: 'Purge Control',
            })),
            'mnEquipStatus.htm':intl.formatMessage(defineMessage({
                id: 'p2BasicSetup[12].mnEquipStatus',
                defaultMessage: 'Equipment Status',
            })),
            'mnNetMonitor.htm':intl.formatMessage(defineMessage({
                id: 'p2BasicSetup[27].mnNetMonitor',
                defaultMessage: 'Network Monitor',
            })),
        },
        loginSecure:{
            0: intl.formatMessage(defineMessage({
                id:'p2BasicSetup[15].login.secure.no',
                defaultMessage:'No',
            })),
            1: intl.formatMessage(defineMessage({
                id:'p2BasicSetup[16].login.secure.yes',
                defaultMessage:'Yes',
            })),
        },
        TempType: {
            0: intl.formatMessage(defineMessage({
                id:'p2BasicSetup[18].fahrenheit',
                defaultMessage:'Fahrenheit',
            })),
            1: intl.formatMessage(defineMessage({
                id:'p2BasicSetup[19].celsius',
                defaultMessage:'Celsius',
            })),
        },
        SystemMode: {
            0: intl.formatMessage(defineMessage({
                id:'p2BasicSetup[21].systemmode.potato',
                defaultMessage:'Potato',
            })),
            1: intl.formatMessage(defineMessage({
                id:'p2BasicSetup[22].systemmode.onion',
                defaultMessage:'Onion',
            })),
        },
        Animations:{
            0: intl.formatMessage(defineMessage({
                id:'p2BasicSetup[25].animations.no',
                defaultMessage:'No',
            })),
            1: intl.formatMessage(defineMessage({
                id:'p2BasicSetup[26].animations.yes',
                defaultMessage:'Yes',
            })),
        },
    }

    const beeLabel = intl.formatMessage(defineMessage({
        id:'p2BasicSetup[28].systemmode.beemode',
        defaultMessage:'Bee',
    }));

    const pecanLabel = intl.formatMessage(defineMessage({
        id:'p2BasicSetup[29].systemmode.pecanmode',
        defaultMessage:'Pecan',
    }));

    const [bee, setBee] = useState(beeMode ? { "2": beeLabel } : undefined);

    const [pecan, setPecan] = useState(pecanMode ? { "3": pecanLabel } : undefined);

    const [systemModeLabel, setSystemModeLabel] = useState({
        ...mapValueToLabel.SystemMode,
        ...bee,
        ...pecan,
    });

    useEffect(() => {
        setBee(beeMode ? { "2": beeLabel } : undefined);
    }, [beeMode, beeLabel]);

    useEffect(() => {
        setPecan(pecanMode ? { "3": pecanLabel } : undefined);
    }, [pecanMode, pecanLabel]);

    useEffect(() => {
        if (beeMode) {
            setSystemModeLabel({...mapValueToLabel.SystemMode, ...bee });
        } else if (pecanMode) {
            setSystemModeLabel({...mapValueToLabel.SystemMode, ...pecan });
        } else {
            setSystemModeLabel({...mapValueToLabel.SystemMode });
        }
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [bee, pecan]);

    const [homePage, setHomePage] = useState(mapValueToLabel.HomePage)

    useEffect(() => {
        if (onionMode) {
            const hp = {...mapValueToLabel.HomePage};
            delete hp['mnCo2Purge.htm'];
            setHomePage(hp);
        } else {
            setHomePage(mapValueToLabel.HomePage)
        }
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [onionMode])

    // ---------CHANGE HANDLERS-----------------------
    const handlePayloadToSend = (key, val) => {
        if (key === 'CO2_50K') {
            setPayloadToSend({
                ...payloadToSend,
                [key]: val ? '1' : '0',
            })
        } else {
            setPayloadToSend({
                ...payloadToSend,
                [key]:val
            });
        }
    }

    // ---------SUBMIT--------------------------------
    const submitPayloadToSend = () => {
        dispatch(postAgristar2Action(iotClient, payloadToSend))
    }
    
    // ---------ERRORS--------------------------------
    const [errors, setErrors] = useState({});
    useEffect(()=>{
        setErrors({
            StorageName: saving.p2BasicSetup?.errors?.StorageName,
            HomePage: saving.p2BasicSetup?.errors?.HomePage,
            dlr0: saving.p2BasicSetup?.errors?.dlr0,
            loginSecure: saving.p2BasicSetup?.errors?.loginSecure,
            TempType: saving.p2BasicSetup?.errors?.TempType,
            SystemMode: saving.p2BasicSetup?.errors?.SystemMode,
            MultiView: saving.p2BasicSetup?.errors?.MultiView,
            Animations: saving.p2BasicSetup?.errors?.Animations,
            CO2_50K: saving.p2BasicSetup?.errors?.CO2_50K,
        });
    }, [saving])

    return{
        isAuthorized,
        payloadToSend,
        mapValueToLabel,
        saving,
        errors,
        handlePayloadToSend,
        submitPayloadToSend,
        beeMode,
        homePage,
        systemModeLabel,
    }
}
export default useAS2FormP2Basic;
