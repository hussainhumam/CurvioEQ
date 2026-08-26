#pragma once

#include "audio/eqprocessor.h"
#include "audio/eqstate.h"

#include <QString>
#include <QVector>

enum class OnlinePresetKind {
    AutoEq = 0,
    Squiglink = 1,
    Opra = 2
};

struct OnlinePresetProfile {
    OnlinePresetKind kind = OnlinePresetKind::AutoEq;
    QString name;
    QString source;       // AutoEQ source or Squiglink site name
    QString relativePath; // AutoEQ relative path
    QString dataBaseUrl;  // Squiglink …/data
    QString fileBase;     // Squiglink measurement file base (no L/R)
    QString dbType;       // IEMs / Headphones / …
};

struct SquigSiteDb {
    QString siteName;
    QString username;
    QString urlType;
    QString dbType;
    QString folder;
};

class SquiglinkCatalog
{
public:
    static QString cacheDirectory();
    static QString cacheFilePath();
    static QString sitesUrl();

    static bool loadCache(QVector<OnlinePresetProfile> *out, QString *errorMessage = nullptr);
    static bool saveCache(const QVector<OnlinePresetProfile> &entries, QString *errorMessage = nullptr);

    static bool parseSitesJson(const QByteArray &json,
                               QVector<SquigSiteDb> *out,
                               QString *errorMessage = nullptr);

    static QString dataBaseUrl(const SquigSiteDb &db);
    static QString phoneBookUrl(const SquigSiteDb &db);
    static QString measurementUrl(const OnlinePresetProfile &profile, const QString &channelSuffix);

    static bool parsePhoneBook(const QByteArray &json,
                               const SquigSiteDb &db,
                               QVector<OnlinePresetProfile> *out,
                               QString *errorMessage = nullptr);

    // Extra Crinacle graphing DBs (not always listed on squigsites.json).
    static QVector<SquigSiteDb> extraDatabases();

    // REW-style measurement → Advanced peaking EQ (center @ 1 kHz, invert toward flat).
    static bool parseMeasurementToAdvancedEq(const QString &text,
                                             EqState *state,
                                             QString *errorMessage = nullptr);

    static QVector<OnlinePresetProfile> filter(const QVector<OnlinePresetProfile> &all,
                                               const QString &query);
};
