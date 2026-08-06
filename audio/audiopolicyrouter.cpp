#include "audiopolicyrouter.h"

#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <roapi.h>
#include <winstring.h>
#include <inspectable.h>

#include <Propidl.h>

namespace {

constexpr wchar_t kAudioPolicyConfigClass[] = L"Windows.Media.Internal.AudioPolicyConfig";
constexpr wchar_t kRenderInterfaceToken[] =
    L"#{e6327cad-dcec-4949-ae8a-991e976a79d2}";

constexpr int kVtableSetPersisted = 25;
constexpr int kVtableGetPersisted = 26;
constexpr int kVtableClearPersisted = 27;

using SetPersistedFn = HRESULT(STDMETHODCALLTYPE *)(void *, UINT32, EDataFlow, ERole, HSTRING);
using GetPersistedFn = HRESULT(STDMETHODCALLTYPE *)(void *, UINT32, EDataFlow, ERole, HSTRING *);
using ClearPersistedFn = HRESULT(STDMETHODCALLTYPE *)(void *);

struct PolicyFactory {
    void *instance = nullptr;
    SetPersistedFn setPersisted = nullptr;
    GetPersistedFn getPersisted = nullptr;
    ClearPersistedFn clearPersisted = nullptr;
    QString selectedIid;
};

PolicyFactory g_factory;
bool g_roInitialized = false;

QString iidToString(const IID &iid)
{
    return QStringLiteral("{%1-%2-%3-%4%5-%6%7%8%9%10%11}")
        .arg(iid.Data1, 8, 16, QChar('0'))
        .arg(iid.Data2, 4, 16, QChar('0'))
        .arg(iid.Data3, 4, 16, QChar('0'))
        .arg(iid.Data4[0], 2, 16, QChar('0'))
        .arg(iid.Data4[1], 2, 16, QChar('0'))
        .arg(iid.Data4[2], 2, 16, QChar('0'))
        .arg(iid.Data4[3], 2, 16, QChar('0'))
        .arg(iid.Data4[4], 2, 16, QChar('0'))
        .arg(iid.Data4[5], 2, 16, QChar('0'))
        .arg(iid.Data4[6], 2, 16, QChar('0'))
        .arg(iid.Data4[7], 2, 16, QChar('0'));
}

bool ensureRoInitialized()
{
    if (g_roInitialized) {
        return true;
    }

    const HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) {
        g_roInitialized = true;
        return true;
    }
    return false;
}

bool ensurePolicyFactory()
{
    if (g_factory.instance) {
        return true;
    }

    if (!ensureRoInitialized()) {
        return false;
    }

    HSTRING classId = nullptr;
    if (FAILED(WindowsCreateString(kAudioPolicyConfigClass,
                                   static_cast<UINT32>(wcslen(kAudioPolicyConfigClass)),
                                   &classId))) {
        return false;
    }

    IInspectable *inspectable = nullptr;
    HRESULT hr = RoGetActivationFactory(classId, IID_PPV_ARGS(&inspectable));
    WindowsDeleteString(classId);
    if (FAILED(hr) || !inspectable) {
        return false;
    }

    ULONG iidCount = 0;
    IID *iids = nullptr;
    hr = inspectable->GetIids(&iidCount, &iids);
    if (FAILED(hr) || !iids || iidCount == 0) {
        inspectable->Release();
        return false;
    }

    const IID &targetIid = iids[iidCount - 1];
    g_factory.selectedIid = iidToString(targetIid);

    void *factoryInstance = nullptr;
    hr = inspectable->QueryInterface(targetIid, &factoryInstance);
    CoTaskMemFree(iids);
    inspectable->Release();

    if (FAILED(hr) || !factoryInstance) {
        return false;
    }

    auto **vtable = *reinterpret_cast<void ***>(factoryInstance);
    g_factory.instance = factoryInstance;
    g_factory.setPersisted = reinterpret_cast<SetPersistedFn>(vtable[kVtableSetPersisted]);
    g_factory.getPersisted = reinterpret_cast<GetPersistedFn>(vtable[kVtableGetPersisted]);
    g_factory.clearPersisted = reinterpret_cast<ClearPersistedFn>(vtable[kVtableClearPersisted]);
    return g_factory.setPersisted && g_factory.getPersisted && g_factory.clearPersisted;
}

QString wrapDeviceId(const QString &deviceId)
{
    if (deviceId.isEmpty()) {
        return {};
    }
    if (deviceId.startsWith(QStringLiteral("\\\\?\\SWD#"))) {
        return deviceId;
    }
    return QStringLiteral("\\\\?\\SWD#MMDEVAPI#") + deviceId + kRenderInterfaceToken;
}

HRESULT setPersistedEndpoint(unsigned long processId, HSTRING deviceId)
{
    if (!ensurePolicyFactory()) {
        return E_FAIL;
    }

    const UINT32 pid = static_cast<UINT32>(processId);
    const HRESULT hrMultimedia =
        g_factory.setPersisted(g_factory.instance, pid, eRender, eMultimedia, deviceId);
    const HRESULT hrConsole =
        g_factory.setPersisted(g_factory.instance, pid, eRender, eConsole, deviceId);

    if (FAILED(hrMultimedia)) {
        return hrMultimedia;
    }
    if (FAILED(hrConsole)) {
        return hrConsole;
    }
    return S_OK;
}

