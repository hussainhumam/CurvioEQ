#pragma once

#include <QString>
#include <QVector>

struct AudioRenderDeviceInfo {
    QString id;
    QString friendlyName;
    bool isDefault = false;
};

class AudioPolicyRouter
{
public:
    static bool isRoutingSupported();
    static QVector<AudioRenderDeviceInfo> listRenderDevices();
    static QVector<AudioRenderDeviceInfo> listRenderDevicesExcluding(const QString &excludeDeviceId);
    static bool defaultRenderDevice(QString *deviceId = nullptr, QString *friendlyName = nullptr);
    static bool preferredRenderDevice(QString *deviceId = nullptr, QString *friendlyName = nullptr);
    static bool preferredRoutingSinkDevice(QString *deviceId = nullptr, QString *friendlyName = nullptr);
    static bool routeProcessToDevice(unsigned long processId, const QString &deviceId, QString *errorMessage);
    static bool routeProcessTreeToDevice(unsigned long rootProcessId,
                                         const QString &deviceId,
                                         int *routedCount = nullptr,
                                         QString *errorMessage = nullptr);
    static bool clearProcessRouting(unsigned long processId, QString *errorMessage);
    static bool clearProcessTreeRouting(unsigned long rootProcessId, QString *errorMessage = nullptr);
    static bool verifyProcessTreeRouted(unsigned long rootProcessId, const QString &deviceId);
    static bool clearAllPersistedRouting(QString *errorMessage);
    static QString persistedRenderDeviceId(unsigned long processId);
};
