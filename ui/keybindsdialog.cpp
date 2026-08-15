#include "keybindsdialog.h"

#include "ui/eqcolorpalette.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMessageBox>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

class KeySequenceClearFilter : public QObject
{
public:
    explicit KeySequenceClearFilter(QKeySequenceEdit *edit, QObject *parent = nullptr)
        : QObject(parent)
        , m_edit(edit)
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched != m_edit || event->type() != QEvent::KeyPress) {
            return QObject::eventFilter(watched, event);
        }

        const auto *keyEvent = static_cast<const QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape && keyEvent->modifiers() == Qt::NoModifier) {
            m_edit->clear();
            return true;
        }

        return QObject::eventFilter(watched, event);
    }

private:
    QKeySequenceEdit *m_edit = nullptr;
};

void installClearOnEscape(QKeySequenceEdit *edit)
{
    edit->installEventFilter(new KeySequenceClearFilter(edit, edit));
}

QWidget *makeColorSwatch(const QColor &color, QWidget *parent)
{
    auto *swatch = new QFrame(parent);
    swatch->setFixedSize(18, 18);
    swatch->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #666; border-radius: 9px;")
                              .arg(color.name()));
    return swatch;
}

} // namespace

KeybindsDialog::KeybindsDialog(const AppSettings &current, QWidget *parent)
    : QDialog(parent)
    , m_result(current)
{
    setWindowTitle(QStringLiteral("Keybinds"));
    resize(460, 520);

    auto *outerLayout = new QVBoxLayout(this);

    m_enableKeybindsCheck = new QCheckBox(QStringLiteral("Enable keybinds"), this);
    m_enableKeybindsCheck->setChecked(current.keybindsEnabled);
    outerLayout->addWidget(m_enableKeybindsCheck);

    auto *hint = new QLabel(
        QStringLiteral("Global hotkeys work while CurvioEQ is running, including from the tray. "
                       "Assign only the actions you want. Click a field and press Esc to unbind it."),
        this);
    hint->setWordWrap(true);
    outerLayout->addWidget(hint);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outerLayout->addWidget(scroll, 1);

    auto *scrollContent = new QWidget(scroll);
    scroll->setWidget(scrollContent);
    auto *layout = new QVBoxLayout(scrollContent);

    auto *generalForm = new QFormLayout();
    m_eqToggleEdit = new QKeySequenceEdit(scrollContent);
    m_eqToggleEdit->setKeySequence(QKeySequence(current.eqToggleKeybind));
    m_eqToggleEdit->setToolTip(
        QStringLiteral("Disables all active EQ sessions when pressed. Does nothing if EQ is off."));
    installClearOnEscape(m_eqToggleEdit);
    generalForm->addRow(QStringLiteral("EQ disable all (when active):"), m_eqToggleEdit);

    m_outputMuteEdit = new QKeySequenceEdit(scrollContent);
    m_outputMuteEdit->setKeySequence(QKeySequence(current.outputMuteKeybind));
    m_outputMuteEdit->setToolTip(QStringLiteral("Toggles master mute on the output device from Settings."));
    installClearOnEscape(m_outputMuteEdit);
    generalForm->addRow(QStringLiteral("Mute output device:"), m_outputMuteEdit);
    layout->addLayout(generalForm);

    auto *colorHeading = new QLabel(QStringLiteral("EQ color labels"), scrollContent);
    colorHeading->setStyleSheet(QStringLiteral("font-weight: bold; margin-top: 8px;"));
    layout->addWidget(colorHeading);

    auto *colorHint = new QLabel(
        QStringLiteral("Toggle mute on app(s) that currently have active EQ using the matching color label."),
        scrollContent);
    colorHint->setWordWrap(true);
    layout->addWidget(colorHint);

    auto *colorForm = new QFormLayout();
    for (int colorIndex = 0; colorIndex < AppSettings::kEqColorKeybindCount; ++colorIndex) {
        auto *labelRow = new QWidget(scrollContent);
        auto *labelLayout = new QHBoxLayout(labelRow);
        labelLayout->setContentsMargins(0, 0, 0, 0);
        labelLayout->setSpacing(6);
        labelLayout->addWidget(makeColorSwatch(EqColorPalette::presetColorAt(colorIndex), labelRow));
        labelLayout->addWidget(new QLabel(EqColorPalette::presetColorLabel(colorIndex), labelRow));
        labelLayout->addStretch(1);

        auto *edit = new QKeySequenceEdit(scrollContent);
        edit->setKeySequence(QKeySequence(current.eqColorKeybinds[static_cast<size_t>(colorIndex)]));
        edit->setToolTip(
            QStringLiteral("Toggles mute on apps with active EQ using the %1 label.")
                .arg(EqColorPalette::presetColorLabel(colorIndex)));
        installClearOnEscape(edit);
        m_colorEdits[static_cast<size_t>(colorIndex)] = edit;
        colorForm->addRow(labelRow, edit);
    }
    layout->addLayout(colorForm);
    layout->addStretch(1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outerLayout->addWidget(buttons);

    connect(m_enableKeybindsCheck, &QCheckBox::toggled, this, &KeybindsDialog::onKeybindsEnabledToggled);
    updateEditorState();
}

AppSettings KeybindsDialog::resultSettings() const
{
    return m_result;
}

void KeybindsDialog::onKeybindsEnabledToggled(bool enabled)
{
    Q_UNUSED(enabled)
    updateEditorState();
}

void KeybindsDialog::updateEditorState()
{
    const bool enabled = m_enableKeybindsCheck->isChecked();
    m_eqToggleEdit->setEnabled(enabled);
    m_outputMuteEdit->setEnabled(enabled);
    for (QKeySequenceEdit *edit : m_colorEdits) {
        if (edit) {
            edit->setEnabled(enabled);
        }
    }
}

QStringList KeybindsDialog::assignedKeybinds() const
{
    QStringList assigned;
    auto addIfSet = [&assigned](QKeySequenceEdit *edit) {
        if (!edit) {
            return;
        }
        const QString text = edit->keySequence().toString(QKeySequence::PortableText);
        if (!text.isEmpty()) {
            assigned.append(text);
        }
    };

    addIfSet(m_eqToggleEdit);
    addIfSet(m_outputMuteEdit);
    for (QKeySequenceEdit *edit : m_colorEdits) {
        addIfSet(edit);
    }
    return assigned;
}

void KeybindsDialog::accept()
{
    m_result.keybindsEnabled = m_enableKeybindsCheck->isChecked();
    m_result.eqToggleKeybind = m_eqToggleEdit->keySequence().toString(QKeySequence::PortableText);
    m_result.outputMuteKeybind = m_outputMuteEdit->keySequence().toString(QKeySequence::PortableText);
    for (int colorIndex = 0; colorIndex < AppSettings::kEqColorKeybindCount; ++colorIndex) {
        QKeySequenceEdit *edit = m_colorEdits[static_cast<size_t>(colorIndex)];
        m_result.eqColorKeybinds[static_cast<size_t>(colorIndex)] =
            edit ? edit->keySequence().toString(QKeySequence::PortableText) : QString();
    }

    if (m_result.keybindsEnabled) {
        const QStringList assigned = assignedKeybinds();
        for (int i = 0; i < assigned.size(); ++i) {
            for (int j = i + 1; j < assigned.size(); ++j) {
                if (assigned.at(i) == assigned.at(j)) {
                    QMessageBox::warning(this,
                                         QStringLiteral("Keybinds"),
                                         QStringLiteral("Each assigned action needs a different key combination."));
                    return;
                }
            }
        }
    }

    QDialog::accept();
}
