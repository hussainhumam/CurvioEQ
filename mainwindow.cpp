#include "mainwindow.h"

#include "ui_mainwindow.h"

#include "audio/clipfrequencyanalyzer.h"
#include "audio/dspstatus.h"
#include "audio/hrtfpresets.h"
#include "audio/log.h"
#include "audio/audioendpointvolume.h"
#include "audio/audiosessionvolume.h"
#include "vst3/vst3addonmanager.h"
#include "ui/addonspanel.h"
#include "ui/appconstants.h"
#include "ui/appiconprovider.h"
#include "ui/apppaths.h"
#include "ui/audiodeviceresolver.h"
#include "ui/eqcolorpalette.h"
#include "ui/eqsessionmanager.h"
#include "ui/explorerstartupverb.h"
#include "ui/globalhotkeymanager.h"
#include "ui/keybindsdialog.h"
#include "ui/presetpanelcontroller.h"
#include "ui/parametriceqpanel.h"
#include "ui/sessionlistcontroller.h"
#include "ui/audiochaindialog.h"
#include "ui/settingsdialog.h"
#include "ui/setupdialog.h"
#include "ui/singleinstanceserver.h"
#include "ui/slidervaluetip.h"
#include "ui/spectrumwidget.h"
#include "audio/mixlimiter.h"
#include "ui/soundmoddialog.h"
#include "ui/traycontroller.h"
#include "ui/updatechecker.h"

#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QShowEvent>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QToolTip>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>

namespace {

QString dynamicsModeLabelForAmount(int amount)
{
    if (amount <= DynamicRangeSettings::kAmountMin + 10) {
        return QStringLiteral("Ultra Open");
    }
    if (amount < 0) {
        return QStringLiteral("Extra Open");
    }
    if (amount <= 20) {
        return QStringLiteral("Open");
    }
    if (amount <= 45) {
        return QStringLiteral("Natural");
    }
    if (amount <= 70) {
        return QStringLiteral("Controlled");
    }
    if (amount <= 100) {
        return QStringLiteral("Tight");
    }
    if (amount < DynamicRangeSettings::kAmountMax - 10) {
        return QStringLiteral("Extra Tight");
    }
    return QStringLiteral("Ultra Tight");
}

QString loudnessTargetLabelForAmount(int amount)
{
    if (amount <= DynamicRangeSettings::kLoudnessMin) {
        return QStringLiteral("Off");
    }
    return QStringLiteral("%1 LUFS").arg(static_cast<double>(loudnessAmountToTargetLufs(amount)), 0, 'f', 2);
}

bool isEqHistoryShortcutWindow(const QObject *watched, const QWidget *mainWindow)
{
    const auto *widget = qobject_cast<const QWidget *>(watched);
    return widget && mainWindow && widget->window() == mainWindow;
}

bool isEqUndoShortcut(const QKeyEvent *key)
{
    if (!key || key->isAutoRepeat() || !key->modifiers().testFlag(Qt::ControlModifier)
        || key->modifiers().testFlag(Qt::AltModifier) || key->modifiers().testFlag(Qt::ShiftModifier)) {
        return false;
    }
    if (key->matches(QKeySequence::Undo)) {
        return true;
    }
    return key->nativeVirtualKey() == 0x5A; // VK_Z, layout-independent
}

bool isEqRedoShortcut(const QKeyEvent *key)
{
    if (!key || key->isAutoRepeat() || !key->modifiers().testFlag(Qt::ControlModifier)
        || key->modifiers().testFlag(Qt::AltModifier)) {
        return false;
    }
    const bool shift = key->modifiers().testFlag(Qt::ShiftModifier);
    if (!shift && (key->matches(QKeySequence::Redo) || key->key() == Qt::Key_Y)) {
        return true;
    }
    const quint32 vk = key->nativeVirtualKey();
    return (!shift && vk == 0x59) || (shift && vk == 0x5A); // VK_Y or Ctrl+Shift+Z
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle(QString::fromLatin1(AppConstants::kAppDisplayName));
    setStatusBar(nullptr);

    ui->presetsLayout->setStretch(0, 1);
    ui->presetsLayout->setStretch(1, 0);

    setupSurroundUi();
    setupDynamicsUi();
    restructureLayout();

    m_spectrumWidget->setCapture(&m_spectrumCapture);
    m_audioEngine.setSpectrumCapture(&m_spectrumCapture);
    connect(m_spectrumWidget, &SpectrumWidget::spectrumEnabledChanged, this, [this](bool) {
        saveSpectrumSettings();
        updateSpectrumForSelection();
    });
    connect(m_spectrumWidget, &SpectrumWidget::limiterCeilingChanged, this, [this](float db) {
        m_audioEngine.setOutputLimiterThreshold(MixLimiter::dbToLinear(db));
    });
    connect(m_spectrumWidget, &SpectrumWidget::limiterCeilingEditFinished, this, [this]() {
        saveSpectrumSettings();
    });

    QFont logFont = ui->logTextEdit->font();
    logFont.setFamily(QStringLiteral("Consolas"));
    logFont.setStyleHint(QFont::Monospace);
    ui->logTextEdit->setFont(logFont);
    ui->logTextEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    ui->logTextEdit->setPlaceholderText(
        QStringLiteral("DSP verification and app log — select and copy (Ctrl+C)"));
    ui->logTextEdit->setUndoRedoEnabled(false);

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
        installSliderValueTip(slider, [](int value) { return formatSignedDb(value); });
        connect(slider, &QSlider::valueChanged, this, [this, slider](int) {
            if (m_loadingSliders) {
                return;
            }
            if (slider->isSliderDown()) {
                beginUserEqEdit();
            } else {
                beginUserEqEdit();
                markSimpleEqEdited();
                if (m_eqSessionManager) {
                    const unsigned long pid = m_sessionList ? m_sessionList->selectedProcessId() : 0UL;
                    if (pid != 0) {
                        m_eqSessionManager->scheduleLiveGainsForProcess(pid);
                    }
                }
                endUserEqEdit();
                return;
            }
            markSimpleEqEdited();
            if (!m_eqSessionManager) {
                return;
            }
            const unsigned long pid = m_sessionList ? m_sessionList->selectedProcessId() : 0UL;
            if (pid == 0) {
                return;
            }
            m_eqSessionManager->scheduleLiveGainsForProcess(pid);
        });
        connect(slider, &QSlider::sliderReleased, this, [this]() {
            endUserEqEdit();
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
    installSliderValueTip(m_masterSlider, [](int value) {
        return QStringLiteral("Shift all bands together\n%1").arg(formatSignedDb(value));
    });

    auto *masterLabel = new QLabel(QStringLiteral("All"), ui->eqGroup);
    masterLabel->setAlignment(Qt::AlignCenter);

    masterColumn->addWidget(m_masterSlider, 1, Qt::AlignHCenter);
    masterColumn->addWidget(masterLabel, 0, Qt::AlignHCenter);
    ui->horizontalLayout->insertLayout(0, masterColumn);

    connect(m_masterSlider, &QSlider::valueChanged, this, &MainWindow::onMasterSliderChanged);
    connect(m_masterSlider, &QSlider::sliderReleased, this, [this]() {
        endUserEqEdit();
    });
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

    setupEqModeUi();

    ui->runningAppsLayout->setStretch(0, 0);
    ui->runningAppsLayout->setStretch(1, 1);

    m_sessionList = new SessionListController(ui->appListView, ui->runningAppsCountLabel, this);
    connect(m_sessionList, &SessionListController::selectionChanged, this, [this]() {
        syncSlidersToSelection();
        updateEqControlState();
    });
    connect(m_sessionList, &SessionListController::refreshRequested, this, [this]() {
        m_audioEngine.pruneEndedSessions();
        applyStartupPresetsToVisibleSessions();
        if (m_sessionList && m_eqSessionManager) {
            m_sessionList->setEqSessions(m_eqSessionManager->activeSessionColors());
        }
        updateEqControlState();
    });
    connect(m_sessionList, &SessionListController::logMessage, this, &MainWindow::appendLog);
    connect(m_sessionList, &SessionListController::errorOccurred, this, &MainWindow::showCopyableError);
    connect(m_sessionList, &SessionListController::enableEqRequested, this, [this](unsigned long pid) {
        if (!m_eqSessionManager) {
            return;
        }
        if (m_eqSessionManager->enableForProcess(pid)) {
            attachAddonsForProcess(pid);
            m_sliderEditPid = pid;
            refreshSessionList();
            updateSpectrumForSelection();
            updateEqControlState();
        }
    });
    connect(m_sessionList, &SessionListController::startupPresetToggled, this,
            [this](unsigned long pid, bool enable) {
                const QString exePath = AppIconProvider::executablePathForProcess(pid);
                if (exePath.isEmpty()) {
                    showCopyableError(QStringLiteral("Start at app startup"),
                                      QStringLiteral("Could not find this app's executable."));
                    return;
                }
                if (!enable) {
                    if (!m_startupPresetStore.removeBinding(exePath)) {
                        showCopyableError(QStringLiteral("Start at app startup"),
                                          QStringLiteral("Could not update startup preset bindings."));
                    }
                    return;
                }
                bindStartupPresetForExe(exePath);
            });
    m_sessionList->setStartupPresetBoundQuery([this](unsigned long pid) {
        return m_startupPresetStore.hasBinding(AppIconProvider::executablePathForProcess(pid));
    });
    connect(m_sessionList, &SessionListController::disableEqRequested, this, [this](unsigned long pid) {
        if (!m_eqSessionManager) {
            return;
        }
        m_eqSessionManager->disableForProcess(pid);
        syncSlidersToSelection();
        refreshSessionList();
        updateSpectrumForSelection();
        updateEqControlState();
    });
    connect(m_sessionList, &SessionListController::soundModsRequested, this, [this](unsigned long pid) {
        const QString displayName = m_sessionList ? m_sessionList->displayNameForPid(pid) : QStringLiteral("App");
        auto *dialog = new SoundModDialog(pid, displayName, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->open();
    });
    connect(m_sessionList, &SessionListController::appVolumeChanged, this,
            [this](unsigned long pid, int percent) {
                const float boost = AudioSessionVolume::outputGainForPercent(percent);
                m_audioEngine.setSessionOutputGain(pid, boost);
            });
    m_addonManager = new Vst3AddonManager(&m_audioEngine, this);
    m_addonManager->setExtraFolders(m_settingsStore.settings().vst3ExtraFolders);
    connect(m_addonManager, &Vst3AddonManager::logMessage, this, &MainWindow::appendLog);
    m_clipAnalyzer = new ClipFrequencyAnalyzer(this);
    connect(m_clipAnalyzer, &ClipFrequencyAnalyzer::logMessage, this, &MainWindow::appendLog);
    connect(m_clipAnalyzer, &ClipFrequencyAnalyzer::recordingChanged, this,
            [this](bool recording, unsigned long pid, const QString &name) {
                if (!m_sessionList) {
                    return;
                }
                m_sessionList->setClipRecording(recording ? pid : 0, name);
            });
    connect(m_sessionList, &SessionListController::recordClipRequested, this,
            [this](unsigned long pid) {
                if (!m_clipAnalyzer || !m_sessionList) {
                    return;
                }
                m_clipAnalyzer->start(pid, m_sessionList->displayNameForPid(pid));
            });
    connect(m_sessionList, &SessionListController::stopClipAnalyzeRequested, this, [this]() {
        if (m_clipAnalyzer) {
            m_clipAnalyzer->stopAndAnalyze();
        }
    });
    connect(ui->refreshButton, &QPushButton::clicked, this, &MainWindow::onRefreshClicked);

    m_eqSessionManager = new EqSessionManager(&m_audioEngine, &m_settingsStore, this);
    m_eqSessionManager->setEqStateReader([this]() { return readEqState(); });
    m_eqSessionManager->setSurroundStateReader([this]() { return readVirtualSurroundState(); });
    m_eqSessionManager->setDynamicsStateReader([this]() { return readDynamicRangeState(); });
    m_eqSessionManager->setAudioChainOrderReader([this]() { return readAudioChainOrder(); });
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
                                              ui->autoEqPresetsButton,
                                              &m_presetStore,
                                              this);
    m_presetPanel->setBandSliders(m_bandSliders);
    m_presetPanel->setEqStateReader([this]() { return readEqState(); });
    m_presetPanel->setEqStateApplier([this](const EqState &state) {
        EqState applied = state;
        if (applied.filterCount > 0) {
            applied.advanced = true;
        }
        beginUserEqEdit();
        applyEqStateToUi(applied);
        endUserEqEdit();
    });
    m_presetPanel->setSurroundStateReader([this]() { return readVirtualSurroundState(); });
    m_presetPanel->setSurroundStateApplier([this](const VirtualSurroundSettings &settings) {
        m_loadingSliders = true;
        applySurroundToUi(settings);
        m_loadingSliders = false;
        applySurroundToEngine();
    });
    m_presetPanel->setDynamicsStateReader([this]() { return readDynamicRangeState(); });
    m_presetPanel->setDynamicsStateApplier([this](const DynamicRangeSettings &settings) {
        m_loadingSliders = true;
        applyDynamicRangeToUi(settings);
        m_loadingSliders = false;
        applyDynamicRangeToEngine();
    });
    m_presetPanel->setAudioChainOrderReader([this]() { return readAudioChainOrder(); });
    m_presetPanel->setAudioChainOrderApplier([this](const AudioChainOrder &order) {
        applyAudioChainToUi(order);
        applyAudioChainToEngine();
    });
    connect(m_presetPanel, &PresetPanelController::logMessage, this, &MainWindow::appendLog);
    connect(m_presetPanel, &PresetPanelController::errorOccurred, this, &MainWindow::showCopyableError);
    connect(m_presetPanel, &PresetPanelController::presetApplied, this, [this](const EqPreset &preset) {
        if (preset.hasEq) {
            resetMasterSlider();
            persistEqUiMode();
            if (preset.eq.advanced || preset.eq.filterCount > 0) {
                appendLog(QStringLiteral("INFO"),
                          QStringLiteral("Advanced preset loaded (Advanced mode): %1").arg(preset.name));
            }
        }
        if (!m_eqSessionManager || !m_sessionList) {
            return;
        }
        const unsigned long pid = m_sessionList->selectedProcessId();
        if (pid == 0) {
            return;
        }
        m_eqSessionManager->saveDraftForProcess(pid, readEqState(), readVirtualSurroundState(),
                                                readDynamicRangeState(),
                                                readAudioChainOrder());
        if (preset.hasEq) {
            m_eqSessionManager->scheduleLiveGainsForProcess(pid);
        }
    });
    m_presetPanel->refreshList();

    connect(ui->disableAllButton, &QPushButton::clicked, this, &MainWindow::onDisableAllEq);
    connect(ui->resetBandsButton, &QPushButton::clicked, this, &MainWindow::onResetClicked);
    connect(m_resetSurroundButton, &QPushButton::clicked, this, &MainWindow::onResetSurroundClicked);
    connect(m_resetDynamicsButton, &QPushButton::clicked, this, &MainWindow::onResetDynamicsClicked);
    connect(m_surroundEnableCheckBox, &QCheckBox::toggled, this, [this](bool) {
        updateSurroundControlsEnabled();
        if (m_loadingSliders || !m_eqSessionManager) {
            return;
        }
        markCurrentPresetDirty();
        applySurroundToEngine();
        saveSurroundSettings();
    });
    if (m_hrtfPresetCombo) {
        connect(m_hrtfPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            if (m_loadingSliders || !m_eqSessionManager) {
                return;
            }
            markCurrentPresetDirty();
            applySurroundToEngine();
            saveSurroundSettings();
        });
    }
    if (m_hrtfStrengthSlider) {
        connect(m_hrtfStrengthSlider, &QSlider::valueChanged, this, [this](int value) {
            if (m_hrtfStrengthValueLabel) {
                m_hrtfStrengthValueLabel->setText(QStringLiteral("%1%").arg(value));
            }
            if (m_loadingSliders || !m_eqSessionManager) {
                return;
            }
            markCurrentPresetDirty();
            applySurroundToEngine();
            saveSurroundSettings();
        });
    }
    for (QSpinBox *spin : m_surroundSpins) {
        if (spin) {
            connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
                if (m_loadingSliders || !m_eqSessionManager) {
                    return;
                }
                markCurrentPresetDirty();
                applySurroundToEngine();
                saveSurroundSettings();
            });
        }
    }
    if (m_dynamicsEnableCheckBox) {
        connect(m_dynamicsEnableCheckBox, &QCheckBox::toggled, this, [this](bool) {
            updateDynamicsControlsEnabled();
            if (m_loadingSliders || !m_eqSessionManager) {
                return;
            }
            markCurrentPresetDirty();
            applyDynamicRangeToEngine();
            saveDynamicRangeSettings();
        });
    }
    if (m_dynamicsAmountSlider) {
        connect(m_dynamicsAmountSlider, &QSlider::valueChanged, this, [this](int value) {
            if (m_dynamicsModeLabel) {
                m_dynamicsModeLabel->setText(dynamicsModeLabelForAmount(value));
            }
            if (m_loadingSliders || !m_eqSessionManager) {
                return;
            }
            markCurrentPresetDirty();
            applyDynamicRangeToEngine();
            saveDynamicRangeSettings();
        });
    }
    if (m_loudnessAmountSlider) {
        connect(m_loudnessAmountSlider, &QSlider::valueChanged, this, [this](int value) {
            if (m_loudnessTargetLabel) {
                m_loudnessTargetLabel->setText(loudnessTargetLabelForAmount(value));
            }
            if (m_loadingSliders || !m_eqSessionManager) {
                return;
            }
            markCurrentPresetDirty();
            applyDynamicRangeToEngine();
            saveDynamicRangeSettings();
        });
    }
    connect(ui->actionSettings, &QAction::triggered, this, &MainWindow::onSettingsClicked);
    connect(ui->actionAudioChain, &QAction::triggered, this, &MainWindow::onAudioChainClicked);
    connect(ui->actionKeybinds, &QAction::triggered, this, &MainWindow::onKeybindsClicked);
    connect(ui->actionQuit, &QAction::triggered, this, &MainWindow::onQuitApp);

