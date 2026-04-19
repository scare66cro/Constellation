import { createReducer } from '@reduxjs/toolkit'
import { differenceInMinutes } from 'date-fns';
import { getAgristar2DataFailed, getAgristar2DataStart, getAgristar2DataSuccess,
    loadIotClientsFailed, loadIotClientsStart, loadIotClientsSuccess,
    postAgristar2ActionFailed, postAgristar2ActionStart, postAgristar2ActionSuccess,
    postUpgradeActionFailed, postUpgradeActionStart, postUpgradeActionSuccess,
    resetStatus, setSelectedIotClient, resetSavingRefreshAction, postAgristar2ActionValidation,
    getClientTokenStart, getClientTokenSuccess, getClientTokenFailed, resetClientTokenStart,
    resetClientTokenSuccess, resetClientTokenFailed, getAgristar2FrontMatterStart,
    getAgristar2FrontMatterSuccess, getAgristar2FrontMatterFailed, setPanelActiveStateStart,
    setPanelActiveStateSuccess, setPanelActiveStateFailed, saveNewPanelFailed,
    saveNewPanelSuccess } from './actions'
import { isVersionAtLeast } from './selectors'

const initialState = {
    _status: "pending", //pending, idle, success, failed
    _iot: "pending", // status for loading iot client
    _msg: undefined,
    saving: { refresh: false },
    iotClients: [],
    activeToken: {},
    tokenRetrieved: { value: false, error: '' },
    _selectedIotClient: undefined, //iotClient's id (uuidv4)
    _iotClientActions:{ // PENDING deviceActions sent to the physical Agristar2
        // ...remain here remain here until being resolved.
        // "the device UUID4": {
        //     "p1Plenum": {
        //         "status": "failed",
        //         "error_response": {
        //             "detail": "Device not an agristar"
        //         },
        //     },
        //     "p1OutsideAir": {
        //         "status": "failed",
        //         "error_response": {
        //             "detail": "Device not an agristar"
        //         }
        //     } 
        // },
        // "the device UUID4": {
        //     "p1OutsideAir": {
        //         "status": "failed",
        //         "error_response": {
        //             "detail": "Device not an agristar"
        //         }
        //     }
        // }
    }
}

const getPileSensorTypes = (iotVersion) => {
    if (!iotVersion) return ['3', '9'];
    return isVersionAtLeast(iotVersion, 2, 0, 0) ? ['9'] : ['3'];
};

const setIoTFrontMatterError = (state, id) => {
    const index = state.iotClients.findIndex(i => i.id === id);
    const iotClient = index >= 0 ? state.iotClients[index] : undefined;
    if (iotClient?.front_matter) {
        return [
            ...state.iotClients.slice(0,index),
            { 
                ...iotClient,
                status: 'idle',
                front_matter: {
                    ...iotClient.front_matter,
                    error: true,
                }
            },
            ...state.iotClients.slice(index+1),
        ];
    } else {
        return [
            ...state.iotClients.slice(0,index),
            { 
                ...iotClient,
                status: 'idle',
            },
            ...state.iotClients.slice(index+1),
        ];
    }
}

const setIoTClientError = (state, id) => {
    const index = state.iotClients.findIndex(i => i.id === id);
    const iotClient = index >= 0 ? state.iotClients[index] : undefined;
    if (iotClient) {
        return [
            ...state.iotClients.slice(0,index),
            { 
                ...iotClient,
                status: 'idle',
                last_log: { 
                    payload: { 
                        payload: {
                            ...(iotClient.last_log?.payload ? iotClient.last_log.payload.payload :  {}),
                            error: true,
                        },
                    },
                    time_stamp: (iotClient.last_log ? iotClient.last_log.time_stamp : Date.now()),
                },
            },
            ...state.iotClients.slice(index+1),
        ];
    } else {
        return [...state.iotClients];
    }
}

/** TODO
 * We need to handle front-matter and settings better.
 */
