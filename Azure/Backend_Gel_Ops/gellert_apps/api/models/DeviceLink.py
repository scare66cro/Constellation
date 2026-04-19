"""Device ↔ UserAccount link.

A DeviceLink binds an IoTClient (one Constellation panel) to a Django
UserAccount, allowing that user to sign in remotely through the bridge
without using the legacy basic[8] "Remote Login Password".

Each link carries:
- an opaque ``link_token`` the bridge stores locally and presents on
  subsequent remote-login calls
- an optional ``local_slot`` (0-9) when the cloud user maps to one of
  the ten local ARM slots; cloud-only admins leave it null
- a cached ``role`` ('operator'|'admin') mirroring what the local
  accounts page shows, so the bridge can decide access level offline
"""

import uuid

from django.conf import settings as django_settings
from django.db import models

from .IoTClient import IoTClient


def _default_link_token() -> str:
    # 32 hex chars is plenty; bridge treats this as opaque.
    return uuid.uuid4().hex + uuid.uuid4().hex[:16]


class DeviceLink(models.Model):
    ROLE_CHOICES = (
        ('operator', 'Operator'),
        ('admin', 'Admin'),
    )

    id = models.UUIDField(primary_key=True, default=uuid.uuid4, editable=False)
    iot_client = models.ForeignKey(
        IoTClient,
        related_name='device_links',
        on_delete=models.CASCADE,
    )
    user = models.ForeignKey(
        django_settings.AUTH_USER_MODEL,
        related_name='device_links',
        on_delete=models.CASCADE,
    )
    link_token = models.CharField(
        max_length=96,
        unique=True,
        default=_default_link_token,
        db_index=True,
    )
    role = models.CharField(max_length=16, choices=ROLE_CHOICES, default='admin')
    local_slot = models.SmallIntegerField(null=True, blank=True)
    created_at = models.DateTimeField(auto_now_add=True)
    last_remote_login = models.DateTimeField(null=True, blank=True)
    last_remote_login_ip = models.CharField(max_length=64, null=True, blank=True)
    revoked = models.BooleanField(default=False)

    class Meta:
        db_table = 'DeviceLink'
        unique_together = (('iot_client', 'user'),)

    def __str__(self):
        return f'DeviceLink({self.iot_client_id} ↔ {self.user_id})'