    m_hotkeyManager = new GlobalHotkeyManager(this);
    connect(m_hotkeyManager, &GlobalHotkeyManager::hotkeyTriggered, this, &MainWindow::onHotkeyTriggered);
    connect(m_hotkeyManager, &GlobalHotkeyManager::registrationFailed, this, [this](const QString &message) {
        appendLog(QStringLiteral("WARN"), message);
    });

    m_settingsStore.load();

    AppSettings loadedSettings = m_settingsStore.settings();
    const bool alreadyConfigured = !loadedSettings.routingSinkDeviceId.isEmpty()
                                   && !loadedSettings.eqOutputDeviceId.isEmpty();
    const bool skipWelcome = loadedSettings.setupCompleted
                             || alreadyConfigured
                             || AppPaths::hasExistingSettingsFile()
                             || AppPaths::welcomeMarkerExists();
    AppPaths::markWelcomeShown();
    loadedSettings.setupCompleted = true;
    applySettings(loadedSettings);

    if (!skipWelcome) {
        SetupDialog setupDialog(m_settingsStore.settings(), this);
        if (setupDialog.exec() == QDialog::Accepted) {
            applySettings(setupDialog.resultSettings());
        }
    }

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
    connect(m_singleInstance, &SingleInstanceServer::startAtAppStartupRequested, this,
            &MainWindow::handleStartAtAppStartup);
    connect(m_singleInstance, &SingleInstanceServer::listenFailed, this,
            [this](const QString &errorMessage) {
                appendLog(QStringLiteral("WARN"),
                          QStringLiteral("Could not listen for second-instance requests: %1")
                              .arg(errorMessage));
            });

