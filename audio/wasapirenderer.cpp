#include "wasapirenderer.h"

#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <ksmedia.h>

#include <cstring>
#include <algorithm>
#include <vector>

namespace {

bool formatIsFloat(const WAVEFORMATEX *format)
{
    if (!format) {
        return false;
    }
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        return true;
    }
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= 22) {
        const auto *extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(format);
        return IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }
    return false;
}

WAVEFORMATEX *copyFormat(const WAVEFORMATEX *source)
{
    if (!source) {
        return nullptr;
    }
    const size_t size = sizeof(WAVEFORMATEX) + source->cbSize;
    auto *copy = static_cast<WAVEFORMATEX *>(CoTaskMemAlloc(size));
    if (!copy) {
        return nullptr;
    }
    std::memcpy(copy, source, size);
    return copy;
}

int speakerBitIndex(DWORD channelMask, DWORD speakerBit)
{
    if ((channelMask & speakerBit) == 0) {
        return -1;
    }

    int index = 0;
    for (DWORD bit = 1; bit < speakerBit; bit <<= 1) {
        if (channelMask & bit) {
            ++index;
        }
    }
    return index;
}

} // namespace

WasapiRenderer::~WasapiRenderer()
{
    close();
}

bool WasapiRenderer::open(const QString &deviceId,
                          float /*sampleRate*/,
                          int /*channelCount*/,
                          QString *errorMessage)
{
    const QString tag = QStringLiteral("WasapiRenderer");
    close();

    IMMDeviceEnumerator *deviceEnumerator = nullptr;
    IMMDevice *device = nullptr;

    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void **>(&deviceEnumerator));
    if (FAILED(hr)) {
        const QString message = QStringLiteral("[WasapiRenderer] CoCreateInstance(MMDeviceEnumerator) failed: %1")
                                    .arg(AudioLog::hresultToString(hr));
        AudioLog::error(tag, message);
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    }

    if (deviceId.isEmpty()) {
        hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    } else {
        hr = deviceEnumerator->GetDevice(reinterpret_cast<LPCWSTR>(deviceId.utf16()), &device);
    }
    deviceEnumerator->Release();
    if (FAILED(hr)) {
        const QString message = QStringLiteral("[WasapiRenderer] Open render device failed: %1")
                                    .arg(AudioLog::hresultToString(hr));
        AudioLog::error(tag, message);
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    }

    hr = device->Activate(
        __uuidof(IAudioClient),
        CLSCTX_ALL,
        nullptr,
        reinterpret_cast<void **>(&m_audioClient));
    device->Release();
    if (FAILED(hr)) {
        const QString message = QStringLiteral("[WasapiRenderer] Activate(IAudioClient) failed: %1")
                                    .arg(AudioLog::hresultToString(hr));
        AudioLog::error(tag, message);
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    }

    WAVEFORMATEX *mixFormat = nullptr;
    hr = m_audioClient->GetMixFormat(&mixFormat);
    if (FAILED(hr) || !mixFormat) {
        const QString message = QStringLiteral("[WasapiRenderer] GetMixFormat failed: %1")
                                    .arg(AudioLog::hresultToString(hr));
        AudioLog::error(tag, message);
        if (errorMessage) {
            *errorMessage = message;
        }
        close();
        return false;
    }

    m_format = copyFormat(mixFormat);
    CoTaskMemFree(mixFormat);
    if (!m_format) {
        const QString message = QStringLiteral("[WasapiRenderer] Failed to copy mix format");
        AudioLog::error(tag, message);
        if (errorMessage) {
            *errorMessage = message;
        }
        close();
        return false;
    }

    hr = m_audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
        0,
        0,
        m_format,
        nullptr);
    if (FAILED(hr)) {
        const QString message = QStringLiteral("[WasapiRenderer] IAudioClient::Initialize failed: %1")
                                    .arg(AudioLog::hresultToString(hr));
        AudioLog::error(tag, message);
        if (errorMessage) {
            *errorMessage = message;
        }
        close();
        return false;
    }

    hr = m_audioClient->GetBufferSize(&m_bufferFrameCount);
    if (FAILED(hr)) {
        const QString message = QStringLiteral("[WasapiRenderer] GetBufferSize failed: %1")
                                    .arg(AudioLog::hresultToString(hr));
        AudioLog::error(tag, message);
        if (errorMessage) {
            *errorMessage = message;
        }
        close();
        return false;
    }

    hr = m_audioClient->GetService(__uuidof(IAudioRenderClient),
                                   reinterpret_cast<void **>(&m_renderClient));
    if (FAILED(hr)) {
        const QString message = QStringLiteral("[WasapiRenderer] GetService(IAudioRenderClient) failed: %1")
                                    .arg(AudioLog::hresultToString(hr));
        AudioLog::error(tag, message);
        if (errorMessage) {
            *errorMessage = message;
        }
        close();
        return false;
    }

    hr = m_audioClient->Start();
    if (FAILED(hr)) {
        const QString message = QStringLiteral("[WasapiRenderer] IAudioClient::Start failed: %1")
                                    .arg(AudioLog::hresultToString(hr));
        AudioLog::error(tag, message);
        if (errorMessage) {
            *errorMessage = message;
        }
        close();
        return false;
    }

    m_sampleRate = static_cast<float>(m_format->nSamplesPerSec);
    m_channelCount = m_format->nChannels;
    m_formatIsFloat = formatIsFloat(m_format);
    buildLogicalChannelMap();

    AudioLog::info(tag, QStringLiteral("Render opened: %1 Hz, %2 channels, buffer=%3 frames, float=%4")
                             .arg(m_sampleRate)
                             .arg(m_channelCount)
                             .arg(m_bufferFrameCount)
                             .arg(m_formatIsFloat));
    return true;
}

