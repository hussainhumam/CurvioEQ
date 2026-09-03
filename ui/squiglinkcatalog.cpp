#include "squiglinkcatalog.h"

#include "ui/appconstants.h"
#include "ui/apppaths.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace {

float interpolateLogGain(const std::vector<std::pair<float, float>> &points, float freqHz)
{
    if (points.empty()) {
        return 0.f;
    }
    if (freqHz <= points.front().first) {
        return points.front().second;
    }
    if (freqHz >= points.back().first) {
        return points.back().second;
    }
    for (size_t i = 1; i < points.size(); ++i) {
        const float f0 = points[i - 1].first;
        const float f1 = points[i].first;
        if (freqHz > f1) {
            continue;
        }
        const float g0 = points[i - 1].second;
        const float g1 = points[i].second;
        const float t = (std::log10(freqHz) - std::log10(f0)) / (std::log10(f1) - std::log10(f0));
        return g0 + t * (g1 - g0);
    }
    return points.back().second;
}

QStringList fileBasesFromValue(const QJsonValue &value)
{
    QStringList out;
    if (value.isString()) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty()) {
            out.append(text);
        }
    } else if (value.isArray()) {
        for (const QJsonValue &item : value.toArray()) {
            const QString text = item.toString().trimmed();
            if (!text.isEmpty()) {
                out.append(text);
            }
        }
    }
    return out;
}

} // namespace

QString SquiglinkCatalog::cacheDirectory()
{
    return QDir(AppPaths::dataRoot()).filePath(QStringLiteral("squiglink"));
}

QString SquiglinkCatalog::cacheFilePath()
{
    return QDir(cacheDirectory()).filePath(QStringLiteral("profiles.json"));
}

QString SquiglinkCatalog::sitesUrl()
{
    return QStringLiteral("https://squig.link/squigsites.json");
}

bool SquiglinkCatalog::loadCache(QVector<OnlinePresetProfile> *out, QString *errorMessage)
{
    if (!out) {
        return false;
    }
    QFile file(cacheFilePath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No Squiglink cache found");
        }
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid Squiglink cache");
        }
        return false;
    }
    out->clear();
    for (const QJsonValue &value : doc.array()) {
        const QJsonObject object = value.toObject();
        OnlinePresetProfile profile;
        profile.kind = OnlinePresetKind::Squiglink;
        profile.name = object.value(QStringLiteral("name")).toString();
        profile.source = object.value(QStringLiteral("source")).toString();
        profile.dataBaseUrl = object.value(QStringLiteral("dataBaseUrl")).toString();
        profile.fileBase = object.value(QStringLiteral("fileBase")).toString();
        profile.dbType = object.value(QStringLiteral("dbType")).toString();
        // Drop legacy Crinacle graphing entries — raw .txt files are not HTTP-accessible.
        if (profile.dataBaseUrl.contains(QLatin1String("crinacle.com"), Qt::CaseInsensitive)) {
            continue;
        }
        if (!profile.name.isEmpty() && !profile.dataBaseUrl.isEmpty() && !profile.fileBase.isEmpty()) {
            out->push_back(profile);
        }
    }
    return !out->isEmpty();
}

bool SquiglinkCatalog::saveCache(const QVector<OnlinePresetProfile> &entries, QString *errorMessage)
{
    QDir().mkpath(cacheDirectory());
    QJsonArray array;
    for (const OnlinePresetProfile &profile : entries) {
        QJsonObject object;
        object.insert(QStringLiteral("name"), profile.name);
        object.insert(QStringLiteral("source"), profile.source);
        object.insert(QStringLiteral("dataBaseUrl"), profile.dataBaseUrl);
        object.insert(QStringLiteral("fileBase"), profile.fileBase);
        object.insert(QStringLiteral("dbType"), profile.dbType);
        array.append(object);
    }
    QFile file(cacheFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not write Squiglink cache");
        }
        return false;
    }
    file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
    return true;
}

bool SquiglinkCatalog::clearCache(QString *errorMessage)
{
    QFile file(cacheFilePath());
    if (!file.exists()) {
        return true;
    }
    if (!file.remove()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not delete Squiglink cache");
        }
        return false;
    }
    return true;
}