function mergeFrontMatter(existingFrontMatter, newData) {
    const newFrontMatter = getFrontMatter(newData);
    
    // If there's no existing front matter, just return the new one
    if (!existingFrontMatter) {
        return newFrontMatter;
    }
    
    // Helper function to merge arrays element by element, keeping existing values when new values are undefined
    const mergeArrays = (newArray, existingArray) => {
        if (!newArray) return existingArray;
        if (!existingArray) return newArray;
        
        // Use the longer array length to ensure we don't lose any data
        const maxLength = Math.max(newArray.length, existingArray.length);
        const result = [];
        
        for (let i = 0; i < maxLength; i++) {
            // Use new value if it exists and is not undefined/null, otherwise use existing value
            if (i < newArray.length && newArray[i] !== undefined && newArray[i] !== null) {
                result[i] = newArray[i];
            } else if (i < existingArray.length) {
                result[i] = existingArray[i];
            } else {
                result[i] = undefined;
            }
        }
        
        return result;
    };
    
    return {
        error: newFrontMatter.error !== undefined ? newFrontMatter.error : (existingFrontMatter.error ?? false),
        main: mergeArrays(newFrontMatter.main, existingFrontMatter.main),
        misc: mergeArrays(newFrontMatter.misc, existingFrontMatter.misc),
        AlarmData: newFrontMatter.AlarmData !== undefined ? newFrontMatter.AlarmData : existingFrontMatter.AlarmData,
    };
}

function getFrontMatter(data) {
    let pileAvg = 0;
    const pileSensorTypes = getPileSensorTypes(data.IoTClientVersion);
    if (data.Sensors?.length > 8) {
        const sensors = data.Sensors.reduce((result, _, index, array) => {
            if (index % 4 === 0) {
                result.push(array.slice(index, index + 4));
            }
            return result;
        }, []);
        const valid = sensors.filter((i, index) => index >= 2 && i[3] !== '--' && pileSensorTypes.includes(i[2]));
        if (valid.length > 0) {
            pileAvg = valid.reduce(
            (prev, current) => prev + current[3] * 1,
            0,
            ) / valid.length;
        }
    } else if (data.PileTempsData?.length > 0) {
        const valid = data.PileTempsData.filter((i, index) => index % 2 === 0 && i !== '--');
        if (valid.length > 0) {
            pileAvg = valid.reduce(
            (prev, current) => prev + current * 1,
            0,
            ) / valid.length;
        }
    }

    const beeMode = data.P2BasicSetupData?.[4] === '2';
    const purgeMode = data.Co2PurgeData?.[0];
    const co2SetPoint = purgeMode === '2' ? data.Co2PurgeData?.[beeMode ? 7 : 4] : '1200';

    return {
        error: false,
        main: [
            data.CurrentMode?.[0], // 0 - currentMode
            data.P2BasicSetupData?.[1], // 1 - TempType
            data.MainData?.[0], // 2 - plenumTemp
            data.PgmData?.[0], // 3 - plenumTempSet
            data.PgmData?.[5] ?? null, // 4 - plenumTempSet2
            data.MainData?.[1], // 5 - plenumHumid
            data.PgmData?.[1], // 6 - plenumHumidSet
            data.MainData?.[2], // 7 - outsideTemp
            data.MainData?.[4], // 8 - outsideHumid
            data.MainData?.[8], // 9 - returnTemp
            data.MainData?.[7], // 10 - returnHumid
            data.AirCureData?.[0], // 11 - CureStartTemp
            data.AirCureData?.[2], // 12 - CureStartHumid
            pileAvg.toString(), // 13 - PileTempAvg
            data.MainData?.[10], // 14 - fanSpeed
            data.MainData?.[11], // 15 - Output
            data.MainData?.[12], // 16 - Mode
            data.MainData?.[9], // 17 - co2Level
            data.MainData?.[13], // 18 - BurnerOutput
            data.EquipStatusData?.[56], // 19 - BayLight1
            data.LoadMonitorData?.[0], // 20 - BayLight1Name
            data.EquipStatusData?.[58], // 21 - BayLight2
            data.LoadMonitorData?.[1], // 22 - BayLight2Name
            data.P2BasicSetupData?.[4], // 23 - SystemMode (Potato, Onion, etc.)
            data.EquipStatusData?.[8], // 24 - Cure Output
            data.EquipStatusData?.[52], // 25 - Cure Remote Off
            data.PlenTempDevData?.[4], // 26 - Cure Temp Low
            data.PlenTempDevData?.[5], // 27 - Cure Temp High
            data.AirCureData?.[3], // 28 - Cure Humid High Limit
            data.PgmData?.[2], // 29 - Plenum Humid Reference
            data.AirCureData?.[1], // 30 - Cure Humid Reference
            data.MainData?.[14], // 31 - Calc Humid
            data.Co2PurgeData?.[0], // 32 - CO2 Purge Mode
            co2SetPoint, // 33 - CO2 Set Point
            data.MainData?.[17], // 34 - Refrigeration Output
            data.MainData?.[19], // 35 - Return Humidity 2
            data.MainData?.[20], // 36 - CO2 2
            data.MainData?.[21], // 37 - Return Temp 2
            data.MainData?.[22], // 38 - Moisture Loss 1
            data.MainData?.[23], // 39 - Moisture Loss 2
        ],
        misc: [
            data.BoardType?.[0], // 0 - BoardType
            data.P2BasicSetupData?.[0], // 1 - panelName
            data.IoTClientVersion, // 2 - IoTClientVersion
            data.SysVersions?.[0], // 3 - ControllerVersion
        ],
        AlarmData: data.AlarmData,
    };
}

