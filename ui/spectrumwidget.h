#pragma once

#include "appconstants.h"

#include <QWidget>
#include <QVector>

#include <vector>

class QCheckBox;
class QLabel;
class QTimer;
class SpectrumCapture;

class SpectrumPlotArea : public QWidget
{
    Q_OBJECT

public:
    explicit SpectrumPlotArea(QWidget *parent = nullptr);

    void setCurveData(const QVector<float> &beforeBars, const QVector<float> &afterBars, bool showCurves);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<float> m_beforeBars;
    QVector<float> m_afterBars;
    bool m_showCurves = false;
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

signals:
    void spectrumEnabledChanged(bool enabled);

private slots:
    void onRefreshTimer();
    void onSpectrumToggled(bool checked);

private:
    void updateSubtitle();
    void updateTimerState();
    void clearCurveBuffers();
    void applySmoothing(const QVector<float> &target, QVector<float> *smoothed);
    void scaleBarsForDisplay();

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
    float m_displayScalePeak = AppConstants::kSpectrumYMinPeak;
    std::vector<float> m_beforeSnapshot;
    std::vector<float> m_afterSnapshot;
};
