#include "realfft.h"

#include <cmath>
#include <stdexcept>

namespace {

bool isPowerOfTwo(int value)
{
    return value > 0 && (value & (value - 1)) == 0;
}

} // namespace

void RealFft::configure(int size)
{
    if (!isPowerOfTwo(size)) {
        throw std::invalid_argument("RealFft size must be a power of two");
    }

    m_size = size;
    m_bitReversal.assign(static_cast<size_t>(size), 0);
    m_cosTable.assign(static_cast<size_t>(size / 2), 0.f);
    m_sinTable.assign(static_cast<size_t>(size / 2), 0.f);

    for (int i = 0; i < size; ++i) {
        int reversed = 0;
        int value = i;
        for (int bit = 0; bit < static_cast<int>(std::log2(size)); ++bit) {
            reversed = (reversed << 1) | (value & 1);
            value >>= 1;
        }
        m_bitReversal[static_cast<size_t>(i)] = reversed;
    }

    for (int i = 0; i < size / 2; ++i) {
        const float angle = -2.f * 3.14159265358979323846f * static_cast<float>(i) / static_cast<float>(size);
        m_cosTable[static_cast<size_t>(i)] = std::cos(angle);
        m_sinTable[static_cast<size_t>(i)] = std::sin(angle);
    }
}

void RealFft::forward(const float *input, std::vector<float> *real, std::vector<float> *imag) const
{
    if (!input || !real || !imag || m_size <= 0) {
        return;
    }

    real->assign(static_cast<size_t>(m_size), 0.f);
    imag->assign(static_cast<size_t>(m_size), 0.f);

    for (int i = 0; i < m_size; ++i) {
        (*real)[static_cast<size_t>(m_bitReversal[static_cast<size_t>(i)])] = input[i];
    }

    for (int span = 2; span <= m_size; span <<= 1) {
        const int halfSpan = span / 2;
        const int tableStep = m_size / span;
        for (int i = 0; i < m_size; i += span) {
            for (int j = 0; j < halfSpan; ++j) {
                const float cosVal = m_cosTable[static_cast<size_t>(j * tableStep)];
                const float sinVal = m_sinTable[static_cast<size_t>(j * tableStep)];

                const float evenReal = (*real)[static_cast<size_t>(i + j)];
                const float evenImag = (*imag)[static_cast<size_t>(i + j)];
                const float oddReal = (*real)[static_cast<size_t>(i + j + halfSpan)];
                const float oddImag = (*imag)[static_cast<size_t>(i + j + halfSpan)];

                const float twiddleReal = oddReal * cosVal - oddImag * sinVal;
                const float twiddleImag = oddReal * sinVal + oddImag * cosVal;

                (*real)[static_cast<size_t>(i + j)] = evenReal + twiddleReal;
                (*imag)[static_cast<size_t>(i + j)] = evenImag + twiddleImag;
                (*real)[static_cast<size_t>(i + j + halfSpan)] = evenReal - twiddleReal;
                (*imag)[static_cast<size_t>(i + j + halfSpan)] = evenImag - twiddleImag;
            }
        }
    }
}

void RealFft::inverse(const std::vector<float> &real, const std::vector<float> &imag, float *output) const
{
    if (!output || m_size <= 0 || static_cast<int>(real.size()) < m_size || static_cast<int>(imag.size()) < m_size) {
        return;
    }

    std::vector<float> bufferReal = real;
    std::vector<float> bufferImag = imag;

    for (int i = 1; i < m_size / 2; ++i) {
        bufferImag[static_cast<size_t>(i)] = -bufferImag[static_cast<size_t>(i)];
    }

    forward(bufferReal.data(), &bufferReal, &bufferImag);

    const float scale = 1.f / static_cast<float>(m_size);
    for (int i = 0; i < m_size; ++i) {
        output[i] = bufferReal[static_cast<size_t>(i)] * scale;
    }
}
