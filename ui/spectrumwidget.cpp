#include "spectrumwidget.h"

#include "spectrumanalyzer.h"

#include "appconstants.h"

#include <QPainter>
#include <QPaintEvent>
#include <QTimer>

SpectrumWidget::SpectrumWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(78);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_beforeBars.fill(0.f, SpectrumCapture::kDisplayBars);
    m_afterBars.fill(0.f, SpectrumCapture::kDisplayBars);

    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &SpectrumWidget::onRefreshTimer);
    timer->start(AppConstants::kSpectrumRefreshIntervalMs);
}

void SpectrumWidget::setCapture(SpectrumCapture *capture)
{
    m_capture = capture;
}

void SpectrumWidget::setActiveAppName(const QString &name)
{
    m_appName = name;
    update();
}

void SpectrumWidget::setEqActive(bool active)
{
    m_eqActive = active;
    update();
}

void SpectrumWidget::onRefreshTimer()
{
    if (!m_capture || !m_eqActive) {
        m_beforeBars.fill(0.f);
        m_afterBars.fill(0.f);
        update();
        return;
    }

    std::vector<float> before;
    std::vector<float> after;
    int sampleRate = 48000;
    if (!m_capture->snapshot(&before, &after, &sampleRate)) {
        update();
        return;
    }

    SpectrumAnalyzer::computeBarMagnitudes(before,
                                           sampleRate,
                                           SpectrumCapture::kDisplayBars,
                                           &m_beforeBars);
    SpectrumAnalyzer::computeBarMagnitudes(after,
                                           sampleRate,
                                           SpectrumCapture::kDisplayBars,
                                           &m_afterBars);
    update();
}

void SpectrumWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.fillRect(rect(), palette().window());

    constexpr int margin = 8;
    const int titleY = 14;
    const int plotTop = 24;
    const QRect plotRect(margin, plotTop, std::max(1, width() - margin * 2), std::max(1, height() - plotTop - margin));

    painter.setPen(palette().text().color());
    if (m_eqActive && !m_appName.isEmpty()) {
        painter.drawText(margin, titleY, QStringLiteral("Live spectrum — %1").arg(m_appName));
    } else {
        painter.drawText(margin, titleY, QStringLiteral("Live spectrum — no EQ active"));
    }

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

    if (!m_eqActive) {
        return;
    }

    const int barCount = SpectrumCapture::kDisplayBars;
    const float plotWidth = static_cast<float>(plotRect.width() - 2);
    const float plotHeight = static_cast<float>(plotRect.height() - 2);
    const float barWidth = plotWidth / static_cast<float>(barCount);
    const float gap = 1.f;
    const float plotLeft = static_cast<float>(plotRect.left() + 1);
    const float plotBottom = static_cast<float>(plotRect.bottom() - 1);

    for (int i = 0; i < barCount; ++i) {
        const float x = plotLeft + i * barWidth;
        const float beforeH = m_beforeBars.value(i) * plotHeight;
        const float afterH = m_afterBars.value(i) * plotHeight;

        painter.fillRect(QRectF(x, plotBottom - beforeH, barWidth - gap, beforeH),
                         QColor(140, 140, 140, 180));
        painter.fillRect(QRectF(x, plotBottom - afterH, barWidth - gap, afterH),
                         QColor(40, 167, 69, 220));
    }
}
