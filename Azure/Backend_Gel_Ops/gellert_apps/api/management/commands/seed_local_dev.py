"""
Django management command to seed local development database.

Usage:
    python manage.py seed_local_dev

This creates:
    - A CorporateAccount (Gellert Corp)
    - A DealerAccount (Test Dealer)
    - A CustomerAccount (Test Customer)
    - A Site (Test Site)
    - An IoTClient with token for bridge testing
    - Optional: A test user (aaron@gellert.com)
"""

from django.core.management.base import BaseCommand
from django.contrib.auth import get_user_model
from gellert_apps.api.models.CorporateAccount import CorporateAccount
from gellert_apps.api.models.DealerAccount import DealerAccount
from gellert_apps.api.models.CustomerAccount import CustomerAccount
from gellert_apps.api.models.Site import Site
from gellert_apps.api.models.IoTClient import IoTClient
from gellert_apps.api.models.user_sites import UserSites
import uuid


class Command(BaseCommand):
    help = 'Seeds the local development database with test data'

    def add_arguments(self, parser):
        parser.add_argument(
            '--reset',
            action='store_true',
            help='Delete existing test data before creating new data',
        )

    def handle(self, *args, **options):
        self.stdout.write('Seeding local development database...')
        
        UserAccount = get_user_model()
        
        # Fixed UUIDs for consistent test data
        CORP_ID = uuid.UUID('11111111-1111-1111-1111-111111111111')
        DEALER_ID = uuid.UUID('22222222-2222-2222-2222-222222222222')
        CUSTOMER_ID = uuid.UUID('33333333-3333-3333-3333-333333333333')
        SITE_ID = uuid.UUID('44444444-4444-4444-4444-444444444444')
        CLIENT_ID = uuid.UUID('55555555-5555-5555-5555-555555555555')
        
        if options['reset']:
            self.stdout.write('Deleting existing test data...')
            IoTClient.objects.filter(id=CLIENT_ID).delete()
            Site.objects.filter(id=SITE_ID).delete()
            CustomerAccount.objects.filter(id=CUSTOMER_ID).delete()
            DealerAccount.objects.filter(id=DEALER_ID).delete()
            CorporateAccount.objects.filter(id=CORP_ID).delete()
        
        # 1. Create CorporateAccount
        corp, created = CorporateAccount.objects.get_or_create(
            id=CORP_ID,
            defaults={
                'name': 'Gellert Corp',
                'is_active': True,
            }
        )
        if created:
            self.stdout.write(self.style.SUCCESS(f'  Created CorporateAccount: {corp.name}'))
        else:
            self.stdout.write(f'  CorporateAccount already exists: {corp.name}')
        
        # 2. Create DealerAccount
        dealer, created = DealerAccount.objects.get_or_create(
            id=DEALER_ID,
            defaults={
                'name': 'Test Dealer',
                'is_active': True,
                'parent': corp,
            }
        )
        if created:
            self.stdout.write(self.style.SUCCESS(f'  Created DealerAccount: {dealer.name}'))
        else:
            self.stdout.write(f'  DealerAccount already exists: {dealer.name}')
        
        # 3. Create CustomerAccount
        customer, created = CustomerAccount.objects.get_or_create(
            id=CUSTOMER_ID,
            defaults={
                'name': 'Test Customer',
                'is_active': True,
                'parent': dealer,
            }
        )
        if created:
            self.stdout.write(self.style.SUCCESS(f'  Created CustomerAccount: {customer.name}'))
        else:
            self.stdout.write(f'  CustomerAccount already exists: {customer.name}')
        
        # 4. Create Site
        site, created = Site.objects.get_or_create(
            id=SITE_ID,
            defaults={
                'name': 'Test Site',
                'category': 'storage',
                'owner': customer,
                'is_active': True,
            }
        )
        if created:
            self.stdout.write(self.style.SUCCESS(f'  Created Site: {site.name}'))
        else:
            self.stdout.write(f'  Site already exists: {site.name}')
        
        # 5. Create IoTClient with known token for testing
        TEST_TOKEN = 'localdevtoken1'
        client, created = IoTClient.objects.get_or_create(
            id=CLIENT_ID,
            defaults={
                'name': 'Test Panel',
                'site': site,
                'client_type': 'agristar2',
                'token': TEST_TOKEN,
                'token_spent': False,
                'is_active': True,
            }
        )
        # Use register=False to skip Azure IoT Hub
        if created:
            client.save(register=False)
            self.stdout.write(self.style.SUCCESS(f'  Created IoTClient: {client.name}'))
            self.stdout.write(self.style.SUCCESS(f'    Token: {TEST_TOKEN}'))
            self.stdout.write(self.style.SUCCESS(f'    Device ID: {CLIENT_ID}'))
        else:
            self.stdout.write(f'  IoTClient already exists: {client.name}')
            self.stdout.write(f'    Token: {client.token}')
        
        # 6. Create test user if not exists
        test_email = 'aaron@gellert.com'
        if not UserAccount.objects.filter(username=test_email).exists():
            user = UserAccount.objects.create_user(
                username=test_email,
                email=test_email,
                password='admin123',
                is_staff=True,
                is_superuser=True,
                organization=customer,
            )
            self.stdout.write(self.style.SUCCESS(f'  Created User: {test_email} / admin123'))
            
            # Grant user access to the test site
            user_sites, _ = UserSites.objects.get_or_create(user=user)
            user_sites.sites.add(site)
            self.stdout.write(self.style.SUCCESS(f'    Granted access to Site: {site.name}'))
        else:
            user = UserAccount.objects.get(username=test_email)
            self.stdout.write(f'  User already exists: {test_email}')
            # Ensure user has access to site
            user_sites, _ = UserSites.objects.get_or_create(user=user)
            if site not in user_sites.sites.all():
                user_sites.sites.add(site)
                self.stdout.write(self.style.SUCCESS(f'    Granted access to Site: {site.name}'))
        
        self.stdout.write('')
        self.stdout.write(self.style.SUCCESS('Local development database seeded successfully!'))
        self.stdout.write('')
        self.stdout.write('Bridge configuration:')
        self.stdout.write(f'  DJANGO_URL=http://localhost:8000')
        self.stdout.write(f'  DJANGO_TOKEN={TEST_TOKEN}')
        self.stdout.write(f'  DJANGO_DEVICE_ID={CLIENT_ID}')
        self.stdout.write('')
        self.stdout.write('Login credentials:')
        self.stdout.write(f'  Email: {test_email}')
        self.stdout.write(f'  Password: admin123')
