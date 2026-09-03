#pragma once

#include "audio/audiochainorder.h"

#include <QDialog>

class QListWidget;
class QPushButton;

class AudioChainDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AudioChainDialog(const AudioChainOrder &current, QWidget *parent = nullptr);

    AudioChainOrder order() const;

signals:
    void orderChanged(const AudioChainOrder &order);

private:
    void populateList(const AudioChainOrder &order);
    void emitCurrentOrder();
    void moveSelection(int delta);
    void updateButtons();

    QListWidget *m_list = nullptr;
    QPushButton *m_upButton = nullptr;
    QPushButton *m_downButton = nullptr;
    bool m_updating = false;
};
