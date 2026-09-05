#include "spectrumwidget.h"

#include "spectrumanalyzer.h"

#include "appconstants.h"

#include <QCheckBox>
#include <QCursor>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

QPainterPath buildFilledCurvePath(const QRectF &plotRect, const QVector<float> &magnitudes)
{
    QPainterPath path;
    const int barCount = magnitudes.size();
    if (barCount < 2 || plotRect.width() <= 1.f || plotRect.height() <= 1.f) {
        return path;
    }

    const float plotWidth = plotRect.width();
    const float plotHeight = plotRect.height();
    const float plotLeft = plotRect.left();
    const float plotBottom = plotRect.bottom();
    const float barWidth = plotWidth / static_cast<float>(barCount);

    QVector<QPointF> points;
    points.reserve(barCount);
    for (int i = 0; i < barCount; ++i) {
        const float x = plotLeft + (static_cast<float>(i) + 0.5f) * barWidth;
        const float y = plotBottom - magnitudes.value(i) * plotHeight;
        points.append(QPointF(x, y));
    }

    path.moveTo(plotLeft, plotBottom);
    path.lineTo(points.first());

    for (int i = 0; i < barCount - 1; ++i) {
        const QPointF &p0 = (i > 0) ? points[i - 1] : points[i];
        const QPointF &p1 = points[i];
        const QPointF &p2 = points[i + 1];
        const QPointF &p3 = (i + 2 < barCount) ? points[i + 2] : points[i + 1];

        const QPointF c1 = p1 + (p2 - p0) * 0.2;
        const QPointF c2 = p2 - (p3 - p1) * 0.2;
        path.cubicTo(c1, c2, p2);
    }

    path.lineTo(points.last().x(), plotBottom);
    path.closeSubpath();
    return path;
}

constexpr int kPlotLeftMargin = 8;
constexpr int kPlotTopMargin = 4;
constexpr int kPlotBottomMargin = 8;
constexpr int kRightScaleWidth = 58;
constexpr int kHandleWidth = 54;
constexpr int kHandleHeight = 18;
constexpr float kScaleMarksDb[] = {0.f, -12.f, -24.f, -36.f, -48.f};

QString formatCeilingDb(float db)
{
    if (db > -0.05f) {
        return QStringLiteral("0.0 dB");
    }
    return QStringLiteral("%1 dB").arg(db, 0, 'f', 1);
}

QString formatScaleDb(float db)
{
    if (db > -0.05f) {
        return QStringLiteral("0");
    }
    return QString::number(static_cast<int>(std::lround(db)));
}

} // namespace

SpectrumPlotArea::SpectrumPlotArea(QWidget *parent)
    : QWidget(parent)
    , m_handleLabel(new QLabel(this))
{
    setMinimumHeight(56);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);

    m_handleLabel->setAlignment(Qt::AlignCenter);
    m_handleLabel->setAutoFillBackground(false);
    m_handleLabel->setCursor(Qt::OpenHandCursor);
    m_handleLabel->installEventFilter(this);
    layoutHandle();
}

QRect SpectrumPlotArea::plotRect() const
{
    return QRect(kPlotLeftMargin,
                 kPlotTopMargin,
                 std::max(1, width() - kPlotLeftMargin - kRightScaleWidth),
                 std::max(1, height() - kPlotTopMargin - kPlotBottomMargin));
}

QRect SpectrumPlotArea::handleRect() const
{
    if (m_handleLabel) {
        return m_handleLabel->geometry();
    }
    const QRect plot = plotRect();
    const int y = std::clamp(dbToY(m_ceilingDb) - kHandleHeight / 2,
                             plot.top(),
                             plot.bottom() - kHandleHeight);
    return QRect(plot.right() + 3, y, kHandleWidth, kHandleHeight);
}

