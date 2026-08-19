#include "gameidentity.h"

#include "ui/appiconprovider.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>

GameIdentity GameIdentityUtil::fromProcess(unsigned long processId, const QString &displayName)
{
    GameIdentity identity;
    identity.displayName = displayName;
    identity.executablePath = AppIconProvider::executablePathForProcess(processId);

    if (!identity.executablePath.isEmpty()) {
        identity.scanRoot = QFileInfo(identity.executablePath).absolutePath();
        identity.id = QCryptographicHash::hash(normalizePathKey(identity.executablePath).toUtf8(),
                                               QCryptographicHash::Sha256)
                          .toHex()
                          .left(16);
    } else {
        identity.id = QStringLiteral("pid_%1").arg(processId);
        identity.scanRoot.clear();
    }

    return identity;
}

QString GameIdentityUtil::normalizePathKey(const QString &path)
{
    return QDir::fromNativeSeparators(path).toLower();
}
