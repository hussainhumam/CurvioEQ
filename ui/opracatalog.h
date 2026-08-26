#pragma once

#include "audio/eqstate.h"
#include "ui/squiglinkcatalog.h"

#include <QString>
#include <QVector>

struct OpraBand {
    EqFilterType type = EqFilterType::Peaking;
    float freqHz = 1000.f;
    float gainDb = 0.f;
    float q = 0.7f;
};

struct OpraProfile {
    QString id;
    QString name;
    QString author;
    QString details;
    QString link;
    float preampDb = 0.f;
    QVector<OpraBand> bands;
};

class OpraCatalog
{
public:
    static QString cacheDirectory();
    static QString cacheFilePath();
    static QString databaseUrl();

    static bool loadCache(QVector<OpraProfile> *out, QString *errorMessage = nullptr);
    static bool saveCache(const QVector<OpraProfile> &entries, QString *errorMessage = nullptr);

    static bool parseDatabaseJsonl(const QByteArray &jsonl,
                                   QVector<OpraProfile> *out,
                                   QString *errorMessage = nullptr);

    static bool toEqState(const OpraProfile &profile, EqState *state, QString *errorMessage = nullptr);

    static OnlinePresetProfile toOnlineProfile(const OpraProfile &profile);
    static QVector<OnlinePresetProfile> toOnlineProfiles(const QVector<OpraProfile> &profiles);

    static QVector<OpraProfile> filter(const QVector<OpraProfile> &all, const QString &query);
};
