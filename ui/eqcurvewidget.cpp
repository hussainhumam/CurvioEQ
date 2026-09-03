#include "eqcurvewidget.h"

#include "ui/appconstants.h"

#include <QApplication>
#include <QColor>
#include <QContextMenuEvent>
#include <QFont>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineF>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWidgetAction>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {
constexpr int kCurvePoints = 160;
constexpr float kMinFreq = 20.f;
constexpr float kMaxFreq = 20000.f;
constexpr qreal kDragThresholdPx = 3.0;
}

EqCurveWidget::EqCurveWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(180);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setContextMenuPolicy(Qt::DefaultContextMenu);
    rebuildCurve();
}

void EqCurveWidget::setFilters(const EqFilter *filters, int count, float sampleRate)
{
    m_sampleRate = std::max(sampleRate, 1.f);
    m_filterCount = std::clamp(count, 0, EqState::kMaxParametricFilters);
    for (int i = 0; i < m_filterCount; ++i) {
        m_filters[static_cast<size_t>(i)] = filters[i];
    }
    if (m_hoverIndex >= m_filterCount) {
        m_hoverIndex = -1;
    }
    pruneSelection();
    rebuildCurve();
    update();
}

EqFilter EqCurveWidget::filterAt(int index) const
{
    if (index < 0 || index >= m_filterCount) {
        return {};
    }
    return m_filters[static_cast<size_t>(index)];
}

std::vector<int> EqCurveWidget::selectedIndices() const
{
    std::vector<int> indices;
    indices.reserve(static_cast<size_t>(m_selected.count()));
    for (int i = 0; i < m_filterCount; ++i) {
        if (m_selected.test(static_cast<size_t>(i))) {
            indices.push_back(i);
        }
    }
    return indices;
}

bool EqCurveWidget::isHandleSelected(int index) const
{
    return index >= 0 && index < m_filterCount && m_selected.test(static_cast<size_t>(index));
}

void EqCurveWidget::selectOnly(int index)
{
    m_selected.reset();
    if (index < 0 || index >= m_filterCount) {
        m_selectedIndex = -1;
        return;
    }
    m_selectedIndex = index;
    m_selected.set(static_cast<size_t>(index));
}

void EqCurveWidget::pruneSelection()
{
    for (int i = m_filterCount; i < EqState::kMaxParametricFilters; ++i) {
        m_selected.reset(static_cast<size_t>(i));
    }
    if (m_selectedIndex >= 0 && m_selectedIndex < m_filterCount) {
        m_selected.set(static_cast<size_t>(m_selectedIndex));
        return;
    }
    m_selectedIndex = -1;
    for (int i = 0; i < m_filterCount; ++i) {
        if (m_selected.test(static_cast<size_t>(i))) {
            m_selectedIndex = i;
            return;
        }
    }
}

void EqCurveWidget::setSelectedIndex(int index)
{
    selectOnly(index);
    update();
}

QRectF EqCurveWidget::plotRect() const
{
    return QRectF(rect()).adjusted(36, 12, -12, -24);
}

QPointF EqCurveWidget::plotToPoint(float freqHz, float gainDb) const
{
    const QRectF plot = plotRect();
    const float logMin = std::log10(kMinFreq);
    const float logMax = std::log10(kMaxFreq);
    const float clampedFreq = std::clamp(freqHz, kMinFreq, kMaxFreq);
    const float maxDb = static_cast<float>(AppConstants::kMaxGainDb);
    const float clampedGain = std::clamp(gainDb, -maxDb, maxDb);
    const float x = plot.left()
                    + (std::log10(clampedFreq) - logMin) / (logMax - logMin) * plot.width();
    const float y = plot.top()
                    + (1.f - (clampedGain + maxDb) / (2.f * maxDb)) * plot.height();
    return {x, y};
}

float EqCurveWidget::responseDbAt(float freqHz) const
{
    return EqResponse::cascadeMagnitudeDb(m_filters.data(), m_filterCount, freqHz, m_sampleRate);
}

