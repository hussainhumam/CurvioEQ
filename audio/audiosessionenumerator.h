#pragma once

#include <QString>
#include <QVector>

struct AudioSessionInfo {
    unsigned long processId = 0;
    QString displayName;
    QString sessionId;
    QString deviceId;
    QString deviceName;
    bool muted = false;
};

class AudioSessionEnumerator
{
public:
    static QVector<AudioSessionInfo> listActiveSessions();
};
