#include <Windows.h>

/* Punkt wejścia nie wykonuje obliczeń i nie uruchamia wątków. */
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID)
{
    return TRUE;
}
