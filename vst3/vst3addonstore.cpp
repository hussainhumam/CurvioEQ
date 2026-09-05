#include "vst3addonstore.h"

#include "ui/apppaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
QString normalizeExe(const QString &exePath)
{
    return QDir::toNativeSeparators(QFileInfo(exePath).absoluteFilePath()).toLower();
}

QJsonObject slotToJson(const Vst3SlotState &slot)
{
    QJsonObject object;
    object.insert(QStringLiteral("occupied"), slot.occupied);
    object.insert(QStringLiteral("uid"), slot.uid);
    object.insert(QStringLiteral("name"), slot.name);
    object.insert(QStringLiteral("vendor"), slot.vendor);
    object.insert(QStringLiteral("path"), slot.path);
    object.insert(QStringLiteral("state"), QString::fromLatin1(slot.state.toBase64()));
    return object;
}

Vst3SlotState slotFromJson(const QJsonObject &object)
{
    Vst3SlotState slot;
    slot.occupied = object.value(QStringLiteral("occupied")).toBool(false);
    slot.uid = object.value(QStringLiteral("uid")).toString();
    slot.name = object.value(QStringLiteral("name")).toString();
    slot.vendor = object.value(QStringLiteral("vendor")).toString();
    slot.path = object.value(QStringLiteral("path")).toString();
    slot.state = QByteArray::fromBase64(object.value(QStringLiteral("state")).toString().toLatin1());
    if (slot.uid.isEmpty() || slot.path.isEmpty()) {
        slot.occupied = false;
    }
    return slot;
}
} // namespace

QString Vst3AddonStore::storeFilePath()
{
    return QDir(AppPaths::dataRoot()).filePath(QStringLiteral("addons.json"));
}

bool Vst3AddonStore::load()
{
    m_byExe.clear();
    QFile file(storeFilePath());
    if (!file.exists()) {
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return false;
    }
    const QJsonObject root = doc.object();
    const QJsonObject apps = root.value(QStringLiteral("apps")).toObject();
    for (auto it = apps.begin(); it != apps.end(); ++it) {
        const QJsonObject appObject = it.value().toObject();
        Vst3AppAddons addons;
        addons.exePath = it.key();
        const QJsonArray slotJson = appObject.value(QStringLiteral("slots")).toArray();
        for (int i = 0; i < kAudioChainAddonCount && i < slotJson.size(); ++i) {
            addons.pluginSlots[static_cast<size_t>(i)] = slotFromJson(slotJson.at(i).toObject());
        }
        AudioChainOrder chain = defaultAudioChainOrder();
        const QJsonArray chainJson = appObject.value(QStringLiteral("chain")).toArray();
        if (!chainJson.isEmpty()) {
            chain.count = 0;
            for (int i = 0; i < chainJson.size() && chain.count < kAudioChainMaxStages; ++i) {
                AudioChainStage stage = AudioChainStage::Eq;
                const QByteArray id = chainJson.at(i).toString().toUtf8();
                if (!audioChainStageFromId(id.constData(), &stage)) {
                    chain = defaultAudioChainOrder();
                    break;
                }
                chain.stages[static_cast<size_t>(chain.count++)] = stage;
            }
        }
        uint8_t mask = 0;
        for (int i = 0; i < kAudioChainAddonCount; ++i) {
            if (addons.pluginSlots[static_cast<size_t>(i)].occupied) {
                mask = static_cast<uint8_t>(mask | (1u << i));
                chain = appendAddonToChain(chain, i);
            } else {
                chain = removeAddonFromChain(chain, i);
            }
        }
        addons.chain = normalizeAudioChainOrder(chain);
        m_byExe.insert(normalizeExe(addons.exePath), addons);
    }
    return true;
}

bool Vst3AddonStore::save() const
{
    const QString path = storeFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonObject apps;
    for (auto it = m_byExe.begin(); it != m_byExe.end(); ++it) {
        const Vst3AppAddons &addons = it.value();
        if (addons.occupiedCount() == 0) {
            continue;
        }
        QJsonObject appObject;
        QJsonArray slotJson;
        for (const Vst3SlotState &slot : addons.pluginSlots) {
            slotJson.append(slotToJson(slot));
        }
        QJsonArray chain;
        const AudioChainOrder normalized = normalizeAudioChainOrder(addons.chain);
        for (int i = 0; i < normalized.count; ++i) {
            chain.append(QString::fromLatin1(audioChainStageId(normalized.stages[static_cast<size_t>(i)])));
        }
        appObject.insert(QStringLiteral("slots"), slotJson);
        appObject.insert(QStringLiteral("chain"), chain);
        apps.insert(addons.exePath.isEmpty() ? it.key() : addons.exePath, appObject);
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("apps"), apps);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

Vst3AppAddons Vst3AddonStore::addonsForExe(const QString &exePath) const
{
    const QString key = normalizeExe(exePath);
    if (m_byExe.contains(key)) {
        return m_byExe.value(key);
    }
    Vst3AppAddons addons;
    addons.exePath = exePath;
    return addons;
}

void Vst3AddonStore::setAddonsForExe(const Vst3AppAddons &addons)
{
    const QString key = normalizeExe(addons.exePath);
    if (key.isEmpty()) {
        return;
    }
    if (addons.occupiedCount() == 0) {
        m_byExe.remove(key);
        return;
    }
    Vst3AppAddons stored = addons;
    stored.exePath = addons.exePath;
    m_byExe.insert(key, stored);
}
