import uuid
from rest_framework.response import Response
from rest_framework.permissions import IsAuthenticated, AllowAny
from rest_framework.decorators import action
from rest_framework.views import APIView
from rest_framework import viewsets
from rest_framework import status, serializers
from rest_framework.exceptions import PermissionDenied, AuthenticationFailed
from rest_framework_simplejwt.views import TokenObtainPairView

from django.contrib.auth import authenticate, login, logout, password_validation
from django.contrib.auth.models import Permission
from django.contrib.contenttypes.models import ContentType
from django.core.exceptions import ValidationError
from django.conf import settings

from dry_rest_permissions.generics import DRYPermissions
from gellert_apps.api.models import Site, CustomerAccount
from gellert_project.microsoft_authentication import MicrosoftAuthentication
from ..api.models import IoTClient, upgrades, user_sites
from .serializers import MyTokenObtainPairSerializer, UserAccountSerializer

# pylint: disable=import-error
from .models import UserAccount, UserAccountFilterBackend

# this is an Overriden DRF class... only the customizations are listed here
class MyTokenObtainPairView(TokenObtainPairView):
    serializer_class = MyTokenObtainPairSerializer

class LoginView(APIView):
    permission_classes = (AllowAny,)
    def post(self, request, format=None):
        user = None
        data = request.data
        msAuth = MicrosoftAuthentication()
        if len(data) == 0:
            [email, token] = msAuth.authenticate(request)
            if token in settings.VALID_TENANTS and email is not None:
                user = UserAccount.objects.get(email=email)
        else:
            username = data.get('username', None)
            password = data.get('password', None)
            # print(username, password)
            user = authenticate(username=username, password=password)
        if user is not None:
            login(request, user)
            serialized_user = UserAccountSerializer(user, context={'request':request})
            return Response(serialized_user.data)
        else:
            content = {"message":"Username or Password is incorrect"}
            return Response(content, status=status.HTTP_401_UNAUTHORIZED)

    def get(self, request, format=None):
        user = request.user
        if user.is_authenticated:
            serialized_user = UserAccountSerializer(user, context={'request':request})
            return Response(serialized_user.data)

        if not user.is_authenticated:
            msAuth = MicrosoftAuthentication()
            [email, token] = msAuth.authenticate(request)
            if token in settings.TENANTS and email is not None:
                user = UserAccount.objects.get(email=email)
                if user is not None:
                    login(request, user)
                    serialized_user = UserAccountSerializer(user, context={'request':request})
                    return Response(serialized_user.data)

        return Response(status=status.HTTP_401_UNAUTHORIZED)

class Logout(APIView):
    def get(self, request, format=None):
        logout(request)
        return Response(status=status.HTTP_200_OK)

