"""DeviceLink: bind an IoTClient to a UserAccount for cloud sign-in."""

import uuid

import django.db.models.deletion
from django.conf import settings
from django.db import migrations, models

import gellert_apps.api.models.DeviceLink as device_link_module


class Migration(migrations.Migration):

    dependencies = [
        ('api', '0018_add_created_at_to_httpmessagequeue'),
        migrations.swappable_dependency(settings.AUTH_USER_MODEL),
    ]

    operations = [
        migrations.CreateModel(
            name='DeviceLink',
            fields=[
                ('id', models.UUIDField(default=uuid.uuid4, editable=False, primary_key=True, serialize=False)),
                ('link_token', models.CharField(db_index=True, default=device_link_module._default_link_token, max_length=96, unique=True)),
                ('role', models.CharField(choices=[('operator', 'Operator'), ('admin', 'Admin')], default='admin', max_length=16)),
                ('local_slot', models.SmallIntegerField(blank=True, null=True)),
                ('created_at', models.DateTimeField(auto_now_add=True)),
                ('last_remote_login', models.DateTimeField(blank=True, null=True)),
                ('last_remote_login_ip', models.CharField(blank=True, max_length=64, null=True)),
                ('revoked', models.BooleanField(default=False)),
                ('iot_client', models.ForeignKey(on_delete=django.db.models.deletion.CASCADE, related_name='device_links', to='api.iotclient')),
                ('user', models.ForeignKey(on_delete=django.db.models.deletion.CASCADE, related_name='device_links', to=settings.AUTH_USER_MODEL)),
            ],
            options={
                'db_table': 'DeviceLink',
                'unique_together': {('iot_client', 'user')},
            },
        ),
    ]
