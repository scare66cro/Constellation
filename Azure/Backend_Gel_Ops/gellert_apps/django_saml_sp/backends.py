"""
    SAML Service Provider
"""
from gellert_apps.user_account_app.models import UserAccount
class SAMLServiceProviderBackend():
    """
        Class for the saml service provider backend
    """
    def authenticate(self, saml_authentication=None):
        """
            Authenticate using Saml
        """
        if not saml_authentication:  # Using another authentication method
            return None

        if saml_authentication.is_authenticated():
            user = UserAccount.objects.get(email=saml_authentication.get_attribute('http://schemas.xmlsoap.org/ws/2005/05/identity/claims/name')[0])
            return user
        return None

    def get_user(self, user_id):
        """
            Get the authenticated user
        """
        return UserAccount.objects.get(pk=user_id)
