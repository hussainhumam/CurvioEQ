#include "mainwindow.h"

#include "ui_mainwindow.h"

#include "audio/dspstatus.h"
#include "audio/log.h"
#include "audio/audioendpointvolume.h"
#include "audio/audiosessionvolume.h"
#include "ui/appconstants.h"
#include "ui/audiodeviceresolver.h"
#include "ui/eqcolorpalette.h"
#include "ui/eqsessionmanager.h"
#include "ui/globalhotkeymanager.h"
#include "ui/keybindsdialog.h"
#include "ui/presetpanelcontroller.h"
#include "ui/sessionlistcontroller.h"
#include "ui/settingsdialog.h"
#include "ui/singleinstanceserver.h"
#include "ui/spectrumwidget.h"
#include "ui/traycontroller.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QScrollBar>
#include <QShowEvent>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle(QString::fromLatin1(AppConstants::kAppDisplayName));
    setStatusBar(nullptr);

    ui->presetsLayout->setStretch(0, 1);
    ui->presetsLayout->setStretch(1, 0);

    ui->horizontalLayout_3->setContentsMargins(0, 4, 0, 0);
    ui->horizontalLayout_3->setSpacing(8);
    for (QPushButton *btn : {ui->enableEqButton, ui->disableEqButton, ui->resetBandsButton}) {
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    setupSurroundUi();
    setupEqControls();
    restructureLayout();

    m_spectrumWidget->setCapture(&m_spectrumCapture);
    m_audioEngine.setSpectrumCapture(&m_spectrumCapture);
    connect(m_spectrumWidget, &SpectrumWidget::spectrumEnabledChanged, this, [this](bool) {
        saveSpectrumSettings();
        updateSpectrumForSelection();
    });

    QFont logFont = ui->logTextEdit->font();
    logFont.setFamily(QStringLiteral("Consolas"));
    logFont.setStyleHint(QFont::Monospace);
    ui->logTextEdit->setFont(logFont);
    ui->logTextEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    ui->logTextEdit->setPlaceholderText(
        QStringLiteral("DSP verification and app log — select and copy (Ctrl+C)"));

    showDspVerificationInLog();

    m_bandSliders = {
        ui->verticalSlider,
        ui->verticalSlider_2,
        ui->verticalSlider_3,
        ui->verticalSlider_4,
        ui->verticalSlider_5,
        ui->verticalSlider_6,
        ui->verticalSlider_7,
        ui->verticalSlider_8,
        ui->verticalSlider_9,
        ui->verticalSlider_10,
    };

    for (QSlider *slider : m_bandSliders) {
        slider->setMinimum(-AppConstants::kMaxGainDb);
        slider->setMaximum(AppConstants::kMaxGainDb);
        slider->setMinimumHeight(120);
        slider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        slider->setValue(0);
        connect(slider, &QSlider::valueChanged, this, [this](int) {
            if (m_loadingSliders || !m_eqSessionManager) {
                return;
            }
            const unsigned long pid = m_sessionList ? m_sessionList->selectedProcessId() : 0UL;
            if (pid == 0) {
                return;
            }
            m_eqSessionManager->scheduleLiveGainsForProcess(pid);
        });
    }

    auto *masterColumn = new QVBoxLayout();
    masterColumn->setContentsMargins(0, 0, 0, 0);
    masterColumn->setSpacing(ui->bandLayout1->spacing());

    m_masterSlider = new QSlider(Qt::Vertical, ui->eqGroup);
    m_masterSlider->setMinimum(-AppConstants::kMaxGainDb);
    m_masterSlider->setMaximum(AppConstants::kMaxGainDb);
    m_masterSlider->setValue(0);
    m_masterSlider->setMinimumWidth(24);
    m_masterSlider->setMinimumHeight(120);
    m_masterSlider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_masterSlider->setToolTip(QStringLiteral("Shift all bands together"));

    auto *masterLabel = new QLabel(QStringLiteral("All"), ui->eqGroup);
    masterLabel->setAlignment(Qt::AlignCenter);

    masterColumn->addWidget(m_masterSlider, 1, Qt::AlignHCenter);
    masterColumn->addWidget(masterLabel, 0, Qt::AlignHCenter);
    ui->horizontalLayout->insertLayout(0, masterColumn);

    connect(m_masterSlider, &QSlider::valueChanged, this, &MainWindow::onMasterSliderChanged);
    for (int i = 0; i < ui->horizontalLayout->count(); ++i) {
        QLayoutItem *item = ui->horizontalLayout->itemAt(i);
        if (item && item->layout()) {
            item->layout()->setContentsMargins(0, 0, 0, 0);
            if (auto *bandLayout = qobject_cast<QVBoxLayout *>(item->layout())) {
                bandLayout->setStretch(0, 1);
                bandLayout->setStretch(1, 0);
            }
        }
        ui->horizontalLayout->setStretch(i, 1);
    }

    ui->sliderRow->setStretch(0, 0);
    ui->sliderRow->setStretch(1, 1);

    ui->verticalLayout->setContentsMargins(0, 0, 0, 0);
    ui->verticalLayout->setSpacing(0);
    ui->verticalLayout->setStretch(0, 0);
    ui->verticalLayout->setStretch(1, 1);
    ui->verticalLayout->setStretch(2, 0);
    ui->verticalLayout->setStretch(3, 1);
    ui->verticalLayout->setStretch(4, 0);
    ui->gainMaxLabel->setAlignment(Qt::AlignRight | Qt::AlignTop);
    ui->gainZeroLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    ui->gainMinLabel->setAlignment(Qt::AlignRight | Qt::AlignBottom);
    ui->gainMaxLabel->setText(QStringLiteral("+%1 db").arg(AppConstants::kMaxGainDb));
    ui->gainMinLabel->setText(QStringLiteral("-%1 db").arg(AppConstants::kMaxGainDb));
    auto *freqRowPad = new QWidget(ui->eqGroup);
    freqRowPad->setFixedHeight(ui->label_9->sizeHint().height() + ui->bandLayout1->spacing());
    ui->verticalLayout->addWidget(freqRowPad);

    ui->eqPanelLayout->setStretch(0, 1);
    ui->eqPanelLayout->setStretch(1, 0);
    ui->centralwidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    ui->runningAppsLayout->setStretch(1, 1);

    m_sessionList = new SessionListController(ui->appListView,
                                              ui->runningAppsCountLabel,
                                              ui->emptyStateLabel,
                                              this);
    connect(m_sessionList, &SessionListController::selectionChanged, this, [this]() {
        syncSlidersToSelection();
        updateEqControlState();
    });
    connect(m_sessionList, &SessionListController::refreshRequested, this, [this]() {
        m_audioEngine.pruneEndedSessions();
        updateEqControlState();
    });
    connect(m_sessionList, &SessionListController::logMessage, this, &MainWindow::appendLog);
    connect(m_sessionList, &SessionListController::errorOccurred, this, &MainWindow::showCopyableError);
    connect(ui->refreshButton, &QPushButton::clicked, this, &MainWindow::onRefreshClicked);

    m_eqSessionManager = new EqSessionManager(&m_audioEngine, &m_settingsStore, this);
    m_eqSessionManager->setGainReader([this]() { return readSliderGains(); });
    m_eqSessionManager->setSurroundStateReader([this]() { return readSurroundState(); });
    m_eqSessionManager->setDisplayNameProvider([this](unsigned long pid) {
        return m_sessionList ? m_sessionList->displayNameForPid(pid) : QString();
    });
    connect(m_eqSessionManager, &EqSessionManager::logMessage, this, &MainWindow::appendLog);
    connect(m_eqSessionManager, &EqSessionManager::errorOccurred, this,
            [this](const QString &title, const QString &message) {
                showCopyableError(title, message);
                if (m_tray && title.startsWith(QStringLiteral("EQ failed"))) {
                    m_tray->showCriticalMessage(QString::fromLatin1(AppConstants::kAppDisplayName), message);
                }
            });
    connect(m_eqSessionManager, &EqSessionManager::settingsRequested, this, &MainWindow::onSettingsClicked);
    connect(m_eqSessionManager, &EqSessionManager::controlStateChanged, this, &MainWindow::updateEqControlState);
    connect(m_eqSessionManager, &EqSessionManager::eqStateChanged, this, [this]() {
        refreshSessionList();
        updateSpectrumForSelection();
        updateEqControlState();
    });

    m_presetStore.load();
    m_presetPanel = new PresetPanelController(ui->presetsListWidget,
                                              ui->savePresetButton,
                                              ui->importPresetButton,
                                              ui->exportPresetButton,
                                              ui->deletePresetButton,
                                              &m_presetStore,
                                              this);
    m_presetPanel->setBandSliders(m_bandSliders);
    m_presetPanel->setGainReader([this]() { return readSliderGains(); });
    m_presetPanel->setEngineGainApplier([this](const std::array<float, EqProcessor::kBandCount> &gains) {
        if (!m_eqSessionManager || !m_sessionList) {
            return;
        }
        const unsigned long pid = m_sessionList->selectedProcessId();
        if (pid == 0) {
            return;
        }
        m_eqSessionManager->saveDraftForProcess(pid, gains, readSurroundState());
    });
    connect(m_presetPanel, &PresetPanelController::logMessage, this, &MainWindow::appendLog);
    connect(m_presetPanel, &PresetPanelController::errorOccurred, this, &MainWindow::showCopyableError);
    connect(m_presetPanel, &PresetPanelController::presetApplied, this, [this](const EqPreset &) {
        resetMasterSlider();
    });
    m_presetPanel->refreshList();

    connect(ui->enableEqButton, &QPushButton::clicked, this, &MainWindow::onEnableEq);
    connect(ui->disableEqButton, &QPushButton::clicked, this, &MainWindow::onDisableEq);
    connect(m_disableAllButton, &QPushButton::clicked, this, &MainWindow::onDisableAllEq);
    connect(ui->resetBandsButton, &QPushButton::clicked, this, &MainWindow::onResetClicked);
    connect(m_resetSurroundButton, &QPushButton::clicked, this, &MainWindow::onResetSurroundClicked);
    connect(m_applySurroundButton, &QPushButton::clicked, this, &MainWindow::onApplySurroundClicked);
    connect(m_surroundEnableCheckBox, &QCheckBox::toggled, this, [this](bool) {
        updateSurroundControlsEnabled();
        applySurroundToEngine();
        saveSurroundSettings();
    });
    for (QSpinBox *spin : m_surroundSpins) {
        if (spin) {
            connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
                if (m_loadingSliders || !m_eqSessionManager) {
                    return;
                }
                applySurroundToEngine();
                saveSurroundSettings();
            });
        }
    }
    connect(ui->actionSettings, &QAction::triggered, this, &MainWindow::onSettingsClicked);
    connect(ui->actionKeybinds, &QAction::triggered, this, &MainWindow::onKeybindsClicked);
    connect(ui->actionQuit, &QAction::triggered, this, &MainWindow::onQuitApp);

    m_hotkeyManager = new GlobalHotkeyManager(this);
    connect(m_hotkeyManager, &GlobalHotkeyManager::hotkeyTriggered, this, &MainWindow::onHotkeyTriggered);
    connect(m_hotkeyManager, &GlobalHotkeyManager::registrationFailed, this, [this](const QString &message) {
        appendLog(QStringLiteral("WARN"), message);
    });

    m_settingsStore.load();
    applySettings(m_settingsStore.settings());
    applyKeybindSettings();

    connect(&m_audioEngine, &AudioEngine::statusChanged, this, &MainWindow::onEngineStatusChanged);
    connect(&m_audioEngine, &AudioEngine::errorOccurred, this, &MainWindow::onEngineError);
    connect(&m_audioEngine, &AudioEngine::sessionStopped, m_eqSessionManager,
            &EqSessionManager::onSessionStopped, Qt::QueuedConnection);
    connect(&m_audioEngine, &AudioEngine::sessionStopped, this, [this](unsigned long) {
        refreshSessionList();
        updateSpectrumForSelection();
        updateEqControlState();
    }, Qt::QueuedConnection);

    m_tray = new TrayController(this, this);
    connect(m_tray, &TrayController::showWindowRequested, this, &MainWindow::onShowWindow);
    connect(m_tray, &TrayController::toggleEqForProcessRequested, this, &MainWindow::onTrayToggleEq);
    connect(m_tray, &TrayController::quitRequested, this, &MainWindow::onQuitApp);
    connect(m_tray, &TrayController::logMessage, this, &MainWindow::appendLog);
    m_tray->setup();

    m_singleInstance = new SingleInstanceServer(this);
    connect(m_singleInstance, &SingleInstanceServer::showRequested, this, &MainWindow::onShowWindow);
    connect(m_singleInstance, &SingleInstanceServer::listenFailed, this,
            [this](const QString &errorMessage) {
                appendLog(QStringLiteral("WARN"),
                          QStringLiteral("Could not listen for second-instance requests: %1")
                              .arg(errorMessage));
            });

    m_singleInstance->listen();

    updateEqControlState();
    refreshSessionList();
    syncSlidersToSelection();

    appendLog(QStringLiteral("INFO"), QStringLiteral("Ready"));
    AudioLog::info(QStringLiteral("MainWindow"), QStringLiteral("UI initialized"));
}