QString queryPersistedEndpoint(unsigned long processId, ERole role)
{
    if (!ensurePolicyFactory() || !g_factory.getPersisted) {
        return {};
    }

    HSTRING hDeviceId = nullptr;
    const HRESULT hr = g_factory.getPersisted(g_factory.instance,
                                            static_cast<UINT32>(processId),
                                            eRender,
                                            role,
                                            &hDeviceId);

    if (FAILED(hr) || !hDeviceId) {
        return {};
    }

    UINT32 length = 0;
    const wchar_t *raw = WindowsGetStringRawBuffer(hDeviceId, &length);
    const QString result = raw ? QString::fromWCharArray(raw, static_cast<int>(length)) : QString();
    WindowsDeleteString(hDeviceId);
    return result;
}

QString deviceFriendlyName(IMMDevice *device)
{
    IPropertyStore *props = nullptr;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &props)) || !props) {
        return {};
    }

    PROPVARIANT value;
    PropVariantInit(&value);
    QString name;
    if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &value))) {
        if (value.vt == VT_LPWSTR && value.pwszVal) {
            name = QString::fromWCharArray(value.pwszVal);
        }
    }
    PropVariantClear(&value);
    props->Release();
    return name;
}

QString unwrapPersistedDeviceId(const QString &wrapped)
{
    const QString prefix = QStringLiteral("\\\\?\\SWD#MMDEVAPI#");
    if (!wrapped.startsWith(prefix)) {
        return wrapped;
    }

    const QString remainder = wrapped.mid(prefix.size());
    const int tokenIndex = remainder.indexOf(kRenderInterfaceToken);
    if (tokenIndex < 0) {
        return remainder;
    }
    return remainder.left(tokenIndex);
}

} // namespace

bool AudioPolicyRouter::isRoutingSupported()
{
    return ensurePolicyFactory();
}

QVector<AudioRenderDeviceInfo> AudioPolicyRouter::listRenderDevices()
{
    QVector<AudioRenderDeviceInfo> devices;

    IMMDeviceEnumerator *enumerator = nullptr;
    IMMDevice *defaultDevice = nullptr;
    IMMDeviceCollection *collection = nullptr;

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                  nullptr,
                                  CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void **>(&enumerator));
    if (FAILED(hr)) {
        return devices;
    }

    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice);
    if (FAILED(hr)) {
        enumerator->Release();
        return devices;
    }

    LPWSTR defaultId = nullptr;
    defaultDevice->GetId(&defaultId);
    const QString defaultDeviceId = defaultId ? QString::fromWCharArray(defaultId) : QString();
    CoTaskMemFree(defaultId);
    defaultDevice->Release();

    hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
    enumerator->Release();
    if (FAILED(hr) || !collection) {
        return devices;
    }

    UINT count = 0;
    collection->GetCount(&count);

    for (UINT i = 0; i < count; ++i) {
        IMMDevice *device = nullptr;
        if (FAILED(collection->Item(i, &device)) || !device) {
            continue;
        }

        LPWSTR id = nullptr;
        device->GetId(&id);
        const QString deviceId = id ? QString::fromWCharArray(id) : QString();
        CoTaskMemFree(id);

        if (deviceId.isEmpty()) {
            device->Release();
            continue;
        }

        AudioRenderDeviceInfo info;
        info.id = deviceId;
        info.friendlyName = deviceFriendlyName(device);
        if (info.friendlyName.isEmpty()) {
            info.friendlyName = deviceId;
        }
        info.isDefault = (deviceId == defaultDeviceId);
        devices.push_back(info);
        device->Release();
    }

    collection->Release();

    return devices;
}

QVector<AudioRenderDeviceInfo> AudioPolicyRouter::listRenderDevicesExcluding(const QString &excludeDeviceId)
{
    const QVector<AudioRenderDeviceInfo> devices = listRenderDevices();
    if (excludeDeviceId.isEmpty()) {
        return devices;
    }

    QVector<AudioRenderDeviceInfo> filtered;
    filtered.reserve(devices.size());
    for (const AudioRenderDeviceInfo &device : devices) {
        if (device.id != excludeDeviceId) {
            filtered.push_back(device);
        }
    }
    return filtered;
}

