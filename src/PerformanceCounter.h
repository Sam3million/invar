#pragma once

#include <string>
#include <vector>
#include <cassert>

class PerformanceCounter
{
    size_t m_headIndex;
    size_t m_count;
    std::vector<float> m_data;
    std::string m_name;
    std::string m_units;
    [[nodiscard]] const float* begin() const;
    [[nodiscard]] const float* end() const;

public:
    PerformanceCounter(std::string name, std::string units, size_t bufferSize);
    void add(float val);
    void clear();
    [[nodiscard]] size_t count() const;
    [[nodiscard]] const std::string& name() const;
    [[nodiscard]] const std::string& unit() const;
    [[nodiscard]] float min() const;
    [[nodiscard]] float max() const;
    [[nodiscard]] float mean() const;
    [[nodiscard]] float variance() const;
    [[nodiscard]] float stddev() const;
    [[nodiscard]] float percentile(float p) const;
    [[nodiscard]] float median() const;
    [[nodiscard]] std::vector<float> samples() const;
    void writeCsv(const char* path, const char* run) const;
};
