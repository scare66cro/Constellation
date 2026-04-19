from django.db import models
import uuid
from .Organization import Organization

# PERMISSION imports
from dry_rest_permissions.generics import allow_staff_or_superuser, authenticated_users, DRYPermissionFiltersBase

class CorporateAccount(Organization):

    def __str__(self):
        return 'Corp. - ' + self.name

    class Meta:
        db_table = 'CorporateAccount'
        verbose_name = 'Corporate Account'

# ---------------------- PERMISSIONS -------------------------
# READ
    @staticmethod
    @authenticated_users
    def has_read_permission(request):
        return True

    # staff can see all corps
    @allow_staff_or_superuser
    def has_object_read_permission(self, request):
        print('-----corp read permission---')
        try:
            # 1) corp user can see own corp
            if self == request.user.organization:
                return True
            # 2) dealer user can see parent corp
            if self == request.user.organization.parent:
                return True
            # 3) customer user can see its dealer's parent corp
            if self == request.user.organization.parent.parent:
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
class CorporateAccountFilterBackend(DRYPermissionFiltersBase):
    def filter_list_queryset(self, request, queryset, view):

        user_corp = request.user.users_corporation()

        queryset = queryset.filter(is_active=True)
        return queryset if request.user.is_staff else queryset.filter(
            models.Q(id=user_corp.id)
        )