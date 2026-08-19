#include "soundmoddialog.h"



#include "audio/processloopbackcapture.h"

#include "soundmods/assetscanner.h"

#include "soundmods/gameidentity.h"

#include "soundmods/gamerootresolver.h"

#include "soundmods/patchengine.h"

#include "soundmods/soundmodstore.h"



#include <QCheckBox>

#include <QDir>

#include <QFileDialog>

#include <QFileInfo>

#include <QGridLayout>

#include <QGroupBox>

#include <QHBoxLayout>

#include <QHeaderView>

#include <QLabel>

#include <QLineEdit>

#include <QMessageBox>

#include <QProgressBar>

#include <QPushButton>

#include <QSettings>

#include <QSlider>

#include <QTreeWidget>

#include <QTreeWidgetItem>

#include <QVBoxLayout>

#include <QtConcurrent/QtConcurrent>



#include <cmath>



namespace {



enum ItemRole {

    RelativePathRole = Qt::UserRole,

    ItemKindRole = Qt::UserRole + 1,

};



constexpr auto kFileKind = "file";

constexpr auto kFolderKind = "folder";



QString statusText(SoundAssetStatus status)

{

    switch (status) {

    case SoundAssetStatus::Modified:

        return QStringLiteral("Modified");

    case SoundAssetStatus::Unsupported:

        return QStringLiteral("Unsupported");

    case SoundAssetStatus::Pending:

        return QStringLiteral("Pending");

    case SoundAssetStatus::Error:

        return QStringLiteral("Error");

    case SoundAssetStatus::Original:

    default:

        return QStringLiteral("Original");

    }

}



QString parentFolderPath(const QString &relativePath)

{

    const int slashIndex = relativePath.lastIndexOf(QLatin1Char('/'));

    if (slashIndex <= 0) {

        return QString();

    }

    return relativePath.left(slashIndex);

}



bool matchesFilter(const SoundAssetEntry &asset, const QString &filter)

{

    return asset.displayName.contains(filter, Qt::CaseInsensitive)

           || asset.relativePath.contains(filter, Qt::CaseInsensitive);

}



bool pruneEmptyFolders(QTreeWidgetItem *item)

{

    if (item->data(0, ItemKindRole).toString() == QLatin1String(kFileKind)) {

        return false;

    }



    for (int row = item->childCount() - 1; row >= 0; --row) {

        if (pruneEmptyFolders(item->child(row))) {

            delete item->takeChild(row);

        }

    }



    return item->childCount() == 0;

}



} // namespace



SoundModDialog::SoundModDialog(unsigned long processId, const QString &displayName, QWidget *parent)

    : QDialog(parent)

    , m_processId(processId)

    , m_game(GameIdentityUtil::fromProcess(processId, displayName))

{

    m_game.scanRoot = GameRootResolver::resolveScanRoot(m_game.executablePath);

    setWindowTitle(QStringLiteral("Manage sound files - %1").arg(displayName));

    resize(920, 620);

    buildUi();

    showFirstRunWarningIfNeeded();



    if (!m_game.scanRoot.isEmpty()) {

        m_scanRootEdit->setText(m_game.scanRoot);

        onScan();

    }

}



void SoundModDialog::buildUi()