bool AudioPolicyRouter::preferredRoutingSinkDevice(QString *deviceId, QString *friendlyName)
{
    const QVector<AudioRenderDeviceInfo> devices = listRenderDevices();
    if (devices.isEmpty()) {
        return false;
    }

    auto sinkScore = [](const AudioRenderDeviceInfo &device) {
        int score = 0;
        const QString &name = device.friendlyName;
        if (name.contains(QStringLiteral("VB-Audio"), Qt::CaseInsensitive)
            || name.contains(QStringLiteral("VB-Cable"), Qt::CaseInsensitive)
            || name.contains(QStringLiteral("CABLE"), Qt::CaseInsensitive)) {
            score += 100;
        }
        if (name.contains(QStringLiteral("Voicemeeter"), Qt::CaseInsensitive)) {
            score += 90;
        }
        if (name.contains(QStringLiteral("Virtual"), Qt::CaseInsensitive)) {
            score += 50;
        }
        return score;
    };

    const AudioRenderDeviceInfo *best = nullptr;
    int bestScore = -1;
    for (const AudioRenderDeviceInfo &device : devices) {
        const int score = sinkScore(device);
        if (score > bestScore) {
            bestScore = score;
            best = &device;
        }
    }

    if (!best || bestScore <= 0) {
        return false;
    }

    if (deviceId) {
        *deviceId = best->id;
    }
    if (friendlyName) {
        *friendlyName = best->friendlyName;
    }
    return true;
}

bool AudioPolicyRouter::defaultRenderDevice(QString *deviceId, QString *friendlyName)
{
    const QVector<AudioRenderDeviceInfo> devices = listRenderDevices();
    for (const AudioRenderDeviceInfo &device : devices) {
        if (!device.isDefault) {
            continue;
        }
        if (deviceId) {
            *deviceId = device.id;
        }
        if (friendlyName) {
            *friendlyName = device.friendlyName;
        }
        return true;
    }
    return false;
}

bool AudioPolicyRouter::preferredRenderDevice(QString *deviceId, QString *friendlyName)
{
    const QVector<AudioRenderDeviceInfo> devices = listRenderDevices();
    if (devices.isEmpty()) {
        return defaultRenderDevice(deviceId, friendlyName);
    }

    auto deviceScore = [](const AudioRenderDeviceInfo &device) {
        int score = 0;
        if (device.isDefault) {
            score += 10;
        }
        if (device.friendlyName.contains(QStringLiteral("HyperX"), Qt::CaseInsensitive)) {
            score += 100;
        }
        if (device.friendlyName.contains(QStringLiteral("Headphone"), Qt::CaseInsensitive)) {
            score += 50;
        }
        if (device.friendlyName.contains(QStringLiteral("Speakers"), Qt::CaseInsensitive)) {
            score += 20;
        }
        return score;
    };

    const AudioRenderDeviceInfo *best = &devices.first();
    int bestScore = deviceScore(devices.first());
    for (const AudioRenderDeviceInfo &device : devices) {
        const int score = deviceScore(device);
        if (score > bestScore) {
            bestScore = score;
            best = &device;
        }
    }

    if (deviceId) {
        *deviceId = best->id;
    }
    if (friendlyName) {
        *friendlyName = best->friendlyName;
    }
    return true;
}

bool AudioPolicyRouter::routeProcessToDevice(unsigned long processId,
                                             const QString &deviceId,
                                             QString *errorMessage)
{
    if (deviceId.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No routing device available");
        }
        return false;
    }

    if (!ensurePolicyFactory()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Per-app routing is not supported on this Windows build");
        }
        return false;
    }

    const QString wrappedId = wrapDeviceId(deviceId);
    HSTRING hDeviceId = nullptr;
    const HRESULT createHr = WindowsCreateString(reinterpret_cast<LPCWSTR>(wrappedId.utf16()),
                                                 static_cast<UINT32>(wrappedId.size()),
                                                 &hDeviceId);
    if (FAILED(createHr) || !hDeviceId) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to create device id string");
        }
        return false;
    }

    const HRESULT hr = setPersistedEndpoint(processId, hDeviceId);
    WindowsDeleteString(hDeviceId);

    if (FAILED(hr)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("SetPersistedDefaultAudioEndpoint failed: %1")
                                .arg(AudioLog::hresultToString(hr));
        }
        return false;
    }
    return true;
}

bool AudioPolicyRouter::clearProcessRouting(unsigned long processId, QString *errorMessage)
{
    if (!ensurePolicyFactory()) {
        return true;
    }

    const HRESULT hr = setPersistedEndpoint(processId, nullptr);
    if (FAILED(hr)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Clear process routing failed: %1")
                                .arg(AudioLog::hresultToString(hr));
        }
        return false;
    }
    return true;
}

bool AudioPolicyRouter::clearAllPersistedRouting(QString *errorMessage)
{
    if (!ensurePolicyFactory()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Per-app routing is not supported on this Windows build");
        }
        return false;
    }

    const HRESULT hr = g_factory.clearPersisted(g_factory.instance);

    if (FAILED(hr)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Clear all persisted routing failed: %1")
                                .arg(AudioLog::hresultToString(hr));
        }
        return false;
    }
    return true;
}

QString AudioPolicyRouter::persistedRenderDeviceId(unsigned long processId)
{
    if (processId == 0) {
        return {};
    }

    QString persisted = queryPersistedEndpoint(processId, eConsole);
    if (persisted.isEmpty()) {
        persisted = queryPersistedEndpoint(processId, eMultimedia);
    }
    return unwrapPersistedDeviceId(persisted);
}

