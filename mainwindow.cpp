#include "mainwindow.h"

#include "ui_mainwindow.h"

#include "audio/log.h"
#include "ui/appconstants.h"
#include "ui/eqsessioncontroller.h"
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
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle(QString::fromLatin1(AppConstants::kAppDisplayName));
    setStatusBar(nullptr);

    ui->horizontalLayout_3->setContentsMargins(0, 4, 0, 0);
    ui->horizontalLayout_3->setSpacing(8);
    for (QPushButton *btn : {ui->enableEqButton, ui->disableEqButton, ui->resetBandsButton}) {
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    setupSurroundUi();
    restructureLayout();

    m_spectrumWidget->setCapture(&m_spectrumCapture);
    m_audioEngine.setSpectrumCapture(&m_spectrumCapture);

    QFont logFont = ui->logTextEdit->font();
    logFont.setFamily(QStringLiteral("Consolas"));
    logFont.setStyleHint(QFont::Monospace);
    ui->logTextEdit->setFont(logFont);
    ui->logTextEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);

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
        slider->setMinimumHeight(120);
        slider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        slider->setValue(0);
        connect(slider, &QSlider::valueChanged, this, [this](int) {
            if (m_audioEngine.isRunning()) {
                m_audioEngine.setGains(readSliderGains());
            }
        });
    }
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
    connect(m_sessionList, &SessionListController::selectionChanged, this, &MainWindow::updateEqControlState);
    connect(m_sessionList, &SessionListController::refreshRequested, this, &MainWindow::updateEqControlState);
    connect(m_sessionList, &SessionListController::logMessage, this, &MainWindow::appendLog);
    connect(m_sessionList, &SessionListController::errorOccurred, this, &MainWindow::showCopyableError);
    connect(ui->refreshButton, &QPushButton::clicked, this, &MainWindow::onRefreshClicked);

    m_eqSession = new EqSessionController(&m_audioEngine, &m_settingsStore, this);
    m_eqSession->setGainReader([this]() { return readSliderGains(); });
    m_eqSession->setSurroundStateReader([this]() { return readSurroundState(); });
    m_eqSession->setSelectedProcessIdProvider([this]() {
        return m_sessionList ? m_sessionList->selectedProcessId() : 0UL;
    });
    m_eqSession->setDisplayNameProvider([this](unsigned long pid) {
        return m_sessionList ? m_sessionList->displayNameForPid(pid) : QString();
    });
    connect(m_eqSession, &EqSessionController::logMessage, this, &MainWindow::appendLog);
    connect(m_eqSession, &EqSessionController::errorOccurred, this,
            [this](const QString &title, const QString &message) {
                showCopyableError(title, message);
                if (m_tray && title.startsWith(QStringLiteral("EQ failed"))) {
                    m_tray->showCriticalMessage(QString::fromLatin1(AppConstants::kAppDisplayName), message);
                }
            });
    connect(m_eqSession, &EqSessionController::settingsRequested, this, &MainWindow::onSettingsClicked);
    connect(m_eqSession, &EqSessionController::controlStateChanged, this, &MainWindow::updateEqControlState);
    connect(m_eqSession, &EqSessionController::eqStateChanged, this,
            [this](bool running, unsigned long pid, const QString &appName) {
                if (m_sessionList) {
                    m_sessionList->setEqActive(pid, running);
                    refreshSessionList();
                }
                updateSpectrumUi(running, appName);
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
        if (m_audioEngine.isRunning()) {
            m_audioEngine.setGains(gains);
        }
    });
    connect(m_presetPanel, &PresetPanelController::logMessage, this, &MainWindow::appendLog);
    connect(m_presetPanel, &PresetPanelController::errorOccurred, this, &MainWindow::showCopyableError);
    m_presetPanel->refreshList();

    connect(ui->enableEqButton, &QPushButton::clicked, this, &MainWindow::onEnableEq);
    connect(ui->disableEqButton, &QPushButton::clicked, this, &MainWindow::onDisableEq);
    connect(ui->resetBandsButton, &QPushButton::clicked, this, &MainWindow::onResetClicked);
    connect(m_resetSurroundButton, &QPushButton::clicked, this, &MainWindow::onResetSurroundClicked);
    connect(m_applySurroundButton, &QPushButton::clicked, this, &MainWindow::onApplySurroundClicked);
    connect(m_surroundEnableCheckBox, &QCheckBox::toggled, this, [this](bool) {
        updateSurroundControlsEnabled();
        applySurroundToEngine();
        saveSurroundSettings();
    });
    connect(ui->actionSettings, &QAction::triggered, this, &MainWindow::onSettingsClicked);
    connect(ui->actionQuit, &QAction::triggered, this, &MainWindow::onQuitApp);

    m_settingsStore.load();
    applySettings(m_settingsStore.settings());

    connect(&m_audioEngine, &AudioEngine::statusChanged, this, &MainWindow::onEngineStatusChanged);
    connect(&m_audioEngine, &AudioEngine::errorOccurred, this, &MainWindow::onEngineError);

    m_tray = new TrayController(this, this);
    connect(m_tray, &TrayController::showWindowRequested, this, &MainWindow::onShowWindow);
    connect(m_tray, &TrayController::enableEqRequested, this, &MainWindow::onEnableEq);
    connect(m_tray, &TrayController::disableEqRequested, this, &MainWindow::onDisableEq);
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

    appendLog(QStringLiteral("INFO"), QStringLiteral("Ready"));
    AudioLog::info(QStringLiteral("MainWindow"), QStringLiteral("UI initialized"));
}