const iotClientsReducer = createReducer(initialState, (builder) => {
    builder
        // add dismiss status action reset to 'idle'
        .addCase(resetStatus, state => {
            return{
                ...state,
                _status: 'idle'
            }
        })
        .addCase(setSelectedIotClient, (state, action) => {
            return {
                ...state,
                _selectedIotClient: action.payload
            }
        })
        .addCase(loadIotClientsStart, (state, action) => {
            return{
                ...state,
                _iot: 'pending',
            }
        })
        .addCase(loadIotClientsSuccess, (state, action) => {
            const currentTime = new Date();
            return {
                ...state,
                _iot: 'idle',
                iotClients:action.payload.map((client) => {
                    const diffTime = differenceInMinutes(currentTime, new Date(client?.time_stamp))
                    if (client.front_matter) {
                        return {
                            ...client,
                            front_matter: {
                                ...client.front_matter,
                                error: client.front_matter.error || diffTime >= 30,
                            },
                        };
                    } else {
                        return { ...client, last_log: { payload: null }};
                    }
                }),
            }
        })
        .addCase(loadIotClientsFailed, (state, action) => {
            return{
                ...state,
                _iot: 'failed',
                _msg:action.payload
            };
        })
        .addCase(saveNewPanelSuccess, (state, action) => {
            const panel = action.payload;
            let index = -1;
            let iotClients = [];
            for (let i = 0; i < state.iotClients.length; i += 1) {
                if (state.iotClients[i].name.toUpperCase() > panel.name.toUpperCase()) {
                    index = i;
                    break;
                }
            }
            if (index > 0) {
                iotClients = [...state.iotClients.slice(0, index), {...panel}, ...state.iotClients.slice(index + 1)];
            } else if (index === 0) {
                iotClients = [{...panel}, ...state.iotClients];
            } else {
                iotClients = [...state.iotClients, {...panel}];
            }
            return {
                ...state,
                _status: 'idle',
                iotClients,
            };
        })
        .addCase(saveNewPanelFailed, (state, action) => {
            return {
                ...state,
                _status: 'failed',
                _msg: action.payload,
            };
        })
        .addCase(resetClientTokenStart, (state, action) => {
            return {
                ...state,
                _status: 'pending',
                activeToken: {},
                tokenRetrieved: { value: false, error: '' },
            }
        })
        .addCase(resetClientTokenSuccess, (state, action) => {
            return{
                ...state,
                _status: 'idle',
                activeToken: action.payload.data,
                tokenRetrieved: { value: true, error: '' },
            }
        })
        .addCase(resetClientTokenFailed, (state, action) => {
            return{
                ...state,
                _status: 'failed',
                tokenRetrieved: { value: false, error: action.payload },
            }
        })
        .addCase(setPanelActiveStateStart, (state, action) => {
            return {
                ...state,
                _status: 'pending',
            }
        })
        .addCase(setPanelActiveStateSuccess, (state, action) => {
            const index = state.iotClients.findIndex((i) => i.id === action.payload.data.id);
            return {
                ...state,
                _status: 'idle',
                iotClients: [
                    ...state.iotClients.slice(0, index),
                    { ...state.iotClients[index], is_active: action.payload.data.is_active, time_stamp: new Date().toISOString(), status: 'idle' },
                    ...state.iotClients.slice(index + 1),
                ],
            };
        })
        .addCase(setPanelActiveStateFailed, (state, action) => {
            return {
                ...state,
                _status: 'failed',
            }
        })
        .addCase(getClientTokenStart, (state, action) => {
            return{
                ...state,
                _status: 'pending',
                activeToken: {},
                tokenRetrieved: { value: false, error: '' },
            }
        })
        .addCase(getClientTokenSuccess, (state, action) => {
            return{
                ...state,
                _status: 'idle',
                activeToken: action.payload.data,
                tokenRetrieved: { value: true, error: '' },
            }
        })
        .addCase(getClientTokenFailed, (state, action) => {
            return{
                ...state,
                _status: 'failed',
                tokenRetrieved: { value: false, error: action.payload },
            }
        })
    // -----------------UPGRADE IOTCLIENT-------------------
        .addCase(postUpgradeActionStart, (state, action) => {
            const id = action.payload.id;
            const iotClient = state.iotClients.find((i) => i.id === id);
            const index = state.iotClients.findIndex((i) => i.id === id);
            return {
                ...state,
                iotClients: [
                    ...state.iotClients.slice(0,index),
                    {...iotClient, inUpgrade: true},
                    ...state.iotClients.slice(index+1),
                ]
            }
        })
        .addCase(postUpgradeActionSuccess, (state, action) => {
            const id = action.payload.id;
            const iotClient = state.iotClients.find((i) => i.id === id);
            const index = state.iotClients.findIndex((i) => i.id === id);
            return {
                ...state,
                iotClients: [
                    ...state.iotClients.slice(0,index),
                    { ...iotClient, inUpgrade: false },
                    ...state.iotClients.slice(index+1),
                ],
            };
        })
        .addCase(postUpgradeActionFailed, (state, action) => {
            const id = action.payload.id;
            const iotClient = state.iotClients.find((i) => i.id === id);
            const index = state.iotClients.findIndex((i) => i.id === id);
            return {
                ...state,
                iotClients: [
                    ...state.iotClients.slice(0,index),
                    {...iotClient, inUpgrade: false},
                    ...state.iotClients.slice(index+1),
                ]
            }
        })
    // -----------------GET AGRISTAR2 DATA-------------------
        .addCase(getAgristar2DataStart, (state, action) => {
            const id = action.payload.id;
            const index = state.iotClients.findIndex(i => i.id === id);
            const iotClient = index >= 0 ? state.iotClients[index] : undefined;
            return iotClient
                ? {
                    ...state,
                    iotClients: [
                        ...state.iotClients.slice(0, index),
                        {...iotClient, status: 'pending' },
                        ...state.iotClients.slice(index+1),
                    ]
                } : {
                    ...state
                };
        })
        .addCase(getAgristar2DataSuccess, (state, action) => {
            const id = action.payload.id
            const index = state.iotClients.findIndex(i => i.id === id);
            const iotClient = index >= 0 ? state.iotClients[index] : undefined;
            const newLastLog = { 
                payload: {payload:{...iotClient?.last_log?.payload.payload, ...action.payload.data}, error: false},
            }
            return{
                ...state,
                iotClients: [
                    ...state.iotClients.slice(0,index),
                    { ...iotClient, front_matter: mergeFrontMatter(iotClient.front_matter, action.payload.data), last_log: newLastLog, time_stamp: new Date().toISOString(), status: 'idle' },
                    ...state.iotClients.slice(index+1),
                ]
            }
        })  
        .addCase(getAgristar2DataFailed, (state, action) => {
            return {
                ...state,
                iotClients: setIoTClientError(state, action.payload?.id)
            }
        })
    // -----------------GET AGRISTAR2 FRONT MATTER -------------------
        .addCase(getAgristar2FrontMatterStart, (state, action) => {
            const id = action.payload.id;
            const index = state.iotClients.findIndex(i => i.id === id);
            const iotClient = index >= 0 ? state.iotClients[index] : undefined;
            return {
                ...state,
                iotClients: [
                    ...state.iotClients.slice(0, index),
                    {...iotClient, status: 'pending'},
                    ...state.iotClients.slice(index+ 1),
                ]
            };
        })
        .addCase(getAgristar2FrontMatterSuccess, (state, action) => {
            const id = action.payload.id;
            const index = state.iotClients.findIndex(i => i.id === id);
            const iotClient = index >= 0 ? state.iotClients[index] : undefined;
            const frontMatter = {
                ...action.payload.data,
                error: false,
            };
            return {
                ...state,
                iotClients: [
                    ...state.iotClients.slice(0, index),
                    { ...iotClient, front_matter: {...frontMatter }, time_stamp: new Date().toISOString(), status: 'idle' },
                    ...state.iotClients.slice(index + 1),
                ],
            };
        })
        .addCase(getAgristar2FrontMatterFailed, (state, action) => {
            return {
                ...state,
                iotClients: setIoTFrontMatterError(state, action.payload?.id),
            };
        })
    // -----------------POST AGRISTAR2 ACTION-----------------
        .addCase(postAgristar2ActionStart, (state, action) => {
            console.log('...start post as2 action')
            return{
                ...state,
                saving: { ...state.saving, [action.payload.tag]: { status: true, errors: [] }},
                _status: 'pending'
            }
        })
        .addCase(postAgristar2ActionSuccess, (state, action) => {
            const id = action.payload.id
            const iotClient = state.iotClients.find(i => i.id === id)
            const index = state.iotClients.findIndex(i => i.id === id)
            const newLastLog = {
                payload: {payload:{...iotClient?.last_log?.payload.payload, ...action.payload.data}, error: false},
                time_stamp: new Date().toISOString()
            }
            return{
                ...state,
                _status: 'idle',
                saving: { ...state.saving, [action.payload.tag]: { status: false, errors: [] }, refresh: true},
                iotClients: [
                    ...state.iotClients.slice(0,index),
                    { ...iotClient, front_matter: mergeFrontMatter(iotClient.front_matter, action.payload.data), last_log: newLastLog },
                    ...state.iotClients.slice(index+1),
                ]
            }
        })
        .addCase(postAgristar2ActionFailed, (state, action) => {
            return {
                ...state,
                saving: { ...state.saving, [action.payload.tag]: { status: false, errors: action.payload.data }},
                _status: 'failed',
                iotClients: setIoTClientError(state, action.payload.id),
            }
        })
        .addCase(postAgristar2ActionValidation, (state, action) => {
            return {
                ...state,
                saving: { ...state.saving, [action.payload.tag]: { status: false, errors: action.payload.data }},
                _status: 'failed',
            }
        })
        .addCase(resetSavingRefreshAction, (state, action) => {
            return {
                ...state,
                saving: { ...state.saving, refresh: false }
            }
        })
})
export default iotClientsReducer