{

    auto *layout = new QVBoxLayout(this);



    auto *header = new QGroupBox(QStringLiteral("Game folder"), this);

    auto *headerLayout = new QGridLayout(header);

    headerLayout->addWidget(new QLabel(QStringLiteral("Executable:"), header), 0, 0);

    headerLayout->addWidget(new QLabel(m_game.executablePath.isEmpty() ? QStringLiteral("(unknown)")

                                                                       : m_game.executablePath),

                            0,

                            1);

    headerLayout->addWidget(new QLabel(QStringLiteral("Scan root:"), header), 1, 0);

    m_scanRootEdit = new QLineEdit(m_game.scanRoot, header);

    headerLayout->addWidget(m_scanRootEdit, 1, 1);

    auto *browseButton = new QPushButton(QStringLiteral("Browse…"), header);

    auto *scanButton = new QPushButton(QStringLiteral("Scan"), header);

    auto *browseScanRow = new QHBoxLayout();

    browseScanRow->addWidget(browseButton);

    browseScanRow->addWidget(scanButton);

    headerLayout->addLayout(browseScanRow, 1, 2);

    layout->addWidget(header);



    m_searchEdit = new QLineEdit(this);

    m_searchEdit->setPlaceholderText(QStringLiteral("Search folders and sound files (e.g. footstep)"));

    layout->addWidget(m_searchEdit);



    m_scanSummaryLabel = new QLabel(this);

    m_scanSummaryLabel->setWordWrap(true);

    layout->addWidget(m_scanSummaryLabel);



    m_tree = new QTreeWidget(this);

    m_tree->setColumnCount(4);

    m_tree->setHeaderLabels(

        {QStringLiteral("Name"), QStringLiteral("Format"), QStringLiteral("Gain (dB)"), QStringLiteral("Status")});

    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);

    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);

    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);

    m_tree->setRootIsDecorated(true);

    m_tree->header()->setStretchLastSection(true);

    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);

    layout->addWidget(m_tree, 1);



    auto *selectedGroup = new QGroupBox(QStringLiteral("Selected file"), this);

    auto *selectedLayout = new QHBoxLayout(selectedGroup);

    m_enabledCheckBox = new QCheckBox(QStringLiteral("Enable mod"), selectedGroup);

    m_enabledCheckBox->setChecked(true);

    m_gainSlider = new QSlider(Qt::Horizontal, selectedGroup);

    m_gainSlider->setRange(-240, 240);

    m_gainSlider->setValue(0);

    m_gainValueLabel = new QLabel(QStringLiteral("0.0 dB"), selectedGroup);

    selectedLayout->addWidget(m_enabledCheckBox);

    selectedLayout->addWidget(new QLabel(QStringLiteral("Gain"), selectedGroup));

    selectedLayout->addWidget(m_gainSlider, 1);

    selectedLayout->addWidget(m_gainValueLabel);

    layout->addWidget(selectedGroup);



    m_progressBar = new QProgressBar(this);

    m_progressBar->setRange(0, 0);

    m_progressBar->setVisible(false);

    layout->addWidget(m_progressBar);



    auto *buttonsRow = new QHBoxLayout();

    m_restoreAllButton = new QPushButton(QStringLiteral("Restore all defaults"), this);

    m_restoreSelectedButton = new QPushButton(QStringLiteral("Restore selected"), this);

    m_applyButton = new QPushButton(QStringLiteral("Apply"), this);

    auto *closeButton = new QPushButton(QStringLiteral("Close"), this);

    buttonsRow->addWidget(m_restoreAllButton);

    buttonsRow->addWidget(m_restoreSelectedButton);

    buttonsRow->addStretch();

    buttonsRow->addWidget(m_applyButton);

    buttonsRow->addWidget(closeButton);

    layout->addLayout(buttonsRow);



    connect(browseButton, &QPushButton::clicked, this, &SoundModDialog::onBrowseRoot);

    connect(scanButton, &QPushButton::clicked, this, &SoundModDialog::onScan);

    connect(m_applyButton, &QPushButton::clicked, this, &SoundModDialog::onApply);

    connect(m_restoreAllButton, &QPushButton::clicked, this, &SoundModDialog::onRestoreAll);

    connect(m_restoreSelectedButton, &QPushButton::clicked, this, &SoundModDialog::onRestoreSelected);

    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &SoundModDialog::onSelectionChanged);

    connect(m_gainSlider, &QSlider::valueChanged, this, &SoundModDialog::onGainSliderChanged);

    connect(m_enabledCheckBox, &QCheckBox::toggled, this, [this](bool) { onGainSliderChanged(m_gainSlider->value()); });

    connect(m_searchEdit, &QLineEdit::textChanged, this, &SoundModDialog::onSearchChanged);



    updateScanSummary();

}



void SoundModDialog::showFirstRunWarningIfNeeded()

