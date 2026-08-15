#include "audiosessionenumerator.h"

#include "audiopolicyrouter.h"
#include "log.h"
#include "ui/appiconprovider.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>

#include <QHash>

namespace {

QString wideToQString(const wchar_t *text)
{
    if (!text) {
        return {};
    }
    return QString::fromWCharArray(text);
}

bool isUsableSessionDisplayName(const QString &displayName)
{
    if (displayName.isEmpty()) {
        return false;
    }
    if (displayName.startsWith(QLatin1Char('@'))) {
        return false;
    }
    if (displayName.startsWith(QStringLiteral("PID "))) {
        return false;
    }
    return true;
}

int devicePreferenceScore(const QString &deviceName)
{
    if (deviceName.contains(QStringLiteral("Steam Streaming"), Qt::CaseInsensitive)) {
        return 0;
    }
    if (deviceName.contains(QStringLiteral("Microphone"), Qt::CaseInsensitive)) {
        return 1;
    }
    return 10;
}

int sessionStateScore(AudioSessionState state)
{
    if (state == AudioSessionStateActive) {
        return 2;
    }
    if (state == AudioSessionStateInactive) {
        return 1;
    }
    return 0;
}

bool shouldPreferSession(const AudioSessionInfo &candidate,
                         AudioSessionState candidateState,
                         const AudioSessionInfo &existing,
                         AudioSessionState existingState)
{
    const int candidateScore = sessionStateScore(candidateState) * 100 + devicePreferenceScore(candidate.deviceName);
    const int existingScore = sessionStateScore(existingState) * 100 + devicePreferenceScore(existing.deviceName);
    return candidateScore > existingScore;
}

void enumerateDeviceSessions(IMMDevice *device,
                             const QString &deviceId,
                             const QString &deviceName,
                             QHash<unsigned long, AudioSessionInfo> *bestByPid,
                             QHash<unsigned long, AudioSessionState> *stateByPid)
{
    if (!device || !bestByPid || !stateByPid) {
        return;
    }

    IAudioSessionManager2 *sessionManager = nullptr;
    const HRESULT hr = device->Activate(
        __uuidof(IAudioSessionManager2),
        CLSCTX_ALL,
        nullptr,
        reinterpret_cast<void **>(&sessionManager));
    if (FAILED(hr) || !sessionManager) {
        return;
    }

    IAudioSessionEnumerator *sessionEnumerator = nullptr;
    if (FAILED(sessionManager->GetSessionEnumerator(&sessionEnumerator)) || !sessionEnumerator) {
        sessionManager->Release();
        return;
    }

    int count = 0;
    sessionEnumerator->GetCount(&count);

    for (int i = 0; i < count; ++i) {
        IAudioSessionControl *sessionControl = nullptr;
        if (FAILED(sessionEnumerator->GetSession(i, &sessionControl)) || !sessionControl) {
            continue;
        }

        IAudioSessionControl2 *sessionControl2 = nullptr;
        const HRESULT qiHr = sessionControl->QueryInterface(__uuidof(IAudioSessionControl2),
                                                            reinterpret_cast<void **>(&sessionControl2));
        sessionControl->Release();
        if (FAILED(qiHr) || !sessionControl2) {
            continue;
        }

        AudioSessionState state = AudioSessionStateInactive;
        sessionControl2->GetState(&state);
        if (state == AudioSessionStateExpired) {
            sessionControl2->Release();
            continue;
        }

        DWORD processId = 0;
        if (FAILED(sessionControl2->GetProcessId(&processId)) || processId == 0) {
            sessionControl2->Release();
            continue;
        }

        if (processId == static_cast<DWORD>(GetCurrentProcessId())) {
            sessionControl2->Release();
            continue;
        }

        LPWSTR displayName = nullptr;
        sessionControl2->GetDisplayName(&displayName);

        LPWSTR sessionId = nullptr;
        sessionControl2->GetSessionIdentifier(&sessionId);

        AudioSessionInfo info;
        info.processId = processId;
        info.displayName = wideToQString(displayName);
        info.sessionId = wideToQString(sessionId);
        info.deviceId = deviceId;
        info.deviceName = deviceName;

        if (!isUsableSessionDisplayName(info.displayName)) {
            info.displayName = AppIconProvider::displayNameForProcess(processId);
        }
        if (info.displayName.isEmpty()) {
            info.displayName = QStringLiteral("PID %1").arg(processId);
        }

        if (!bestByPid->contains(processId)
            || shouldPreferSession(info, state, bestByPid->value(processId), stateByPid->value(processId))) {
            bestByPid->insert(processId, info);
            stateByPid->insert(processId, state);
        }

        if (displayName) {
            CoTaskMemFree(displayName);
        }
        if (sessionId) {
            CoTaskMemFree(sessionId);
        }

        sessionControl2->Release();
    }

    sessionEnumerator->Release();
    sessionManager->Release();
}

} // namespace

QVector<AudioSessionInfo> AudioSessionEnumerator::listActiveSessions()
{
    QVector<AudioSessionInfo> sessions;
    const QString tag = QStringLiteral("SessionEnum");

    IMMDeviceEnumerator *deviceEnumerator = nullptr;
    const HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void **>(&deviceEnumerator));
    if (FAILED(hr)) {
        AudioLog::logHresult(tag, QStringLiteral("CoCreateInstance(MMDeviceEnumerator)"), hr);
        return sessions;
    }

    const QVector<AudioRenderDeviceInfo> devices = AudioPolicyRouter::listRenderDevices();
    QHash<unsigned long, AudioSessionInfo> bestByPid;
    QHash<unsigned long, AudioSessionState> stateByPid;

    for (const AudioRenderDeviceInfo &renderDevice : devices) {
        IMMDevice *device = nullptr;
        const HRESULT openHr = deviceEnumerator->GetDevice(
            reinterpret_cast<LPCWSTR>(renderDevice.id.utf16()),
            &device);
        if (FAILED(openHr) || !device) {
            continue;
        }

        enumerateDeviceSessions(device,
                                renderDevice.id,
                                renderDevice.friendlyName,
                                &bestByPid,
                                &stateByPid);
        device->Release();
    }

    deviceEnumerator->Release();

    sessions.reserve(bestByPid.size());
    for (auto it = bestByPid.constBegin(); it != bestByPid.constEnd(); ++it) {
        const AudioSessionInfo &info = it.value();
        sessions.push_back(info);
        AudioLog::info(tag,
                        QStringLiteral("Session: %1 (pid=%2, device=%3)")
                            .arg(info.displayName)
                            .arg(info.processId)
                            .arg(info.deviceName));
    }

    return sessions;
}
