# 
# 
#    NOTE THIS IS TEMPORARY  
#    all serializers will end up with their own serializer file once authorizations are implemented
#    this is because the serializer classes will get more complicated
# 
from django.contrib.contenttypes.models import ContentType

from rest_framework import serializers
from dry_rest_permissions.generics import DRYPermissionsField
from .models import CorporateAccount, DealerAccount, CustomerAccount, \
    Site, SiteZone, SiteGroup, IoTClient, IoTLog, SiteEvent, http_message_queue, \
    upgrades, Organization, UpgradeFile, user_sites
from dry_rest_permissions.generics import DRYPermissionsField

class CorporateAccountSerializer(serializers.ModelSerializer):
    obj_permissions = DRYPermissionsField()

    class Meta:
        model = CorporateAccount.CorporateAccount
        fields = [
            'url',
            'id',
            'name',
            'parent',
            # 'subsidiaries',
            'is_active',
            'obj_permissions',
        ]

class DealerAccountSerializer(serializers.ModelSerializer):
    obj_permissions = DRYPermissionsField()

    class Meta:
        model = DealerAccount.DealerAccount
        fields = [
            'url',
            'id',
            'name',
            'parent',
            # 'subsidiaries',
            'is_active',
            'obj_permissions',
        ]

class CustomerAccountSerializer(serializers.ModelSerializer):
    obj_permissions = DRYPermissionsField()

    class Meta:
        model = CustomerAccount.CustomerAccount
        fields = [
            'url',
            'id',
            'name',
            'parent',
            'sites',
            # 'subsidiaries',
            'is_active',
            'obj_permissions',
        ]

# will have to write custom code for this to get the data exactly as we want it
class IoTLogSerializer(serializers.ModelSerializer):

    class Meta:
        model = IoTLog.IoTLog
        fields = [
            'iot_client',
            'time_stamp',
            'payload',
        ]

class HttpMessageQueueSerializer(serializers.ModelSerializer):
    class Meta:
        model = http_message_queue.HttpMessageQueue
        fields = [
            'id',
            'iot_client',
            'method',
            'payload',
        ]

class IoTClientSerializer(serializers.ModelSerializer):
    obj_permissions = DRYPermissionsField(additional_actions=['agristar2_data','agristar2_action_level1','agristar2_action_level2'])
    location_name = serializers.SerializerMethodField()
    
    class Meta:
        model = IoTClient.IoTClient
        fields = [
            'url',
            'id',
            'site',
            'name',
            'client_type',
            'unsecured_ip',
            'is_active',
            'location_id',
            'location_name',
            'obj_permissions',
            'front_matter',
            'time_stamp'
        ]
    
    def get_location_name(self, obj):
        """Get the location name for the serialized object"""
        return obj.get_location_name()

class OrganizationSerializer(serializers.ModelSerializer):
    obj_permissions = DRYPermissionsField()
    ctype = serializers.SerializerMethodField()
    class Meta:
        model = Organization.Organization
        fields = [
            'id',
            'name',
            'parent',
            'is_active',
            'obj_permissions',
            'ctype'
        ]

    def get_ctype(self, obj):
        return ContentType.objects.get_for_id(obj.polymorphic_ctype_id).name

class SiteZoneSerializer(serializers.ModelSerializer):
    class Meta:
        model = SiteZone.SiteZone
        fields = [
            # 'url',
            'id',
            'name',
            'site',
        ]

class SiteSerializer(serializers.ModelSerializer):
    obj_permissions = DRYPermissionsField()
    zones = SiteZoneSerializer(many=True)

    class Meta:
        model = Site.Site
        fields = [
            'url',
            'id',
            'category',
            'name',
            'owner',
            'is_active',
            'iot_devices',
            'zones',
            'obj_permissions',
        ]

class SiteGroupSerializer(serializers.ModelSerializer):
    obj_permissions = DRYPermissionsField()

    class Meta:
        model = SiteGroup.SiteGroup
        fields = [
            'url',
            'id',
            'name',
            'customer_account',
            'description',
            'sites',
            'obj_permissions',
        ]

class SiteEventSerializer(serializers.ModelSerializer):
    class Meta:
        model = SiteEvent.SiteEvent
        fields = '__all__'

class UpgradeSerializer(serializers.ModelSerializer):
    obj_permissions = DRYPermissionsField(additional_actions=['upgrade_data','upgrade_action'])
    class Meta:
        model = upgrades.Upgrade
    fields = [
        'version',
        'description',
        'payload',
        'obj_permissions',
    ]

class UpgradeFileSerializer(serializers.ModelSerializer):
    class Meta:
        model = UpgradeFile.UpgradeFile
    fields = [
        'id',
        'content',
        'size'
    ]

class UserSitesSerializer(serializers.ModelSerializer):
    obj_permissions = DRYPermissionsField()

    class Meta:
        model = user_sites.UserSites
        fields = [
            'user',
            'sites'
        ]
