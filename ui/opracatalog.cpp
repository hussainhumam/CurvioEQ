#include "opracatalog.h"

#include "ui/apppaths.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>
#include <cmath>

namespace {

QString deriveProductIdFromEqId(const QString &eqId)
{
    // EQ ids look like "vendor:product::slug"; product ids use "vendor::product".
    const int sep = eqId.indexOf(QLatin1String("::"));
    if (sep <= 0) {
        return {};
    }
    QString head = eqId.left(sep);
    const int colon = head.indexOf(QLatin1Char(':'));
    if (colon > 0 && !head.contains(QLatin1String("::"))) {
        head.replace(colon, 1, QLatin1String("::"));
    }
    return head;
}

bool mapBandType(const QString &type, EqFilterType *out)
{
    if (!out) {
        return false;
    }
    if (type == QLatin1String("peak_dip")) {
        *out = EqFilterType::Peaking;
        return true;
    }
    if (type == QLatin1String("low_shelf")) {
        *out = EqFilterType::LowShelf;
        return true;
    }
    if (type == QLatin1String("high_shelf")) {
        *out = EqFilterType::HighShelf;
        return true;
    }
    return false;
}

QString bandTypeKey(EqFilterType type)
{
    switch (type) {
    case EqFilterType::LowShelf:
        return QStringLiteral("low_shelf");
    case EqFilterType::HighShelf:
        return QStringLiteral("high_shelf");
    case EqFilterType::Peaking:
    default:
        return QStringLiteral("peak_dip");
    }
}

} // namespace

QString OpraCatalog::cacheDirectory()
{
    return QDir(AppPaths::dataRoot()).filePath(QStringLiteral("opra"));
}

QString OpraCatalog::cacheFilePath()
{
    return QDir(cacheDirectory()).filePath(QStringLiteral("index-cache.json"));
}

QString OpraCatalog::databaseUrl()
{
    // Preferred mirror for open-source / personal use (see OPRA docs/CONSUMING.md).
    return QStringLiteral("http://opra.roonlabs.net/database_v1.jsonl");
}

bool OpraCatalog::loadCache(QVector<OpraProfile> *out, QString *errorMessage)
{
    if (!out) {
        return false;
    }
    out->clear();

    QFile file(cacheFilePath());
    if (!file.exists()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No OPRA cache found");
        }
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not read OPRA cache");
        }
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid OPRA cache");
        }
        return false;
    }

    const QJsonArray array = doc.array();
    out->reserve(array.size());
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        OpraProfile profile;
        profile.id = object.value(QStringLiteral("i")).toString();
        profile.name = object.value(QStringLiteral("n")).toString();
        profile.author = object.value(QStringLiteral("a")).toString();
        profile.details = object.value(QStringLiteral("d")).toString();
        profile.link = object.value(QStringLiteral("l")).toString();
        profile.preampDb = static_cast<float>(object.value(QStringLiteral("g")).toDouble());
        const QJsonArray bands = object.value(QStringLiteral("b")).toArray();
        profile.bands.reserve(bands.size());
        for (const QJsonValue &bandValue : bands) {
            const QJsonObject bandObject = bandValue.toObject();
            OpraBand band;
            EqFilterType type = EqFilterType::Peaking;
            if (!mapBandType(bandObject.value(QStringLiteral("t")).toString(), &type)) {
                continue;
            }
            band.type = type;
            band.freqHz = static_cast<float>(bandObject.value(QStringLiteral("f")).toDouble());
            band.gainDb = static_cast<float>(bandObject.value(QStringLiteral("g")).toDouble());
            band.q = static_cast<float>(bandObject.value(QStringLiteral("q")).toDouble(0.7));
            profile.bands.push_back(band);
        }
        if (profile.id.isEmpty() || profile.name.isEmpty() || profile.bands.isEmpty()) {
            continue;
        }
        out->push_back(profile);
    }

    if (out->isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("OPRA cache is empty");
        }
        return false;
    }
    return true;
}

