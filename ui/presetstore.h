#pragma once

#include "audio/eqprocessor.h"
#include "audio/eqstate.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

struct EqPreset {
    QString id;
    QString name;
    EqState eq{};
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
    QVector<EqPreset> allPresets() const;

    bool addUserPreset(const QString &name, const EqState &eqState, EqPreset *createdPreset = nullptr);
    bool removeUserPreset(const QString &id);
    bool importFromFile(const QString &path, QString *errorMessage = nullptr);
    bool exportToFile(const QString &id, const QString &path, QString *errorMessage = nullptr) const;

    EqPreset presetById(const QString &id) const;
    static QString presetsFilePath();

private:
    static QVector<EqPreset> defaultBuiltIns();
    static QString makeUniqueName(const QString &baseName, const QVector<EqPreset> &existing);
    static bool parsePresetObject(const QJsonObject &object, EqPreset *preset, QString *errorMessage);
    static QJsonObject toJsonObject(const EqPreset &preset);
    static QString filterTypeToString(EqFilterType type);
    static bool filterTypeFromString(const QString &text, EqFilterType *type);

    QVector<EqPreset> m_userPresets;
};
