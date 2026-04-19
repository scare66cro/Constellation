import functools
import os
import requests
from .azure_iot_hub import CloudToDevice, cloud_message_to_device
from .models.http_message_queue import HttpMessageQueue
from rest_framework import status
from rest_framework.exceptions import NotFound, PermissionDenied

cust_responses = {
    400: {'data': {'detail':'Cannot understand the request, verify only the required fields are present.'}, 'status':status.HTTP_400_BAD_REQUEST},
    403: {'data': {'detail':'User not authorized to perform action.'}, 'status':status.HTTP_403_FORBIDDEN},
    404: {'data': {'detail':'Agristar2 not found.'}, 'status': status.HTTP_404_NOT_FOUND},
    503: {'data': {'detail':'Connection with iot-client failed.'}, 'status': status.HTTP_503_SERVICE_UNAVAILABLE}
}


def _push_to_bridge(iot_client, method_name, payload):
    """
    Synchronously push a command to the bridge for a 'bridge-direct' device.

    The bridge executes the command on the controller, flushes fresh state
    back to Django, then returns. After the bridge call we re-read
    iot_client.last_log so the response carries the new controller state.

    Returns a response-shaped object (with .status and .payload) on success,
    or None if the bridge is unreachable / not configured / errors out — in
    which case the caller should fall back to the queued HttpMessageQueue path.
    """
    bridge_url = os.environ.get('BRIDGE_URL', 'http://localhost:9001').rstrip('/')
    token = iot_client.token
    try:
        resp = requests.post(
            f'{bridge_url}/api/bridge/command',
            json={'method': method_name, 'payload': payload},
            headers={'Authorization': f'Bearer {token}'},
            timeout=10,
        )
    except requests.RequestException as exc:
        print(f'[bridge push] request failed: {exc}')
        return None

    if resp.status_code != 200:
        print(f'[bridge push] non-200 from bridge: {resp.status_code} {resp.text[:200]}')
        return None

    try:
        body = resp.json()
    except ValueError:
        body = {}
    if not body.get('ok'):
        print(f'[bridge push] bridge reported failure: {body}')
        return None

    # Bridge has flushed fresh state to Django; reload the row so we return
    # the post-command snapshot to the PWA.
    iot_client.refresh_from_db()
    last = iot_client.last_log['payload'] if iot_client.last_log and 'payload' in iot_client.last_log else None

    response = lambda: None
    response.status = status.HTTP_200_OK
    response.payload = last if last is not None else {'id': iot_client.id, 'result': body.get('result', 'ok')}
    return response



def process_upgrade_action(request, iot_client):
    payload = request.data
    protocol = iot_client.last_log['payload']['Protocol'] if 'Protocol' in iot_client.last_log['payload'] else None

    method_name = 'upgrade'

    if iot_client.last_log is not None:
        if 'IoTClientVersion' in iot_client.last_log['payload']:
            iot_client.last_log['payload']['IoTClientVersion'] = payload['upgrade']
            iot_client.save()

    if protocol is None or protocol != 'http':
        # send payload to device and get a response
        device_response = CloudToDevice(iot_client.id, method_name, payload)
        # print(device_response)
    elif protocol is not None and protocol == 'http':
        device_response = cloud_message_to_device(iot_client, method_name, payload)

    return device_response

