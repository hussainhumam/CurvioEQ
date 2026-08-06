#include "processloopbackcapture.h"

#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclientactivationparams.h>

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <vector>

namespace {

constexpr WORD kProcessLoopbackChannels = 2;
constexpr WORD kProcessLoopbackBitsPerSample = 16;

WAVEFORMATEX *createProcessLoopbackFormat(DWORD sampleRate)
{
    auto *format = static_cast<WAVEFORMATEX *>(CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
    if (!format) {
        return nullptr;
    }

    std::memset(format, 0, sizeof(WAVEFORMATEX));
    format->wFormatTag = WAVE_FORMAT_PCM;
    format->nChannels = kProcessLoopbackChannels;
    format->nSamplesPerSec = sampleRate;
    format->wBitsPerSample = kProcessLoopbackBitsPerSample;
    format->nBlockAlign = static_cast<WORD>(format->nChannels * format->wBitsPerSample / 8);
    format->nAvgBytesPerSec = format->nSamplesPerSec * format->nBlockAlign;
    format->cbSize = 0;
    return format;
}

bool tryInitializeCapture(IAudioClient *audioClient, WAVEFORMATEX *format, HRESULT *hrOut)
{
    const HRESULT hr = audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
        0,
        0,
        format,
        nullptr);
    if (hrOut) {
        *hrOut = hr;
    }
    return SUCCEEDED(hr);
}

bool isProcessRunning(unsigned long processId)
{
    HANDLE processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!processHandle) {
        return false;
    }
    DWORD exitCode = 0;
    const BOOL ok = GetExitCodeProcess(processHandle, &exitCode);
    CloseHandle(processHandle);
    return ok && exitCode == STILL_ACTIVE;
}

class AudioInterfaceCompletionHandler : public IActivateAudioInterfaceCompletionHandler
{
public:
    AudioInterfaceCompletionHandler()
        : m_refCount(1)
    {
        CoCreateFreeThreadedMarshaler(
            static_cast<IActivateAudioInterfaceCompletionHandler *>(this),
            &m_freeThreadedMarshaler);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override
    {
        if (!ppvObject) {
            return E_POINTER;
        }
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IActivateAudioInterfaceCompletionHandler)) {
            *ppvObject = static_cast<IActivateAudioInterfaceCompletionHandler *>(this);
            AddRef();
            return S_OK;
        }
        if (m_freeThreadedMarshaler) {
            return m_freeThreadedMarshaler->QueryInterface(riid, ppvObject);
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ++m_refCount;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG count = --m_refCount;
        if (count == 0) {
            if (m_freeThreadedMarshaler) {
                m_freeThreadedMarshaler->Release();
                m_freeThreadedMarshaler = nullptr;
            }
            delete this;
        }
        return count;
    }

    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation *operation) override
    {
        HRESULT hrActivate = S_OK;
        IUnknown *audioInterface = nullptr;
        const HRESULT hr = operation->GetActivateResult(&hrActivate, &audioInterface);
        m_hrGetActivateResult = hr;
        if (SUCCEEDED(hr) && SUCCEEDED(hrActivate) && audioInterface) {
            m_audioClient = reinterpret_cast<IAudioClient *>(audioInterface);
        } else {
            m_hrActivate = FAILED(hr) ? hr : hrActivate;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_completed = true;
        }
        m_cv.notify_one();
        return S_OK;
    }

    IAudioClient *waitForClient(HRESULT *hrOut, HRESULT *hrGetResultOut)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return m_completed; });
        if (hrOut) {
            *hrOut = m_hrActivate;
        }
        if (hrGetResultOut) {
            *hrGetResultOut = m_hrGetActivateResult;
        }
        return m_audioClient;
    }

private:
    std::atomic<ULONG> m_refCount;
    IUnknown *m_freeThreadedMarshaler = nullptr;
    IAudioClient *m_audioClient = nullptr;
    HRESULT m_hrActivate = S_OK;
    HRESULT m_hrGetActivateResult = S_OK;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_completed = false;
};

