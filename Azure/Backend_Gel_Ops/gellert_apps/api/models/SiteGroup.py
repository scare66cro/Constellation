from django.db import models
import uuid
from .CustomerAccount import CustomerAccount
from dry_rest_permissions.generics import allow_staff_or_superuser, authenticated_users, DRYPermissionFiltersBase

class SiteGroup(models.Model):
    id = models.UUIDField(primary_key=True, default=uuid.uuid4, editable=False)
    name = models.CharField(max_length=50)
    customer_account = models.ForeignKey(CustomerAccount, related_name='site_groups', on_delete=models.CASCADE)
    description = models.TextField(null=True, blank=True)
    # is_public = ...indicates that its viewable at cust_acct level
    # creator = ...if is_public false, only the creator will see it
    # sites = ...will need to be a many to many relationship for future versions of this feature

    def __str__(self):
        return self.customer_account.name + ' - ' + self.name

    class Meta:
        db_table = 'SiteGroup'

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
            if self.customer_account.parent.parent == user_org:
                return True
            # 2) dealer-user can see all sites belonging to their customers
            if self.customer_account.parent == user_org:
                return True
            # 3) cust-user can see all sites belonging to their cust-acct
            if self.customer_account == user_org:
                return True
            return False
        except:
            return False

# UPDATE
    # 1) no one can do anything - only via admin center
    @staticmethod
    @authenticated_users
    def has_update_permission(request):
        return False
    def has_object_update_permission(self, request):
        return False

# PARTIAL_UPDATE
    # 1) no one can do anything - only via admin center

# CREATE
    @staticmethod
    @authenticated_users
    def has_create_permission(request):
        return False

# DESTROY
    @staticmethod
    def has_destroy_permission(request):
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
class SiteGroupFilterBackend(DRYPermissionFiltersBase):
    def filter_list_queryset(self, request, queryset, view):
        queryset = queryset.filter(customer_account__is_active=True)
        user = request.user
        new_queryset = None # queryset.filter(id=None)

        try:
            if user.is_corporate_user():
                new_queryset = queryset.filter(customer_account__parent__parent=user.users_corporation())
            if user.is_dealer_user():
                new_queryset = queryset.filter(customer_account__parent=user.users_dealership())
            if user.is_customer_user():
                new_queryset = queryset.filter(customer_account=user.users_customer_account())
        except:
            print('SiteGroup filter backend related field Nonetype error...')

        return queryset if user.is_superuser else new_queryset
