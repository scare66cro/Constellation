from rest_framework_simplejwt.serializers import TokenObtainPairSerializer, TokenRefreshSerializer
from django.contrib.auth.models import Permission
from rest_framework import serializers
from .models import UserAccount
from dry_rest_permissions.generics import DRYPermissionsField

class MyTokenObtainPairSerializer(TokenObtainPairSerializer):
    @classmethod
    def get_token(cls, user):
        token = super().get_token(user)

        # Add custom claims
        token['username'] = user.username

        return token

class PermissionsSerializer(serializers.ModelSerializer):
    class Meta:
        model = Permission
        fields = ('codename',)

class UserAccountSerializer(serializers.ModelSerializer):
    obj_permissions = DRYPermissionsField(additional_actions=['change_password'])
    corporation = serializers.SerializerMethodField()
    dealership = serializers.SerializerMethodField()
    customer_account = serializers.SerializerMethodField()
    user_type = serializers.SerializerMethodField()
    permissions = serializers.SerializerMethodField()

    class Meta:
        model = UserAccount
        fields = [
            'url',
            'id',
            'username',
            'email',
            'first_name',
            'last_name',
            'organization',
            'user_type',
            'corporation',
            'dealership',
            'customer_account',
            # 'date_joined',
            'is_superuser',
            'is_staff',
            'is_active',
            # groups
            'permissions',
            'obj_permissions',
        ]

    def get_permissions(self, obj):
        return obj.get_all_permissions()

    def get_user_type(self, obj):
        return obj.get_user_type()

    def get_corporation(self, obj):
        return obj.users_corporation().id if obj.users_corporation() is not None else None

    def get_dealership(self, obj):
        return obj.users_dealership().id if obj.users_dealership() is not None else None

    def get_customer_account(self, obj):
        return obj.users_customer_account().id if obj.users_customer_account() is not None else None