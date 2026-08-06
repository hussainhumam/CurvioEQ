#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <fstream>
#include <mutex>

static void agentLog(const char *message, DWORD pid, HRESULT hrSync, HRESULT hrGet, HRESULT hrActivate, HRESULT hrInit)
{
    std::ofstream out("c:/projects/debug-0ca580.log", std::ios::app);
    out << "{\"sessionId\":\"0ca580\",\"location\":\"test_loopback.cpp\",\"message\":\"" << message
        << "\",\"hypothesisId\":\"L1\",\"runId\":\"harness\",\"data\":{"
        << "\"processId\":" << pid
        << ",\"hrSync\":" << static_cast<long long>(hrSync)
        << ",\"hrGet\":" << static_cast<long long>(hrGet)
        << ",\"hrActivate\":" << static_cast<long long>(hrActivate)
        << ",\"hrInit\":" << static_cast<long long>(hrInit)
        << "},\"timestamp\":" << static_cast<long long>(GetTickCount64()) << "}\n";
}

class Handler : public IActivateAudioInterfaceCompletionHandler
{
public:
    Handler() : m_ref(1)
    {
        CoCreateFreeThreadedMarshaler(static_cast<IActivateAudioInterfaceCompletionHandler *>(this),
                                      &m_freeThreadedMarshaler);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override
    {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IActivateAudioInterfaceCompletionHandler)) {
            *ppv = static_cast<IActivateAudioInterfaceCompletionHandler *>(this);
            AddRef();
            return S_OK;
        }
        if (m_freeThreadedMarshaler) {
            return m_freeThreadedMarshaler->QueryInterface(riid, ppv);
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG c = --m_ref;
        if (c == 0) {
            if (m_freeThreadedMarshaler) {
                m_freeThreadedMarshaler->Release();
                m_freeThreadedMarshaler = nullptr;
            }
            delete this;
        }
        return c;
    }

    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation *op) override
    {
        HRESULT hrActivate = S_OK;
        IUnknown *iface = nullptr;
        const HRESULT hr = op->GetActivateResult(&hrActivate, &iface);
        m_hrGet = hr;
        m_hrActivate = FAILED(hr) ? hr : hrActivate;
        if (SUCCEEDED(hr) && SUCCEEDED(hrActivate) && iface) {
            m_client = reinterpret_cast<IAudioClient *>(iface);
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_done = true;
        }
        m_cv.notify_one();
        return S_OK;
    }

    IAudioClient *wait(HRESULT *hrActivate, HRESULT *hrGet, DWORD timeoutMs)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (!m_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return m_done; })) {
            if (hrActivate) *hrActivate = HRESULT_FROM_WIN32(WAIT_TIMEOUT);
            if (hrGet) *hrGet = S_OK;
            return nullptr;
        }
        if (hrActivate) *hrActivate = m_hrActivate;
        if (hrGet) *hrGet = m_hrGet;
        return m_client;
    }

private:
    std::atomic<ULONG> m_ref;
    IUnknown *m_freeThreadedMarshaler = nullptr;
    IAudioClient *m_client = nullptr;
    HRESULT m_hrActivate = S_OK;
    HRESULT m_hrGet = S_OK;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_done = false;
};

static void printHr(const char *label, HRESULT hr)
{
    std::printf("%s HRESULT=0x%08lX\n", label, static_cast<unsigned long>(hr));
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::printf("Usage: test_loopback.exe <pid>\n");
        return 1;
    }

    const DWORD pid = static_cast<DWORD>(std::strtoul(argv[1], nullptr, 10));
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    AUDIOCLIENT_ACTIVATION_PARAMS activationParams = {};
    activationParams.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    activationParams.ProcessLoopbackParams.TargetProcessId = pid;
    activationParams.ProcessLoopbackParams.ProcessLoopbackMode =
        PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT activateParams;
    PropVariantInit(&activateParams);
    activateParams.vt = VT_BLOB;
    activateParams.blob.cbSize = sizeof(activationParams);
    activateParams.blob.pBlobData = reinterpret_cast<BYTE *>(&activationParams);

    auto *handler = new Handler();
    IActivateAudioInterfaceAsyncOperation *asyncOp = nullptr;
    const HRESULT hrSync = ActivateAudioInterfaceAsync(
        VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
        __uuidof(IAudioClient),
        &activateParams,
        handler,
        &asyncOp);
    printHr("ActivateAudioInterfaceAsync sync", hrSync);

    HRESULT hrActivate = S_OK;
    HRESULT hrGet = S_OK;
    IAudioClient *client = handler->wait(&hrActivate, &hrGet, 15000);
    printHr("GetActivateResult", hrGet);
    printHr("activation hrActivate", hrActivate);

    HRESULT hrInit = E_FAIL;
    if (client) {
        WAVEFORMATEX wfx = {};
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = 2;
        wfx.nSamplesPerSec = 44100;
        wfx.wBitsPerSample = 16;
        wfx.nBlockAlign = 4;
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
        const HRESULT hrInitCall = client->Initialize(
            AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 10000000, 0, &wfx, nullptr);
        hrInit = hrInitCall;
        printHr("Initialize", hrInitCall);
        client->Release();
    }

    agentLog("harness result", pid, hrSync, hrGet, hrActivate, hrInit);

    if (asyncOp) asyncOp->Release();
    handler->Release();
    CoUninitialize();
    return FAILED(hrSync) || !client ? 1 : 0;
}
