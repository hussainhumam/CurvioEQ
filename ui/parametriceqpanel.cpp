#include "parametriceqpanel.h"

#include "eqcurvewidget.h"
#include "ui/appconstants.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <algorithm>

ParametricEqPanel::ParametricEqPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_curve = new EqCurveWidget(this);
    layout->addWidget(m_curve, 1);

    auto *editor = new QHBoxLayout();
    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItem(QStringLiteral("Peaking"), static_cast<int>(EqFilterType::Peaking));
    m_typeCombo->addItem(QStringLiteral("Low shelf"), static_cast<int>(EqFilterType::LowShelf));
    m_typeCombo->addItem(QStringLiteral("High shelf"), static_cast<int>(EqFilterType::HighShelf));

    m_freqSpin = new QDoubleSpinBox(this);
    m_freqSpin->setRange(20.0, 20000.0);
    m_freqSpin->setDecimals(1);
    m_freqSpin->setSuffix(QStringLiteral(" Hz"));

    m_gainSpin = new QDoubleSpinBox(this);
    m_gainSpin->setRange(-AppConstants::kMaxGainDb, AppConstants::kMaxGainDb);
    m_gainSpin->setDecimals(1);
    m_gainSpin->setSuffix(QStringLiteral(" dB"));

    m_qSpin = new QDoubleSpinBox(this);
    m_qSpin->setRange(0.05, 16.0);
    m_qSpin->setDecimals(2);
    m_qSpin->setSingleStep(0.05);

    editor->addWidget(new QLabel(QStringLiteral("Type"), this));
    editor->addWidget(m_typeCombo);
    editor->addWidget(new QLabel(QStringLiteral("Fc"), this));
    editor->addWidget(m_freqSpin);
    editor->addWidget(new QLabel(QStringLiteral("Gain"), this));
    editor->addWidget(m_gainSpin);
    editor->addWidget(new QLabel(QStringLiteral("Q"), this));
    editor->addWidget(m_qSpin);
    layout->addLayout(editor);

    connect(m_typeCombo, &QComboBox::currentIndexChanged, this, &ParametricEqPanel::onEditorChanged);
    connect(m_freqSpin, &QDoubleSpinBox::valueChanged, this, &ParametricEqPanel::onEditorChanged);
    connect(m_gainSpin, &QDoubleSpinBox::valueChanged, this, &ParametricEqPanel::onEditorChanged);
    connect(m_qSpin, &QDoubleSpinBox::valueChanged, this, &ParametricEqPanel::onEditorChanged);
    connect(m_curve, &EqCurveWidget::filterSelected, this, &ParametricEqPanel::selectFilter);
    connect(m_curve, &EqCurveWidget::filterMoved, this, &ParametricEqPanel::onCurveMoved);
    connect(m_curve, &EqCurveWidget::addFilterRequested, this, &ParametricEqPanel::onAddFilterRequested);
    connect(m_curve, &EqCurveWidget::removeFilterRequested, this, &ParametricEqPanel::onRemoveFilterRequested);

    m_state.advanced = true;
}

void ParametricEqPanel::setEqState(const EqState &state)
{
    m_updating = true;
    m_state = state;
    m_state.advanced = true;
    if (m_state.filterCount <= 0) {
        m_state = EqResponse::simpleToAdvanced(state.gainsDb);
    }
    m_curve->setFilters(m_state.filters.data(), m_state.filterCount);
    m_selectedIndex = m_state.filterCount > 0 ? 0 : -1;
    m_curve->setSelectedIndex(m_selectedIndex);
    loadEditorFromSelection();
    m_updating = false;
}

EqState ParametricEqPanel::eqState() const
{
    return m_state;
}

void ParametricEqPanel::clampSelection()
{
    if (m_state.filterCount <= 0) {
        m_selectedIndex = -1;
    } else if (m_selectedIndex < 0 || m_selectedIndex >= m_state.filterCount) {
        m_selectedIndex = std::clamp(m_selectedIndex, 0, m_state.filterCount - 1);
    }
}

void ParametricEqPanel::selectFilter(int index)
{
    if (index < 0 || index >= m_state.filterCount) {
        return;
    }
    m_selectedIndex = index;
    m_curve->setSelectedIndex(index);
    loadEditorFromSelection();
}

