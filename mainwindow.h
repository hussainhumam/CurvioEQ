#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "audio/audioengine.h"
#include "audio/eqprocessor.h"
#include "audio/surroundprocessor.h"
#include "audio/virtualsurroundsettings.h"
#include "audio/dynamicrangesettings.h"
#include "ui/presetstore.h"
#include "ui/settingsstore.h"
#include "ui/spectrumanalyzer.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>

#include <array>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class EqSessionManager;
class GlobalHotkeyManager;
class PresetPanelController;
class SessionListController;
class SingleInstanceServer;
class SpectrumWidget;
class TrayController;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void onRefreshClicked();
    void onResetClicked();
    void onResetSurroundClicked();
    void onApplySurroundClicked();
    void onResetDynamicsClicked();
    void onEnableEq();
    void onDisableEq();
    void onDisableAllEq();
    void onSettingsClicked();
    void onKeybindsClicked();
    void onHotkeyTriggered(int hotkeyId);
    void onColorKeybindTriggered(int colorIndex);
    void onEngineStatusChanged(const QString &message);
    void onEngineError(const QString &message);
    void onShowWindow();
    void onQuitApp();
    void onTrayToggleEq(unsigned long processId);
    void onMasterSliderChanged(int value);
    void onClearLogClicked();

private:
    void setupSurroundUi();
    void setupDynamicsUi();
    void setupEqControls();
    void restructureLayout();
    void updateSurroundControlsEnabled();
    void updateDynamicsControlsEnabled();

    std::array<float, EqProcessor::kBandCount> readSliderGains() const;
    VirtualSurroundSettings readVirtualSurroundState() const;
    DynamicRangeSettings readDynamicRangeState() const;

    void applyGainsToSliders(const std::array<float, EqProcessor::kBandCount> &gains);
    void applySurroundToUi(const VirtualSurroundSettings &settings);
    void applyDynamicRangeToUi(const DynamicRangeSettings &settings);
    void applySurroundToEngine();
    void applyDynamicRangeToEngine();
    void saveSurroundSettings();
    void saveDynamicRangeSettings();
    void saveSpectrumSettings();
    void syncSlidersToSelection();
    void updateSpectrumForSelection();

    void appendLog(const QString &level, const QString &message);
    void showDspVerificationInLog();
    void showCopyableError(const QString &title, const QString &message);
    void applySettings(const AppSettings &settings);
    void applyKeybindSettings();
    void updateEqControlState();
    void refreshSessionList();
    void resetMasterSlider();

    Ui::MainWindow *ui;
    AudioEngine m_audioEngine;
    PresetStore m_presetStore;
    SettingsStore m_settingsStore;
    SpectrumCapture m_spectrumCapture;
    SpectrumWidget *m_spectrumWidget = nullptr;
    PresetPanelController *m_presetPanel = nullptr;
    SessionListController *m_sessionList = nullptr;
    EqSessionManager *m_eqSessionManager = nullptr;
    TrayController *m_tray = nullptr;
    GlobalHotkeyManager *m_hotkeyManager = nullptr;
    SingleInstanceServer *m_singleInstance = nullptr;

    QGroupBox *m_surroundGroup = nullptr;
    QCheckBox *m_surroundEnableCheckBox = nullptr;
    QComboBox *m_hrtfPresetCombo = nullptr;
    QSlider *m_hrtfStrengthSlider = nullptr;
    QLabel *m_hrtfStrengthValueLabel = nullptr;
    QPushButton *m_resetSurroundButton = nullptr;
    QPushButton *m_applySurroundButton = nullptr;
    QPushButton *m_disableAllButton = nullptr;
    QPushButton *m_clearLogButton = nullptr;
    std::array<QSpinBox *, SurroundProcessor::kChannelCount> m_surroundSpins{};

    QGroupBox *m_dynamicsGroup = nullptr;
    QCheckBox *m_dynamicsEnableCheckBox = nullptr;
    QPushButton *m_resetDynamicsButton = nullptr;
    QSlider *m_dynamicsAmountSlider = nullptr;
    QLabel *m_dynamicsModeLabel = nullptr;
    QSlider *m_loudnessAmountSlider = nullptr;
    QLabel *m_loudnessTargetLabel = nullptr;

    std::array<QSlider *, EqProcessor::kBandCount> m_bandSliders{};
    QSlider *m_masterSlider = nullptr;
    int m_lastMasterValue = 0;
    unsigned long m_sliderEditPid = 0;
    bool m_loadingSliders = false;
    bool m_quitting = false;
};

#endif // MAINWINDOW_H
