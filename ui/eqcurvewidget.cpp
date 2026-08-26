#include "eqcurvewidget.h"

#include "ui/appconstants.h"

#include <QContextMenuEvent>
#include <QHideEvent>
#include <QLineF>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kCurvePoints = 160;
constexpr float kMinFreq = 20.f;
constexpr float kMaxFreq = 20000.f;
}

EqCurveWidget::EqCurveWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(180);
    setMouseTracking(true);
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
    if (m_selectedIndex >= m_filterCount) {
        m_selectedIndex = m_filterCount > 0 ? 0 : -1;
    }
    if (m_hoverIndex >= m_filterCount) {
        m_hoverIndex = -1;
        QToolTip::hideText();
    }
    rebuildCurve();
    update();
}

void EqCurveWidget::setSelectedIndex(int index)
{
    if (index < 0 || index >= m_filterCount) {
        m_selectedIndex = -1;
    } else {
        m_selectedIndex = index;
    }
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

QString EqCurveWidget::filterTooltip(int index) const
{
    if (index < 0 || index >= m_filterCount) {
        return {};
    }
    const EqFilter &filter = m_filters[static_cast<size_t>(index)];
    QString typeName = QStringLiteral("Peaking");
    if (filter.type == EqFilterType::LowShelf) {
        typeName = QStringLiteral("Low shelf");
    } else if (filter.type == EqFilterType::HighShelf) {
        typeName = QStringLiteral("High shelf");
    }
    return QStringLiteral("%1\nFc  %2 Hz\nGain  %3 dB\nQ  %4")
        .arg(typeName)
        .arg(filter.freqHz, 0, 'f', 0)
        .arg(filter.gainDb, 0, 'f', 1)
        .arg(filter.q, 0, 'f', 2);
}

void EqCurveWidget::updateHoverTooltip(const QPointF &pos)
{
    const int hit = hitTestHandle(pos);
    if (hit == m_hoverIndex) {
        return;
    }
    m_hoverIndex = hit;
    if (hit < 0) {
        QToolTip::hideText();
        unsetCursor();
        return;
    }
    setCursor(Qt::PointingHandCursor);
    QToolTip::showText(mapToGlobal(pos.toPoint()), filterTooltip(hit), this);
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

    if (!m_curveDb.empty()) {
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
        const bool selected = (i == m_selectedIndex);
        painter.setBrush(selected ? palette().highlight() : palette().button());
        painter.setPen(QPen(palette().text(), selected ? 2 : 1));
        painter.drawEllipse(pt, selected ? 7.0 : 5.5, selected ? 7.0 : 5.5);
    }
}

void EqCurveWidget::endDrag()
{
    m_dragIndex = -1;
    m_dragMoved = false;
}

void EqCurveWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const int hit = hitTestHandle(event->position());
    if (hit >= 0) {
        m_selectedIndex = hit;
        m_dragIndex = hit;
        m_dragStartPos = event->position();
        m_dragMoved = false;
        emit filterSelected(hit);
        update();
    }
}

void EqCurveWidget::mouseMoveEvent(QMouseEvent *event)
{
    // Resize / leave can leave a stale drag; never move filters without LMB held.
    if (!(event->buttons() & Qt::LeftButton) || m_dragIndex < 0 || m_dragIndex >= m_filterCount) {
        if (m_dragIndex >= 0 && !(event->buttons() & Qt::LeftButton)) {
            endDrag();
        }
        updateHoverTooltip(event->position());
        return;
    }

    constexpr qreal kDragThresholdPx = 3.0;
    if (!m_dragMoved) {
        if (QLineF(m_dragStartPos, event->position()).length() < kDragThresholdPx) {
            return;
        }
        m_dragMoved = true;
    }

    EqFilter &filter = m_filters[static_cast<size_t>(m_dragIndex)];
    float freq = filter.freqHz;
    float desiredResponseDb = 0.f;
    pointToFilter(event->position(), &freq, &desiredResponseDb);
    freq = clampedDragFrequency(m_dragIndex, freq);

    const float maxDb = static_cast<float>(AppConstants::kMaxGainDb);
    desiredResponseDb = std::clamp(desiredResponseDb, -maxDb, maxDb);

    // Move filter gain so the composite response at Fc follows the pointer.
    filter.freqHz = freq;
    const float currentResponseDb = responseDbAt(freq);
    filter.gainDb = std::clamp(filter.gainDb + (desiredResponseDb - currentResponseDb),
                               -maxDb, maxDb);

    rebuildCurve();
    emit filterMoved(m_dragIndex, filter.freqHz, filter.gainDb);
    update();
}

void EqCurveWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        endDrag();
    }
}

void EqCurveWidget::leaveEvent(QEvent *)
{
    endDrag();
    m_hoverIndex = -1;
    QToolTip::hideText();
    unsetCursor();
}

void EqCurveWidget::hideEvent(QHideEvent *event)
{
    endDrag();
    QWidget::hideEvent(event);
}

void EqCurveWidget::contextMenuEvent(QContextMenuEvent *event)
{
    const QPointF localPos = event->pos();
    const int hit = hitTestHandle(localPos);

    float freqHz = 1000.f;
    float gainDb = 0.f;
    pointToFilter(localPos, &freqHz, &gainDb);
    const float maxDb = static_cast<float>(AppConstants::kMaxGainDb);
    gainDb = std::clamp(gainDb, -maxDb, maxDb);
    freqHz = std::clamp(freqHz, kMinFreq, kMaxFreq);

    if (hit >= 0) {
        m_selectedIndex = hit;
        emit filterSelected(hit);
        update();
    }

    const int removeIndex = hit >= 0 ? hit : m_selectedIndex;

    QMenu menu(this);
    QAction *addAction = menu.addAction(QStringLiteral("Add dot"));
    addAction->setEnabled(m_filterCount < EqState::kMaxParametricFilters);
    QAction *removeAction = menu.addAction(QStringLiteral("Remove dot"));
    removeAction->setEnabled(removeIndex >= 0 && removeIndex < m_filterCount);

    QAction *chosen = menu.exec(event->globalPos());
    if (chosen == addAction) {
        emit addFilterRequested(freqHz, gainDb);
    } else if (chosen == removeAction) {
        emit removeFilterRequested(removeIndex);
    }
}
