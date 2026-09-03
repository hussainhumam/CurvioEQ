#include "autoeqpresetsdialog.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr int kRoleKind = Qt::UserRole;
constexpr int kRoleName = Qt::UserRole + 1;
constexpr int kRoleSource = Qt::UserRole + 2;
constexpr int kRolePath = Qt::UserRole + 3;
constexpr int kRoleDataUrl = Qt::UserRole + 4;
constexpr int kRoleFileBase = Qt::UserRole + 5;
constexpr int kRoleDbType = Qt::UserRole + 6;

const char *kUserAgent = "CurvioEQ/1.0 (online preset import)";

QStringList squigChannelSuffixes()
{
    return {QStringLiteral(" L"), QStringLiteral(" R"), QStringLiteral(" L1"), QStringLiteral("")};
}

bool autoEqMatches(const AutoEqProfile &profile, const QString &needle)
{
    if (needle.isEmpty()) {
        return true;
    }
    return profile.name.contains(needle, Qt::CaseInsensitive)
        || profile.source.contains(needle, Qt::CaseInsensitive);
}

bool squigMatches(const OnlinePresetProfile &profile, const QString &needle)
{
    if (needle.isEmpty()) {
        return true;
    }
    return profile.name.contains(needle, Qt::CaseInsensitive)
        || profile.source.contains(needle, Qt::CaseInsensitive)
        || profile.dbType.contains(needle, Qt::CaseInsensitive)
        || profile.fileBase.contains(needle, Qt::CaseInsensitive);
}

bool opraMatches(const OpraProfile &profile, const QString &needle)
{
    if (needle.isEmpty()) {
        return true;
    }
    return profile.name.contains(needle, Qt::CaseInsensitive)
        || profile.author.contains(needle, Qt::CaseInsensitive)
        || profile.details.contains(needle, Qt::CaseInsensitive)
        || profile.id.contains(needle, Qt::CaseInsensitive);
}

OnlinePresetProfile autoEqToOnline(const AutoEqProfile &autoEq)
{
    OnlinePresetProfile profile;
    profile.kind = OnlinePresetKind::AutoEq;
    profile.name = autoEq.name;
    profile.source = autoEq.source;
    profile.relativePath = autoEq.relativePath;
    return profile;
}
} // namespace

