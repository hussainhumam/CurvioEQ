#pragma once

#include "settingsstore.h"

#include <QDialog>
#include <QVector>

#include "audio/audiopolicyrouter.h"

class QCheckBox;
class QComboBox;
class QListWidget;
class QPushButton;
class QSpinBox;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(const AppSettings &current, QWidget *parent = nullptr);

    AppSettings resultSettings() const;

private slots:
    void accept() override;
    void onRoutingSinkChanged(int index);
    void onEqOutputChanged(int index);

private:
    void populateRoutingSinkDevices();
    void populateEqOutputDevices();
    void populateAudioIoControls();
    void setAudioIoDefaults();
    void syncSampleRateCombo();
    void syncBufferSizeCombo();
    void updateSampleRateSpinState();
    void updateBufferSizeSpinState();
    void updateSafetyBufferSpinState();
    void updateCpuAffinitySpinState();
    void onSampleRateComboChanged(int index);
    void onBufferSizeComboChanged(int index);
    void onSampleRateSpinChanged(int value);
    void onBufferSizeSpinChanged(int value);
    void onSafetyBufferComboChanged(int index);
    void onSafetyBufferSpinChanged(int value);
    void onCpuAffinityComboChanged(int index);
    void onCpuAffinitySpinChanged(int value);
    void applyEngineSettingsToControls(const EngineIoSettings &io);
    EngineIoSettings engineSettingsFromControls() const;

    QCheckBox *m_startWithWindowsCheck = nullptr;
    QCheckBox *m_muteRoutingSinkCheck = nullptr;
    QComboBox *m_routingSinkCombo = nullptr;
    QComboBox *m_eqOutputCombo = nullptr;
    QComboBox *m_sampleRateCombo = nullptr;
    QSpinBox *m_sampleRateSpin = nullptr;
    QComboBox *m_bufferSizeCombo = nullptr;
    QSpinBox *m_bufferSizeSpin = nullptr;
    QComboBox *m_precisionCombo = nullptr;
    QComboBox *m_resampleCombo = nullptr;
    QComboBox *m_layoutCombo = nullptr;
    QComboBox *m_outputFormatCombo = nullptr;
    QComboBox *m_driftCombo = nullptr;
    QComboBox *m_safetyBufferCombo = nullptr;
    QSpinBox *m_safetyBufferSpin = nullptr;
    QComboBox *m_threadPriorityCombo = nullptr;
    QComboBox *m_cpuAffinityCombo = nullptr;
    QSpinBox *m_cpuAffinitySpin = nullptr;
    QComboBox *m_shareModeCombo = nullptr;
    QComboBox *m_autoRecoveryCombo = nullptr;
    QPushButton *m_resetAudioIoButton = nullptr;
    QListWidget *m_vst3FolderList = nullptr;
    QVector<AudioRenderDeviceInfo> m_routingSinkDevices;
    QVector<AudioRenderDeviceInfo> m_eqOutputDevices;
    AppSettings m_result;
    bool m_updatingCombos = false;
    bool m_updatingAudioIo = false;
    int m_customBufferFrames = AppConstants::kDefaultBufferFrames;
    int m_customSampleRate = AppConstants::kDefaultSampleRate;
};
