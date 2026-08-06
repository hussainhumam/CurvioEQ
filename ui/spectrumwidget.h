#pragma once

#include <QWidget>
#include <QVector>

class SpectrumCapture;

class SpectrumWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SpectrumWidget(QWidget *parent = nullptr);

    void setCapture(SpectrumCapture *capture);
    void setActiveAppName(const QString &name);
    void setEqActive(bool active);

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onRefreshTimer();

private:
    SpectrumCapture *m_capture = nullptr;
    QString m_appName;
    bool m_eqActive = false;
    QVector<float> m_beforeBars;
    QVector<float> m_afterBars;
};
