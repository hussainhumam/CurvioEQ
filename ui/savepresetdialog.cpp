#include "savepresetdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

SavePresetDialog::SavePresetDialog(const QString &defaultName, bool advancedEq, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(advancedEq ? QStringLiteral("Save Advanced preset")
                              : QStringLiteral("Save preset"));
    setModal(true);
    resize(420, 280);

    auto *layout = new QVBoxLayout(this);

    auto *form = new QFormLayout();
    m_nameEdit = new QLineEdit(defaultName, this);
    m_nameEdit->selectAll();
    form->addRow(advancedEq ? QStringLiteral("Advanced preset name:")
                            : QStringLiteral("Preset name:"),
                 m_nameEdit);
    layout->addLayout(form);

    layout->addWidget(new QLabel(QStringLiteral("Include in this preset:"), this));

    m_eqCheck = new QCheckBox(QStringLiteral("EQ"), this);
    m_eqCheck->setChecked(true);
    m_surroundCheck = new QCheckBox(QStringLiteral("HRTF"), this);
    m_dynamicsCheck = new QCheckBox(QStringLiteral("Dynamics"), this);
    m_chainCheck = new QCheckBox(QStringLiteral("Audio chain"), this);
    layout->addWidget(m_eqCheck);
    layout->addWidget(m_surroundCheck);
    layout->addWidget(m_dynamicsCheck);
    layout->addWidget(m_chainCheck);

    auto *allRow = new QHBoxLayout();
    auto *allButton = new QPushButton(QStringLiteral("All"), this);
    allButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    allRow->addWidget(allButton);
    allRow->addStretch(1);
    layout->addLayout(allRow);

    layout->addStretch(1);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(m_buttons);

    connect(allButton, &QPushButton::clicked, this, &SavePresetDialog::onAllClicked);
    connect(m_nameEdit, &QLineEdit::textChanged, this, &SavePresetDialog::updateOkEnabled);
    connect(m_eqCheck, &QCheckBox::toggled, this, &SavePresetDialog::updateOkEnabled);
    connect(m_surroundCheck, &QCheckBox::toggled, this, &SavePresetDialog::updateOkEnabled);
    connect(m_dynamicsCheck, &QCheckBox::toggled, this, &SavePresetDialog::updateOkEnabled);
    connect(m_chainCheck, &QCheckBox::toggled, this, &SavePresetDialog::updateOkEnabled);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateOkEnabled();
}

QString SavePresetDialog::presetName() const
{
    return m_nameEdit ? m_nameEdit->text().trimmed() : QString();
}

bool SavePresetDialog::includeEq() const
{
    return m_eqCheck && m_eqCheck->isChecked();
}

bool SavePresetDialog::includeSurround() const
{
    return m_surroundCheck && m_surroundCheck->isChecked();
}

bool SavePresetDialog::includeDynamics() const
{
    return m_dynamicsCheck && m_dynamicsCheck->isChecked();
}

bool SavePresetDialog::includeAudioChain() const
{
    return m_chainCheck && m_chainCheck->isChecked();
}

void SavePresetDialog::onAllClicked()
{
    m_eqCheck->setChecked(true);
    m_surroundCheck->setChecked(true);
    m_dynamicsCheck->setChecked(true);
    m_chainCheck->setChecked(true);
}

void SavePresetDialog::updateOkEnabled()
{
    const bool hasName = !presetName().isEmpty();
    const bool hasSection = includeEq() || includeSurround() || includeDynamics() || includeAudioChain();
    if (QPushButton *ok = m_buttons ? m_buttons->button(QDialogButtonBox::Ok) : nullptr) {
        ok->setEnabled(hasName && hasSection);
    }
}