bool waveFormatToFloatInfo(const WAVEFORMATEX *format, float *sampleRate, int *channelCount, int *bytesPerFrame)
{
    if (!format || !sampleRate || !channelCount || !bytesPerFrame) {
        return false;
    }

    *channelCount = format->nChannels;
    *bytesPerFrame = format->nBlockAlign;
    *sampleRate = static_cast<float>(format->nSamplesPerSec);

    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        return true;
    }

    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto *waveFormatExtensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(format);
        if (IsEqualGUID(waveFormatExtensible->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
            return true;
        }
    }

    return false;
}

void convertPcm16ToFloat(const BYTE *source, float *destination, int sampleCount)
{
    const auto *samples = reinterpret_cast<const int16_t *>(source);
    for (int i = 0; i < sampleCount; ++i) {
        destination[i] = static_cast<float>(samples[i]) / 32768.f;
    }
}

void convertPcm32ToFloat(const BYTE *source, float *destination, int sampleCount)
{
    const auto *samples = reinterpret_cast<const int32_t *>(source);
    for (int i = 0; i < sampleCount; ++i) {
        destination[i] = static_cast<float>(samples[i]) / 2147483648.f;
    }
}

} // namespace

ProcessLoopbackCapture::~ProcessLoopbackCapture()
{
    close();
}

bool ProcessLoopbackCapture::open(unsigned long processId, QString *errorMessage)
{
    const QString tag = QStringLiteral("LoopbackCapture");
    close();

    if (!isProcessRunning(processId)) {
        const QString message = QStringLiteral("[LoopbackCapture] Target process is not running (pid=%1)").arg(processId);
        AudioLog::error(tag, message);
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    }

    AudioLog::info(tag, QStringLiteral("Opening process loopback for pid=%1").arg(processId));

    AUDIOCLIENT_ACTIVATION_PARAMS activationParams = {};
    activationParams.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    activationParams.ProcessLoopbackParams.TargetProcessId = processId;
    activationParams.ProcessLoopbackParams.ProcessLoopbackMode =
        PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT activateParams;
    PropVariantInit(&activateParams);
    activateParams.vt = VT_BLOB;
    activateParams.blob.cbSize = sizeof(activationParams);
    activateParams.blob.pBlobData = reinterpret_cast<BYTE *>(&activationParams);

    auto *completionHandler = new AudioInterfaceCompletionHandler();
    IActivateAudioInterfaceAsyncOperation *asyncOperation = nullptr;

    HRESULT hr = ActivateAudioInterfaceAsync(
        VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
        __uuidof(IAudioClient),
        &activateParams,
        completionHandler,
        &asyncOperation);

    if (FAILED(hr)) {
        completionHandler->Release();
        const QString message = QStringLiteral("[LoopbackCapture] ActivateAudioInterfaceAsync failed: %1")
                                    .arg(AudioLog::hresultToString(hr));
        AudioLog::error(tag, message);
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    }

    HRESULT hrActivate = S_OK;
    HRESULT hrGetResult = S_OK;
    m_audioClient = completionHandler->waitForClient(&hrActivate, &hrGetResult);
    completionHandler->Release();
    if (asyncOperation) {
        asyncOperation->Release();
    }

    if (!m_audioClient || FAILED(hrActivate)) {
        const QString message = QStringLiteral("[LoopbackCapture] Process loopback activation failed: %1 (GetActivateResult=%2)")
                                    .arg(AudioLog::hresultToString(hrActivate),
                                         AudioLog::hresultToString(hrGetResult));
        AudioLog::error(tag, message);
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    }

    const DWORD sampleRates[] = {44100, 48000};
    HRESULT hrInit = E_FAIL;
    bool initialized = false;

    for (const DWORD sampleRate : sampleRates) {
        if (m_format) {
            CoTaskMemFree(m_format);
            m_format = nullptr;
        }

        m_format = createProcessLoopbackFormat(sampleRate);
        if (!m_format) {
            const QString message = QStringLiteral("[LoopbackCapture] Failed to allocate process loopback format");
            if (errorMessage) {
                *errorMessage = message;
            }
            close();
            return false;
        }

        if (tryInitializeCapture(m_audioClient, m_format, &hrInit)) {
            initialized = true;
            break;
        }
    }

    if (!initialized) {
        const QString message = QStringLiteral("[LoopbackCapture] IAudioClient::Initialize failed: %1")
                                    .arg(AudioLog::hresultToString(hrInit));
        AudioLog::error(tag, message);
        if (errorMessage) {
            *errorMessage = message;
        }
        close();
        return false;
    }

    m_sampleRate = static_cast<float>(m_format->nSamplesPerSec);
    m_channelCount = m_format->nChannels;
    m_bytesPerFrame = m_format->nBlockAlign;

    hr = m_audioClient->GetService(__uuidof(IAudioCaptureClient),
                                   reinterpret_cast<void **>(&m_captureClient));
    if (FAILED(hr)) {
        const QString message = QStringLiteral("[LoopbackCapture] GetService(IAudioCaptureClient) failed: %1")
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
        const QString message = QStringLiteral("[LoopbackCapture] IAudioClient::Start failed: %1")
                                    .arg(AudioLog::hresultToString(hr));
        AudioLog::error(tag, message);
        if (errorMessage) {
            *errorMessage = message;
        }
        close();
        return false;
    }

    AudioLog::info(tag, QStringLiteral("Capture opened: %1 Hz, %2 channels")
                             .arg(m_sampleRate)
                             .arg(m_channelCount));
    return true;
}

