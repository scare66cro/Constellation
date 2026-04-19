''' IoTClient View Module '''
from django.http.response import JsonResponse
from rest_framework.decorators import api_view, permission_classes, throttle_classes
from rest_framework.permissions import AllowAny
from rest_framework.response import Response
from rest_framework.response import Response
from rest_framework.exceptions import AuthenticationFailed
from django.views.decorators.csrf import csrf_exempt
from gellert_project.query_anon_throttle import QueryAnonymousRateThrottle
import time

from ..models import IoTClient, http_message_queue
from ..serializers import HttpMessageQueueSerializer

def verify_iot_client(request):
    '''
    Method to verify if this is a valid request
    '''
    apikey = request.META.get("HTTP_X_API_KEY", None)
    device = None
    if not apikey:
        return None

    try:
        # pylint: disable=no-member
        device = IoTClient.IoTClient.objects.get(id=apikey)
    except IoTClient.IoTClient.DoesNotExist as err:
        print(err)
        raise AuthenticationFailed("Invalid IoT Client") from err

    if device and not device.is_active:
        print("Verify IoT Client: Device is inactive or deleted")
        raise AuthenticationFailed("Device is inactive or deleted")

    return device


@api_view(['POST'])
@permission_classes([AllowAny])
@csrf_exempt
@throttle_classes([QueryAnonymousRateThrottle])
def validation(request, pk=None):
    '''
    Log any validation errors
    '''
    try:
        verify_iot_client(request)
        message = http_message_queue.HttpMessageQueue.objects.get(id=pk)
        if "Type" in request.data and request.data['Type'] == 'Validation':
            message.validation = request.data['errors']
            message.save()
        return Response(status=200)
    except Exception as err:
        print(err)
        return Response(status=500, data={"error": str(err)})

@api_view(['GET'])
@permission_classes([AllowAny])
@csrf_exempt
@throttle_classes([QueryAnonymousRateThrottle])
def query(request):
    '''Post method to query for settings and actions'''
    # time function
    start_time = time.time()
    id = ''
    try:
        device = verify_iot_client(request)
        if device is not None:
            id = device.id
        items = http_message_queue.HttpMessageQueue.objects.filter(
            iot_client = device.id,
            is_processed = False
        )
        for item in items:
            item.is_processed = True
            item.save()
        serializer = HttpMessageQueueSerializer(items, many=True)
        return JsonResponse(serializer.data, safe=False)
    except Exception as err:
        print(err)
        return Response(status=500, data={"error": str(err), "device": id})
    finally:
        print("Query: " + str(id) + " Time: " + str(time.time() - start_time))
