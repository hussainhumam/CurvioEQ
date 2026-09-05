#include "settingsdialog.h"

#include "ui/appconstants.h"
#include "ui/audiodeviceresolver.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
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

    m_muteRoutingSinkCheck = new QCheckBox(QStringLiteral("Mute routing sink while EQ is active"), this);
    m_muteRoutingSinkCheck->setChecked(current.muteRoutingSink);
    m_muteRoutingSinkCheck->setToolTip(
        QStringLiteral("Prevents the original app audio from leaking to your headphones when the routing sink "
                       "is monitored in Voicemeeter or similar mixers."));
    form->addRow(m_muteRoutingSinkCheck);

    m_startWithWindowsCheck = new QCheckBox(
        QStringLiteral("Start %1 when Windows starts").arg(QString::fromLatin1(AppConstants::kAppDisplayName)),
        this);
    m_startWithWindowsCheck->setChecked(current.startWithWindows);
    form->addRow(m_startWithWindowsCheck);

    m_vst3FolderList = new QListWidget(this);
    m_vst3FolderList->setMinimumHeight(70);
    for (const QString &folder : current.vst3ExtraFolders) {
        m_vst3FolderList->addItem(folder);
    }
    auto *folderButtons = new QHBoxLayout();
    auto *addFolder = new QPushButton(QStringLiteral("Add folder"), this);
    auto *removeFolder = new QPushButton(QStringLiteral("Remove"), this);
    folderButtons->addWidget(addFolder);
    folderButtons->addWidget(removeFolder);
    folderButtons->addStretch();
    connect(addFolder, &QPushButton::clicked, this, [this]() {
        const QString folder = QFileDialog::getExistingDirectory(this, QStringLiteral("VST3 folder"));
        if (!folder.isEmpty() && m_vst3FolderList) {
            m_vst3FolderList->addItem(folder);
        }
    });
    connect(removeFolder, &QPushButton::clicked, this, [this]() {
        if (!m_vst3FolderList) {
            return;
        }
        qDeleteAll(m_vst3FolderList->selectedItems());
    });
    form->addRow(QStringLiteral("Extra VST3 folders:"), m_vst3FolderList);
    form->addRow(QString(), folderButtons);

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
    m_routingSinkDevices =
        AudioPolicyRouter::listRenderDevicesExcluding(m_result.eqOutputDeviceId);
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
    m_eqOutputDevices =
        AudioPolicyRouter::listRenderDevicesExcluding(m_result.routingSinkDeviceId);
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
    m_result.muteRoutingSink = m_muteRoutingSinkCheck->isChecked();

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

    m_result.vst3ExtraFolders.clear();
    if (m_vst3FolderList) {
        for (int i = 0; i < m_vst3FolderList->count(); ++i) {
            const QString folder = m_vst3FolderList->item(i)->text().trimmed();
            if (!folder.isEmpty()) {
                m_result.vst3ExtraFolders.append(folder);
            }
        }
    }

    QDialog::accept();
}
