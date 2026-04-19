from django.core.files.storage import Storage
from django.conf import settings
from .models import UpgradeFile
from django.utils.deconstruct import deconstructible

@deconstructible
class DatabaseStorage(Storage):
    def _open(self, name, mode='rb'):
        upgradeFile = UpgradeFile.UpgradeFile.objects.get(pk=name)
        if upgradeFile:
            return upgradeFile.content
        return None

    def _save(self, name, content):
        name = name.replace('\\', '/')
        binary = content.read()
        file = UpgradeFile.UpgradeFile(id=name, content=binary, size=len(binary))
        file.save()
        return name

    def exists(self, name):
        upgradeFile = UpgradeFile.UpgradeFile.objects.get(pk=name)
        return upgradeFile is not None

    def get_available_name(self, name, max_length):
        return name

    def delete(self, name):
        if self.exists(name):
            upgradeFile = UpgradeFile.UpgradeFile.objects.get(pk=name)
            upgradeFile.delete()

    def url(self, name):
        return name

    def size(self, name):
        upgradeFile = UpgradeFile.UpgradeFile.objects.get(pk=name)
        return upgradeFile.size