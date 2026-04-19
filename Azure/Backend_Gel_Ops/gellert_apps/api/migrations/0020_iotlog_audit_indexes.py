"""Indexes to make audit queries on IoTLog cheap.

- GIN index on payload (jsonb_path_ops) for arbitrary payload->>'...' filters
- Partial B-tree on (iot_client_id, time_stamp DESC) WHERE payload->>'kind' = 'audit'
  to keep the 'list this device's audits, newest first' query O(log n).
"""

from django.db import migrations


class Migration(migrations.Migration):

    dependencies = [
        ('api', '0019_devicelink'),
    ]

    operations = [
        migrations.RunSQL(
            sql=[
                'CREATE INDEX IF NOT EXISTS iotlog_payload_gin '
                'ON "IoTLog" USING GIN (payload jsonb_path_ops);',
                'CREATE INDEX IF NOT EXISTS iotlog_audit_recent '
                'ON "IoTLog" (iot_client_id, time_stamp DESC) '
                "WHERE (payload->>'kind') = 'audit';",
            ],
            reverse_sql=[
                'DROP INDEX IF EXISTS iotlog_audit_recent;',
                'DROP INDEX IF EXISTS iotlog_payload_gin;',
            ],
        ),
    ]