bool SquiglinkCatalog::parseSitesJson(const QByteArray &json,
                                      QVector<SquigSiteDb> *out,
                                      QString *errorMessage)
{
    if (!out) {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isArray()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid squigsites.json");
        }
        return false;
    }
    out->clear();
    for (const QJsonValue &value : doc.array()) {
        const QJsonObject site = value.toObject();
        const QString username = site.value(QStringLiteral("username")).toString().trimmed();
        const QString siteName = site.value(QStringLiteral("name")).toString().trimmed();
        const QString urlType = site.value(QStringLiteral("urlType")).toString().trimmed();
        if (username.isEmpty() || siteName.isEmpty()) {
            continue;
        }
        for (const QJsonValue &dbValue : site.value(QStringLiteral("dbs")).toArray()) {
            const QJsonObject dbObject = dbValue.toObject();
            SquigSiteDb db;
            db.siteName = siteName;
            db.username = username;
            db.urlType = urlType;
            db.dbType = dbObject.value(QStringLiteral("type")).toString(QStringLiteral("IEMs"));
            db.folder = dbObject.value(QStringLiteral("folder")).toString(QStringLiteral("/"));
            if (!db.folder.startsWith(QLatin1Char('/'))) {
                db.folder.prepend(QLatin1Char('/'));
            }
            if (!db.folder.endsWith(QLatin1Char('/'))) {
                db.folder.append(QLatin1Char('/'));
            }
            out->push_back(db);
        }
    }
    return !out->isEmpty();
}

QString SquiglinkCatalog::dataBaseUrl(const SquigSiteDb &db)
{
    return QStringLiteral("https://%1.squig.link%2data").arg(db.username, db.folder);
}

QString SquiglinkCatalog::phoneBookUrl(const SquigSiteDb &db)
{
    return dataBaseUrl(db) + QStringLiteral("/phone_book.json");
}

QString SquiglinkCatalog::measurementUrl(const OnlinePresetProfile &profile, const QString &channelSuffix)
{
    // Encode the full file name so spaces/parentheses survive QUrl parsing.
    const QString fileName = profile.fileBase + channelSuffix + QStringLiteral(".txt");
    const QByteArray encodedFile = QUrl::toPercentEncoding(fileName, QByteArrayLiteral(".-_~"));
    return profile.dataBaseUrl + QLatin1Char('/') + QString::fromUtf8(encodedFile);
}

QVector<SquigSiteDb> SquiglinkCatalog::extraDatabases()
{
    // Crinacle's graphing host exposes phone_book.json but not raw measurement
    // .txt files over HTTP (404). AutoEQ pulls those from Drive instead — skip here.
    return {};
}

