#pragma once

#include <vector>

class RealFft
{
public:
    void configure(int size);

    int size() const { return m_size; }

    void forward(const float *input, std::vector<float> *real, std::vector<float> *imag) const;
    void inverse(const std::vector<float> &real, const std::vector<float> &imag, float *output) const;

private:
    int m_size = 0;
    std::vector<int> m_bitReversal;
    std::vector<float> m_cosTable;
    std::vector<float> m_sinTable;
};
