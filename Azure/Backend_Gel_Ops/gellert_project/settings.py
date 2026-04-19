from pathlib import Path
import os
from dev_secrets import db_settings

# Build paths inside the project like this: BASE_DIR / 'subdir'.
BASE_DIR = Path(__file__).resolve().parent.parent

# Quick-start development settings - unsuitable for production
# See https://docs.djangoproject.com/en/3.1/howto/deployment/checklist/

# SECURITY WARNING: keep the secret key used in production secret!
# In production, DJANGO_SECRET_KEY must be set in the environment.
# The fallback value is for local development only.
SECRET_KEY = os.environ.get(
    'DJANGO_SECRET_KEY',
    '-mjfa$*)3w%9_$+9)xpz&#&3360=k6k7f$@j48qzg4q8xd8835'
)
if not os.environ.get('DJANGO_SECRET_KEY') and not os.environ.get('LOCAL_DEV', 'true').lower() in ('1', 'true', 'yes'):
    raise RuntimeError('DJANGO_SECRET_KEY environment variable is required in production')

# SECURITY WARNING: don't run with debug turned on in production!
DEBUG = os.environ.get('DJANGO_DEBUG', 'true').lower() in ('1', 'true', 'yes')

# Local development mode - skip Azure IoT Hub registration/deletion
LOCAL_DEV = os.environ.get('LOCAL_DEV', 'true').lower() in ('1', 'true', 'yes')

ALLOWED_HOSTS = os.environ.get('DJANGO_ALLOWED_HOSTS', '*').split(',')

# CORS settings — open in dev, restricted in production
CORS_ALLOW_ALL_ORIGINS = DEBUG
CORS_ALLOW_CREDENTIALS = True

# Cookie settings for local development (cross-port)
SESSION_COOKIE_SAMESITE = 'Lax'
CSRF_COOKIE_SAMESITE = 'Lax'
CSRF_TRUSTED_ORIGINS = ['http://localhost:3000', 'http://127.0.0.1:3000']

# --- Server host/port configuration (env-aware) ---
# You can override these via environment variables:
#   SERVER_HOST, SERVER_PORT, SERVER_SCHEME (http|https)
# Defaults match current development values.
SERVER_HOST = os.environ.get('SERVER_HOST', '10.1.2.54')
#SERVER_HOST = os.environ.get('SERVER_HOST', '192.168.0.84')
SERVER_PORT = int(os.environ.get('SERVER_PORT', '8000'))
SERVER_SCHEME = os.environ.get('SERVER_SCHEME', 'https')
SERVER_NETLOC = f"{SERVER_HOST}:{SERVER_PORT}"
BASE_URL = f"{SERVER_SCHEME}://{SERVER_NETLOC}"

# Application definition

INSTALLED_APPS = [
    'django.contrib.admin',
    'django.contrib.auth',
    'django.contrib.contenttypes',
    'django.contrib.sessions',
    'django.contrib.messages',
    'django.contrib.staticfiles',

    'corsheaders',  # CORS support for local development
    'whitenoise.runserver_nostatic', #this will handle all the static files served from django
    'polymorphic',
    # 'rest_framework_simplejwt.token_blacklist',

    'rest_framework',
    'dry_rest_permissions',
    'gellert_apps.api',
    'gellert_apps.user_account_app',
    'gellert_apps.django_saml_sp',
]

if os.getenv("DJANGO_ENV") == "development":
    INSTALLED_APPS += ["django_extensions"]


# this is for using httponly cookies for auth...
SESSION_ENGINE = "django.contrib.sessions.backends.signed_cookies"
SESSION_SAVE_EVERY_REQUEST = True #extends user session on every request
# a header labeled "X_XSRF_TOKEN" must be included in every unsafe http request
CSRF_HEADER_NAME = "HTTP_X_XSRF_TOKEN"

# This is my custom user class based on Django's base user
AUTH_USER_MODEL = 'user_account_app.UserAccount'

# from dev_secrets import rsa

# SIMPLE_JWT = {
#     'ALGORITHM': 'RS256',
#     'SIGNING_KEY': rsa['PRIVATE'],
#     'VERIFYING_KEY': rsa['PUBLIC'],

#     # 'ROTATE_REFRESH_TOKENS': False,
#     # 'BLACKLIST_AFTER_ROTATION': True,
# }

REST_FRAMEWORK = {
    'DEFAULT_AUTHENTICATION_CLASSES': [
        # 'rest_framework_simplejwt.authentication.JWTAuthentication',
        'rest_framework.authentication.SessionAuthentication',
    ],
    'DEFAULT_PERMISSION_CLASSES': [
        'rest_framework.permissions.IsAuthenticated',
    ],
    'DEFAULT_THROTTLE_CLASSES': [
        'rest_framework.throttling.AnonRateThrottle',
    ],
    'DEFAULT_THROTTLE_RATES': {
        'anon': '1000/day',
        'query_anon': '5/min'
    }
}

