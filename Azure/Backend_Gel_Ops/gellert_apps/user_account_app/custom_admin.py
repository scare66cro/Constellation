from django.contrib.auth.admin import UserAdmin

# --------------- Custom User Admin --------------------------
class CustomUserAdmin(UserAdmin):
    fieldsets = UserAdmin.fieldsets + (
        ('Custom Fields', {
            'fields': ['organization']
        }),
    )
