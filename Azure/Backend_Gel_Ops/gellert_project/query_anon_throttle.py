from rest_framework import throttling

class QueryAnonymousRateThrottle(throttling.AnonRateThrottle):
    scope = 'query_anon'
    def allow_request(self, request, view):
        return super().allow_request(request, view)