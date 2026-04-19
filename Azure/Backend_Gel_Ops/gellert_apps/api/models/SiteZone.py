from polymorphic.models import PolymorphicModel
from django.db import models
# from django.contrib.gis.geos import Point
import uuid 
from .Site import Site

class SiteZone(PolymorphicModel):
    id = models.UUIDField(primary_key=True, default=uuid.uuid4, editable=False)
    name = models.CharField(max_length=50)
    site = models.ForeignKey(Site, related_name='zones', on_delete=models.CASCADE)

    def __str__(self):
        # pylint: disable=no-member
        return self.site.owner.name + ' - ' + self.site.name + ' - ' + self.name

    class Meta:
        db_table = 'SiteZone'
        verbose_name = 'Site Zone'