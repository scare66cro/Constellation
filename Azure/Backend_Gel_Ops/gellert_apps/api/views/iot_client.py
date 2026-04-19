''' IoTClient View Module '''
import json
from django.conf import settings
from rest_framework import viewsets, status
from rest_framework.decorators import action, api_view, permission_classes
from rest_framework.permissions import IsAuthenticated, AllowAny
from rest_framework.response import Response
from rest_framework.exceptions import PermissionDenied, AuthenticationFailed, NotFound
from dry_rest_permissions.generics import DRYPermissions
from django.views.decorators.csrf import csrf_exempt
from gellert_apps.api.models.Site import Site
# pylint: disable=import-error
from gellert_project.secrets import api_key_for_view_GetIoTHubConnectionString
from ..models import IoTClient, IoTLog
from ..serializers import IoTClientSerializer
from ..agristar2_actions import process_agristar2_action, get_live_agristar2_data_and_settings, \
    check_message_processed, get_agristar2_front_matter, get_front_matter, get_realtime, \
    get_settings
from ..azure_iot_hub import get_device_from_iot_hub
import time

class IoTClientViewSet(viewsets.ModelViewSet):
    '''IoTClient View'''
    permission_classes = (IsAuthenticated, DRYPermissions,)
    queryset = IoTClient.IoTClient.objects.all().order_by('name')
    filter_backends = (IoTClient.IoTClientFilterBackend,)
    serializer_class = IoTClientSerializer

    @action(detail=True, methods=['get'])
    def agristar2_data(self, request, pk=None):
        iot_client = IoTClient.IoTClient.objects.get(id=pk)
        if iot_client.has_object_read_permission(request) is False:
            raise PermissionDenied

        content = get_live_agristar2_data_and_settings(iot_client)
        return Response(**content)

    @action(detail=True, methods=['get'])
    def front_matter(self, request, pk=None):
        iot_client = IoTClient.IoTClient.objects.get(id=pk)
        if iot_client.has_object_read_permission(request) is False:
            raise PermissionDenied
        content = get_agristar2_front_matter(iot_client)
        return Response(**content)
    
    @action(detail=True, methods=['post'])
    def agristar2_action(self, request, pk=None):
        iot_client = IoTClient.IoTClient.objects.get(id=pk)
        content = process_agristar2_action(request, iot_client)
        return Response(**content)

    @action(detail=True, methods=['get'])
    def check_message(self, request, pk=None):
        iot_client = IoTClient.IoTClient.objects.get(id=pk)
        content = check_message_processed(iot_client, request.query_params["message_id"])
        return Response(**content)

    @action(detail=True, methods=['get'])
    def token_info(self, request, pk=None):
        try:
            iot_client = IoTClient.IoTClient.objects.get(id=pk)
            if iot_client:
                content = {
                    "token": iot_client.token,
                    "token_spent": iot_client.token_spent,
                }
                return Response({'data': content, 'status':status.HTTP_200_OK })
            else:
                return Response({'data': {}, 'status': status.HTTP_400_BAD_REQUEST})
        except IoTClient.IoTClient.DoesNotExist as err:
            return Response({'data': {}, 'status': status.HTTP_400_BAD_REQUEST})

    @action(detail=True, methods=['patch'])
    def active_state(self, request, pk=None):
        iot_client = IoTClient.IoTClient.objects.get(id=pk)
        iot_client.is_active = request.data["is_active"]
        iot_client.save(False)
        return Response({'data': {
            "id": iot_client.id,
            "is_active": iot_client.is_active
        }, 'status': status.HTTP_200_OK})

    @action(detail=True, methods=['post'])
    def reset_token(self, request, pk=None):
        iot_client = IoTClient.IoTClient.objects.get(id=pk)
        iot_client.token_spent = False
        iot_client.save(False)
        content = {
            "token": iot_client.token,
            "token_spent": iot_client.token_spent,
        }
        return Response({'data': content, 'status': status.HTTP_200_OK})

    # leave these methods, they are used by the dry-permissions framework to magically
    # ...return level1 permissions. Other than that, yes they are redundant of 'agristar2_action'.
    @action(detail=True, methods=['post'])
    def agristar2_action_level1(self, request, pk=None):
        iot_client = IoTClient.IoTClient.objects.get(id=pk)
        content = process_agristar2_action(request, iot_client)
        return Response(**content)

    @action(detail=True, methods=['post'])
    def agristar2_action_level2(self, request, pk=None):
        iot_client = IoTClient.IoTClient.objects.get(id=pk)
        content = process_agristar2_action(request, iot_client)
        return Response(**content)

    @action(detail=False, methods=['post'])
    def new_panel(self, request):
        if request.user.has_perm('api.change_iotclient'):
            panel = request.data.get('panel')
            new_panel = IoTClient.IoTClient(
                name = panel["name"],
                site = Site.objects.get(id=panel["site"]),
                unsecured_ip = panel["unsecured_ip"],
                client_type = panel["client_type"],
                is_active = panel["is_active"],
                id = panel["id"],
                token_spent = panel["token_spent"],
            )
            new_panel.save()
            return Response(IoTClientSerializer(new_panel, context={'request': request}).data, status=status.HTTP_200_OK)

        raise PermissionDenied

    def setPanel(self, panel, values):
        panel.name = values['name']
        panel.site = Site.objects.filter(id=values['site']).first()
        panel.unsecured_ip = values['unsecured_ip']
        panel.client_type = values['client_type']
        panel.is_active = values['is_active']
        panel.token_spent = values['token_spent']