bool OpraCatalog::saveCache(const QVector<OpraProfile> &entries, QString *errorMessage)
{
    if (!QDir().mkpath(cacheDirectory())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not create OPRA cache directory");
        }
        return false;
    }

    QJsonArray array;
    for (const OpraProfile &profile : entries) {
        QJsonObject object;
        object.insert(QStringLiteral("i"), profile.id);
        object.insert(QStringLiteral("n"), profile.name);
        object.insert(QStringLiteral("a"), profile.author);
        object.insert(QStringLiteral("d"), profile.details);
        object.insert(QStringLiteral("l"), profile.link);
        object.insert(QStringLiteral("g"), profile.preampDb);
        QJsonArray bands;
        for (const OpraBand &band : profile.bands) {
            QJsonObject bandObject;
            bandObject.insert(QStringLiteral("t"), bandTypeKey(band.type));
            bandObject.insert(QStringLiteral("f"), band.freqHz);
            bandObject.insert(QStringLiteral("g"), band.gainDb);
            bandObject.insert(QStringLiteral("q"), band.q);
            bands.append(bandObject);
        }
        object.insert(QStringLiteral("b"), bands);
        array.append(object);
    }

    QFile file(cacheFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not write OPRA cache");
        }
        return false;
    }
    file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
    return true;
}

bool OpraCatalog::parseDatabaseJsonl(const QByteArray &jsonl,
                                     QVector<OpraProfile> *out,
                                     QString *errorMessage)
{
    if (!out) {
        return false;
    }
    out->clear();

    QHash<QString, QString> vendorNames;
    QHash<QString, QString> productNames;
    QHash<QString, QString> productVendorIds;
    QHash<QString, QString> eqProductIds;
    QVector<OpraProfile> pending;
    pending.reserve(20000);

    const QList<QByteArray> lines = jsonl.split('\n');
    for (const QByteArray &rawLine : lines) {
        const QByteArray line = rawLine.trimmed();
        if (line.isEmpty()) {
            continue;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }

        const QJsonObject root = doc.object();
        const QString type = root.value(QStringLiteral("type")).toString();
        const QString id = root.value(QStringLiteral("id")).toString();
        const QJsonObject data = root.value(QStringLiteral("data")).toObject();
        if (id.isEmpty() || data.isEmpty()) {
            continue;
        }

        if (type == QLatin1String("vendor")) {
            const QString name = data.value(QStringLiteral("name")).toString().trimmed();
            if (!name.isEmpty()) {
                vendorNames.insert(id, name);
            }
            continue;
        }

        if (type == QLatin1String("product")) {
            const QString name = data.value(QStringLiteral("name")).toString().trimmed();
            if (name.isEmpty()) {
                continue;
            }
            productNames.insert(id, name);
            const QString vendorId = data.value(QStringLiteral("vendor_id")).toString();
            if (!vendorId.isEmpty()) {
                productVendorIds.insert(id, vendorId);
            }
            continue;
        }

        if (type != QLatin1String("eq")) {
            continue;
        }
        if (data.value(QStringLiteral("type")).toString() != QLatin1String("parametric_eq")) {
            continue;
        }

        const QJsonObject parameters = data.value(QStringLiteral("parameters")).toObject();
        const QJsonArray bandsJson = parameters.value(QStringLiteral("bands")).toArray();
        if (bandsJson.isEmpty()) {
            continue;
        }

        OpraProfile profile;
        profile.id = id;
        profile.author = data.value(QStringLiteral("author")).toString().trimmed();
        profile.details = data.value(QStringLiteral("details")).toString().trimmed();
        profile.link = data.value(QStringLiteral("link")).toString().trimmed();
        profile.preampDb = static_cast<float>(parameters.value(QStringLiteral("gain_db")).toDouble());

        QString productId = data.value(QStringLiteral("product_id")).toString();
        if (productId.isEmpty()) {
            productId = deriveProductIdFromEqId(id);
        }
        eqProductIds.insert(id, productId);

        profile.bands.reserve(qMin(bandsJson.size(), qsizetype{EqState::kMaxParametricFilters}));
        for (const QJsonValue &bandValue : bandsJson) {
            if (profile.bands.size() >= EqState::kMaxParametricFilters) {
                break;
            }
            const QJsonObject bandObject = bandValue.toObject();
            EqFilterType filterType = EqFilterType::Peaking;
            if (!mapBandType(bandObject.value(QStringLiteral("type")).toString(), &filterType)) {
                continue;
            }
            OpraBand band;
            band.type = filterType;
            band.freqHz = static_cast<float>(bandObject.value(QStringLiteral("frequency")).toDouble());
            band.gainDb = static_cast<float>(bandObject.value(QStringLiteral("gain_db")).toDouble());
            band.q = static_cast<float>(bandObject.value(QStringLiteral("q")).toDouble(0.7));
            if (band.freqHz < 10.f || band.q < 0.05f) {
                continue;
            }
            profile.bands.push_back(band);
        }

        if (profile.bands.isEmpty()) {
            continue;
        }
        pending.push_back(profile);
    }

    out->reserve(pending.size());
    for (OpraProfile &profile : pending) {
        const QString productId = eqProductIds.value(profile.id);
        QString productName = productNames.value(productId);
        QString vendorId = productVendorIds.value(productId);
        if (vendorId.isEmpty() && productId.contains(QLatin1String("::"))) {
            vendorId = productId.section(QLatin1String("::"), 0, 0);
        }
        const QString vendorName = vendorNames.value(vendorId);

        if (!productName.isEmpty() && !vendorName.isEmpty()) {
            profile.name = vendorName + QLatin1Char(' ') + productName;
        } else if (!productName.isEmpty()) {
            profile.name = productName;
        } else {
            profile.name = productId;
            profile.name.replace(QStringLiteral("::"), QStringLiteral(" "));
            profile.name.replace(QLatin1Char(':'), QLatin1Char(' '));
            profile.name.replace(QLatin1Char('_'), QLatin1Char(' '));
        }

        if (profile.author.isEmpty()) {
            profile.author = QStringLiteral("OPRA");
        }
        out->push_back(profile);
    }

    if (out->isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No OPRA parametric EQ profiles found in database");
        }
        return false;
    }
    return true;
}

