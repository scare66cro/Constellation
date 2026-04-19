from .SiteEvent import SiteEvent
from django.db import models
from .DealerAccount import DealerAccount

class SiteDealerEvent(SiteEvent):
    dealer = models.ForeignKey(DealerAccount, related_name='dealer_events', null=True, on_delete=models.SET_NULL)

    def __str__(self):
        return self.dealer.name + ' - ' + self.category + ' - ' + self.title

    class Meta:
        db_table = 'SiteDealerEvent'
        verbose_name = 'Site Dealer Event'