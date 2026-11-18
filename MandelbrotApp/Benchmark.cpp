#include "Engine.h"
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <iomanip>

namespace {
using Kernel = int (__cdecl*)(const MBParams*, uint32_t*, uint64_t, const volatile LONG*);

double Median(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    const size_t middle = values.size() / 2;
    return values.size() % 2 ? values[middle] : (values[middle - 1] + values[middle]) / 2.0;
}

double Measure(Kernel kernel, const MBParams& p, std::vector<uint32_t>& output,
               const volatile LONG* cancelFlag, LARGE_INTEGER frequency, int& status)
{
    LARGE_INTEGER begin{}, end{};
    QueryPerformanceCounter(&begin);
    status = kernel(&p, output.data(), output.size(), cancelFlag);
    QueryPerformanceCounter(&end);
    return static_cast<double>(end.QuadPart - begin.QuadPart) * 1000.0 /
        static_cast<double>(frequency.QuadPart);
}
}

std::unique_ptr<Result> Compute(const Request& request, const volatile LONG* cancelFlag)
{
    auto result = std::make_unique<Result>();
    result->request = request;
    const auto& p = request.params;
    uint32_t dummy{};
    result->status = Validate(&p, &dummy, UINT64_MAX);
    if (result->status != MB_STATUS_OK || request.repetitions < 1 || request.repetitions > 100) {
        result->status = MB_INVALID_ARGUMENT;
        result->error = L"Nieprawidłowe parametry obliczeń.";
        return result;
    }
    const size_t count = static_cast<size_t>(p.width) * p.height;
    std::vector<uint32_t> cpp(count);
    std::vector<uint32_t> asmOutput;
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
        throw std::runtime_error("Nie można odczytać częstotliwości zegara.");

    if (request.mode == RenderMode::Compare) {
        asmOutput.resize(count);
        // Alokacja i pierwsze dotknięcie stron pamięci są poza pomiarem.
        result->status = GenerateCpp(&p, cpp.data(), count, cancelFlag);
        if (result->status != MB_STATUS_OK) return result;
        result->status = MandelbrotAsm(&p, asmOutput.data(), count, cancelFlag);
        if (result->status != MB_STATUS_OK) return result;
        std::vector<double> cppTimes, asmTimes;
        cppTimes.reserve(request.repetitions);
        asmTimes.reserve(request.repetitions);
        for (int i = 0; i < request.repetitions; ++i) {
            const auto runCpp = [&] {
                cppTimes.push_back(Measure(GenerateCpp, p, cpp, cancelFlag, frequency, result->status));
            };
            const auto runAsm = [&] {
                asmTimes.push_back(Measure(MandelbrotAsm, p, asmOutput, cancelFlag, frequency, result->status));
            };
            if (i % 2 == 0) {
                runCpp();
                if (result->status != MB_STATUS_OK) return result;
                runAsm();
            } else {
                runAsm();
                if (result->status != MB_STATUS_OK) return result;
                runCpp();
            }
            if (result->status != MB_STATUS_OK) return result;
        }
        result->cppMs = Median(cppTimes);
        result->asmMs = Median(asmTimes);
        std::wostringstream differences;
        for (size_t i = 0; i < count; ++i) {
            if (cpp[i] != asmOutput[i]) {
                if (result->mismatches < 8) {
                    differences << L"(" << i % p.width << L", " << i / p.width
                        << L"): C++ " << cpp[i] << L", ASM " << asmOutput[i] << L"\r\n";
                }
                ++result->mismatches;
            }
        }
        result->differences = differences.str();
    } else {
        const Kernel kernel = request.mode == RenderMode::Cpp ? GenerateCpp : MandelbrotAsm;
        const double ms = Measure(kernel, p, cpp, cancelFlag, frequency, result->status);
        if (request.mode == RenderMode::Cpp) result->cppMs = ms;
        else result->asmMs = ms;
        if (result->status != MB_STATUS_OK) return result;
    }
    // Wynik jest używany poza odcinkiem pomiarowym, również w trybie bez GUI.
    uint64_t hash = 14695981039346656037ull;
    for (uint32_t value : cpp) {
        hash ^= value;
        hash *= 1099511628211ull;
    }
    result->checksum = hash;
    Colorize(cpp, p.maxIterations, result->pixels);
    return result;
}