# MAIN - call this from a POST View method
def process_agristar2_action(request, iot_client):
    # pylint: disable=no-member
    # This method controls response cycle for posting changes to an Agristar2
    payload = request.data

    # METHOD OVERVIEW
    #0 Verify device is an agristar2
    #1 Authorize user for action
    #2 validate payload integrity
    #3 get_action_context - device's iot hub ID, method name
    #4 send to device and get a response
    #5 hand a packaged response back to the View

    # device_id = 'GellertTest'
    # method_name = 'settings'
    # payload = {
    #     "tag": "p1Plenum",
    #     "PlenumTempSet": 68.5,
    #     "PlenumHumidSet": 96 
    # }

    #0 Verify device is an agristar
    if is_agristar(iot_client) is not True:
        return cust_responses[404]

    #1 Authorize user for action
    # ??? do i need this or should I just check it in the view
    # if authorize_user_action(True) is not True:
    #     return cust_responses[403]
    if authorize_agristar_action(request, iot_client) == False:
        raise PermissionDenied
    
    #2 validate payload integrity
    if validate_user_action_payload(payload) is not True:
        return cust_responses[400]

    #3 get_action_context - device's iot hub ID, method name
    action_context = get_action_context(iot_client, payload['tag'])
    method_name = action_context['method_name']
    iot_hub_id = action_context['iot_hub_id']
    protocol = iot_client.last_log['payload']['Protocol'] if iot_client.last_log is not None and 'Protocol' in iot_client.last_log['payload'] else None

    # Use HttpMessageQueue for http and bridge-direct protocols (skip Azure IoT Hub)
    if protocol is None or protocol not in ['http', 'bridge-direct']:
        #4 send payload to device and get a response
        device_response = CloudToDevice(iot_hub_id, method_name, payload)
        # print(device_response)
    if protocol == 'bridge-direct':
        # Try a synchronous push to the bridge first. The bridge will execute on
        # the controller and flush its state back to Django before returning, so
        # the response we hand back to the PWA already reflects the new state.
        # On any failure, fall through to the polled HttpMessageQueue path so
        # production behavior (resilience to bridge restarts) is preserved.
        bridge_response = _push_to_bridge(iot_client, method_name, payload)
        if bridge_response is not None:
            return prepare_agristar2_response(bridge_response)
    if protocol in ['http', 'bridge-direct'] or (
        device_response is not None and hasattr(device_response, "payload") and \
        "error_message" in device_response.payload
    ):
        device_response = cloud_message_to_device(iot_client, method_name, payload)

    #5 process and package the response and return it to the View method
    response_for_view = prepare_agristar2_response(device_response)
    # print('...as2 response for view:',response_for_view)

    return response_for_view

# OTHER MAIN - call this from a GET View method
def get_live_agristar2_data_and_settings(iot_client):
    # This method grabs AS2 data and settings to provide a fresh view for the frontend
    if is_agristar(iot_client) is not True:
        return cust_responses[404]

    action_context = get_action_context(iot_client, 'GetState')
    method_name = action_context['method_name']
    iot_hub_id = action_context['iot_hub_id']
    protocol = iot_client.last_log['payload']['Protocol'] if iot_client.last_log is not None and 'Protocol' in iot_client.last_log['payload'] else None

    # bridge-direct clients: the Constellation bridge pushes fresh data continuously
    # via POST /api/bridge/sync/. There is no device to ping; just return the cached
    # last_log payload so the UI reflects whatever the bridge last sent.
    if protocol == 'bridge-direct':
        if iot_client.last_log is None:
            return cust_responses[503]
        agristar2_response = lambda: None
        agristar2_response.status = status.HTTP_200_OK
        agristar2_response.payload = iot_client.last_log['payload']
        return prepare_agristar2_response(agristar2_response)

    # Use HttpMessageQueue for http and bridge-direct protocols (skip Azure IoT Hub)
    if protocol is None or protocol not in ['http', 'bridge-direct']:
        # retrieve data or return error code
        agristar2_response = CloudToDevice(iot_hub_id, method_name, {"tag":"GetState"})
    if protocol in ['http', 'bridge-direct'] or (
        agristar2_response is not None and hasattr(agristar2_response, "payload") and \
        "error_message" in agristar2_response.payload
    ):
        agristar2_response = cloud_message_to_device(iot_client, method_name, {"tag":"GetState"})

    return prepare_agristar2_response(agristar2_response)