MainWindow::~MainWindow()
{
    m_quitting = true;
    if (m_hotkeyManager) {
        m_hotkeyManager->clear();
    }
    m_audioEngine.stop();
    delete ui;
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    applyKeybindSettings();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!m_quitting && m_tray && m_tray->isAvailable()) {
        hide();
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::setupSurroundUi()
{
    m_surroundGroup = new QGroupBox(QStringLiteral("7.1 Surround"), this);
    m_surroundGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *groupLayout = new QVBoxLayout(m_surroundGroup);

    auto *headerRow = new QHBoxLayout();
    m_surroundEnableCheckBox = new QCheckBox(QStringLiteral("Enable 7.1"), m_surroundGroup);
    m_resetSurroundButton = new QPushButton(QStringLiteral("Reset"), m_surroundGroup);
    m_applySurroundButton = new QPushButton(QStringLiteral("Apply"), m_surroundGroup);
    headerRow->addWidget(m_surroundEnableCheckBox);
    headerRow->addWidget(m_resetSurroundButton);
    headerRow->addWidget(m_applySurroundButton);
    headerRow->addStretch();
    groupLayout->addLayout(headerRow);

    struct SpeakerCell {
        SurroundProcessor::Channel channel;
        const char *label;
        int row;
        int column;
    };

    const SpeakerCell cells[] = {
        {SurroundProcessor::FrontCenter, "FC", 0, 1},
        {SurroundProcessor::FrontLeft, "FL", 1, 0},
        {SurroundProcessor::FrontRight, "FR", 1, 2},
        {SurroundProcessor::SideLeft, "SL", 2, 0},
        {SurroundProcessor::SideRight, "SR", 2, 2},
        {SurroundProcessor::BackLeft, "RL", 3, 0},
        {SurroundProcessor::Lfe, "LFE", 3, 1},
        {SurroundProcessor::BackRight, "RR", 3, 2},
    };

    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);

    for (const SpeakerCell &cell : cells) {
        auto *cellWidget = new QWidget(m_surroundGroup);
        auto *cellLayout = new QVBoxLayout(cellWidget);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        cellLayout->setSpacing(2);

        auto *label = new QLabel(QString::fromLatin1(cell.label), cellWidget);
        label->setAlignment(Qt::AlignHCenter);

        auto *spin = new QSpinBox(cellWidget);
        spin->setRange(0, 100);
        spin->setValue(50);
        spin->setMinimumWidth(64);
        spin->setAlignment(Qt::AlignCenter);

        cellLayout->addWidget(label);
        cellLayout->addWidget(spin, 0, Qt::AlignHCenter);
        grid->addWidget(cellWidget, cell.row, cell.column, Qt::AlignCenter);

        m_surroundSpins[static_cast<size_t>(cell.channel)] = spin;
    }

    groupLayout->addLayout(grid);
}

