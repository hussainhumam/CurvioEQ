#pragma once

#include "settingsstore.h"

#include <QDialog>
#include <array>

class QCheckBox;
class QKeySequenceEdit;

class KeybindsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit KeybindsDialog(const AppSettings &current, QWidget *parent = nullptr);

    AppSettings resultSettings() const;

private slots:
    void accept() override;
    void onKeybindsEnabledToggled(bool enabled);

private:
    void updateEditorState();
    QStringList assignedKeybinds() const;

    QCheckBox *m_enableKeybindsCheck = nullptr;
    QKeySequenceEdit *m_eqToggleEdit = nullptr;
    QKeySequenceEdit *m_outputMuteEdit = nullptr;
    std::array<QKeySequenceEdit *, AppSettings::kEqColorKeybindCount> m_colorEdits{};
    AppSettings m_result;
};
