#pragma once

#include "settingsstore.h"

#include <QDialog>
#include <QVector>

#include "audio/audiopolicyrouter.h"

class QCheckBox;
class QComboBox;

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

    QCheckBox *m_startWithWindowsCheck = nullptr;
    QCheckBox *m_muteRoutingSinkCheck = nullptr;
    QComboBox *m_routingSinkCombo = nullptr;
    QComboBox *m_eqOutputCombo = nullptr;
    QVector<AudioRenderDeviceInfo> m_routingSinkDevices;
    QVector<AudioRenderDeviceInfo> m_eqOutputDevices;
    AppSettings m_result;
    bool m_updatingCombos = false;
};
