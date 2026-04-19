"""
    Production Settings for the application
"""
import os
from .settings import *
from .secrets import azure_db_pass

DEBUG = False

SECRET_KEY = os.environ['TOKEN_SECRET_KEY']

# Configure the domain name using the environment variable
# that Azure automatically creates for us.
ALLOWED_HOSTS = [ os.environ['WEBSITE_HOSTNAME'], 'agristor-companies.com'] \
    if 'WEBSITE_HOSTNAME' in os.environ else []

# WhiteNoise configuration
# MIDDLEWARE.append('whitenoise.middleware.WhiteNoiseMiddleware',)

INSTALLED_APPS = [
    'django.contrib.admin',
    'django.contrib.auth',
    'django.contrib.contenttypes',
    'django.contrib.sessions',
    'django.contrib.messages',
    'django.contrib.staticfiles',

    'whitenoise.runserver_nostatic', #this will handle all the static files served from django
    'polymorphic',
    # 'rest_framework_simplejwt.token_blacklist',

    'rest_framework',
    'dry_rest_permissions',
    'gellert_apps.django_saml_sp',
    'gellert_apps.user_account_app',
    'gellert_apps.api'
]

MIDDLEWARE = [
    'django.middleware.security.SecurityMiddleware',
# Add whitenoise middleware after the security middleware
    'whitenoise.middleware.WhiteNoiseMiddleware',
    'django.contrib.sessions.middleware.SessionMiddleware',
    'django.middleware.common.CommonMiddleware',
    'django.middleware.csrf.CsrfViewMiddleware',
    'django.contrib.auth.middleware.AuthenticationMiddleware',
    'django.contrib.messages.middleware.MessageMiddleware',
    'django.middleware.clickjacking.XFrameOptionsMiddleware',
]

REST_FRAMEWORK = {
    **REST_FRAMEWORK,
    'DEFAULT_RENDERER_CLASSES': (
        'rest_framework.renderers.JSONRenderer',
    )
}

STATICFILES_STORAGE = 'whitenoise.storage.CompressedManifestStaticFilesStorage'
STATIC_ROOT = os.path.join(BASE_DIR, 'staticfiles')

USE_X_FORWARDED_PORT = True
USE_X_FORWARDED_HOST = True

# JWT---------------------
# used below commands to generate privata & public keys
    # openssl genrsa -out rsa-key 2048
    # openssl rsa -in rsa-key -pubout > rsa-key.pub

# must also enable token_blacklist in installed_apps
# SIMPLE_JWT = {
#     'ALGORITHM': 'RS256',
#     'SIGNING_KEY': open(os.path.join(sys.path[0], "rsa-key"), "r").read(),
#     'VERIFYING_KEY': open(os.path.join(sys.path[0], "rsa-key.pub"), "r").read(),

#     # 'ROTATE_REFRESH_TOKENS': False,
#     # 'BLACKLIST_AFTER_ROTATION': True,
# }

# DBHOST is only the server name, not the full URL
hostname = os.environ['DBHOST']

SP_SETTINGS = {
    "strict": True,
    "debug": False,
    "sp": {
        "entityId": os.environ['SP_ENTITYID'],
        "assertionConsumerService": {
            "url": os.environ['SP_ACS'],
            "binding": "urn:oasis:names:tc:SAML:2.0:bindings:HTTP-POST",
        },
        "singleLogoutService": {
            "url": os.environ['SP_SLS'],
            "binding": "urn:oasis:names:tc:SAML:2.0:bindings:HTTP-Redirect"
        },
        "NameIDFormat": "urn:oasis:names:tc:SAML:1.1:nameid-format:emailAddress"
    },
    "idp": {
        "entityId": os.environ['IDP_ENTITYID'],
        "singleSignOnService": {
            "url": os.environ['IDP_SSO'],
            "binding": "urn:oasis:names:tc:SAML:2.0:bindings:HTTP-Redirect"
        },
        "singleLogoutService": {
            "url": os.environ['IDP_SLS'],
            "binding": "urn:oasis:names:tc:SAML:2.0:bindings:HTTP-Redirect"
        },
        "x509cert": os.environ['IDP_CERT'],
    }
}

# which we construct using the DBHOST value.
DATABASES = {
    'default': {
        'ENGINE': 'django.db.backends.postgresql',
        'NAME': os.environ['DBNAME'], #default name in prostgres is 'postgres'
        'HOST': hostname + ".postgres.database.azure.com",
        'USER': os.environ['DBUSER'],
        'PASSWORD': azure_db_pass,
        # forces django to connect to DB with SSL
        'OPTIONS': {'sslmode': 'require'},
    }
}

LOGIN_REDIRECT_URL = os.environ['LOGIN_REDIRECT_URL']

AUTHENTICATION_BACKENDS = (
    'gellert_apps.django_saml_sp.backends.SAMLServiceProviderBackend',
    'django.contrib.auth.backends.ModelBackend'
)

CSRF_TRUSTED_ORIGINS = [os.environ['DOMAINS']]

DEFAULT_AUTO_FIELD = 'django.db.models.BigAutoField'

DEFAULT_IOT_HUB = os.environ['DEFAULT_IOT_HUB']

# Location Database configuration for production
LOCATION_DBNAME = os.environ.get('LOCATION_DBNAME', 'sheets')
LOCATION_DBHOST = os.environ.get('LOCATION_DBHOST', 'localhost')
LOCATION_DBUSER = os.environ.get('LOCATION_DBUSER', 'gellert')
LOCATION_DBPASS = os.environ.get('LOCATION_DBPASS', '')