def check_message_processed(iot_client, message_id):
    if is_agristar(iot_client) is not True:
        return cust_responses[404]

    http_message = HttpMessageQueue.objects.get(id=message_id)
    agristar2_response = lambda: None
    agristar2_response.status = status.HTTP_200_OK

    if http_message.is_processed:
        if http_message.validation is not None:
            agristar2_response.payload = { "Type": 'Validation', "errors": http_message.validation }
        else:
            agristar2_response.payload = iot_client.last_log['payload']
    else:
        agristar2_response.payload = { "id": iot_client.id, "error_message": "Action not processed" }
        # mark message as processed so we don't get a backlog once connection is restarted
        http_message.is_processed = True
        http_message.save()

    return prepare_agristar2_response(agristar2_response)

def get_agristar2_front_matter(iot_client):
    if is_agristar(iot_client) is not True:
        return cust_responses[404]
    
    # TODO once we get a new iotclient we will add an explicit GetFrontMatter action
    action_context = get_action_context(iot_client, 'GetState')
    method_name = action_context['method_name']
    iot_hub_id = action_context['iot_hub_id']
    protocol = iot_client.last_log['payload']['Protocol'] if iot_client.last_log is not None and 'Protocol' in iot_client.last_log['payload'] else None
    
    agristar2_response = None
    if protocol is None or protocol != 'http':
        agristar2_response = CloudToDevice(iot_hub_id, method_name, {"tag": "GetState"})
    if protocol == 'http' or (
        agristar2_response is not None and hasattr(agristar2_response, "payload") and \
        "error_message" in agristar2_response.payload
    ):
        agristar2_response = cloud_message_to_device(iot_client, method_name, {"tag": "GetState"})
    
    response = prepare_agristar2_response(agristar2_response)
    if "error_message" not in response['data'] and "message_id" not in response['data']:
        data = get_front_matter(response['data'])
        response['data'] = data
    return response

def is_agristar(iot_client):
    # pylint: disable=no-member
    agristar_types = ['agristar1','agristar2','nova']
    return True if iot_client.client_type in agristar_types else False

def is_agristar1(iot_client):
    if iot_client is None or iot_client.client_type != 'agristar1':
        return False
    return True

def is_agristar2(iot_client):
    if iot_client is None or iot_client.client_type != 'agristar2':
        return False
    return True

def authorize_agristar_action(request, agristar):
    try:
        tag = request.data.get('tag')
        required_auth_level = action_contexts[tag]['authorization_level']
        # check object permissions and action_contexts.auth_level
        if required_auth_level == 1:
            return agristar.has_object_agristar2_action_level1_permission(request)
        if required_auth_level == 2:
            return agristar.has_object_agristar2_action_level2_permission(request)
    except:
        raise NotFound

def validate_user_action_payload(payload):
    # this recieves the POST request and returns all 
    # ...the context necessary to execute the agristar2 action. auth_level, method_name, iot_hub_id
    try: # payload may not be formed properly
        tag = payload['tag']
        if tag is None:
            return False

        context = action_contexts[tag]
        if context is None:
            # print('...no action matching that tag name')
            return False

        # DEPRECATED rest of function....
        # ...do not need to verify keys because some forms are dynamic
        # ...and do not send all fields in every post

        # Python deletes duplicate dictionary keys using the last declared instance
        # verify all required fields are in payload
        # for x in context['required_fields']:
        #     if payload[x] is None:
        #         # print('...missing required field')
        #         return False
        # verify there are no extra  payloads
        # for key in payload:
        #     # print('...extra field', payload[key])
        #     if key not in context['required_fields']:
        #         return False

        return True
    except:
        return False

def prepare_agristar2_response(as2_response):

    def select_response(res_status, as2_payload):
        switch = {
            # 200 action successful
            200: {'data': as2_payload, 'status':status.HTTP_200_OK},
            # 400 error data not valid
            400: {'data': as2_payload, 'status':status.HTTP_400_BAD_REQUEST},
            # 500 internal agristar error
            500: {'data': as2_payload, 'status':status.HTTP_500_INTERNAL_SERVER_ERROR},
            # 503 submission to LightTPD failed 'Not Saved' 
            # -- can also indicate AS2 client was not yet finished loading its Data state
            503: {'data': as2_payload, 'status':status.HTTP_503_SERVICE_UNAVAILABLE}
        }
        # print('constructed response', switch[res_status])
        return switch[res_status]

    if as2_response is None or as2_response.status is None:
        # iot_hub response failed
        response = cust_responses[503]
        return response
    else:
        # iot_hub response succeeded
        return select_response(as2_response.status, as2_response.payload)

