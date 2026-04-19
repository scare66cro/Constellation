''' IoT Client Model '''
import uuid
from django.db import models
from django.conf import settings as django_settings
from .Site import Site
from .user_sites import UserSites
from ...user_account_app.models import UserAccount
from rest_framework import status
from rest_framework.response import Response
from ..azure_iot_hub import register_new_devices, get_device_from_iot_hub
from django.db.models.signals import post_delete
from django.dispatch import receiver
from ..azure_iot_hub import delete_device
from django.db.models import signals

from dry_rest_permissions.generics import allow_staff_or_superuser, authenticated_users, DRYPermissionFiltersBase

class IoTClientManager(models.Manager):
    def is_agristar2_or_HTTP_400(self, iot_client_id):
        # pylint: disable=no-member
        iot_client = IoTClient.objects.get(id=iot_client_id)
        if iot_client is not None and iot_client.client_type == 'agristar2':
            content={'detail':'This device is not an agristar2.'}
            return Response(content, status=status.HTTP_400_BAD_REQUEST)
        return 

def get_default_token():
    return str(uuid.uuid4()).split('-')[4]

class IoTClient(models.Model):
    # --------------------FIELDS & CHOICES----------------------------------
    id = models.UUIDField(primary_key=True, default=uuid.uuid4, editable=True)
    site = models.ForeignKey(Site, related_name='iot_devices', blank=True, null=True, on_delete=models.SET_NULL)
    name = models.CharField(max_length=50, default='Agri-Star', blank=True, null=True)
    location_id = models.CharField(max_length=100, blank=True, null=True) # New field

    agristar2 = 'agristar2' # do not change these choices, this is what will be written in the database
    agristar1 = 'agristar1'
    nova = 'nova'
    client_type_choices = [
        (agristar1, 'Agristar1'), # change Agristar1 to change how it appears in the UI type choices list 
        (agristar2, 'Agristar2'),
        (nova, 'Nova'),
    ]
    client_type = models.CharField(max_length=50, choices=client_type_choices, default=agristar2)

    unsecured_ip = models.CharField(max_length=50, blank=True, null=True)
    is_active = models.BooleanField(default=True, db_index=True)
    last_log = models.JSONField(null=True, blank=True)
    front_matter = models.JSONField(null=True, blank=True)
    realtime = models.JSONField(null=True, blank=True)
    settings = models.JSONField(null=True, blank=True)
    time_stamp = models.DateTimeField(null=True, blank=True)
    time_zone = models.CharField(max_length=50, default='America/Boise', blank=False, null=False)

    # LEVEL2 fields
    iot_hub_name = models.CharField(max_length=50, default=django_settings.DEFAULT_IOT_HUB,)
    token = models.CharField(max_length=25, unique=True, default=get_default_token, db_index=True)
    token_spent = models.BooleanField(default=False)

    def save(self, register = True, *args, **kwargs):
        # Skip IoT Hub registration in local development mode
        local_dev = getattr(django_settings, 'LOCAL_DEV', False)
        if local_dev or (not register) or (get_device_from_iot_hub(self.id) is not None):
            super(IoTClient, self).save(*args, **kwargs)
            return
        registered = register_new_devices([self.id])
        if registered is None:
            raise Exception('Failed to save to IoTHub')
        else:
            super(IoTClient, self).save(*args, **kwargs)
    
    # -----------------ADMIN COLUMN NAMES--------------------
    # pylint: disable=no-member    
    def owner_name(self):
        return self.site.name if self.site is not None else '--'
    # owner_name.admin_order_field = '-owner_name'

    def site_name(self):
        return self.site.name if self.site is not None else '--'
    # site_name.admin_order_field = 'site_name'

    def device_available(self):
        return 'Available' if self.token_spent is False else '' 

    def get_location_name(self):
        """Get the location name from the external API"""
        if self.location_id:
            try:
                from ..location_service import LocationAPIService
                service = LocationAPIService()
                location = service.get_location_by_id(int(self.location_id))
                return location.get('name', 'Unknown') if location else 'Not Found'
            except (ValueError, Exception):
                return 'Invalid Location ID'
        return None

    def __str__(self):
        # pylint: disable=no-member
        return  (
            (self.site.owner.name + ' - ' + self.site.name + ' - ' if self.site is not None else 'Site unassigned - ')
            + self.client_type + (' - available' if self.token_spent is False else '')
        )

    class Meta:
        db_table = 'IoTClient'
        verbose_name = 'IoT Client'
        permissions = [
            ('access_level1_IoTClient','Can access Agristar level 1.'),
            ('access_level2_IoTClient','Can access Agristar level 2.'),
        ]

    # ---------------------- PERMISSIONS -------------------------
# READ -groups together list and retrieve
    @staticmethod
    @authenticated_users # unauthenticated users auto fail
    def has_read_permission(request):
        return True

    # @admin_and_staff so we can set justin and aaron to see any corp
    @allow_staff_or_superuser
    def has_object_read_permission(self, request):
        # pylint: disable=no-member
        user_org = request.user.organization
        try:
            # 3) cust-user can see all sites belonging to their cust-acct
            if self.site.owner == user_org:
                return True
            # 2) dealer-user can see all sites belonging to their customers
            if self.site.owner.parent == user_org:
                return True
            # 1) corp-user can see all iot_clients belonging to their customers
            if self.site.owner.parent.parent == user_org:
                return True
            # check to see if user has access to additional site
            if UserSites.objects.filter(user=request.user).exists():
                my_sites = UserSites.objects.get(user=request.user)
                sites = my_sites.sites.all()
                if self.site in sites:
                    return True
            return False
        except:
            return False

