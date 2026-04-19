''' 
    NOTE THIS IS TEMPORARY  
    all views will end up with their own view file once authorizations are implemented
    this is because the view classes will get more complicated
'''

from rest_framework.permissions import IsAuthenticated
from rest_framework import viewsets
from dry_rest_permissions.generics import DRYPermissions

from .models import CorporateAccount, DealerAccount, \
    SiteGroup, SiteEvent, IoTLog, UpgradeFile

from .serializers import CorporateAccountSerializer, DealerAccountSerializer, \
    IoTLogSerializer, SiteGroupSerializer, \
    SiteEventSerializer, UpgradeFileSerializer

class CorporateAccountViewSet(viewsets.ModelViewSet):
    permission_classes = (IsAuthenticated, DRYPermissions,)
    queryset = CorporateAccount.CorporateAccount.objects.all()
    filter_backends = (CorporateAccount.CorporateAccountFilterBackend,)
    serializer_class = CorporateAccountSerializer

class DealerAccountViewSet(viewsets.ModelViewSet):
    permission_classes = (IsAuthenticated, DRYPermissions,)
    queryset = DealerAccount.DealerAccount.objects.all()
    filter_backends = (DealerAccount.DealerAccountFilterBackend,)
    serializer_class = DealerAccountSerializer

class IoTLogViewSet(viewsets.ModelViewSet):
    queryset = IoTLog.IoTLog.objects.all()
    serializer_class = IoTLogSerializer

class SiteGroupViewSet(viewsets.ModelViewSet):
    permission_classes = (IsAuthenticated, DRYPermissions,)
    queryset = SiteGroup.SiteGroup.objects.all().order_by('name')
    filter_backends = (SiteGroup.SiteGroupFilterBackend,)
    serializer_class = SiteGroupSerializer

class SiteEventViewSet(viewsets.ModelViewSet):
    queryset = SiteEvent.SiteEvent.objects.all()
    serializer_class = SiteEventSerializer

class UpgradeFileViewSet(viewsets.ModelViewSet):
    queryset = UpgradeFile.UpgradeFile.objects.all()
    serializer_class = UpgradeFileSerializer
