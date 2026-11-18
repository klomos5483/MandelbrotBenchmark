#pragma once
#include <Windows.h>
#include <string>
#include <cstdint>

namespace ForestUi {
constexpr COLORREF Background = RGB(237, 243, 236);
constexpr COLORREF Header = RGB(22, 49, 37);
constexpr COLORREF Panel = RGB(248, 251, 246);
constexpr COLORREF Ink = RGB(29, 56, 42);
constexpr COLORREF Muted = RGB(99, 121, 104);
constexpr COLORREF Border = RGB(202, 217, 200);
constexpr COLORREF Primary = RGB(47, 104, 70);
constexpr COLORREF PrimaryHover = RGB(58, 122, 83);
constexpr COLORREF Moss = RGB(140, 163, 104);
constexpr COLORREF Canvas = RGB(13, 28, 21);
constexpr COLORREF Error = RGB(148, 76, 57);

struct Resources {
    HFONT body{}, label{}, title{}, metric{}, metricSmall{}, captionFont{}, control{};
    HBRUSH background{}, panel{}, field{};
    ULONG_PTR graphicsToken{};
    explicit Resources(int dpi);
    ~Resources();
    Resources(const Resources&) = delete;
    Resources& operator=(const Resources&) = delete;
};

void Fill(HDC dc, const RECT& rect, COLORREF color);
void Round(HDC dc, const RECT& rect, int radius, COLORREF fill, COLORREF border);
void Label(HDC dc, const std::wstring& value, RECT rect, HFONT font, COLORREF color,
           UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
void Button(HDC dc, RECT rect, const std::wstring& caption, HFONT font, int radius,
            bool primary, bool cancel, bool enabled, bool hot, bool pressed, bool focus);
std::wstring Number(double value, int decimals = 3);
std::wstring Integer(uint64_t value);
bool SavePng(const std::wstring& file, int width, int height, void* pixels);
}
