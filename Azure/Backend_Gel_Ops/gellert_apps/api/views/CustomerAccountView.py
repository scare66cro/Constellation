from rest_framework.decorators import action
from rest_framework.response import Response
from rest_framework.permissions import IsAuthenticated
from rest_framework import viewsets, status
from rest_framework.exceptions import PermissionDenied
from dry_rest_permissions.generics import DRYPermissions
from gellert_apps.api.models import Organization

from gellert_apps.api.serializers import CustomerAccountSerializer
from ..models.CustomerAccount import CustomerAccount, CustomerAccountFilterBackend
from ..models.user_sites import UserSites

class CustomerAccountViewSet(viewsets.ModelViewSet):
    permission_classes = (IsAuthenticated, DRYPermissions,)
    queryset = CustomerAccount.objects.all().order_by('name')
    filter_backends = (CustomerAccountFilterBackend, )
    serializer_class = CustomerAccountSerializer

    @action(detail = False, methods=['post'])
    def new_customer(self, request):
        if request.user.has_perm('api.change_customeraccount'):
            account = request.data.get('customer')
            new_account = CustomerAccount(
                id = account['id'],
                name = account['name'],
                parent = Organization.Organization.objects.get(id=account['parent']),
                is_active = account['isActive'],
            )
            new_account.save()
            return Response(CustomerAccountSerializer(new_account, context={"request": request}).data, status=status.HTTP_200_OK)

        raise PermissionDenied

    @action(detail = False, methods=['get'])
    def my_customers(self, request):
        user = request.user
        sites = None
        unique_owners = []
        seen_owners = set()
        customers = [CustomerAccountSerializer(q, context={"request": request}).data for q in self.filter_queryset(self.queryset)]
        for customer in customers:
            if customer['id'] not in seen_owners:
                unique_owners.append(customer)
                seen_owners.add(customer['id'])
        if UserSites.objects.filter(user=user).exists():
            sites = UserSites.objects.filter(user=user).first().sites
            for site in sites.all():
                owner_data = CustomerAccountSerializer(site.owner, context={"request": request}).data
                owner_id = owner_data['id']
                if owner_id not in seen_owners:
                    seen_owners.add(owner_id)
                    owner_data['sites'] = list(filter(lambda x: x in [site.id for site in sites.all()], owner_data['sites']))
                    unique_owners.append(owner_data)
        return Response(unique_owners, status=status.HTTP_200_OK)