    m_singleInstance->listen();
    if (!ExplorerStartupVerb::registerVerb()) {
        appendLog(QStringLiteral("WARN"),
                  QStringLiteral("Could not register Explorer \"Start at app startup\" menu item"));
    }

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
    updateSessionListAutoRefresh();
    refreshSessionList();
}

void MainWindow::hideEvent(QHideEvent *event)
{
    QMainWindow::hideEvent(event);
    updateSessionListAutoRefresh();
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event && event->type() == QEvent::WindowStateChange) {
        updateSessionListAutoRefresh();
    }
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
    m_surroundGroup = new QGroupBox(QStringLiteral("Virtual Surround"), this);
    m_surroundGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *groupLayout = new QVBoxLayout(m_surroundGroup);
    groupLayout->setContentsMargins(8, 8, 8, 8);
    groupLayout->setSpacing(6);

    auto *headerRow = new QHBoxLayout();
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(6);
    m_surroundEnableCheckBox = new QCheckBox(QStringLiteral("Enable HRTF"), m_surroundGroup);
    m_surroundEnableCheckBox->setToolTip(
        QStringLiteral("Upmix stereo apps and render them with HRTF for headphones."));
    m_resetSurroundButton = new QPushButton(QStringLiteral("Reset"), m_surroundGroup);
    m_resetSurroundButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    headerRow->addWidget(m_surroundEnableCheckBox);
    headerRow->addStretch(1);
    headerRow->addWidget(m_resetSurroundButton);
    groupLayout->addLayout(headerRow);

    auto *controlsRow = new QHBoxLayout();
    controlsRow->setContentsMargins(0, 0, 0, 0);
    controlsRow->setSpacing(8);
    controlsRow->addWidget(new QLabel(QStringLiteral("Preset"), m_surroundGroup));
    m_hrtfPresetCombo = new QComboBox(m_surroundGroup);
    m_hrtfPresetCombo->addItem(QStringLiteral("Default"), static_cast<int>(HrtfPresetId::Default));
    m_hrtfPresetCombo->addItem(QStringLiteral("Wide"), static_cast<int>(HrtfPresetId::Wide));
    m_hrtfPresetCombo->addItem(QStringLiteral("Close"), static_cast<int>(HrtfPresetId::Close));
    m_hrtfPresetCombo->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    controlsRow->addWidget(m_hrtfPresetCombo, 1);

    controlsRow->addWidget(new QLabel(QStringLiteral("Strength"), m_surroundGroup));
    m_hrtfStrengthSlider = new QSlider(Qt::Horizontal, m_surroundGroup);
    m_hrtfStrengthSlider->setRange(0, 100);
    m_hrtfStrengthSlider->setValue(75);
    installSliderValueTip(m_hrtfStrengthSlider, [](int value) {
        return QStringLiteral("%1%").arg(value);
    });
    m_hrtfStrengthValueLabel = new QLabel(QStringLiteral("75%"), m_surroundGroup);
    m_hrtfStrengthValueLabel->setMinimumWidth(36);
    m_hrtfStrengthValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    controlsRow->addWidget(m_hrtfStrengthSlider, 2);
    controlsRow->addWidget(m_hrtfStrengthValueLabel);
    groupLayout->addLayout(controlsRow);

    struct SpeakerCell {
        SurroundProcessor::Channel channel;
        const char *label;
        const char *tip;
        int row;
        int column;
    };

    const SpeakerCell cells[] = {
        {SurroundProcessor::FrontCenter, "Front C", "Front center", 0, 1},
        {SurroundProcessor::FrontLeft, "Front L", "Front left", 1, 0},
        {SurroundProcessor::FrontRight, "Front R", "Front right", 1, 2},
        {SurroundProcessor::SideLeft, "Side L", "Side left", 2, 0},
        {SurroundProcessor::SideRight, "Side R", "Side right", 2, 2},
        {SurroundProcessor::BackLeft, "Rear L", "Rear left", 3, 0},
        {SurroundProcessor::Lfe, "LFE", "Subwoofer (disabled for headphones)", 3, 1},
        {SurroundProcessor::BackRight, "Rear R", "Rear right", 3, 2},
    };

    auto *grid = new QGridLayout();
    grid->setContentsMargins(0, 2, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(4);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);

    auto *youLabel = new QLabel(QStringLiteral("You"), m_surroundGroup);
    youLabel->setAlignment(Qt::AlignCenter);
    QPalette youPalette = youLabel->palette();
    youPalette.setColor(QPalette::WindowText, youPalette.color(QPalette::PlaceholderText));
    youLabel->setPalette(youPalette);
    grid->addWidget(youLabel, 2, 1, Qt::AlignCenter);

    for (const SpeakerCell &cell : cells) {
        auto *cellWidget = new QWidget(m_surroundGroup);
        auto *cellLayout = new QVBoxLayout(cellWidget);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        cellLayout->setSpacing(1);

        auto *label = new QLabel(QString::fromLatin1(cell.label), cellWidget);
        label->setAlignment(Qt::AlignHCenter);
        label->setToolTip(QString::fromLatin1(cell.tip));

        auto *spin = new QSpinBox(cellWidget);
        spin->setRange(0, 100);
        spin->setValue(cell.channel == SurroundProcessor::Lfe ? 0 : 50);
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setMaximumWidth(48);
        spin->setAlignment(Qt::AlignCenter);
        spin->setToolTip(QString::fromLatin1(cell.tip));
        if (cell.channel == SurroundProcessor::Lfe) {
            spin->setEnabled(false);
            spin->setToolTip(QStringLiteral("LFE is disabled for headphone virtual surround to prevent bass rumble."));
        }

        cellLayout->addWidget(label);
        cellLayout->addWidget(spin, 0, Qt::AlignHCenter);

        Qt::Alignment align = Qt::AlignCenter;
        if (cell.row == 3) {
            align = Qt::AlignHCenter | Qt::AlignTop;
            if (cell.channel == SurroundProcessor::Lfe) {
                cellLayout->setContentsMargins(0, 24, 0, 0);
            }
        }
        grid->addWidget(cellWidget, cell.row, cell.column, align);

        m_surroundSpins[static_cast<size_t>(cell.channel)] = spin;
    }

    groupLayout->addLayout(grid);
}

void MainWindow::setupDynamicsUi()
{
    m_dynamicsGroup = new QGroupBox(QStringLiteral("Dynamics"), this);
    m_dynamicsGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *groupLayout = new QVBoxLayout(m_dynamicsGroup);
    groupLayout->setContentsMargins(8, 8, 8, 8);
    groupLayout->setSpacing(6);

    m_dynamicsEnableCheckBox = new QCheckBox(QStringLiteral("Enable dynamics processing"), m_dynamicsGroup);
    m_dynamicsEnableCheckBox->setToolTip(
        QStringLiteral("Enables dynamic range shaping and loudness normalization for the selected app."));
    m_resetDynamicsButton = new QPushButton(QStringLiteral("Reset"), m_dynamicsGroup);
    m_resetDynamicsButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto *headerRow = new QHBoxLayout();
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(6);
    headerRow->addWidget(m_dynamicsEnableCheckBox);
    headerRow->addStretch(1);
    headerRow->addWidget(m_resetDynamicsButton);
    groupLayout->addLayout(headerRow);

    auto *rangeRow = new QHBoxLayout();
    auto *wideLabel = new QLabel(QStringLiteral("Wide"), m_dynamicsGroup);
    wideLabel->setToolTip(QStringLiteral("Extra-wide settings preserve almost all macro-dynamics."));
    m_dynamicsAmountSlider = new QSlider(Qt::Horizontal, m_dynamicsGroup);
    m_dynamicsAmountSlider->setRange(DynamicRangeSettings::kAmountMin, DynamicRangeSettings::kAmountMax);
    m_dynamicsAmountSlider->setValue(DynamicRangeSettings::kAmountDefault);
    installSliderValueTip(m_dynamicsAmountSlider, [](int value) {
        return QStringLiteral("%1 \u2014 %2").arg(value).arg(dynamicsModeLabelForAmount(value));
    });
    auto *tightLabel = new QLabel(QStringLiteral("Tight"), m_dynamicsGroup);
    tightLabel->setToolTip(QStringLiteral("Extra-tight settings add stronger peak control for maximum consistency."));
    m_dynamicsModeLabel = new QLabel(dynamicsModeLabelForAmount(DynamicRangeSettings::kAmountDefault), m_dynamicsGroup);
    m_dynamicsModeLabel->setMinimumWidth(88);
    m_dynamicsModeLabel->setAlignment(Qt::AlignCenter);
    rangeRow->addWidget(wideLabel);
    rangeRow->addWidget(m_dynamicsAmountSlider, 1);
    rangeRow->addWidget(tightLabel);
    rangeRow->addWidget(m_dynamicsModeLabel);
    groupLayout->addLayout(rangeRow);

    auto *loudnessRow = new QHBoxLayout();
    auto *quietLabel = new QLabel(QStringLiteral("Quiet"), m_dynamicsGroup);
    quietLabel->setToolTip(QStringLiteral("Quieter EBU R128 target (−23 LUFS)."));
    m_loudnessAmountSlider = new QSlider(Qt::Horizontal, m_dynamicsGroup);
    m_loudnessAmountSlider->setRange(DynamicRangeSettings::kLoudnessMin, DynamicRangeSettings::kLoudnessMax);
    m_loudnessAmountSlider->setValue(DynamicRangeSettings::kLoudnessDefault);
    m_loudnessAmountSlider->setSingleStep(1);
    m_loudnessAmountSlider->setPageStep(10);
    m_loudnessAmountSlider->setStatusTip(
        QStringLiteral("Target loudness in LUFS. 0 is off; −23 LUFS is EBU; −14 LUFS is streaming."));
    installSliderValueTip(m_loudnessAmountSlider, [](int value) {
        return loudnessTargetLabelForAmount(value);
    });
    auto *loudLabel = new QLabel(QStringLiteral("Loud"), m_dynamicsGroup);
    loudLabel->setToolTip(QStringLiteral("Streaming / YouTube target (−14 LUFS)."));
    m_loudnessTargetLabel = new QLabel(loudnessTargetLabelForAmount(DynamicRangeSettings::kLoudnessDefault), m_dynamicsGroup);
    m_loudnessTargetLabel->setMinimumWidth(88);
    m_loudnessTargetLabel->setAlignment(Qt::AlignCenter);
    loudnessRow->addWidget(quietLabel);
    loudnessRow->addWidget(m_loudnessAmountSlider, 1);
    loudnessRow->addWidget(loudLabel);
    loudnessRow->addWidget(m_loudnessTargetLabel);
    groupLayout->addLayout(loudnessRow);
}

