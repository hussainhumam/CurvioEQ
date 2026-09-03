#pragma once

#include "hotkeysequence.h"

#include <QWidget>

class QEvent;
class QKeyEvent;
class QLabel;
class QPushButton;

class HotkeyEdit : public QWidget
{
    Q_OBJECT

public:
    explicit HotkeyEdit(QWidget *parent = nullptr);

    HotkeySequence sequence() const { return m_sequence; }
    void setSequence(const HotkeySequence &sequence);
    void setStoredString(const QString &text);
    QString storedString() const;

    bool isRecording() const { return m_recording; }
    void stopRecording();
    void setConflict(bool conflict);
    void refreshDisplay();

signals:
    void sequenceChanged();
    void recordingChanged(bool recording);

protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void startRecording();
    void handleKeyPress(QKeyEvent *event);
    void updateDisplay();

    QLabel *m_display = nullptr;
    QPushButton *m_recordButton = nullptr;
    QPushButton *m_clearButton = nullptr;
    HotkeySequence m_sequence;
    bool m_recording = false;
    bool m_conflict = false;
};