QPointF EqCurveWidget::handlePoint(int index) const
{
    if (index < 0 || index >= m_filterCount) {
        return {};
    }
    const EqFilter &filter = m_filters[static_cast<size_t>(index)];
    // Sit on the composite curve (not raw filter gain — shelves/overlaps differ).
    return plotToPoint(filter.freqHz, responseDbAt(filter.freqHz));
}

void EqCurveWidget::pointToFilter(const QPointF &point, float *freqHz, float *gainDb) const
{
    const QRectF plot = plotRect();
    const float logMin = std::log10(kMinFreq);
    const float logMax = std::log10(kMaxFreq);
    const float nx = std::clamp(static_cast<float>((point.x() - plot.left()) / plot.width()), 0.f, 1.f);
    const float ny = std::clamp(static_cast<float>((point.y() - plot.top()) / plot.height()), 0.f, 1.f);
    const float maxDb = static_cast<float>(AppConstants::kMaxGainDb);
    if (freqHz) {
        *freqHz = std::pow(10.f, logMin + nx * (logMax - logMin));
    }
    if (gainDb) {
        *gainDb = maxDb - ny * (2.f * maxDb);
    }
}

void EqCurveWidget::rebuildCurve()
{
    m_curveFreqs.resize(static_cast<size_t>(kCurvePoints));
    m_curveDb.resize(static_cast<size_t>(kCurvePoints));
    const float logMin = std::log10(kMinFreq);
    const float logMax = std::log10(kMaxFreq);
    for (int i = 0; i < kCurvePoints; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kCurvePoints - 1);
        const float freq = std::pow(10.f, logMin + t * (logMax - logMin));
        m_curveFreqs[static_cast<size_t>(i)] = freq;
        m_curveDb[static_cast<size_t>(i)] =
            EqResponse::cascadeMagnitudeDb(m_filters.data(), m_filterCount, freq, m_sampleRate);
    }
}

int EqCurveWidget::hitTestHandle(const QPointF &pos) const
{
    for (int i = 0; i < m_filterCount; ++i) {
        if (QLineF(handlePoint(i), pos).length() <= 10.0) {
            return i;
        }
    }
    return -1;
}

float EqCurveWidget::clampedDragFrequency(int index, float requestedFreqHz) const
{
    float freq = std::clamp(requestedFreqHz, kMinFreq, kMaxFreq);
    if (index < 0 || index >= m_filterCount) {
        return freq;
    }

    // Keep dragged handle from crossing neighbors on the frequency axis.
    constexpr float kMinSeparationRatio = 1.05f;
    float lowerBound = kMinFreq;
    float upperBound = kMaxFreq;
    const float current = m_filters[static_cast<size_t>(index)].freqHz;

    for (int i = 0; i < m_filterCount; ++i) {
        if (i == index) {
            continue;
        }
        const float other = m_filters[static_cast<size_t>(i)].freqHz;
        if (other < current || (other == current && i < index)) {
            lowerBound = std::max(lowerBound, other * kMinSeparationRatio);
        } else {
            upperBound = std::min(upperBound, other / kMinSeparationRatio);
        }
    }

    if (lowerBound > upperBound) {
        return current;
    }
    return std::clamp(freq, lowerBound, upperBound);
}

QString EqCurveWidget::filterInfoText(EqFilterType type, float freqHz, float gainDb, float q) const
{
    QString typeName = QStringLiteral("Peaking");
    if (type == EqFilterType::LowShelf) {
        typeName = QStringLiteral("Low shelf");
    } else if (type == EqFilterType::HighShelf) {
        typeName = QStringLiteral("High shelf");
    }
    return QStringLiteral("%1    Fc  %2 Hz    Gain  %3 dB    Q  %4")
        .arg(typeName)
        .arg(freqHz, 0, 'f', 0)
        .arg(gainDb, 0, 'f', 1)
        .arg(q, 0, 'f', 2);
}