class UserAccountViewSet(viewsets.ModelViewSet):
    permission_classes = (IsAuthenticated, DRYPermissions,)
    queryset = UserAccount.objects.all().order_by('username')
    filter_backends = (UserAccountFilterBackend,)
    serializer_class = UserAccountSerializer


    @action(detail=True, methods=['get'])
    def managed_users(self, request, pk=None):
        self.queryset = UserAccount.objects.all().order_by('username')
        user = self.queryset.get(pk=pk)
        result = []
        user_type = user.get_user_type()
        if request.user.is_staff:
            result = self.queryset.all()
        else:
            for managed in self.queryset:
                if user_type == 'corporate' and managed.users_corporation() == user.users_corporation():
                    result.append(managed)
                elif user_type == 'dealer' and managed.users_dealership() == user.users_dealership():
                    result.append(managed)
                elif user_type == 'customer' and managed.users_customer_account() == user.users_customer_account():
                    result.append(managed)

        return Response(UserAccountSerializer(result, many=True, context={'request': request}).data)

    @action(detail=True, methods=['post'])
    def change_password(self, request, pk=None):
        # json { "current_password":"1234", "new_password":"12345", "new_password_confirm":"12345" }
        # object level permissions are not called automatically for custom actions...
        if UserAccount.objects.get(id=pk).has_object_change_password_permission(request):
            user = request.user
            current_password = request.data.get('current_password')
            new_password = request.data.get('new_password')
            new_password_confirm = request.data.get('new_password_confirm')

            if user is not None and user.check_password(current_password):
                if new_password != new_password_confirm:
                    return Response({
                        'new_password_confirm':[
                            'Confirmation password does not match new password.'
                        ]
                    }, status=status.HTTP_400_BAD_REQUEST)
                try:
                    password_validation.validate_password(new_password, request.user)
                except ValidationError as exc:
                    raise serializers.ValidationError({'new_password_confirm': exc.messages})
                user.set_password(new_password)
                user.save()
                return Response()

            raise AuthenticationFailed

        raise PermissionDenied

    @action(detail=False,methods=['post'])
    def delete_users(self, request):
        if request.user.has_perm('user_account_app.delete_useraccount'):
            payload = self.queryset.filter(pk__in = request.data.get('ids'))
            payload.delete()
            self.queryset = UserAccount.objects.all().order_by('username')
            return Response(status=status.HTTP_200_OK)

        raise PermissionDenied

    # set detail to False if operating on more than one
    @action(detail=False, methods=['post'])
    def save_users(self, request):
        if request.user.has_perm('user_account_app.change_useraccount'):
            newusers = [user for user in request.data.get('users') if 'password' in user]
            updateusers = [user for user in request.data.get('users') if 'password' not in user]
            for user in updateusers:
                update = self.queryset.get(pk=user['id'])
                self.setUser(update, user)
                update.save()

            for user in newusers:
                password = user['password']
                if password == '':
                    password = str(uuid.uuid4())
                new_user = UserAccount.objects.create_user(user['username'], user['email'], password)
                self.setUser(new_user, user)
                new_user.save()

            return Response(status=status.HTTP_200_OK)

        raise PermissionDenied

    def getPermission(self, perm):
        # Resolve content types lazily at call-time to avoid DB access during import/module load
        if perm in ['access_level1_IoTClient', 'access_level2_IoTClient', 'view_iotclient', 'add_iotclient', 'change_iotclient']:
            ct = ContentType.objects.get_for_model(IoTClient.IoTClient)
            return Permission.objects.get(content_type=ct, codename=perm)
        if perm == 'view_upgrade':
            ct = ContentType.objects.get_for_model(upgrades.Upgrade)
            return Permission.objects.get(content_type=ct, codename=perm)
        if perm in ['view_useraccount', 'add_useraccount', 'delete_useraccount', 'change_useraccount']:
            ct = ContentType.objects.get_for_model(UserAccount)
            return Permission.objects.get(content_type=ct, codename=perm)
        if perm in ['view_usersites', 'add_usersites', 'delete_usersites', 'change_usersites']:
            ct = ContentType.objects.get_for_model(user_sites.UserSites)
            return Permission.objects.get(content_type=ct, codename=perm)
        if perm in ['view_customeraccount', 'add_customeraccount', 'delete_customeraccount', 'change_customeraccount']:
            ct = ContentType.objects.get_for_model(CustomerAccount.CustomerAccount)
            return Permission.objects.get(content_type=ct, codename=perm)
        if perm in ['view_site', 'add_site', 'delete_site', 'change_site']:
            ct = ContentType.objects.get_for_model(Site.Site)
            return Permission.objects.get(content_type=ct, codename=perm)

    def setUser(self, user, values):
        user.organization_id = values['organization']
        user.user_permissions.remove(
            self.getPermission('access_level1_IoTClient'),
            self.getPermission('access_level2_IoTClient'),
            self.getPermission('view_upgrade'),
            self.getPermission('view_useraccount'),
            self.getPermission('add_useraccount'),
            self.getPermission('delete_useraccount'),
            self.getPermission('change_useraccount'),
            self.getPermission('view_usersites'),
            self.getPermission('add_usersites'),
            self.getPermission('delete_usersites'),
            self.getPermission('change_usersites'),
            # manage storages
            self.getPermission('view_iotclient'),
            self.getPermission('add_iotclient'),
            self.getPermission('delete_iotclient'),
            self.getPermission('change_iotclient'),
            self.getPermission('view_customeraccount'),
            self.getPermission('add_customeraccount'),
            self.getPermission('delete_customeraccount'),
            self.getPermission('change_customeraccount'),
            self.getPermission('view_site'),
            self.getPermission('add_site'),
            self.getPermission('delete_site'),
            self.getPermission('change_site'),
        )
        for perm in values['permissions']:
            permission = self.getPermission(perm.split('.')[1])
            if permission:
                user.user_permissions.add(permission)
        user.is_active = values['is_active']
        user.last_name = values['last_name']
        user.first_name = values['first_name']
        user.email = values['email']

