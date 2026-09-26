#include "settingsdialog.h"

#include "ui/appconstants.h"
#include "ui/audiodeviceresolver.h"

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSizePolicy>
#include <QSpinBox>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>

namespace {

void addComboItem(QComboBox *combo, const QString &label, int value)
{
    if (combo) {
        combo->addItem(label, value);
    }
}

void configureNumericSpin(QSpinBox *spin)
{
    if (!spin) {
        return;
    }
    spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}

} // namespace

SettingsDialog::SettingsDialog(const AppSettings &current, QWidget *parent)
    : QDialog(parent)
    , m_result(current)
    , m_customBufferFrames(EngineIoSettings::bufferPresetFromFrames(current.bufferFrames)
                                   == BufferSizePreset::Custom
                               ? AppConstants::clampBufferFrames(current.bufferFrames)
                               : AppConstants::kDefaultBufferFrames)
    , m_customSampleRate(AppConstants::isListedSampleRate(current.sampleRate)
                             ? AppConstants::kDefaultSampleRate
                             : AppConstants::clampSampleRate(current.sampleRate))
{
    setWindowTitle(QStringLiteral("Settings"));

    auto *layout = new QVBoxLayout(this);
    auto *form = new QGridLayout();
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(8);
    form->setColumnStretch(1, 1);

    int row = 0;
    auto addLabeledField = [&](const QString &text, QWidget *field) {
        auto *label = new QLabel(text, this);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        form->addWidget(label, row, 0);
        form->addWidget(field, row, 1, 1, 2);
        ++row;
    };
    auto addComboRow = [&](const QString &text, QComboBox **comboOut, const QString &tooltip) {
        auto *combo = new QComboBox(this);
        combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        combo->setToolTip(tooltip);
        *comboOut = combo;
        addLabeledField(text, combo);
    };

    m_routingSinkCombo = new QComboBox(this);
    addLabeledField(QStringLiteral("Routing sink (original app audio):"), m_routingSinkCombo);

    m_eqOutputCombo = new QComboBox(this);
    addLabeledField(QStringLiteral("EQ output device:"), m_eqOutputCombo);

    m_muteRoutingSinkCheck = new QCheckBox(QStringLiteral("Mute routing sink while EQ is active"), this);
    m_muteRoutingSinkCheck->setChecked(current.muteRoutingSink);
    m_muteRoutingSinkCheck->setToolTip(
        QStringLiteral("Prevents the original app audio from leaking to your headphones when the routing sink "
                       "is monitored in Voicemeeter or similar mixers."));
    form->addWidget(m_muteRoutingSinkCheck, row, 0, 1, 3);
    ++row;

    m_startWithWindowsCheck = new QCheckBox(
        QStringLiteral("Start %1 when Windows starts").arg(QString::fromLatin1(AppConstants::kAppDisplayName)),
        this);
    m_startWithWindowsCheck->setChecked(current.startWithWindows);
    form->addWidget(m_startWithWindowsCheck, row, 0, 1, 3);
    ++row;

    m_sampleRateCombo = new QComboBox(this);
    m_sampleRateSpin = new QSpinBox(this);
    m_sampleRateSpin->setRange(AppConstants::kMinSampleRate, AppConstants::kMaxSampleRate);
    m_sampleRateSpin->setSingleStep(100);
    m_sampleRateSpin->setGroupSeparatorShown(true);
    m_sampleRateSpin->setAccelerated(true);

    m_bufferSizeCombo = new QComboBox(this);
    m_bufferSizeSpin = new QSpinBox(this);
    m_bufferSizeSpin->setRange(AppConstants::kMinBufferFrames, AppConstants::kMaxBufferFrames);
    m_bufferSizeSpin->setSingleStep(1);
    m_bufferSizeSpin->setGroupSeparatorShown(true);
    m_bufferSizeSpin->setAccelerated(true);

    m_safetyBufferCombo = new QComboBox(this);
    m_safetyBufferSpin = new QSpinBox(this);
    m_safetyBufferSpin->setRange(0, EngineIoSettings::kMaxSafetyExtraFrames);
    m_safetyBufferSpin->setSingleStep(16);
    m_safetyBufferSpin->setGroupSeparatorShown(true);
    m_safetyBufferSpin->setAccelerated(true);

    m_cpuAffinityCombo = new QComboBox(this);
    m_cpuAffinitySpin = new QSpinBox(this);
    const int maxCore = std::max(0, QThread::idealThreadCount() - 1);
    m_cpuAffinitySpin->setRange(0, maxCore);
    m_cpuAffinitySpin->setToolTip(
        QStringLiteral("Pin the audio threads to this CPU core. 0 is the first core."));

    const int spinWidth = std::max({m_sampleRateSpin->sizeHint().width(),
                                    m_bufferSizeSpin->sizeHint().width(),
                                    m_safetyBufferSpin->sizeHint().width(),
                                    m_cpuAffinitySpin->sizeHint().width()});
    m_sampleRateSpin->setFixedWidth(spinWidth);
    m_bufferSizeSpin->setFixedWidth(spinWidth);
    m_safetyBufferSpin->setFixedWidth(spinWidth);
    m_cpuAffinitySpin->setFixedWidth(spinWidth);
    configureNumericSpin(m_sampleRateSpin);
    configureNumericSpin(m_bufferSizeSpin);
    configureNumericSpin(m_safetyBufferSpin);
    configureNumericSpin(m_cpuAffinitySpin);
    m_sampleRateCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_bufferSizeCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_safetyBufferCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_cpuAffinityCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *sampleLabel = new QLabel(QStringLiteral("Sample rate:"), this);
    sampleLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->addWidget(sampleLabel, row, 0);
    form->addWidget(m_sampleRateCombo, row, 1);
    form->addWidget(m_sampleRateSpin, row, 2);
    ++row;

    auto *bufferLabel = new QLabel(QStringLiteral("Buffer size:"), this);
    bufferLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->addWidget(bufferLabel, row, 0);
    form->addWidget(m_bufferSizeCombo, row, 1);
    form->addWidget(m_bufferSizeSpin, row, 2);
    ++row;

    auto *safetyLabel = new QLabel(QStringLiteral("Safety buffer:"), this);
    safetyLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->addWidget(safetyLabel, row, 0);
    form->addWidget(m_safetyBufferCombo, row, 1);
    form->addWidget(m_safetyBufferSpin, row, 2);
    ++row;
    m_safetyBufferCombo->setToolTip(
        QStringLiteral("Auto keeps a short extra cushion for stability. Custom adds more frames on top of that "
                       "if this PC still crackles. More cushion means more latency."));
    m_safetyBufferSpin->setToolTip(
        QStringLiteral("Extra frames added on top of the normal safety cushion. Use this on PCs that still "
                       "crack or drop audio with Auto."));

    auto *affinityLabel = new QLabel(QStringLiteral("CPU affinity:"), this);
    affinityLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->addWidget(affinityLabel, row, 0);
    form->addWidget(m_cpuAffinityCombo, row, 1);
    form->addWidget(m_cpuAffinitySpin, row, 2);
    ++row;
    m_cpuAffinityCombo->setToolTip(
        QStringLiteral("Auto lets Windows schedule the audio threads. Custom pins them to one core, which can "
                       "help on some PCs and hurt on others."));

    addComboRow(QStringLiteral("Processing precision:"),
                &m_precisionCombo,
                QStringLiteral("32-bit is the stable default. 64-bit uses extra precision in EQ, dynamics, "
                               "loudness, and limiters. WASAPI, VST plugins, and HRTF stay 32-bit."));
    addComboRow(QStringLiteral("Resampling quality:"),
                &m_resampleCombo,
                QStringLiteral("Used when capture and output rates differ, or when drift correction is nudging "
                               "the clock. Fast is lightest. Balanced is the default. Maximum is slowest and "
                               "cleanest."));
    addComboRow(QStringLiteral("Channel layout:"),
                &m_layoutCombo,
                QStringLiteral("Auto matches your output device. Stereo / 5.1 / 7.1 force that mix width; "
                               "extra channels are mixed in or dropped to fit the device."));
    addComboRow(QStringLiteral("Output format:"),
                &m_outputFormatCombo,
                QStringLiteral("Auto uses the device mix format. Float32 is typical. PCM 16 is more compatible "
                               "on some devices. If the request fails, CurvioEQ falls back to Auto."));
    addComboRow(QStringLiteral("Drift correction:"),
                &m_driftCombo,
                QStringLiteral("Auto gently matches capture and playback clocks so the buffer does not slowly "
                               "empty or overflow. Off keeps the rate locked and only trims if the buffer is "
                               "already too full."));

    addComboRow(QStringLiteral("Thread priority:"),
                &m_threadPriorityCombo,
                QStringLiteral("Real-time Audio asks Windows to treat the mixer and capture threads as pro-audio "
                               "work. Normal uses ordinary thread priority if Real-time causes stuttering."));

    addComboRow(QStringLiteral("Exclusive / Shared:"),
                &m_shareModeCombo,
                QStringLiteral("Prefer Shared is the stable default and lets other apps play at the same time. "
                               "Exclusive tries to take the device for lower latency, then falls back to Shared "
                               "if that fails."));
    addComboRow(QStringLiteral("Auto recovery:"),
                &m_autoRecoveryCombo,
                QStringLiteral("On restarts EQ if the output device is unplugged or reset. Off stops EQ instead "
                               "of restarting."));

    populateAudioIoControls();

    m_resetAudioIoButton = new QPushButton(QStringLiteral("Reset audio defaults"), this);
    m_resetAudioIoButton->setToolTip(
        QStringLiteral("Restore stable defaults: %1 Hz, Low buffer, 32-bit, Balanced resample, Auto layout "
                       "and format, drift off, shared output, real-time threads.")
            .arg(AppConstants::kDefaultSampleRate));
    connect(m_resetAudioIoButton, &QPushButton::clicked, this, &SettingsDialog::setAudioIoDefaults);
    connect(m_sampleRateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsDialog::onSampleRateComboChanged);
    connect(m_bufferSizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsDialog::onBufferSizeComboChanged);
    connect(m_sampleRateSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::onSampleRateSpinChanged);
    connect(m_bufferSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::onBufferSizeSpinChanged);
    connect(m_safetyBufferCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsDialog::onSafetyBufferComboChanged);
    connect(m_safetyBufferSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::onSafetyBufferSpinChanged);
    connect(m_cpuAffinityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsDialog::onCpuAffinityComboChanged);
    connect(m_cpuAffinitySpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SettingsDialog::onCpuAffinitySpinChanged);
    form->addWidget(m_resetAudioIoButton, row, 1, 1, 2);
    ++row;

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
    addLabeledField(QStringLiteral("Extra VST3 folders:"), m_vst3FolderList);
    form->addLayout(folderButtons, row, 1, 1, 2);

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

void SettingsDialog::populateAudioIoControls()
{
    if (!m_sampleRateCombo || !m_bufferSizeCombo || !m_sampleRateSpin || !m_bufferSizeSpin) {
        return;
    }

    m_updatingAudioIo = true;
    m_sampleRateCombo->clear();
    for (int i = 0; i < AppConstants::kSampleRateChoiceCount; ++i) {
        const int rate = AppConstants::kSampleRateChoices[i];
        m_sampleRateCombo->addItem(QStringLiteral("%1 Hz").arg(rate), rate);
    }
    m_sampleRateCombo->addItem(QStringLiteral("Custom"), AppConstants::kCustomAudioIoChoice);
    m_sampleRateCombo->setToolTip(
        QStringLiteral("EQ output sample rate. Default %1 Hz is the stable value. "
                       "Choose Custom or type any rate from %2–%3 Hz.")
            .arg(AppConstants::kDefaultSampleRate)
            .arg(AppConstants::kMinSampleRate)
            .arg(AppConstants::kMaxSampleRate));

    m_bufferSizeCombo->clear();
    addComboItem(m_bufferSizeCombo, QStringLiteral("Low"), static_cast<int>(BufferSizePreset::Low));
    addComboItem(m_bufferSizeCombo, QStringLiteral("Balanced"), static_cast<int>(BufferSizePreset::Balanced));
    addComboItem(m_bufferSizeCombo, QStringLiteral("Safe"), static_cast<int>(BufferSizePreset::Safe));
    addComboItem(m_bufferSizeCombo, QStringLiteral("Custom"), static_cast<int>(BufferSizePreset::Custom));
    m_bufferSizeCombo->setToolTip(
        QStringLiteral("How large the audio buffer is. Low is lowest latency. "
                       "Balanced and Safe add stability. Custom lets you type %1–%2 frames. "
                       "EQ restarts if you change this.")
            .arg(AppConstants::kMinBufferFrames)
            .arg(AppConstants::kMaxBufferFrames));

    m_precisionCombo->clear();
    addComboItem(m_precisionCombo,
                 QStringLiteral("32-bit float"),
                 static_cast<int>(ProcessingPrecision::Float32));
    addComboItem(m_precisionCombo,
                 QStringLiteral("64-bit float"),
                 static_cast<int>(ProcessingPrecision::Float64));

    m_resampleCombo->clear();
    addComboItem(m_resampleCombo, QStringLiteral("Fast"), static_cast<int>(ResampleQuality::Fast));
    addComboItem(m_resampleCombo,
                 QStringLiteral("Balanced"),
                 static_cast<int>(ResampleQuality::Balanced));
    addComboItem(m_resampleCombo, QStringLiteral("Maximum"), static_cast<int>(ResampleQuality::Maximum));

    m_layoutCombo->clear();
    addComboItem(m_layoutCombo, QStringLiteral("Auto"), static_cast<int>(ChannelLayout::Auto));
    addComboItem(m_layoutCombo, QStringLiteral("Stereo"), static_cast<int>(ChannelLayout::Stereo));
    addComboItem(m_layoutCombo, QStringLiteral("5.1"), static_cast<int>(ChannelLayout::Surround51));
    addComboItem(m_layoutCombo, QStringLiteral("7.1"), static_cast<int>(ChannelLayout::Surround71));

    m_outputFormatCombo->clear();
    addComboItem(m_outputFormatCombo, QStringLiteral("Auto"), static_cast<int>(OutputFormat::Auto));
    addComboItem(m_outputFormatCombo, QStringLiteral("Float32"), static_cast<int>(OutputFormat::Float32));
    addComboItem(m_outputFormatCombo, QStringLiteral("PCM 16"), static_cast<int>(OutputFormat::Pcm16));

    m_driftCombo->clear();
    addComboItem(m_driftCombo, QStringLiteral("Auto"), 1);
    addComboItem(m_driftCombo, QStringLiteral("Off"), 0);

    m_safetyBufferCombo->clear();
    addComboItem(m_safetyBufferCombo, QStringLiteral("Auto"), 1);
    addComboItem(m_safetyBufferCombo, QStringLiteral("Custom"), 0);

    m_threadPriorityCombo->clear();
    addComboItem(m_threadPriorityCombo,
                 QStringLiteral("Real-time Audio"),
                 static_cast<int>(ThreadPriority::RealtimeAudio));
    addComboItem(m_threadPriorityCombo,
                 QStringLiteral("Normal"),
                 static_cast<int>(ThreadPriority::Normal));

    m_cpuAffinityCombo->clear();
    addComboItem(m_cpuAffinityCombo, QStringLiteral("Auto"), 1);
    addComboItem(m_cpuAffinityCombo, QStringLiteral("Custom"), 0);

    m_shareModeCombo->clear();
    addComboItem(m_shareModeCombo,
                 QStringLiteral("Prefer Shared"),
                 static_cast<int>(ShareMode::PreferShared));
    addComboItem(m_shareModeCombo,
                 QStringLiteral("Exclusive"),
                 static_cast<int>(ShareMode::Exclusive));

    m_autoRecoveryCombo->clear();
    addComboItem(m_autoRecoveryCombo, QStringLiteral("On"), 1);
    addComboItem(m_autoRecoveryCombo, QStringLiteral("Off"), 0);

    m_updatingAudioIo = false;
    applyEngineSettingsToControls(m_result.engineIo());
}

void SettingsDialog::applyEngineSettingsToControls(const EngineIoSettings &io)
{
    const int sampleRate = AppConstants::clampSampleRate(io.sampleRate);
    if (!AppConstants::isListedSampleRate(sampleRate)) {
        m_customSampleRate = sampleRate;
    }
    m_updatingAudioIo = true;
    if (m_sampleRateCombo) {
        const int listedIndex = m_sampleRateCombo->findData(sampleRate);
        const int customIndex = m_sampleRateCombo->findData(AppConstants::kCustomAudioIoChoice);
        if (listedIndex >= 0) {
            m_sampleRateCombo->setCurrentIndex(listedIndex);
        } else if (customIndex >= 0) {
            m_sampleRateCombo->setCurrentIndex(customIndex);
        }
    }
    const BufferSizePreset preset = EngineIoSettings::bufferPresetFromFrames(io.bufferFrames);
    if (preset == BufferSizePreset::Custom) {
        m_customBufferFrames = AppConstants::clampBufferFrames(io.bufferFrames);
    }
    if (m_bufferSizeCombo) {
        const int index = m_bufferSizeCombo->findData(static_cast<int>(preset));
        if (index >= 0) {
            m_bufferSizeCombo->setCurrentIndex(index);
        }
    }
    if (m_precisionCombo) {
        m_precisionCombo->setCurrentIndex(
            m_precisionCombo->findData(static_cast<int>(io.precision)));
    }
    if (m_resampleCombo) {
        m_resampleCombo->setCurrentIndex(
            m_resampleCombo->findData(static_cast<int>(io.resampleQuality)));
    }
    if (m_layoutCombo) {
        m_layoutCombo->setCurrentIndex(m_layoutCombo->findData(static_cast<int>(io.channelLayout)));
    }
    if (m_outputFormatCombo) {
        m_outputFormatCombo->setCurrentIndex(
            m_outputFormatCombo->findData(static_cast<int>(io.outputFormat)));
    }
    if (m_driftCombo) {
        m_driftCombo->setCurrentIndex(m_driftCombo->findData(io.driftCorrection ? 1 : 0));
    }
    if (m_safetyBufferCombo) {
        m_safetyBufferCombo->setCurrentIndex(m_safetyBufferCombo->findData(io.safetyBufferAuto ? 1 : 0));
    }
    if (m_safetyBufferSpin) {
        m_safetyBufferSpin->setValue(io.safetyBufferAuto ? 0 : io.safetyBufferFrames);
    }
    if (m_threadPriorityCombo) {
        m_threadPriorityCombo->setCurrentIndex(
            m_threadPriorityCombo->findData(static_cast<int>(io.threadPriority)));
    }
    if (m_cpuAffinityCombo) {
        m_cpuAffinityCombo->setCurrentIndex(m_cpuAffinityCombo->findData(io.cpuAffinityAuto ? 1 : 0));
    }
    if (m_cpuAffinitySpin) {
        m_cpuAffinitySpin->setValue(std::clamp(io.cpuAffinityCore, m_cpuAffinitySpin->minimum(),
                                               m_cpuAffinitySpin->maximum()));
    }
    if (m_shareModeCombo) {
        m_shareModeCombo->setCurrentIndex(m_shareModeCombo->findData(static_cast<int>(io.shareMode)));
    }
    if (m_autoRecoveryCombo) {
        m_autoRecoveryCombo->setCurrentIndex(m_autoRecoveryCombo->findData(io.autoRecovery ? 1 : 0));
    }
    m_updatingAudioIo = false;
    updateSampleRateSpinState();
    updateBufferSizeSpinState();
    updateSafetyBufferSpinState();
    updateCpuAffinitySpinState();
}

void SettingsDialog::setAudioIoDefaults()
{
    applyEngineSettingsToControls(EngineIoSettings{});
}

void SettingsDialog::syncSampleRateCombo()
{
    if (!m_sampleRateCombo || !m_sampleRateSpin) {
        return;
    }
    const int rate = m_sampleRateSpin->value();
    const int listedIndex = m_sampleRateCombo->findData(rate);
    const int customIndex = m_sampleRateCombo->findData(AppConstants::kCustomAudioIoChoice);
    m_updatingAudioIo = true;
    if (listedIndex >= 0) {
        m_sampleRateCombo->setCurrentIndex(listedIndex);
    } else if (customIndex >= 0) {
        m_sampleRateCombo->setCurrentIndex(customIndex);
    }
    m_updatingAudioIo = false;
}

void SettingsDialog::syncBufferSizeCombo()
{
    if (!m_bufferSizeCombo || !m_bufferSizeSpin) {
        return;
    }
    const int frames = m_bufferSizeSpin->value();
    const BufferSizePreset preset = EngineIoSettings::bufferPresetFromFrames(frames);
    const int listedIndex = m_bufferSizeCombo->findData(static_cast<int>(preset));
    m_updatingAudioIo = true;
    if (listedIndex >= 0) {
        m_bufferSizeCombo->setCurrentIndex(listedIndex);
    }
    m_updatingAudioIo = false;
    updateBufferSizeSpinState();
}

void SettingsDialog::updateSampleRateSpinState()
{
    if (!m_sampleRateCombo || !m_sampleRateSpin) {
        return;
    }
    const int comboData = m_sampleRateCombo->currentData().toInt();
    const bool custom = comboData == AppConstants::kCustomAudioIoChoice;
    m_updatingAudioIo = true;
    if (custom) {
        m_sampleRateSpin->setValue(m_customSampleRate);
    } else {
        m_sampleRateSpin->setValue(comboData);
    }
    m_updatingAudioIo = false;
    m_sampleRateSpin->setEnabled(custom);
}

void SettingsDialog::updateBufferSizeSpinState()
{
    if (!m_bufferSizeCombo || !m_bufferSizeSpin) {
        return;
    }
    const auto preset = static_cast<BufferSizePreset>(m_bufferSizeCombo->currentData().toInt());
    const bool custom = preset == BufferSizePreset::Custom;
    m_updatingAudioIo = true;
    if (custom) {
        m_bufferSizeSpin->setValue(m_customBufferFrames);
    } else {
        m_bufferSizeSpin->setValue(EngineIoSettings::framesForBufferPreset(preset, m_customBufferFrames));
    }
    m_updatingAudioIo = false;
    m_bufferSizeSpin->setEnabled(custom);
}

void SettingsDialog::updateSafetyBufferSpinState()
{
    if (!m_safetyBufferCombo || !m_safetyBufferSpin) {
        return;
    }
    const bool custom = m_safetyBufferCombo->currentData().toInt() == 0;
    m_updatingAudioIo = true;
    m_safetyBufferSpin->setValue(custom ? m_result.safetyBufferFrames : 0);
    m_updatingAudioIo = false;
    m_safetyBufferSpin->setEnabled(custom);
}

void SettingsDialog::updateCpuAffinitySpinState()
{
    if (!m_cpuAffinityCombo || !m_cpuAffinitySpin) {
        return;
    }
    const bool custom = m_cpuAffinityCombo->currentData().toInt() == 0;
    m_cpuAffinitySpin->setEnabled(custom);
}

void SettingsDialog::onSampleRateComboChanged(int index)
{
    if (m_updatingAudioIo || !m_sampleRateCombo || !m_sampleRateSpin || index < 0) {
        return;
    }
    updateSampleRateSpinState();
    const int rate = m_sampleRateCombo->itemData(index).toInt();
    if (rate == AppConstants::kCustomAudioIoChoice && m_sampleRateSpin) {
        m_sampleRateSpin->setFocus();
        m_sampleRateSpin->selectAll();
    }
}

void SettingsDialog::onBufferSizeComboChanged(int index)
{
    if (m_updatingAudioIo || !m_bufferSizeCombo || !m_bufferSizeSpin || index < 0) {
        return;
    }
    updateBufferSizeSpinState();
    const auto preset = static_cast<BufferSizePreset>(m_bufferSizeCombo->itemData(index).toInt());
    if (preset == BufferSizePreset::Custom && m_bufferSizeSpin) {
        m_bufferSizeSpin->setFocus();
        m_bufferSizeSpin->selectAll();
    }
}

void SettingsDialog::onSampleRateSpinChanged(int value)
{
    if (m_updatingAudioIo) {
        return;
    }
    m_customSampleRate = AppConstants::clampSampleRate(value);
    syncSampleRateCombo();
}

void SettingsDialog::onBufferSizeSpinChanged(int value)
{
    if (m_updatingAudioIo) {
        return;
    }
    m_customBufferFrames = AppConstants::clampBufferFrames(value);
}

void SettingsDialog::onSafetyBufferComboChanged(int index)
{
    if (m_updatingAudioIo || index < 0) {
        return;
    }
    updateSafetyBufferSpinState();
    if (m_safetyBufferSpin && m_safetyBufferSpin->isEnabled()) {
        m_safetyBufferSpin->setFocus();
        m_safetyBufferSpin->selectAll();
    }
}

void SettingsDialog::onCpuAffinityComboChanged(int index)
{
    if (m_updatingAudioIo || index < 0) {
        return;
    }
    updateCpuAffinitySpinState();
    if (m_cpuAffinitySpin && m_cpuAffinitySpin->isEnabled()) {
        m_cpuAffinitySpin->setFocus();
        m_cpuAffinitySpin->selectAll();
    }
}

void SettingsDialog::onSafetyBufferSpinChanged(int value)
{
    if (m_updatingAudioIo) {
        return;
    }
    m_result.safetyBufferFrames = value;
}

void SettingsDialog::onCpuAffinitySpinChanged(int value)
{
    if (m_updatingAudioIo) {
        return;
    }
    m_result.cpuAffinityCore = value;
}

EngineIoSettings SettingsDialog::engineSettingsFromControls() const
{
    EngineIoSettings io;
    if (m_sampleRateCombo) {
        const int rate = m_sampleRateCombo->currentData().toInt();
        if (rate == AppConstants::kCustomAudioIoChoice && m_sampleRateSpin) {
            io.sampleRate = AppConstants::clampSampleRate(m_sampleRateSpin->value());
        } else if (rate != AppConstants::kCustomAudioIoChoice) {
            io.sampleRate = AppConstants::clampSampleRate(rate);
        }
    }
    if (m_bufferSizeCombo) {
        const auto preset = static_cast<BufferSizePreset>(m_bufferSizeCombo->currentData().toInt());
        const int custom = m_bufferSizeSpin ? m_bufferSizeSpin->value() : m_customBufferFrames;
        io.bufferFrames = EngineIoSettings::framesForBufferPreset(preset, custom);
    }
    if (m_precisionCombo) {
        io.precision = static_cast<ProcessingPrecision>(m_precisionCombo->currentData().toInt());
    }
    if (m_resampleCombo) {
        io.resampleQuality = static_cast<ResampleQuality>(m_resampleCombo->currentData().toInt());
    }
    if (m_layoutCombo) {
        io.channelLayout = static_cast<ChannelLayout>(m_layoutCombo->currentData().toInt());
    }
    if (m_outputFormatCombo) {
        io.outputFormat = static_cast<OutputFormat>(m_outputFormatCombo->currentData().toInt());
    }
    if (m_driftCombo) {
        io.driftCorrection = m_driftCombo->currentData().toInt() != 0;
    }
    if (m_safetyBufferCombo) {
        io.safetyBufferAuto = m_safetyBufferCombo->currentData().toInt() != 0;
    }
    if (m_safetyBufferSpin && m_safetyBufferCombo
        && m_safetyBufferCombo->currentData().toInt() == 0) {
        io.safetyBufferFrames = m_safetyBufferSpin->value();
    }
    if (m_threadPriorityCombo) {
        io.threadPriority = static_cast<ThreadPriority>(m_threadPriorityCombo->currentData().toInt());
    }
    if (m_cpuAffinityCombo) {
        io.cpuAffinityAuto = m_cpuAffinityCombo->currentData().toInt() != 0;
    }
    if (m_cpuAffinitySpin && m_cpuAffinityCombo && m_cpuAffinityCombo->currentData().toInt() == 0) {
        io.cpuAffinityCore = m_cpuAffinitySpin->value();
    }
    if (m_shareModeCombo) {
        io.shareMode = static_cast<ShareMode>(m_shareModeCombo->currentData().toInt());
    }
    if (m_autoRecoveryCombo) {
        io.autoRecovery = m_autoRecoveryCombo->currentData().toInt() != 0;
    }
    return io;
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

    const EngineIoSettings io = engineSettingsFromControls();
    m_result.sampleRate = io.sampleRate;
    m_result.bufferFrames = io.bufferFrames;
    m_result.processingPrecision = io.precision;
    m_result.resampleQuality = io.resampleQuality;
    m_result.channelLayout = io.channelLayout;
    m_result.outputFormat = io.outputFormat;
    m_result.driftCorrection = io.driftCorrection;
    m_result.safetyBufferAuto = io.safetyBufferAuto;
    m_result.safetyBufferFrames = io.safetyBufferFrames;
    m_result.threadPriority = io.threadPriority;
    m_result.cpuAffinityAuto = io.cpuAffinityAuto;
    m_result.cpuAffinityCore = io.cpuAffinityCore;
    m_result.shareMode = io.shareMode;
    m_result.autoRecovery = io.autoRecovery;

    QDialog::accept();
}
