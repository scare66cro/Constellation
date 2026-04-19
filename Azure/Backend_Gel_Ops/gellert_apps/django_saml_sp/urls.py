"""
    URL patterns for saml
"""
from django.urls import path
from .views import metadata, initiate_login, complete_login, complete_logout

urlpatterns = [
    path('initiate-login/', initiate_login, name="saml_sp_initiate_login"),
    path('complete-login/', complete_login, name="saml_sp_complete_login"),
    path('complete-logout/', complete_logout, name="saml_sp_complete_logout"),
    path('metadata/', metadata, name="saml_sp_metadata"),
]