{

    QSettings settings;

    if (settings.value(QStringLiteral("soundMods/warnedFirstRun"), false).toBool()) {

        return;

    }



    QMessageBox::warning(this,

                         QStringLiteral("Sound file modding"),

                         QStringLiteral("Modifying game files can break after updates and may violate online game "

                                        "terms of service or trigger anti-cheat systems.\n\n"

                                        "Changes are written to disk when you press Apply and take effect the next "

                                        "time you launch the app."));

    settings.setValue(QStringLiteral("soundMods/warnedFirstRun"), true);

}



void SoundModDialog::mergeProfileIntoAssets()

{

    SoundModStore store;

    QVector<SoundModProfileEntry> profile;

    store.loadProfile(m_game.id, &profile);



    for (SoundAssetEntry &asset : m_assets) {

        for (const SoundModProfileEntry &entry : profile) {

            if (entry.relativePath == asset.relativePath) {

                asset.gainDb = entry.gainDb;

                asset.enabled = entry.enabled;

                if (asset.patchSupport != SoundPatchSupport::Unsupported && std::fabs(entry.gainDb) > 0.01f) {

                    asset.status = SoundAssetStatus::Modified;

                }

                break;

            }

        }

    }

}



QTreeWidgetItem *SoundModDialog::ensureFolderNode(const QString &relativeFolderPath)

{

    const QString normalized = QDir::fromNativeSeparators(relativeFolderPath);

    if (m_folderNodes.contains(normalized)) {

        return m_folderNodes.value(normalized);

    }



    const int slashIndex = normalized.lastIndexOf(QLatin1Char('/'));

    const QString parentPath = slashIndex >= 0 ? normalized.left(slashIndex) : QString();

    const QString folderName = slashIndex >= 0 ? normalized.mid(slashIndex + 1) : normalized;



    QTreeWidgetItem *parentItem = slashIndex >= 0 ? ensureFolderNode(parentPath) : m_treeRoot;

    auto *folderItem = new QTreeWidgetItem(parentItem, {folderName, QString(), QString(), QString()});

    folderItem->setData(0, RelativePathRole, normalized);

    folderItem->setData(0, ItemKindRole, QString::fromLatin1(kFolderKind));

    folderItem->setExpanded(true);

    m_folderNodes.insert(normalized, folderItem);

    return folderItem;

}



void SoundModDialog::populateTree()

{

    m_tree->clear();

    m_folderNodes.clear();

    m_treeRoot = nullptr;



    const QString scanRoot = m_scanRootEdit->text().trimmed();

    if (scanRoot.isEmpty()) {

        return;

    }



    const QString rootLabel = QFileInfo(scanRoot).fileName();

    m_treeRoot = new QTreeWidgetItem(m_tree,

                                     {rootLabel.isEmpty() ? scanRoot : rootLabel, QString(), QString(), QString()});

    m_treeRoot->setData(0, RelativePathRole, QString());

    m_treeRoot->setData(0, ItemKindRole, QString::fromLatin1(kFolderKind));

    m_treeRoot->setExpanded(true);

    m_folderNodes.insert(QString(), m_treeRoot);



    const QString filter = m_searchEdit ? m_searchEdit->text().trimmed() : QString();



    for (const QString &folderPath : m_folderPaths) {

        if (folderPath.isEmpty()) {

            continue;

        }

        if (!filter.isEmpty() && !folderPath.contains(filter, Qt::CaseInsensitive)) {

            continue;

        }

        ensureFolderNode(folderPath);

    }



    for (const SoundAssetEntry &asset : m_assets) {

        if (!filter.isEmpty() && !matchesFilter(asset, filter)) {

            continue;

        }



        const QString parentPath = parentFolderPath(asset.relativePath);

        QTreeWidgetItem *parentItem = parentPath.isEmpty() ? m_treeRoot : ensureFolderNode(parentPath);



        auto *fileItem = new QTreeWidgetItem(parentItem,

                                             {asset.displayName,

                                              asset.statusMessage,

                                              QString::number(asset.gainDb, 'f', 1),

                                              statusText(asset.status)});

        fileItem->setData(0, RelativePathRole, asset.relativePath);

        fileItem->setData(0, ItemKindRole, QString::fromLatin1(kFileKind));

    }



    if (!filter.isEmpty()) {

        for (int row = m_treeRoot->childCount() - 1; row >= 0; --row) {

            if (pruneEmptyFolders(m_treeRoot->child(row))) {

                delete m_treeRoot->takeChild(row);

            }

        }

    }



    m_tree->expandItem(m_treeRoot);

}



