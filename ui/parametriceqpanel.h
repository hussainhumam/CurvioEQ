#pragma once

#include "audio/eqstate.h"

#include <QWidget>

class EqCurveWidget;
class QComboBox;
class QDoubleSpinBox;

class ParametricEqPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ParametricEqPanel(QWidget *parent = nullptr);

    void setEqState(const EqState &state);
    EqState eqState() const;
    int selectedIndex() const { return m_selectedIndex; }

signals:
    void eqChanged();

public slots:
    void selectFilter(int index);

private slots:
    void onAddFilterRequested(float freqHz, float gainDb);
    void onRemoveFilterRequested(int index);
    void onEditorChanged();
    void onCurveMoved(int index, float freqHz, float gainDb);

private:
    void loadEditorFromSelection();
    void pushEditorToSelection();
    void emitChanged();
    int insertFilterSorted(const EqFilter &filter);
    void clampSelection();

    EqCurveWidget *m_curve = nullptr;
    QComboBox *m_typeCombo = nullptr;
    QDoubleSpinBox *m_freqSpin = nullptr;
    QDoubleSpinBox *m_gainSpin = nullptr;
    QDoubleSpinBox *m_qSpin = nullptr;

    EqState m_state{};
    int m_selectedIndex = -1;
    bool m_updating = false;
};
