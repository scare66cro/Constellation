from django.db import models
import uuid
from .Organization import Organization
from .DealerAccount import DealerAccount

from dry_rest_permissions.generics import allow_staff_or_superuser, authenticated_users, DRYPermissionFiltersBase

class CustomerAccount(Organization):
    # Note: To add seasonal temporary tenants create a Tenant model with a time frame or something

    def __str__(self):
        return 'Customer - ' + self.name

    class Meta:
        db_table = 'CustomerAccount'
        verbose_name = 'Customer Account'

# ---------------------- PERMISSIONS -------------------------
# READ
    @staticmethod
    @authenticated_users
    def has_read_permission(request):
        return True

    # staff can see all corps
    @allow_staff_or_superuser
    def has_object_read_permission(self, request):
        print('-----Customer-Acct read permissions------', self.parent)
        try:
            # 1) corp user can see corp's subsidiary dealers & customers  
            # pylint: disable=no-member
            if self.parent.parent == request.user.organization:
                return True
            # 2) dealer user can see own dealer account
            if self.parent == request.user.organization:
                return True
            # 3) customer user can see its cust-account's parent dealer
            if self == request.user.organization:
                return True
            return False
        except:
            return False

# UPDATE
    @staticmethod
    @authenticated_users
    def has_update_permission(request):
        return request.user.has_perm('api.change_customeraccount')

    @allow_staff_or_superuser
    @authenticated_users
    def has_object_update_permission(self, request):
        user_org = CustomerAccount.objects.filter(id=request.user.id).first()
        self_org = CustomerAccount.objects.filter(id=self.id).first()
        if (user_org and self_org and
            (self_org.organization.parent == user_org.organization or
            (self_org.organization.parent and self_org.organization.parent.parent == user_org.organization))
            and request.user.has_perm('api.change_customeraccount')):
            return True

        return False

# PARTIAL UPDATE
# 1) partial updates can't really be
#  ...implemented until multiple serializers are set up

    @staticmethod
    @authenticated_users
    def has_write_permission(request):
        return True


# CREATE
    @staticmethod
    @authenticated_users
    def has_create_permission(request):
        return request.user.has_perm('api.add_customeraccount')

    @staticmethod
    def has_destroy_permission(request):
        return request.user.has_perm('api.delete_customeraccount')

    @allow_staff_or_superuser
    @authenticated_users
    def has_object_destroy_permission(self, request):
        user_org = CustomerAccount.objects.filter(id=request.user.id).first()
        self_org = CustomerAccount.objects.filter(id=self.id).first()
        if (user_org and self_org and
            (self_org.organization.parent == user_org.organization or
            (self_org.organization.parent and self_org.organization.parent.parent == user_org.organization))
            and request.user.has_perm('api.delete_customeraccount')):
            return True

        return False

# META_DATA
    @staticmethod
    @authenticated_users
    def has_metadata_permission(request):
        return True

# ---------------- QUERYSET FILTERS --------------------------
class CustomerAccountFilterBackend(DRYPermissionFiltersBase):
    def filter_list_queryset(self, request, queryset, view):
        queryset = queryset.filter(is_active=True)
        user = request.user
        new_queryset = None # queryset.filter(id=None)

        if user.is_corporate_user():
            new_queryset = queryset.filter(parent__parent=user.users_corporation())
        if user.is_dealer_user():
            new_queryset = queryset.filter(parent=user.users_dealership())
        if user.is_customer_user():
            new_queryset = queryset.filter(id=user.users_customer_account().id)

        return queryset if user.is_superuser else new_queryset