bool OpraCatalog::toEqState(const OpraProfile &profile, EqState *state, QString *errorMessage)
{
    if (!state) {
        return false;
    }
    *state = EqState{};
    state->advanced = true;

    if (profile.bands.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("OPRA profile has no usable bands");
        }
        return false;
    }

    for (const OpraBand &band : profile.bands) {
        if (state->filterCount >= EqState::kMaxParametricFilters) {
            break;
        }
        EqFilter filter;
        filter.type = band.type;
        filter.freqHz = band.freqHz;
        filter.gainDb = band.gainDb;
        filter.q = std::max(0.05f, band.q);
        state->filters[static_cast<size_t>(state->filterCount++)] = filter;
    }

    if (state->filterCount == 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("OPRA profile produced no filters");
        }
        return false;
    }

    // OPRA gain_db is a separate overall preamp; CurvioEQ's mix limiter covers clipping
    // headroom, so we keep band gains as authored (same approach as AutoEQ import).
    (void)profile.preampDb;

    *state = EqResponse::advancedToSimple(*state);
    state->advanced = true;
    return true;
}

OnlinePresetProfile OpraCatalog::toOnlineProfile(const OpraProfile &profile)
{
    OnlinePresetProfile online;
    online.kind = OnlinePresetKind::Opra;
    online.name = profile.name;
    online.source = profile.author;
    if (!profile.details.isEmpty()) {
        online.source += QStringLiteral(" · %1").arg(profile.details);
    }
    online.relativePath = profile.id;
    online.dbType = QStringLiteral("OPRA");
    return online;
}

QVector<OnlinePresetProfile> OpraCatalog::toOnlineProfiles(const QVector<OpraProfile> &profiles)
{
    QVector<OnlinePresetProfile> out;
    out.reserve(profiles.size());
    for (const OpraProfile &profile : profiles) {
        out.push_back(toOnlineProfile(profile));
    }
    return out;
}

QVector<OpraProfile> OpraCatalog::filter(const QVector<OpraProfile> &all, const QString &query)
{
    const QString needle = query.trimmed();
    if (needle.isEmpty()) {
        return all;
    }

    QVector<OpraProfile> matched;
    matched.reserve(qMin(all.size(), qsizetype{512}));
    for (const OpraProfile &profile : all) {
        if (profile.name.contains(needle, Qt::CaseInsensitive)
            || profile.author.contains(needle, Qt::CaseInsensitive)
            || profile.details.contains(needle, Qt::CaseInsensitive)
            || profile.id.contains(needle, Qt::CaseInsensitive)) {
            matched.push_back(profile);
        }
    }
    return matched;
}