OnlinePresetsDialog::OnlinePresetsDialog(PresetStore *store, QWidget *parent)
    : QDialog(parent)
    , m_store(store)
    , m_nam(new QNetworkAccessManager(this))
{
    setWindowTitle(QStringLiteral("Online presets"));
    resize(620, 680);

    auto *layout = new QVBoxLayout(this);

    auto *filterRow = new QHBoxLayout();
    filterRow->addWidget(new QLabel(QStringLiteral("Source"), this));
    m_sourceCombo = new QComboBox(this);
    m_sourceCombo->addItem(QStringLiteral("All"), static_cast<int>(SourceFilter::All));
    m_sourceCombo->addItem(QStringLiteral("AutoEQ"), static_cast<int>(SourceFilter::AutoEq));
    m_sourceCombo->addItem(QStringLiteral("Squiglink"), static_cast<int>(SourceFilter::Squiglink));
    m_sourceCombo->addItem(QStringLiteral("OPRA"), static_cast<int>(SourceFilter::Opra));
    filterRow->addWidget(m_sourceCombo, 1);
    layout->addLayout(filterRow);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(QStringLiteral("Search headphones, brand, or source…"));
    m_searchEdit->setClearButtonEnabled(true);
    layout->addWidget(m_searchEdit);

    m_listWidget = new QListWidget(this);
    m_listWidget->setUniformItemSizes(true);
    m_listWidget->setAlternatingRowColors(true);
    layout->addWidget(m_listWidget, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    m_creditLabel = new QLabel(
        QStringLiteral(
            "AutoEQ · Squiglink · OPRA (opra-project / Roon Labs, CC BY-SA). OPRA imports use "
            "parametric EQ bands from the open database. Squiglink FR imports target flat. "
            "All imports are saved as Advanced presets."),
        this);
    m_creditLabel->setWordWrap(true);
    m_creditLabel->setStyleSheet(QStringLiteral("color: #ffffff;"));
    layout->addWidget(m_creditLabel);

    auto *buttonRow = new QHBoxLayout();
    m_refreshButton = new QPushButton(QStringLiteral("Refresh"), this);
    m_refreshButton->setToolTip(QStringLiteral("Reload the lists from the local cache."));
    m_redownloadButton = new QPushButton(QStringLiteral("Redownload presets"), this);
    m_redownloadButton->setToolTip(
        QStringLiteral("Download the latest AutoEQ, OPRA, and Squiglink catalogs."));
    m_importButton = new QPushButton(QStringLiteral("Import"), this);
    m_importButton->setEnabled(false);
    m_importButton->setDefault(true);
    m_closeButton = new QPushButton(QStringLiteral("Close"), this);
    buttonRow->addWidget(m_refreshButton);
    buttonRow->addWidget(m_redownloadButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_importButton);
    buttonRow->addWidget(m_closeButton);
    layout->addLayout(buttonRow);

    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(150);

    connect(m_searchEdit, &QLineEdit::textChanged, this, &OnlinePresetsDialog::onSearchTextChanged);
    connect(m_searchDebounce, &QTimer::timeout, this, &OnlinePresetsDialog::applyPendingSearch);
    connect(m_sourceCombo, &QComboBox::currentIndexChanged, this, &OnlinePresetsDialog::onSourceFilterChanged);
    connect(m_listWidget, &QListWidget::itemSelectionChanged, this, &OnlinePresetsDialog::onSelectionChanged);
    connect(m_listWidget, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        onImportClicked();
    });
    connect(m_listWidget->verticalScrollBar(), &QScrollBar::valueChanged, this,
            &OnlinePresetsDialog::onListScrollChanged);
    connect(m_refreshButton, &QPushButton::clicked, this, &OnlinePresetsDialog::onRefreshClicked);
    connect(m_redownloadButton, &QPushButton::clicked, this, &OnlinePresetsDialog::onRedownloadClicked);
    connect(m_importButton, &QPushButton::clicked, this, &OnlinePresetsDialog::onImportClicked);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::reject);

    loadCaches();
    if (m_autoEqProfiles.isEmpty() && m_opraProfiles.isEmpty() && m_squigProfiles.isEmpty()) {
        startRefresh();
    }
}

OnlinePresetsDialog::SourceFilter OnlinePresetsDialog::currentFilter() const
{
    return static_cast<SourceFilter>(m_sourceCombo->currentData().toInt());
}

int OnlinePresetsDialog::matchingCount() const
{
    const SourceFilter filter = currentFilter();
    const QString needle = m_searchEdit->text().trimmed();
    int count = 0;

    if (filter == SourceFilter::All || filter == SourceFilter::AutoEq) {
        if (needle.isEmpty()) {
            count += m_autoEqProfiles.size();
        } else {
            for (const AutoEqProfile &profile : m_autoEqProfiles) {
                if (autoEqMatches(profile, needle)) {
                    ++count;
                }
            }
        }
    }
    if (filter == SourceFilter::All || filter == SourceFilter::Squiglink) {
        if (needle.isEmpty()) {
            count += m_squigProfiles.size();
        } else {
            for (const OnlinePresetProfile &profile : m_squigProfiles) {
                if (squigMatches(profile, needle)) {
                    ++count;
                }
            }
        }
    }
    if (filter == SourceFilter::All || filter == SourceFilter::Opra) {
        if (needle.isEmpty()) {
            count += m_opraProfiles.size();
        } else {
            for (const OpraProfile &profile : m_opraProfiles) {
                if (opraMatches(profile, needle)) {
                    ++count;
                }
            }
        }
    }
    return count;
}

void OnlinePresetsDialog::onSearchTextChanged(const QString &)
{
    m_searchDebounce->start();
}

void OnlinePresetsDialog::applyPendingSearch()
{
    rebuildFilteredProfiles();
}

void OnlinePresetsDialog::onSourceFilterChanged(int)
{
    m_searchDebounce->stop();
    rebuildFilteredProfiles();
}

void OnlinePresetsDialog::onListScrollChanged(int value)
{
    if (m_loadingPage || !m_hasMore) {
        return;
    }
    QScrollBar *bar = m_listWidget->verticalScrollBar();
    if (!bar || bar->maximum() <= 0) {
        return;
    }
    if (value < bar->maximum() - 24) {
        return;
    }
    appendNextPage();
}

