from django.contrib import admin
from .models import UserAccount
from .custom_admin import CustomUserAdmin

# Register your models here.
admin.site.register(UserAccount, CustomUserAdmin)