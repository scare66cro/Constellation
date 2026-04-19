from polymorphic.models import PolymorphicModel
from django.db import models
import uuid
# pylint: disable=import-error
from gellert_project.settings import AUTH_USER_MODEL
from .Site import Site

class SiteEvent(PolymorphicModel):
    id = models.UUIDField(primary_key=True, default=uuid.uuid4, editable=False)
    site = models.ForeignKey(Site, related_name='site_events', null=True, blank=True, on_delete=models.SET_NULL)

    category = models.CharField(max_length=50) # sortable field 'chem application' get all chem applications 
    title = models.CharField(max_length=50) # easy quick custom to event instance description 'sprout nip 100lbs'
    notes = models.TextField(null=True, blank=True) # through out model types should remain place to store customer facing event description

    started_at = models.DateTimeField(blank=True, null=True) 
    ended_at = models.DateTimeField(blank=True, null=True)

    created_at_server = models.DateTimeField(auto_now_add=True)
    updated_at_server = models.DateTimeField(auto_now=True)

    dismissed = models.BooleanField(default=False)

    created_by = models.ForeignKey(AUTH_USER_MODEL, related_name='my_site_events', on_delete=models.SET_NULL, null=True)
    updated_by = models.ForeignKey(AUTH_USER_MODEL, related_name='recent_event_updates', on_delete=models.SET_NULL, null=True)

    def __str__(self):
        return self.category + ' - ' + self.title

    class Meta:
        db_table = 'SiteEvent'
        verbose_name = 'Site Event'