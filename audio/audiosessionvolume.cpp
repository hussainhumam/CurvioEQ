#include "audiosessionvolume.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>

#include <algorithm>
#include <vector>

#include <QString>

namespace {

std::vector<ISimpleAudioVolume *> collectSimpleVolumesForProcess(unsigned long processId)
{
    std::vector<ISimpleAudioVolume *> volumes;
    if (processId == 0) {
        return volumes;
    }

    IMMDeviceEnumerator *deviceEnumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                  nullptr,
                                  CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void **>(&deviceEnumerator));
    if (FAILED(hr) || !deviceEnumerator) {
        return volumes;
    }

    IMMDeviceCollection *collection = nullptr;
    hr = deviceEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
    deviceEnumerator->Release();
    if (FAILED(hr) || !collection) {
        return volumes;
    }

    UINT deviceCount = 0;
    collection->GetCount(&deviceCount);

    for (UINT deviceIndex = 0; deviceIndex < deviceCount; ++deviceIndex) {
        IMMDevice *device = nullptr;
        if (FAILED(collection->Item(deviceIndex, &device)) || !device) {
            continue;
        }

        IAudioSessionManager2 *sessionManager = nullptr;
        hr = device->Activate(__uuidof(IAudioSessionManager2),
                              CLSCTX_ALL,
                              nullptr,
                              reinterpret_cast<void **>(&sessionManager));
        device->Release();
        if (FAILED(hr) || !sessionManager) {
            continue;
        }

        IAudioSessionEnumerator *sessionEnumerator = nullptr;
        if (FAILED(sessionManager->GetSessionEnumerator(&sessionEnumerator)) || !sessionEnumerator) {
            sessionManager->Release();
            continue;
        }

        int sessionCount = 0;
        sessionEnumerator->GetCount(&sessionCount);

        for (int sessionIndex = 0; sessionIndex < sessionCount; ++sessionIndex) {
            IAudioSessionControl *sessionControl = nullptr;
            if (FAILED(sessionEnumerator->GetSession(sessionIndex, &sessionControl)) || !sessionControl) {
                continue;
            }

            IAudioSessionControl2 *sessionControl2 = nullptr;
            hr = sessionControl->QueryInterface(__uuidof(IAudioSessionControl2),
                                                reinterpret_cast<void **>(&sessionControl2));
            sessionControl->Release();
            if (FAILED(hr) || !sessionControl2) {
                continue;
            }

            DWORD sessionProcessId = 0;
            if (SUCCEEDED(sessionControl2->GetProcessId(&sessionProcessId))
                && sessionProcessId == processId) {
                ISimpleAudioVolume *volume = nullptr;
                hr = sessionControl2->QueryInterface(__uuidof(ISimpleAudioVolume),
                                                     reinterpret_cast<void **>(&volume));
                if (SUCCEEDED(hr) && volume) {
                    volumes.push_back(volume);
                }
            }

            sessionControl2->Release();
        }

        sessionEnumerator->Release();
        sessionManager->Release();
    }

    collection->Release();
    return volumes;
}

void releaseVolumes(const std::vector<ISimpleAudioVolume *> &volumes)
{
    for (ISimpleAudioVolume *volume : volumes) {
        if (volume) {
            volume->Release();
        }
    }
}

} // namespace

bool AudioSessionVolume::getMute(unsigned long processId, bool *muted, QString *errorMessage)
{
    if (!muted) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid mute output");
        }
        return false;
    }

    const std::vector<ISimpleAudioVolume *> volumes = collectSimpleVolumesForProcess(processId);
    if (volumes.empty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No active audio session found for that app");
        }
        return false;
    }

    BOOL value = FALSE;
    const HRESULT hr = volumes.front()->GetMute(&value);
    releaseVolumes(volumes);
    if (FAILED(hr)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to read app mute state");
        }
        return false;
    }

    *muted = value != FALSE;
    return true;
}

bool AudioSessionVolume::setMute(unsigned long processId, bool muted, QString *errorMessage)
{
    const std::vector<ISimpleAudioVolume *> volumes = collectSimpleVolumesForProcess(processId);
    if (volumes.empty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No active audio session found for that app");
        }
        return false;
    }

    bool anySucceeded = false;
    for (ISimpleAudioVolume *volume : volumes) {
        const HRESULT hr = volume->SetMute(muted ? TRUE : FALSE, nullptr);
        if (SUCCEEDED(hr)) {
            anySucceeded = true;
        }
    }
    releaseVolumes(volumes);

    if (!anySucceeded) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to set app mute state");
        }
        return false;
    }
    return true;
}

bool AudioSessionVolume::toggleMute(unsigned long processId, QString *errorMessage)
{
    const std::vector<ISimpleAudioVolume *> volumes = collectSimpleVolumesForProcess(processId);
    if (volumes.empty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No active audio session found for that app");
        }
        return false;
    }

    BOOL muted = FALSE;
    HRESULT hr = volumes.front()->GetMute(&muted);
    if (FAILED(hr)) {
        releaseVolumes(volumes);
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to read app mute state");
        }
        return false;
    }

    bool anySucceeded = false;
    for (ISimpleAudioVolume *volume : volumes) {
        hr = volume->SetMute(muted ? FALSE : TRUE, nullptr);
        if (SUCCEEDED(hr)) {
            anySucceeded = true;
        }
    }
    releaseVolumes(volumes);

    if (!anySucceeded) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to toggle app mute state");
        }
        return false;
    }
    return true;
}

bool AudioSessionVolume::getMasterVolume(unsigned long processId, float *level01, QString *errorMessage)
{
    if (!level01) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid volume output");
        }
        return false;
    }

    const std::vector<ISimpleAudioVolume *> volumes = collectSimpleVolumesForProcess(processId);
    if (volumes.empty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No active audio session found for that app");
        }
        return false;
    }

    bool anySucceeded = false;
    for (ISimpleAudioVolume *volume : volumes) {
        float level = 1.f;
        const HRESULT hr = volume->GetMasterVolume(&level);
        if (SUCCEEDED(hr)) {
            *level01 = std::clamp(level, 0.f, 1.f);
            anySucceeded = true;
            break;
        }
    }
    releaseVolumes(volumes);

    if (!anySucceeded) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to read app volume");
        }
        return false;
    }
    return true;
}

bool AudioSessionVolume::setMasterVolume(unsigned long processId, float level01, QString *errorMessage)
{
    const float clamped = std::clamp(level01, 0.f, 1.f);
    const std::vector<ISimpleAudioVolume *> volumes = collectSimpleVolumesForProcess(processId);
    if (volumes.empty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No active audio session found for that app");
        }
        return false;
    }

    bool anySucceeded = false;
    for (ISimpleAudioVolume *volume : volumes) {
        const HRESULT hr = volume->SetMasterVolume(clamped, nullptr);
        if (SUCCEEDED(hr)) {
            anySucceeded = true;
        }
    }
    releaseVolumes(volumes);

    if (!anySucceeded) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to set app volume");
        }
        return false;
    }
    return true;
}
