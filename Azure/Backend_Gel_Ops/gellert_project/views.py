from rest_framework.decorators import api_view, permission_classes
from rest_framework.permissions import AllowAny
from rest_framework import status
from django.views.decorators.csrf import csrf_exempt
from rest_framework.response import Response

@api_view(['GET'])
@permission_classes([AllowAny])
@csrf_exempt
def HealthCheck(request):
    return Response(status=status.HTTP_200_OK)

@api_view(['GET'])
@permission_classes([AllowAny])
@csrf_exempt
def KeepAlive(request):
    return Response(status=status.HTTP_200_OK)