QString EqCurveWidget::filterTooltip(int index) const
{
    if (index < 0 || index >= m_filterCount) {
        return {};
    }
    const EqFilter &filter = m_filters[static_cast<size_t>(index)];
    return filterInfoText(filter.type, filter.freqHz, filter.gainDb, filter.q);
}

QString EqCurveWidget::proposedFilterTooltip() const
{
    return filterInfoText(EqFilterType::Peaking, m_ghostFreqHz, m_proposedGainDb, EqResponse::kDefaultQ);
}

QString EqCurveWidget::hoverReadoutText() const
{
    if (m_dragIndex >= 0 && m_dragIndex < m_filterCount) {
        return filterTooltip(m_dragIndex);
    }
    if (m_hoverIndex >= 0 && m_hoverIndex < m_filterCount) {
        return filterTooltip(m_hoverIndex);
    }
    if (m_ghostVisible) {
        return proposedFilterTooltip();
    }
    if (m_selectedIndex >= 0 && m_selectedIndex < m_filterCount) {
        return filterTooltip(m_selectedIndex);
    }
    return {};
}

EqFilter EqCurveWidget::proposedFilter() const
{
    EqFilter filter;
    filter.type = EqFilterType::Peaking;
    filter.freqHz = m_ghostFreqHz;
    filter.gainDb = m_proposedGainDb;
    filter.q = EqResponse::kDefaultQ;
    return filter;
}

void EqCurveWidget::rebuildPreviewCurve()
{
    m_previewDb.assign(static_cast<size_t>(kCurvePoints), 0.f);
    if (!m_ghostVisible || m_filterCount >= EqState::kMaxParametricFilters || m_curveFreqs.empty()) {
        return;
    }

    std::array<EqFilter, EqState::kMaxParametricFilters> previewFilters = m_filters;
    previewFilters[static_cast<size_t>(m_filterCount)] = proposedFilter();
    const int previewCount = m_filterCount + 1;
    for (int i = 0; i < static_cast<int>(m_curveFreqs.size()); ++i) {
        m_previewDb[static_cast<size_t>(i)] =
            EqResponse::cascadeMagnitudeDb(previewFilters.data(), previewCount,
                                          m_curveFreqs[static_cast<size_t>(i)], m_sampleRate);
    }
}

QPointF EqCurveWidget::ghostPoint() const
{
    return plotToPoint(m_ghostFreqHz, m_ghostTargetDb);
}

void EqCurveWidget::proposedAt(const QPointF &pos, float *freqHz, float *gainDb) const
{
    float freq = m_ghostFreqHz;
    float gain = m_proposedGainDb;
    if (!m_ghostVisible) {
        const float maxDb = static_cast<float>(AppConstants::kMaxGainDb);
        float targetDb = 0.f;
        pointToFilter(pos, &freq, &targetDb);
        freq = std::clamp(freq, kMinFreq, kMaxFreq);
        targetDb = std::clamp(targetDb, -maxDb, maxDb);
        gain = std::clamp(targetDb - responseDbAt(freq), -maxDb, maxDb);
    }
    freq = std::clamp(freq, kMinFreq, kMaxFreq);
    if (freqHz) {
        *freqHz = freq;
    }
    if (gainDb) {
        *gainDb = gain;
    }
}

void EqCurveWidget::clearGhost()
{
    if (!m_ghostVisible) {
        return;
    }
    m_ghostVisible = false;
    update();
}

