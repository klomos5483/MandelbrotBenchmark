#pragma once
#include <Windows.h>
#include <stdint.h>
#include <stddef.h>

/* Stały układ danych współdzielony przez C++ i MASM. Rozmiary bufora są w elementach. */
typedef struct MBParams {
    uint32_t width;
    uint32_t height;
    uint32_t maxIterations;
    uint32_t reserved;
    double minRe;
    double maxRe;
    double minIm;
    double maxIm;
} MBParams;

enum MBStatus {
    MB_STATUS_OK = 0,
    MB_INVALID_ARGUMENT = 1,
    MB_BUFFER_TOO_SMALL = 2,
    MB_CANCELLED = 3
};

#ifdef __cplusplus
static_assert(sizeof(MBParams) == 48, "Nieprawidłowy rozmiar MBParams.");
static_assert(offsetof(MBParams, minRe) == 16, "Nieprawidłowy układ MBParams.");
static_assert(offsetof(MBParams, maxIm) == 40, "Nieprawidłowy układ MBParams.");
extern "C" {
#endif

#ifndef MANDELBROT_ASM_BUILD
__declspec(dllimport)
#endif
int __cdecl MandelbrotAsm(const MBParams* params, uint32_t* output,
                         uint64_t capacity, const volatile LONG* cancelFlag);

#ifdef __cplusplus
}
#endif
