#include "autoeqcatalog.h"

#include "ui/appconstants.h"
#include "ui/apppaths.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace {

QString stripLeadingDotSlash(QString path)
{
    if (path.startsWith(QLatin1String("./"))) {
        path = path.mid(2);
    }
    return path;
}

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

    for (size_t i = 0; i + 1 < points.size(); ++i) {
        const float f0 = points[i].first;
        const float f1 = points[i + 1].first;
        if (freqHz < f0 || freqHz > f1) {
            continue;
        }
        if (f1 <= f0) {
            return points[i].second;
        }
        const float g0 = points[i].second;
        const float g1 = points[i + 1].second;
        const float logF = std::log(freqHz);
        const float log0 = std::log(f0);
        const float log1 = std::log(f1);
        const float t = (logF - log0) / (log1 - log0);
        return g0 + t * (g1 - g0);
    }

    return points.back().second;
}

} // namespace

QString AutoEqCatalog::cacheDirectory()
{
    return QDir(AppPaths::dataRoot()).filePath(QStringLiteral("autoeq"));
}

QString AutoEqCatalog::cacheFilePath()
{
    return QDir(cacheDirectory()).filePath(QStringLiteral("index-cache.json"));
}

bool AutoEqCatalog::loadCache(QVector<AutoEqProfile> *out, QString *errorMessage)
{
    if (!out) {
        return false;
    }
    out->clear();

    QFile file(cacheFilePath());
    if (!file.exists()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No cached AutoEQ index");
        }
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not read AutoEQ cache: %1").arg(cacheFilePath());
        }
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid AutoEQ cache format");
        }
        return false;
    }

    const QJsonArray array = doc.array();
    out->reserve(array.size());
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        AutoEqProfile profile;
        profile.name = object.value(QStringLiteral("n")).toString();
        profile.source = object.value(QStringLiteral("s")).toString();
        profile.relativePath = object.value(QStringLiteral("p")).toString();
        if (profile.name.isEmpty() || profile.relativePath.isEmpty()) {
            continue;
        }
        out->push_back(profile);
    }

    if (out->isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("AutoEQ cache is empty");
        }
        return false;
    }
    return true;
}

bool AutoEqCatalog::saveCache(const QVector<AutoEqProfile> &entries, QString *errorMessage)
{
    if (!QDir().mkpath(cacheDirectory())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not create AutoEQ cache directory");
        }
        return false;
    }

    QJsonArray array;
    for (const AutoEqProfile &profile : entries) {
        QJsonObject object;
        object.insert(QStringLiteral("n"), profile.name);
        object.insert(QStringLiteral("s"), profile.source);
        object.insert(QStringLiteral("p"), profile.relativePath);
        array.append(object);
    }

    QFile file(cacheFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not write AutoEQ cache: %1").arg(cacheFilePath());
        }
        return false;
    }
    file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
    return true;
}

bool AutoEqCatalog::parseIndexMarkdown(const QByteArray &markdown,
                                       QVector<AutoEqProfile> *out,
                                       QString *errorMessage)
{
    if (!out) {
        return false;
    }
    out->clear();

    static const QRegularExpression lineRe(
        QStringLiteral(R"(\[([^\]]+)\]\((.*)\)\s+by\s+(.+))"),
        QRegularExpression::CaseInsensitiveOption);

    const QString text = QString::fromUtf8(markdown);
    const QStringList lines = text.split(QLatin1Char('\n'));
    out->reserve(9000);

    for (QString line : lines) {
        line = line.trimmed();
        if (line.startsWith(QLatin1String("- "))) {
            line = line.mid(2).trimmed();
        }

        const QRegularExpressionMatch match = lineRe.match(line);
        if (!match.hasMatch()) {
            continue;
        }

        AutoEqProfile profile;
        profile.name = match.captured(1).trimmed();
        profile.relativePath = stripLeadingDotSlash(match.captured(2).trimmed());
        profile.source = match.captured(3).trimmed();
        if (profile.name.isEmpty() || profile.relativePath.isEmpty()) {
            continue;
        }
        out->push_back(profile);
    }

    if (out->isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No AutoEQ profiles found in INDEX.md");
        }
        return false;
    }
    return true;
}

QString AutoEqCatalog::indexMarkdownUrl()
{
    return QStringLiteral("https://raw.githubusercontent.com/jaakkopasanen/AutoEq/master/results/INDEX.md");
}

QString AutoEqCatalog::graphicEqUrl(const AutoEqProfile &profile)
{
    QString pathPart = stripLeadingDotSlash(profile.relativePath);
    while (pathPart.endsWith(QLatin1Char('/'))) {
        pathPart.chop(1);
    }

    // INDEX paths are partially percent-encoded and may contain raw '&'; decode then
    // re-encode each segment so the request URL is valid.
    const QString decodedPath = QUrl::fromPercentEncoding(pathPart.toUtf8());
    QStringList segments = QStringLiteral("jaakkopasanen/AutoEq/master/results")
                               .split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &segment : decodedPath.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        segments.append(segment);
    }
    segments.append(profile.name + QStringLiteral(" GraphicEQ.txt"));

    QString path = QStringLiteral("/");
    for (int i = 0; i < segments.size(); ++i) {
        if (i > 0) {
            path += QLatin1Char('/');
        }
        path += QString::fromUtf8(QUrl::toPercentEncoding(segments.at(i)));
    }

    return QStringLiteral("https://raw.githubusercontent.com") + path;
}

