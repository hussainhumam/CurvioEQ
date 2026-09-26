#include "wasapirenderer.h"

#include "log.h"
#include "engineiosettings.h"
#include "ui/appconstants.h"
#include "wasapierror.h"
#include "wasapilowlatency.h"

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

void setFormatSampleRate(WAVEFORMATEX *format, DWORD sampleRate)
{
    if (!format || sampleRate == 0) {
        return;
    }
    format->nSamplesPerSec = sampleRate;
    format->nAvgBytesPerSec = format->nSamplesPerSec * format->nBlockAlign;
}

void updateFormatBlockAlign(WAVEFORMATEX *format)
{
    if (!format || format->nChannels == 0 || format->wBitsPerSample == 0) {
        return;
    }
    format->nBlockAlign = static_cast<WORD>((format->nChannels * format->wBitsPerSample) / 8);
    format->nAvgBytesPerSec = format->nSamplesPerSec * format->nBlockAlign;
}

void applyRequestedOutputFormat(WAVEFORMATEX *format, OutputFormat requested)
{
    if (!format || requested == OutputFormat::Auto) {
        return;
    }

    const bool asFloat = requested == OutputFormat::Float32;
    const WORD bits = asFloat ? 32 : 16;
    format->wBitsPerSample = bits;
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= 22) {
        auto *extensible = reinterpret_cast<WAVEFORMATEXTENSIBLE *>(format);
        extensible->Samples.wValidBitsPerSample = bits;
        extensible->SubFormat = asFloat ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : KSDATAFORMAT_SUBTYPE_PCM;
    } else {
        format->wFormatTag = asFloat ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
        format->cbSize = 0;
    }
    updateFormatBlockAlign(format);
}

REFERENCE_TIME requestedBufferDuration(int bufferFrames, DWORD sampleRate)
{
    if (bufferFrames <= 0 || sampleRate == 0) {
        return 0;
    }
    const int periodFrames = AppConstants::requestedEnginePeriodFrames(bufferFrames);
    return static_cast<REFERENCE_TIME>(
        (static_cast<long long>(periodFrames) * 10000000LL) / static_cast<long long>(sampleRate));
}

} // namespace

WasapiRenderer::~WasapiRenderer()
{
    close();
}