def get_front_matter(payload):
    pile_avg = 0
    cure_start_temp = None
    cure_start_humid = None
    main_length = len(payload["MainData"])
    purge_mode = payload["Co2PurgeData"][0]
    co2_set_point = payload["Co2PurgeData"][7] if len(payload["Co2PurgeData"]) > 7 and purge_mode == "2" else "1200"

    if "Sensors" in payload.keys() and len(payload["Sensors"]) > 8:
        valid = [payload["Sensors"][i:i + 4] for i in range(0, len(payload["Sensors"]), 4)]
        valid = filter(lambda item: item[3] != '--' and item[2] == '9', valid)
        valid = list(valid)
        if len(valid) > 1:
            pile_avg = functools.reduce(lambda prev, current: float(prev) + float(current[-1]), valid, 0) / len(valid)
    elif "PileTempsData" in payload.keys() and len(payload["PileTempsData"]) > 0:
        valid = filter(lambda item: item != '--', payload["PileTempsData"][::2])
        valid = list(valid)
        if len(valid) > 1:
            pile_avg = functools.reduce(lambda prev, current: float(prev) + float(current), valid) / len(valid)

    if "AirCureData" in payload.keys():
        cure_start_temp = payload["AirCureData"][0] # 11 - CureStartTemp
        cure_start_humid = payload["AirCureData"][2] # 12 - CureStartHumid

    data = {
        "main": [
            payload["CurrentMode"][0], # 0 - currentMode
            payload["P2BasicSetupData"][1], # 1 - TempType
            payload["MainData"][0], # 2 - plenumTemp
            payload["PgmData"][0], # 3 - plenumTempSet
            payload["PgmData"][5] if len(payload["PgmData"]) > 5 else None, # 4 - plenumTempSet2
            payload["MainData"][1], # 5 - plenumHumid
            payload["PgmData"][1], # 6 - plenumHumidSet
            payload["MainData"][2], # 7 - outsideTemp
            payload["MainData"][4], # 8 - outsideHumid
            payload["MainData"][8], # 9 - returnTemp
            payload["MainData"][7], # 10 - returnHumid
            cure_start_temp, # 11 - CureStartTemp
            cure_start_humid, # 12 - CureStartHumid
            pile_avg, # 13 - PileTempAvg
            payload["MainData"][10], # 14 - fanSpeed
            payload["MainData"][11], # 15 - Output
            payload["MainData"][12], # 16 - Mode
            payload["MainData"][9], # 17 - co2Level
            payload["MainData"][13], # 18 - BurnerOutput
            payload["EquipStatusData"][56], # 19 - BayLight1
            payload["LoadMonitorData"][0] if "LoadMonitorData" in payload.keys() else None, # 20 - BayLight1Name
            payload["EquipStatusData"][58], # 21 - BayLight2
            payload["LoadMonitorData"][1] if "LoadMonitorData" in payload.keys() else None, # 22 - BayLight2Name
            payload["P2BasicSetupData"][4], # 23 - SystemMode (Potato, Onion, etc)
            payload["EquipStatusData"][8], # 24 - Cure Output
            payload["EquipStatusData"][52], # 25 - Cure Remote Off
            payload["PlenTempDevData"][4], # 26 - Cure Temp Low
            payload["PlenTempDevData"][5], # 27 - Cure Temp High
            payload["AirCureData"][3] if "AirCureData" in payload else None, # 28 - Cure Humid High Limit
            payload["PgmData"][2], # 29 - Plenum Humid Reference
            payload["AirCureData"][1] if "AirCureData" in payload else None, # 30 - Cure Humid Reference
            payload["MainData"][14], # 31 - Calc Humid
            payload["Co2PurgeData"][0], # 32 - CO2 Purge Mode
            co2_set_point, # 33 - CO2 Set Point
            payload["MainData"][17] if main_length > 17 else None, # 34 - Refrigeration Output
            payload["MainData"][19] if main_length > 19 else None, # 35 - Return Humidity 2
            payload["MainData"][20] if main_length > 20 else None, # 36 - CO2 2
            payload["MainData"][21] if main_length > 21 else None, # 37 - Return Temp 2
            payload["MainData"][22] if main_length > 22 else None, # 38 - Moisture Loss Index 1
            payload["MainData"][23] if main_length > 23 else None, # 39 - Moisture Loss Index 2
        ],
        "misc": [
            payload["BoardType"][0], # 0 - BoardType
            payload["P2BasicSetupData"][0], # 1 - panelName
            payload["IoTClientVersion"], # 2 - IoTClientVersion
            payload["SysVersions"][0], # 3 - ControllerVersion
        ],
        "AlarmData": payload["AlarmData"]
    }

    return data

