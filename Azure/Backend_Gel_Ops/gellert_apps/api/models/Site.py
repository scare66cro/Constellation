from polymorphic.models import PolymorphicModel
from django.db import models
import uuid
# from .CustomerAccount import CustomerAccount
from .CustomerAccount import CustomerAccount
from .SiteGroup import SiteGroup
from .CorporateAccount import CorporateAccount
from ...user_account_app.models import UserAccount

from dry_rest_permissions.generics import allow_staff_or_superuser, authenticated_users, DRYPermissionFiltersBase

class Site(PolymorphicModel):
    # must change this in each inherited model, informs frontend of schema type
    # model = models.CharField(max_length=50, default='site')

    id = models.UUIDField(primary_key=True, default=uuid.uuid4, editable=False)

    category = models.CharField(max_length=50)
    name = models.CharField(max_length=50)
    owner = models.ForeignKey(CustomerAccount, related_name='sites', on_delete=models.CASCADE)

    site_group =  models.ForeignKey(SiteGroup, related_name='sites', null=True, blank=True, on_delete=models.CASCADE)

    is_active = models.BooleanField(default=True, db_index=True)

    def __str__(self):
        return self.owner.name + ' - ' + self.name

    class Meta:
        db_table = 'Site'

# ---------------------- PERMISSIONS -------------------------
# READ -groups together list and retrieve
    @staticmethod
    @authenticated_users # unauthenticated users auto fail
    def has_read_permission(request):
        return True

    # @admin_and_staff so we can set justin and aaron to see any corp
    @allow_staff_or_superuser
    def has_object_read_permission(self, request):
        user_org = request.user.organization
        try:
            # 1) corp-user can see all sites belonging to their customers
            # pylint: disable=no-member
            if self.owner.parent.parent == user_org:
                return True
            # 2) dealer-user can see all sites belonging to their customers
            if self.owner.parent == user_org:
                return True
            # 3) cust-user can see all sites belonging to their cust-acct
            if self.owner == user_org:
                return True
            return False
        except:
            return False

# UPDATE
    # 1) no one can do anything - only via admin center
    @staticmethod
    @authenticated_users
    def has_update_permission(request):
        return request.user.has_perm('api.change_site')

    def has_object_update_permission(self, request):
        user_org = UserAccount.objects.filter(id=request.user.id).first()
        self_org = UserAccount.objects.filter(id=self.id).first()
        if (self_org and user_org and
                (self_org.organization.parent == user_org.organization or
                (self_org.organization.parent and self_org.organization.parent.parent == user_org.organization))
            and request.user.has_perm('api.change_site')):
            return True

        return False

# PARTIAL_UPDATE
    # 1) no one can do anything - only via admin center

    @staticmethod
    @authenticated_users
    def has_write_permission(request):
        return True

# CREATE
    @staticmethod
    @authenticated_users
    def has_create_permission(request):
        return request.user.has_perm('api.add_site')

# DESTROY
    @staticmethod
    def has_destroy_permission(request):
        return request.user.has_perm('api.delete_site')

    @allow_staff_or_superuser
    @authenticated_users
    def has_object_destroy_permission(self, request):
        user_org = UserAccount.objects.filter(id=request.user.id).first()
        self_org = UserAccount.objects.filter(id=self.id).first()
        if (self_org and user_org and
            (self_org.organization.parent == user_org.organization or
            (self_org.organization.parent and self_org.organization.parent.parent == user_org.organization))
            and request.user.has_perm('api.delete_site')):
            return True

        return False

# WRITE -groups together destroy, update and create
    # # user can change own account - partially
    # @staticmethod
    # def has_write_permission(request):
    #     return True
    
    # def has_object_write_permission(self, request):
    #     return request.user == self

# META_DATA
    @staticmethod
    @authenticated_users
    def has_metadata_permission(request):
        return True

# ---------------- QUERYSET FILTERS --------------------------
class SiteFilterBackend(DRYPermissionFiltersBase):
    def filter_list_queryset(self, request, queryset, view):
        queryset = queryset.filter(is_active=True)
        user = request.user
        new_queryset = None # queryset.filter(id=None)

        try:
            if user.is_corporate_user():
                new_queryset = queryset.filter(owner__parent__parent=user.users_corporation())
            if user.is_dealer_user():
                new_queryset = queryset.filter(owner__parent=user.users_dealership())
            if user.is_customer_user():
                new_queryset = queryset.filter(owner=user.users_customer_account())
        except:
            print('site filter backend related field Nonetype error...')

        return queryset if user.is_superuser else new_queryset
