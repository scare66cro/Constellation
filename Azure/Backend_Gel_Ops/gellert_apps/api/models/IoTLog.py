from django.db import models
from .IoTClient import IoTClient

class IoTLog(models.Model):
    id = models.AutoField(primary_key=True)
    iot_client = models.ForeignKey(
        IoTClient, related_name='iot_client_logs', on_delete=models.CASCADE
    )
    time_stamp = models.DateTimeField()
    payload = models.JSONField()

    def __str__(self):
        # pylint: disable=no-member
        return  (self.iot_client.site.owner.name + ' - '
            + self.iot_client.site.name
            + ' - '  if self.iot_client.site is not None else 'Site unassigned - '
            + self.iot_client.client_type + ' - '
            + str(self.time_stamp)
        )

    class Meta:
        db_table = 'IoTLog'
        verbose_name = 'IoT Log'
