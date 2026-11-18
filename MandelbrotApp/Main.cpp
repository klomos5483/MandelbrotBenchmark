#include "Engine.h"
#include <shellapi.h>
#include <cwchar>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    int count{};
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    bool selfTest = false, benchmark = false, guiSmoke = false, missingPreviewPath = false;
    std::wstring preview;
    for (int i = 1; arguments && i < count; ++i) {
        if (wcscmp(arguments[i], L"--self-test") == 0) selfTest = true;
        if (wcscmp(arguments[i], L"--benchmark") == 0) benchmark = true;
        if (wcscmp(arguments[i], L"--gui-smoke") == 0) guiSmoke = true;
        if (wcscmp(arguments[i], L"--ui-preview") == 0) {
            if (i + 1 < count) preview = arguments[++i];
            else missingPreviewPath = true;
        }
    }
    if (arguments) LocalFree(arguments);
    if (missingPreviewPath) { Log("Podaj ścieżkę pliku PNG dla podglądu interfejsu."); return 2; }
    if (selfTest) return RunSelfTests(benchmark);
    if (!preview.empty()) return RenderGuiPreview(instance, preview);
    return RunGui(instance, showCommand, guiSmoke);
}
