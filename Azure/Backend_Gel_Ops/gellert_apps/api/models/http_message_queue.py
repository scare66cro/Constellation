''' Handle http message queue for settings and actions '''
from django.db import models

class HttpMessageQueue(models.Model):
    ''' Message queue to store settings and actions '''
    id = models.AutoField(primary_key=True)
    iot_client = models.ForeignKey('IoTClient', on_delete=models.CASCADE)

    # settings or action
    method = models.CharField(max_length=25, default='settings', blank=False, null=False)

    payload = models.JSONField()
    is_processed = models.BooleanField(default=False, db_index=True)
    validation = models.JSONField(null=True)
    created_at = models.DateTimeField(auto_now_add=True, null=True)  # When command was queued
    objects = models.Manager()

    class Meta:
        '''Define database name'''
        db_table = 'HttpMessageQueue'
        verbose_name = 'Http Message Queue'