void SoundModDialog::updateScanSummary()

{

    if (!m_scanSummaryLabel) {

        return;

    }



    if (m_assets.empty() && m_folderPaths.isEmpty() && m_scannedFileCount == 0) {

        m_scanSummaryLabel->setText(QStringLiteral("Scan a folder to list subfolders and moddable sound files."));

        return;

    }



    m_scanSummaryLabel->setText(

        QStringLiteral("Scanned %1 folders and %2 files. Found %3 moddable sound files in %4 folders.")

            .arg(m_scannedFolderCount)

            .arg(m_scannedFileCount)

            .arg(static_cast<int>(m_assets.size()))

            .arg(m_folderPaths.size()));

}



bool SoundModDialog::isFileItem(const QTreeWidgetItem *item) const

{

    return item && item->data(0, ItemKindRole).toString() == QLatin1String(kFileKind);

}



QString SoundModDialog::selectedFileRelativePath() const

{

    const QList<QTreeWidgetItem *> selected = m_tree->selectedItems();

    if (selected.isEmpty() || !isFileItem(selected.first())) {

        return QString();

    }

    return selected.first()->data(0, RelativePathRole).toString();

}



QVector<SoundAssetEntry> SoundModDialog::currentAssets() const

{

    QVector<SoundAssetEntry> assets;

    assets.reserve(static_cast<int>(m_assets.size()));

    for (const SoundAssetEntry &asset : m_assets) {

        assets.push_back(asset);

    }

    return assets;

}



void SoundModDialog::onBrowseRoot()

{

    const QString directory =

        QFileDialog::getExistingDirectory(this, QStringLiteral("Select game folder"), m_scanRootEdit->text());

    if (!directory.isEmpty()) {

        m_scanRootEdit->setText(QDir::fromNativeSeparators(directory));

    }

}



void SoundModDialog::onScan()

{

    if (m_scanning) {

        return;

    }



    const QString scanRoot = m_scanRootEdit->text().trimmed();

    if (scanRoot.isEmpty() || !QDir(scanRoot).exists()) {

        QMessageBox::warning(this, QStringLiteral("Scan"), QStringLiteral("Select a valid scan folder."));

        return;

    }



    m_game.scanRoot = scanRoot;

    m_scanning = true;

    m_progressBar->setVisible(true);

    m_applyButton->setEnabled(false);



    (void)QtConcurrent::run([this, scanRoot]() {

        AssetScanner scanner;

        const SoundScanResult result = scanner.scan(scanRoot);

        QMetaObject::invokeMethod(this,

                                  [this, result]() {

                                      m_assets = result.assets;

                                      m_folderPaths = result.folderPaths;

                                      m_scannedFileCount = result.scannedFileCount;

                                      m_scannedFolderCount = result.scannedFolderCount;

                                      mergeProfileIntoAssets();

                                      populateTree();

                                      updateScanSummary();

                                      m_scanning = false;

                                      m_progressBar->setVisible(false);

                                      m_applyButton->setEnabled(true);

                                  },

                                  Qt::QueuedConnection);

    });

}



bool SoundModDialog::confirmRunningProcessWarning() const

{

    if (m_processId == 0 || !ProcessLoopbackCapture::isProcessRunning(m_processId)) {

        return true;

    }



    const QMessageBox::StandardButton choice =

        QMessageBox::warning(const_cast<SoundModDialog *>(this),

                             QStringLiteral("App is still running"),

                             QStringLiteral("Close the app before applying file changes.\n\n"

                                            "Changes take effect on the next launch, but open files may block "

                                            "patching or be overwritten when the app exits."),

                             QMessageBox::Cancel | QMessageBox::Apply,

                             QMessageBox::Cancel);

    return choice == QMessageBox::Apply;

}



void SoundModDialog::onApply()