void MainWindow::setupEqModeUi()
{
    auto *modeRow = new QHBoxLayout();
    modeRow->setContentsMargins(0, 0, 0, 0);
    modeRow->setSpacing(6);

    m_simpleModeButton = new QPushButton(QStringLiteral("Simple"), ui->eqGroup);
    m_advancedModeButton = new QPushButton(QStringLiteral("Advanced"), ui->eqGroup);
    m_simpleModeButton->setCheckable(true);
    m_advancedModeButton->setCheckable(true);
    m_simpleModeButton->setChecked(true);

    auto *modeGroup = new QButtonGroup(this);
    modeGroup->setExclusive(true);
    modeGroup->addButton(m_simpleModeButton);
    modeGroup->addButton(m_advancedModeButton);

    modeRow->addWidget(m_simpleModeButton);
    modeRow->addWidget(m_advancedModeButton);
    modeRow->addStretch(1);
    modeRow->addWidget(ui->resetBandsButton);
    ui->eqPanelLayout->insertLayout(0, modeRow);

    m_eqModeStack = new QStackedWidget(ui->eqGroup);
    m_eqModeStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *simplePage = new QWidget(m_eqModeStack);
    simplePage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *simpleLayout = new QVBoxLayout(simplePage);
    simpleLayout->setContentsMargins(0, 0, 0, 0);
    simpleLayout->setSpacing(0);

    // QLayout inherits QLayoutItem, so takeAt() returns the layout itself.
    // Do not delete that pointer — ownership moves to simpleLayout via addLayout().
    for (int i = 0; i < ui->eqPanelLayout->count(); ++i) {
        QLayoutItem *item = ui->eqPanelLayout->itemAt(i);
        if (item && item->layout() == ui->sliderRow) {
            ui->eqPanelLayout->takeAt(i);
            simpleLayout->addLayout(ui->sliderRow, 1);
            break;
        }
    }

    m_parametricPanel = new ParametricEqPanel(m_eqModeStack);
    m_parametricPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_eqModeStack->addWidget(simplePage);
    m_eqModeStack->addWidget(m_parametricPanel);
    ui->eqPanelLayout->insertWidget(1, m_eqModeStack, 1);
    ui->eqPanelLayout->setStretch(0, 0);
    ui->eqPanelLayout->setStretch(1, 1);

    auto *balanceRow = new QHBoxLayout();
    balanceRow->setContentsMargins(8, 0, 8, 4);
    balanceRow->setSpacing(6);
    auto *balanceLeftLabel = new QLabel(QStringLiteral("L"), ui->eqGroup);
    balanceLeftLabel->setAlignment(Qt::AlignCenter);
    balanceLeftLabel->setFixedWidth(14);
    m_balanceSlider = new QSlider(Qt::Horizontal, ui->eqGroup);
    m_balanceSlider->setRange(AppConstants::kMinBalance, AppConstants::kMaxBalance);
    m_balanceSlider->setValue(AppConstants::kDefaultBalance);
    m_balanceSlider->setSingleStep(1);
    m_balanceSlider->setPageStep(1);
    m_balanceSlider->setToolTip(
        QStringLiteral("Shift output toward the left or right speaker. 0 is centered. "
                       "Values near 0 snap to center."));
    m_balanceLeftValueLabel = new QLabel(QStringLiteral("0"), ui->eqGroup);
    m_balanceLeftValueLabel->setAlignment(Qt::AlignCenter);
    m_balanceLeftValueLabel->setFixedWidth(24);
    m_balanceValueLabel = new QLabel(QStringLiteral("0"), ui->eqGroup);
    m_balanceValueLabel->setAlignment(Qt::AlignCenter);
    m_balanceValueLabel->setFixedWidth(24);
    auto *balanceRightLabel = new QLabel(QStringLiteral("R"), ui->eqGroup);
    balanceRightLabel->setAlignment(Qt::AlignCenter);
    balanceRightLabel->setFixedWidth(14);
    balanceRow->addWidget(balanceLeftLabel);
    balanceRow->addWidget(m_balanceLeftValueLabel);
    balanceRow->addWidget(m_balanceSlider, 1);
    balanceRow->addWidget(m_balanceValueLabel);
    balanceRow->addWidget(balanceRightLabel);
    connect(m_balanceSlider, &QSlider::sliderPressed, this, []() { QToolTip::hideText(); });
    connect(m_balanceSlider, &QSlider::sliderMoved, this, []() { QToolTip::hideText(); });
    ui->eqPanelLayout->insertLayout(2, balanceRow);

    auto pushLiveBalance = [this](int value) {
        if (!m_eqSessionManager || !m_sessionList) {
            return;
        }
        const unsigned long pid = m_sessionList->selectedProcessId();
        if (pid != 0) {
            m_eqSessionManager->applyLiveBalance(pid, value);
        }
    };
    auto snapBalanceToCenter = [this]() -> int {
        if (!m_balanceSlider) {
            return AppConstants::kDefaultBalance;
        }
        int value = m_balanceSlider->value();
        constexpr int kSnapLow = -1;
        constexpr int kSnapHigh = 1;
        if (value >= kSnapLow && value <= kSnapHigh && value != AppConstants::kDefaultBalance) {
            m_balanceSlider->blockSignals(true);
            m_balanceSlider->setValue(AppConstants::kDefaultBalance);
            m_balanceSlider->blockSignals(false);
            value = AppConstants::kDefaultBalance;
        }
        return value;
    };

    connect(m_balanceSlider, &QSlider::valueChanged, this,
            [this, pushLiveBalance, snapBalanceToCenter](int value) {
                if (m_loadingSliders) {
                    return;
                }
                if (m_balanceSlider->isSliderDown()) {
                    QToolTip::hideText();
                }
                if (!m_balanceSlider->isSliderDown()) {
                    value = snapBalanceToCenter();
                }
                updateBalanceLabels(value);
                if (m_balanceSlider->isSliderDown()) {
                    if (!m_eqHistoryCoalescing) {
                        beginUserEqEdit();
                    }
                } else {
                    beginUserEqEdit();
                    pushLiveBalance(value);
                    endUserEqEdit();
                    return;
                }
                pushLiveBalance(value);
            });
    connect(m_balanceSlider, &QSlider::sliderReleased, this,
            [this, pushLiveBalance, snapBalanceToCenter]() {
                const int value = snapBalanceToCenter();
                updateBalanceLabels(value);
                pushLiveBalance(value);
                endUserEqEdit();
            });

    connect(m_simpleModeButton, &QPushButton::clicked, this, &MainWindow::onEqModeToggled);
    connect(m_advancedModeButton, &QPushButton::clicked, this, &MainWindow::onEqModeToggled);
    connect(m_parametricPanel, &ParametricEqPanel::eqChanged, this, &MainWindow::onParametricEqChanged);
    connect(m_parametricPanel, &ParametricEqPanel::eqEditStarted, this, &MainWindow::beginUserEqEdit);
    connect(m_parametricPanel, &ParametricEqPanel::eqEditEnded, this, &MainWindow::endUserEqEdit);
    connect(m_parametricPanel, &ParametricEqPanel::resetRequested, this, &MainWindow::onResetClicked);
    setupEqHistory();
    setupUpdateChecker();
}

void MainWindow::markSimpleEqEdited()
{
    m_cachedSimpleGains = readSliderGains();
    m_simpleEditedSinceAdvanced = true;
}

void MainWindow::syncEqModeCachesFromState(const EqState &state)
{
    m_cachedSimpleGains = state.gainsDb;
    if (state.advanced && state.filterCount > 0) {
        m_cachedAdvancedEq = state;
        m_cachedAdvancedEq.advanced = true;
        m_hasCachedAdvanced = true;
        m_simpleEditedSinceAdvanced = false;
        captureAdvancedEntryBaseline(m_cachedAdvancedEq);
        m_advancedEdited = !eqStateIsFlat(m_cachedAdvancedEq);
        // Any loaded Advanced EQ (preset / AutoEQ) stays Advanced-only until Reset/flat.
        m_advancedPresetLocked = !eqStateIsFlat(m_cachedAdvancedEq);
        m_cachedAdvancedPresetLocked = m_advancedPresetLocked;
    } else {
        m_cachedAdvancedEq = EqResponse::simpleToAdvanced(state.gainsDb);
        m_hasCachedAdvanced = true;
        m_simpleEditedSinceAdvanced = false;
        captureAdvancedEntryBaseline(m_cachedAdvancedEq);
        m_advancedEdited = false;
        m_advancedPresetLocked = false;
        m_cachedAdvancedPresetLocked = false;
    }
}

void MainWindow::captureAdvancedEntryBaseline(const EqState &state)
{
    m_advancedEntryBaseline = state;
    m_advancedEntryBaseline.advanced = true;
}

