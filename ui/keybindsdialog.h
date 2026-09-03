#pragma once

#include "settingsstore.h"

#include <QDialog>
#include <QVector>
#include <array>

class QCheckBox;
class QEvent;
class QLabel;
class HotkeyEdit;

class KeybindsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit KeybindsDialog(const AppSettings &current, QWidget *parent = nullptr);
    ~KeybindsDialog() override;

    AppSettings resultSettings() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void accept() override;
    void onKeybindsEnabledToggled(bool enabled);
    void onSequenceChanged();

private:
    void updateEditorState();
    void refreshConflictsAndStatus();
    QVector<HotkeyEdit *> allEdits() const;

    QCheckBox *m_enableKeybindsCheck = nullptr;
    HotkeyEdit *m_eqToggleEdit = nullptr;
    HotkeyEdit *m_outputMuteEdit = nullptr;
    std::array<HotkeyEdit *, AppSettings::kEqColorKeybindCount> m_colorEdits{};
    QLabel *m_statusLabel = nullptr;
    AppSettings m_result;
};