void SpectrumPlotArea::layoutHandle()
{
    if (!m_handleLabel) {
        return;
    }
    m_handleLabel->setText(formatCeilingDb(m_ceilingDb));
    m_handleLabel->adjustSize();
    const QRect plot = plotRect();
    const int labelWidth = std::max(m_handleLabel->width(), kHandleWidth - 4);
    const int labelHeight = std::max(m_handleLabel->height(), kHandleHeight);
    m_handleLabel->resize(labelWidth, labelHeight);
    const int y = std::clamp(dbToY(m_ceilingDb) - labelHeight / 2,
                             plot.top(),
                             std::max(plot.top(), plot.bottom() - labelHeight));
    m_handleLabel->move(plot.right() + 4, y);
}

void SpectrumPlotArea::setGrabCursor(bool grabbing)
{
    const Qt::CursorShape shape = grabbing ? Qt::ClosedHandCursor : Qt::OpenHandCursor;
    setCursor(shape);
    if (m_handleLabel) {
        m_handleLabel->setCursor(shape);
    }
}

bool SpectrumPlotArea::isNearHandle(const QPoint &pos) const
{
    return handleRect().adjusted(-4, -2, 4, 2).contains(pos);
}

float SpectrumPlotArea::yToDb(int y) const
{
    const QRect area = plotRect();
    const float height = static_cast<float>(std::max(1, area.height()));
    const float t = std::clamp(static_cast<float>(y - area.top()) / height, 0.f, 1.f);
    const float db = t * AppConstants::kSpectrumLimiterMinDb;
    return std::clamp(db, AppConstants::kSpectrumLimiterMinDb, AppConstants::kSpectrumLimiterMaxDb);
}

int SpectrumPlotArea::dbToY(float db) const
{
    const QRect area = plotRect();
    const float clamped = std::clamp(db, AppConstants::kSpectrumLimiterMinDb, AppConstants::kSpectrumLimiterMaxDb);
    const float span = 0.f - AppConstants::kSpectrumLimiterMinDb;
    const float t = (0.f - clamped) / span;
    return area.top() + static_cast<int>(std::lround(t * static_cast<float>(area.height())));
}

bool SpectrumPlotArea::isNearCeiling(int y) const
{
    return std::abs(y - dbToY(m_ceilingDb)) <= AppConstants::kSpectrumLimiterGrabPx;
}

void SpectrumPlotArea::applyCeilingFromY(int y)
{
    const float db = std::round(yToDb(y) * 10.f) / 10.f;
    if (std::abs(db - m_ceilingDb) < 0.05f) {
        return;
    }
    m_ceilingDb = db;
    layoutHandle();
    update();
    emit ceilingChanged(m_ceilingDb);
}

void SpectrumPlotArea::updateHoverCursor(const QPoint &pos)
{
    const bool overControl = m_dragging || isNearHandle(pos)
        || (isNearCeiling(pos.y()) && plotRect().contains(pos));
    if (overControl) {
        setGrabCursor(m_dragging);
    } else {
        unsetCursor();
        if (m_handleLabel) {
            m_handleLabel->setCursor(Qt::OpenHandCursor);
        }
    }
}

void SpectrumPlotArea::setCurveData(const QVector<float> &beforeBars,
                                    const QVector<float> &afterBars,
                                    bool showCurves)
{
    m_beforeBars = beforeBars;
    m_afterBars = afterBars;
    m_showCurves = showCurves;
    update();
}

void SpectrumPlotArea::setCeilingDb(float db)
{
    const float clamped = std::clamp(db, AppConstants::kSpectrumLimiterMinDb, AppConstants::kSpectrumLimiterMaxDb);
    if (std::abs(clamped - m_ceilingDb) < 0.05f) {
        return;
    }
    m_ceilingDb = clamped;
    layoutHandle();
    update();
}