QString AutoEqCatalog::parametricEqUrl(const AutoEqProfile &profile)
{
    QString pathPart = stripLeadingDotSlash(profile.relativePath);
    while (pathPart.endsWith(QLatin1Char('/'))) {
        pathPart.chop(1);
    }

    const QString decodedPath = QUrl::fromPercentEncoding(pathPart.toUtf8());
    QStringList segments = QStringLiteral("jaakkopasanen/AutoEq/master/results")
                               .split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &segment : decodedPath.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        segments.append(segment);
    }
    segments.append(profile.name + QStringLiteral(" ParametricEQ.txt"));

    QString path = QStringLiteral("/");
    for (int i = 0; i < segments.size(); ++i) {
        if (i > 0) {
            path += QLatin1Char('/');
        }
        path += QString::fromUtf8(QUrl::toPercentEncoding(segments.at(i)));
    }

    return QStringLiteral("https://raw.githubusercontent.com") + path;
}

bool AutoEqCatalog::parseGraphicEqToBands(const QString &text,
                                          std::array<float, EqProcessor::kBandCount> *gainsDb,
                                          QString *errorMessage)
{
    if (!gainsDb) {
        return false;
    }
    gainsDb->fill(0.f);

    QString payload = text.trimmed();
    const int colon = payload.indexOf(QLatin1Char(':'));
    if (payload.startsWith(QLatin1String("GraphicEQ"), Qt::CaseInsensitive) && colon >= 0) {
        payload = payload.mid(colon + 1).trimmed();
    }

    std::vector<std::pair<float, float>> points;
    points.reserve(128);

    const QStringList pairs = payload.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &pair : pairs) {
        const QStringList parts = pair.trimmed().split(QRegularExpression(QStringLiteral("\\s+")),
                                                       Qt::SkipEmptyParts);
        if (parts.size() < 2) {
            continue;
        }
        bool okFreq = false;
        bool okGain = false;
        const float freq = parts.at(0).toFloat(&okFreq);
        const float gain = parts.at(1).toFloat(&okGain);
        if (!okFreq || !okGain || !(freq > 0.f) || !std::isfinite(gain)) {
            continue;
        }
        points.emplace_back(freq, gain);
    }

    if (points.size() < 2) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("GraphicEQ data did not contain enough frequency points");
        }
        return false;
    }

    std::sort(points.begin(), points.end(),
              [](const std::pair<float, float> &a, const std::pair<float, float> &b) {
                  return a.first < b.first;
              });

    const float maxGain = static_cast<float>(AppConstants::kMaxGainDb);
    for (int band = 0; band < EqProcessor::kBandCount; ++band) {
        const float freq = EqProcessor::kBandFreqs[static_cast<size_t>(band)];
        float gain = interpolateLogGain(points, freq);
        if (!std::isfinite(gain)) {
            gain = 0.f;
        }
        (*gainsDb)[static_cast<size_t>(band)] = std::clamp(gain, -maxGain, maxGain);
    }
    return true;
}

bool AutoEqCatalog::parseParametricEqToState(const QString &text,
                                             EqState *state,
                                             QString *errorMessage)
{
    if (!state) {
        return false;
    }
    *state = EqState{};
    state->advanced = true;

    float preampDb = 0.f;
    static const QRegularExpression preampRe(
        QStringLiteral(R"(Preamp:\s*([+-]?\d+(?:\.\d+)?)\s*dB)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression filterRe(
        QStringLiteral(
            R"(Filter\s+\d+:\s*ON\s+(PK|LSC|HSC|LS|HS)\s+Fc\s+([+-]?\d+(?:\.\d+)?)\s*Hz\s+Gain\s+([+-]?\d+(?:\.\d+)?)\s*dB\s+Q\s+([+-]?\d+(?:\.\d+)?))"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch preampMatch = preampRe.match(text);
    if (preampMatch.hasMatch()) {
        preampDb = preampMatch.captured(1).toFloat();
    }

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QRegularExpressionMatch match = filterRe.match(line.trimmed());
        if (!match.hasMatch()) {
            continue;
        }
        if (state->filterCount >= EqState::kMaxParametricFilters) {
            break;
        }

        EqFilter filter;
        const QString type = match.captured(1).toUpper();
        if (type == QLatin1String("LSC") || type == QLatin1String("LS")) {
            filter.type = EqFilterType::LowShelf;
        } else if (type == QLatin1String("HSC") || type == QLatin1String("HS")) {
            filter.type = EqFilterType::HighShelf;
        } else {
            filter.type = EqFilterType::Peaking;
        }
        filter.freqHz = match.captured(2).toFloat();
        filter.gainDb = match.captured(3).toFloat();
        filter.q = std::max(0.05f, match.captured(4).toFloat());
        state->filters[static_cast<size_t>(state->filterCount++)] = filter;
    }

    if (state->filterCount == 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("ParametricEQ data did not contain any filters");
        }
        return false;
    }

    // Keep filter gains as authored. AutoEq "Preamp" is a separate APO gain stage for
    // clipping headroom; CurvioEQ's mix limiter covers that, so we omit baking it in.
    (void)preampDb;

    *state = EqResponse::advancedToSimple(*state);
    state->advanced = true;
    return true;
}

QVector<AutoEqProfile> AutoEqCatalog::filter(const QVector<AutoEqProfile> &all, const QString &query)
{
    const QString needle = query.trimmed();
    if (needle.isEmpty()) {
        return all;
    }

    QVector<AutoEqProfile> matched;
    matched.reserve(qMin(all.size(), qsizetype{512}));
    for (const AutoEqProfile &profile : all) {
        if (profile.name.contains(needle, Qt::CaseInsensitive)
            || profile.source.contains(needle, Qt::CaseInsensitive)) {
            matched.push_back(profile);
        }
    }
    return matched;
}
