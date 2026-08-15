#pragma once

#include <QMutex>
#include <QVector>
#include <vector>

class SpectrumCapture
{
public:
    static constexpr int kFftSize = 2048;
    static constexpr int kDisplayBars = 48;

    void reset();
    void pushBefore(const float *samples, int frameCount, int channelCount);
    void pushAfter(const float *samples, int frameCount, int channelCount);
    void pushBeforeAndAfter(const float *beforeSamples,
                            const float *afterSamples,
                            int frameCount,
                            int channelCount);
    void setSampleRate(int sampleRate);

    bool snapshot(std::vector<float> *before, std::vector<float> *after, int *sampleRate);

private:
    void pushRing(std::vector<float> *ring, size_t *writeIndex, bool *filled,
                  const float *samples, int frameCount, int channelCount);

    QMutex m_mutex;
    std::vector<float> m_beforeRing;
    std::vector<float> m_afterRing;
    size_t m_beforeWrite = 0;
    size_t m_afterWrite = 0;
    bool m_beforeFilled = false;
    bool m_afterFilled = false;
    int m_sampleRate = 48000;
};

class SpectrumAnalyzer
{
public:
    static void computeBarMagnitudes(const std::vector<float> &timeDomain,
                                     int sampleRate,
                                     int barCount,
                                     QVector<float> *magnitudes);

private:
    static void fftRadix2(std::vector<float> &real, std::vector<float> &imag);
};
