#include "apppaths.h"

#include "appconstants.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace {
constexpr char kPortableMarker[] = "portable.txt";

QString roamingAppData()
{
    QString roaming = QString::fromLocal8Bit(qgetenv("APPDATA"));
    if (roaming.isEmpty()) {
        roaming = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    }
    if (roaming.isEmpty()) {
        roaming = QDir::homePath() + QStringLiteral("/AppData/Roaming");
    }
    return roaming;
}

QString settingsFileIn(const QString &root)
{
    return QDir(root).filePath(QStringLiteral("settings.json"));
}

QString detectPortableRoot()
{
    const QString exeDir = QCoreApplication::applicationDirPath();
    if (exeDir.isEmpty()) {
        return {};
    }

    if (QFile::exists(QDir(exeDir).filePath(QString::fromLatin1(kPortableMarker)))) {
        return QDir(exeDir).absolutePath();
    }

    QDir dir(exeDir);
    if (dir.dirName().compare(QStringLiteral("bin"), Qt::CaseInsensitive) == 0 && dir.cdUp()) {
        if (QFile::exists(dir.filePath(QString::fromLatin1(kPortableMarker)))) {
            return dir.absolutePath();
        }
    }
    return {};
}
}

bool AppPaths::isPortable()
{
    return !detectPortableRoot().isEmpty();
}

QStringList AppPaths::settingsSearchRoots()
{
    const QString portableRoot = detectPortableRoot();
    if (!portableRoot.isEmpty()) {
        return {portableRoot};
    }

    const QString roaming = roamingAppData();
    const QString appId = QString::fromLatin1(AppConstants::kAppId);
    return {
        QDir(roaming).filePath(appId),
        QDir(roaming).filePath(appId + QLatin1Char('/') + appId),
        QDir(roaming).filePath(QStringLiteral("PerAppEQ")),
        QDir(roaming).filePath(QStringLiteral("PerAppEQ/PerAppEQ")),
    };
}

QString AppPaths::dataRoot()
{
    const QStringList roots = settingsSearchRoots();
    for (const QString &root : roots) {
        if (QFile::exists(settingsFileIn(root))) {
            return root;
        }
    }

    const QString root = roots.constFirst();
    QDir().mkpath(root);
    return root;
}

QString AppPaths::soundModsRoot()
{
    return QDir(dataRoot()).filePath(QStringLiteral("soundmods"));
}

bool AppPaths::hasExistingSettingsFile()
{
    for (const QString &root : settingsSearchRoots()) {
        if (QFile::exists(settingsFileIn(root))) {
            return true;
        }
    }
    return false;
}

static QString welcomeMarkerPath()
{
    return QDir(AppPaths::dataRoot()).filePath(QStringLiteral("welcome.shown"));
}

bool AppPaths::welcomeMarkerExists()
{
    return QFile::exists(welcomeMarkerPath());
}

void AppPaths::markWelcomeShown()
{
    const QString path = welcomeMarkerPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write("1\n");
    }
}
