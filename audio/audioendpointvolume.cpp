#include "audioendpointvolume.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>

#include <QString>

namespace {

bool activateEndpointVolume(const QString &deviceId, IAudioEndpointVolume **volume, QString *errorMessage)
{
    if (deviceId.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No audio device selected");
        }
        return false;
    }

    IMMDeviceEnumerator *enumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                    nullptr,
                                    CLSCTX_ALL,
                                    __uuidof(IMMDeviceEnumerator),
                                    reinterpret_cast<void **>(&enumerator));
    if (FAILED(hr) || !enumerator) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to create audio device enumerator");
        }
        return false;
    }

    IMMDevice *device = nullptr;
    hr = enumerator->GetDevice(reinterpret_cast<LPCWSTR>(deviceId.utf16()), &device);
    enumerator->Release();
    if (FAILED(hr) || !device) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to open audio device");
        }
        return false;
    }

    hr = device->Activate(__uuidof(IAudioEndpointVolume),
                          CLSCTX_ALL,
                          nullptr,
                          reinterpret_cast<void **>(volume));
    device->Release();
    if (FAILED(hr) || !*volume) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to access device volume control");
        }
        return false;
    }

    return true;
}

} // namespace

bool AudioEndpointVolume::getMute(const QString &deviceId, bool *muted, QString *errorMessage)
{
    if (!muted) {
        return false;
    }

    IAudioEndpointVolume *volume = nullptr;
    if (!activateEndpointVolume(deviceId, &volume, errorMessage)) {
        return false;
    }

    BOOL nativeMuted = FALSE;
    const HRESULT hr = volume->GetMute(&nativeMuted);
    volume->Release();

    if (FAILED(hr)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to read device mute state");
        }
        return false;
    }

    *muted = nativeMuted == TRUE;
    return true;
}

bool AudioEndpointVolume::setMute(const QString &deviceId, bool muted, QString *errorMessage)
{
    IAudioEndpointVolume *volume = nullptr;
    if (!activateEndpointVolume(deviceId, &volume, errorMessage)) {
        return false;
    }

    const HRESULT hr = volume->SetMute(muted ? TRUE : FALSE, nullptr);
    volume->Release();

    if (FAILED(hr)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to set device mute state");
        }
        return false;
    }

    return true;
}

bool AudioEndpointVolume::toggleMute(const QString &deviceId, QString *errorMessage)
{
    IAudioEndpointVolume *volume = nullptr;
    if (!activateEndpointVolume(deviceId, &volume, errorMessage)) {
        return false;
    }

    BOOL muted = FALSE;
    HRESULT hr = volume->GetMute(&muted);
    if (FAILED(hr)) {
        volume->Release();
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to read device mute state");
        }
        return false;
    }

    hr = volume->SetMute(muted ? FALSE : TRUE, nullptr);
    volume->Release();

    if (FAILED(hr)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to toggle device mute state");
        }
        return false;
    }

    return true;
}