void SpectrumPlotArea::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), palette().window());

    const QRect plot = plotRect();
    painter.fillRect(plot, palette().alternateBase());
    painter.setPen(palette().mid().color());
    painter.drawRect(plot);

    QFont scaleFont = font();
    scaleFont.setPointSizeF(std::max(7.0, font().pointSizeF() - 1.5));
    painter.setFont(scaleFont);

    QColor gridColor = palette().mid().color();
    gridColor.setAlpha(90);
    QColor scaleColor = palette().text().color();
    scaleColor.setAlpha(160);

    for (float markDb : kScaleMarksDb) {
        const int y = std::clamp(dbToY(markDb), plot.top(), plot.bottom());
        painter.setPen(gridColor);
        painter.drawLine(plot.left() + 1, y, plot.right() - 1, y);
        painter.drawLine(plot.right() + 1, y, plot.right() + 6, y);
        const int ceilingY = dbToY(m_ceilingDb);
        if (std::abs(y - ceilingY) < handleRect().height() / 2 + 2) {
            continue;
        }
        painter.setPen(scaleColor);
        const QRect labelRect(plot.right() + 8, y - 8, kRightScaleWidth - 12, 16);
        painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, formatScaleDb(markDb));
    }

    if (m_showCurves) {
        const QRectF plotArea(static_cast<float>(plot.left() + 1),
                              static_cast<float>(plot.top() + 1),
                              static_cast<float>(plot.width() - 2),
                              static_cast<float>(plot.height() - 2));

        const QPainterPath beforePath = buildFilledCurvePath(plotArea, m_beforeBars);
        if (!beforePath.isEmpty()) {
            painter.fillPath(beforePath, QColor(140, 140, 140, 180));
            painter.setPen(QPen(QColor(120, 120, 120, 200), 1.2));
            painter.drawPath(beforePath);
        }

        const QPainterPath afterPath = buildFilledCurvePath(plotArea, m_afterBars);
        if (!afterPath.isEmpty()) {
            painter.fillPath(afterPath, QColor(40, 167, 69, 200));
            painter.setPen(QPen(QColor(30, 140, 55, 230), 1.2));
            painter.drawPath(afterPath);
        }
    }

    const QColor accent = palette().highlight().color();
    const int ceilingY = std::clamp(dbToY(m_ceilingDb), plot.top(), plot.bottom());
    QRect ceilingBand(plot.left() + 1, plot.top() + 1, std::max(1, plot.width() - 2),
                      std::max(0, ceilingY - plot.top()));
    QColor cutFill = accent;
    cutFill.setAlpha(36);
    painter.fillRect(ceilingBand, cutFill);

    painter.setRenderHint(QPainter::Antialiasing, false);
    QColor lineColor = accent;
    lineColor.setAlpha(200);
    QPen ceilingPen(lineColor, 1.0);
    ceilingPen.setStyle(Qt::DashLine);
    ceilingPen.setDashPattern({2, 3});
    painter.setPen(ceilingPen);
    painter.drawLine(plot.left() + 1, ceilingY, plot.right() - 1, ceilingY);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRect handle = handleRect();
    painter.setPen(QPen(lineColor, 1.0));
    painter.drawLine(plot.right() + 1, ceilingY, handle.left(), ceilingY);
}

void SpectrumPlotArea::mousePressEvent(QMouseEvent *event)
{
    const QRect plot = plotRect();
    const bool onHandle = isNearHandle(event->pos());
    const bool onLine = isNearCeiling(event->pos().y()) && plot.contains(event->pos());
    if (event->button() != Qt::LeftButton || (!onHandle && !onLine)) {
        QWidget::mousePressEvent(event);
        return;
    }

    m_dragging = true;
    grabMouse();
    setGrabCursor(true);
    applyCeilingFromY(event->pos().y());
    event->accept();
}

void SpectrumPlotArea::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging) {
        applyCeilingFromY(event->pos().y());
        event->accept();
        return;
    }

    updateHoverCursor(event->pos());
    QWidget::mouseMoveEvent(event);
}

