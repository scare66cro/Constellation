"""Device ↔ Cloud account link API.

Endpoints consumed by the Constellation bridge (``/iot/cloud/*`` on the
bridge forwards here). The bridge authenticates itself using its own
IoTClient token just like the sync endpoints. For ``device_link_create``
the caller ALSO supplies Django user credentials which we verify inline
because at link time the cloud user has no session yet.

All responses are safe for the bridge to cache in
``account-meta.json → cloudLinks[]``.
"""

from django.contrib.auth import authenticate
from django.utils import timezone
from rest_framework import status
from rest_framework.decorators import (
    api_view,
    authentication_classes,
    permission_classes,
)
from rest_framework.permissions import AllowAny, IsAuthenticated
from rest_framework.response import Response

from ..authentication import IoTClientTokenAuthentication
from ..models.DeviceLink import DeviceLink
from ..models.IoTClient import IoTClient


def _serialize_link(link: DeviceLink) -> dict:
    u = link.user
    display = (u.get_full_name() or '').strip() or u.username
    return {
        'id': str(link.id),
        'cloudUserId': str(u.id),
        'username': u.username,
        'displayName': display,
        'email': u.email or '',
        'role': link.role,
        'localSlot': link.local_slot,
        'linkToken': link.link_token,
        'linkedAt': link.created_at.isoformat(),
        'lastRemoteLogin': link.last_remote_login.isoformat() if link.last_remote_login else None,
        'revoked': link.revoked,
    }


def _client_ip(request):
    xff = request.META.get('HTTP_X_FORWARDED_FOR', '')
    if xff:
        return xff.split(',')[0].strip()
    return request.META.get('REMOTE_ADDR')


def _get_client_by_token(request) -> IoTClient | None:
    token = request.data.get('deviceToken') or request.data.get('token')
    if not token:
        return None
    try:
        return IoTClient.objects.get(token=token)
    except IoTClient.DoesNotExist:
        return None


@api_view(['POST'])
@permission_classes([AllowAny])
def device_link_create(request):
    """Create (or refresh) a DeviceLink.

    Body: ``{deviceToken, username, password, localSlot?, role?}``

    Verifies the device token and user credentials inline, then
    upserts a DeviceLink row. Returns the full serialized link on
    success, including the opaque ``linkToken`` the bridge will store
    for later remote-login calls.
    """
    client = _get_client_by_token(request)
    if client is None:
        return Response({'error': 'Invalid device token'}, status=status.HTTP_401_UNAUTHORIZED)

    username = request.data.get('username')
    password = request.data.get('password')
    if not username or not password:
        return Response({'error': 'username and password required'}, status=status.HTTP_400_BAD_REQUEST)

    user = authenticate(username=username, password=password)
    if user is None or not user.is_active:
        return Response({'error': 'Invalid credentials'}, status=status.HTTP_401_UNAUTHORIZED)

    role = request.data.get('role') or 'admin'
    if role not in ('operator', 'admin'):
        role = 'admin'

    local_slot = request.data.get('localSlot')
    if local_slot is not None:
        try:
            local_slot = int(local_slot)
            if local_slot < 0 or local_slot > 9:
                local_slot = None
        except (TypeError, ValueError):
            local_slot = None

    link, _ = DeviceLink.objects.get_or_create(
        iot_client=client,
        user=user,
        defaults={'role': role, 'local_slot': local_slot},
    )
    # Always refresh these on re-link so the user can rotate the role/slot.
    link.role = role
    link.local_slot = local_slot
    link.revoked = False
    link.save(update_fields=['role', 'local_slot', 'revoked'])

    return Response(_serialize_link(link))


@api_view(['POST'])
@authentication_classes([IoTClientTokenAuthentication])
@permission_classes([IsAuthenticated])
def device_link_list(request):
    """List DeviceLinks for the calling device.

    Body: ``{token}`` (standard IoTClient token auth).
    """
    client = request.auth
    links = DeviceLink.objects.filter(iot_client=client, revoked=False).select_related('user')
    return Response({'links': [_serialize_link(l) for l in links]})


