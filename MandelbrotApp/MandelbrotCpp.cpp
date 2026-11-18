#include "Engine.h"
#include <array>
#include <cmath>
#include <algorithm>

int Validate(const MBParams* p, const uint32_t* output, uint64_t capacity) noexcept
{
    if (!p || !output) return MB_INVALID_ARGUMENT;
    if (p->width < 1 || p->width > 8192 || p->height < 1 || p->height > 8192 ||
        p->maxIterations < 1 || p->maxIterations > 100000 || p->reserved != 0)
        return MB_INVALID_ARGUMENT;
    const double values[]{p->minRe, p->maxRe, p->minIm, p->maxIm};
    for (double value : values) {
        if (!std::isfinite(value) || value < -1000000.0 || value > 1000000.0)
            return MB_INVALID_ARGUMENT;
    }
    if (!(p->minRe < p->maxRe) || !(p->minIm < p->maxIm))
        return MB_INVALID_ARGUMENT;
    const uint64_t count = uint64_t(p->width) * p->height;
    if (count > 16777216) return MB_INVALID_ARGUMENT;
    if (capacity < count) return MB_BUFFER_TOO_SMALL;
    return MB_STATUS_OK;
}

int __cdecl GenerateCpp(const MBParams* p, uint32_t* output, uint64_t capacity,
                        const volatile LONG* cancelFlag) noexcept
{
    const int status = Validate(p, output, capacity);
    if (status != MB_STATUS_OK) return status;
    const double stepX = (p->maxRe - p->minRe) /
        static_cast<double>(p->width > 1 ? p->width - 1 : 1);
    const double stepY = (p->maxIm - p->minIm) /
        static_cast<double>(p->height > 1 ? p->height - 1 : 1);
    uint64_t index = 0;
    // Dokumentowana dyrektywa MSVC blokuje wektoryzację pętli referencyjnej.
    #pragma loop(no_vector)
    for (uint32_t y = 0; y < p->height; ++y) {
        const double ci = static_cast<double>(y) * stepY + p->minIm;
        #pragma loop(no_vector)
        for (uint32_t x = 0; x < p->width; ++x) {
            // Windows /volatile:ms: odczyt LONG, zapis przez InterlockedExchange.
            if (cancelFlag && *cancelFlag) return MB_CANCELLED;
            const double cr = static_cast<double>(x) * stepX + p->minRe;
            double zr = 0.0;
            double zi = 0.0;
            uint32_t n = 0;
            #pragma loop(no_vector)
            for (;;) {
                const double zr2 = zr * zr;
                const double zi2 = zi * zi;
                if (zr2 + zi2 > 4.0 || n >= p->maxIterations) break;
                const double product = zr * zi;
                const double nextIm = (product + product) + ci;
                const double nextRe = (zr2 - zi2) + cr;
                zr = nextRe;
                zi = nextIm;
                ++n;
            }
            output[index++] = n;
        }
    }
    return MB_STATUS_OK;
}

void Colorize(const std::vector<uint32_t>& iterations, uint32_t limit,
              std::vector<uint32_t>& pixels)
{
    // Wspólna paleta leśna: sosna, mech, złote światło i jasne liście.
    constexpr std::array<std::array<int, 3>, 5> stops{{
        {{12, 29, 23}}, {{29, 75, 54}}, {{72, 128, 86}},
        {{190, 176, 99}}, {{236, 242, 216}}
    }};
    std::array<uint32_t, 256> palette{};
    for (size_t i = 0; i < palette.size(); ++i) {
        const double t = static_cast<double>(i) * 4.0 / 255.0;
        const size_t segment = std::min<size_t>(static_cast<size_t>(t), 3);
        const double f = t - static_cast<double>(segment);
        const auto channel = [&](size_t c) {
            return static_cast<uint32_t>(stops[segment][c] +
                (stops[segment + 1][c] - stops[segment][c]) * f);
        };
        palette[i] = (channel(0) << 16) | (channel(1) << 8) | channel(2);
    }
    pixels.resize(iterations.size());
    for (size_t i = 0; i < iterations.size(); ++i) {
        const uint32_t n = iterations[i];
        pixels[i] = n >= limit ? 0x000D1C15u : palette[(n * 7u) % 256u];
    }
}
