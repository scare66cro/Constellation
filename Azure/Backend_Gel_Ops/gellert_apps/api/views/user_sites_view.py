from rest_framework.permissions import IsAuthenticated
from rest_framework import viewsets, status
from rest_framework.decorators import action
from rest_framework.response import Response
from dry_rest_permissions.generics import DRYPermissions

from ...user_account_app.models import UserAccount
from ..models.user_sites import UserSites, UserSitesFilterBackend
from ..models.Site import Site
from ..serializers import SiteSerializer, UserSitesSerializer

class UserSitesViewSet(viewsets.ModelViewSet):
    permission_classes = (IsAuthenticated, DRYPermissions,)
    queryset = UserSites.objects.all()
    filter_backends = (UserSitesFilterBackend,)
    serializer_class = UserSitesSerializer

    def update(self, request, pk=None):
        try:
            selected_user = UserAccount.objects.filter(id=pk).first()
            sites = request.data.get('sites', [])
            if self.queryset.filter(user=selected_user):
                user_sites = self.queryset.filter(user=selected_user)
                user_sites.delete()
            created_sites = UserSites.objects.create(user=selected_user)
            created_sites.sites.set(sites)
            created_sites.save()
            added_sites = self.queryset.filter(user=selected_user).first().sites.all().order_by('name')
            data = [SiteSerializer(obj, context={'request': request}).data for obj in added_sites]
            return Response(data, status.HTTP_200_OK)
        except Exception as exc:
            return Response(str(exc), status=status.HTTP_400_BAD_REQUEST)

    @action(detail=True, methods=['get'])
    def assigned_sites(self, request, pk=None):
        selected_user = UserAccount.objects.filter(id=pk).first()
        sites = None
        if self.queryset.filter(user=selected_user).exists():
            sites = self.queryset.filter(user=selected_user).first().sites.all().order_by('name')
        if sites:
            data = [SiteSerializer(obj, context={'request': request}).data for obj in sites]
            return Response(data, status=status.HTTP_200_OK)
        return Response(status.HTTP_404_NOT_FOUND)
        

    @action(detail=False, methods=['get'])
    def my_sites(self, request):
        new_queryset = None
        user = request.user
        sites = None
        if self.queryset.filter(user=user).exists():
            sites = self.queryset.filter(user=user).first().sites

        try:
            if user.is_superuser:
                new_queryset = Site.objects.all()
            else:
                if user.is_corporate_user():
                    new_queryset = Site.objects.filter(owner__parent__parent=user.users_corporation())
                if user.is_dealer_user():
                    new_queryset = Site.objects.filter(owner__parent=user.users_dealership())
                if user.is_customer_user():
                    new_queryset = Site.objects.filter(owner=user.users_customer_account())
            ret_val = None
            if new_queryset and sites:
                ret_val = sites.union(new_queryset)
            elif new_queryset:
                ret_val = new_queryset
            elif sites:
                ret_val = sites.all()

            if ret_val:
                ret_val.order_by('name')
                data = [SiteSerializer(obj, context={'request': request}).data for obj in ret_val]
                return Response(data, status=status.HTTP_200_OK)
        except Exception as exc:
            return Response(str(exc), status=status.HTTP_400_BAD_REQUEST)

        return Response(status.HTTP_404_NOT_FOUND)