int ParametricEqPanel::insertFilterSorted(const EqFilter &filter)
{
    int insertAt = m_state.filterCount;
    for (int i = 0; i < m_state.filterCount; ++i) {
        if (filter.freqHz < m_state.filters[static_cast<size_t>(i)].freqHz) {
            insertAt = i;
            break;
        }
    }
    for (int i = m_state.filterCount; i > insertAt; --i) {
        m_state.filters[static_cast<size_t>(i)] = m_state.filters[static_cast<size_t>(i - 1)];
    }
    m_state.filters[static_cast<size_t>(insertAt)] = filter;
    ++m_state.filterCount;
    return insertAt;
}

void ParametricEqPanel::onAddFilterRequested(float freqHz, float gainDb)
{
    if (m_state.filterCount >= EqState::kMaxParametricFilters) {
        return;
    }
    EqFilter filter;
    filter.type = EqFilterType::Peaking;
    filter.freqHz = freqHz;
    filter.gainDb = gainDb;
    filter.q = EqResponse::kDefaultQ;
    m_selectedIndex = insertFilterSorted(filter);
    emitChanged();
    loadEditorFromSelection();
}

void ParametricEqPanel::onRemoveFilterRequested(int index)
{
    if (index < 0 || index >= m_state.filterCount) {
        return;
    }
    for (int i = index; i + 1 < m_state.filterCount; ++i) {
        m_state.filters[static_cast<size_t>(i)] = m_state.filters[static_cast<size_t>(i + 1)];
    }
    --m_state.filterCount;
    if (m_state.filterCount <= 0) {
        m_selectedIndex = -1;
    } else if (m_selectedIndex >= m_state.filterCount) {
        m_selectedIndex = m_state.filterCount - 1;
    } else if (m_selectedIndex > index) {
        --m_selectedIndex;
    } else if (m_selectedIndex == index) {
        m_selectedIndex = std::min(index, m_state.filterCount - 1);
    }
    emitChanged();
    loadEditorFromSelection();
}

void ParametricEqPanel::onEditorChanged()
{
    if (m_updating) {
        return;
    }
    pushEditorToSelection();
    emitChanged();
}

void ParametricEqPanel::onCurveMoved(int index, float freqHz, float gainDb)
{
    if (index < 0 || index >= m_state.filterCount) {
        return;
    }
    m_updating = true;
    m_state.filters[static_cast<size_t>(index)].freqHz = freqHz;
    m_state.filters[static_cast<size_t>(index)].gainDb = gainDb;
    m_selectedIndex = index;
    if (m_selectedIndex == index) {
        m_freqSpin->setValue(freqHz);
        m_gainSpin->setValue(gainDb);
    }
    m_updating = false;
    emitChanged();
}

void ParametricEqPanel::loadEditorFromSelection()
{
    clampSelection();
    const int row = m_selectedIndex;
    const bool valid = row >= 0 && row < m_state.filterCount;
    m_typeCombo->setEnabled(valid);
    m_freqSpin->setEnabled(valid);
    m_gainSpin->setEnabled(valid);
    m_qSpin->setEnabled(valid);
    if (!valid) {
        return;
    }

    m_updating = true;
    const EqFilter &filter = m_state.filters[static_cast<size_t>(row)];
    const int typeIndex = m_typeCombo->findData(static_cast<int>(filter.type));
    m_typeCombo->setCurrentIndex(typeIndex >= 0 ? typeIndex : 0);
    m_freqSpin->setValue(filter.freqHz);
    m_gainSpin->setValue(filter.gainDb);
    m_qSpin->setValue(filter.q);
    m_updating = false;
}

void ParametricEqPanel::pushEditorToSelection()
{
    clampSelection();
    const int row = m_selectedIndex;
    if (row < 0 || row >= m_state.filterCount) {
        return;
    }
    EqFilter &filter = m_state.filters[static_cast<size_t>(row)];
    filter.type = static_cast<EqFilterType>(m_typeCombo->currentData().toInt());
    filter.freqHz = static_cast<float>(m_freqSpin->value());
    filter.gainDb = static_cast<float>(m_gainSpin->value());
    filter.q = static_cast<float>(m_qSpin->value());
}

void ParametricEqPanel::emitChanged()
{
    m_state.advanced = true;
    m_state = EqResponse::advancedToSimple(m_state);
    m_state.advanced = true;
    clampSelection();
    m_curve->setFilters(m_state.filters.data(), m_state.filterCount);
    m_curve->setSelectedIndex(m_selectedIndex);
    emit eqChanged();
}
