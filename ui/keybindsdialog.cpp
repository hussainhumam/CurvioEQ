#include "keybindsdialog.h"

#include "hotkeyedit.h"
#include "hotkeysequence.h"
#include "ui/eqcolorpalette.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QVector>

namespace {

QWidget *makeColorSwatch(const QColor &color, QWidget *parent)
{
    auto *swatch = new QFrame(parent);
    swatch->setFixedSize(18, 18);
    swatch->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #666; border-radius: 9px;")
                              .arg(color.name()));
    return swatch;
}

QWidget *makeActionRow(const QString &labelText, HotkeyEdit *edit, QWidget *parent, QWidget *leading = nullptr)
{
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 2, 0, 2);
    layout->setSpacing(8);
    if (leading) {
        layout->addWidget(leading);
    }
    auto *label = new QLabel(labelText, row);
    label->setMinimumWidth(160);
    layout->addWidget(label);
    layout->addWidget(edit, 1);
    return row;
}

} // namespace

KeybindsDialog::KeybindsDialog(const AppSettings &current, QWidget *parent)
    : QDialog(parent)
    , m_result(current)
{
    setWindowTitle(QStringLiteral("Keybinds"));
    resize(560, 560);

    auto *outerLayout = new QVBoxLayout(this);

    m_enableKeybindsCheck = new QCheckBox(QStringLiteral("Enable keybinds"), this);
    m_enableKeybindsCheck->setChecked(current.keybindsEnabled);
    outerLayout->addWidget(m_enableKeybindsCheck);

    auto *hint = new QLabel(
        QStringLiteral("Global hotkeys work while CurvioEQ is running, including from the tray. "
                       "Click Record (or the shortcut box), then press a combination."),
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
    layout->setContentsMargins(0, 0, 8, 0);

    auto *generalGroup = new QGroupBox(QStringLiteral("General"), scrollContent);
    auto *generalLayout = new QVBoxLayout(generalGroup);

    m_eqToggleEdit = new HotkeyEdit(generalGroup);
    m_eqToggleEdit->setStoredString(current.eqToggleKeybind);
    m_eqToggleEdit->setToolTip(
        QStringLiteral("Disables all active EQ sessions when pressed. Does nothing if EQ is off."));
    generalLayout->addWidget(makeActionRow(QStringLiteral("EQ disable all (when active)"),
                                            m_eqToggleEdit,
                                            generalGroup));

    m_outputMuteEdit = new HotkeyEdit(generalGroup);
    m_outputMuteEdit->setStoredString(current.outputMuteKeybind);
    m_outputMuteEdit->setToolTip(QStringLiteral("Toggles master mute on the output device from Settings."));
    generalLayout->addWidget(makeActionRow(QStringLiteral("Mute output device"),
                                           m_outputMuteEdit,
                                           generalGroup));
    layout->addWidget(generalGroup);

    auto *colorGroup = new QGroupBox(QStringLiteral("Mute by color"), scrollContent);
    auto *colorLayout = new QVBoxLayout(colorGroup);

    auto *colorHint = new QLabel(
        QStringLiteral("Toggle mute on app(s) that currently have active EQ using the matching color label."),
        colorGroup);
    colorHint->setWordWrap(true);
    colorLayout->addWidget(colorHint);

    for (int colorIndex = 0; colorIndex < AppSettings::kEqColorKeybindCount; ++colorIndex) {
        auto *edit = new HotkeyEdit(colorGroup);
        edit->setStoredString(current.eqColorKeybinds[static_cast<size_t>(colorIndex)]);
        edit->setToolTip(QStringLiteral("Toggles mute on apps with active EQ using the %1 label.")
                             .arg(EqColorPalette::presetColorLabel(colorIndex)));
        m_colorEdits[static_cast<size_t>(colorIndex)] = edit;
        colorLayout->addWidget(makeActionRow(EqColorPalette::presetColorLabel(colorIndex),
                                             edit,
                                             colorGroup,
                                             makeColorSwatch(EqColorPalette::presetColorAt(colorIndex),
                                                             colorGroup)));
    }
    layout->addWidget(colorGroup);
    layout->addStretch(1);

    auto *footer = new QLabel(
        QStringLiteral("Esc cancels recording. Clear unbinds. Shortcuts without Ctrl, Alt, Shift, or Win "
                       "steal that key in every app."),
        this);
    footer->setWordWrap(true);
    outerLayout->addWidget(footer);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(QStringLiteral("color: #c44848;"));
    m_statusLabel->hide();
    outerLayout->addWidget(m_statusLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outerLayout->addWidget(buttons);

    connect(m_enableKeybindsCheck, &QCheckBox::toggled, this, &KeybindsDialog::onKeybindsEnabledToggled);
    for (HotkeyEdit *edit : allEdits()) {
        connect(edit, &HotkeyEdit::sequenceChanged, this, &KeybindsDialog::onSequenceChanged);
    }

    qApp->installEventFilter(this);

    updateEditorState();
    refreshConflictsAndStatus();
}

KeybindsDialog::~KeybindsDialog()
{
    qApp->removeEventFilter(this);
}

bool KeybindsDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyboardLayoutChange) {
        for (HotkeyEdit *edit : allEdits()) {
            if (edit) {
                edit->refreshDisplay();
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}

AppSettings KeybindsDialog::resultSettings() const
{
    return m_result;
}

void KeybindsDialog::onKeybindsEnabledToggled(bool enabled)
{
    Q_UNUSED(enabled)
    updateEditorState();
    refreshConflictsAndStatus();
}

void KeybindsDialog::onSequenceChanged()
{
    refreshConflictsAndStatus();
}

void KeybindsDialog::updateEditorState()
{
    const bool enabled = m_enableKeybindsCheck->isChecked();
    for (HotkeyEdit *edit : allEdits()) {
        if (edit) {
            edit->setEnabled(enabled);
        }
    }
}

QVector<HotkeyEdit *> KeybindsDialog::allEdits() const
{
    QVector<HotkeyEdit *> edits;
    edits.append(m_eqToggleEdit);
    edits.append(m_outputMuteEdit);
    for (HotkeyEdit *edit : m_colorEdits) {
        edits.append(edit);
    }
    return edits;
}

void KeybindsDialog::refreshConflictsAndStatus()
{
    const QVector<HotkeyEdit *> edits = allEdits();
    QStringList problems;
    bool anyUnmodified = false;

    for (HotkeyEdit *edit : edits) {
        if (edit) {
            edit->setConflict(false);
        }
    }

    if (m_enableKeybindsCheck->isChecked()) {
        for (int i = 0; i < edits.size(); ++i) {
            HotkeyEdit *edit = edits.at(i);
            if (!edit || edit->sequence().isEmpty()) {
                continue;
            }

            if (!edit->sequence().hasModifier()) {
                anyUnmodified = true;
            }

            quint32 modifiers = 0;
            quint32 virtualKey = 0;
            if (!edit->sequence().toNative(&modifiers, &virtualKey)) {
                problems.append(QStringLiteral("\"%1\" cannot be registered as a global hotkey.")
                                    .arg(edit->sequence().displayString()));
                edit->setConflict(true);
            }

            for (int j = i + 1; j < edits.size(); ++j) {
                HotkeyEdit *other = edits.at(j);
                if (!other || other->sequence().isEmpty()) {
                    continue;
                }
                if (edit->sequence() == other->sequence()) {
                    edit->setConflict(true);
                    other->setConflict(true);
                    const QString text = edit->sequence().displayString();
                    const QString problem = QStringLiteral("\"%1\" is assigned more than once.").arg(text);
                    if (!problems.contains(problem)) {
                        problems.append(problem);
                    }
                }
            }
        }
    }

    QStringList statusLines = problems;
    if (anyUnmodified && m_enableKeybindsCheck->isChecked()) {
        statusLines.prepend(
            QStringLiteral("A shortcut has no modifier. That key will be captured in every app."));
    }

    if (statusLines.isEmpty()) {
        m_statusLabel->hide();
        m_statusLabel->clear();
        return;
    }

    m_statusLabel->setText(statusLines.join(QLatin1Char('\n')));
    m_statusLabel->show();
}

void KeybindsDialog::accept()
{
    for (HotkeyEdit *edit : allEdits()) {
        if (edit) {
            edit->stopRecording();
        }
    }

    m_result.keybindsEnabled = m_enableKeybindsCheck->isChecked();
    m_result.eqToggleKeybind = m_eqToggleEdit->storedString();
    m_result.outputMuteKeybind = m_outputMuteEdit->storedString();
    for (int colorIndex = 0; colorIndex < AppSettings::kEqColorKeybindCount; ++colorIndex) {
        HotkeyEdit *edit = m_colorEdits[static_cast<size_t>(colorIndex)];
        m_result.eqColorKeybinds[static_cast<size_t>(colorIndex)] = edit ? edit->storedString() : QString();
    }

    if (m_result.keybindsEnabled) {
        const QVector<HotkeyEdit *> edits = allEdits();
        for (int i = 0; i < edits.size(); ++i) {
            HotkeyEdit *edit = edits.at(i);
            if (!edit || edit->sequence().isEmpty()) {
                continue;
            }

            quint32 modifiers = 0;
            quint32 virtualKey = 0;
            if (!edit->sequence().toNative(&modifiers, &virtualKey)) {
                QMessageBox::warning(this,
                                     QStringLiteral("Keybinds"),
                                     QStringLiteral("\"%1\" cannot be registered as a global hotkey.")
                                         .arg(edit->sequence().displayString()));
                return;
            }

            for (int j = i + 1; j < edits.size(); ++j) {
                HotkeyEdit *other = edits.at(j);
                if (!other || other->sequence().isEmpty()) {
                    continue;
                }
                if (edit->sequence() == other->sequence()) {
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
