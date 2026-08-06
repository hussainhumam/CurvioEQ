#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "audio/audioengine.h"
#include "audio/eqprocessor.h"
#include "audio/surroundprocessor.h"
#include "ui/eqsessioncontroller.h"
#include "ui/presetstore.h"
#include "ui/settingsstore.h"
#include "ui/spectrumanalyzer.h"

#include <QCheckBox>
#include <QGroupBox>
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

private slots:
    void onRefreshClicked();
    void onResetClicked();
    void onResetSurroundClicked();
    void onApplySurroundClicked();
    void onEnableEq();
    void onDisableEq();
    void onSettingsClicked();
    void onEngineStatusChanged(const QString &message);
    void onEngineError(const QString &message);
    void onShowWindow();
    void onQuitApp();

private:
    void setupSurroundUi();
    void restructureLayout();
    void updateSurroundControlsEnabled();

    std::array<float, EqProcessor::kBandCount> readSliderGains() const;
    std::array<int, SurroundProcessor::kChannelCount> readSurroundChannelLevels() const;
    std::pair<bool, std::array<int, SurroundProcessor::kChannelCount>> readSurroundState() const;

    void applySurroundToUi(bool enabled, const std::array<int, SurroundProcessor::kChannelCount> &levels);
    void applySurroundToEngine();
    void saveSurroundSettings();

    void appendLog(const QString &level, const QString &message);
    void showCopyableError(const QString &title, const QString &message);
    void applySettings(const AppSettings &settings);
    void updateEqControlState();
    void updateSpectrumUi(bool eqActive, const QString &appName);
    void refreshSessionList();

    Ui::MainWindow *ui;
    AudioEngine m_audioEngine;
    PresetStore m_presetStore;
    SettingsStore m_settingsStore;
    SpectrumCapture m_spectrumCapture;
    SpectrumWidget *m_spectrumWidget = nullptr;
    PresetPanelController *m_presetPanel = nullptr;
    SessionListController *m_sessionList = nullptr;
    EqSessionController *m_eqSession = nullptr;
    TrayController *m_tray = nullptr;
    SingleInstanceServer *m_singleInstance = nullptr;

    QGroupBox *m_surroundGroup = nullptr;
    QCheckBox *m_surroundEnableCheckBox = nullptr;
    QPushButton *m_resetSurroundButton = nullptr;
    QPushButton *m_applySurroundButton = nullptr;
    std::array<QSpinBox *, SurroundProcessor::kChannelCount> m_surroundSpins{};

    std::array<QSlider *, EqProcessor::kBandCount> m_bandSliders{};
    bool m_quitting = false;
};

#endif // MAINWINDOW_H