void MainWindow::setupEqControls()
{
    auto *colorLabel = new QLabel(QStringLiteral("Color:"), ui->eqGroup);
    m_colorPalette = new EqColorPalette(ui->eqGroup);
    ui->horizontalLayout_3->insertWidget(0, colorLabel);
    ui->horizontalLayout_3->insertWidget(1, m_colorPalette);

    m_disableAllButton = new QPushButton(QStringLiteral("Disable all"), ui->eqGroup);
    m_disableAllButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ui->horizontalLayout_3->addWidget(m_disableAllButton);

    ui->disableEqButton->setText(QStringLiteral("Disable for app"));

    connect(m_colorPalette, &EqColorPalette::colorSelected, this, [this](const QColor &) {
        updateEqControlState();
    });
    connect(m_colorPalette, &EqColorPalette::selectionChanged, this, [this]() {
        updateEqControlState();
    });
}

void MainWindow::restructureLayout()
{
    ui->mainLayout->removeWidget(ui->logTextEdit);

    ui->contentRow->removeWidget(ui->presetsGroup);
    ui->contentRow->removeWidget(ui->runningAppsGroup);

    auto *rightWidget = new QWidget(this);
    rightWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);

    auto *appsRow = new QHBoxLayout();
    appsRow->setSpacing(16);
    appsRow->addWidget(ui->presetsGroup, 0);
    appsRow->addWidget(ui->runningAppsGroup, 1);
    rightLayout->addLayout(appsRow, 1);

    m_spectrumWidget = new SpectrumWidget(this);
    m_spectrumWidget->setMinimumHeight(96);
    m_spectrumWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    rightLayout->addWidget(m_spectrumWidget, 1);

    ui->logTextEdit->setMinimumHeight(80);
    rightLayout->addWidget(ui->logTextEdit, 0);

    auto *leftWidget = new QWidget(this);
    leftWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);

    ui->contentRow->removeWidget(ui->eqGroup);
    ui->eqGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    leftLayout->addWidget(ui->eqGroup, 1);
    leftLayout->addWidget(m_surroundGroup, 0);

    ui->contentRow->addWidget(leftWidget, 1);
    ui->contentRow->addWidget(rightWidget, 2);
    ui->contentRow->setStretch(0, 1);
    ui->contentRow->setStretch(1, 2);

    ui->mainLayout->setStretch(0, 1);
}

