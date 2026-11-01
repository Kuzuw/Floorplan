#include "DSP/SineTable.h"

#include <cmath>

namespace fp::dsp
{

namespace
{
struct TableData
{
    float values[SineTable::kSize + 1];

    TableData()
    {
        for (int i = 0; i <= SineTable::kSize; ++i)
            values[i] = (float) std::sin (2.0 * 3.14159265358979323846 * (double) i / (double) SineTable::kSize);
    }
};
} // namespace

const float* SineTable::table() noexcept
{
    static const TableData data; // thread-safe one-time init (C++11 magic statics)
    return data.values;
}

} // namespace fp::dsp
