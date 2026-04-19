''' Handle sending upgrades to iotclient '''
from django.db import models
from dry_rest_permissions.generics import allow_staff_or_superuser, authenticated_users

from ..database_storage import DatabaseStorage

class Upgrade(models.Model):
    ''' IoTClient upgrades '''
    id = models.AutoField(primary_key=True)

    version = models.CharField(max_length=25, blank=False, null=False)
    payload = models.FileField(blank=False, null=False, storage=DatabaseStorage())
    description = models.TextField(blank=False, null=False)

    def delete(self, using=None, keep_parents=False):
        self.payload.storage.delete(self.payload.name)
        super().delete()

    class Meta:
        '''Define database name'''
        db_table = 'Upgrade'
        verbose_name = 'Upgrade File'

    @staticmethod
    @authenticated_users
    def has_read_permission(request):
        return True

    @allow_staff_or_superuser
    @authenticated_users
    def has_object_read_permission(self, request):
        return True

    # Only add or change Upgrades through admin center
    @staticmethod
    @authenticated_users
    def has_update_permission(request):
        return False

    def has_object_update_permission(self, request):
        return False
    
    @staticmethod
    def has_create_permission(request):
        return False
    
    @staticmethod
    def has_destroy_permission(request):
        return False

    @staticmethod
    @authenticated_users
    def has_metadata_permission(request):
        return True

    # ACTION -> Check for available upgrades
    @staticmethod
    @allow_staff_or_superuser
    @authenticated_users
    def has_upgrade_data_permission(request):
        user = request.user
        return user.has_perm('api.view_upgrade')
    
    @allow_staff_or_superuser
    @authenticated_users
    def has_object_upgrade_data_permission(self, request):
        return self.has_object_read_permission(request)

    # ACTION -> Check for user permission to upgrade
    @staticmethod
    @allow_staff_or_superuser
    @authenticated_users
    def has_upgrade_action_permission(request):
        user = request.user
        return user.has_perm('api.view_upgrade')

    @allow_staff_or_superuser
    @authenticated_users
    def has_object_upgrade_action_permission(self, request):
        return self.has_object_read_permission(request)