void OnlinePresetsDialog::onRefreshClicked()
{
    loadCaches();
}

void OnlinePresetsDialog::onRedownloadClicked()
{
    startRefresh();
}

void OnlinePresetsDialog::onImportClicked()
{
    if (m_busy || !m_store) {
        return;
    }

    m_pendingImport = selectedProfile();
    if (m_pendingImport.name.isEmpty()) {
        setStatus(QStringLiteral("Select a profile to import."), true);
        return;
    }

    if (m_pendingImport.kind == OnlinePresetKind::AutoEq) {
        AutoEqProfile autoEq;
        autoEq.name = m_pendingImport.name;
        autoEq.source = m_pendingImport.source;
        autoEq.relativePath = m_pendingImport.relativePath;
        setBusy(true, QStringLiteral("Downloading ParametricEQ for %1…").arg(autoEq.name));
        getUrl(QUrl(AutoEqCatalog::parametricEqUrl(autoEq)), JobKind::AutoEqParametric);
        return;
    }

    if (m_pendingImport.kind == OnlinePresetKind::Opra) {
        setBusy(true, QStringLiteral("Importing OPRA profile for %1…").arg(m_pendingImport.name));
        QString errorMessage;
        if (!finishOpraImport(m_pendingImport, &errorMessage)) {
            setBusy(false);
            setStatus(errorMessage, true);
            return;
        }
        setBusy(false);
        setStatus(QStringLiteral("Imported “%1”.").arg(m_importedPreset.name));
        accept();
        return;
    }

    m_squigChannelAttempt = 0;
    const QString suffix = squigChannelSuffixes().at(m_squigChannelAttempt);
    setBusy(true, QStringLiteral("Downloading Squiglink measurement for %1…").arg(m_pendingImport.name));
    getUrl(QUrl(SquiglinkCatalog::measurementUrl(m_pendingImport, suffix)), JobKind::SquigMeasurement);
}

void OnlinePresetsDialog::onSelectionChanged()
{
    m_importButton->setEnabled(!m_busy && m_listWidget->currentItem() != nullptr);
}

void OnlinePresetsDialog::abortActive()
{
    if (!m_activeReply) {
        return;
    }
    m_activeReply->disconnect(this);
    m_activeReply->abort();
    m_activeReply->deleteLater();
    m_activeReply = nullptr;
    m_job = JobKind::None;
}

void OnlinePresetsDialog::getUrl(const QUrl &url, JobKind kind)
{
    abortActive();
    m_job = kind;
    // fromEncoded avoids QUrl re-parsing an already percent-encoded path.
    const QUrl requestUrl = QUrl::fromEncoded(url.toString(QUrl::FullyEncoded).toUtf8());
    QNetworkRequest netRequest(requestUrl.isValid() ? requestUrl : url);
    netRequest.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
    m_activeReply = m_nam->get(netRequest);
    connect(m_activeReply, &QNetworkReply::finished, this, &OnlinePresetsDialog::onNetworkFinished);
}

void OnlinePresetsDialog::onNetworkFinished()
{
    QNetworkReply *reply = m_activeReply;
    m_activeReply = nullptr;
    const JobKind job = m_job;
    m_job = JobKind::None;
    if (!reply) {
        setBusy(false);
        return;
    }
    reply->deleteLater();

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool ok = reply->error() == QNetworkReply::NoError && statusCode < 400;
    const QByteArray body = ok ? reply->readAll() : QByteArray();
    const QString error = reply->errorString();

    switch (job) {
    case JobKind::AutoEqIndex:
        handleAutoEqIndex(body, ok, error);
        break;
    case JobKind::OpraDatabase:
        handleOpraDatabase(body, ok, error);
        break;
    case JobKind::SquigSites:
        handleSquigSites(body, ok, error);
        break;
    case JobKind::SquigPhoneBook:
        handleSquigPhoneBook(body, ok, error);
        break;
    case JobKind::AutoEqParametric:
        handleAutoEqEqFile(body, ok, error, true);
        break;
    case JobKind::AutoEqGraphic:
        handleAutoEqEqFile(body, ok, error, false);
        break;
    case JobKind::SquigMeasurement:
        handleSquigMeasurement(body, ok, error);
        break;
    case JobKind::None:
        setBusy(false);
        break;
    }
}

