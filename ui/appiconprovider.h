#pragma once

#include <QHash>
#include <QIcon>
#include <QString>

class AppIconProvider
{
public:
    static QIcon appIcon();
    static QIcon iconForProcess(unsigned long processId);
    static QString displayNameForProcess(unsigned long processId);

private:
    static QString executablePathForProcess(unsigned long processId);

    static QHash<unsigned long, QIcon> s_iconCache;
    static QHash<unsigned long, QString> s_nameCache;
};