# post an api_key and token -> recieve id, hub name, and devices shared_access_key
@api_view(['POST'])
@permission_classes([AllowAny])
@csrf_exempt # this is actually unnecessary because csrf_tokens are not required for anonymous posts
def GetIoTHubConnectionString(request):
    # In local development mode, IoT Hub is not available
    # Devices should use bridge-direct mode instead
    local_dev = getattr(settings, 'LOCAL_DEV', False)
    if local_dev:
        return Response(
            {
                'detail': 'IoT Hub not available in local development mode. Use bridge-direct sync instead.',
                'bridge_url': '/api/bridge/register/',
            },
            status=status.HTTP_503_SERVICE_UNAVAILABLE
        )
    
    # incoming payload:
        # {"api_key":"be63506f-d400-4015-bdb3-85d0c2310f05","token":"d4de57be0cdf"}
    # outgoing payload:
        # {
        #     "id": "1000290200-800-00805f9b34fb",
        #     "iot_hub": "IoTHubGellert",
        #     "shared_access_key": "feinvs4BQdpJJoaMlMdeRgNM9WqGQj+9Rmml3+E80="
        # }
    # print(request.data)
    api_key = api_key_for_view_GetIoTHubConnectionString
    request_key = request.data.get('api_key', None)

    if request_key is None or request_key != api_key or request.data.get('token', None) is None:
        return Response({'detail':'Not authorized'}, status=status.HTTP_401_UNAUTHORIZED)

    # pylint: disable=no-member
    device = IoTClient.IoTClient.objects.filter(token=request.data.get('token', None)).first()
    if device is None or device.token_spent is True:
        return Response(
            {'detail':'No matching resources found.'},
            status=status.HTTP_404_NOT_FOUND
        )

    try:
        key = get_device_from_iot_hub(device.id).authentication.symmetric_key.primary_key
        content = {
            "primary_connection_string": (
                'HostName=' + settings.DEFAULT_IOT_HUB
                + '.azure-devices.net;DeviceId=' + str(device.id)
                + ';SharedAccessKey=' + str(key)
            ),
        }
        if key is not None:
            device.token_spent = True
            device.save(False)
            return Response(content)
        return Response(status=status.HTTP_404_NOT_FOUND)
    except:
        return Response(status=status.HTTP_404_NOT_FOUND)

@api_view(['POST'])
@permission_classes([AllowAny])
@csrf_exempt
def httpdata(request):
    '''Post method to store agristar data over https into database

    Parameters:
    request: Request object

    Returns:
    void
    '''
    apikey = request.META.get("HTTP_X_API_KEY", None)
    if not apikey:
        print('No api key')
        raise PermissionDenied("No key")
    
    start_time = time.time()

    try:
        # pylint: disable=no-member
        device = IoTClient.IoTClient.objects.get(id=apikey)
    except IoTClient.IoTClient.DoesNotExist as err:
        print(err)
        raise NotFound("Invalid IoT Client") from err

    if not device.is_active:
        print("Device is inactive")
        raise NotFound("Device is inactive")

    try:
        payload = json.loads(request.data['data'])
        if payload['payload']['Type'] == 'Settings':
            iot_log = IoTLog.IoTLog(
                iot_client = device,
                time_stamp = payload['timestamp'],
                payload = get_realtime(payload['payload'])
            )
            iot_log.save()
            device.last_log = payload
            device.front_matter = get_front_matter(payload['payload'])
            device.realtime = get_realtime(payload['payload'])
            device.settings = get_settings(payload['payload'])
            device.time_stamp = payload['timestamp']
            device.save(False)

        return Response(status=status.HTTP_200_OK)
    except Exception as err:
        print(device.id)
        print(err)
        raise AuthenticationFailed("Invalid login") from err
    finally:
        print('httpdata: ' + apikey + ' Time: ' + str(time.time() - start_time))
