#include "startuppresetstore.h"

#include "appiconprovider.h"
#include "apppaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

StartupPresetStore::StartupPresetStore()
{
    load();
}

QString StartupPresetStore::normalizeExe(const QString &exePath)
{
    return AppIconProvider::normalizeExePath(exePath);
}

QString StartupPresetStore::storeFilePath()
{
    return QDir(AppPaths::dataRoot()).filePath(QStringLiteral("startuppresets.json"));
}

bool StartupPresetStore::load()
{
    m_presetIdByExe.clear();
    QFile file(storeFilePath());
    if (!file.exists()) {
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return false;
    }

    const QJsonArray bindings = document.object().value(QStringLiteral("bindings")).toArray();
    for (const QJsonValue &value : bindings) {
        const QJsonObject object = value.toObject();
        const QString exe = normalizeExe(object.value(QStringLiteral("exe")).toString());
        const QString presetId = object.value(QStringLiteral("presetId")).toString();
        if (!exe.isEmpty() && !presetId.isEmpty()) {
            m_presetIdByExe.insert(exe, presetId);
        }
    }
    return true;
}

bool StartupPresetStore::save() const
{
    QJsonArray bindings;
    for (auto it = m_presetIdByExe.constBegin(); it != m_presetIdByExe.constEnd(); ++it) {
        QJsonObject object;
        object.insert(QStringLiteral("exe"), it.key());
        object.insert(QStringLiteral("presetId"), it.value());
        bindings.append(object);
    }

    QJsonObject root;
    root.insert(QStringLiteral("bindings"), bindings);

    QDir().mkpath(QFileInfo(storeFilePath()).absolutePath());
    QFile file(storeFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

QString StartupPresetStore::presetIdForExe(const QString &exePath) const
{
    if (exePath.isEmpty()) {
        return {};
    }
    return m_presetIdByExe.value(normalizeExe(exePath));
}

bool StartupPresetStore::hasBinding(const QString &exePath) const
{
    return !presetIdForExe(exePath).isEmpty();
}

bool StartupPresetStore::setBinding(const QString &exePath, const QString &presetId)
{
    const QString key = normalizeExe(exePath);
    if (key.isEmpty() || presetId.isEmpty()) {
        return false;
    }
    m_presetIdByExe.insert(key, presetId);
    return save();
}

bool StartupPresetStore::removeBinding(const QString &exePath)
{
    const QString key = normalizeExe(exePath);
    if (key.isEmpty() || !m_presetIdByExe.contains(key)) {
        return true;
    }
    m_presetIdByExe.remove(key);
    return save();
}
