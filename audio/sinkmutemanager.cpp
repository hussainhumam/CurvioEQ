#include "sinkmutemanager.h"

#include "audioendpointvolume.h"

SinkMuteManager &SinkMuteManager::instance()
{
    static SinkMuteManager manager;
    return manager;
}

bool SinkMuteManager::acquire(const QString &deviceId, bool enabled, QString *errorMessage)
{
    if (!enabled || deviceId.isEmpty()) {
        return true;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    SinkState &state = m_states[deviceId];
    if (state.refcount == 0) {
        bool muted = false;
        if (!AudioEndpointVolume::getMute(deviceId, &muted, errorMessage)) {
            m_states.remove(deviceId);
            return false;
        }
        state.previousMute = muted;
        state.capturedPrevious = true;
        if (!muted) {
            if (!AudioEndpointVolume::setMute(deviceId, true, errorMessage)) {
                m_states.remove(deviceId);
                return false;
            }
        }
    }
    ++state.refcount;
    return true;
}

void SinkMuteManager::release(const QString &deviceId, bool enabled)
{
    if (!enabled || deviceId.isEmpty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_states.find(deviceId);
    if (it == m_states.end()) {
        return;
    }

    SinkState &state = it.value();
    if (state.refcount <= 0) {
        m_states.erase(it);
        return;
    }

    --state.refcount;
    if (state.refcount > 0) {
        return;
    }

    if (state.capturedPrevious && !state.previousMute) {
        QString errorMessage;
        AudioEndpointVolume::setMute(deviceId, false, &errorMessage);
    }
    m_states.erase(it);
}
