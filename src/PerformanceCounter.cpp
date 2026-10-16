#include "PerformanceCounter.h"

PerformanceCounter::PerformanceCounter(std::string name, std::string units, const size_t bufferSize)
{
    assert(bufferSize > 0);
    m_headIndex = 0;
    m_count = 0;
    m_data = std::vector<float>(bufferSize);
    m_name = std::move(name);
    m_units = std::move(units);
}

void PerformanceCounter::add(float val)
{
    m_data[m_headIndex] = val;
    m_headIndex = (m_headIndex + 1) % m_data.size();
    if (m_count < m_data.size()) m_count++;
}

void PerformanceCounter::clear()
{
    m_headIndex = 0;
    m_count = 0;
}

size_t PerformanceCounter::count() const { return m_count; }
const std::string& PerformanceCounter::name() const { return m_name; }
const std::string& PerformanceCounter::unit() const { return m_units; }

const float* PerformanceCounter::begin() const
{
    return m_data.data();
}

const float* PerformanceCounter::end() const
{
    return m_data.data() + m_count;
}

float PerformanceCounter::min() const
{
    return m_count ? *std::min_element(begin(), end()) : 0.0f;
}
float PerformanceCounter::max() const
{
    return m_count ? *std::max_element(begin(), end()) : 0.0f;
}

float PerformanceCounter::mean() const
{
    if (!m_count) return 0.0f;
    double sum = 0.0;
    for (const float* p = begin(); p != end(); ++p)
    {
        sum += *p;
    }
    return static_cast<float>(sum / static_cast<double>(m_count));
}

float PerformanceCounter::variance() const
{
    if (m_count < 2) return 0.0f;
    double m = mean(), sum = 0.0;
    for (const float* p = begin(); p != end(); ++p)
    {
        double d = *p - m; sum += d * d;
    }
    return static_cast<float>(sum / static_cast<double>(m_count - 1));
}

float PerformanceCounter::stddev() const
{
    return std::sqrt(variance());
}

float PerformanceCounter::percentile(float p) const
{
    if (!m_count) return 0.0f;
    std::vector<float> tmp(begin(), end());
    const auto k = static_cast<size_t>(std::lround(p / 100.0f * static_cast<float>(m_count - 1)));
    std::ranges::nth_element(tmp, tmp.begin() + k);
    return tmp[k];
}

float PerformanceCounter::median() const
{
    return percentile(50.0f);
}

std::vector<float> PerformanceCounter::samples() const
{
    std::vector<float> out(m_count);
    const size_t start = (m_headIndex + m_data.size() - m_count) % m_data.size();
    for (size_t i = 0; i < m_count; i++)
    {
        out[i] = m_data[(start + i) % m_data.size()];
    }
    return out;
}

void PerformanceCounter::writeCsv(const char* path, const char* run) const
{
    bool exists = false;
    if (FILE* t = std::fopen(path, "r"))
    {
        exists = true;
        std::fclose(t);
    }

    FILE* f = std::fopen(path, "a");
    if (!f)
    {
        std::printf("Could not open %s\n", path);
        return;
    }
    if (!exists)
    {
        std::fprintf(f, "run,counter,unit,index,value\n");
    }

    std::vector<float> s = samples();
    for (size_t i = 0; i < s.size(); i++)
    {
        std::fprintf(f, "%s,%s,%s,%zu,%.5f\n", run, m_name.c_str(), m_units.c_str(), i, s[i]);
    }
    std::fclose(f);
}