bool MainWindow::eqStateIsFlat(const EqState &state)
{
    if (state.advanced) {
        for (int i = 0; i < state.filterCount; ++i) {
            if (std::fabs(state.filters[static_cast<size_t>(i)].gainDb) > 0.05f) {
                return false;
            }
        }
        return true;
    }
    for (float gain : state.gainsDb) {
        if (std::fabs(gain) > 0.05f) {
            return false;
        }
    }
    return true;
}

bool MainWindow::eqStatesMatch(const EqState &a, const EqState &b)
{
    if (a.filterCount != b.filterCount) {
        return false;
    }
    for (int i = 0; i < EqState::kBandCount; ++i) {
        if (std::fabs(a.gainsDb[static_cast<size_t>(i)] - b.gainsDb[static_cast<size_t>(i)]) > 0.05f) {
            return false;
        }
    }
    for (int i = 0; i < a.filterCount; ++i) {
        const EqFilter &fa = a.filters[static_cast<size_t>(i)];
        const EqFilter &fb = b.filters[static_cast<size_t>(i)];
        if (fa.type != fb.type
            || std::fabs(fa.freqHz - fb.freqHz) > 0.5f
            || std::fabs(fa.gainDb - fb.gainDb) > 0.05f
            || std::fabs(fa.q - fb.q) > 0.02f) {
            return false;
        }
    }
    return a.balance == b.balance;
}

bool MainWindow::canLeaveAdvancedMode() const
{
    const EqState current = m_parametricPanel ? m_parametricPanel->eqState() : m_cachedAdvancedEq;
    if (eqStateIsFlat(current)) {
        return true;
    }
    // Loaded Advanced / AutoEQ presets cannot be used in Simple, even if untouched.
    if (m_advancedPresetLocked) {
        return false;
    }
    // Simple→Advanced peek with no real edits may return to Simple.
    if (!m_advancedEdited || eqStatesMatch(current, m_advancedEntryBaseline)) {
        return true;
    }
    return false;
}

void MainWindow::setEqUiModeAdvanced(bool advanced, bool convertState)
{
    m_eqUiModeAdvanced = advanced;
    if (m_simpleModeButton) {
        m_simpleModeButton->setChecked(!advanced);
    }
    if (m_advancedModeButton) {
        m_advancedModeButton->setChecked(advanced);
    }
    if (m_eqModeStack) {
        m_eqModeStack->setCurrentIndex(advanced ? 1 : 0);
    }

    if (convertState) {
        if (advanced) {
            m_cachedSimpleGains = readSliderGains();
            const bool convertFromSimple = m_simpleEditedSinceAdvanced || !m_hasCachedAdvanced;
            if (convertFromSimple) {
                m_cachedAdvancedEq = EqResponse::simpleToAdvanced(m_cachedSimpleGains);
                m_hasCachedAdvanced = true;
                m_simpleEditedSinceAdvanced = false;
                m_advancedEdited = false;
                m_advancedPresetLocked = false;
                m_cachedAdvancedPresetLocked = false;
            } else {
                m_advancedEdited = !eqStateIsFlat(m_cachedAdvancedEq);
                m_advancedPresetLocked = m_cachedAdvancedPresetLocked;
            }
            if (m_parametricPanel) {
                m_parametricPanel->setEqState(m_cachedAdvancedEq);
            }
            captureAdvancedEntryBaseline(m_cachedAdvancedEq);
        } else if (m_parametricPanel) {
            m_cachedAdvancedEq = m_parametricPanel->eqState();
            m_cachedAdvancedEq.advanced = true;
            m_hasCachedAdvanced = true;
            m_cachedAdvancedPresetLocked = m_advancedPresetLocked;
            m_loadingSliders = true;
            applyGainsToSliders(m_cachedSimpleGains);
            m_loadingSliders = false;
        }
    }

    if (m_masterSlider) {
        m_masterSlider->setEnabled(!advanced);
    }
}

void MainWindow::persistEqUiMode()
{
    AppSettings settings = m_settingsStore.settings();
    settings.eqUiModeAdvanced = m_eqUiModeAdvanced;
    m_settingsStore.setSettings(settings);
    m_settingsStore.save();
}

void MainWindow::onEqModeToggled()
{
    const bool wantAdvanced = m_advancedModeButton && m_advancedModeButton->isChecked();
    if (wantAdvanced == m_eqUiModeAdvanced) {
        return;
    }

    if (m_eqUiModeAdvanced && !wantAdvanced && !canLeaveAdvancedMode()) {
        QMessageBox::information(
            this,
            QStringLiteral("Advanced preset"),
            QStringLiteral(
                "An Advanced preset can't be used in Simple mode.\n\n"
                "Simple uses 10 fixed bands; Advanced uses parametric filters "
                "that don't convert cleanly.\n\n"
                "Stay in Advanced, or Reset to 0 dB if you want to switch back to Simple."));
        if (m_advancedModeButton) {
            m_advancedModeButton->setChecked(true);
        }
        if (m_simpleModeButton) {
            m_simpleModeButton->setChecked(false);
        }
        return;
    }

    setEqUiModeAdvanced(wantAdvanced, true);
    persistEqUiMode();
    m_lastEqSnapshot = readEqState();

    if (m_eqSessionManager && m_sessionList) {
        const unsigned long pid = m_sessionList->selectedProcessId();
        if (pid != 0) {
            m_eqSessionManager->scheduleLiveGainsForProcess(pid);
        }
    }
}

void MainWindow::onParametricEqChanged()
{
    if (m_loadingSliders || !m_eqUiModeAdvanced) {
        return;
    }
    markCurrentPresetDirty();
    if (m_parametricPanel) {
        m_cachedAdvancedEq = m_parametricPanel->eqState();
        m_cachedAdvancedEq.advanced = true;
        m_hasCachedAdvanced = true;
        m_simpleEditedSinceAdvanced = false;
        m_advancedEdited = !eqStateIsFlat(m_cachedAdvancedEq)
                           && !eqStatesMatch(m_cachedAdvancedEq, m_advancedEntryBaseline);
        if (m_advancedEdited) {
            // Manual Advanced edits also lock Simple until Reset/flat.
            m_advancedPresetLocked = !eqStateIsFlat(m_cachedAdvancedEq);
            m_cachedAdvancedPresetLocked = m_advancedPresetLocked;
        }
    }
    if (m_eqSessionManager && m_sessionList) {
        const unsigned long pid = m_sessionList->selectedProcessId();
        if (pid != 0) {
            m_eqSessionManager->scheduleLiveGainsForProcess(pid);
        }
    }
    if (!m_applyingEqHistory) {
        m_lastEqSnapshot = readEqState();
    }
}

void MainWindow::setupEqHistory()
{
    auto *editMenu = new QMenu(QStringLiteral("Edit"), this);
    menuBar()->addMenu(editMenu);

    m_undoAction = editMenu->addAction(QStringLiteral("Undo"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_undoAction->setShortcutContext(Qt::WindowShortcut);
    connect(m_undoAction, &QAction::triggered, this, &MainWindow::onUndoEq);

    m_redoAction = editMenu->addAction(QStringLiteral("Redo"));
    m_redoAction->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Y),
                                QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z),
                                QKeySequence::Redo});
    m_redoAction->setShortcutContext(Qt::WindowShortcut);
    connect(m_redoAction, &QAction::triggered, this, &MainWindow::onRedoEq);
    addAction(m_undoAction);
    addAction(m_redoAction);
    qApp->installEventFilter(this);

    m_updateAction = menuBar()->addAction(QStringLiteral("Update"));
    connect(m_updateAction, &QAction::triggered, this, &MainWindow::onUpdateClicked);
    m_changelogAction = menuBar()->addAction(QStringLiteral("Changelog"));
    connect(m_changelogAction, &QAction::triggered, this, &MainWindow::onChangelogClicked);
    m_addonsMenu = new QMenu(QStringLiteral("Add-ons"), this);
    menuBar()->addMenu(m_addonsMenu);
    connect(m_addonsMenu, &QMenu::aboutToShow, this, [this]() {
        const unsigned long pid = m_sessionList ? m_sessionList->selectedProcessId() : 0;
        const QString exePath = pid == 0 ? QString() : AppIconProvider::executablePathForProcess(pid);
        populateAddonsMenu(m_addonsMenu, m_addonManager, pid, exePath, this);
    });

    m_lastEqSnapshot = readEqState();
    updateEqHistoryActions();
}

