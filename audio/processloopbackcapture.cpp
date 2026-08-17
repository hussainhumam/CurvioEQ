#include "processloopbackcapture.h"

#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclientactivationparams.h>
#include <ksmedia.h>

#include <algorithm>
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

WAVEFORMATEX *createProcessLoopbackFloatFormat(DWORD sampleRate)
{
    auto *format = static_cast<WAVEFORMATEXTENSIBLE *>(CoTaskMemAlloc(sizeof(WAVEFORMATEXTENSIBLE)));
    if (!format) {
        return nullptr;
    }

    std::memset(format, 0, sizeof(WAVEFORMATEXTENSIBLE));
    format->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format->Format.nChannels = kProcessLoopbackChannels;
    format->Format.nSamplesPerSec = sampleRate;
    format->Format.wBitsPerSample = 32;
    format->Format.nBlockAlign = static_cast<WORD>(format->Format.nChannels * format->Format.wBitsPerSample / 8);
    format->Format.nAvgBytesPerSec = format->Format.nSamplesPerSec * format->Format.nBlockAlign;
    format->Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    format->Samples.wValidBitsPerSample = 32;
    format->SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    return reinterpret_cast<WAVEFORMATEX *>(format);
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

} // namespace

bool ProcessLoopbackCapture::isProcessRunning(unsigned long processId)
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

namespace {

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

bool ProcessLoopbackCapture::open(unsigned long processId, float preferredSampleRate, QString *errorMessage)
{
    const QString tag = QStringLiteral("LoopbackCapture");
    close();

    if (!ProcessLoopbackCapture::isProcessRunning(processId)) {
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

    std::vector<DWORD> sampleRates;
    const DWORD preferredRate = preferredSampleRate >= 44100.f ? static_cast<DWORD>(preferredSampleRate + 0.5f)
                                                               : 0;
    if (preferredRate == 48000 || preferredRate == 44100) {
        sampleRates.push_back(preferredRate);
    }
    for (const DWORD fallbackRate : {48000U, 44100U}) {
        if (std::find(sampleRates.begin(), sampleRates.end(), fallbackRate) == sampleRates.end()) {
            sampleRates.push_back(fallbackRate);
        }
    }

    HRESULT hrInit = E_FAIL;
    bool initialized = false;

    using FormatFactory = WAVEFORMATEX *(*)(DWORD);
    const FormatFactory formatFactories[] = {createProcessLoopbackFloatFormat, createProcessLoopbackFormat};

    for (const DWORD sampleRate : sampleRates) {
        for (const FormatFactory createFormat : formatFactories) {
            if (m_format) {
                CoTaskMemFree(m_format);
                m_format = nullptr;
            }

            m_format = createFormat(sampleRate);
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

        if (initialized) {
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
    m_pendingFrames.clear();
}

bool ProcessLoopbackCapture::copyPendingFrames(float *interleavedBuffer, int frameCount, int *totalFramesRead)
{
    if (!totalFramesRead || m_pendingFrames.empty() || m_channelCount <= 0) {
        return true;
    }

    const int pendingFrameCount =
        static_cast<int>(m_pendingFrames.size() / static_cast<size_t>(m_channelCount));
    const int framesToCopy = std::min(frameCount - *totalFramesRead, pendingFrameCount);
    if (framesToCopy <= 0) {
        return true;
    }

    const size_t sampleCount = static_cast<size_t>(framesToCopy * m_channelCount);
    std::memcpy(interleavedBuffer + static_cast<size_t>(*totalFramesRead * m_channelCount),
                m_pendingFrames.data(),
                sampleCount * sizeof(float));

    m_pendingFrames.erase(m_pendingFrames.begin(),
                          m_pendingFrames.begin() + static_cast<ptrdiff_t>(sampleCount));
    *totalFramesRead += framesToCopy;
    return true;
}

bool ProcessLoopbackCapture::appendPacketFrames(const BYTE *data,
                                                UINT32 numFramesAvailable,
                                                DWORD flags,
                                                QString *errorMessage)
{
    if (numFramesAvailable == 0 || m_channelCount <= 0) {
        return true;
    }

    const size_t sampleCount = static_cast<size_t>(numFramesAvailable * m_channelCount);
    const size_t previousSize = m_pendingFrames.size();
    m_pendingFrames.resize(previousSize + sampleCount);

    float *writePtr = m_pendingFrames.data() + previousSize;
    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
        std::memset(writePtr, 0, sampleCount * sizeof(float));
        return true;
    }

    if (m_format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT
        || (m_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && m_format->wBitsPerSample == 32)) {
        std::memcpy(writePtr, data, sampleCount * sizeof(float));
        return true;
    }

    if (m_format->wBitsPerSample == 16) {
        convertPcm16ToFloat(data, writePtr, static_cast<int>(sampleCount));
        return true;
    }

    if (m_format->wBitsPerSample == 32) {
        convertPcm32ToFloat(data, writePtr, static_cast<int>(sampleCount));
        return true;
    }

    const QString message = QStringLiteral("Unsupported bit depth: %1").arg(m_format->wBitsPerSample);
    if (errorMessage) {
        *errorMessage = message;
    }
    return false;
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
    if (!copyPendingFrames(interleavedBuffer, frameCount, &totalFramesRead)) {
        return false;
    }

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

        if (framesToCopy < static_cast<int>(numFramesAvailable)) {
            const BYTE *remainingData = data + static_cast<size_t>(framesToCopy * m_bytesPerFrame);
            const UINT32 remainingFrames = numFramesAvailable - static_cast<UINT32>(framesToCopy);
            if (!appendPacketFrames(remainingData, remainingFrames, flags, errorMessage)) {
                m_captureClient->ReleaseBuffer(numFramesAvailable);
                return false;
            }
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

        if (!copyPendingFrames(interleavedBuffer, frameCount, &totalFramesRead)) {
            return false;
        }
    }

    if (framesRead) {
        *framesRead = totalFramesRead;
    }
    return true;
}
