''' azure iot hub interactions '''
import json
from azure.iot.hub import IoTHubRegistryManager
from azure.iot.hub.models import CloudToDeviceMethod, ExportImportDevice
from msrest.exceptions import HttpOperationError
from rest_framework import status
from .models.http_message_queue import HttpMessageQueue

def get_connection_string_service():
    try:
        from gellert_project.secrets import iot_hub_connection_string
        if iot_hub_connection_string is None:
            raise ValueError('connection string is None')
        return iot_hub_connection_string
    except:
        import os
        return os.environ['CUSTOMCONNSTR_IOT_HUB_SERVICE_CONNECTION']

def get_connection_string_owner():
    try:
        from gellert_project.secrets import iot_hub_connection_string_hubowner
        if iot_hub_connection_string_hubowner is None:
            raise ValueError('connection string is None')
        return iot_hub_connection_string_hubowner
    except:
        import os
        return os.environ['CUSTOMCONNSTR_IOT_HUB_OWNER_CONNECTION']

def CloudToDevice(device_id, method_name, payload):
    # CONNECTION_STRING = get_connection_string_service()
    CONNECTION_STRING = get_connection_string_owner()
    registry_manager = IoTHubRegistryManager(CONNECTION_STRING)
    DEVICE_ID = device_id    
    METHOD_NAME = method_name
    METHOD_PAYLOAD = json.dumps(payload)
    deviceMethod = CloudToDeviceMethod(method_name=METHOD_NAME, payload=METHOD_PAYLOAD)
    deviceMethod.response_timeout_in_seconds = 20
    # print(CONNECTION_STRING, DEVICE_ID, METHOD_NAME, METHOD_PAYLOAD)
    # attempt call from cloud to device 
    try:
        print('starting...', payload)
        response = registry_manager.invoke_device_method(DEVICE_ID, deviceMethod)
        print('finished...')
        return response
    except HttpOperationError as error:
        agristar2_response = lambda: None
        agristar2_response.status = status.HTTP_200_OK
        agristar2_response.payload = { "id": device_id,  "error_message": error.message }
        return agristar2_response

def cloud_message_to_device(iot_client, method_name, payload):
    ''' Store a message into the message queue '''
    message = HttpMessageQueue(
        # pylint: disable=no-member
        iot_client = iot_client,
        method = method_name,
        payload = payload
    )
    try:
        print('saving...', payload)
        message.save()
        print('finished...')
        agristar2_response = lambda: None
        agristar2_response.status = status.HTTP_200_OK
        agristar2_response.payload = { "id": iot_client.id, "message_id": message.id }
        return agristar2_response
    # pylint: disable=broad-except
    except Exception:
        return None

def get_device_from_iot_hub(device_id):
    try:
        resp = IoTHubRegistryManager(get_connection_string_owner()).get_device(device_id)
        return resp
    except:
        return None

def device_exists_in_iot_hub(device_id):
    if get_device_from_iot_hub(device_id) is not None:
        return True
    return False

def register_new_devices(device_id_array):
    devices = []

    for device_id in device_id_array:
        devices.append(ExportImportDevice(id=device_id, import_mode='create'))
    
    connection_string = get_connection_string_owner()

    # try:
    resp = IoTHubRegistryManager(connection_string).bulk_create_or_update_devices(devices)
    # print('resp.....', resp.errors[0])
    if resp.is_successful is True:
        return resp
    return None
    # except:
        # return None

def delete_device(device_id):
    connection_string = get_connection_string_owner()
    try:
        resp = IoTHubRegistryManager(connection_string).delete_device(device_id)
        return resp
    except:
        return None