@api_view(['POST'])
@authentication_classes([IoTClientTokenAuthentication])
@permission_classes([IsAuthenticated])
def device_link_delete(request):
    """Revoke a DeviceLink.

    Body: ``{token, cloudUserId}``.
    """
    client = request.auth
    cloud_user_id = request.data.get('cloudUserId')
    if not cloud_user_id:
        return Response({'error': 'cloudUserId required'}, status=status.HTTP_400_BAD_REQUEST)
    try:
        link = DeviceLink.objects.get(iot_client=client, user_id=cloud_user_id)
    except DeviceLink.DoesNotExist:
        return Response({'error': 'Link not found'}, status=status.HTTP_404_NOT_FOUND)
    link.revoked = True
    link.save(update_fields=['revoked'])
    return Response({'ok': True})


@api_view(['POST'])
@permission_classes([AllowAny])
def device_remote_login(request):
    """Verify a linkToken and return the bound user info.

    Body: ``{deviceToken, linkToken}``. Used by the bridge when a cloud
    admin presents their stored token to sign in without re-entering
    a password.
    """
    client = _get_client_by_token(request)
    if client is None:
        return Response({'valid': False, 'error': 'Invalid device token'},
                        status=status.HTTP_401_UNAUTHORIZED)

    link_token = request.data.get('linkToken')
    if not link_token:
        return Response({'valid': False, 'error': 'linkToken required'},
                        status=status.HTTP_400_BAD_REQUEST)

    try:
        link = DeviceLink.objects.select_related('user').get(
            link_token=link_token, iot_client=client, revoked=False,
        )
    except DeviceLink.DoesNotExist:
        return Response({'valid': False, 'error': 'Unknown or revoked link'},
                        status=status.HTTP_404_NOT_FOUND)

    if not link.user.is_active:
        return Response({'valid': False, 'error': 'User inactive'},
                        status=status.HTTP_403_FORBIDDEN)

    link.last_remote_login = timezone.now()
    link.last_remote_login_ip = _client_ip(request)
    link.save(update_fields=['last_remote_login', 'last_remote_login_ip'])

    data = _serialize_link(link)
    data['valid'] = True
    return Response(data)


@api_view(['POST'])
@permission_classes([AllowAny])
def device_password_login(request):
    """Verify Django user credentials and require an existing DeviceLink.

    Body: ``{deviceToken, username, password}``.

    Unlike :func:`device_link_create` this does NOT auto-create a link;
    the caller must already have been registered on the device by an
    admin. Returns the serialized link (including ``linkToken``) on
    success so the bridge can cache it for offline fallback.
    """
    client = _get_client_by_token(request)
    if client is None:
        return Response({'valid': False, 'error': 'Invalid device token'},
                        status=status.HTTP_401_UNAUTHORIZED)

    username = request.data.get('username')
    password = request.data.get('password')
    if not username or not password:
        return Response({'valid': False, 'error': 'username and password required'},
                        status=status.HTTP_400_BAD_REQUEST)

    user = authenticate(username=username, password=password)
    if user is None or not user.is_active:
        return Response({'valid': False, 'error': 'Invalid credentials'},
                        status=status.HTTP_401_UNAUTHORIZED)

    try:
        link = DeviceLink.objects.get(iot_client=client, user=user, revoked=False)
    except DeviceLink.DoesNotExist:
        return Response({'valid': False, 'error': 'Not linked to this device'},
                        status=status.HTTP_403_FORBIDDEN)

    link.last_remote_login = timezone.now()
    link.last_remote_login_ip = _client_ip(request)
    link.save(update_fields=['last_remote_login', 'last_remote_login_ip'])

    data = _serialize_link(link)
    data['valid'] = True
    return Response(data)