void WasapiRenderer::close()
{
    if (m_audioClient) {
        m_audioClient->Stop();
    }
    if (m_renderClient) {
        m_renderClient->Release();
        m_renderClient = nullptr;
    }
    if (m_audioClient) {
        m_audioClient->Release();
        m_audioClient = nullptr;
    }
    if (m_format) {
        CoTaskMemFree(m_format);
        m_format = nullptr;
    }
    m_sampleRate = 0.f;
    m_channelCount = 0;
    m_bufferFrameCount = 0;
    m_formatIsFloat = true;
    m_upmixBuffer.clear();
    m_logicalToDevice.fill(-1);
    m_hasLogicalChannelMap = false;
}

void WasapiRenderer::buildLogicalChannelMap()
{
    m_logicalToDevice.fill(-1);
    m_hasLogicalChannelMap = false;

    if (!m_format || m_channelCount <= 0) {
        return;
    }

    const DWORD logicalSpeakers[8] = {
        SPEAKER_FRONT_LEFT,
        SPEAKER_FRONT_RIGHT,
        SPEAKER_FRONT_CENTER,
        SPEAKER_LOW_FREQUENCY,
        SPEAKER_BACK_LEFT,
        SPEAKER_BACK_RIGHT,
        SPEAKER_SIDE_LEFT,
        SPEAKER_SIDE_RIGHT,
    };

    DWORD channelMask = 0;
    if (m_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && m_format->cbSize >= 22) {
        const auto *extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(m_format);
        channelMask = extensible->dwChannelMask;
    }

    if (channelMask != 0) {
        for (int logical = 0; logical < 8; ++logical) {
            m_logicalToDevice[static_cast<size_t>(logical)] =
                speakerBitIndex(channelMask, logicalSpeakers[logical]);
        }
        m_hasLogicalChannelMap = true;
        return;
    }

    const int mappedCount = std::min(8, m_channelCount);
    for (int logical = 0; logical < mappedCount; ++logical) {
        m_logicalToDevice[static_cast<size_t>(logical)] = logical;
    }
    m_hasLogicalChannelMap = mappedCount > 0;
}

