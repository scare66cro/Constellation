from rest_framework.authentication import BaseAuthentication
import requests

class MicrosoftAuthentication(BaseAuthentication):
    def authenticate(self, request):
        token = request.META.get('HTTP_AUTHORIZATION')
        if token:
            result = requests.get(
            'https://graph.microsoft.com/v1.0/me',
            headers = {
                'Authorization': token
            },
            params = {
                '$select': 'mail'
            })
            if result:
                email = result.json()['mail']

            # Graph Explorer link
            # https://developer.microsoft.com/en-us/graph/graph-explorer?request=organization&method=GET&version=v1.0&GraphUrl=https://graph.microsoft.com&requestBody=eyJleHRlbnNpb25fZjhiMDRhYmYzOWQ0NGMxZDhiMWJlNDNkYzg2ZTNiMzVfcG9ydG9FbmRwb2ludHMiOiJ0ZXN0c2hhcmU6NDQ1LHRlc3RzaGFyZTI6NDQ1In0=
            result = requests.get(
                'https://graph.microsoft.com/v1.0/organization',
                headers = {
                    'Authorization': token
                },
                params = {
                    '$select': 'id'
                }
            )
            if result:
                tenant = result.json()['value'][0]['id']
        else:
            return [None, None]
        return [email, tenant]
