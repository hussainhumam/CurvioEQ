#pragma once

#include <QDialog>

class QCheckBox;
class QDialogButtonBox;
class QLineEdit;
class QPushButton;

class SavePresetDialog : public QDialog
{
    Q_OBJECT

public:
    SavePresetDialog(const QString &defaultName, bool advancedEq, QWidget *parent = nullptr);

    QString presetName() const;
    bool includeEq() const;
    bool includeSurround() const;
    bool includeDynamics() const;
    bool includeAudioChain() const;

private slots:
    void onAllClicked();
    void updateOkEnabled();

private:
    QLineEdit *m_nameEdit = nullptr;
    QCheckBox *m_eqCheck = nullptr;
    QCheckBox *m_surroundCheck = nullptr;
    QCheckBox *m_dynamicsCheck = nullptr;
    QCheckBox *m_chainCheck = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};