void WasapiRenderer::upmixToDeviceFormat(const float *input, int frameCount, int inputChannelCount)
{
    if (!input || frameCount <= 0 || inputChannelCount <= 0 || m_channelCount <= 0) {
        m_upmixBuffer.clear();
        return;
    }

    m_upmixBuffer.assign(static_cast<size_t>(frameCount * m_channelCount), 0.f);

    if (inputChannelCount == 8 && m_hasLogicalChannelMap) {
        for (int frame = 0; frame < frameCount; ++frame) {
            for (int logical = 0; logical < 8; ++logical) {
                const int deviceChannel = m_logicalToDevice[static_cast<size_t>(logical)];
                if (deviceChannel < 0 || deviceChannel >= m_channelCount) {
                    continue;
                }
                m_upmixBuffer[static_cast<size_t>(frame * m_channelCount + deviceChannel)] =
                    input[static_cast<size_t>(frame * inputChannelCount + logical)];
            }
        }
        return;
    }

    const int channelsToCopy = std::min(inputChannelCount, m_channelCount);
    for (int frame = 0; frame < frameCount; ++frame) {
        for (int channel = 0; channel < channelsToCopy; ++channel) {
            m_upmixBuffer[static_cast<size_t>(frame * m_channelCount + channel)] =
                input[static_cast<size_t>(frame * inputChannelCount + channel)];
        }
    }
}

bool WasapiRenderer::write(const float *interleavedBuffer, int frameCount, int inputChannelCount, QString *errorMessage)
{
    if (!m_renderClient || !interleavedBuffer || frameCount <= 0 || inputChannelCount <= 0 || !m_format) {
        return false;
    }

    const float *writeSource = interleavedBuffer;
    if (inputChannelCount != m_channelCount) {
        upmixToDeviceFormat(interleavedBuffer, frameCount, inputChannelCount);
        writeSource = m_upmixBuffer.data();
    }

    int framesWritten = 0;
    while (framesWritten < frameCount) {
        UINT32 padding = 0;
        HRESULT hr = m_audioClient->GetCurrentPadding(&padding);
        if (FAILED(hr)) {
            const QString message = QStringLiteral("[WasapiRenderer] GetCurrentPadding failed: %1")
                                        .arg(AudioLog::hresultToString(hr));
            if (errorMessage) {
                *errorMessage = message;
            }
            return false;
        }

        const UINT32 availableFrames = m_bufferFrameCount - padding;
        if (availableFrames == 0) {
            Sleep(1);
            continue;
        }

        const int framesToWrite = static_cast<int>(std::min<UINT32>(
            availableFrames,
            static_cast<UINT32>(frameCount - framesWritten)));

        BYTE *data = nullptr;
        hr = m_renderClient->GetBuffer(static_cast<UINT32>(framesToWrite), &data);
        if (FAILED(hr)) {
            const QString message = QStringLiteral("[WasapiRenderer] GetBuffer failed: %1")
                                        .arg(AudioLog::hresultToString(hr));
            if (errorMessage) {
                *errorMessage = message;
            }
            return false;
        }

        const float *source = writeSource + (framesWritten * m_channelCount);
        if (m_formatIsFloat) {
            std::memcpy(
                data,
                source,
                static_cast<size_t>(framesToWrite * m_channelCount) * sizeof(float));
        } else if (m_format->wBitsPerSample == 16) {
            auto *destination = reinterpret_cast<int16_t *>(data);
            for (int frame = 0; frame < framesToWrite; ++frame) {
                for (int channel = 0; channel < m_channelCount; ++channel) {
                    const float sample = source[frame * m_channelCount + channel];
                    const float clamped = std::max(-1.f, std::min(1.f, sample));
                    destination[frame * m_channelCount + channel] =
                        static_cast<int16_t>(clamped * 32767.f);
                }
            }
        } else {
            const QString message = QStringLiteral("[WasapiRenderer] Unsupported render format (bits=%1)")
                                        .arg(m_format->wBitsPerSample);
            if (errorMessage) {
                *errorMessage = message;
            }
            m_renderClient->ReleaseBuffer(static_cast<UINT32>(framesToWrite), 0);
            return false;
        }

        hr = m_renderClient->ReleaseBuffer(static_cast<UINT32>(framesToWrite), 0);
        if (FAILED(hr)) {
            const QString message = QStringLiteral("[WasapiRenderer] ReleaseBuffer failed: %1")
                                        .arg(AudioLog::hresultToString(hr));
            if (errorMessage) {
                *errorMessage = message;
            }
            return false;
        }

        framesWritten += framesToWrite;
    }

    return true;
}
