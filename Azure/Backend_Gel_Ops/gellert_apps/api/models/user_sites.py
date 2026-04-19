'''
    UserSites
    Contains any additional sites the user may have permissions to view
'''
from django.db import models
from dry_rest_permissions.generics import allow_staff_or_superuser, authenticated_users, DRYPermissionFiltersBase
from .Site import Site
from ...user_account_app.models import UserAccount

class UserSites(models.Model):
    '''
        UserSites model
    '''
    user = models.ForeignKey(UserAccount, on_delete=models.CASCADE)
    sites = models.ManyToManyField(Site)

    class Meta:
        '''Define database name'''
        db_table = 'UserSites'
        verbose_name = 'User Site List'

    @staticmethod
    @authenticated_users
    def has_read_permission(request):
        '''
        Authenticated user has read permissions
        '''
        return True

    @allow_staff_or_superuser
    @authenticated_users
    def has_object_read_permission(self, request):
        '''
        Authenticated or staff users have object read permissions
        '''
        return True

    # Only add or change UserSites through admin center
    @staticmethod
    @authenticated_users
    def has_update_permission(request):
        '''
        Authenticated users with permissions have update permissions
        '''
        return request.user.has_perm('api.change_usersites')


    @authenticated_users
    def has_object_update_permission(self, request):
        '''
        Authenticated users with permission have object update permissions
        '''
        return request.user.has_perm('api.change_usersites')

    @staticmethod
    @authenticated_users
    def has_create_permission(request):
        '''
        Authenticated users with permission have create permissions
        '''
        return request.user.has_perm('api.add_usersites')

    @staticmethod
    def has_destroy_permission(request):
        '''
        No users have destroy persmissions
        '''
        return False

    @staticmethod
    @authenticated_users
    def has_metadata_permission(request):
        '''
        Authenticated users have meta data permissions
        '''
        return True

# ---------------- QUERYSET FILTERS --------------------------
class UserSitesFilterBackend(DRYPermissionFiltersBase):
    def filter_list_queryset(self, request, queryset, view):
        user = request.user
        queryset = queryset.filter(user=user)
        return queryset