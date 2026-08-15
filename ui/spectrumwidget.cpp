#include "spectrumwidget.h"

#include "spectrumanalyzer.h"

#include "appconstants.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

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

} // namespace

SpectrumPlotArea::SpectrumPlotArea(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(56);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
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

void SpectrumPlotArea::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), palette().window());

    constexpr int margin = 8;
    const QRect plotRect(margin, 4, std::max(1, width() - margin * 2), std::max(1, height() - margin));

    painter.fillRect(plotRect, palette().alternateBase());
    painter.setPen(palette().mid().color());
    painter.drawRect(plotRect);

    QColor gridColor = palette().mid().color();
    gridColor.setAlpha(100);
    painter.setPen(gridColor);
    for (int i = 1; i <= 3; ++i) {
        const int y = plotRect.top() + (plotRect.height() * i) / 4;
        painter.drawLine(plotRect.left() + 1, y, plotRect.right() - 1, y);
    }

    if (!m_showCurves) {
        return;
    }

    const QRectF plotArea(static_cast<float>(plotRect.left() + 1),
                          static_cast<float>(plotRect.top() + 1),
                          static_cast<float>(plotRect.width() - 2),
                          static_cast<float>(plotRect.height() - 2));

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

    m_subtitleLabel->setText(QStringLiteral("Live spectrum — no EQ active"));
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
    m_plotArea->setCurveData(m_displayBeforeBars, m_displayAfterBars, m_eqActive);
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
    if (!m_appName.isEmpty()) {
        m_subtitleLabel->setText(QStringLiteral("Live spectrum — %1").arg(m_appName));
    } else {
        m_subtitleLabel->setText(QStringLiteral("Live spectrum — no EQ active"));
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
    m_displayScalePeak = AppConstants::kSpectrumYMinPeak;
}

void SpectrumWidget::scaleBarsForDisplay()
{
    float peak = 0.f;
    for (int i = 0; i < m_smoothedBeforeBars.size(); ++i) {
        peak = std::max(peak, m_smoothedBeforeBars[i]);
        peak = std::max(peak, m_smoothedAfterBars[i]);
    }

    float targetPeak = std::max(peak * AppConstants::kSpectrumYHeadroom, AppConstants::kSpectrumYMinPeak);
    const float alpha = (targetPeak >= m_displayScalePeak) ? AppConstants::kSpectrumAttackAlpha
                                                           : AppConstants::kSpectrumReleaseAlpha;
    m_displayScalePeak += alpha * (targetPeak - m_displayScalePeak);
    m_displayScalePeak = std::max(m_displayScalePeak, AppConstants::kSpectrumYMinPeak);

    const float scale = m_displayScalePeak;
    if (m_displayBeforeBars.size() != m_smoothedBeforeBars.size()) {
        m_displayBeforeBars.resize(m_smoothedBeforeBars.size());
        m_displayAfterBars.resize(m_smoothedAfterBars.size());
    }

    for (int i = 0; i < m_smoothedBeforeBars.size(); ++i) {
        m_displayBeforeBars[i] = std::min(1.f, m_smoothedBeforeBars[i] / scale);
        m_displayAfterBars[i] = std::min(1.f, m_smoothedAfterBars[i] / scale);
    }
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
    scaleBarsForDisplay();
    m_plotArea->setCurveData(m_displayBeforeBars, m_displayAfterBars, true);
}
