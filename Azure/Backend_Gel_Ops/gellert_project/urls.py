"""
    URLS for the gellert project
"""
from django.contrib import admin
from django.urls import path, include, re_path
from gellert_apps.user_account_app import views as user_views
from gellert_project.views import HealthCheck, KeepAlive
from .static_react_apps import storage_pwa_static

urlpatterns = [
    path(
        'health-check',
        HealthCheck,
        name='health-check'
    ),
    path('', KeepAlive, name='keep-alive'),
    #catches all react queries related to loading content
    re_path('storage-app/', storage_pwa_static),

    path('saml/', include('gellert_apps.django_saml_sp.urls')),
    path('admin/', admin.site.urls),
    path('login/', user_views.LoginView.as_view(), name='login'),
    path('logout/', user_views.Logout.as_view(), name='logout'),
    path('api/', include('gellert_apps.api.urls')),
]

    # FOR API TOKENs -> but currently only using session cookies and csrf tokens... 
    # path('api/token/refresh/',  jwt_views.TokenRefreshView.as_view(), name='token_refresh'),
    # path('api/token/', user_views.MyTokenObtainPairView.as_view(), name='token_obtain_pair'),
