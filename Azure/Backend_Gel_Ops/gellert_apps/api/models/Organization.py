from django.db import models
import uuid
from polymorphic.models import PolymorphicModel
from dry_rest_permissions.generics import DRYPermissionFiltersBase, allow_staff_or_superuser, \
    authenticated_users

class Organization(PolymorphicModel):
    id = models.UUIDField(primary_key=True, default=uuid.uuid4, editable=False)
    name = models.CharField(max_length=100)
    parent = models.ForeignKey('self', blank=True, null=True, related_name='subsidiaries', on_delete=models.CASCADE)
    # temp_phone = models.CharField(max_length=)
    is_active = models.BooleanField(default=True, db_index=True)

    def is_model_type(self, class_type):
        return self.get_real_instance_class() == class_type

    def __str__(self) -> str:
        return self.name

    class Meta:
        db_table = 'Organization'
        verbose_name = 'Organization'

    @staticmethod
    @authenticated_users
    def has_read_permission(request):
        return True

    @allow_staff_or_superuser
    @authenticated_users
    def has_object_read_permission(self, request):
        return True

class OrganizationsFilterBackend(DRYPermissionFiltersBase):
    def filter_list_queryset(self, request, queryset, view):
        user = request.user
        queryset = queryset.filter(is_active=True)
        return queryset if user.is_superuser else queryset.filter(
            models.Q(id=user.organization_id)
            or models.Q(parent__id=user.organization_id)
            or models.Q(parent__parent__id=user.organization_id)
        )