void OnlinePresetsDialog::handleAutoEqIndex(const QByteArray &body, bool ok, const QString &error)
{
    if (!ok) {
        setStatus(QStringLiteral("AutoEQ index refresh failed: %1").arg(error), true);
    } else {
        QString parseError;
        QVector<AutoEqProfile> parsed;
        if (!AutoEqCatalog::parseIndexMarkdown(body, &parsed, &parseError)) {
            setStatus(parseError, true);
        } else {
            m_autoEqProfiles = std::move(parsed);
            AutoEqCatalog::saveCache(m_autoEqProfiles);
        }
    }

    // Continue with OPRA, then Squiglink.
    startOpraDownload();
}

void OnlinePresetsDialog::startOpraDownload()
{
    setBusy(true, QStringLiteral("Downloading OPRA database…"));
    getUrl(QUrl(OpraCatalog::databaseUrl()), JobKind::OpraDatabase);
}

void OnlinePresetsDialog::handleOpraDatabase(const QByteArray &body, bool ok, const QString &error)
{
    if (!ok) {
        setStatus(QStringLiteral("OPRA download failed: %1").arg(error), true);
    } else {
        QString parseError;
        QVector<OpraProfile> parsed;
        if (!OpraCatalog::parseDatabaseJsonl(body, &parsed, &parseError)) {
            setStatus(parseError, true);
        } else {
            m_opraProfiles = std::move(parsed);
            OpraCatalog::saveCache(m_opraProfiles);
        }
    }

    startSquigRefresh();
}

void OnlinePresetsDialog::startSquigRefresh()
{
    setBusy(true, QStringLiteral("Downloading Squiglink site list…"));
    getUrl(QUrl(SquiglinkCatalog::sitesUrl()), JobKind::SquigSites);
}

void OnlinePresetsDialog::handleSquigSites(const QByteArray &body, bool ok, const QString &error)
{
    m_squigQueue.clear();
    m_squigQueueIndex = 0;
    m_squigOkCount = 0;
    m_squigFailCount = 0;
    m_squigProfiles.clear();

    if (ok) {
        QString parseError;
        if (!SquiglinkCatalog::parseSitesJson(body, &m_squigQueue, &parseError)) {
            setStatus(parseError, true);
        }
    } else {
        setStatus(QStringLiteral("Squiglink sites list failed: %1").arg(error), true);
    }

    m_squigQueue += SquiglinkCatalog::extraDatabases();
    if (m_squigQueue.isEmpty()) {
        setBusy(false);
        rebuildFilteredProfiles();
        setStatus(QStringLiteral("No Squiglink databases available."), true);
        return;
    }

    continueSquigQueue();
}

void OnlinePresetsDialog::continueSquigQueue()
{
    if (m_squigQueueIndex >= m_squigQueue.size()) {
        SquiglinkCatalog::saveCache(m_squigProfiles);
        setBusy(false);
        rebuildFilteredProfiles();
        setStatus(QStringLiteral("Loaded AutoEQ %1 · OPRA %2 · Squiglink %3 (ok %4 / fail %5)")
                      .arg(m_autoEqProfiles.size())
                      .arg(m_opraProfiles.size())
                      .arg(m_squigProfiles.size())
                      .arg(m_squigOkCount)
                      .arg(m_squigFailCount));
        return;
    }

    const SquigSiteDb &db = m_squigQueue.at(m_squigQueueIndex);
    setBusy(true,
            QStringLiteral("Squiglink %1/%2: %3 (%4)…")
                .arg(m_squigQueueIndex + 1)
                .arg(m_squigQueue.size())
                .arg(db.siteName, db.dbType));
    getUrl(QUrl(SquiglinkCatalog::phoneBookUrl(db)), JobKind::SquigPhoneBook);
}

void OnlinePresetsDialog::handleSquigPhoneBook(const QByteArray &body, bool ok, const QString &error)
{
    if (m_squigQueueIndex < m_squigQueue.size()) {
        const SquigSiteDb db = m_squigQueue.at(m_squigQueueIndex);
        if (ok) {
            QString parseError;
            if (SquiglinkCatalog::parsePhoneBook(body, db, &m_squigProfiles, &parseError)) {
                ++m_squigOkCount;
            } else {
                ++m_squigFailCount;
            }
        } else {
            Q_UNUSED(error);
            ++m_squigFailCount;
        }
        ++m_squigQueueIndex;
    }
    continueSquigQueue();
}

