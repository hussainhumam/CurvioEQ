#pragma once

#include <QString>

#include <QHash>
#include <mutex>

class SinkMuteManager
{
public:
    static SinkMuteManager &instance();

    bool acquire(const QString &deviceId, bool enabled, QString *errorMessage = nullptr);
    void release(const QString &deviceId, bool enabled);

private:
    SinkMuteManager() = default;

    struct SinkState {
        int refcount = 0;
        bool previousMute = false;
        bool capturedPrevious = false;
    };

    std::mutex m_mutex;
    QHash<QString, SinkState> m_states;
};
