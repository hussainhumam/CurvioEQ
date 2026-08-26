#pragma once

#include "audio/eqprocessor.h"
#include "audio/eqstate.h"

#include <QString>
#include <QVector>

#include <array>

struct AutoEqProfile {
    QString name;
    QString source;
    QString relativePath;
};

class AutoEqCatalog
{
public:
    static QString cacheDirectory();
    static QString cacheFilePath();

    static bool loadCache(QVector<AutoEqProfile> *out, QString *errorMessage = nullptr);
    static bool saveCache(const QVector<AutoEqProfile> &entries, QString *errorMessage = nullptr);

    static bool parseIndexMarkdown(const QByteArray &markdown,
                                   QVector<AutoEqProfile> *out,
                                   QString *errorMessage = nullptr);

    static QString indexMarkdownUrl();
    static QString graphicEqUrl(const AutoEqProfile &profile);
    static QString parametricEqUrl(const AutoEqProfile &profile);

    static bool parseGraphicEqToBands(const QString &text,
                                      std::array<float, EqProcessor::kBandCount> *gainsDb,
                                      QString *errorMessage = nullptr);
    static bool parseParametricEqToState(const QString &text,
                                         EqState *state,
                                         QString *errorMessage = nullptr);

    static QVector<AutoEqProfile> filter(const QVector<AutoEqProfile> &all, const QString &query);
};