void OnlinePresetsDialog::handleAutoEqEqFile(const QByteArray &body, bool ok, const QString &error, bool parametric)
{
    AutoEqProfile autoEq;
    autoEq.name = m_pendingImport.name;
    autoEq.source = m_pendingImport.source;
    autoEq.relativePath = m_pendingImport.relativePath;

    if (!ok) {
        if (parametric) {
            setStatus(QStringLiteral("ParametricEQ missing — trying GraphicEQ…"));
            getUrl(QUrl(AutoEqCatalog::graphicEqUrl(autoEq)), JobKind::AutoEqGraphic);
            return;
        }
        setBusy(false);
        setStatus(QStringLiteral("Could not download AutoEQ file: %1").arg(error), true);
        return;
    }

    QString errorMessage;
    if (!finishAutoEqImport(autoEq, body, parametric, &errorMessage)) {
        if (parametric) {
            setStatus(QStringLiteral("%1 — falling back to GraphicEQ…").arg(errorMessage));
            getUrl(QUrl(AutoEqCatalog::graphicEqUrl(autoEq)), JobKind::AutoEqGraphic);
            return;
        }
        setBusy(false);
        setStatus(errorMessage, true);
        return;
    }

    setBusy(false);
    setStatus(QStringLiteral("Imported “%1”.").arg(m_importedPreset.name));
    accept();
}

void OnlinePresetsDialog::handleSquigMeasurement(const QByteArray &body, bool ok, const QString &error)
{
    if (!ok) {
        ++m_squigChannelAttempt;
        const QStringList suffixes = squigChannelSuffixes();
        if (m_squigChannelAttempt < suffixes.size()) {
            const QString suffix = suffixes.at(m_squigChannelAttempt);
            setStatus(QStringLiteral("Retrying measurement channel…"));
            getUrl(QUrl(SquiglinkCatalog::measurementUrl(m_pendingImport, suffix)),
                   JobKind::SquigMeasurement);
            return;
        }
        setBusy(false);
        setStatus(
            QStringLiteral(
                "Could not download Squiglink measurement for “%1” (file not found on host).\n"
                "Try another Squiglink site, or use AutoEQ for this headphone.")
                .arg(m_pendingImport.name),
            true);
        return;
    }

    QString errorMessage;
    if (!finishSquigImport(m_pendingImport, body, &errorMessage)) {
        setBusy(false);
        setStatus(errorMessage, true);
        return;
    }

    setBusy(false);
    setStatus(QStringLiteral("Imported “%1”.").arg(m_importedPreset.name));
    accept();
}

bool OnlinePresetsDialog::finishAutoEqImport(const AutoEqProfile &profile,
                                             const QByteArray &body,
                                             bool parametric,
                                             QString *errorMessage)
{
    EqState state;
    if (parametric) {
        if (!AutoEqCatalog::parseParametricEqToState(QString::fromUtf8(body), &state, errorMessage)) {
            return false;
        }
    } else {
        std::array<float, EqProcessor::kBandCount> gains{};
        if (!AutoEqCatalog::parseGraphicEqToBands(QString::fromUtf8(body), &gains, errorMessage)) {
            return false;
        }
        state = EqResponse::simpleToAdvanced(gains);
        state.advanced = true;
    }

    const QString presetName = profile.source.isEmpty()
                                   ? QStringLiteral("%1 (AutoEQ)").arg(profile.name)
                                   : QStringLiteral("%1 (%2 · AutoEQ)").arg(profile.name, profile.source);

    EqPreset created;
    if (!m_store->addUserPreset(presetName, state, &created)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not save preset to disk.");
        }
        return false;
    }
    m_importedPreset = created;
    m_didImport = true;
    return true;
}

bool OnlinePresetsDialog::finishSquigImport(const OnlinePresetProfile &profile,
                                            const QByteArray &body,
                                            QString *errorMessage)
{
    EqState state;
    if (!SquiglinkCatalog::parseMeasurementToAdvancedEq(QString::fromUtf8(body), &state, errorMessage)) {
        return false;
    }

    const QString presetName =
        QStringLiteral("%1 (%2 · Squiglink)").arg(profile.name, profile.source);

    EqPreset created;
    if (!m_store->addUserPreset(presetName, state, &created)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not save preset to disk.");
        }
        return false;
    }
    m_importedPreset = created;
    m_didImport = true;
    return true;
}