def get_realtime(payload):
    return {
        "Type": "Realtime2",
        "AlarmData": payload["AlarmData"],
        "MainData": payload["MainData"],
        "EquipStatusData": payload["EquipStatusData"],
        "CurrentMode": payload["CurrentMode"],
        "DailyFanRun": payload["DailyFanRun"],
        "TotalFanRun": payload["TotalFanRun"],
        "DateTimeData": payload["DateTimeData"],
        "PgmData": payload["PgmData"],
        "PileTempsData": payload["PileTempsData"] if "PileTempsData" in payload.keys() and len(payload["PileTempsData"]) > 1 else None,
        "PileHumidsData": payload["PileHumidsData"] if "PileHumidsData" in payload.keys() and len(payload["PileHumidsData"]) > 1 else None,
    }

def get_settings(payload):
    return {
        "IoTClientVersion": payload["IoTClientVersion"],
        "Protocol": payload["Protocol"],
        "BoardType": payload["BoardType"],
        "OutsideAirData": payload["OutsideAirData"],
        "AirCureData": payload["AirCureData"] if "AirCureData" in payload.keys() else None,
        "FreqCtrlData": payload["FreqCtrlData"],
        "RampRateData": payload["RampRateData"],
        "HumidCtrlData": payload["HumidCtrlData"],
        "Co2PurgeData": payload["Co2PurgeData"],
        "MiscData": payload["MiscData"],
        "PlenTempDevData": payload["PlenTempDevData"],
        "HumidModes": payload["HumidModes"],
        "PileTempsLabels": payload["PileTempsLabels"],
        "PileHumidsLabels": payload["PileHumidsLabels"],
        "AvailableIoData": payload["AvailableIoData"],
        "ClimacellTimesData": payload["ClimacellTimesData"],
        "LtxVersion": payload["LtxVersion"],
        "PgmData": payload["PgmData"],
        "P2BasicSetupData": payload["P2BasicSetupData"],
        "ControllerList": payload["ControllerList"] if "ControllerList" in payload.keys() else None,
        "DisplayList": payload["DisplayList"] if "DisplayList" in payload.keys() else None,
        "P2AnalogBoardData": payload["P2AnalogBoardData"] if "P2AnalogBoardData" in payload.keys() else None,
        "P2FreshAirData": payload["P2FreshAirData"],
        "P2RefrigerationData": payload["P2RefrigerationData"],
        "P2ClimacellData": payload["P2ClimacellData"],
        "P2FailuresData": payload["P2FailuresData"],
        "P2Failures2Data": payload["P2Failures2Data"],
        "P2ServiceData": payload["P2ServiceData"],
        "P2BurnerData": payload["P2BurnerData"],
        "UserAccounts": payload["UserAccounts"],
        "AlertSetupData": payload["AlertSetupData"],
        "P2PwmChannelData": payload["P2PwmChannelData"],
        "OutputConfigData": payload["OutputConfigData"],
        "InputConfigData": payload["InputConfigData"],
        "AuxProgramData": payload["AuxProgramData"],
        "AuxSwitchesData": payload["AuxSwitchesData"],
        "SysVersion": payload["SysVersions"],
        "EmailAlertData": payload["EmailAlertData"],
        "IoNames": payload["IoNames"] if "IoNames" in payload.keys() else None,
        "UserLogSettings": payload["UserLogSettings"],
    }

