#pragma once

#include "vst3plugin.h"
#include "audio/audiochainorder.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <array>

struct Vst3SlotState {
    QString uid;
    QString name;
    QString vendor;
    QString path;
    QByteArray state;
    bool occupied = false;
};

struct Vst3AppAddons {
    QString exePath;
    std::array<Vst3SlotState, kAudioChainAddonCount> pluginSlots{};
    AudioChainOrder chain = defaultAudioChainOrder();

    int occupiedCount() const
    {
        int count = 0;
        for (const Vst3SlotState &slot : pluginSlots) {
            if (slot.occupied) {
                ++count;
            }
        }
        return count;
    }

    int firstFreeSlot() const
    {
        for (int i = 0; i < kAudioChainAddonCount; ++i) {
            if (!pluginSlots[static_cast<size_t>(i)].occupied) {
                return i;
            }
        }
        return -1;
    }
};

class Vst3AddonStore
{
public:
    bool load();
    bool save() const;

    Vst3AppAddons addonsForExe(const QString &exePath) const;
    void setAddonsForExe(const Vst3AppAddons &addons);

    static QString storeFilePath();

private:
    QHash<QString, Vst3AppAddons> m_byExe;
};