void MainWindow::updateSurroundControlsEnabled()
{
    const bool enabled = m_surroundEnableCheckBox && m_surroundEnableCheckBox->isChecked();
    if (m_resetSurroundButton) {
        m_resetSurroundButton->setEnabled(enabled);
    }
    if (m_applySurroundButton) {
        m_applySurroundButton->setEnabled(enabled);
    }
    for (QSpinBox *spin : m_surroundSpins) {
        if (spin) {
            spin->setEnabled(enabled);
        }
    }
}

std::array<int, SurroundProcessor::kChannelCount> MainWindow::readSurroundChannelLevels() const
{
    std::array<int, SurroundProcessor::kChannelCount> levels{};
    for (int i = 0; i < SurroundProcessor::kChannelCount; ++i) {
        QSpinBox *spin = m_surroundSpins[static_cast<size_t>(i)];
        levels[static_cast<size_t>(i)] = spin ? spin->value() : 50;
    }
    return levels;
}

std::pair<bool, std::array<int, SurroundProcessor::kChannelCount>> MainWindow::readSurroundState() const
{
    return {m_surroundEnableCheckBox && m_surroundEnableCheckBox->isChecked(), readSurroundChannelLevels()};
}

void MainWindow::applySurroundToUi(bool enabled, const std::array<int, SurroundProcessor::kChannelCount> &levels)
{
    if (m_surroundEnableCheckBox) {
        m_surroundEnableCheckBox->setChecked(enabled);
    }
    for (int i = 0; i < SurroundProcessor::kChannelCount; ++i) {
        if (QSpinBox *spin = m_surroundSpins[static_cast<size_t>(i)]) {
            spin->setValue(levels[static_cast<size_t>(i)]);
        }
    }
    updateSurroundControlsEnabled();
}