void SpectrumPlotArea::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_dragging) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    m_dragging = false;
    releaseMouse();
    updateHoverCursor(event->pos());
    emit ceilingEditFinished();
    event->accept();
}

void SpectrumPlotArea::leaveEvent(QEvent *event)
{
    if (!m_dragging) {
        unsetCursor();
        if (m_handleLabel) {
            m_handleLabel->setCursor(Qt::OpenHandCursor);
        }
    }
    QWidget::leaveEvent(event);
}

void SpectrumPlotArea::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutHandle();
}

bool SpectrumPlotArea::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_handleLabel && event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            m_dragging = true;
            grabMouse();
            setGrabCursor(true);
            applyCeilingFromY(m_handleLabel->mapToParent(mouse->pos()).y());
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

SpectrumWidget::SpectrumWidget(QWidget *parent)
    : QWidget(parent)
    , m_enableCheckBox(new QCheckBox(QStringLiteral("Show spectrum"), this))
    , m_subtitleLabel(new QLabel(this))
    , m_plotArea(new SpectrumPlotArea(this))
    , m_refreshTimer(new QTimer(this))
{
    setMinimumHeight(96);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_beforeBars.fill(0.f, SpectrumCapture::kDisplayBars);
    m_afterBars.fill(0.f, SpectrumCapture::kDisplayBars);
    m_smoothedBeforeBars.fill(0.f, SpectrumCapture::kDisplayBars);
    m_smoothedAfterBars.fill(0.f, SpectrumCapture::kDisplayBars);
    m_displayBeforeBars.fill(0.f, SpectrumCapture::kDisplayBars);
    m_displayAfterBars.fill(0.f, SpectrumCapture::kDisplayBars);
    m_beforeSnapshot.resize(SpectrumCapture::kFftSize);
    m_afterSnapshot.resize(SpectrumCapture::kFftSize);

    m_enableCheckBox->setChecked(true);

    m_subtitleLabel->setText(QStringLiteral("Output ceiling — drag the line"));
    m_subtitleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto *headerRow = new QHBoxLayout();
    headerRow->setContentsMargins(8, 4, 8, 0);
    headerRow->setSpacing(12);
    headerRow->addWidget(m_enableCheckBox, 0);
    headerRow->addWidget(m_subtitleLabel, 1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 4);
    layout->setSpacing(2);
    layout->addLayout(headerRow);
    layout->addWidget(m_plotArea, 1);

    m_refreshTimer->setInterval(AppConstants::kSpectrumRefreshIntervalMs);
    connect(m_refreshTimer, &QTimer::timeout, this, &SpectrumWidget::onRefreshTimer);
    connect(m_enableCheckBox, &QCheckBox::toggled, this, &SpectrumWidget::onSpectrumToggled);
    connect(m_plotArea, &SpectrumPlotArea::ceilingChanged, this, [this](float db) {
        updateSubtitle();
        emit limiterCeilingChanged(db);
    });
    connect(m_plotArea, &SpectrumPlotArea::ceilingEditFinished, this, &SpectrumWidget::limiterCeilingEditFinished);
}

void SpectrumWidget::setCapture(SpectrumCapture *capture)
{
    m_capture = capture;
}

void SpectrumWidget::setActiveAppName(const QString &name)
{
    m_appName = name;
    updateSubtitle();
}

void SpectrumWidget::setEqActive(bool active)
{
    if (m_eqActive == active) {
        return;
    }

    m_eqActive = active;
    updateTimerState();
    if (!active) {
        clearCurveBuffers();
    }
    m_plotArea->setCurveData(m_displayBeforeBars, m_displayAfterBars,
                             m_eqActive && m_enableCheckBox->isChecked());
}

