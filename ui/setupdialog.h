#pragma once

#include "settingsstore.h"

#include <QDialog>

class QComboBox;
class QLabel;

class SetupDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SetupDialog(const AppSettings &current, QWidget *parent = nullptr);

    AppSettings resultSettings() const;

private slots:
    void accept() override;

private:
    void populateDevices();

    QComboBox *m_routingSinkCombo = nullptr;
    QComboBox *m_eqOutputCombo = nullptr;
    QLabel *m_infoLabel = nullptr;
    AppSettings m_result;
};