# UPDATE
    # 1) no one can do anything - only via admin center
    @staticmethod
    @authenticated_users
    def has_update_permission(request):
        return request.user.has_perm('api.change_iotclient')

    def has_object_update_permission(self, request):
        user_org = UserAccount.objects.filter(id=request.user.id).first()
        self_org = UserAccount.objects.filter(id=self.id).first()
        if (user_org and self_org and
                (self_org.organization.parent == user_org.organization or
                (self_org.organization.parent and self_org.organization.parent.parent == user_org.organization))
            and request.user.has_perm('api.change_iotclient')):
            return True

        return False

# PARTIAL_UPDATE
    # 1) no one can do anything - only via admin center

# CREATE
    @staticmethod
    @authenticated_users
    def has_create_permission(request):
        return request.user.has_perm('api.add_iotclient')

# DESTROY
    @staticmethod
    def has_destroy_permission(request):
        return request.user.has_perm('api.delete_iotclient')

    @allow_staff_or_superuser
    @authenticated_users
    def has_object_destroy_permission(self, request):
        user_org = UserAccount.objects.filter(id=request.user.id).first()
        self_org = UserAccount.objects.filter(id=self.id).first()
        if (user_org and self_org and
            (self_org.organization.parent == user_org.organization or
            (self_org.organization.parent and self_org.organization.parent.parent == user_org.organization))
            and request.user.has_perm('api.delete_iotclient')):
            return True

        return False

# WRITE -groups together destroy, update and create
    # user can change own account - partially
    @staticmethod
    def has_write_permission(request):
        return True
    
    # def has_object_write_permission(self, request):
    #     return request.user == self

# META_DATA
    @staticmethod
    @authenticated_users
    def has_metadata_permission(request):
        return True

# ACTION -> Get Agristar2 Data
    @staticmethod
    @authenticated_users
    def has_agristar2_data_permission(request):
        return True

    @allow_staff_or_superuser
    def has_object_agristar2_data_permission(self, request):
        return self.has_object_read_permission(request)

# ACTION -> LEVEL1 POST
    @staticmethod
    @authenticated_users
    @allow_staff_or_superuser
    def has_agristar2_action_permission(request):
        user = request.user
        return user.has_perm('api.access_level1_IoTClient') or user.has_perm('api.access_level2_IoTClient')

    def has_object_agristar2_action_level1_permission(self, request):
        additional = get_additional_iotclients(request.user)
        # pylint: disable=no-member
        user = request.user
        user_org = user.organization
        try:
            if user.is_corporate_user() and self.site.owner.parent.parent == user_org:
                if user.has_perm('api.access_level1_IoTClient') or user.has_perm('api.access_level2_IoTClient'):
                    return True
            if user.is_dealer_user() and self.site.owner.parent == user_org:
                if user.has_perm('api.access_level1_IoTClient') or user.has_perm('api.access_level2_IoTClient'):
                    return True
            if user.is_customer_user() and self.site.owner == user_org and (user.has_perm('api.access_level1_IoTClient') or user.has_perm('api.access_level2_IoTClient')):
                return True
            if user.is_staff and user.has_perm('api.access_level1_IoTClient') or user.has_perm('api.access_level2_IoTClient'):
                return True
            if self in additional and (user.has_perm('api.access_level1_IoTClient') or user.has_perm('api.access_level2_IoTClient')):
                return True
            return user.is_superuser
        except:
            return False

# ACTION -> LEVEL2 POST
    def has_object_agristar2_action_level2_permission(self, request):
        additional = get_additional_iotclients(request.user)
        # pylint: disable=no-member
        user = request.user
        user_org = user.organization
        try:
            if (
                    user.is_corporate_user() and
                    self.site.owner.parent.parent == user_org and
                    user.has_perm('api.access_level2_IoTClient')
                ):
                    return True
            if (
                    user.is_dealer_user() and \
                    self.site.owner.parent == user_org and \
                    user.has_perm('api.access_level2_IoTClient')
                ):
                    return True
            if (
                    user.is_customer_user() and \
                    self.site.owner == user_org and \
                     user.has_perm('api.access_level2_IoTClient')
                ):
                    return True
            if (
                    user.is_staff and
                    user.has_perm('api.access_level2_IoTClient')
                ):
                    return True
            if self in additional and user.has_perm('api.access_level2_IoTClient'):
                return True
            return user.is_superuser
        except:
            return False

# ---------------- QUERYSET FILTERS --------------------------
class IoTClientFilterBackend(DRYPermissionFiltersBase):
    def filter_list_queryset(self, request, queryset, view):
        user = request.user
        new_queryset = None # queryset.filter(id=None)

        additional = get_additional_iotclients(user)

        try:
            if user.is_corporate_user():
                new_queryset = queryset.filter(site__owner__parent__parent=user.users_corporation())
            if user.is_dealer_user():
                new_queryset = queryset.filter(site__owner__parent=user.users_dealership())
            if user.is_customer_user():
                new_queryset = queryset.filter(site__owner=user.users_customer_account())
        except:
            print('IoTClient filter backend related field Nonetype error...')

        if additional:
            return queryset.union(additional) if user.is_superuser else new_queryset.union(additional)
        return queryset if user.is_superuser else new_queryset

def get_additional_iotclients(user):
    if UserSites.objects.filter(user=user).exists():
        my_sites = UserSites.objects.get(user=user)
        sites = my_sites.sites.all()
        clients = IoTClient.objects.filter(site__in=sites)
        return clients
    return None

def delete_azure_iothub_device(sender, instance, **kwargs):
    '''Delete the Azure IoT Hub device when the IoTClient is deleted'''
    # Skip in local development mode
    local_dev = getattr(django_settings, 'LOCAL_DEV', False)
    if local_dev:
        return
    if instance.id:
        delete_device(instance.id)

signals.post_delete.connect(delete_azure_iothub_device, sender=IoTClient)