void ProcessLoopbackCapture::close()
{
    if (m_audioClient) {
        m_audioClient->Stop();
    }
    if (m_captureClient) {
        m_captureClient->Release();
        m_captureClient = nullptr;
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
    m_bytesPerFrame = 0;
}

bool ProcessLoopbackCapture::read(float *interleavedBuffer, int frameCount, int *framesRead, QString *errorMessage)
{
    if (!m_captureClient || !interleavedBuffer || frameCount <= 0) {
        if (framesRead) {
            *framesRead = 0;
        }
        return false;
    }

    int totalFramesRead = 0;

    while (totalFramesRead < frameCount) {
        UINT32 packetLength = 0;
        HRESULT hr = m_captureClient->GetNextPacketSize(&packetLength);
        if (FAILED(hr)) {
            const QString message = QStringLiteral("GetNextPacketSize failed: %1")
                                        .arg(AudioLog::hresultToString(hr));
            if (errorMessage) {
                *errorMessage = message;
            }
            return false;
        }

        if (packetLength == 0) {
            break;
        }

        BYTE *data = nullptr;
        UINT32 numFramesAvailable = 0;
        DWORD flags = 0;
        hr = m_captureClient->GetBuffer(&data, &numFramesAvailable, &flags, nullptr, nullptr);
        if (FAILED(hr)) {
            const QString message = QStringLiteral("GetBuffer failed: %1")
                                        .arg(AudioLog::hresultToString(hr));
            if (errorMessage) {
                *errorMessage = message;
            }
            return false;
        }

        const int framesToCopy = static_cast<int>(std::min<UINT32>(numFramesAvailable,
                                                                   static_cast<UINT32>(frameCount - totalFramesRead)));
        const int sampleCount = framesToCopy * m_channelCount;
        float *writePtr = interleavedBuffer + (totalFramesRead * m_channelCount);

        if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
            std::memset(writePtr, 0, static_cast<size_t>(sampleCount) * sizeof(float));
        } else if (m_format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT
                   || (m_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE
                       && m_format->wBitsPerSample == 32)) {
            std::memcpy(writePtr, data, static_cast<size_t>(sampleCount) * sizeof(float));
        } else if (m_format->wBitsPerSample == 16) {
            convertPcm16ToFloat(data, writePtr, sampleCount);
        } else if (m_format->wBitsPerSample == 32) {
            convertPcm32ToFloat(data, writePtr, sampleCount);
        } else {
            m_captureClient->ReleaseBuffer(numFramesAvailable);
            const QString message = QStringLiteral("Unsupported bit depth: %1").arg(m_format->wBitsPerSample);
            if (errorMessage) {
                *errorMessage = message;
            }
            return false;
        }

        hr = m_captureClient->ReleaseBuffer(numFramesAvailable);
        if (FAILED(hr)) {
            const QString message = QStringLiteral("ReleaseBuffer failed: %1")
                                        .arg(AudioLog::hresultToString(hr));
            if (errorMessage) {
                *errorMessage = message;
            }
            return false;
        }

        totalFramesRead += framesToCopy;
    }

    if (framesRead) {
        *framesRead = totalFramesRead;
    }
    return true;
}