void MainWindow::setupUpdateChecker()
{
    if (!m_updateAction) {
        return;
    }

    m_updateChecker = new UpdateChecker(this);
    connect(m_updateChecker, &UpdateChecker::updateAvailable, this,
            [this](const QString &version, const QUrl &installerUrl) {
                m_pendingInstallerUrl = installerUrl;
                m_updating = false;
                m_updateAction->setText(QStringLiteral("Update"));
                m_updateAction->setEnabled(true);
                if (m_tray) {
                    m_tray->showUpdateAvailableMessage(version);
                }
                appendLog(QStringLiteral("INFO"),
                          QStringLiteral("Update available: %1").arg(version));
            });
    connect(m_updateChecker, &UpdateChecker::upToDate, this, [this]() {
        m_pendingInstallerUrl.clear();
        m_updating = false;
        m_updateAction->setText(QStringLiteral("Up to date"));
        m_updateAction->setEnabled(false);
    });
    connect(m_updateChecker, &UpdateChecker::checkFailed, this, [this](const QString &error) {
        m_updating = false;
        m_updateAction->setText(QStringLiteral("Update"));
        m_updateAction->setEnabled(true);
        appendLog(QStringLiteral("WARN"), QStringLiteral("Update check failed: %1").arg(error));
    });
    connect(m_updateChecker, &UpdateChecker::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                if (!m_updateAction || total <= 0) {
                    return;
                }
                const int percent = static_cast<int>((received * 100) / total);
                m_updateAction->setText(QStringLiteral("Updating %1%").arg(percent));
            });
    connect(m_updateChecker, &UpdateChecker::installReady, this, [this](const QString &installerPath) {
        QString error;
        if (!UpdateChecker::launchInstaller(installerPath, &error)) {
            m_updating = false;
            m_updateAction->setText(QStringLiteral("Update"));
            m_updateAction->setEnabled(true);
            appendLog(QStringLiteral("WARN"), error);
            return;
        }
        appendLog(QStringLiteral("INFO"), QStringLiteral("Installing update…"));
        onQuitApp();
    });
    connect(m_updateChecker, &UpdateChecker::updateFailed, this, [this](const QString &error) {
        m_updating = false;
        m_updateAction->setText(QStringLiteral("Update"));
        m_updateAction->setEnabled(true);
        appendLog(QStringLiteral("WARN"), QStringLiteral("Update failed: %1").arg(error));
    });
    if (m_tray) {
        connect(m_tray, &TrayController::updateRequested, this, &MainWindow::onUpdateClicked);
    }

    connect(m_updateChecker, &UpdateChecker::changelogReady, this, [this](const QString &markdown) {
        const QString current = UpdateChecker::currentVersion();
        const QString since = m_changelogAutoShow ? m_changelogSinceVersion : QString();
        QString notes = UpdateChecker::extractRelevantChangelog(markdown, since, current);
        if (notes.isEmpty()) {
            notes = QStringLiteral("No changelog entries found for this version.");
        }
        if (m_changelogAction) {
            m_changelogAction->setEnabled(true);
        }
        showChangelogDialog(notes);
        if (m_changelogAutoShow) {
            markChangelogShown();
            m_changelogAutoShow = false;
        }
    });
    connect(m_updateChecker, &UpdateChecker::changelogFailed, this, [this](const QString &error) {
        const bool autoShow = m_changelogAutoShow;
        m_changelogAutoShow = false;
        if (m_changelogAction) {
            m_changelogAction->setEnabled(true);
        }
        appendLog(QStringLiteral("WARN"), QStringLiteral("Changelog download failed: %1").arg(error));
        if (!autoShow) {
            showCopyableError(QStringLiteral("Changelog"), error);
        }
    });

    AppSettings settings = m_settingsStore.settings();
    const QString current = UpdateChecker::currentVersion();
    if (settings.lastShownChangelogVersion.isEmpty()) {
        settings.lastShownChangelogVersion = current;
        m_settingsStore.setSettings(settings);
        m_settingsStore.save();
    } else if (settings.lastShownChangelogVersion != current) {
        m_changelogAutoShow = true;
        m_changelogSinceVersion = settings.lastShownChangelogVersion;
        if (m_changelogAction) {
            m_changelogAction->setEnabled(false);
        }
        QTimer::singleShot(1800, this, [this]() {
            if (m_updateChecker) {
                m_updateChecker->fetchChangelog();
            }
        });
    }

    QTimer::singleShot(1500, this, [this]() {
        if (m_updateChecker) {
            m_updateChecker->check();
        }
    });
}

void MainWindow::onUpdateClicked()
{
    if (m_updating) {
        return;
    }
    if (m_pendingInstallerUrl.isValid() && m_updateChecker) {
        m_updating = true;
        m_updateAction->setText(QStringLiteral("Updating…"));
        m_updateAction->setEnabled(false);
        m_updateChecker->startUpdate(m_pendingInstallerUrl);
        return;
    }
    if (m_updateChecker) {
        m_updateAction->setText(QStringLiteral("Update"));
        m_updateAction->setEnabled(true);
        m_updateChecker->check();
    }
}

void MainWindow::onChangelogClicked()
{
    if (!m_updateChecker) {
        return;
    }
    m_changelogAutoShow = false;
    m_changelogSinceVersion.clear();
    if (m_changelogAction) {
        m_changelogAction->setEnabled(false);
    }
    m_updateChecker->fetchChangelog();
}

void MainWindow::markChangelogShown()
{
    AppSettings settings = m_settingsStore.settings();
    settings.lastShownChangelogVersion = UpdateChecker::currentVersion();
    m_settingsStore.setSettings(settings);
    m_settingsStore.save();
}

