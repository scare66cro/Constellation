import os
from django.http.response import HttpResponse
from django.views.decorators.csrf import csrf_exempt
from rest_framework import viewsets
from rest_framework.permissions import AllowAny, IsAuthenticated
from rest_framework.decorators import action, api_view, permission_classes
from rest_framework import status
from rest_framework.response import Response
from rest_framework.exceptions import AuthenticationFailed
from dry_rest_permissions.generics import DRYPermissions
from wsgiref.util import FileWrapper

from ..database_storage import DatabaseStorage

from ..agristar2_actions import process_upgrade_action
from ..models import upgrades, IoTClient
from ..serializers import UpgradeSerializer

class UpgradesViewSet(viewsets.ModelViewSet):
    permission_classes = (IsAuthenticated, DRYPermissions,)
    queryset = upgrades.Upgrade.objects.all()
    serializer_class = UpgradeSerializer

    @action(detail=True, methods=['get'])
    def upgrade_data(self, request, pk=None):
        '''
        Check to see if any upgrades are available.
        If user does not have permission then just return 'latest'
        '''
        upgrade = upgrades.Upgrade.objects.values('version', 'description').last()
        return Response(
            {'upgrade': upgrade},
            status=status.HTTP_200_OK
        )

    @action(detail=True, methods=['post'])
    def upgrade_action(self, request, pk=None):
        iot_client = IoTClient.IoTClient.objects.get(id=pk)
        process_upgrade_action(request, iot_client)
        return Response(status=status.HTTP_200_OK)

@api_view(['GET'])
@permission_classes([AllowAny])
@csrf_exempt
def payload(request, upgrade=None):
    apikey = request.META.get("HTTP_X_API_KEY", None)
    if not apikey:
        return None

    try:
        # pylint: disable=no-member
        device = IoTClient.IoTClient.objects.get(id=apikey)
    except IoTClient.IoTClient.DoesNotExist as err:
        raise AuthenticationFailed("Invalid IoT Client") from err

    if not device.is_active:
        raise AuthenticationFailed("Device is inactive or deleted")

    storage = DatabaseStorage()

    upgradeFile = upgrades.Upgrade.objects.get(version=upgrade)
    upgrade = storage.open(upgradeFile.payload.name, 'rb')
    response = HttpResponse(upgrade, content_type='application/octet-stream')
    response['Content-Length'] = len(upgrade)
    response['Content-Disposition'] = 'inline; filename=%s '%upgradeFile.payload.name
    return response
