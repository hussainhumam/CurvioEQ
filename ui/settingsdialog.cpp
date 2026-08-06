#include "settingsdialog.h"

#include "ui/appconstants.h"
#include "ui/audiodeviceresolver.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(const AppSettings &current, QWidget *parent)
    : QDialog(parent)
    , m_result(current)
{
    setWindowTitle(QStringLiteral("Settings"));

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    m_routingSinkCombo = new QComboBox(this);
    form->addRow(QStringLiteral("Routing sink (original app audio):"), m_routingSinkCombo);

    m_eqOutputCombo = new QComboBox(this);
    form->addRow(QStringLiteral("EQ output device:"), m_eqOutputCombo);

    m_startWithWindowsCheck = new QCheckBox(
        QStringLiteral("Start %1 when Windows starts").arg(QString::fromLatin1(AppConstants::kAppDisplayName)),
        this);
    m_startWithWindowsCheck->setChecked(current.startWithWindows);
    form->addRow(m_startWithWindowsCheck);

    layout->addLayout(form);

    connect(m_routingSinkCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsDialog::onRoutingSinkChanged);
    connect(m_eqOutputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsDialog::onEqOutputChanged);

    populateRoutingSinkDevices();
    populateEqOutputDevices();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void SettingsDialog::populateRoutingSinkDevices()
{
    m_updatingCombos = true;
    const QString excludeId = m_result.eqOutputDeviceId;
    m_routingSinkDevices = AudioPolicyRouter::listRenderDevicesExcluding(excludeId);
    AudioDeviceResolver::populateCombo(m_routingSinkCombo,
                                       m_routingSinkDevices,
                                       m_result.routingSinkDeviceId,
                                       [](QString *deviceId) {
                                           return AudioPolicyRouter::preferredRoutingSinkDevice(deviceId,
                                                                                                nullptr);
                                       });
    m_updatingCombos = false;
}

void SettingsDialog::populateEqOutputDevices()
{
    m_updatingCombos = true;
    const QString excludeId = m_result.routingSinkDeviceId;
    m_eqOutputDevices = AudioPolicyRouter::listRenderDevicesExcluding(excludeId);
    AudioDeviceResolver::populateCombo(m_eqOutputCombo,
                                       m_eqOutputDevices,
                                       m_result.eqOutputDeviceId,
                                       [](QString *deviceId) {
                                           return AudioPolicyRouter::preferredRenderDevice(deviceId, nullptr);
                                       });
    m_updatingCombos = false;
}

void SettingsDialog::onRoutingSinkChanged(int index)
{
    if (m_updatingCombos || index < 0 || index >= m_routingSinkDevices.size()) {
        return;
    }

    m_result.routingSinkDeviceId = m_routingSinkDevices.at(index).id;
    m_result.routingSinkDeviceName = m_routingSinkDevices.at(index).friendlyName;
    populateEqOutputDevices();
}

void SettingsDialog::onEqOutputChanged(int index)
{
    if (m_updatingCombos || index < 0 || index >= m_eqOutputDevices.size()) {
        return;
    }

    m_result.eqOutputDeviceId = m_eqOutputDevices.at(index).id;
    m_result.eqOutputDeviceName = m_eqOutputDevices.at(index).friendlyName;
    populateRoutingSinkDevices();
}

AppSettings SettingsDialog::resultSettings() const
{
    return m_result;
}

void SettingsDialog::accept()
{
    m_result.startWithWindows = m_startWithWindowsCheck->isChecked();

    const int sinkIndex = m_routingSinkCombo->currentIndex();
    if (sinkIndex >= 0 && sinkIndex < m_routingSinkDevices.size()) {
        m_result.routingSinkDeviceId = m_routingSinkDevices.at(sinkIndex).id;
        m_result.routingSinkDeviceName = m_routingSinkDevices.at(sinkIndex).friendlyName;
    }

    const int outputIndex = m_eqOutputCombo->currentIndex();
    if (outputIndex >= 0 && outputIndex < m_eqOutputDevices.size()) {
        m_result.eqOutputDeviceId = m_eqOutputDevices.at(outputIndex).id;
        m_result.eqOutputDeviceName = m_eqOutputDevices.at(outputIndex).friendlyName;
    }

    QDialog::accept();
}
