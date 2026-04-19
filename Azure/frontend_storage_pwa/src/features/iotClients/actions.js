import { createAction } from '@reduxjs/toolkit'
import axiosCatchDefaults from '../../utilities/axiosCatchDefaults'
import axiosInstance from '../../utilities/axiosConfig'
import { agristar2PostActionErrorHTTP503 } from '../../utilities/translationObjects'
import { extractAgristar2PayloadFromIoTClient, extractIoTClientVersion, isVersionAtLeast } from './selectors'

export const setSelectedIotClient = createAction('setSelectedIotClient')
export const resetStatus = createAction('resetStatus')

export const saveNewPanelSuccess = createAction('saveNewPanel/Success');
export const saveNewPanelFailed = createAction('saveNewPanel/Failed');

export const saveNewPanel = (panel) => {
    return (dispatch) => {
        axiosInstance.post('/api/iot-clients/new_panel/', {
            panel
        })
            .then((resp) => {
                dispatch(saveNewPanelSuccess(resp.data));
            })
            .catch((err) => {
                dispatch(saveNewPanelFailed(err.message));
            });
    }
}

export const resetClientTokenStart = createAction('resetClientToken/Start');
export const resetClientTokenSuccess = createAction('resetClientToken/Success');
export const resetClientTokenFailed = createAction('resetClientToken/Failed');

export const resetClientToken = (id) => {
    return (dispatch) => {
        dispatch(resetClientTokenStart());
        axiosInstance.post(`/api/iot-clients/${id}/reset_token/`)
            .then((res) => {
                dispatch(resetClientTokenSuccess(res.data));
            })
            .catch((err) => {
                dispatch(resetClientTokenFailed(err.message))
            })
    }
}

export const setPanelActiveStateStart = createAction('setPanelActiveState/Start');
export const setPanelActiveStateSuccess = createAction('setPanelActiveState/Success');
export const setPanelActiveStateFailed = createAction('setPanelActiveState/Failed');

export const setPanelActiveState = (iotClient, active) => {
    return (dispatch) => {
        dispatch(setPanelActiveStateStart());
        axiosInstance.patch(`/api/iot-clients/${iotClient.id}/active_state/`, { is_active: active })
            .then((res) => {
                dispatch(setPanelActiveStateSuccess(res.data));
            })
            .catch((err) => {
                dispatch(setPanelActiveStateFailed(err.message));
            });
    }
}

export const loadIotClientsStart = createAction('loadIotClients/start')
export const loadIotClientsSuccess = createAction('loadIotClients/Success')
export const loadIotClientsFailed = createAction('loadIotClients/Failed')

export const loadIotClients = () => {
    return(dispatch) => {
        dispatch(loadIotClientsStart())
        axiosInstance.get('/api/iot-clients/')
            .then(res => {
                dispatch(loadIotClientsSuccess(res.data))
            })
            .catch(err => {
                axiosCatchDefaults(err, dispatch)
                dispatch(loadIotClientsFailed())
            })
    }
}

export const getClientTokenStart = createAction('getClientToken/start')
export const getClientTokenSuccess = createAction('getClientToken/Success')
export const getClientTokenFailed = createAction('getClientToken/Failed')

export const getClientToken = (id) => {
    return(dispatch) => {
        dispatch(getClientTokenStart())
        axiosInstance.get(`/api/iot-clients/${id}/token_info/`)
            .then(res => {
                if (res.status === 200) {
                    dispatch(getClientTokenSuccess(res.data));
                } else {
                    dispatch(getClientTokenFailed(res.data));
                }
            })
            .catch(err => {
                console.log('failed get iot-clients...', err)
                dispatch(getClientTokenFailed())
            });
    }
}

// ------------------AGRISTAR2 ACTIONS------------------------------------
export const getAgristar2FrontMatterStart = createAction('getAgristar2FrontMatter/start')
export const getAgristar2FrontMatterSuccess = createAction('getAgristar2FrontMatter/Success')
export const getAgristar2FrontMatterFailed = createAction('getAgristar2FrontMatter/Failed')

