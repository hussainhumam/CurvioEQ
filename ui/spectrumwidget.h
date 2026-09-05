#pragma once

#include "appconstants.h"

#include <QWidget>
#include <QVector>

#include <vector>

class QCheckBox;
class QLabel;
class QMouseEvent;
class QTimer;
class SpectrumCapture;

class SpectrumPlotArea : public QWidget
{
    Q_OBJECT

public:
    explicit SpectrumPlotArea(QWidget *parent = nullptr);

    void setCurveData(const QVector<float> &beforeBars, const QVector<float> &afterBars, bool showCurves);
    void setCeilingDb(float db);
    float ceilingDb() const { return m_ceilingDb; }

signals:
    void ceilingChanged(float db);
    void ceilingEditFinished();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QRect plotRect() const;
    QRect handleRect() const;
    float yToDb(int y) const;
    int dbToY(float db) const;
    bool isNearCeiling(int y) const;
    bool isNearHandle(const QPoint &pos) const;
    void applyCeilingFromY(int y);
    void updateHoverCursor(const QPoint &pos);
    void layoutHandle();
    void setGrabCursor(bool grabbing);

    QLabel *m_handleLabel = nullptr;
    QVector<float> m_beforeBars;
    QVector<float> m_afterBars;
    bool m_showCurves = false;
    float m_ceilingDb = AppConstants::kSpectrumLimiterMaxDb;
    bool m_dragging = false;
};

class SpectrumWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SpectrumWidget(QWidget *parent = nullptr);

    void setCapture(SpectrumCapture *capture);
    void setActiveAppName(const QString &name);
    void setEqActive(bool active);
    void setSpectrumEnabled(bool enabled);
    bool isSpectrumEnabled() const;
    void setLimiterCeilingDb(float db);
    float limiterCeilingDb() const;

signals:
    void spectrumEnabledChanged(bool enabled);
    void limiterCeilingChanged(float db);
    void limiterCeilingEditFinished();

private slots:
    void onRefreshTimer();
    void onSpectrumToggled(bool checked);

private:
    void updateSubtitle();
    void updateTimerState();
    void clearCurveBuffers();
    void applySmoothing(const QVector<float> &target, QVector<float> *smoothed);

    SpectrumCapture *m_capture = nullptr;
    QCheckBox *m_enableCheckBox = nullptr;
    QLabel *m_subtitleLabel = nullptr;
    SpectrumPlotArea *m_plotArea = nullptr;
    QTimer *m_refreshTimer = nullptr;
    QString m_appName;
    bool m_eqActive = false;
    QVector<float> m_beforeBars;
    QVector<float> m_afterBars;
    QVector<float> m_smoothedBeforeBars;
    QVector<float> m_smoothedAfterBars;
    QVector<float> m_displayBeforeBars;
    QVector<float> m_displayAfterBars;
    std::vector<float> m_beforeSnapshot;
    std::vector<float> m_afterSnapshot;
};
