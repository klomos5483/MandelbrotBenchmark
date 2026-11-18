#pragma once
#include "../Shared/MandelbrotApi.h"
#include <string>
#include <vector>
#include <memory>

enum class RenderMode { Cpp, Asm, Compare };

struct Request {
    MBParams params{800, 600, 500, 0, -2.5, 1.0, -1.25, 1.25};
    RenderMode mode{RenderMode::Compare};
    int repetitions{5};
};

struct Result {
    Request request;
    int status{MB_STATUS_OK};
    double cppMs{-1.0};
    double asmMs{-1.0};
    uint64_t mismatches{};
    uint64_t checksum{};
    std::wstring differences;
    std::wstring error;
    std::vector<uint32_t> pixels;
};

int Validate(const MBParams* p, const uint32_t* output, uint64_t capacity) noexcept;
int __cdecl GenerateCpp(const MBParams* p, uint32_t* output, uint64_t capacity,
                        const volatile LONG* cancelFlag) noexcept;
std::unique_ptr<Result> Compute(const Request& request, const volatile LONG* cancelFlag);
void Colorize(const std::vector<uint32_t>& iterations, uint32_t limit,
              std::vector<uint32_t>& pixels);
void Log(const std::string& text);
int RunSelfTests(bool benchmark);
int RunGui(HINSTANCE instance, int showCommand, bool smokeTest);
int RenderGuiPreview(HINSTANCE instance, const std::wstring& file);