MIDDLEWARE = [
    'corsheaders.middleware.CorsMiddleware',  # CORS must be first
    'django.middleware.security.SecurityMiddleware',
    'whitenoise.middleware.WhiteNoiseMiddleware',
    'django.contrib.sessions.middleware.SessionMiddleware',
    'django.middleware.locale.LocaleMiddleware',
    'django.middleware.common.CommonMiddleware',
    'django.middleware.csrf.CsrfViewMiddleware',
    'django.contrib.auth.middleware.AuthenticationMiddleware',
    'django.contrib.messages.middleware.MessageMiddleware',
    'django.middleware.clickjacking.XFrameOptionsMiddleware',
]

ROOT_URLCONF = 'gellert_project.urls'

TEMPLATES = [
    {
        'BACKEND': 'django.template.backends.django.DjangoTemplates',
        'DIRS': [os.path.join(os.path.join(BASE_DIR, "frontend_apps"), "templates"),],
        'APP_DIRS': True,
        'OPTIONS': {
            'context_processors': [
                'django.template.context_processors.debug',
                'django.template.context_processors.request',
                'django.contrib.auth.context_processors.auth',
                'django.contrib.messages.context_processors.messages',
            ],
        },
    },
]

WSGI_APPLICATION = 'gellert_project.wsgi.application'

SP_SETTINGS = {
    "strict": True,
    "debug": True,
    "sp": {
        "entityId": f"{BASE_URL}/saml/metadata/",
        "assertionConsumerService": {
            "url": f"{BASE_URL}/saml/complete-login/",
            "binding": "urn:oasis:names:tc:SAML:2.0:bindings:HTTP-POST",
        },
        "singleLogoutService": {
            "url": f"{BASE_URL}/saml/complete-logout/",
            "binding": "urn:oasis:names:tc:SAML:2.0:bindings:HTTP-Redirect"
        },
        "NameIDFormat": "urn:oasis:names:tc:SAML:1.1:nameid-format:emailAddress"
    },
    "idp": {
        "entityId": "https://sts.windows.net/df39466e-d00d-458a-8329-bf8a8751d4eb/",
        "singleSignOnService": {
            "url": "https://login.microsoftonline.com/df39466e-d00d-458a-8329-bf8a8751d4eb/saml2",
            "binding": "urn:oasis:names:tc:SAML:2.0:bindings:HTTP-Redirect"
        },
        "singleLogoutService": {
            "url": "https://login.microsoftonline.com/df39466e-d00d-458a-8329-bf8a8751d4eb/saml2",
            "binding": "urn:oasis:names:tc:SAML:2.0:bindings:HTTP-Redirect"
        },
        "x509cert": "MIIC8DCCAdigAwIBAgIQJ1rvR5XC+q1KtXL0vQ0dmTANBgkqhkiG9w0BAQsFADA0MTIwMAYDVQQDEylNaWNyb3NvZnQgQXp1cmUgRmVkZXJhdGVkIFNTTyBDZXJ0aWZpY2F0ZTAeFw0yMjAzMDkyMTMyMTJaFw0yNTAzMDkyMDMyMTJaMDQxMjAwBgNVBAMTKU1pY3Jvc29mdCBBenVyZSBGZWRlcmF0ZWQgU1NPIENlcnRpZmljYXRlMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAvLIlP+Gxh7CXcHd0gMzm6AGDhL9iGzxP6WVjXaDJSVacsLxE/hhUC3liTMFJGkXXXzQXbL1wyPg3DdU741w7NwjwXtsEEduCmZDwoNgf/RyoetY7v0JDi9lNEAMYgeH6EnXDkK2tEv7H53hOYHUPqapI/eb+S2He6wBGQhoPhAggnFX/+ExQwIqtx5ttziFTXffCkdwM9NNLv9XjnkKQoz94e2Eq+c7nb4fBiDECP5eWXMftzMFITb6esdoM/v9Le5bn4M2D0b2tGwzk8OKV7jV/+qKb0MN+Zu05sMGtKCDm4EEHKoa9F5QUcS8W0tlXEZ2+Ztpb767byGF/gmuYgQIDAQABMA0GCSqGSIb3DQEBCwUAA4IBAQCtwCEvT4fEnlHwAJ9alSEag1Tubq31K0mMkfuVuu16THlKqSbhCoNNCVUWSRiBOK8pbGcgLNnmatvOZSesnQ+rqm5qRJEmEI5QDw2EdV6FBs3yu8QZMYE5lNO0+WuNEJAIorZfr8unNFdoNgCq4BysLdYrYFEaSu/NBxfZSKZYxALk65c4LvHupmNHR6VP/UJ2q0yngkA+WfRZxDjfa+o8HYBl2AQqLjHZgocehV/mHfUCVM/J8YqQom4rL47E/LSdf9HB+mqDhq1AsZXfG6+pl6yLLn2do71BiTxe3TO7rnMpwLY2BLhS9S/M6+e8Yvq/XegL0d+6kM6DxIngGMVr",
    }
}

