from rest_framework import viewsets
from rest_framework.decorators import action
from rest_framework.permissions import IsAuthenticated
from rest_framework.response import Response
from dry_rest_permissions.generics import DRYPermissions

from ..models import Organization
from ..serializers import OrganizationSerializer

class OrganizationViewSet(viewsets.ModelViewSet):
    permission_classes = (IsAuthenticated, DRYPermissions,)
    queryset = Organization.Organization.objects.all().order_by('polymorphic_ctype_id', 'name')
    filter_backends = (Organization.OrganizationsFilterBackend,)
    serializer_class = OrganizationSerializer

