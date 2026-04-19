from django.db import models
from django.contrib.auth.models import AbstractUser
import uuid
from dry_rest_permissions.generics import allow_staff_or_superuser, authenticated_users, DRYPermissionFiltersBase
# pylint: disable=import-error
from gellert_apps.api.models.Organization import Organization
from gellert_apps.api.models.CorporateAccount import CorporateAccount
from gellert_apps.api.models.DealerAccount import DealerAccount
from gellert_apps.api.models.CustomerAccount import CustomerAccount

# Create your models here.
class UserAccount(AbstractUser):
    id = models.UUIDField(primary_key=True, default=uuid.uuid4, editable=False)
    organization = models.ForeignKey(Organization, related_name="users", on_delete=models.SET_NULL, null=True)

    user_types_choices = {
        'corporate': 'corporate',
        'dealer':'dealer',
        'customer':'customer',
    }

    class Meta:
        db_table = 'UserAccount'

# --------------------- User Types ---------------------------
    def get_user_type(self):
        # pylint: disable=no-member
        try:
            if self.organization.is_model_type(CorporateAccount):
                return self.user_types_choices['corporate']
            if self.organization.is_model_type(DealerAccount):
                return self.user_types_choices['dealer']
            if self.organization.is_model_type(CustomerAccount):
                return self.user_types_choices['customer']
            return None
        except:
            return None

    def is_corporate_user(self):
        return self.get_user_type() == self.user_types_choices['corporate']
    
    def is_dealer_user(self):
        return self.get_user_type() == self.user_types_choices['dealer']
    
    def is_customer_user(self):
        return self.get_user_type() == self.user_types_choices['customer']
    
    def users_corporation(self):
        # pylint: disable=no-member
        if self.is_corporate_user():
            return self.organization
        if self.is_dealer_user():
            return self.organization.parent
        if self.is_customer_user():
            return self.organization.parent.parent
        return None
    
    def users_dealership(self):
        # pylint: disable=no-member
        if self.is_corporate_user():
            return None
        if self.is_dealer_user():
            return self.organization
        if self.is_customer_user():
            return self.organization.parent
        return None

    def users_customer_account(self):
        # pylint: disable=no-member
        if self.is_corporate_user():
            return None
        if self.is_dealer_user():
            return None
        if self.is_customer_user():
            return self.organization
        return None

# ---------------------- PERMISSIONS -------------------------
# READ -groups together list and retrieve
    @staticmethod
    @authenticated_users # unauthenticated users auto fail
    def has_read_permission(request):
        return True

    # @admin_and_staff so we can set justin and aaron to see any corp
    @allow_staff_or_superuser
    @authenticated_users
    def has_object_read_permission(self, request):
    # 1) user can see own account
        if request.user == self:
            return True
    # 2) user can see partial of other users in their org & of all users in orgs below their org
        user_org = UserAccount.objects.get(id=request.user.id).organization
        self_org = UserAccount.objects.get(id=self.id).organization
        if self_org.parent == user_org or (self_org.parent and self_org.parent.parent == user_org):
            return True
        return False

# UPDATE
    # 1) no one can do anything - only via admin center
    @staticmethod
    @authenticated_users
    def has_update_permission(request):
        return request.user.has_perm('user_account_app.change_useraccount')

    @allow_staff_or_superuser
    @authenticated_users
    def has_object_update_permission(self, request):
        user_org = UserAccount.objects.filter(id=request.user.id).first().organization
        self_org = UserAccount.objects.filter(id=self.id).first().organization
        if (self_org.parent == user_org or (self_org.parent and self_org.parent.parent == user_org)) \
            and request.user.has_perm('user_account_app.change_useraccount'):
            return True

        return False
    
    @staticmethod
    @authenticated_users
    def has_write_permission(request):
        return True

# CREATE
    @staticmethod
    @authenticated_users
    def has_create_permission(request):
        return request.user.has_perm('user_account_app.create_useraccount')

# DESTROY
    @staticmethod
    def has_destroy_permission(request):
        return request.user.has_perm('user_account_app.delete_useraccount')

    @allow_staff_or_superuser
    @authenticated_users
    def has_object_destroy_permission(self, request):
        user_org = UserAccount.objects.filter(id=request.user.id).first().organization
        self_org = UserAccount.objects.filter(id=self.id).first().organization
        if (self_org.parent == user_org or (self_org.parent and self_org.parent.parent == user_org)) \
            and request.user.has_perm('user_account_app.delete_useraccount'):
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

# CHANGE_PASSWORD
    @staticmethod
    @authenticated_users
    def has_change_password_permission(request):
        return True

    def has_object_change_password_permission(self, request):
        return request.user == self

# ---------------- QUERYSET FILTERS --------------------------
class UserAccountFilterBackend(DRYPermissionFiltersBase):
    def filter_list_queryset(self, request, queryset, view):
        # looks up the foreign key chain on the queryset model side (left side of '=')
        # idk how to look down the through the many side to find matching instances
        queryset = queryset.filter(is_active=True)
        return queryset if request.user.is_staff else queryset.filter(
            models.Q(organization=request.user.organization)
            or models.Q(organization__parent=request.user.organization)
            or models.Q(organization__parent__parent=request.user.organization)
        )
