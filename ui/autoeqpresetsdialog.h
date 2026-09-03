#pragma once

#include "ui/autoeqcatalog.h"
#include "ui/opracatalog.h"
#include "ui/presetstore.h"
#include "ui/squiglinkcatalog.h"

#include <QDialog>
#include <QVector>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;
class QTimer;

class OnlinePresetsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OnlinePresetsDialog(PresetStore *store, QWidget *parent = nullptr);

    EqPreset importedPreset() const { return m_importedPreset; }
    bool didImport() const { return m_didImport; }

private slots:
    void onSearchTextChanged(const QString &text);
    void onSourceFilterChanged(int);
    void onRefreshClicked();
    void onRedownloadClicked();
    void onImportClicked();
    void onSelectionChanged();
    void onNetworkFinished();
    void onListScrollChanged(int value);
    void applyPendingSearch();

private:
    enum class JobKind {
        None,
        AutoEqIndex,
        OpraDatabase,
        SquigSites,
        SquigPhoneBook,
        AutoEqParametric,
        AutoEqGraphic,
        SquigMeasurement
    };

    enum class SourceFilter {
        All = 0,
        AutoEq = 1,
        Squiglink = 2,
        Opra = 3
    };

    static constexpr int kPageSize = 500;

    void setBusy(bool busy, const QString &statusText = {});
    void setStatus(const QString &text, bool isError = false);
    void rebuildFilteredProfiles();
    void resetVisiblePage();
    void appendNextPage();
    void addProfileItem(const OnlinePresetProfile &profile);
    void updateListStatus();
    void loadCaches();
    int matchingCount() const;
    void startRefresh();
    void startOpraDownload();
    void startSquigRefresh();
    void continueSquigQueue();
    void getUrl(const QUrl &url, JobKind kind);
    void abortActive();

    void handleAutoEqIndex(const QByteArray &body, bool ok, const QString &error);
    void handleOpraDatabase(const QByteArray &body, bool ok, const QString &error);
    void handleSquigSites(const QByteArray &body, bool ok, const QString &error);
    void handleSquigPhoneBook(const QByteArray &body, bool ok, const QString &error);
    void handleAutoEqEqFile(const QByteArray &body, bool ok, const QString &error, bool parametric);
    void handleSquigMeasurement(const QByteArray &body, bool ok, const QString &error);

    bool finishAutoEqImport(const AutoEqProfile &profile, const QByteArray &body, bool parametric, QString *errorMessage);
    bool finishSquigImport(const OnlinePresetProfile &profile, const QByteArray &body, QString *errorMessage);
    bool finishOpraImport(const OnlinePresetProfile &profile, QString *errorMessage);
    const OpraProfile *findOpraProfile(const QString &id) const;
    OnlinePresetProfile selectedProfile() const;
    SourceFilter currentFilter() const;

    PresetStore *m_store = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_activeReply = nullptr;
    JobKind m_job = JobKind::None;

    QComboBox *m_sourceCombo = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QListWidget *m_listWidget = nullptr;
    QPushButton *m_importButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_redownloadButton = nullptr;
    QPushButton *m_closeButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_creditLabel = nullptr;
    QTimer *m_searchDebounce = nullptr;

    QVector<AutoEqProfile> m_autoEqProfiles;
    QVector<OnlinePresetProfile> m_squigProfiles;
    QVector<OpraProfile> m_opraProfiles;
    int m_scanAutoEq = 0;
    int m_scanSquig = 0;
    int m_scanOpra = 0;
    int m_visibleCount = 0;
    int m_matchTotal = 0;
    bool m_hasMore = false;
    bool m_loadingPage = false;

    QVector<SquigSiteDb> m_squigQueue;
    int m_squigQueueIndex = 0;
    int m_squigOkCount = 0;
    int m_squigFailCount = 0;
    OnlinePresetProfile m_pendingImport;
    int m_squigChannelAttempt = 0;

    EqPreset m_importedPreset;
    bool m_didImport = false;
    bool m_busy = false;
};

// Compatibility alias for existing includes.
using AutoEqPresetsDialog = OnlinePresetsDialog;