{

    if (!confirmRunningProcessWarning()) {

        return;

    }



    const QString scanRoot = m_scanRootEdit->text().trimmed();

    PatchEngine engine;

    const PatchApplyResult result = engine.apply(m_game, currentAssets(), scanRoot);



    if (result.success) {

        QMessageBox::information(this,

                                 QStringLiteral("Sound mods saved"),

                                 QStringLiteral("Sound modifications saved.\nThey will take effect the next time "

                                                  "you launch %1.")

                                     .arg(m_game.displayName));

        onScan();

    } else {

        QMessageBox::warning(this,

                             QStringLiteral("Apply completed with errors"),

                             result.message + QStringLiteral("\n\nFailed files:\n")

                                 + result.failedFiles.join(QStringLiteral("\n")));

    }

}



void SoundModDialog::onRestoreAll()

{

    if (!confirmRunningProcessWarning()) {

        return;

    }



    const PatchApplyResult result =

        PatchEngine().restoreAll(m_game, currentAssets(), m_scanRootEdit->text().trimmed());



    QMessageBox::information(this,

                             QStringLiteral("Restore defaults"),

                             result.message + QStringLiteral("\nRestart the app for changes to take effect."));

    onScan();

}



void SoundModDialog::onRestoreSelected()

{

    const QString relativePath = selectedFileRelativePath();

    if (relativePath.isEmpty()) {

        return;

    }

    if (!confirmRunningProcessWarning()) {

        return;

    }



    const PatchApplyResult result = PatchEngine().restoreSelected(m_game,

                                                                  currentAssets(),

                                                                  m_scanRootEdit->text().trimmed(),

                                                                  {relativePath});

    QMessageBox::information(this, QStringLiteral("Restore selected"), result.message);

    onScan();

}



void SoundModDialog::onSelectionChanged()

{

    updateSelectedGainControls();

}



void SoundModDialog::updateSelectedGainControls()

{

    const QString relativePath = selectedFileRelativePath();

    if (relativePath.isEmpty()) {

        m_gainSlider->setEnabled(false);

        m_enabledCheckBox->setEnabled(false);

        return;

    }



    for (SoundAssetEntry &asset : m_assets) {

        if (asset.relativePath == relativePath) {

            m_gainSlider->blockSignals(true);

            m_enabledCheckBox->blockSignals(true);

            m_gainSlider->setEnabled(asset.patchSupport == SoundPatchSupport::Full);

            m_enabledCheckBox->setEnabled(asset.patchSupport == SoundPatchSupport::Full);

            m_gainSlider->setValue(static_cast<int>(std::lround(asset.gainDb * 10.f)));

            m_enabledCheckBox->setChecked(asset.enabled);

            m_gainValueLabel->setText(QStringLiteral("%1 dB").arg(asset.gainDb, 0, 'f', 1));

            m_gainSlider->blockSignals(false);

            m_enabledCheckBox->blockSignals(false);

            return;

        }

    }

}



void SoundModDialog::onGainSliderChanged(int value)

{

    const QString relativePath = selectedFileRelativePath();

    if (relativePath.isEmpty()) {

        return;

    }



    const float gainDb = static_cast<float>(value) / 10.f;

    m_gainValueLabel->setText(QStringLiteral("%1 dB").arg(gainDb, 0, 'f', 1));



    for (SoundAssetEntry &asset : m_assets) {

        if (asset.relativePath == relativePath) {

            asset.gainDb = gainDb;

            asset.enabled = m_enabledCheckBox->isChecked();

            if (asset.patchSupport == SoundPatchSupport::Full && asset.enabled && std::fabs(gainDb) > 0.01f) {

                asset.status = SoundAssetStatus::Pending;

            } else if (asset.patchSupport == SoundPatchSupport::Unsupported) {

                asset.status = SoundAssetStatus::Unsupported;

            } else {

                asset.status = SoundAssetStatus::Original;

            }



            const QList<QTreeWidgetItem *> selected = m_tree->selectedItems();

            if (!selected.isEmpty()) {

                QTreeWidgetItem *item = selected.first();

                item->setText(2, QString::number(gainDb, 'f', 1));

                item->setText(3, statusText(asset.status));

            }

            return;

        }

    }

}



void SoundModDialog::onSearchChanged(const QString &)

{

    populateTree();

}