void EqCurveWidget::updatePointerState(const QPointF &pos)
{
    const int hit = hitTestHandle(pos);
    const bool inPlot = plotRect().contains(pos);
    const bool dragging = m_dragIndex >= 0;
    const bool canAdd = m_filterCount < EqState::kMaxParametricFilters;

    bool ghostVisible = false;
    float ghostFreq = m_ghostFreqHz;
    float ghostTargetDb = m_ghostTargetDb;
    float proposedGain = m_proposedGainDb;
    if (!dragging && !m_marqueeActive && inPlot && hit < 0 && canAdd) {
        const float maxDb = static_cast<float>(AppConstants::kMaxGainDb);
        pointToFilter(pos, &ghostFreq, &ghostTargetDb);
        ghostFreq = std::clamp(ghostFreq, kMinFreq, kMaxFreq);
        ghostTargetDb = std::clamp(ghostTargetDb, -maxDb, maxDb);
        proposedGain = std::clamp(ghostTargetDb - responseDbAt(ghostFreq), -maxDb, maxDb);
        ghostVisible = true;
    }

    const bool ghostChanged = ghostVisible != m_ghostVisible
                              || (ghostVisible
                                  && (std::abs(ghostFreq - m_ghostFreqHz) > 0.01f
                                      || std::abs(ghostTargetDb - m_ghostTargetDb) > 0.01f
                                      || std::abs(proposedGain - m_proposedGainDb) > 0.01f));
    m_ghostVisible = ghostVisible;
    m_ghostFreqHz = ghostFreq;
    m_ghostTargetDb = ghostTargetDb;
    m_proposedGainDb = proposedGain;
    if (ghostChanged) {
        rebuildPreviewCurve();
    }

    const int previousHover = m_hoverIndex;
    m_hoverIndex = hit;
    if (hit < 0) {
        unsetCursor();
    } else {
        setCursor(Qt::PointingHandCursor);
    }

    if (ghostChanged || hit != previousHover) {
        update();
    }
}

void EqCurveWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF plot = plotRect();
    painter.fillRect(rect(), palette().base());
    painter.fillRect(plot, palette().alternateBase());
    painter.setPen(QPen(palette().mid(), 1));
    painter.drawRect(plot);

    const float maxDb = static_cast<float>(AppConstants::kMaxGainDb);
    painter.setPen(QPen(palette().mid(), 1, Qt::DotLine));
    for (float db : {-maxDb, -maxDb * 0.5f, 0.f, maxDb * 0.5f, maxDb}) {
        const float y = plot.top() + (1.f - (db + maxDb) / (2.f * maxDb)) * plot.height();
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        painter.drawText(QRectF(4, y - 8, 30, 16),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(static_cast<int>(db)));
    }

    for (float freq : {20.f, 100.f, 1000.f, 10000.f, 20000.f}) {
        const float logMin = std::log10(kMinFreq);
        const float logMax = std::log10(kMaxFreq);
        const float x = plot.left()
                        + (std::log10(freq) - logMin) / (logMax - logMin) * plot.width();
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        QString label;
        if (freq >= 1000.f) {
            label = QStringLiteral("%1k").arg(freq / 1000.f, 0, 'f', freq >= 10000.f ? 0 : 1);
        } else {
            label = QString::number(static_cast<int>(freq));
        }
        painter.drawText(QRectF(x - 18, plot.bottom() + 2, 36, 16), Qt::AlignCenter, label);
    }

    if (!m_curveFreqs.empty()) {
        if (m_ghostVisible && m_previewDb.size() == m_curveFreqs.size()) {
            QPainterPath previewPath;
            for (int i = 0; i < static_cast<int>(m_curveFreqs.size()); ++i) {
                const QPointF pt = plotToPoint(m_curveFreqs[static_cast<size_t>(i)],
                                               m_previewDb[static_cast<size_t>(i)]);
                if (i == 0) {
                    previewPath.moveTo(pt);
                } else {
                    previewPath.lineTo(pt);
                }
            }
            QColor previewColor = palette().mid().color();
            previewColor.setAlpha(110);
            painter.setPen(QPen(previewColor, 2));
            painter.drawPath(previewPath);
        }

        QPainterPath path;
        for (int i = 0; i < static_cast<int>(m_curveDb.size()); ++i) {
            const QPointF pt = plotToPoint(m_curveFreqs[static_cast<size_t>(i)],
                                           m_curveDb[static_cast<size_t>(i)]);
            if (i == 0) {
                path.moveTo(pt);
            } else {
                path.lineTo(pt);
            }
        }
        painter.setPen(QPen(palette().highlight(), 2));
        painter.drawPath(path);
    }

    for (int i = 0; i < m_filterCount; ++i) {
        const QPointF pt = handlePoint(i);
        const bool selected = isHandleSelected(i);
        const bool primary = (i == m_selectedIndex);
        painter.setBrush(selected ? palette().highlight() : palette().button());
        painter.setPen(QPen(palette().text(), primary ? 2 : 1));
        const qreal radius = selected ? 7.0 : 5.5;
        painter.drawEllipse(pt, radius, radius);
    }

    if (m_ghostVisible) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(Qt::white, 1.5));
        painter.drawEllipse(ghostPoint(), 5.5, 5.5);
    }

    if (m_marqueeActive && m_marqueeMoved) {
        const QRectF rect = marqueeRect();
        QColor fill = palette().highlight().color();
        fill.setAlpha(60);
        painter.fillRect(rect, fill);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(palette().highlight(), 1));
        painter.drawRect(rect);
    }

    const QString readout = hoverReadoutText();
    if (!readout.isEmpty()) {
        QFont readoutFont = painter.font();
        readoutFont.setItalic(true);
        painter.setFont(readoutFont);
        painter.setPen(QPen(Qt::white, 1));
        const QRectF textRect = plot.adjusted(8, 0, -8, -6);
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignBottom, readout);
    }
}