void MainWindow::showChangelogDialog(const QString &markdown)
{
    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(QStringLiteral("What's new in CurvioEQ %1").arg(UpdateChecker::currentVersion()));
    dialog->resize(560, 480);
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    auto *layout = new QVBoxLayout(dialog);
    auto *browser = new QTextBrowser(dialog);
    browser->setOpenExternalLinks(true);
    browser->setMarkdown(markdown);
    layout->addWidget(browser, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::close);
    layout->addWidget(buttons);

    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (isEqHistoryShortcutWindow(watched, this)) {
            const bool undo = isEqUndoShortcut(key);
            const bool redo = !undo && isEqRedoShortcut(key);
            if (undo || redo) {
                if (event->type() == QEvent::ShortcutOverride) {
                    event->accept();
                    return true;
                }
                if (undo) {
                    onUndoEq();
                } else {
                    onRedoEq();
                }
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::updateBalanceLabels(int value)
{
    if (m_balanceLeftValueLabel) {
        m_balanceLeftValueLabel->setText(value < 0 ? QStringLiteral("+%1").arg(-value)
                                                   : QStringLiteral("0"));
    }
    if (m_balanceValueLabel) {
        m_balanceValueLabel->setText(value > 0 ? QStringLiteral("+%1").arg(value) : QStringLiteral("0"));
    }
}

void MainWindow::beginUserEqEdit()
{
    if (m_loadingSliders || m_applyingEqHistory || m_eqHistoryCoalescing) {
        return;
    }
    markCurrentPresetDirty();
    m_eqHistory.push(m_lastEqSnapshot);
    m_eqHistoryCoalescing = true;
    updateEqHistoryActions();
}

void MainWindow::endUserEqEdit()
{
    if (!m_eqHistoryCoalescing) {
        return;
    }
    m_eqHistoryCoalescing = false;
    m_lastEqSnapshot = readEqState();
    if (m_eqHistory.canUndo() && eqStatesMatch(m_eqHistory.undoTop(), m_lastEqSnapshot)) {
        m_eqHistory.popUndo();
    }
    updateEqHistoryActions();
}

void MainWindow::applyEqHistoryState(const EqState &state)
{
    m_applyingEqHistory = true;
    m_loadingSliders = true;

    EqState applied = state;
    if (m_eqUiModeAdvanced) {
        applied.advanced = true;
        if (applied.filterCount <= 0) {
            applied = EqResponse::simpleToAdvanced(applied.gainsDb);
            applied.advanced = true;
        }
        syncEqModeCachesFromState(applied);
        if (m_parametricPanel) {
            m_parametricPanel->setEqState(applied);
        }
        applyGainsToSliders(applied.gainsDb);
    } else {
        applied.advanced = false;
        syncEqModeCachesFromState(applied);
        applyGainsToSliders(applied.gainsDb);
    }

    m_loadingSliders = false;
    m_lastEqSnapshot = readEqState();
    m_eqHistoryCoalescing = false;
    m_applyingEqHistory = false;

    if (m_eqSessionManager && m_sessionList) {
        const unsigned long pid = m_sessionList->selectedProcessId();
        if (pid != 0) {
            m_eqSessionManager->saveDraftForProcess(pid,
                                                    readEqState(),
                                                    readVirtualSurroundState(),
                                                    readDynamicRangeState(),
                                                    readAudioChainOrder());
        }
    }
    updateEqHistoryActions();
    markCurrentPresetDirty();
}

void MainWindow::onUndoEq()
{
    if (!m_eqHistory.canUndo() || m_applyingEqHistory || m_eqHistoryCoalescing) {
        return;
    }
    applyEqHistoryState(m_eqHistory.undo(readEqState()));
}

void MainWindow::onRedoEq()
{
    if (!m_eqHistory.canRedo() || m_applyingEqHistory || m_eqHistoryCoalescing) {
        return;
    }
    applyEqHistoryState(m_eqHistory.redo(readEqState()));
}

void MainWindow::updateEqHistoryActions()
{
    if (m_undoAction) {
        m_undoAction->setEnabled(m_eqHistory.canUndo());
    }
    if (m_redoAction) {
        m_redoAction->setEnabled(m_eqHistory.canRedo());
    }
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

    auto *logRow = new QHBoxLayout();
    logRow->addStretch();
    m_clearLogButton = new QPushButton(QStringLiteral("Clear log"), this);
    m_clearLogButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(m_clearLogButton, &QPushButton::clicked, this, &MainWindow::onClearLogClicked);
    logRow->addWidget(m_clearLogButton);
    rightLayout->addLayout(logRow, 0);

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
    leftLayout->addWidget(m_dynamicsGroup, 0);

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
    if (m_hrtfPresetCombo) {
        m_hrtfPresetCombo->setEnabled(enabled);
    }
    if (m_hrtfStrengthSlider) {
        m_hrtfStrengthSlider->setEnabled(enabled);
    }
    if (m_hrtfStrengthValueLabel) {
        m_hrtfStrengthValueLabel->setEnabled(enabled);
    }
    for (int i = 0; i < SurroundProcessor::kChannelCount; ++i) {
        QSpinBox *spin = m_surroundSpins[static_cast<size_t>(i)];
        if (!spin) {
            continue;
        }
        if (i == SurroundProcessor::Lfe) {
            spin->setEnabled(false);
            continue;
        }
        spin->setEnabled(enabled);
    }
}

void MainWindow::updateDynamicsControlsEnabled()
{
    const bool enabled = m_dynamicsEnableCheckBox && m_dynamicsEnableCheckBox->isChecked();
    if (m_resetDynamicsButton) {
        m_resetDynamicsButton->setEnabled(enabled);
    }
    if (m_dynamicsAmountSlider) {
        m_dynamicsAmountSlider->setEnabled(enabled);
    }
    if (m_dynamicsModeLabel) {
        m_dynamicsModeLabel->setEnabled(enabled);
    }
    if (m_loudnessAmountSlider) {
        m_loudnessAmountSlider->setEnabled(enabled);
    }
    if (m_loudnessTargetLabel) {
        m_loudnessTargetLabel->setEnabled(enabled);
    }
}

VirtualSurroundSettings MainWindow::readVirtualSurroundState() const
{
    VirtualSurroundSettings settings;
    settings.enabled = m_surroundEnableCheckBox && m_surroundEnableCheckBox->isChecked();
    settings.presetId = m_hrtfPresetCombo ? m_hrtfPresetCombo->currentData().toInt() : 0;
    settings.strength = m_hrtfStrengthSlider ? m_hrtfStrengthSlider->value() : 75;
    for (int i = 0; i < SurroundProcessor::kChannelCount; ++i) {
        QSpinBox *spin = m_surroundSpins[static_cast<size_t>(i)];
        settings.channelLevels[static_cast<size_t>(i)] = spin ? spin->value() : 50;
    }
    return settings;
}

void MainWindow::applySurroundToUi(const VirtualSurroundSettings &settings)
{
    if (m_surroundEnableCheckBox) {
        m_surroundEnableCheckBox->setChecked(settings.enabled);
    }
    if (m_hrtfPresetCombo) {
        const int index = m_hrtfPresetCombo->findData(settings.presetId);
        if (index >= 0) {
            m_hrtfPresetCombo->setCurrentIndex(index);
        }
    }
    if (m_hrtfStrengthSlider) {
        m_hrtfStrengthSlider->setValue(settings.strength);
    }
    if (m_hrtfStrengthValueLabel) {
        m_hrtfStrengthValueLabel->setText(QStringLiteral("%1%").arg(settings.strength));
    }
    for (int i = 0; i < SurroundProcessor::kChannelCount; ++i) {
        if (QSpinBox *spin = m_surroundSpins[static_cast<size_t>(i)]) {
            spin->setValue(settings.channelLevels[static_cast<size_t>(i)]);
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
    const VirtualSurroundSettings state = readVirtualSurroundState();
    settings.surroundEnabled = state.enabled;
    settings.hrtfPresetId = state.presetId;
    settings.hrtfStrength = state.strength;
    settings.surroundChannelLevels = state.channelLevels;
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
    settings.spectrumLimiterDb = m_spectrumWidget->limiterCeilingDb();
    m_settingsStore.setSettings(settings);
    m_settingsStore.save();
}

DynamicRangeSettings MainWindow::readDynamicRangeState() const
{
    DynamicRangeSettings settings;
    settings.enabled = m_dynamicsEnableCheckBox && m_dynamicsEnableCheckBox->isChecked();
    settings.amount = m_dynamicsAmountSlider ? m_dynamicsAmountSlider->value() : DynamicRangeSettings::kAmountDefault;
    settings.loudnessAmount =
        m_loudnessAmountSlider ? m_loudnessAmountSlider->value() : DynamicRangeSettings::kLoudnessDefault;
    return settings;
}

void MainWindow::applyDynamicRangeToUi(const DynamicRangeSettings &settings)
{
    if (m_dynamicsEnableCheckBox) {
        m_dynamicsEnableCheckBox->setChecked(settings.enabled);
    }
    if (m_dynamicsAmountSlider) {
        m_dynamicsAmountSlider->setValue(settings.amount);
    }
    if (m_dynamicsModeLabel) {
        m_dynamicsModeLabel->setText(dynamicsModeLabelForAmount(settings.amount));
    }
    if (m_loudnessAmountSlider) {
        m_loudnessAmountSlider->setValue(settings.loudnessAmount);
    }
    if (m_loudnessTargetLabel) {
        m_loudnessTargetLabel->setText(loudnessTargetLabelForAmount(settings.loudnessAmount));
    }
    updateDynamicsControlsEnabled();
}

void MainWindow::applyDynamicRangeToEngine()
{
    if (!m_eqSessionManager || !m_sessionList) {
        return;
    }
    const unsigned long pid = m_sessionList->selectedProcessId();
    if (pid == 0) {
        return;
    }
    m_eqSessionManager->pushLiveDynamicsForProcess(pid);
}

void MainWindow::saveDynamicRangeSettings()
{
    AppSettings settings = m_settingsStore.settings();
    const DynamicRangeSettings state = readDynamicRangeState();
    settings.dynamicsEnabled = state.enabled;
    settings.dynamicsAmount = state.amount;
    settings.dynamicsLoudnessAmount = state.loudnessAmount;
    m_settingsStore.setSettings(settings);
    m_settingsStore.save();
}

void MainWindow::onResetDynamicsClicked()
{
    DynamicRangeSettings settings;
    applyDynamicRangeToUi(settings);
    markCurrentPresetDirty();
    applyDynamicRangeToEngine();
    saveDynamicRangeSettings();
    appendLog(QStringLiteral("INFO"), QStringLiteral("Dynamics settings reset to defaults"));
}

AudioChainOrder MainWindow::readAudioChainOrder() const
{
    if (m_addonManager && m_sessionList) {
        const unsigned long pid = m_sessionList->selectedProcessId();
        if (pid != 0) {
            const Vst3AppAddons addons =
                m_addonManager->addonsForExe(AppIconProvider::executablePathForProcess(pid));
            if (addons.occupiedCount() > 0) {
                return normalizeAudioChainOrder(addons.chain);
            }
        }
    }
    return normalizeAudioChainOrder(m_audioChainOrder);
}

void MainWindow::applyAudioChainToUi(const AudioChainOrder &order)
{
    const AudioChainOrder normalized = normalizeAudioChainOrder(order);
    if (m_addonManager && m_sessionList) {
        const unsigned long pid = m_sessionList->selectedProcessId();
        if (pid != 0) {
            const QString exePath = AppIconProvider::executablePathForProcess(pid);
            if (m_addonManager->addonsForExe(exePath).occupiedCount() > 0) {
                m_addonManager->setChain(pid, exePath, normalized);
                return;
            }
        }
    }
    m_audioChainOrder = builtinsOnly(normalized);
}

void MainWindow::applyAudioChainToEngine()
{
    if (!m_eqSessionManager || !m_sessionList) {
        return;
    }
    const unsigned long pid = m_sessionList->selectedProcessId();
    if (pid == 0) {
        return;
    }
    m_eqSessionManager->pushLiveAudioChainForProcess(pid);
}

void MainWindow::saveAudioChainSettings()
{
    AppSettings settings = m_settingsStore.settings();
    settings.audioChainOrder = builtinsOnly(m_audioChainOrder);
    m_settingsStore.setSettings(settings);
    m_settingsStore.save();
}

void MainWindow::onAudioChainClicked()
{
    QStringList addonNames;
    addonNames.reserve(kAudioChainAddonCount);
    if (m_addonManager && m_sessionList) {
        const unsigned long pid = m_sessionList->selectedProcessId();
        if (pid != 0) {
            const Vst3AppAddons addons =
                m_addonManager->addonsForExe(AppIconProvider::executablePathForProcess(pid));
            for (int i = 0; i < kAudioChainAddonCount; ++i) {
                addonNames.append(addons.pluginSlots[static_cast<size_t>(i)].occupied
                                      ? addons.pluginSlots[static_cast<size_t>(i)].name
                                      : QString());
            }
        }
    }
    AudioChainDialog dialog(readAudioChainOrder(), addonNames, this);
    connect(&dialog, &AudioChainDialog::orderChanged, this, [this](const AudioChainOrder &order) {
        markCurrentPresetDirty();
        applyAudioChainToUi(order);
        applyAudioChainToEngine();
        saveAudioChainSettings();
    });
    dialog.exec();
}

void MainWindow::onResetSurroundClicked()
{
    VirtualSurroundSettings settings;
    settings.enabled = true;
    settings.presetId = static_cast<int>(HrtfPresetId::Default);
    settings.strength = 75;
    settings.channelLevels = defaultVirtualSurroundChannelLevels();
    applySurroundToUi(settings);
    markCurrentPresetDirty();
    applySurroundToEngine();
    saveSurroundSettings();
    appendLog(QStringLiteral("INFO"), QStringLiteral("Virtual surround speaker levels reset to 50"));
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
    applyStartupPresetsToVisibleSessions();
    m_sessionList->setEqSessions(m_eqSessionManager->activeSessionColors());
}

void MainWindow::markCurrentPresetDirty()
{
    if (m_presetPanel) {
        m_presetPanel->markDirty();
    }
}

void MainWindow::attachAddonsForProcess(unsigned long processId)
{
    if (!m_addonManager || processId == 0) {
        return;
    }
    m_addonManager->attachToSession(processId, AppIconProvider::executablePathForProcess(processId),
                                    static_cast<float>(m_settingsStore.settings().sampleRate));
}

bool MainWindow::enableEqWithPreset(unsigned long processId, const EqPreset &preset)
{
    if (!m_eqSessionManager || processId == 0) {
        return false;
    }

    EqSessionStartSettings settings;
    settings.eq = preset.hasEq ? preset.eq : readEqState();
    settings.virtualSurround = preset.hasSurround ? preset.surround : readVirtualSurroundState();
    settings.dynamicRange = preset.hasDynamics ? preset.dynamics : readDynamicRangeState();
    settings.audioChainOrder = preset.hasAudioChain ? preset.audioChainOrder : readAudioChainOrder();

    const bool selected = m_sessionList && m_sessionList->selectedProcessId() == processId;
    if (selected && m_presetPanel) {
        m_presetPanel->applyPresetToUi(preset);
        m_presetPanel->markClean(preset.id);
    }

    if (m_eqSessionManager->isRunning(processId)) {
        m_eqSessionManager->saveDraftForProcess(processId, settings.eq, settings.virtualSurround,
                                                settings.dynamicRange, settings.audioChainOrder);
        return true;
    }

    if (!m_eqSessionManager->enableForProcess(processId, settings)) {
        return false;
    }
    attachAddonsForProcess(processId);
    return true;
}

void MainWindow::bindStartupPresetForExe(const QString &exePath)
{
    if (!m_presetPanel || exePath.isEmpty()) {
        return;
    }

    EqPreset preset;
    if (!m_presetPanel->ensureNamedPreset(&preset)) {
        return;
    }

    if (!m_startupPresetStore.setBinding(exePath, preset.id)) {
        showCopyableError(QStringLiteral("Start at app startup"),
                          QStringLiteral("Could not save the startup preset binding."));
        return;
    }

    appendLog(QStringLiteral("INFO"),
              QStringLiteral("Start at app startup: %1 → %2")
                  .arg(QFileInfo(exePath).fileName(), preset.name));

    if (!m_sessionList) {
        return;
    }
    for (unsigned long pid : m_sessionList->processIds()) {
        if (StartupPresetStore::normalizeExe(AppIconProvider::executablePathForProcess(pid))
            == StartupPresetStore::normalizeExe(exePath)) {
            enableEqWithPreset(pid, preset);
        }
    }
    refreshSessionList();
    updateSpectrumForSelection();
    updateEqControlState();
}

void MainWindow::applyStartupPresetsToVisibleSessions()
{
    if (!m_sessionList || !m_eqSessionManager) {
        return;
    }

    QSet<unsigned long> visible;
    for (unsigned long pid : m_sessionList->processIds()) {
        visible.insert(pid);
        if (m_eqSessionManager->isRunning(pid) || m_startupApplyAttempted.contains(pid)) {
            continue;
        }
        const QString exePath = AppIconProvider::executablePathForProcess(pid);
        const QString presetId = m_startupPresetStore.presetIdForExe(exePath);
        if (presetId.isEmpty()) {
            continue;
        }
        const EqPreset preset = m_presetStore.presetById(presetId);
        if (preset.id.isEmpty()) {
            m_startupPresetStore.removeBinding(exePath);
            continue;
        }
        m_startupApplyAttempted.insert(pid);
        enableEqWithPreset(pid, preset);
    }

    m_startupApplyAttempted.intersect(visible);
}

void MainWindow::handleStartAtAppStartup(const QString &exePath)
{
    onShowWindow();
    bindStartupPresetForExe(exePath);
}

void MainWindow::onResetClicked()
{
    beginUserEqEdit();
    m_loadingSliders = true;
    EqState flat;
    if (m_eqUiModeAdvanced && m_parametricPanel) {
        flat.advanced = true;
        flat = EqResponse::simpleToAdvanced(flat.gainsDb);
        m_parametricPanel->setEqState(flat);
    } else {
        applyGainsToSliders({});
    }
    syncEqModeCachesFromState(flat);
    if (m_balanceSlider) {
        m_balanceSlider->setValue(AppConstants::kDefaultBalance);
    }
    m_loadingSliders = false;
    resetMasterSlider();

    if (m_eqSessionManager && m_sessionList) {
        const unsigned long pid = m_sessionList->selectedProcessId();
        if (pid != 0) {
            m_eqSessionManager->saveDraftForProcess(pid,
                                                    readEqState(),
                                                    readVirtualSurroundState(),
                                                    readDynamicRangeState(),
                                                    readAudioChainOrder());
        }
    }

    appendLog(QStringLiteral("INFO"), QStringLiteral("EQ bands reset to 0 dB"));
    AudioLog::info(QStringLiteral("MainWindow"), QStringLiteral("Reset sliders to 0 dB"));
    endUserEqEdit();
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
        if (m_addonManager) {
            m_addonManager->attachToSession(processId,
                                            AppIconProvider::executablePathForProcess(processId),
                                            static_cast<float>(m_settingsStore.settings().sampleRate));
        }
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

void MainWindow::onClearLogClicked()
{
    ui->logTextEdit->clear();
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
    const AppSettings previous = m_settingsStore.settings();
    const EngineIoSettings previousIo = previous.engineIo();
    const EngineIoSettings nextIo = settings.engineIo();
    const bool ioChanged = previousIo != nextIo
                           || previous.eqOutputDeviceId != settings.eqOutputDeviceId
                           || previous.routingSinkDeviceId != settings.routingSinkDeviceId;

    m_settingsStore.setSettings(settings);
    m_settingsStore.save();
    m_audioEngine.setEngineSettings(nextIo);

    VirtualSurroundSettings surroundSettings;
    surroundSettings.enabled = settings.surroundEnabled;
    surroundSettings.presetId = settings.hrtfPresetId;
    surroundSettings.strength = settings.hrtfStrength;
    surroundSettings.channelLevels = settings.surroundChannelLevels;
    applySurroundToUi(surroundSettings);
    applySurroundToEngine();

    DynamicRangeSettings dynamicRangeSettings;
    dynamicRangeSettings.enabled = settings.dynamicsEnabled;
    dynamicRangeSettings.amount = settings.dynamicsAmount;
    dynamicRangeSettings.loudnessAmount = settings.dynamicsLoudnessAmount;
    applyDynamicRangeToUi(dynamicRangeSettings);
    applyDynamicRangeToEngine();

    applyAudioChainToUi(settings.audioChainOrder);
    applyAudioChainToEngine();
    if (m_addonManager) {
        m_addonManager->setExtraFolders(settings.vst3ExtraFolders);
    }

    if (m_spectrumWidget) {
        m_spectrumWidget->setSpectrumEnabled(settings.spectrumEnabled);
        m_spectrumWidget->setLimiterCeilingDb(settings.spectrumLimiterDb);
    }
    m_audioEngine.setOutputLimiterThreshold(MixLimiter::dbToLinear(settings.spectrumLimiterDb));

    setEqUiModeAdvanced(settings.eqUiModeAdvanced, false);

    QString startupError;
    if (!SettingsStore::applyStartWithWindows(settings.startWithWindows, &startupError)) {
        showCopyableError(QStringLiteral("Startup setting failed"), startupError);
    }

    applyKeybindSettings();

    if (ioChanged && m_eqSessionManager && m_eqSessionManager->isAnyRunning()) {
        m_eqSessionManager->restartActiveSessions();
        if (m_addonManager) {
            const float mixRate = static_cast<float>(settings.sampleRate);
            for (unsigned long pid : m_audioEngine.activeProcessIds()) {
                m_addonManager->attachToSession(pid, AppIconProvider::executablePathForProcess(pid), mixRate);
            }
        }
        refreshSessionList();
        updateEqControlState();
    }
}

void MainWindow::updateEqControlState()
{
    const bool anyRunning = m_eqSessionManager && m_eqSessionManager->isAnyRunning();

    if (ui->disableAllButton) {
        ui->disableAllButton->setEnabled(anyRunning);
    }
    if (m_tray) {
        m_tray->updateEqSessions(m_eqSessionManager ? m_eqSessionManager->configuredTraySessions()
                                                    : QVector<ConfiguredEqSession>{});
    }
    updateSessionListAutoRefresh();
}

void MainWindow::updateSessionListAutoRefresh()
{
    if (!m_sessionList) {
        return;
    }
    const bool windowInUse = isVisible() && !isMinimized();
    m_sessionList->setAutoRefreshEnabled(windowInUse);
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

    if (m_masterSlider && m_masterSlider->isSliderDown()) {
        beginUserEqEdit();
    } else {
        beginUserEqEdit();
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
    markSimpleEqEdited();

    if (m_eqSessionManager && m_sessionList) {
        const unsigned long pid = m_sessionList->selectedProcessId();
        if (pid != 0) {
            m_eqSessionManager->scheduleLiveGainsForProcess(pid);
        }
    }

    if (!m_masterSlider || !m_masterSlider->isSliderDown()) {
        endUserEqEdit();
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
        [this](const EqState &eq) { applyEqStateToUi(eq); },
        [this](const VirtualSurroundSettings &settings) { applySurroundToUi(settings); },
        [this](const DynamicRangeSettings &settings) { applyDynamicRangeToUi(settings); },
        [this](const AudioChainOrder &order) { applyAudioChainToUi(order); });
    m_loadingSliders = false;
    resetMasterSlider();

    m_eqHistory.clear();
    m_eqHistoryCoalescing = false;
    m_lastEqSnapshot = readEqState();
    updateEqHistoryActions();

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

void MainWindow::applyEqStateToUi(const EqState &state)
{
    m_loadingSliders = true;
    EqState applied = state;
    // Advanced presets (any parametric filters) cannot run in Simple mode.
    if (applied.filterCount > 0) {
        applied.advanced = true;
    }
    syncEqModeCachesFromState(applied);
    setEqUiModeAdvanced(applied.advanced, false);
    if (applied.advanced && m_parametricPanel) {
        m_parametricPanel->setEqState(applied);
    } else if (m_parametricPanel && m_hasCachedAdvanced) {
        m_parametricPanel->setEqState(m_cachedAdvancedEq);
    }
    applyGainsToSliders(applied.gainsDb);
    if (m_balanceSlider) {
        const int balance = std::clamp(applied.balance,
                                       AppConstants::kMinBalance,
                                       AppConstants::kMaxBalance);
        m_balanceSlider->blockSignals(true);
        m_balanceSlider->setValue(balance);
        m_balanceSlider->blockSignals(false);
        updateBalanceLabels(balance);
    }
    m_loadingSliders = false;
}

EqState MainWindow::readEqState() const
{
    EqState state;
    if (m_eqUiModeAdvanced && m_parametricPanel) {
        state = m_parametricPanel->eqState();
    } else {
        state.advanced = false;
        state.gainsDb = readSliderGains();
    }
    if (m_balanceSlider) {
        state.balance = m_balanceSlider->value();
    }
    return state;
}

std::array<float, EqProcessor::kBandCount> MainWindow::readSliderGains() const
{
    std::array<float, EqProcessor::kBandCount> gains{};
    for (int band = 0; band < EqProcessor::kBandCount; ++band) {
        gains[static_cast<size_t>(band)] = static_cast<float>(m_bandSliders[static_cast<size_t>(band)]->value());
    }
    return gains;
}
