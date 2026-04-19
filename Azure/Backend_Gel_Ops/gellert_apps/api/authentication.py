"""
Custom authentication for bridge/IoTClient endpoints.

IoTClientTokenAuthentication validates the token parameter against known IoTClients.
This is used for device-to-cloud communication where session auth isn't available.
"""

from rest_framework import authentication
from rest_framework import exceptions
from .models.IoTClient import IoTClient


class IoTClientTokenAuthentication(authentication.BaseAuthentication):
    """
    Token-based authentication for IoTClient devices.
    
    Looks for token in:
    1. Authorization header: "Token <token>"
    2. Query param: ?token=<token>
    3. Request body: {"token": "<token>"}
    
    On success, attaches the IoTClient to request.auth and a pseudo-user to request.user.
    """
    
    def authenticate(self, request):
        token = self._get_token(request)
        
        if not token:
            return None  # No token provided, let other auth methods try
        
        try:
            client = IoTClient.objects.select_related('site').get(token=token)
        except IoTClient.DoesNotExist:
            raise exceptions.AuthenticationFailed('Invalid token')

        if not client.is_active:
            raise exceptions.AuthenticationFailed('Device is inactive')

        # Return (user, auth) tuple
        # user is a pseudo-user object for DRF compatibility
        return (IoTClientUser(client), client)
    
    def _get_token(self, request):
        """Extract token from various locations."""
        # 1. Check Authorization header
        auth_header = request.META.get('HTTP_AUTHORIZATION', '')
        if auth_header.startswith('Token '):
            return auth_header[6:].strip()
        
        # 2. Check query params
        token = request.query_params.get('token')
        if token:
            return token
        
        # 3. Check request body (for POST requests)
        if hasattr(request, 'data') and isinstance(request.data, dict):
            token = request.data.get('token')
            if token:
                return token
        
        return None


class IoTClientUser:
    """
    Pseudo-user object representing an authenticated IoTClient device.
    Satisfies DRF's requirement for request.user to be truthy.
    """
    
    def __init__(self, iot_client):
        self.iot_client = iot_client
        self.is_authenticated = True
        self.is_active = iot_client.is_active
        self.id = str(iot_client.id)
        self.pk = str(iot_client.id)
    
    def __str__(self):
        return f"IoTClient:{self.iot_client.name}"
    
    def __bool__(self):
        return True


class IsIoTClientAuthenticated:
    """
    Permission class that requires IoTClientTokenAuthentication.
    Use this as permission_classes for bridge endpoints.
    """
    
    def has_permission(self, request, view):
        return (
            request.auth is not None 
            and isinstance(request.auth, IoTClient)
        )
