#include "Engine.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>

void Log(const std::string& text)
{
    const std::string line = text + "\r\n";
    DWORD written{};
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output && output != INVALID_HANDLE_VALUE)
        WriteFile(output, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
}

namespace {
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void CompareImage(const MBParams& p)
{
    const size_t count = static_cast<size_t>(p.width) * p.height;
    constexpr uint32_t guard = 0xA5C39E71u;
    std::vector<uint32_t> a(count + 2, guard), b(count + 2, guard);
    Require(GenerateCpp(&p, a.data() + 1, count, nullptr) == MB_STATUS_OK,
        "Błąd obliczeń C++.");
    Require(MandelbrotAsm(&p, b.data() + 1, count, nullptr) == MB_STATUS_OK,
        "Błąd obliczeń MASM.");
    Require(a.front() == guard && a.back() == guard && b.front() == guard && b.back() == guard,
        "Naruszenie granicy bufora.");
    for (size_t i = 0; i < count; ++i) {
        if (a[i + 1] != b[i + 1]) {
            std::ostringstream message;
            message << "Rozbieżność przy pikselu (" << i % p.width << ", " << i / p.width
                << "): C++ " << a[i + 1] << ", ASM " << b[i + 1];
            throw std::runtime_error(message.str());
        }
        Require(a[i + 1] <= p.maxIterations, "Wynik przekracza limit iteracji.");
    }
}

void Point(double re, double im, uint32_t expected)
{
    MBParams p{1, 1, 100, 0, re, re + 0.25, im, im + 0.25};
    uint32_t a{}, b{};
    Require(GenerateCpp(&p, &a, 1, nullptr) == MB_STATUS_OK, "Błąd testu punktu C++.");
    Require(MandelbrotAsm(&p, &b, 1, nullptr) == MB_STATUS_OK, "Błąd testu punktu ASM.");
    Require(a == expected && b == expected, "Nieprawidłowa liczba iteracji znanego punktu.");
}

void Invalid(const MBParams& p)
{
    uint32_t out = 0x12345678u;
    Require(GenerateCpp(&p, &out, UINT64_MAX, nullptr) == MB_INVALID_ARGUMENT,
        "C++ przyjął nieprawidłowe parametry.");
    Require(MandelbrotAsm(&p, &out, UINT64_MAX, nullptr) == MB_INVALID_ARGUMENT,
        "ASM przyjął nieprawidłowe parametry.");
    Require(out == 0x12345678u, "Zapis do bufora przy nieprawidłowych parametrach.");
}
}

