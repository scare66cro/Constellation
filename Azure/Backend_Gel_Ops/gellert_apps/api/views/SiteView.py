from rest_framework import viewsets, status
from rest_framework.decorators import action
from rest_framework.exceptions import PermissionDenied
from rest_framework.permissions import IsAuthenticated
from rest_framework.response import Response
from dry_rest_permissions.generics import DRYPermissions
from ..models import Site
from ..serializers import SiteSerializer

class SiteViewSet(viewsets.ModelViewSet):
    permission_classes = (IsAuthenticated, DRYPermissions,)
    queryset = Site.Site.objects.all().order_by('name')
    filter_backends = (Site.SiteFilterBackend,)
    serializer_class = SiteSerializer

    @action(detail = False, methods=['post'])
    def new_site(self, request):
        if request.user.has_perm('api.change_site'):
            site = request.data.get('site')
            new_site = Site.Site(
                id = site["id"],
                category = site["category"],
                is_active = site["is_active"],
                owner_id = site["owner_id"],
                name = site["name"]
            )
            new_site.save()
            return Response(SiteSerializer(new_site, context={'request': request}).data, status=status.HTTP_200_OK)

        raise PermissionDenied

