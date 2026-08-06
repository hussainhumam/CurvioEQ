#pragma once

#include "settingsstore.h"

#include "audio/audiopolicyrouter.h"

#include <QVector>

class QComboBox;

struct ResolvedDevice {
    QString id;
    QString name;
    bool ok = false;
};

class AudioDeviceResolver
{
public:
    static int indexForDeviceId(const QVector<AudioRenderDeviceInfo> &devices, const QString &deviceId);

    static int populateCombo(QComboBox *combo,
                             const QVector<AudioRenderDeviceInfo> &devices,
                             const QString &selectedId,
                             bool (*preferredIdLookup)(QString *deviceId) = nullptr);

    static ResolvedDevice resolveRoutingSink(const AppSettings &settings);
    static ResolvedDevice resolveEqOutput(const AppSettings &settings);
};