int RunSelfTests(bool benchmark)
{
    try {
        Log("TESTY MandelbrotBenchmark x64");
        HMODULE module = GetModuleHandleW(L"MandelbrotAsm.dll");
        Require(module != nullptr, "Biblioteka DLL nie jest załadowana.");
        FARPROC exported = GetProcAddress(module, "MandelbrotAsm");
        Require(exported != nullptr, "Brak eksportu MandelbrotAsm.");
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            reinterpret_cast<const BYTE*>(module) + dos->e_lfanew);
        Require(nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64, "DLL nie jest x64.");
        Log("OK: załadowana DLL x64 i eksport MandelbrotAsm.");

        Point(0.0, 0.0, 100);
        Point(-1.0, 0.0, 100);
        Point(-2.0, 0.0, 100);
        Point(1.0, 0.0, 3);
        Point(2.0, 0.0, 2);
        Point(3.0, 0.0, 1);
        Point(0.0, 2.0, 2);
        Log("OK: 7 znanych punktów, w tym granica kwadratu modułu 4.");

        const uint32_t shapes[][2]{{1, 1}, {1, 19}, {23, 1}, {63, 47}, {128, 96}, {321, 241}};
        const uint32_t limits[]{1, 2, 50, 500};
        unsigned images{};
        for (const auto& shape : shapes) {
            for (uint32_t limit : limits) {
                CompareImage({shape[0], shape[1], limit, 0, -2.5, 1.0, -1.25, 1.25});
                ++images;
            }
        }
        CompareImage({127, 93, 1200, 0, -0.749, -0.739, 0.125, 0.135});
        ++images;
        CompareImage({17, 11, 100, 0, -1000000.0, 1000000.0, -1000000.0, 1000000.0});
        ++images;
        std::mt19937 generator(20261116);
        std::uniform_real_distribution<double> coordinate(-1.8, 0.8);
        for (int i = 0; i < 24; ++i) {
            const double re = coordinate(generator);
            const double im = coordinate(generator);
            MBParams p{1u + generator() % 80u, 1u + generator() % 60u, 300, 0,
                re, re + 0.1, im, im + 0.1};
            CompareImage(p);
            ++images;
        }
        Log("OK: " + std::to_string(images) + " obrazów, dokładna zgodność i wartownicy bufora.");

        MBParams valid{8, 6, 80, 0, -2.5, 1.0, -1.25, 1.25};
        MBParams p = valid;
        p.width = 0; Invalid(p);
        p = valid; p.height = 0; Invalid(p);
        p = valid; p.width = 8193; Invalid(p);
        p = valid; p.height = 8193; Invalid(p);
        p = valid; p.width = 8192; p.height = 8192; Invalid(p);
        p = valid; p.maxIterations = 0; Invalid(p);
        p = valid; p.maxIterations = 100001; Invalid(p);
        p = valid; p.reserved = 1; Invalid(p);
        p = valid; p.minRe = p.maxRe; Invalid(p);
        p = valid; p.minIm = p.maxIm + 1.0; Invalid(p);
        p = valid; p.minRe = std::numeric_limits<double>::quiet_NaN(); Invalid(p);
        p = valid; p.maxRe = std::numeric_limits<double>::infinity(); Invalid(p);
        p = valid; p.minIm = -std::numeric_limits<double>::infinity(); Invalid(p);
        p = valid; p.maxIm = 1000001.0; Invalid(p);
        uint32_t output = 0x12345678u;
        Require(GenerateCpp(nullptr, &output, 1, nullptr) == MB_INVALID_ARGUMENT,
            "C++ nie odrzucił NULL.");
        Require(MandelbrotAsm(nullptr, &output, 1, nullptr) == MB_INVALID_ARGUMENT,
            "ASM nie odrzucił NULL.");
        Require(GenerateCpp(&valid, nullptr, 100, nullptr) == MB_INVALID_ARGUMENT,
            "C++ nie odrzucił pustego bufora.");
        Require(MandelbrotAsm(&valid, nullptr, 100, nullptr) == MB_INVALID_ARGUMENT,
            "ASM nie odrzucił pustego bufora.");
        Require(GenerateCpp(&valid, &output, 0, nullptr) == MB_BUFFER_TOO_SMALL,
            "C++ nie odrzucił zbyt małego bufora.");
        Require(MandelbrotAsm(&valid, &output, 0, nullptr) == MB_BUFFER_TOO_SMALL,
            "ASM nie odrzucił zbyt małego bufora.");
        Log("OK: walidacja wymiarów, limitów, zakresów, NaN, nieskończoności, NULL i pojemności.");

        volatile LONG cancel = 1;
        Require(GenerateCpp(&valid, &output, 100, &cancel) == MB_CANCELLED,
            "C++ nie obsłużył anulowania.");
        Require(MandelbrotAsm(&valid, &output, 100, &cancel) == MB_CANCELLED,
            "ASM nie obsłużył anulowania.");
        Require(output == 0x12345678u, "Anulowana operacja zmieniła pierwszy piksel.");
        Log("OK: anulowanie przed pierwszym pikselem.");

        if (benchmark) {
            Request request;
            request.mode = RenderMode::Compare;
            auto result = Compute(request, nullptr);
            Require(result->status == MB_STATUS_OK && result->mismatches == 0,
                "Niepowodzenie benchmarku.");
            std::ostringstream message;
            message << std::fixed << std::setprecision(3)
                << "BENCHMARK Release: 800x600, 500 iteracji, mediana z 5 prób; C++ "
                << result->cppMs << " ms; ASM " << result->asmMs << " ms; ";
            if (result->asmMs > 0.0) message << "czas C++ / czas ASM " << result->cppMs / result->asmMs;
            message << "; rozbieżności " << result->mismatches << "; suma kontrolna "
                << result->checksum;
            Log(message.str());
        }
        Log("PASS: wszystkie testy obliczeniowe.");
        return 0;
    } catch (const std::exception& error) {
        Log(std::string("FAIL: ") + error.what());
        return 1;
    }
}