const OpraProfile *OnlinePresetsDialog::findOpraProfile(const QString &id) const
{
    for (const OpraProfile &profile : m_opraProfiles) {
        if (profile.id == id) {
            return &profile;
        }
    }
    return nullptr;
}

bool OnlinePresetsDialog::finishOpraImport(const OnlinePresetProfile &profile, QString *errorMessage)
{
    const OpraProfile *opra = findOpraProfile(profile.relativePath);
    if (!opra) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("OPRA profile data is missing from the local cache.");
        }
        return false;
    }

    EqState state;
    if (!OpraCatalog::toEqState(*opra, &state, errorMessage)) {
        return false;
    }

    const QString presetName = opra->author.isEmpty()
                                   ? QStringLiteral("%1 (OPRA)").arg(opra->name)
                                   : QStringLiteral("%1 (%2 · OPRA)").arg(opra->name, opra->author);

    EqPreset created;
    if (!m_store->addUserPreset(presetName, state, &created)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not save preset to disk.");
        }
        return false;
    }
    m_importedPreset = created;
    m_didImport = true;
    return true;
}

void OnlinePresetsDialog::setBusy(bool busy, const QString &statusText)
{
    m_busy = busy;
    m_searchEdit->setEnabled(!busy);
    m_sourceCombo->setEnabled(!busy);
    m_listWidget->setEnabled(!busy);
    m_refreshButton->setEnabled(!busy);
    m_redownloadButton->setEnabled(!busy);
    m_importButton->setEnabled(!busy && m_listWidget->currentItem() != nullptr);
    if (!statusText.isEmpty()) {
        setStatus(statusText, false);
    }
}

void OnlinePresetsDialog::setStatus(const QString &text, bool isError)
{
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(isError ? QStringLiteral("color: #b00020;") : QStringLiteral("color: #ffffff;"));
}

void OnlinePresetsDialog::rebuildFilteredProfiles()
{
    m_matchTotal = matchingCount();
    resetVisiblePage();
}

void OnlinePresetsDialog::resetVisiblePage()
{
    m_loadingPage = true;
    m_listWidget->clear();
    m_visibleCount = 0;
    m_scanAutoEq = 0;
    m_scanSquig = 0;
    m_scanOpra = 0;
    m_hasMore = true;
    m_loadingPage = false;
    appendNextPage();
    onSelectionChanged();
}

void OnlinePresetsDialog::appendNextPage()
{
    if (!m_hasMore) {
        updateListStatus();
        return;
    }

    m_loadingPage = true;
    const SourceFilter filter = currentFilter();
    const QString needle = m_searchEdit->text().trimmed();
    const bool includeAutoEq = filter == SourceFilter::All || filter == SourceFilter::AutoEq;
    const bool includeSquig = filter == SourceFilter::All || filter == SourceFilter::Squiglink;
    const bool includeOpra = filter == SourceFilter::All || filter == SourceFilter::Opra;
    int added = 0;

    if (includeAutoEq) {
        while (m_scanAutoEq < m_autoEqProfiles.size() && added < kPageSize) {
            const AutoEqProfile &profile = m_autoEqProfiles.at(m_scanAutoEq);
            ++m_scanAutoEq;
            if (!autoEqMatches(profile, needle)) {
                continue;
            }
            addProfileItem(autoEqToOnline(profile));
            ++added;
        }
    } else {
        m_scanAutoEq = m_autoEqProfiles.size();
    }

    if (includeSquig) {
        while (m_scanSquig < m_squigProfiles.size() && added < kPageSize) {
            const OnlinePresetProfile &profile = m_squigProfiles.at(m_scanSquig);
            ++m_scanSquig;
            if (!squigMatches(profile, needle)) {
                continue;
            }
            addProfileItem(profile);
            ++added;
        }
    } else {
        m_scanSquig = m_squigProfiles.size();
    }

    if (includeOpra) {
        while (m_scanOpra < m_opraProfiles.size() && added < kPageSize) {
            const OpraProfile &profile = m_opraProfiles.at(m_scanOpra);
            ++m_scanOpra;
            if (!opraMatches(profile, needle)) {
                continue;
            }
            addProfileItem(OpraCatalog::toOnlineProfile(profile));
            ++added;
        }
    } else {
        m_scanOpra = m_opraProfiles.size();
    }

    m_visibleCount += added;
    m_hasMore = added == kPageSize
        && ((includeAutoEq && m_scanAutoEq < m_autoEqProfiles.size())
            || (includeSquig && m_scanSquig < m_squigProfiles.size())
            || (includeOpra && m_scanOpra < m_opraProfiles.size()));
    m_loadingPage = false;
    updateListStatus();
}

