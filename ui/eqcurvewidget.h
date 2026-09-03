#pragma once

#include "audio/eqstate.h"

#include <QElapsedTimer>
#include <QWidget>

#include <array>
#include <bitset>
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
    EqFilter filterAt(int index) const;
    std::vector<int> selectedIndices() const;

signals:
    void filterSelected(int index);
    void filtersMoved();
    void filtersMoveFinished();
    void addFilterRequested(float freqHz, float gainDb);
    void removeFilterRequested(int index);
    void removeSelectedRequested();
    void resetRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QRectF plotRect() const;
    QPointF plotToPoint(float freqHz, float gainDb) const;
    QPointF handlePoint(int index) const;
    float responseDbAt(float freqHz) const;
    void pointToFilter(const QPointF &point, float *freqHz, float *gainDb) const;
    void rebuildCurve();
    int hitTestHandle(const QPointF &pos) const;
    float clampedDragFrequency(int index, float requestedFreqHz) const;
    QString filterInfoText(EqFilterType type, float freqHz, float gainDb, float q) const;
    QString filterTooltip(int index) const;
    QString proposedFilterTooltip() const;
    QString hoverReadoutText() const;
    EqFilter proposedFilter() const;
    void rebuildPreviewCurve();
    void updatePointerState(const QPointF &pos);
    void clearGhost();
    QPointF ghostPoint() const;
    void proposedAt(const QPointF &pos, float *freqHz, float *gainDb) const;
    bool isHandleSelected(int index) const;
    void selectOnly(int index);
    void pruneSelection();
    QRectF marqueeRect() const;
    void selectHandlesInRect(const QRectF &rect, const QPointF &cursor);
    void endMarquee();
    void endDrag();

    std::array<EqFilter, EqState::kMaxParametricFilters> m_filters{};
    int m_filterCount = 0;
    float m_sampleRate = 48000.f;
    int m_selectedIndex = -1;
    std::bitset<EqState::kMaxParametricFilters> m_selected{};
    int m_dragIndex = -1;
    int m_hoverIndex = -1;
    bool m_ghostVisible = false;
    float m_ghostFreqHz = 1000.f;
    float m_ghostTargetDb = 0.f;
    float m_proposedGainDb = 0.f;
    QPointF m_dragStartPos{};
    std::array<EqFilter, EqState::kMaxParametricFilters> m_dragStartFilters{};
    bool m_dragMoved = false;
    bool m_marqueeActive = false;
    bool m_marqueeMoved = false;
    QPointF m_marqueeOrigin{};
    QPointF m_marqueeCurrent{};
    QPointF m_lastLeftClickPos{};
    int m_leftClickCount = 0;
    QElapsedTimer m_leftClickClock;
    std::vector<float> m_curveFreqs;
    std::vector<float> m_curveDb;
    std::vector<float> m_previewDb;
};