void MainWindow::applySurroundToEngine()
{
    if (!m_eqSessionManager || !m_sessionList) {
        return;
    }
    const unsigned long pid = m_sessionList->selectedProcessId();
    if (pid == 0) {
        return;
    }
    m_eqSessionManager->pushLiveSurroundForProcess(pid);
}

void MainWindow::saveSurroundSettings()
{
    AppSettings settings = m_settingsStore.settings();
    const auto state = readSurroundState();
    settings.surroundEnabled = state.first;
    settings.surroundChannelLevels = state.second;
    m_settingsStore.setSettings(settings);
    m_settingsStore.save();
}

void MainWindow::saveSpectrumSettings()
{
    if (!m_spectrumWidget) {
        return;
    }

    AppSettings settings = m_settingsStore.settings();
    settings.spectrumEnabled = m_spectrumWidget->isSpectrumEnabled();
    m_settingsStore.setSettings(settings);
    m_settingsStore.save();
}

void MainWindow::onResetSurroundClicked()
{
    applySurroundToUi(true, {50, 50, 50, 50, 50, 50, 50, 50});
    applySurroundToEngine();
    saveSurroundSettings();
    appendLog(QStringLiteral("INFO"), QStringLiteral("7.1 speaker levels reset to 50"));
}

void MainWindow::onApplySurroundClicked()
{
    applySurroundToEngine();
    saveSurroundSettings();
    appendLog(QStringLiteral("INFO"), QStringLiteral("7.1 surround settings applied"));
}

void MainWindow::onRefreshClicked()
{
    refreshSessionList();
    ui->appListView->viewport()->update();
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    const int count = m_sessionList ? m_sessionList->appCount() : 0;
    ui->runningAppsCountLabel->setText(
        count > 0 ? QStringLiteral("%1 app(s) playing audio — refreshed %2").arg(count).arg(timestamp)
                  : QStringLiteral("Active audio sessions — refreshed %1").arg(timestamp));
    appendLog(QStringLiteral("INFO"), QStringLiteral("Refreshed app list"));
}

void MainWindow::refreshSessionList()
{
    if (!m_sessionList || !m_eqSessionManager) {
        return;
    }

    m_sessionList->setEqSessions(m_eqSessionManager->activeSessionColors());
    m_sessionList->refresh();
}

void MainWindow::onResetClicked()
{
    m_loadingSliders = true;
    applyGainsToSliders({});
    m_loadingSliders = false;
    resetMasterSlider();

    if (m_eqSessionManager && m_sessionList) {
        const unsigned long pid = m_sessionList->selectedProcessId();
        if (pid != 0) {
            m_eqSessionManager->saveDraftForProcess(pid, readSliderGains(), readSurroundState());
        }
    }

    appendLog(QStringLiteral("INFO"), QStringLiteral("EQ bands reset to 0 dB"));
    AudioLog::info(QStringLiteral("MainWindow"), QStringLiteral("Reset sliders to 0 dB"));
}