bool WasapiRenderer::open(const QString &deviceId, const EngineIoSettings &settings, QString *errorMessage)
{
    const QString tag = QStringLiteral("WasapiRenderer");
    close();

    const float sampleRate = static_cast<float>(settings.sampleRate);
    const int bufferFrames = settings.requestedBufferFrames();
    const bool useDevicePeriod = bufferFrames <= 0;
    const bool requestSampleRate = sampleRate >= 8000.f;

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

    auto activateClient = [&]() -> HRESULT {
        if (m_audioClient) {
            m_audioClient->Release();
            m_audioClient = nullptr;
        }
        return device->Activate(
            __uuidof(IAudioClient),
            CLSCTX_ALL,
            nullptr,
            reinterpret_cast<void **>(&m_audioClient));
    };

    hr = activateClient();
    if (FAILED(hr)) {
        const QString message = QStringLiteral("[WasapiRenderer] Activate(IAudioClient) failed: %1")
                                    .arg(AudioLog::hresultToString(hr));
        AudioLog::error(tag, message);
        if (errorMessage) {
            *errorMessage = message;
        }
        device->Release();
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
        device->Release();
        close();
        return false;
    }

    auto rebuildFormat = [&](bool forceMixEncoding) {
        if (m_format) {
            CoTaskMemFree(m_format);
            m_format = nullptr;
        }
        m_format = copyFormat(mixFormat);
        if (!m_format) {
            return false;
        }
        if (requestSampleRate && static_cast<DWORD>(sampleRate + 0.5f) != mixFormat->nSamplesPerSec) {
            setFormatSampleRate(m_format, static_cast<DWORD>(sampleRate + 0.5f));
        }
        if (!forceMixEncoding) {
            applyRequestedOutputFormat(m_format, settings.outputFormat);
        }
        return true;
    };

    if (!rebuildFormat(false)) {
        const QString message = QStringLiteral("[WasapiRenderer] Failed to copy mix format");
        AudioLog::error(tag, message);
        if (errorMessage) {
            *errorMessage = message;
        }
        CoTaskMemFree(mixFormat);
        device->Release();
        close();
        return false;
    }

    const DWORD mixSampleRate = mixFormat->nSamplesPerSec;
    const DWORD requestedSampleRate = requestSampleRate
                                          ? static_cast<DWORD>(sampleRate + 0.5f)
                                          : mixSampleRate;
    const int snappedBufferFrames =
        useDevicePeriod ? 0 : AppConstants::clampBufferFrames(bufferFrames);
    const REFERENCE_TIME bufferDuration =
        useDevicePeriod ? 0 : requestedBufferDuration(snappedBufferFrames, m_format->nSamplesPerSec);
    const bool usedCustomIo = (requestedSampleRate != mixSampleRate)
        || !useDevicePeriod
        || settings.outputFormat != OutputFormat::Auto
        || settings.preferExclusive();

    m_bufferEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_bufferEvent) {
        const QString message = QStringLiteral("[WasapiRenderer] CreateEvent failed");
        AudioLog::error(tag, message);
        if (errorMessage) {
            *errorMessage = message;
        }
        CoTaskMemFree(mixFormat);
        device->Release();
        close();
        return false;
    }

    const DWORD sharedEventFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;
    const DWORD sharedPollFlags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;
    const DWORD exclusiveEventFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;

    auto tryExclusive = [&]() -> bool {
        REFERENCE_TIME defaultPeriod = 0;
        REFERENCE_TIME minimumPeriod = 0;
        m_audioClient->GetDevicePeriod(&defaultPeriod, &minimumPeriod);
        REFERENCE_TIME hns = bufferDuration;
        if (hns <= 0) {
            hns = defaultPeriod > 0 ? defaultPeriod : minimumPeriod;
        }
        if (minimumPeriod > 0 && hns < minimumPeriod) {
            hns = minimumPeriod;
        }
        if (hns <= 0) {
            hns = 100000;
        }

        HRESULT exclusiveHr = m_audioClient->Initialize(
            AUDCLNT_SHAREMODE_EXCLUSIVE,
            exclusiveEventFlags,
            hns,
            hns,
            m_format,
            nullptr);
        m_eventDriven = SUCCEEDED(exclusiveHr);
        if (FAILED(exclusiveHr)) {
            exclusiveHr = activateClient();
            if (FAILED(exclusiveHr)) {
                return false;
            }
            exclusiveHr = m_audioClient->Initialize(
                AUDCLNT_SHAREMODE_EXCLUSIVE,
                0,
                hns,
                hns,
                m_format,
                nullptr);
            m_eventDriven = false;
        }
        return SUCCEEDED(exclusiveHr);
    };

    auto tryShared = [&]() -> bool {
        const SharedModeEnginePeriod engine = querySharedModeEnginePeriod(m_audioClient, m_format);
        UINT32 requestedPeriod = 0;
        if (useDevicePeriod) {
            requestedPeriod = engine.ok ? engine.defaultPeriod : 0;
        } else {
            requestedPeriod = static_cast<UINT32>(AppConstants::requestedEnginePeriodFrames(snappedBufferFrames));
        }

        UINT32 client3Period = 0;
        bool usedClient3 = false;
        if (requestedPeriod > 0) {
            usedClient3 = initializeSharedSnappedPeriod(
                m_audioClient, m_format, sharedEventFlags, requestedPeriod, &client3Period);
            m_eventDriven = usedClient3;
            if (!usedClient3) {
                usedClient3 = initializeSharedSnappedPeriod(
                    m_audioClient, m_format, sharedPollFlags, requestedPeriod, &client3Period);
                m_eventDriven = false;
            }
        }
        if (usedClient3) {
            m_periodFrameCount = client3Period > 0 ? client3Period : requestedPeriod;
            return true;
        }

        HRESULT sharedHr = m_audioClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            sharedEventFlags,
            bufferDuration,
            0,
            m_format,
            nullptr);
        m_eventDriven = SUCCEEDED(sharedHr);
        if (FAILED(sharedHr)) {
            sharedHr = m_audioClient->Initialize(
                AUDCLNT_SHAREMODE_SHARED,
                sharedPollFlags,
                bufferDuration,
                0,
                m_format,
                nullptr);
            m_eventDriven = false;
        }
        return SUCCEEDED(sharedHr);
    };

    bool openedExclusive = false;
    bool usedClient3 = false;
    SharedModeEnginePeriod engine{};
    UINT32 client3Period = 0;

    bool opened = false;
    if (settings.preferExclusive()) {
        opened = tryExclusive();
        openedExclusive = opened;
        if (!opened) {
            AudioLog::warn(tag, QStringLiteral("Exclusive mode failed; falling back to shared"));
            hr = activateClient();
            if (FAILED(hr)) {
                const QString message = QStringLiteral("[WasapiRenderer] Activate(IAudioClient) failed: %1")
                                            .arg(AudioLog::hresultToString(hr));
                AudioLog::error(tag, message);
                if (errorMessage) {
                    *errorMessage = message;
                }
                CoTaskMemFree(mixFormat);
                device->Release();
                close();
                return false;
            }
        }
    }

    if (!opened) {
        engine = querySharedModeEnginePeriod(m_audioClient, m_format);
        UINT32 requestedPeriod = 0;
        if (useDevicePeriod) {
            requestedPeriod = engine.ok ? engine.defaultPeriod : 0;
        } else {
            requestedPeriod = static_cast<UINT32>(AppConstants::requestedEnginePeriodFrames(snappedBufferFrames));
        }
        if (requestedPeriod > 0) {
            usedClient3 = initializeSharedSnappedPeriod(
                m_audioClient, m_format, sharedEventFlags, requestedPeriod, &client3Period);
            m_eventDriven = usedClient3;
            if (!usedClient3) {
                usedClient3 = initializeSharedSnappedPeriod(
                    m_audioClient, m_format, sharedPollFlags, requestedPeriod, &client3Period);
                m_eventDriven = false;
            }
        }
        if (usedClient3) {
            opened = true;
        } else {
            opened = tryShared();
        }
    }

    if (!opened && settings.outputFormat != OutputFormat::Auto) {
        AudioLog::warn(tag, QStringLiteral("Requested output format failed; using device mix format"));
        hr = activateClient();
        if (SUCCEEDED(hr) && rebuildFormat(true)) {
            engine = querySharedModeEnginePeriod(m_audioClient, m_format);
            usedClient3 = false;
            client3Period = 0;
            opened = tryShared();
        }
    }

    if (!opened) {
        const QString message = QStringLiteral("[WasapiRenderer] IAudioClient::Initialize failed");
        AudioLog::error(tag, message);
        CoTaskMemFree(mixFormat);
        device->Release();
        close();
        if (usedCustomIo
            && (requestSampleRate || !useDevicePeriod || settings.outputFormat != OutputFormat::Auto
                || settings.preferExclusive())) {
            AudioLog::warn(tag, QStringLiteral("Requested sample rate/buffer failed; using device defaults"));
            EngineIoSettings fallback;
            fallback.sampleRate = 0;
            fallback.bufferFrames = 0;
            fallback.outputFormat = OutputFormat::Auto;
            fallback.shareMode = ShareMode::PreferShared;
            return open(deviceId, fallback, errorMessage);
        }
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    }

    CoTaskMemFree(mixFormat);
    device->Release();

    if (openedExclusive) {
        usedClient3 = false;
        engine.ok = false;
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

    m_sampleRate = static_cast<float>(m_format->nSamplesPerSec);
    if (usedClient3 && client3Period > 0) {
        m_periodFrameCount = client3Period;
    } else {
        REFERENCE_TIME defaultPeriod = 0;
        REFERENCE_TIME minimumPeriod = 0;
        hr = m_audioClient->GetDevicePeriod(&defaultPeriod, &minimumPeriod);
        if (SUCCEEDED(hr) && defaultPeriod > 0 && m_sampleRate > 0.f) {
            const double periodFrames =
                (static_cast<double>(defaultPeriod) * static_cast<double>(m_sampleRate)) / 10000000.0;
            m_periodFrameCount = static_cast<UINT32>(std::max(1.0, std::round(periodFrames)));
        } else {
            m_periodFrameCount = std::max<UINT32>(1, static_cast<UINT32>(m_sampleRate / 100.f));
        }
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

    hr = m_audioClient->GetService(__uuidof(IAudioClock), reinterpret_cast<void **>(&m_audioClock));
    if (FAILED(hr)) {
        AudioLog::warn(tag, QStringLiteral("GetService(IAudioClock) failed: %1")
                                .arg(AudioLog::hresultToString(hr)));
        m_audioClock = nullptr;
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

    AudioLog::info(tag, QStringLiteral("Render opened: %1 Hz, %2 channels, period=%3 frames, buffer=%4 frames, preroll=%5, event=%6, float=%7")
                             .arg(m_sampleRate)
                             .arg(m_channelCount)
                             .arg(m_periodFrameCount)
                             .arg(m_bufferFrameCount)
                             .arg(m_prerollFrameCount)
                             .arg(m_eventDriven)
                             .arg(m_formatIsFloat));
    const double periodMs = m_sampleRate > 0.f
                                ? (static_cast<double>(m_periodFrameCount) * 1000.0) / static_cast<double>(m_sampleRate)
                                : 0.0;
    AudioLog::info(tag,
                   QStringLiteral("I/O diag: period=%1 frames (%2 ms), clientBuffer=%3, preroll=%4, requestedBuffer=%5, 2xPeriod=%6")
                       .arg(m_periodFrameCount)
                       .arg(periodMs, 0, 'f', 1)
                       .arg(m_bufferFrameCount)
                       .arg(m_prerollFrameCount)
                       .arg(snappedBufferFrames)
                       .arg(m_periodFrameCount * 2));
    if (usedClient3) {
        AudioLog::info(tag,
                       QStringLiteral("I/O diag: IAudioClient3 init period=%1 default=%2 min=%3 fund=%4 max=%5")
                           .arg(m_periodFrameCount)
                           .arg(engine.defaultPeriod)
                           .arg(engine.minPeriod)
                           .arg(engine.fundamentalPeriod)
                           .arg(engine.maxPeriod));
    } else if (engine.ok) {
        AudioLog::info(tag,
                       QStringLiteral("I/O diag: IAudioClient3 default=%1 min=%2 fund=%3 max=%4 (fallback Initialize)")
                           .arg(engine.defaultPeriod)
                           .arg(engine.minPeriod)
                           .arg(engine.fundamentalPeriod)
                           .arg(engine.maxPeriod));
    } else {
        AudioLog::warn(tag, QStringLiteral("I/O diag: IAudioClient3 GetSharedModeEnginePeriod unavailable"));
    }
    if (m_bufferFrameCount < m_periodFrameCount * 2) {
        AudioLog::warn(tag,
                       QStringLiteral("I/O diag: clientBuffer %1 < 2x period %2 — padding can hit zero")
                           .arg(m_bufferFrameCount)
                           .arg(m_periodFrameCount * 2));
    }
    m_deviceLost = false;
    m_generation.fetch_add(1, std::memory_order_acq_rel);
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
    if (m_audioClock) {
        m_audioClock->Release();
        m_audioClock = nullptr;
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
    m_prerollFrameCount = 0;
    m_formatIsFloat = true;
    m_eventDriven = false;
    m_lastPaddingFrames = 0;
    m_lastWaitTimedOut = false;
    m_deviceLost = false;
    m_generation.fetch_add(1, std::memory_order_acq_rel);
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
        m_lastPaddingFrames = 0;
        return 0;
    }

    UINT32 padding = 0;
    const HRESULT hr = m_audioClient->GetCurrentPadding(&padding);
    if (FAILED(hr)) {
        m_lastPaddingFrames = 0;
        m_deviceLost = true;
        return 0;
    }
    m_lastPaddingFrames = padding;
    if (padding >= m_bufferFrameCount) {
        return 0;
    }
    return m_bufferFrameCount - padding;
}

bool WasapiRenderer::readClock(uint64_t *frames, uint64_t *qpc) const
{
    if (!frames || !qpc || !m_audioClock || m_sampleRate <= 0.f) {
        return false;
    }

    UINT64 position = 0;
    UINT64 qpcPosition = 0;
    if (FAILED(m_audioClock->GetPosition(&position, &qpcPosition))) {
        return false;
    }
    UINT64 frequency = 0;
    if (FAILED(m_audioClock->GetFrequency(&frequency)) || frequency == 0) {
        return false;
    }
    *frames = static_cast<uint64_t>(
        (static_cast<double>(position) / static_cast<double>(frequency)) * static_cast<double>(m_sampleRate));
    if (qpcPosition == 0) {
        LARGE_INTEGER now = {};
        QueryPerformanceCounter(&now);
        qpcPosition = static_cast<UINT64>(now.QuadPart);
    }
    *qpc = qpcPosition;
    return true;
}

bool WasapiRenderer::waitForNextPeriod(DWORD timeoutMs)
{
    m_lastWaitTimedOut = false;
    if (m_eventDriven && m_bufferEvent) {
        const DWORD result = WaitForSingleObject(m_bufferEvent, timeoutMs);
        m_lastWaitTimedOut = (result == WAIT_TIMEOUT);
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
        m_prerollFrameCount = 0;
        return true;
    }

    const UINT32 twoPeriods = m_periodFrameCount * 2;
    UINT32 prerollFrames = twoPeriods > 0 ? twoPeriods : m_periodFrameCount;
    if (prerollFrames < m_periodFrameCount) {
        prerollFrames = m_periodFrameCount;
    }
    if (prerollFrames > m_bufferFrameCount) {
        prerollFrames = m_bufferFrameCount;
    }
    m_prerollFrameCount = prerollFrames;

    BYTE *data = nullptr;
    HRESULT hr = m_renderClient->GetBuffer(prerollFrames, &data);
    if (FAILED(hr)) {
        const QString message = QStringLiteral("[WasapiRenderer] Preroll GetBuffer failed: %1")
                                    .arg(AudioLog::hresultToString(hr));
        AudioLog::error(QStringLiteral("WasapiRenderer"), message);
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    }

    hr = m_renderClient->ReleaseBuffer(prerollFrames, AUDCLNT_BUFFERFLAGS_SILENT);
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
        if (wasapiDeviceLost(hr)) {
            m_deviceLost = true;
        }
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
        if (wasapiDeviceLost(hr)) {
            m_deviceLost = true;
        }
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
        return;
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

    const int framesToWrite = static_cast<int>(std::min(availableFrames, static_cast<UINT32>(frameCount)));
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

bool WasapiRenderer::writePrepared(const float *interleavedBuffer, int frameCount, int inputChannelCount)
{
    return write(interleavedBuffer, frameCount, inputChannelCount, nullptr);
}