# Database
# https://docs.djangoproject.com/en/3.1/ref/settings/#databases

DATABASES = {
    # 'default': {
    #     'ENGINE': 'django.db.backends.sqlite3',
    #     'NAME': BASE_DIR / 'db.sqlite3',
    # }

    # local postgresDB instance
    'default': {
        'ENGINE': 'django.db.backends.postgresql',
        'NAME': db_settings['DBNAME'], #default name in postgres is 'postgres'
        'HOST': db_settings['DBHOST'],
        'USER': db_settings['DBUSER'],
        'PASSWORD': db_settings['DBPASS'],
        # forces django to connect to DB with SSL
        # 'OPTIONS': {'sslmode': 'require'},
    }
}

# Password validation
# https://docs.djangoproject.com/en/3.1/ref/settings/#auth-password-validators

AUTH_PASSWORD_VALIDATORS = [
    {
        'NAME': 'django.contrib.auth.password_validation.UserAttributeSimilarityValidator',
    },
    {
        'NAME': 'django.contrib.auth.password_validation.MinimumLengthValidator',
    },
    {
        'NAME': 'django.contrib.auth.password_validation.CommonPasswordValidator',
    },
    {
        'NAME': 'django.contrib.auth.password_validation.NumericPasswordValidator',
    },
]

# Internationalization
# https://docs.djangoproject.com/en/3.1/topics/i18n/

LANGUAGE_CODE = 'en-us'

TIME_ZONE = 'UTC'

USE_I18N = True

USE_L10N = True

USE_TZ = True

# Static files (CSS, JavaScript, Images)
# https://docs.djangoproject.com/en/3.1/howto/static-files/

STATICFILES_DIRS = [
    # Tell Django where to look for React's static files (css, js)
    os.path.join(os.path.join(BASE_DIR, "frontend_apps", )),
]

STATIC_URL = '/static/'

STATICFILES_STORAGE = 'whitenoise.storage.CompressedManifestStaticFilesStorage'
STATIC_ROOT = os.path.join(BASE_DIR, 'staticfiles')

LOGIN_REDIRECT_URL = f"{BASE_URL}/storage-app/"

TENANTS = ['df39466e-d00d-458a-8329-bf8a8751d4eb']

DEV_MACHINE = f"{SERVER_SCHEME}://{SERVER_HOST}"

# --- Development React dev-server proxy settings (used by storage_pwa_static) ---
# Optional port override for local React dev server
DEV_REACT_PORT = int(os.environ.get('DEV_REACT_PORT', '3000'))
# Path to a PEM bundle containing the custom Root / Intermediate CA(s) to trust
DEV_CA_BUNDLE = os.environ.get('DEV_CA_BUNDLE')  # e.g. c:/Projects/Backend_Gel_Ops/cert.pem
# If set to true/1, allow insecure HTTPS (certificate verification disabled) ONLY for local dev proxy.
# Strongly discouraged outside of isolated development.
DEV_ALLOW_INSECURE_SSL = os.environ.get('DEV_ALLOW_INSECURE_SSL', 'false').lower() in ('1', 'true', 'yes')

AUTHENTICATION_BACKENDS = (
    'gellert_apps.django_saml_sp.backends.SAMLServiceProviderBackend',
    'django.contrib.auth.backends.ModelBackend'
)

DOMAINS = [
    BASE_URL,
]

DEFAULT_AUTO_FIELD = 'django.db.models.BigAutoField'

DEFAULT_IOT_HUB = 'USWest'

# Location Database configuration
LOCATION_DBNAME = db_settings['LOCATION_DBNAME'] or ''
LOCATION_DBHOST = db_settings['LOCATION_DBHOST'] or ''
LOCATION_DBUSER = db_settings['LOCATION_DBUSER'] or ''
LOCATION_DBPASS = db_settings['LOCATION_DBPASS'] or ''

# --- Logging configuration ---
# Route application logs to console so Azure captures them from stdout/stderr.
LOGGING = {
    'version': 1,
    'disable_existing_loggers': False,
    'formatters': {
        'standard': {
            'format': '%(asctime)s %(levelname)s %(name)s: %(message)s',
        },
    },
    'handlers': {
        'console': {
            'class': 'logging.StreamHandler',
            'formatter': 'standard',
            'stream': 'ext://sys.stdout',
        },
    },
    'loggers': {
        # Your SAML app logger used in gellert_apps/django_saml_sp/views.py
        'django_saml_sp': {
            'handlers': ['console'],
            'level': 'DEBUG',  # set to INFO in production if too chatty
            'propagate': False,
        },
        # Gunicorn loggers (for Linux/App Service or containerized deploys)
        'gunicorn.error': {
            'handlers': ['console'],
            'level': 'INFO',
            'propagate': False,
        },
        'gunicorn.access': {
            'handlers': ['console'],
            'level': 'INFO',
            'propagate': False,
        },
    },
}