void MainWindow::onEnableEq()
{
    if (!m_eqSessionManager || !m_sessionList || !m_colorPalette) {
        return;
    }

    const unsigned long pid = m_sessionList->selectedProcessId();
    if (pid == 0) {
        showCopyableError(QStringLiteral("Enable EQ"), QStringLiteral("Select an app from the list first."));
        return;
    }

    if (!m_colorPalette->hasSelection()) {
        const EqSessionSnapshot snapshot = m_eqSessionManager->snapshotFor(pid);
        if (!snapshot.labelColor.isValid()) {
            showCopyableError(QStringLiteral("Enable EQ"), QStringLiteral("Pick a color before enabling EQ."));
            return;
        }
    }

    const QColor labelColor = m_colorPalette->hasSelection()
                                  ? m_colorPalette->selectedColor()
                                  : m_eqSessionManager->snapshotFor(pid).labelColor;

    if (m_eqSessionManager->enableForProcess(pid, labelColor)) {
        if (m_colorPalette->hasSelection()) {
            m_colorPalette->clearSelection();
        }
        m_sliderEditPid = pid;
        refreshSessionList();
        updateSpectrumForSelection();
        updateEqControlState();
    }
}

void MainWindow::onDisableEq()
{
    if (!m_eqSessionManager || !m_sessionList) {
        return;
    }

    const unsigned long pid = m_sessionList->selectedProcessId();
    if (pid == 0) {
        return;
    }

    m_eqSessionManager->disableForProcess(pid);
    syncSlidersToSelection();
    refreshSessionList();
    updateSpectrumForSelection();
    updateEqControlState();
}

void MainWindow::onDisableAllEq()
{
    if (!m_eqSessionManager) {
        return;
    }

    m_eqSessionManager->disableAll();
    syncSlidersToSelection();
    refreshSessionList();
    updateSpectrumForSelection();
    updateEqControlState();
}

void MainWindow::onTrayToggleEq(unsigned long processId)
{
    if (!m_eqSessionManager || processId == 0) {
        return;
    }

    if (m_eqSessionManager->isRunning(processId)) {
        m_eqSessionManager->disableForProcess(processId);
    } else {
        m_eqSessionManager->restoreForProcess(processId);
    }

    syncSlidersToSelection();
    refreshSessionList();
    updateSpectrumForSelection();
    updateEqControlState();
}

void MainWindow::onShowWindow()
{
    show();
    raise();
    activateWindow();
}

void MainWindow::onQuitApp()
{
    m_quitting = true;
    m_audioEngine.stop();
    QApplication::quit();
}

void MainWindow::appendLog(const QString &level, const QString &message)
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    ui->logTextEdit->appendPlainText(
        QStringLiteral("[%1] [%2] %3").arg(timestamp, level, message));
    ui->logTextEdit->verticalScrollBar()->setValue(ui->logTextEdit->verticalScrollBar()->maximum());
}

void MainWindow::showDspVerificationInLog()
{
    const DspStatusReport report = collectDspStatusReport(false);

    ui->logTextEdit->clear();
    for (const std::string &line : report.lines) {
        ui->logTextEdit->appendPlainText(QString::fromStdString(line));
    }

    const QString summary = report.isHealthy()
                                ? QStringLiteral("DSP verification passed at startup")
                                : QStringLiteral("DSP verification failed (%1 check(s))")
                                      .arg(report.failureCount);
    appendLog(report.isHealthy() ? QStringLiteral("INFO") : QStringLiteral("ERROR"), summary);
    ui->logTextEdit->verticalScrollBar()->setValue(0);
}

void MainWindow::showCopyableError(const QString &title, const QString &message)
{
    const QString line = title.isEmpty() ? message : QStringLiteral("%1: %2").arg(title, message);
    appendLog(QStringLiteral("ERROR"), line);
    AudioLog::error(QStringLiteral("MainWindow"), line);

    QMessageBox box(QMessageBox::Critical, title.isEmpty() ? QStringLiteral("Error") : title, message, QMessageBox::Ok, this);
    box.setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    box.exec();
}

void MainWindow::onEngineStatusChanged(const QString &message)
{
    const QString level = message.startsWith(QStringLiteral("WARN:"), Qt::CaseInsensitive)
                              ? QStringLiteral("WARN")
                              : QStringLiteral("INFO");
    appendLog(level, message);

    if (message.contains(QStringLiteral("EQ stopped"), Qt::CaseInsensitive)) {
        refreshSessionList();
        updateSpectrumForSelection();
    }
    updateEqControlState();
}

void MainWindow::onEngineError(const QString &message)
{
    m_audioEngine.stop();
    showCopyableError(QStringLiteral("Audio engine error"), message);
    if (m_tray) {
        m_tray->showCriticalMessage(QString::fromLatin1(AppConstants::kAppDisplayName), message);
    }
    if (m_eqSessionManager) {
        m_eqSessionManager->disableAll();
    }
    refreshSessionList();
    updateSpectrumForSelection();
    updateEqControlState();
}

