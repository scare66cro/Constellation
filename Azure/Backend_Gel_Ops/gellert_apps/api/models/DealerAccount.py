from django.db import models
import uuid
from .Organization import Organization

from dry_rest_permissions.generics import allow_staff_or_superuser, authenticated_users, DRYPermissionFiltersBase

class DealerAccount(Organization):

    def __str__(self):
        return 'Dealer - ' + self.name

    class Meta:
        db_table = 'DealerAccount'
        verbose_name = 'Dealer Account'

# ---------------------- Queries -----------------------------
    # def queryset_corporate_user(self, request, qset=DealerAccount.objects.all()):
    #     return qset

# ---------------------- PERMISSIONS -------------------------
# READ
    @staticmethod
    @authenticated_users
    def has_read_permission(request):
        return True

    # staff can see all corps
    @allow_staff_or_superuser
    def has_object_read_permission(self, request):
        print('-----dealer read permissions------')
        try:
            # 1) corp user can see corp's subsidiary dealers
            if self.parent == request.user.organization:
                return True
            # 2) dealer user can see own dealer account
            if self == request.user.organization:
                return True
            # 3) customer user can see its cust-account's parent dealer
            if self == request.user.organization.parent:
                return True
            return False
        except:
            return False

# UPDATE
    @staticmethod
    @authenticated_users
    def has_update_permission(request):
        return False

# PARTIAL UPDATE
# 1) partial updates can't really be
#  ...implemented until multiple serializers are set up

# CREATE
    @staticmethod
    @authenticated_users
    def has_create_permission(request):
        return False

# DESTROY
    @staticmethod
    @authenticated_users
    def has_destroy_permission(request):
        return False

# META_DATA
    @staticmethod
    @authenticated_users
    def has_metadata_permission(request):
        return True

# ---------------- QUERYSET FILTERS --------------------------
class DealerAccountFilterBackend(DRYPermissionFiltersBase):
    def filter_list_queryset(self, request, queryset, view):
        queryset = queryset.filter(is_active=True)
        user = request.user
        new_queryset = None # queryset.filter(id=None)

        if user.is_corporate_user():
            new_queryset = queryset.filter(parent=user.users_corporation())
        if user.is_dealer_user():
            new_queryset = queryset.filter(id=user.organization.id)
        if user.is_customer_user():
            new_queryset = queryset.filter(id=user.users_dealership().id) 

        return queryset if user.is_superuser else new_queryset