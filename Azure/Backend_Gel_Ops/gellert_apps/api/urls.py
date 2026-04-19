from django.urls import path, include
# from .views import CustomerAccount
from rest_framework import routers
from gellert_apps.api.views.CustomerAccountView import CustomerAccountViewSet
# pylint: disable=import-error
from gellert_apps.user_account_app.views import UserAccountViewSet
from gellert_apps.api import views_temporary as api_views
from gellert_apps.api.views import OrganizationView, iot_client, \
    upgrades_view, SiteView, http_message_queue_view, user_sites_view, location_view, bridge_sync, \
    device_link_api

router = routers.DefaultRouter()
router.register(r'user-accounts', UserAccountViewSet)
router.register(r'corporate-accounts', api_views.CorporateAccountViewSet)
router.register(r'dealer-accounts', api_views.DealerAccountViewSet)
router.register(r'customer-accounts', CustomerAccountViewSet)
router.register(r'sites', SiteView.SiteViewSet)
router.register(r'site-groups', api_views.SiteGroupViewSet)
router.register(r'iot-clients', iot_client.IoTClientViewSet)
router.register(r'iot-logs', api_views.IoTLogViewSet)
router.register(r'site-events', api_views.SiteEventViewSet)
router.register(r'upgrades', upgrades_view.UpgradesViewSet)
router.register(r'organizations', OrganizationView.OrganizationViewSet)
router.register(r'usersites', user_sites_view.UserSitesViewSet)
router.register(r'locations', location_view.LocationAPIViewSet, basename='location')

urlpatterns = [
    path('', include(router.urls)),
    # Agristar2 actions Endpoint
    # path('iot-clients/<client_id>/agristar2-action',
    #   IoTClient.Agristar2Action,
    #   name='Agristar2Action'),
    path(
        'iot-clients/connection-string',
        iot_client.GetIoTHubConnectionString,
        name='GetIoTHubConnectionString'
    ),
    path(
        'iot-clients/httpdata',
        iot_client.httpdata,
        name='httpdata'
    ),
    path(
        'msg-queue/<int:pk>/validation/',
        http_message_queue_view.validation,
        name='validation'
    ),
    path(
        'msg-queue/query/',
        http_message_queue_view.query,
        name='query'
    ),
    # Bridge server sync endpoints (replaces iotclient → Azure IoT Hub path)
    path(
        'bridge/sync/',
        bridge_sync.bridge_sync,
        name='bridge-sync'
    ),
    path(
        'bridge/commands/',
        bridge_sync.bridge_commands,
        name='bridge-commands'
    ),
    path(
        'bridge/verify/',
        bridge_sync.verify_token,
        name='bridge-verify'
    ),
    path(
        'bridge/register/',
        bridge_sync.bridge_register,
        name='bridge-register'
    ),
    path(
        'bridge/command-ack/',
        bridge_sync.command_ack,
        name='bridge-command-ack'
    ),
    path(
        'bridge/data/<uuid:iot_client_id>/',
        bridge_sync.bridge_data,
        name='bridge-data'
    ),
    path(
        'bridge/upgrade/latest/',
        bridge_sync.bridge_upgrade_info,
        name='bridge-upgrade-info'
    ),
    path(
        'bridge/upgrade/<str:version>/payload/',
        bridge_sync.bridge_upgrade_payload,
        name='bridge-upgrade-payload'
    ),
    # Device ↔ UserAccount cloud-identity linkage (consumed by bridge /iot/cloud/*)
    path('bridge/device-link/create/', device_link_api.device_link_create, name='device-link-create'),
    path('bridge/device-link/list/',   device_link_api.device_link_list,   name='device-link-list'),
    path('bridge/device-link/delete/', device_link_api.device_link_delete, name='device-link-delete'),
    path('bridge/device-link/remote-login/', device_link_api.device_remote_login, name='device-remote-login'),
    path('bridge/device-link/password-login/', device_link_api.device_password_login, name='device-password-login'),
    # Accountability audit: bridge ingest + dashboard query
    path('bridge/audit/', bridge_sync.bridge_audit, name='bridge-audit'),
    path('iot-audit/',    bridge_sync.audit_query, name='iot-audit'),
]