void MainWindow::onSettingsClicked()
{
    SettingsDialog dialog(m_settingsStore.settings(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    applySettings(dialog.resultSettings());
}

void MainWindow::onKeybindsClicked()
{
    KeybindsDialog dialog(m_settingsStore.settings(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    AppSettings settings = m_settingsStore.settings();
    const AppSettings keybindSettings = dialog.resultSettings();
    settings.keybindsEnabled = keybindSettings.keybindsEnabled;
    settings.eqToggleKeybind = keybindSettings.eqToggleKeybind;
    settings.outputMuteKeybind = keybindSettings.outputMuteKeybind;
    settings.eqColorKeybinds = keybindSettings.eqColorKeybinds;
    applySettings(settings);
}

void MainWindow::onHotkeyTriggered(int hotkeyId)
{
    if (hotkeyId == GlobalHotkeyManager::kEqToggleHotkeyId) {
        if (m_eqSessionManager && m_eqSessionManager->isAnyRunning()) {
            onDisableAllEq();
        }
        return;
    }

    if (hotkeyId == GlobalHotkeyManager::kOutputMuteHotkeyId) {
        const ResolvedDevice output = AudioDeviceResolver::resolveEqOutput(m_settingsStore.settings());
        if (output.id.isEmpty()) {
            appendLog(QStringLiteral("WARN"),
                      QStringLiteral("Mute hotkey: no EQ output device configured in Settings"));
            return;
        }

        QString errorMessage;
        if (AudioEndpointVolume::toggleMute(output.id, &errorMessage)) {
            appendLog(QStringLiteral("INFO"),
                      QStringLiteral("Toggled mute on EQ output device: %1").arg(output.name));
        } else {
            appendLog(QStringLiteral("WARN"),
                      QStringLiteral("Mute hotkey failed: %1").arg(errorMessage));
        }
        return;
    }

    const int colorIndex = GlobalHotkeyManager::eqColorIndexFromHotkeyId(hotkeyId);
    if (colorIndex >= 0) {
        onColorKeybindTriggered(colorIndex);
    }
}

void MainWindow::onColorKeybindTriggered(int colorIndex)
{
    if (colorIndex < 0 || colorIndex >= AppSettings::kEqColorKeybindCount || !m_eqSessionManager) {
        return;
    }

    const QColor labelColor = EqColorPalette::presetColorAt(colorIndex);
    const QVector<unsigned long> processIds =
        m_eqSessionManager->activeProcessIdsForLabelColor(labelColor);

    if (processIds.isEmpty()) {
        appendLog(QStringLiteral("INFO"),
                  QStringLiteral("No active EQ sessions with label %1")
                      .arg(EqColorPalette::presetColorLabel(colorIndex)));
        return;
    }

    int toggledCount = 0;
    for (unsigned long pid : processIds) {
        QString errorMessage;
        if (AudioSessionVolume::toggleMute(pid, &errorMessage)) {
            ++toggledCount;
        } else {
            appendLog(QStringLiteral("WARN"),
                      QStringLiteral("Failed to toggle mute for PID %1 (%2): %3")
                          .arg(pid)
                          .arg(EqColorPalette::presetColorLabel(colorIndex))
                          .arg(errorMessage));
        }
    }

    if (toggledCount > 0) {
        appendLog(QStringLiteral("INFO"),
                  QStringLiteral("Toggled mute for %1 app(s) with label %2")
                      .arg(toggledCount)
                      .arg(EqColorPalette::presetColorLabel(colorIndex)));
    }
}

void MainWindow::applyKeybindSettings()
{
    if (!m_hotkeyManager) {
        return;
    }

    m_hotkeyManager->apply(m_settingsStore.settings(), winId());
}

void MainWindow::applySettings(const AppSettings &settings)
{
    m_settingsStore.setSettings(settings);
    m_settingsStore.save();

    applySurroundToUi(settings.surroundEnabled, settings.surroundChannelLevels);
    applySurroundToEngine();

    if (m_spectrumWidget) {
        m_spectrumWidget->setSpectrumEnabled(settings.spectrumEnabled);
    }

    QString startupError;
    if (!SettingsStore::applyStartWithWindows(settings.startWithWindows, &startupError)) {
        showCopyableError(QStringLiteral("Startup setting failed"), startupError);
    }

    applyKeybindSettings();
}

void MainWindow::updateEqControlState()
{
    const unsigned long selectedPid = m_sessionList ? m_sessionList->selectedProcessId() : 0UL;
    const bool anyRunning = m_eqSessionManager && m_eqSessionManager->isAnyRunning();
    const bool selectedRunning = m_eqSessionManager && selectedPid != 0
                                 && m_eqSessionManager->isRunning(selectedPid);
    const bool canRestore = m_eqSessionManager && m_eqSessionManager->canRestoreProcess(selectedPid);
    const bool canEnable = m_eqSessionManager && selectedPid != 0 && !selectedRunning
                           && m_colorPalette
                           && (m_colorPalette->hasSelection() || canRestore);

    if (ui->enableEqButton) {
        ui->enableEqButton->setEnabled(canEnable);
    }
    if (ui->disableEqButton) {
        ui->disableEqButton->setEnabled(selectedRunning);
    }
    if (m_disableAllButton) {
        m_disableAllButton->setEnabled(anyRunning);
    }
    if (m_tray) {
        m_tray->updateEqSessions(m_eqSessionManager ? m_eqSessionManager->configuredTraySessions()
                                                    : QVector<ConfiguredEqSession>{});
    }
    if (m_sessionList) {
        m_sessionList->setAutoRefreshEnabled(anyRunning);
    }
}

void MainWindow::applyGainsToSliders(const std::array<float, EqProcessor::kBandCount> &gains)
{
    for (int band = 0; band < EqProcessor::kBandCount; ++band) {
        if (m_bandSliders[static_cast<size_t>(band)]) {
            const int value = qBound(-AppConstants::kMaxGainDb,
                                     static_cast<int>(gains[static_cast<size_t>(band)]),
                                     AppConstants::kMaxGainDb);
            m_bandSliders[static_cast<size_t>(band)]->setValue(value);
        }
    }
}

void MainWindow::resetMasterSlider()
{
    if (!m_masterSlider) {
        return;
    }

    m_masterSlider->blockSignals(true);
    m_masterSlider->setValue(0);
    m_masterSlider->blockSignals(false);
    m_lastMasterValue = 0;
}

void MainWindow::onMasterSliderChanged(int value)
{
    if (m_loadingSliders) {
        return;
    }

    const int delta = value - m_lastMasterValue;
    m_lastMasterValue = value;
    if (delta == 0) {
        return;
    }

    m_loadingSliders = true;
    for (QSlider *slider : m_bandSliders) {
        if (!slider) {
            continue;
        }
        const int next = qBound(-AppConstants::kMaxGainDb, slider->value() + delta, AppConstants::kMaxGainDb);
        slider->setValue(next);
    }
    m_loadingSliders = false;

    if (m_eqSessionManager && m_sessionList) {
        const unsigned long pid = m_sessionList->selectedProcessId();
        if (pid != 0) {
            m_eqSessionManager->scheduleLiveGainsForProcess(pid);
        }
    }
}

void MainWindow::syncSlidersToSelection()
{
    if (!m_eqSessionManager || !m_sessionList) {
        return;
    }

    const unsigned long pid = m_sessionList->selectedProcessId();
    if (m_sliderEditPid != 0 && m_sliderEditPid != pid) {
        m_eqSessionManager->pushLiveGainsForProcess(m_sliderEditPid);
    }
    m_sliderEditPid = pid;

    m_loadingSliders = true;
    m_eqSessionManager->applySnapshotToUi(
        pid,
        [this](const std::array<float, EqProcessor::kBandCount> &gains) { applyGainsToSliders(gains); },
        [this](bool enabled, const std::array<int, SurroundProcessor::kChannelCount> &levels) {
            applySurroundToUi(enabled, levels);
        });
    m_loadingSliders = false;
    resetMasterSlider();

    updateSpectrumForSelection();
}

void MainWindow::updateSpectrumForSelection()
{
    if (!m_spectrumWidget || !m_sessionList || !m_eqSessionManager) {
        return;
    }

    const unsigned long pid = m_sessionList->selectedProcessId();
    const bool sessionActive = pid != 0 && m_eqSessionManager->isRunning(pid);
    const bool feedSpectrum = sessionActive && m_spectrumWidget->isSpectrumEnabled();
    const QString appName = sessionActive ? m_sessionList->displayNameForPid(pid) : QString();

    m_audioEngine.setSpectrumProcessId(feedSpectrum ? pid : 0UL);
    m_spectrumWidget->setEqActive(feedSpectrum);
    m_spectrumWidget->setActiveAppName(appName);
}

std::array<float, EqProcessor::kBandCount> MainWindow::readSliderGains() const
{
    std::array<float, EqProcessor::kBandCount> gains{};
    for (int band = 0; band < EqProcessor::kBandCount; ++band) {
        gains[static_cast<size_t>(band)] = static_cast<float>(m_bandSliders[static_cast<size_t>(band)]->value());
    }
    return gains;
}
