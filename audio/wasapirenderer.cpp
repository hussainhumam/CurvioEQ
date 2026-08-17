#include "wasapirenderer.h"

#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <ksmedia.h>

#include <algorithm>
#include <cmath>
#include <cstring>
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

    m_bufferEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_bufferEvent) {
        const QString message = QStringLiteral("[WasapiRenderer] CreateEvent failed");
        AudioLog::error(tag, message);
        if (errorMessage) {
            *errorMessage = message;
        }
        close();
        return false;
    }

    const DWORD eventFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;
    hr = m_audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        eventFlags,
        0,
        0,
        m_format,
        nullptr);
    m_eventDriven = SUCCEEDED(hr);
    if (FAILED(hr)) {
        hr = m_audioClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
            0,
            0,
            m_format,
            nullptr);
    }
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

    if (m_eventDriven) {
        hr = m_audioClient->SetEventHandle(m_bufferEvent);
        if (FAILED(hr)) {
            AudioLog::warn(tag, QStringLiteral("SetEventHandle failed; falling back to polling"));
            m_eventDriven = false;
        }
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

    REFERENCE_TIME defaultPeriod = 0;
    REFERENCE_TIME minimumPeriod = 0;
    hr = m_audioClient->GetDevicePeriod(&defaultPeriod, &minimumPeriod);
    m_sampleRate = static_cast<float>(m_format->nSamplesPerSec);
    if (SUCCEEDED(hr) && defaultPeriod > 0 && m_sampleRate > 0.f) {
        const double periodFrames =
            (static_cast<double>(defaultPeriod) * static_cast<double>(m_sampleRate)) / 10000000.0;
        m_periodFrameCount = static_cast<UINT32>(std::max(1.0, std::round(periodFrames)));
    } else {
        m_periodFrameCount = std::max<UINT32>(1, static_cast<UINT32>(m_sampleRate / 100.f));
    }
    if (m_bufferFrameCount > 1) {
        m_periodFrameCount = std::min(m_periodFrameCount, m_bufferFrameCount / 2);
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

    m_channelCount = m_format->nChannels;
    m_formatIsFloat = formatIsFloat(m_format);
    buildLogicalChannelMap();
    m_upmixBuffer.assign(static_cast<size_t>(m_bufferFrameCount * m_channelCount), 0.f);

    if (!prerollSilence(errorMessage)) {
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

    AudioLog::info(tag, QStringLiteral("Render opened: %1 Hz, %2 channels, period=%3 frames, buffer=%4 frames, event=%5, float=%6")
                             .arg(m_sampleRate)
                             .arg(m_channelCount)
                             .arg(m_periodFrameCount)
                             .arg(m_bufferFrameCount)
                             .arg(m_eventDriven)
                             .arg(m_formatIsFloat));
    return true;
}

void WasapiRenderer::close()
{
    interruptWait();
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
    if (m_bufferEvent) {
        CloseHandle(m_bufferEvent);
        m_bufferEvent = nullptr;
    }
    m_sampleRate = 0.f;
    m_channelCount = 0;
    m_bufferFrameCount = 0;
    m_periodFrameCount = 480;
    m_formatIsFloat = true;
    m_eventDriven = false;
    m_upmixBuffer.clear();
    m_logicalToDevice.fill(-1);
    m_hasLogicalChannelMap = false;
}

void WasapiRenderer::interruptWait()
{
    if (m_bufferEvent) {
        SetEvent(m_bufferEvent);
    }
}

UINT32 WasapiRenderer::availableWriteFrames() const
{
    if (!m_audioClient || m_bufferFrameCount == 0) {
        return 0;
    }

    UINT32 padding = 0;
    if (FAILED(m_audioClient->GetCurrentPadding(&padding))) {
        return 0;
    }
    if (padding >= m_bufferFrameCount) {
        return 0;
    }
    return m_bufferFrameCount - padding;
}

bool WasapiRenderer::waitForNextPeriod(DWORD timeoutMs)
{
    if (m_eventDriven && m_bufferEvent) {
        const DWORD result = WaitForSingleObject(m_bufferEvent, timeoutMs);
        return result == WAIT_OBJECT_0 || result == WAIT_TIMEOUT;
    }

    const DWORD sleepMs = m_sampleRate > 0.f
                              ? std::max<DWORD>(1, static_cast<DWORD>((m_periodFrameCount * 1000.f) / m_sampleRate))
                              : 10;
    Sleep(std::min(sleepMs, timeoutMs == INFINITE ? sleepMs : timeoutMs));
    return true;
}

bool WasapiRenderer::prerollSilence(QString *errorMessage)
{
    if (!m_renderClient || m_bufferFrameCount == 0) {
        return true;
    }

    BYTE *data = nullptr;
    HRESULT hr = m_renderClient->GetBuffer(m_bufferFrameCount, &data);
    if (FAILED(hr)) {
        const QString message = QStringLiteral("[WasapiRenderer] Preroll GetBuffer failed: %1")
                                    .arg(AudioLog::hresultToString(hr));
        AudioLog::error(QStringLiteral("WasapiRenderer"), message);
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    }

    hr = m_renderClient->ReleaseBuffer(m_bufferFrameCount, AUDCLNT_BUFFERFLAGS_SILENT);
    if (FAILED(hr)) {
        const QString message = QStringLiteral("[WasapiRenderer] Preroll ReleaseBuffer failed: %1")
                                    .arg(AudioLog::hresultToString(hr));
        AudioLog::error(QStringLiteral("WasapiRenderer"), message);
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    }
    return true;
}

bool WasapiRenderer::copyFramesToDevice(const float *source, int framesToWrite, QString *errorMessage)
{
    BYTE *data = nullptr;
    HRESULT hr = m_renderClient->GetBuffer(static_cast<UINT32>(framesToWrite), &data);
    if (FAILED(hr)) {
        const QString message = QStringLiteral("[WasapiRenderer] GetBuffer failed: %1")
                                    .arg(AudioLog::hresultToString(hr));
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    }

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
    return true;
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
        return;
    }

    const size_t neededSamples = static_cast<size_t>(frameCount * m_channelCount);
    if (m_upmixBuffer.size() < neededSamples) {
        m_upmixBuffer.resize(neededSamples);
    }
    std::fill(m_upmixBuffer.begin(), m_upmixBuffer.begin() + static_cast<ptrdiff_t>(neededSamples), 0.f);

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

    const UINT32 availableFrames = availableWriteFrames();
    if (availableFrames == 0) {
        return true;
    }

    const int framesToWrite = static_cast<int>(std::min({
        availableFrames,
        static_cast<UINT32>(frameCount),
        m_periodFrameCount,
    }));
    if (framesToWrite <= 0) {
        return true;
    }

    const float *writeSource = interleavedBuffer;
    if (inputChannelCount != m_channelCount) {
        upmixToDeviceFormat(interleavedBuffer, framesToWrite, inputChannelCount);
        writeSource = m_upmixBuffer.data();
    }

    return copyFramesToDevice(writeSource, framesToWrite, errorMessage);
}
