#pragma once

#include <QString>
#include <QStringList>

class AppPaths
{
public:
    static QString dataRoot();
    static QString soundModsRoot();
    static QStringList settingsSearchRoots();
    static bool hasExistingSettingsFile();
    static bool welcomeMarkerExists();
    static void markWelcomeShown();
};
