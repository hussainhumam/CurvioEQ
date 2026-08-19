#include "setupdialog.h"

#include "ui/audiodeviceresolver.h"

#include "audio/audiopolicyrouter.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

SetupDialog::SetupDialog(const AppSettings &current, QWidget *parent)
    : QDialog(parent)
    , m_result(current)
{
    setWindowTitle(QStringLiteral("Welcome to CurvioEQ"));
    resize(520, 280);

    auto *layout = new QVBoxLayout(this);

    m_infoLabel = new QLabel(
        QStringLiteral("CurvioEQ routes each app's original audio to a routing sink (for example VB-Cable or "
                         "Steam Streaming Speakers), captures it, applies EQ, and plays the result on your "
                         "headphones.\n\n"
                         "Choose a routing sink and your EQ output device below. You can change these later "
                         "in Settings."),
        this);
    m_infoLabel->setWordWrap(true);
    layout->addWidget(m_infoLabel);

    auto *form = new QFormLayout();
    m_routingSinkCombo = new QComboBox(this);
    form->addRow(QStringLiteral("Routing sink:"), m_routingSinkCombo);

    m_eqOutputCombo = new QComboBox(this);
    form->addRow(QStringLiteral("EQ output device:"), m_eqOutputCombo);
    layout->addLayout(form);

    populateDevices();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);
}

void SetupDialog::populateDevices()
{
    const QVector<AudioRenderDeviceInfo> devices = AudioPolicyRouter::listRenderDevices();

    AudioDeviceResolver::populateCombo(m_routingSinkCombo,
                                       devices,
                                       m_result.routingSinkDeviceId,
                                       [](QString *deviceId) {
                                           return AudioPolicyRouter::preferredRoutingSinkDevice(deviceId,
                                                                                                nullptr);
                                       });

    AudioDeviceResolver::populateCombo(m_eqOutputCombo,
                                       devices,
                                       m_result.eqOutputDeviceId,
                                       [](QString *deviceId) {
                                           return AudioPolicyRouter::preferredRenderDevice(deviceId, nullptr);
                                       });
}

AppSettings SetupDialog::resultSettings() const
{
    return m_result;
}

void SetupDialog::accept()
{
    const int sinkIndex = m_routingSinkCombo->currentIndex();
    if (sinkIndex >= 0) {
        m_result.routingSinkDeviceId = m_routingSinkCombo->itemData(sinkIndex).toString();
        m_result.routingSinkDeviceName = m_routingSinkCombo->itemText(sinkIndex);
    }

    const int outputIndex = m_eqOutputCombo->currentIndex();
    if (outputIndex >= 0) {
        m_result.eqOutputDeviceId = m_eqOutputCombo->itemData(outputIndex).toString();
        m_result.eqOutputDeviceName = m_eqOutputCombo->itemText(outputIndex);
    }

    m_result.setupCompleted = true;
    QDialog::accept();
}
