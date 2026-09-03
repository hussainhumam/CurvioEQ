#pragma once

#include "audio/eqprocessor.h"
#include "audio/eqstate.h"
#include "audio/virtualsurroundsettings.h"
#include "audio/dynamicrangesettings.h"
#include "audio/audiochainorder.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

struct EqPreset {
    QString id;
    QString name;
    EqState eq{};
    VirtualSurroundSettings surround{};
    DynamicRangeSettings dynamics{};
    AudioChainOrder audioChainOrder{};
    bool hasEq = true;
    bool hasSurround = false;
    bool hasDynamics = false;
    bool hasAudioChain = false;
    bool isBuiltIn = false;
};

class PresetStore
{
public:
    PresetStore();

    bool load();
    bool save() const;

    QVector<EqPreset> builtInPresets() const;
    QVector<EqPreset> userPresets() const;

    bool addUserPreset(const QString &name, const EqState &eqState, EqPreset *createdPreset = nullptr);
    bool addUserPreset(const EqPreset &preset, EqPreset *createdPreset = nullptr);
    bool removeUserPreset(const QString &id);
    bool importFromFile(const QString &path, QString *errorMessage = nullptr);
    bool exportToFile(const QString &id, const QString &path, QString *errorMessage = nullptr) const;

    bool isFavorite(const QString &id) const;
    bool setFavorite(const QString &id, bool favorite);
    QStringList favoriteIds() const;

    EqPreset presetById(const QString &id) const;
    static QString presetsFilePath();
    static QString includedSectionsLabel(const EqPreset &preset);

private:
    static QVector<EqPreset> defaultBuiltIns();
    static QString makeUniqueName(const QString &baseName, const QVector<EqPreset> &existing);
    static bool parsePresetObject(const QJsonObject &object, EqPreset *preset, QString *errorMessage);
    static QJsonObject toJsonObject(const EqPreset &preset);

    QVector<EqPreset> m_userPresets;
    QStringList m_favoriteIds;
};