bool SquiglinkCatalog::parsePhoneBook(const QByteArray &json,
                                      const SquigSiteDb &db,
                                      QVector<OnlinePresetProfile> *out,
                                      QString *errorMessage)
{
    if (!out) {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isArray()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid phone_book.json");
        }
        return false;
    }

    const QString baseUrl = dataBaseUrl(db);

    int added = 0;
    for (const QJsonValue &brandValue : doc.array()) {
        const QJsonObject brand = brandValue.toObject();
        QString brandName = brand.value(QStringLiteral("name")).toString().trimmed();
        const QString brandSuffix = brand.value(QStringLiteral("suffix")).toString().trimmed();
        if (!brandSuffix.isEmpty()) {
            brandName = brandName + QLatin1Char(' ') + brandSuffix;
        }

        for (const QJsonValue &phoneValue : brand.value(QStringLiteral("phones")).toArray()) {
            if (phoneValue.isString()) {
                const QString model = phoneValue.toString().trimmed();
                if (model.isEmpty()) {
                    continue;
                }
                OnlinePresetProfile profile;
                profile.kind = OnlinePresetKind::Squiglink;
                profile.name = (brandName + QLatin1Char(' ') + model).trimmed();
                profile.source = db.siteName;
                profile.dataBaseUrl = baseUrl;
                profile.fileBase = model;
                profile.dbType = db.dbType;
                out->push_back(profile);
                ++added;
                continue;
            }

            const QJsonObject phone = phoneValue.toObject();
            const QString modelName = phone.value(QStringLiteral("name")).toString().trimmed();
            const QStringList files = fileBasesFromValue(phone.value(QStringLiteral("file")));
            QStringList suffixes;
            const QJsonValue suffixValue = phone.value(QStringLiteral("suffix"));
            if (suffixValue.isArray()) {
                for (const QJsonValue &s : suffixValue.toArray()) {
                    suffixes.append(s.toString().trimmed());
                }
            } else if (suffixValue.isString()) {
                suffixes.append(suffixValue.toString().trimmed());
            }

            if (files.isEmpty()) {
                continue;
            }

            for (int i = 0; i < files.size(); ++i) {
                OnlinePresetProfile profile;
                profile.kind = OnlinePresetKind::Squiglink;
                profile.fileBase = files.at(i);
                profile.source = db.siteName;
                profile.dataBaseUrl = baseUrl;
                profile.dbType = db.dbType;
                QString display = (brandName + QLatin1Char(' ') + modelName).trimmed();
                if (i < suffixes.size() && !suffixes.at(i).isEmpty()) {
                    display += QLatin1Char(' ') + suffixes.at(i);
                } else if (files.size() > 1) {
                    display += QStringLiteral(" [%1]").arg(i + 1);
                }
                profile.name = display;
                out->push_back(profile);
                ++added;
            }
        }
    }

    if (added <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("phone_book.json contained no models");
        }
        return false;
    }
    return true;
}

bool SquiglinkCatalog::parseMeasurementToAdvancedEq(const QString &text,
                                                    EqState *state,
                                                    QString *errorMessage)
{
    if (!state) {
        return false;
    }

    std::vector<std::pair<float, float>> points;
    points.reserve(512);
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\r\n]+")),
                                         Qt::SkipEmptyParts);
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('*')) || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const QStringList parts = line.split(QRegularExpression(QStringLiteral("[\\s,;\\t]+")),
                                             Qt::SkipEmptyParts);
        if (parts.size() < 2) {
            continue;
        }
        bool okFreq = false;
        bool okSpl = false;
        const float freq = parts.at(0).toFloat(&okFreq);
        const float spl = parts.at(1).toFloat(&okSpl);
        if (!okFreq || !okSpl || !(freq > 0.f) || !std::isfinite(spl)) {
            continue;
        }
        points.emplace_back(freq, spl);
    }

    if (points.size() < 8) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Measurement did not contain enough FR points");
        }
        return false;
    }

    std::sort(points.begin(), points.end(),
              [](const std::pair<float, float> &a, const std::pair<float, float> &b) {
                  return a.first < b.first;
              });

    const float centerSpl = interpolateLogGain(points, 1000.f);
    const float maxGain = static_cast<float>(AppConstants::kMaxGainDb);

    *state = EqState{};
    state->advanced = true;
    state->filterCount = 0;

    for (int band = 0; band < EqState::kBandCount
                       && state->filterCount < EqState::kMaxParametricFilters;
         ++band) {
        const float freq = EqState::kBandFreqs[static_cast<size_t>(band)];
        const float measured = interpolateLogGain(points, freq) - centerSpl;
        // Invert toward a flat target (not Harman). Useful baseline from raw Squiglink FR.
        const float gainDb = std::clamp(-measured, -maxGain, maxGain);
        if (std::fabs(gainDb) < 0.35f) {
            continue;
        }
        EqFilter filter;
        filter.type = EqFilterType::Peaking;
        filter.freqHz = freq;
        filter.gainDb = gainDb;
        filter.q = EqResponse::kDefaultQ;
        state->filters[static_cast<size_t>(state->filterCount++)] = filter;
        state->gainsDb[static_cast<size_t>(band)] = gainDb;
    }

    if (state->filterCount <= 0) {
        // Flat-ish measurement — still create a valid Advanced state.
        *state = EqResponse::simpleToAdvanced(state->gainsDb);
        state->advanced = true;
    } else {
        *state = EqResponse::advancedToSimple(*state);
        state->advanced = true;
    }
    return true;
}
