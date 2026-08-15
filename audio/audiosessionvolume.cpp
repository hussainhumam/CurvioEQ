#include "audiosessionvolume.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>

namespace {

ISimpleAudioVolume *findSimpleVolumeForProcess(unsigned long processId)
{
    if (processId == 0) {
        return nullptr;
    }

    IMMDeviceEnumerator *deviceEnumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                  nullptr,
                                  CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void **>(&deviceEnumerator));
    if (FAILED(hr) || !deviceEnumerator) {
        return nullptr;
    }

    IMMDeviceCollection *collection = nullptr;
    hr = deviceEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
    deviceEnumerator->Release();
    if (FAILED(hr) || !collection) {
        return nullptr;
    }

    UINT deviceCount = 0;
    collection->GetCount(&deviceCount);

    ISimpleAudioVolume *foundVolume = nullptr;

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
                hr = sessionControl2->QueryInterface(__uuidof(ISimpleAudioVolume),
                                                     reinterpret_cast<void **>(&foundVolume));
            }

            sessionControl2->Release();
            if (foundVolume) {
                break;
            }
        }

        sessionEnumerator->Release();
        sessionManager->Release();

        if (foundVolume) {
            break;
        }
    }

    collection->Release();
    return foundVolume;
}

} // namespace

bool AudioSessionVolume::setMute(unsigned long processId, bool muted, QString *errorMessage)
{
    ISimpleAudioVolume *volume = findSimpleVolumeForProcess(processId);
    if (!volume) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No active audio session found for that app");
        }
        return false;
    }

    const HRESULT hr = volume->SetMute(muted ? TRUE : FALSE, nullptr);
    volume->Release();

    if (FAILED(hr)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to set app mute state");
        }
        return false;
    }

    return true;
}

bool AudioSessionVolume::toggleMute(unsigned long processId, QString *errorMessage)
{
    ISimpleAudioVolume *volume = findSimpleVolumeForProcess(processId);
    if (!volume) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No active audio session found for that app");
        }
        return false;
    }

    BOOL muted = FALSE;
    HRESULT hr = volume->GetMute(&muted);
    if (FAILED(hr)) {
        volume->Release();
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to read app mute state");
        }
        return false;
    }

    hr = volume->SetMute(muted ? FALSE : TRUE, nullptr);
    volume->Release();

    if (FAILED(hr)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to toggle app mute state");
        }
        return false;
    }

    return true;
}
