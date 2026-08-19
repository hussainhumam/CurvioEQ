#pragma once

#include <QString>

class ExternalToolLocator
{
public:
    static QString ffmpegPath();
    static QString vgmstreamPath();
    static bool runProcess(const QString &program,
                           const QStringList &arguments,
                           QString *stdOut,
                           QString *stdErr,
                           int timeoutMs = 120000);
};
