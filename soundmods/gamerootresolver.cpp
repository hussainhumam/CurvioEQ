#include "gamerootresolver.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {

QString readSteamLibraryContaining(const QString &executablePath)
{
    const QStringList configRoots = {
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation),
        QDir::homePath() + QStringLiteral("/AppData/Local"),
    };

    for (const QString &configRoot : configRoots) {
        const QString vdfPath = QDir(configRoot).filePath(QStringLiteral("Steam/config/libraryfolders.vdf"));
        QFile file(vdfPath);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }

        const QString content = QString::fromUtf8(file.readAll());
        const QRegularExpression pathRegex(QStringLiteral("\"path\"\\s+\"([^\"]+)\""));
        QRegularExpressionMatchIterator it = pathRegex.globalMatch(content);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            const QString libraryPath = QDir::fromNativeSeparators(match.captured(1));
            if (!libraryPath.isEmpty() && executablePath.startsWith(libraryPath, Qt::CaseInsensitive)) {
                return libraryPath;
            }
        }
    }

    return {};
}

} // namespace

QString GameRootResolver::resolveScanRoot(const QString &executablePath, const QString &preferredRoot)
{
    if (!preferredRoot.isEmpty() && QDir(preferredRoot).exists()) {
        return QDir(QDir::fromNativeSeparators(preferredRoot)).absolutePath();
    }

    if (executablePath.isEmpty()) {
        return {};
    }

    const QFileInfo exeInfo(executablePath);
    QString root = exeInfo.absolutePath();

    const QString steamLibrary = readSteamLibraryContaining(executablePath);
    if (!steamLibrary.isEmpty()) {
        QDir dir(root);
        for (int depth = 0; depth < 6 && dir.exists(); ++depth) {
            if (dir.dirName().compare(QStringLiteral("common"), Qt::CaseInsensitive) == 0) {
                root = dir.absolutePath();
                break;
            }
            if (!dir.cdUp()) {
                break;
            }
        }
    }

    return QDir::fromNativeSeparators(root);
}
