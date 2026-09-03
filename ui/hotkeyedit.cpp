#include "hotkeyedit.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPointer>
#include <QPushButton>

namespace {

QPointer<HotkeyEdit> s_recordingEdit;

} // namespace

HotkeyEdit::HotkeyEdit(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_display = new QLabel(this);
    m_display->setFrameShape(QFrame::StyledPanel);
    m_display->setFrameShadow(QFrame::Sunken);
    m_display->setMinimumWidth(140);
    m_display->setMinimumHeight(24);
    m_display->setAlignment(Qt::AlignCenter);
    m_display->setFocusPolicy(Qt::ClickFocus);
    m_display->installEventFilter(this);
    layout->addWidget(m_display, 1);

    m_recordButton = new QPushButton(QStringLiteral("Record"), this);
    m_recordButton->setAutoDefault(false);
    m_recordButton->setDefault(false);
    layout->addWidget(m_recordButton);

    m_clearButton = new QPushButton(QStringLiteral("Clear"), this);
    m_clearButton->setAutoDefault(false);
    m_clearButton->setDefault(false);
    layout->addWidget(m_clearButton);

    connect(m_recordButton, &QPushButton::clicked, this, [this]() {
        if (m_recording) {
            stopRecording();
        } else {
            startRecording();
        }
    });
    connect(m_clearButton, &QPushButton::clicked, this, [this]() {
        stopRecording();
        if (m_sequence.isEmpty()) {
            return;
        }
        m_sequence = {};
        updateDisplay();
        emit sequenceChanged();
    });

    updateDisplay();
}

void HotkeyEdit::setSequence(const HotkeySequence &sequence)
{
    if (m_sequence == sequence) {
        return;
    }
    m_sequence = sequence;
    updateDisplay();
    emit sequenceChanged();
}

void HotkeyEdit::setStoredString(const QString &text)
{
    setSequence(HotkeySequence::fromStoredString(text));
}

QString HotkeyEdit::storedString() const
{
    return m_sequence.toStoredString();
}

void HotkeyEdit::stopRecording()
{
    if (!m_recording) {
        return;
    }

    m_recording = false;
    if (s_recordingEdit == this) {
        s_recordingEdit = nullptr;
    }
    releaseKeyboard();
    m_recordButton->setText(QStringLiteral("Record"));
    updateDisplay();
    emit recordingChanged(false);
}

void HotkeyEdit::setConflict(bool conflict)
{
    if (m_conflict == conflict) {
        return;
    }
    m_conflict = conflict;
    updateDisplay();
}

void HotkeyEdit::refreshDisplay()
{
    updateDisplay();
}

bool HotkeyEdit::event(QEvent *event)
{
    if (m_recording && (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride)) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (event->type() == QEvent::ShortcutOverride) {
            keyEvent->accept();
            return true;
        }
        handleKeyPress(keyEvent);
        return true;
    }

    return QWidget::event(event);
}

bool HotkeyEdit::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_display && event->type() == QEvent::MouseButtonPress && isEnabled()) {
        startRecording();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void HotkeyEdit::keyPressEvent(QKeyEvent *event)
{
    if (m_recording) {
        handleKeyPress(event);
        return;
    }
    QWidget::keyPressEvent(event);
}

void HotkeyEdit::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::EnabledChange && !isEnabled()) {
        stopRecording();
    }
    if (event->type() == QEvent::KeyboardLayoutChange) {
        updateDisplay();
    }
    QWidget::changeEvent(event);
}

void HotkeyEdit::startRecording()
{
    if (!isEnabled()) {
        return;
    }
    if (m_recording) {
        return;
    }

    if (s_recordingEdit && s_recordingEdit != this) {
        s_recordingEdit->stopRecording();
    }

    m_recording = true;
    s_recordingEdit = this;
    grabKeyboard();
    m_recordButton->setText(QStringLiteral("Cancel"));
    updateDisplay();
    emit recordingChanged(true);
}

void HotkeyEdit::handleKeyPress(QKeyEvent *event)
{
    if (!m_recording || event->isAutoRepeat()) {
        return;
    }

    if (event->key() == Qt::Key_Escape) {
        event->accept();
        stopRecording();
        return;
    }

    const HotkeySequence captured = HotkeySequence::fromKeyEvent(event);
    if (!captured.isValid()) {
        event->accept();
        return;
    }

    m_sequence = captured;
    event->accept();
    stopRecording();
    updateDisplay();
    emit sequenceChanged();
}

void HotkeyEdit::updateDisplay()
{
    if (m_recording) {
        m_display->setText(QStringLiteral("Press a shortcut\u2026"));
    } else if (m_sequence.isEmpty()) {
        m_display->setText(QStringLiteral("None"));
    } else {
        m_display->setText(m_sequence.displayString());
    }

    QString style = QStringLiteral("padding: 2px 8px;");
    if (m_recording) {
        style += QStringLiteral(" border: 1px solid #3d7ea6;");
    } else if (m_conflict) {
        style += QStringLiteral(" border: 1px solid #c44848; color: #c44848;");
    }
    m_display->setStyleSheet(style);
    m_clearButton->setEnabled(isEnabled() && !m_sequence.isEmpty() && !m_recording);
}