def get_action_context(iot_client, tag):
    # pylint: disable=no-member
    # recieves the users payload and gets the correct action context from the dictionary action_contexts
    result = {
        'method_name':action_contexts[tag]['method_name'],
        'iot_hub_id': iot_client.id
    }
    return result

action_contexts = {
    # -----------------------LEVEL 2------------------
    'ShowPassword':{
        'authorization_level':2,
        'method_name':'action',
        'required_fields':[
            'tag',
        ]
    },
    # this payload is capable of sending any settings change or action completely unvalidated
    'unsecurePost':{
        'authorization_level':2,
        'method_name':'settings',
        'required_fields':[
            'tag', # 'unsecurePost'
            'pageName', # ex: 'p1Plenum'
            'postType', # postSave, postButton
            'postCommand', # 'none' or 'PostGellert.jsp' or 'PostAlertSetup.jsp' etc...
            'postFormString' # form data in string format 
            # ....(ex: maxFanSpeed=100&minFanSpeed=30&refrFanSpeed=76&recircFanSpeed=50&updFanSpeed=6&tempDiff=7.0&selDiff1=1&selDiff2=255)
        ]
    },
    # -----------------------LEVEL 0------------------
    'GetState':{
        'authorization_level':0,
        'method_name':'action',
        'required_fields':[
            'tag',
        ],
    },
    # -----------------------LEVEL 1------------------
    # actions---------------
    'RemoteStop':{
        'authorization_level':1,
        'method_name':'action',
        'required_fields':[
            'tag',
            'remoteStop',
        ],
    },
    'DailyFanRuntime':{
        'authorization_level':1,
        'method_name':'action',
        'required_fields':[
            'tag',
        ],
    },
    'TotalFanRuntime':{  
        'authorization_level':1,
        'method_name':'action',
        'required_fields':[
            'tag',
        ],
    },
    'ClearAlarm':{
        'authorization_level':1,
        'method_name':'action',
        'required_fields':[
            'tag',
        ],
    },
    'ClearDiag':{
        'authorization_level':1,
        'method_name':'action',
        'required_fields':[
            'tag',
        ],
    },
    'EquipCtrlAction':{
        'authorization_level':1,
        'method_name':'action',
        'required_fields':[
            'tag',
            'action',
            'value',
        ],
    },
    'lights1Btn':{
        'authorization_level':1,
        'method_name':'action',
        'required_fields':[
            'tag',
        ],
    },
    'lights2Btn':{
        'authorization_level':1,
        'method_name':'action',
        'required_fields':[
            'tag',
        ],
    },
    'TestEmail':{
        'authorization_level':1,
        'method_name':'action',
        'required_fields':[
            'tag',
        ],
    },
    # settings--------------
    'p1Plenum':{
        'authorization_level':1,
        'method_name':'settings',
        'required_fields':[
            'tag',
            'PlenumTempSet',
            'PlenumHumidSet'
        ],
    },
    'p1PlenTempDev':{
        'authorization_level':1,
        'method_name':'settings',
        'required_fields':[
            'tag',
            'AlarmTempLow',
            'AlarmMinLow',
            'AlarmTempHigh',
            'AlarmMinHigh'
        ],
    },
    'p1RampRate': {
        'authorization_level': 1,
        'method_name': 'settings',
        'required_fields': [
            'tag',
            'updTemp',
            'rampUpdateHours',
            'rampAutomatic',
            'rampTempDiff',
            'selTemp',
            'targetTemp'
        ]
    },
    'p1Co2Purge':{
        'authorization_level':1,
        'method_name':'settings',
        'required_fields':[
            'tag',
            'selPurgeMode',
            'PurgeHours',
            'co2SetPoint',
            'minTemp',
            'maxTemp',
            'time',
            'fanOutput',
            'doorOutput',
        ],
    },
    'p1OutsideAir':{
        'authorization_level':1,
        'method_name':'settings',
        'required_fields':[
            'tag',
            'ctrlMode',
            'OutsideAirSet',
            'selAboveBelow',
            'selTempRef'
        ],
    },
    'p1RunTimes':{
        'authorization_level':1,
        'method_name':'settings',
        'required_fields':[
            'tag',
            'runTimes'
        ],
    },
    'p1SetFanSpeed':{
        'authorization_level':1,
        'method_name':'settings',
        'required_fields':[
            'tag',
            'setFanSpeed'
        ]
    },
    'p1FreqCtrl':{
        'authorization_level':1,
        'method_name':'settings',
        'required_fields':[
            'tag',
            "maxFanSpeed",
            "minFanSpeed",
            "refrFanSpeed",
            "recircFanSpeed",
            "updFanSpeed",
            "tempDiff",
            "selDiff1",
            "selDiff2"
        ],
    },
    'p1HumidCtrl':{
        'authorization_level':1,
        'method_name':'settings',
        'required_fields':[
            'tag',
            'selHumidType',
            'selHumidMode',
            'coolOn',
            'coolOff',
            'recircOn',
            'recircOff',
            'refrigOn',
            'refrigOff'
        ]
    },
    'p1ClimacellTimes':{
        'authorization_level':1,
        'method_name':'settings',
        'required_fields':[
            'tag',
            'climacellTimes'
        ]
    },
    'p1FanBoost':{
        'authorization_level':1,
        'method_name':'settings',
        'required_fields':[
            'tag',
            'selBoostMode',
            'speed',
            'time',
            'temp',
            'hours',
        ]
    },
    'p1BaylightNames':{
        'authorization_level':1,
        'method_name':'settings',
        'required_fields':[
            'tag',
        ]
    },
    'p1Misc':{
        'authorization_level':1,
        'method_name':'settings',
        'required_fields':[
            'tag',
            'selRefrMode',
            'defrostInterval',
            'defrostTime',
            'tempThresh',
            'selCtrlMode',
            'selCavityCtrl',
            'cavityDiff',
            'selCavityCtrlSensor',
            'cavityDutyCycle',
            'kbPref',
        ]
    },
    'p1Comm':{
        'authorization_level':1,
        'method_name':'settings',
        'required_fields':[
            'tag',
        ]
    },
    'p1CommDisplay': {
        'authorization_level': 1,
        'method_name':'action',
        'required_fields':[
            'tag'
        ],
    },
    'p1AlertFrame': {
        'authorization_level': 1,
        'method_name': 'settings',
        'required_fields': [
            'tag',
        ],
    },
    'p1DateTime': {
        'authorization_level': 1,
        'method_name': 'settings',
        'required_fields': [
            'tag',
            'Date',
            'Time',
            'TimeType',
        ],
    },
    'p2BasicSetup':{
        'authorization_level':2,
        'method_name':'settings',
        'required_fields':[
            'tag',
        ]
    },
    'p2Burner':{
        'authorization_level': 2,
        'method_name': 'settings',
        'required_fields': [
            'tag',
            'Altitude',
            'AltType',
        ],
    },
    'p2FreshAirSetup': {
        'authorization_level': 2,
        'method_name': 'settings',
        'required_fields': [
            'tag',
            'PAirValue',
            'IAirValue',
            'DAirValue',
            'UAirValue',
            'ActuatorTimes',
            'CoolAirCycle',
        ]
    },
    'p2Climacell': {
        'authorization_level': 2,
        'method_name': 'settings',
        'required_fields': [
            'tag',
            'ClimacellEff',
            'Altitude',
            'AltType',
            'PClimacellValue',
            'IClimacellValue',
            'DClimacellValue',
            'UClimacellValue',
        ]
    },
    'p2AnalogBoardSetup': {
        'authorization_level': 2,
        'method_name': 'settings',
        'required_fields': [
            'tag',
            'BAdd', 'BTyp', 'BdLbl', 'BVer', 'BDis',
            'Sen1Typ', 'Sen1Lbl', 'Sen1Off', 'Sen1Dis',
            'Sen2Typ', 'Sen2Lbl', 'Sen2Off', 'Sen2Dis',
            'Sen3Typ', 'Sen3Lbl', 'Sen3Off', 'Sen3Dis',
            'Sen4Typ', 'Sen4Lbl', 'Sen4Off', 'Sen4Dis',
        ],
    },
    'p2IoConfigFrame': {
        'authorization_level': 2,
        'method_name': 'settings',
        'required_fields': [
            'tag',
            'p2IoConfig',
        ],
    },
    'p2FailuresSetup': {
        'authorization_level': 2,
        'method_name': 'settings',
        'required_fields': [
            'tag',
        ],
    },
    'p2FailuresSetupAS2': {
        'authorization_level': 2,
        'method_name': 'settings',
        'required_fields': [
            'tag',
        ],
    },
    'p2FailuresSetup2': {
        'authorization_level': 2,
        'method_name': 'settings',
        'required_fields': [
            'tag',
        ],
    },
    'p2LogSettings': {
        'authorization_level': 2,
        'method_name': 'settings',
        'required_fields': [
            'tag',
            'recInterval',
            'sdWrap',
        ],
    },
    'p2Refrigeration': {
        'authorization_level': 2,
        'method_name': 'settings',
        'required_fields': [
            'tag',
            'p2Refrigeration',
            'PRefrValue',
            'IRefrValue',
            'DRefrValue',
            'URefrValue',
            'RefrigerationPurge',
            'PurgeThreshold',
        ],
    },
    'p2RefrigerationAS2': {
        'authorization_level': 2,
        'method_name': 'settings',
        'required_fields': [
            'tag',
            'p2Refrigeration',
            'PRefrValue',
            'IRefrValue',
            'DRefrValue',
            'URefrValue',
            'RefrigerationPurge',
            'PurgeThreshold',
        ],
    },
    'p2PwmFrame': {
        'authorization_level': 2,
        'method_name': 'settings',
        'required_fields': [
            'tag',
            'p2PwmOutputs',
        ],
    },
    'p2PwmFrameAS2': {
        'authorization_level': 2,
        'method_name': 'settings',
        'required_fields': [
            'tag',
            'p2PwmOutputs',
        ],
    },
    'p2AuxProgFrame': {
        'authorization_level': 2,
        'method_name': 'settings',
        'required_fields': [
            'tag', 'AuxProgram',
            'type1', 'io1', 'st1', 'op1', 'ref1', 'sen1', 'diff1',
            'andOr2', 'type2', 'io2', 'st2', 'op2', 'ref2', 'sen2', 'diff2',
            'andOr3', 'type3', 'io3', 'st3', 'op3', 'ref3', 'sen3', 'diff3',
            'andOr4', 'type4', 'io4', 'st4', 'op4', 'ref4', 'sen4', 'diff4',
            'andOr5', 'type5', 'io5', 'st5', 'op5', 'ref5', 'sen5', 'diff5',
            'andOr6', 'type6', 'io6', 'st6', 'op6', 'ref6', 'sen6', 'diff6',
            'dutyCycle', 'period', 'units',
        ],
    },
    'p2PIDLogs': {
        'authorization_level': 2,
        'method_name': 'settings',
        'required_fields': [
            'tag',
            'pidWrap',
        ],
    },
    'button2': {
        'authorization_level': 2,
        'method_name': 'action',
        'required_fields': [
            'tag',
        ],
    },
    'gellert': {
        'authorization_level': 1,
        'method_name': 'action',
        'required_fields': [
            'tag',
        ],
    },
}
