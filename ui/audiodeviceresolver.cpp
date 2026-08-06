#include "audiodeviceresolver.h"

#include <QComboBox>

int AudioDeviceResolver::indexForDeviceId(const QVector<AudioRenderDeviceInfo> &devices,
                                        const QString &deviceId)
{
    for (int i = 0; i < devices.size(); ++i) {
        if (devices.at(i).id == deviceId) {
            return i;
        }
    }
    return -1;
}

int AudioDeviceResolver::populateCombo(QComboBox *combo,
                                       const QVector<AudioRenderDeviceInfo> &devices,
                                       const QString &selectedId,
                                       bool (*preferredIdLookup)(QString *deviceId))
{
    if (!combo) {
        return -1;
    }

    combo->clear();
    int selectedIndex = -1;
    for (int i = 0; i < devices.size(); ++i) {
        const AudioRenderDeviceInfo &device = devices.at(i);
        QString label = device.friendlyName;
        if (device.isDefault) {
            label += QStringLiteral(" (Windows default)");
        }
        combo->addItem(label, device.id);
        if (device.id == selectedId) {
            selectedIndex = i;
        }
    }

    if (selectedIndex < 0 && !devices.isEmpty() && preferredIdLookup) {
        QString preferredId;
        if (preferredIdLookup(&preferredId)) {
            selectedIndex = indexForDeviceId(devices, preferredId);
        }
    }

    if (selectedIndex >= 0) {
        combo->setCurrentIndex(selectedIndex);
    }
    return selectedIndex;
}

ResolvedDevice AudioDeviceResolver::resolveRoutingSink(const AppSettings &settings)
{
    ResolvedDevice resolved;
    if (!settings.routingSinkDeviceId.isEmpty()) {
        const QVector<AudioRenderDeviceInfo> devices = AudioPolicyRouter::listRenderDevices();
        for (const AudioRenderDeviceInfo &device : devices) {
            if (device.id == settings.routingSinkDeviceId) {
                resolved.id = device.id;
                resolved.name = device.friendlyName;
                resolved.ok = true;
                return resolved;
            }
        }
    }

    if (AudioPolicyRouter::preferredRoutingSinkDevice(&resolved.id, &resolved.name)) {
        resolved.ok = true;
    }
    return resolved;
}

ResolvedDevice AudioDeviceResolver::resolveEqOutput(const AppSettings &settings)
{
    ResolvedDevice resolved;
    if (!settings.eqOutputDeviceId.isEmpty()) {
        const QVector<AudioRenderDeviceInfo> devices = AudioPolicyRouter::listRenderDevices();
        for (const AudioRenderDeviceInfo &device : devices) {
            if (device.id == settings.eqOutputDeviceId) {
                resolved.id = device.id;
                resolved.name = device.friendlyName;
                resolved.ok = true;
                return resolved;
            }
        }
    }

    if (AudioPolicyRouter::preferredRenderDevice(&resolved.id, &resolved.name)) {
        resolved.ok = true;
    }
    return resolved;
}
