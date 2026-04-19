const version = [100, 10, 1];

export default function isUpgradeable(clientVersion, upgradeVersion) {
    const clientNumber = clientVersion.split('.').reduce(
        (previous, current, index) => previous + current * version[index], 0,
    );
    const upgrade = upgradeVersion === '104' ? '1.0.4' : upgradeVersion;
    const upgradeNumber = upgrade?.split('.').reduce(
        (previous, current, index) => previous + current * version[index], 0,
    );
    return upgradeNumber > clientNumber;
}
