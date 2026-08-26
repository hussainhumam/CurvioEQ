#pragma once

#include "audio/eqstate.h"

#include <QWidget>

#include <vector>

class EqCurveWidget : public QWidget
{
    Q_OBJECT

public:
    explicit EqCurveWidget(QWidget *parent = nullptr);

    void setFilters(const EqFilter *filters, int count, float sampleRate = 48000.f);
    void setSelectedIndex(int index);
    int selectedIndex() const { return m_selectedIndex; }
    int filterCount() const { return m_filterCount; }

signals:
    void filterSelected(int index);
    void filterMoved(int index, float freqHz, float gainDb);
    void addFilterRequested(float freqHz, float gainDb);
    void removeFilterRequested(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    QRectF plotRect() const;
    QPointF plotToPoint(float freqHz, float gainDb) const;
    QPointF handlePoint(int index) const;
    float responseDbAt(float freqHz) const;
    void pointToFilter(const QPointF &point, float *freqHz, float *gainDb) const;
    void rebuildCurve();
    int hitTestHandle(const QPointF &pos) const;
    float clampedDragFrequency(int index, float requestedFreqHz) const;
    QString filterTooltip(int index) const;
    void updateHoverTooltip(const QPointF &pos);
    void endDrag();

    std::array<EqFilter, EqState::kMaxParametricFilters> m_filters{};
    int m_filterCount = 0;
    float m_sampleRate = 48000.f;
    int m_selectedIndex = -1;
    int m_dragIndex = -1;
    int m_hoverIndex = -1;
    QPointF m_dragStartPos{};
    bool m_dragMoved = false;
    std::vector<float> m_curveFreqs;
    std::vector<float> m_curveDb;
};