void SpectrumWidget::setSpectrumEnabled(bool enabled)
{
    if (m_enableCheckBox->isChecked() == enabled) {
        return;
    }

    m_enableCheckBox->blockSignals(true);
    m_enableCheckBox->setChecked(enabled);
    m_enableCheckBox->blockSignals(false);
    updateTimerState();
    if (!enabled) {
        clearCurveBuffers();
        m_plotArea->setCurveData(m_displayBeforeBars, m_displayAfterBars, false);
    }
}

bool SpectrumWidget::isSpectrumEnabled() const
{
    return m_enableCheckBox->isChecked();
}

void SpectrumWidget::setLimiterCeilingDb(float db)
{
    m_plotArea->setCeilingDb(db);
    updateSubtitle();
}

float SpectrumWidget::limiterCeilingDb() const
{
    return m_plotArea->ceilingDb();
}

void SpectrumWidget::onSpectrumToggled(bool checked)
{
    Q_UNUSED(checked)
    updateTimerState();
    if (!m_enableCheckBox->isChecked()) {
        clearCurveBuffers();
        m_plotArea->setCurveData(m_displayBeforeBars, m_displayAfterBars, false);
    }
    emit spectrumEnabledChanged(m_enableCheckBox->isChecked());
}

void SpectrumWidget::updateSubtitle()
{
    const QString ceiling = formatCeilingDb(m_plotArea->ceilingDb());
    if (!m_appName.isEmpty()) {
        m_subtitleLabel->setText(QStringLiteral("Ceiling %1 — %2").arg(ceiling, m_appName));
    } else {
        m_subtitleLabel->setText(QStringLiteral("Output ceiling %1 — drag the label").arg(ceiling));
    }
}

void SpectrumWidget::updateTimerState()
{
    if (m_eqActive && m_enableCheckBox->isChecked()) {
        m_refreshTimer->start();
    } else {
        m_refreshTimer->stop();
    }
}

void SpectrumWidget::clearCurveBuffers()
{
    m_beforeBars.fill(0.f);
    m_afterBars.fill(0.f);
    m_smoothedBeforeBars.fill(0.f);
    m_smoothedAfterBars.fill(0.f);
    m_displayBeforeBars.fill(0.f);
    m_displayAfterBars.fill(0.f);
}

void SpectrumWidget::applySmoothing(const QVector<float> &target, QVector<float> *smoothed)
{
    if (!smoothed || smoothed->size() != target.size()) {
        if (smoothed) {
            *smoothed = target;
        }
        return;
    }

    for (int i = 0; i < target.size(); ++i) {
        const float goal = target[i];
        float current = (*smoothed)[i];
        const float alpha = (goal >= current) ? AppConstants::kSpectrumAttackAlpha
                                              : AppConstants::kSpectrumReleaseAlpha;
        current += alpha * (goal - current);
        (*smoothed)[i] = current;
    }
}

void SpectrumWidget::onRefreshTimer()
{
    if (!m_capture || !m_eqActive || !m_enableCheckBox->isChecked()) {
        return;
    }

    int sampleRate = 48000;
    if (!m_capture->snapshot(&m_beforeSnapshot, &m_afterSnapshot, &sampleRate)) {
        m_plotArea->update();
        return;
    }

    SpectrumAnalyzer::computeBarMagnitudes(m_beforeSnapshot,
                                           sampleRate,
                                           SpectrumCapture::kDisplayBars,
                                           &m_beforeBars);
    SpectrumAnalyzer::computeBarMagnitudes(m_afterSnapshot,
                                           sampleRate,
                                           SpectrumCapture::kDisplayBars,
                                           &m_afterBars);

    applySmoothing(m_beforeBars, &m_smoothedBeforeBars);
    applySmoothing(m_afterBars, &m_smoothedAfterBars);
    m_displayBeforeBars = m_smoothedBeforeBars;
    m_displayAfterBars = m_smoothedAfterBars;
    m_plotArea->setCurveData(m_displayBeforeBars, m_displayAfterBars, true);
}