MainWindow::~MainWindow()
{
    m_quitting = true;
    m_audioEngine.stop();
    delete ui;
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
    m_spectrumWidget->setMinimumHeight(78);
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
    const auto state = readSurroundState();
    m_audioEngine.setSurroundEnabled(state.first);
    m_audioEngine.setSurroundChannelLevels(state.second);
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
    if (!m_sessionList || !m_eqSession) {
        return;
    }

    m_sessionList->setEqActive(m_eqSession->activePid(), m_eqSession->isRunning());
    m_sessionList->refresh();
}

void MainWindow::onResetClicked()
{
    for (QSlider *slider : m_bandSliders) {
        slider->setValue(0);
    }

    if (m_eqSession) {
        m_eqSession->resetBandGains();
    }

    appendLog(QStringLiteral("INFO"), QStringLiteral("EQ bands reset to 0 dB"));
    AudioLog::info(QStringLiteral("MainWindow"), QStringLiteral("Reset sliders to 0 dB"));
}

void MainWindow::onEnableEq()
{
    if (m_eqSession && m_eqSession->snapshot().valid) {
        applySurroundToUi(m_eqSession->snapshot().surroundEnabled,
                          m_eqSession->snapshot().surroundChannelLevels);
        applySurroundToEngine();
    }

    if (m_eqSession) {
        m_eqSession->enableEq();
    }
}

void MainWindow::onDisableEq()
{
    if (m_eqSession) {
        m_eqSession->disableEq();
    }
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
        if (m_eqSession) {
            m_eqSession->notifyEngineStopped();
        }
        refreshSessionList();
        updateSpectrumUi(false, {});
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
    if (m_eqSession) {
        m_eqSession->notifyEngineStopped();
    }
    refreshSessionList();
    updateSpectrumUi(false, {});
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

void MainWindow::applySettings(const AppSettings &settings)
{
    m_settingsStore.setSettings(settings);
    m_settingsStore.save();

    applySurroundToUi(settings.surroundEnabled, settings.surroundChannelLevels);
    applySurroundToEngine();

    QString startupError;
    if (!SettingsStore::applyStartWithWindows(settings.startWithWindows, &startupError)) {
        showCopyableError(QStringLiteral("Startup setting failed"), startupError);
    }
}

void MainWindow::updateEqControlState()
{
    const bool running = m_eqSession && m_eqSession->isRunning();
    const bool canEnable = m_eqSession
                           && !running
                           && (m_eqSession->snapshot().valid
                               || (m_sessionList && m_sessionList->selectedProcessId() != 0));

    if (ui->enableEqButton) {
        ui->enableEqButton->setEnabled(canEnable);
    }
    if (ui->disableEqButton) {
        ui->disableEqButton->setEnabled(running);
    }
    if (m_tray) {
        m_tray->updateEqControls(canEnable, running);
    }
}

void MainWindow::updateSpectrumUi(bool eqActive, const QString &appName)
{
    if (!m_spectrumWidget) {
        return;
    }

    m_spectrumWidget->setEqActive(eqActive);
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