void OnlinePresetsDialog::addProfileItem(const OnlinePresetProfile &profile)
{
    QString kindTag;
    switch (profile.kind) {
    case OnlinePresetKind::Opra:
        kindTag = QStringLiteral("OPRA");
        break;
    case OnlinePresetKind::Squiglink:
        kindTag = QStringLiteral("Squiglink");
        break;
    case OnlinePresetKind::AutoEq:
    default:
        kindTag = QStringLiteral("AutoEQ");
        break;
    }
    QString label = QStringLiteral("[%1] %2").arg(kindTag, profile.name);
    if (!profile.source.isEmpty()) {
        label += QStringLiteral(" — %1").arg(profile.source);
    }
    if (profile.kind == OnlinePresetKind::Squiglink && !profile.dbType.isEmpty()) {
        label += QStringLiteral(" · %1").arg(profile.dbType);
    }
    auto *item = new QListWidgetItem(label);
    item->setData(kRoleKind, static_cast<int>(profile.kind));
    item->setData(kRoleName, profile.name);
    item->setData(kRoleSource, profile.source);
    item->setData(kRolePath, profile.relativePath);
    item->setData(kRoleDataUrl, profile.dataBaseUrl);
    item->setData(kRoleFileBase, profile.fileBase);
    item->setData(kRoleDbType, profile.dbType);
    m_listWidget->addItem(item);
}

void OnlinePresetsDialog::updateListStatus()
{
    if (m_busy) {
        return;
    }
    if (m_matchTotal <= 0) {
        setStatus(QStringLiteral("No matching profiles (AutoEQ %1 · OPRA %2 · Squiglink %3)")
                      .arg(m_autoEqProfiles.size())
                      .arg(m_opraProfiles.size())
                      .arg(m_squigProfiles.size()));
        return;
    }
    setStatus(QStringLiteral("Showing %1 of %2 profiles (AutoEQ %3 · OPRA %4 · Squiglink %5)")
                  .arg(m_visibleCount)
                  .arg(m_matchTotal)
                  .arg(m_autoEqProfiles.size())
                  .arg(m_opraProfiles.size())
                  .arg(m_squigProfiles.size()));
}

void OnlinePresetsDialog::loadCaches()
{
    AutoEqCatalog::loadCache(&m_autoEqProfiles);
    OpraCatalog::loadCache(&m_opraProfiles);
    SquiglinkCatalog::loadCache(&m_squigProfiles);
    rebuildFilteredProfiles();
}

void OnlinePresetsDialog::startRefresh()
{
    abortActive();
    m_autoEqProfiles.clear();
    m_opraProfiles.clear();
    m_squigProfiles.clear();
    m_squigQueue.clear();
    m_squigQueueIndex = 0;
    m_squigOkCount = 0;
    m_squigFailCount = 0;
    AutoEqCatalog::clearCache();
    OpraCatalog::clearCache();
    SquiglinkCatalog::clearCache();
    rebuildFilteredProfiles();
    setBusy(true, QStringLiteral("Downloading AutoEQ profile index…"));
    getUrl(QUrl(AutoEqCatalog::indexMarkdownUrl()), JobKind::AutoEqIndex);
}

OnlinePresetProfile OnlinePresetsDialog::selectedProfile() const
{
    OnlinePresetProfile profile;
    const QListWidgetItem *item = m_listWidget->currentItem();
    if (!item) {
        return profile;
    }
    profile.kind = static_cast<OnlinePresetKind>(item->data(kRoleKind).toInt());
    profile.name = item->data(kRoleName).toString();
    profile.source = item->data(kRoleSource).toString();
    profile.relativePath = item->data(kRolePath).toString();
    profile.dataBaseUrl = item->data(kRoleDataUrl).toString();
    profile.fileBase = item->data(kRoleFileBase).toString();
    profile.dbType = item->data(kRoleDbType).toString();
    return profile;
}