export const getAgristar2FrontMatter = (iotclient, updateLoading) => {
    const payload = extractAgristar2PayloadFromIoTClient(iotclient);
    const IoTVersion = extractIoTClientVersion(payload);
    const is200plus = isVersionAtLeast(IoTVersion, 2, 0, 0);

    return (dispatch) => {
        dispatch(getAgristar2FrontMatterStart({id: iotclient.id}));
        axiosInstance.get(`/api/iot-clients/${iotclient.id}/front_matter`)
            .then((res) => {
                if (res.data.error_message) {
                    dispatch(getAgristar2FrontMatterFailed({data: res.data, id: iotclient.id}));
                    updateLoading?.();
                } else if (res.data.message_id) {
                    setTimeout(() => {
                        dispatch(checkMessageProcessed(iotclient.id, res.data.message_id));
                        updateLoading?.();
                    }, is200plus ? 18000 : 65000);
                } else {
                    dispatch(getAgristar2FrontMatterSuccess({data: res.data, id: iotclient.id}));
                    updateLoading?.();
                }
            })
            .catch((err) => {
                console.log('failed get agristar2 front matter...', err);
                dispatch(getAgristar2FrontMatterFailed());
                updateLoading?.();
            });
    }
}

export const getAgristar2DataStart = createAction('getAgristar2Data/start')
export const getAgristar2DataSuccess = createAction('getAgristar2Data/Success')
export const getAgristar2DataFailed = createAction('getAgristar2Data/Failed')

export const getAgristar2Data = (iotclient) => {
    const payload = extractAgristar2PayloadFromIoTClient(iotclient);
    const IoTVersion = extractIoTClientVersion(payload);
    const is200plus = isVersionAtLeast(IoTVersion, 2, 0, 0);

    return(dispatch) => {
        dispatch(getAgristar2DataStart({id: iotclient.id}))
        axiosInstance.get(`/api/iot-clients/${iotclient.id}/agristar2_data/`)
            .then(res => {
                if (res.data.error_message) {
                    dispatch(getAgristar2DataFailed({data: res.data, id: iotclient.id}))
                } else if (res.data.message_id) {
                    // wait 70 sec and check for message processed
                    setTimeout(() => { dispatch(checkMessageProcessed(iotclient.id, res.data.message_id))}, is200plus ? 18000 : 65000)
                } else {
                    dispatch(getAgristar2DataSuccess({data: res.data, id: iotclient.id}))
                }
            })
            .catch(err => {
                console.log('failed get agristar2 data...', err)
                // axiosCatchDefaults(err, dispatch)
                dispatch(getAgristar2DataFailed())
            })
    }
}

const checkMessageProcessed = (iotclient_id, message_id) => {
    return (dispatch) => {
        axiosInstance.get(`/api/iot-clients/${iotclient_id}/check_message/`,
        { params: { message_id } })
            .then((res) => {
                if (res.data.error_message) {
                    dispatch(getAgristar2DataFailed({ data: res.data, id: iotclient_id }))
                } else {
                    dispatch(getAgristar2DataSuccess({ data: res.data, id: iotclient_id }))
                }
            })
            .catch((err) => {
                dispatch(getAgristar2DataFailed())
            })
    }
}

export const postAgristar2ActionStart = createAction('postAgristar2Action/start');
export const postAgristar2ActionSuccess = createAction('postAgristar2Action/Success');
export const postAgristar2ActionFailed = createAction('postAgristar2Action/Failed');
export const postAgristar2ActionValidation = createAction('postAgristar2Action/Validation');

export const postAgristar2Action = (iotclient, form) => {
    const id = iotclient.id;
    const is200plus = isVersionAtLeast(iotclient.front_matter.misc[2], 2, 0, 0);

    return(dispatch) => {
        dispatch(postAgristar2ActionStart({tag: form.tag}))
        axiosInstance.post(`/api/iot-clients/${id}/agristar2_action/`, form)
            .then(res => {
                if (res.data.error_message) {
                    dispatch(postAgristar2ActionFailed({data: res.data, id, tag: form.tag}))
                } else if (res.data.message_id) {
                    // wait and check for message processed
                    setTimeout(() => { dispatch(checkPostProcessed(id, res.data.message_id, form.tag))}, is200plus ? 18000 : 65000)
                } else if (res.data.Type === 'Validation') {
                    const errors = {};
                    const keys = Object.keys(res.data.errors);
                    keys.forEach((key) => errors[key] = res.data.errors[key][0]?.substring(res.data.errors[key][0].indexOf(':') + 1));
                    dispatch(postAgristar2ActionValidation({data: errors, id, tag: form.tag}));
                } else {
                    dispatch(postAgristar2ActionSuccess({data: res.data, id, tag: form.tag}))
                }
            })
            .catch(err => {
                console.log('as2 action error', err)
                axiosCatchDefaults(err, dispatch, agristar2PostActionErrorHTTP503.id)
                const keys = Object.keys(err.response.data);
                const errors = { };
                keys.forEach(
                    (key) => errors[key] =
                        err.response.data[key][0]?.substring(err.response.data[key][0].indexOf(':') + 1)
                );
                dispatch(postAgristar2ActionFailed({tag: form.tag, id, data: errors }))
            })
    }
}

