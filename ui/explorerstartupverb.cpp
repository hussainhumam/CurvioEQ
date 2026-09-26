#include "explorerstartupverb.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

bool ExplorerStartupVerb::registerVerb()
{
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    if (exe.isEmpty()) {
        return false;
    }

    const QString base =
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\exefile\\shell\\CurvioEQ.StartAtAppStartup");
    QSettings verb(base, QSettings::NativeFormat);
    verb.setValue(QStringLiteral("."), QStringLiteral("Start at app startup"));
    verb.setValue(QStringLiteral("Icon"), exe);

    QSettings command(base + QStringLiteral("\\command"), QSettings::NativeFormat);
    command.setValue(QStringLiteral("."),
                     QStringLiteral("\"%1\" --start-at-app-startup \"%2\"").arg(exe, QStringLiteral("%1")));

    verb.sync();
    command.sync();
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return verb.status() == QSettings::NoError && command.status() == QSettings::NoError;
}
