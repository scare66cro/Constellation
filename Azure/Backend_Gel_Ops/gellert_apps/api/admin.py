from django.contrib import admin
from django import forms
from django.utils.safestring import mark_safe
from django.urls import reverse
from django.utils.html import format_html

from .models import Site, SiteEvent, SiteDealerEvent, IoTClient, SiteZone, SiteGroup, \
    CorporateAccount, DealerAccount, CustomerAccount, upgrades, user_sites, IoTLog
from .location_service import LocationAPIService

# Custom widget for searchable location dropdown
class SearchableLocationWidget(forms.TextInput):
    """Custom widget that provides autocomplete functionality for locations"""
    
    def __init__(self, attrs=None):
        default_attrs = {
            'class': 'location-autocomplete',
            'autocomplete': 'off',
            'placeholder': 'Start typing to search locations...'
        }
        if attrs:
            default_attrs.update(attrs)
        super().__init__(default_attrs)
    
    class Media:
        css = {
            'all': ('admin/css/location_autocomplete.css',)
        }
        js = ('admin/js/location_autocomplete.js',)

# Custom form for IoTClient with location dropdown
class IoTClientAdminForm(forms.ModelForm):
    location_choice = forms.CharField(
        required=False,
        label="Location",
        widget=SearchableLocationWidget(),
        help_text="Start typing to search for locations"
    )
    
    class Meta:
        model = IoTClient.IoTClient
        fields = '__all__'
    
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
          # Set the current value if location_id is set
        if self.instance and self.instance.location_id:
            try:
                location_service = LocationAPIService()
                location = location_service.get_location_by_id(int(self.instance.location_id))
                if location:
                    self.fields['location_choice'].initial = location.get('name', '')
            except (ValueError, Exception):
                pass
    
    def clean_location_choice(self):
        """Validate and convert location name to location_id - strict validation only"""
        location_name = self.cleaned_data.get('location_choice')
        if not location_name:
            return ''
        
        # Try to find the location by exact name match
        location_service = LocationAPIService()
        locations = location_service.get_all_locations()
        
        for i, location in enumerate(locations):
            display_name = location.get('name', '')
            
            if display_name == location_name:
                return str(location.get('id', ''))
            
            # Also check for whitespace issues
            if display_name.strip() == location_name.strip():
                return str(location.get('id', ''))
        
        # If no exact match found, log all available names for debugging
        all_names = [loc.get('name', 'NO_NAME') for loc in locations]
        
        # Check if there are similar matches for better error message
        similar_matches = []
        location_name_lower = location_name.lower().strip()
        for location in locations:
            display_name = location.get('name', '').lower().strip()
            if location_name_lower in display_name or display_name in location_name_lower:
                similar_matches.append(location.get('name', ''))
        
        error_msg = f"Invalid location: '{location_name}'. Please select a valid location from the dropdown list."
        if similar_matches:
            error_msg += f" Similar matches found: {', '.join(similar_matches[:3])}"
        
        # Strict validation: raise error if location not found in database
        raise forms.ValidationError(error_msg)
    
    def save(self, commit=True):
        instance = super().save(commit=False)
        
        # Update location_id based on the validated location_choice
        location_choice = self.cleaned_data.get('location_choice')
        if location_choice and location_choice.isdigit():
            # Only store numeric IDs (validated locations)
            instance.location_id = location_choice
        else:
            # Clear location_id if no valid selection
            instance.location_id = None
            
        if commit:
            instance.save()
        return instance

# Register your models here.
admin.site.register(CorporateAccount.CorporateAccount)
admin.site.register(SiteZone.SiteZone)
admin.site.register(SiteEvent.SiteEvent)
admin.site.register(SiteDealerEvent.SiteDealerEvent)
admin.site.register(SiteGroup.SiteGroup)
admin.site.register(upgrades.Upgrade)

@admin.register(IoTClient.IoTClient)
class IoTClientAdmin(admin.ModelAdmin):
    '''
    Specify ordering and other field settings
    '''
    form = IoTClientAdminForm
    exclude = ("id", "location_id")  # Exclude location_id from the admin panel
    readonly_fields = ("id",)
    list_display = ('name', 'site', 'client_type', 'get_location_name', 'device_available', 'is_active')
    ordering = ('site__owner__name', 'site__name', 'name',)
    
    fieldsets = (
        (None, {
            'fields': ('site', 'name', 'client_type', 'unsecured_ip', 'is_active')
        }),
        ('Location Information', {
            'fields': ('location_choice',),  # Only show the dropdown for location selection
            'description': 'Use the Location dropdown to assign a location.'
        }),
        ('Advanced Settings', {
            'fields': ('time_stamp', 'time_zone', 'iot_hub_name', 'token', 'token_spent', 'id'),
        }),
        ('Log Information', {
            'fields': ('last_log', 'front_matter', 'realtime', 'settings'),
            'classes': ('collapse',)
        }),
    )
    
    def get_location_name(self, obj):
        """Display the location name in the admin list view"""
        if obj.location_id:
            try:
                location_service = LocationAPIService()
                location = location_service.get_location_by_id(int(obj.location_id))
                if location:
                    return location.get('name', f'Unknown (ID: {obj.location_id})')
                else:
                    return mark_safe(f'<span style="color: red;">Not Found (ID: {obj.location_id})</span>')
            except (ValueError, Exception):
                return mark_safe(f'<span style="color: orange;">Invalid ID: {obj.location_id}</span>')
        return '-'
    
    get_location_name.short_description = 'Location Name'
    get_location_name.admin_order_field = 'location_id'

    def get_deleted_objects(self, objs, request):
        deleted_objects = [str(obj) for obj in objs]
        model_count = { "IoTLogs": IoTLog.IoTLog.objects.filter(iot_client__in=objs).count() }
        perms_needed = []
        protected = []
        return (deleted_objects, model_count, perms_needed, protected)

@admin.register(CustomerAccount.CustomerAccount)
class CustomerAccountAdmin(admin.ModelAdmin):
    '''
    Specify the ordering for the customer account
    '''
    ordering = ('name',)

@admin.register(DealerAccount.DealerAccount)
class DealerAccountAdmin(admin.ModelAdmin):
    '''
    Specify the ordering for the dealer account
    '''
    ordering = ('name',)

@admin.register(Site.Site)
class SiteAdmin(admin.ModelAdmin):
    '''
    Ordering for the SiteAdmin
    '''
    ordering = ('owner__name', 'name')

@admin.register(user_sites.UserSites)
class UserSitesAdmin(admin.ModelAdmin):
    list_display = ['get_user_name']
    filter_horizontal = ['sites']

    def get_user_name(self, obj):
        return obj.user.username
    get_user_name.short_description = 'User Name'