const checkPostProcessed = (iotclient_id, message_id, tag) => {
    return (dispatch) => {
        axiosInstance.get(`/api/iot-clients/${iotclient_id}/check_message/`,
        { params: { message_id } })
            .then((res) => {
                if (res.data.Type === 'Validation') {
                    const errors = {};
                    const keys = Object.keys(res.data.errors);
                    keys.forEach((key) => errors[key] = res.data.errors[key][0]?.substring(res.data.errors[key][0].indexOf(':') + 1));
                    dispatch(postAgristar2ActionValidation({data: errors, id: iotclient_id, tag }));
                } else if (res.data.error_message) {
                    dispatch(postAgristar2ActionFailed({ data: res.data, id: iotclient_id, tag }))
                } else {
                    dispatch(postAgristar2ActionSuccess({ data: res.data, id: iotclient_id, tag }))
                }
            })
            .catch((err) => {
                dispatch(postAgristar2ActionFailed({ data: err.message, id: iotclient_id, tag }))
            })
    }
}

export const postUpgradeActionStart = createAction('postUpgradeAction/start');
export const postUpgradeActionSuccess = createAction('postUpgradeAction/Success');
export const postUpgradeActionFailed = createAction('postUpgradeAction/Failed');

export const postUpgradeAction = (iotclient, upgrade) => {
    const id = iotclient.id;
    const payload = extractAgristar2PayloadFromIoTClient(iotclient);
    const IoTVersion = extractIoTClientVersion(payload);
    const is200plus = isVersionAtLeast(IoTVersion, 2, 0, 0);

    return (dispatch) => {
        dispatch(postUpgradeActionStart({id}))
        axiosInstance.post(`/api/upgrades/${id}/upgrade_action/`,
            { upgrade }
        )
            .then((res) => {
                if (res.data.error_message) {
                    dispatch(postUpgradeActionFailed({data: res.data, id}))
                } else if (res.data.message_id) {
                    // wait 70 sec and check for message processed
                    setTimeout(() => { dispatch(checkUpgradeProcessed(id, res.data.message_id))}, is200plus ? 18000 : 65000)
                } else {
                    dispatch(postUpgradeActionSuccess({data: res.data, id}))
                }
            })
            .catch((err) => {
                console.log('Upgrade Action Error', err.message);
                axiosCatchDefaults(err, dispatch, agristar2PostActionErrorHTTP503.id);
                dispatch(postUpgradeActionFailed());
            });
    };
}

const checkUpgradeProcessed = (iotclient_id, message_id) => {
    return (dispatch) => {
        axiosInstance.get(`/api/iot-clients/${iotclient_id}/check_message/`,
        { params: { message_id } })
            .then((res) => {
                if (res.data.error_message) {
                    dispatch(postUpgradeActionFailed({ data: res.data, id: iotclient_id }))
                } else {
                    dispatch(postUpgradeActionSuccess({ data: res.data, id: iotclient_id }))
                }
            })
            .catch((err) => {
                dispatch(postUpgradeActionFailed({ data: err.message, id: iotclient_id }))
            })
    }
}

export const resetSavingRefreshAction = createAction('resetSavingRefreshAction');
export const resetSavingRefresh = () => {
    return (dispatch) => {
        dispatch(resetSavingRefreshAction());
    }
}

// -----------PREMADE PAYLOADS FOR POST AGRISTAR2 ACTIONS---------------
export const agristar2PayloadClearAlarm = () => {
    return {"tag":"ClearAlarm"}
}

export const agristar2RemoteStart = (iotclient) => {
    return(dispatch) => {
        dispatch(postAgristar2Action(iotclient, {"tag":"RemoteStop","remoteStop":"Start"}))
    }
}
export const agristar2RemoteStop = (iotclient) => {
    return(dispatch) => {
        dispatch(postAgristar2Action(iotclient, {"tag":"RemoteStop","remoteStop":"Stop"}))
    }
}

export const agristar2BaylightToggle = (iotclient, bayNum) => {
    return(dispatch) => {
        dispatch(postAgristar2Action(iotclient, {"tag":`lights${bayNum}Btn`}))
    }
}