void EqCurveWidget::endDrag()
{
    const bool finishedMove = m_dragMoved && m_dragIndex >= 0;
    m_dragIndex = -1;
    m_dragMoved = false;
    if (finishedMove) {
        emit filtersMoveFinished();
    }
}

QRectF EqCurveWidget::marqueeRect() const
{
    return QRectF(m_marqueeOrigin, m_marqueeCurrent).normalized().intersected(plotRect());
}

void EqCurveWidget::selectHandlesInRect(const QRectF &rect, const QPointF &cursor)
{
    m_selected.reset();
    m_selectedIndex = -1;
    qreal bestDist = std::numeric_limits<qreal>::max();
    for (int i = 0; i < m_filterCount; ++i) {
        const QPointF pt = handlePoint(i);
        if (!rect.contains(pt)) {
            continue;
        }
        m_selected.set(static_cast<size_t>(i));
        const qreal dist = QLineF(pt, cursor).length();
        if (m_selectedIndex < 0 || dist < bestDist) {
            bestDist = dist;
            m_selectedIndex = i;
        }
    }
}

void EqCurveWidget::endMarquee()
{
    m_marqueeActive = false;
    m_marqueeMoved = false;
}

void EqCurveWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }

    const QPointF pos = event->position();
    const bool chained = m_leftClickClock.isValid()
                         && m_leftClickClock.elapsed() <= QApplication::doubleClickInterval()
                         && QLineF(m_lastLeftClickPos, pos).length() <= 8.0;
    m_leftClickCount = chained ? (m_leftClickCount + 1) : 1;
    m_lastLeftClickPos = pos;
    m_leftClickClock.restart();

    if (m_leftClickCount >= 2) {
        m_leftClickCount = 0;
        endDrag();
        endMarquee();
        if (m_filterCount < EqState::kMaxParametricFilters && plotRect().contains(pos)
            && hitTestHandle(pos) < 0) {
            float freqHz = 1000.f;
            float gainDb = 0.f;
            proposedAt(pos, &freqHz, &gainDb);
            emit addFilterRequested(freqHz, gainDb);
        }
        return;
    }

    const int hit = hitTestHandle(pos);
    if (hit >= 0) {
        if (!isHandleSelected(hit)) {
            selectOnly(hit);
        } else {
            m_selectedIndex = hit;
        }
        m_dragIndex = hit;
        m_dragStartPos = pos;
        m_dragStartFilters = m_filters;
        m_dragMoved = false;
        m_ghostVisible = false;
        emit filterSelected(hit);
        update();
        return;
    }

    if (plotRect().contains(pos)) {
        m_marqueeActive = true;
        m_marqueeMoved = false;
        m_marqueeOrigin = pos;
        m_marqueeCurrent = pos;
        m_ghostVisible = false;
        update();
        return;
    }

    selectOnly(-1);
    emit filterSelected(-1);
    update();
}

void EqCurveWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_marqueeActive) {
        if (!(event->buttons() & Qt::LeftButton)) {
            endMarquee();
            updatePointerState(event->position());
            return;
        }
        m_marqueeCurrent = event->position();
        if (!m_marqueeMoved) {
            if (QLineF(m_marqueeOrigin, m_marqueeCurrent).length() < kDragThresholdPx) {
                return;
            }
            m_marqueeMoved = true;
        }
        m_ghostVisible = false;
        selectHandlesInRect(marqueeRect(), m_marqueeCurrent);
        emit filterSelected(m_selectedIndex);
        update();
        return;
    }

    // Resize / leave can leave a stale drag; never move filters without LMB held.
    if (!(event->buttons() & Qt::LeftButton) || m_dragIndex < 0 || m_dragIndex >= m_filterCount) {
        if (m_dragIndex >= 0 && !(event->buttons() & Qt::LeftButton)) {
            endDrag();
        }
        updatePointerState(event->position());
        return;
    }

    if (m_ghostVisible) {
        m_ghostVisible = false;
        update();
    }

    if (!m_dragMoved) {
        if (QLineF(m_dragStartPos, event->position()).length() < kDragThresholdPx) {
            return;
        }
        m_dragMoved = true;
    }

    float freq = 0.f;
    float desiredResponseDb = 0.f;
    pointToFilter(event->position(), &freq, &desiredResponseDb);

    const float maxDb = static_cast<float>(AppConstants::kMaxGainDb);
    desiredResponseDb = std::clamp(desiredResponseDb, -maxDb, maxDb);
    const bool groupMove = m_selected.count() > 1 && isHandleSelected(m_dragIndex);

    if (!groupMove) {
        EqFilter &filter = m_filters[static_cast<size_t>(m_dragIndex)];
        freq = clampedDragFrequency(m_dragIndex, freq);
        filter.freqHz = freq;
        const float currentResponseDb = responseDbAt(freq);
        filter.gainDb = std::clamp(filter.gainDb + (desiredResponseDb - currentResponseDb),
                                   -maxDb, maxDb);
        rebuildCurve();
        emit filtersMoved();
        update();
        return;
    }

    const EqFilter &startPrimary = m_dragStartFilters[static_cast<size_t>(m_dragIndex)];
    const float startFreq = std::max(startPrimary.freqHz, kMinFreq);
    float ratio = freq / startFreq;
    float minRatio = 0.f;
    float maxRatio = 1.0e9f;
    for (int i = 0; i < m_filterCount; ++i) {
        if (!m_selected.test(static_cast<size_t>(i))) {
            continue;
        }
        const float start = std::max(m_dragStartFilters[static_cast<size_t>(i)].freqHz, kMinFreq);
        minRatio = std::max(minRatio, kMinFreq / start);
        maxRatio = std::min(maxRatio, kMaxFreq / start);
    }
    if (minRatio > maxRatio) {
        ratio = 1.f;
    } else {
        ratio = std::clamp(ratio, minRatio, maxRatio);
    }

    for (int i = 0; i < m_filterCount; ++i) {
        if (!m_selected.test(static_cast<size_t>(i))) {
            continue;
        }
        m_filters[static_cast<size_t>(i)].freqHz =
            std::clamp(m_dragStartFilters[static_cast<size_t>(i)].freqHz * ratio, kMinFreq, kMaxFreq);
        m_filters[static_cast<size_t>(i)].gainDb = m_dragStartFilters[static_cast<size_t>(i)].gainDb;
    }

    EqFilter &primary = m_filters[static_cast<size_t>(m_dragIndex)];
    const float currentResponseDb = responseDbAt(primary.freqHz);
    primary.gainDb = std::clamp(startPrimary.gainDb + (desiredResponseDb - currentResponseDb),
                                 -maxDb, maxDb);
    const float gainDelta = primary.gainDb - startPrimary.gainDb;
    for (int i = 0; i < m_filterCount; ++i) {
        if (i == m_dragIndex || !m_selected.test(static_cast<size_t>(i))) {
            continue;
        }
        m_filters[static_cast<size_t>(i)].gainDb =
            std::clamp(m_dragStartFilters[static_cast<size_t>(i)].gainDb + gainDelta, -maxDb, maxDb);
    }

    rebuildCurve();
    emit filtersMoved();
    update();
}

void EqCurveWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }
    if (m_marqueeActive) {
        if (!m_marqueeMoved) {
            selectOnly(-1);
            emit filterSelected(-1);
        }
        endMarquee();
        updatePointerState(event->position());
        update();
        return;
    }
    endDrag();
    updatePointerState(event->position());
}

void EqCurveWidget::leaveEvent(QEvent *)
{
    if (!m_marqueeActive) {
        endDrag();
        m_ghostVisible = false;
    }
    m_hoverIndex = -1;
    unsetCursor();
    update();
}

void EqCurveWidget::hideEvent(QHideEvent *event)
{
    endDrag();
    endMarquee();
    clearGhost();
    QWidget::hideEvent(event);
}

void EqCurveWidget::contextMenuEvent(QContextMenuEvent *event)
{
    const QPointF localPos = event->pos();
    const int hit = hitTestHandle(localPos);

    float freqHz = 1000.f;
    float proposedGain = 0.f;
    proposedAt(localPos, &freqHz, &proposedGain);

    if (hit >= 0) {
        selectOnly(hit);
        emit filterSelected(hit);
        update();
    }

    const int removeIndex = hit >= 0 ? hit : m_selectedIndex;
    const QString infoText = hit >= 0
                                 ? filterTooltip(hit)
                                 : filterInfoText(EqFilterType::Peaking, freqHz, proposedGain,
                                                 EqResponse::kDefaultQ);

    QMenu menu(this);
    auto *infoLabel = new QLabel(infoText, &menu);
    QFont infoFont = infoLabel->font();
    infoFont.setItalic(true);
    infoLabel->setFont(infoFont);
    infoLabel->setStyleSheet(QStringLiteral("color: #ffffff; padding: 4px 10px;"));
    auto *infoAction = new QWidgetAction(&menu);
    infoAction->setDefaultWidget(infoLabel);
    menu.addAction(infoAction);
    menu.addSeparator();
    QAction *addAction = menu.addAction(QStringLiteral("Add dot"));
    addAction->setEnabled(m_filterCount < EqState::kMaxParametricFilters);
    QAction *removeAction = menu.addAction(QStringLiteral("Remove dot"));
    removeAction->setEnabled(removeIndex >= 0 && removeIndex < m_filterCount);
    menu.addSeparator();
    QAction *resetAction = menu.addAction(QStringLiteral("Reset"));

    QAction *chosen = menu.exec(event->globalPos());
    if (chosen == addAction) {
        emit addFilterRequested(freqHz, proposedGain);
    } else if (chosen == removeAction) {
        emit removeFilterRequested(removeIndex);
    } else if (chosen == resetAction) {
        emit resetRequested();
    }
}

void EqCurveWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        if (m_dragIndex < 0 && !m_marqueeActive && m_selected.any()) {
            emit removeSelectedRequested();
            event->accept();
            return;
        }
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}
