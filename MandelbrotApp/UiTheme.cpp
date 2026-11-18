#include "UiTheme.h"
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace ForestUi {
namespace {
Gdiplus::Color Color(COLORREF c)
{
    return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
}

HFONT Font(int dpi, int points, int weight, const wchar_t* face)
{
    return CreateFontW(-MulDiv(points, dpi, 72), 0, 0, 0, weight,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}
}

Resources::Resources(int dpi)
{
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&graphicsToken, &input, nullptr) != Gdiplus::Ok)
        throw std::runtime_error("Nie można uruchomić rysowania interfejsu.");
    body = Font(dpi, 10, FW_NORMAL, L"Segoe UI");
    label = Font(dpi, 10, FW_SEMIBOLD, L"Segoe UI");
    title = Font(dpi, 29, FW_NORMAL, L"Cambria");
    metric = Font(dpi, 24, FW_SEMIBOLD, L"Segoe UI");
    metricSmall = Font(dpi, 18, FW_SEMIBOLD, L"Segoe UI");
    captionFont = Font(dpi, 9, FW_NORMAL, L"Segoe UI");
    control = Font(dpi, 10, FW_SEMIBOLD, L"Segoe UI");
    background = CreateSolidBrush(Background);
    panel = CreateSolidBrush(Panel);
    field = CreateSolidBrush(RGB(255, 255, 255));
}

Resources::~Resources()
{
    for (HFONT font : {body, label, title, metric, metricSmall, captionFont, control}) if (font) DeleteObject(font);
    for (HBRUSH brush : {background, panel, field}) if (brush) DeleteObject(brush);
    if (graphicsToken) Gdiplus::GdiplusShutdown(graphicsToken);
}

void Fill(HDC dc, const RECT& rect, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void Round(HDC dc, const RECT& rect, int radius, COLORREF fill, COLORREF border)
{
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const float x = static_cast<float>(rect.left) + 0.5f;
    const float y = static_cast<float>(rect.top) + 0.5f;
    const float w = static_cast<float>(rect.right - rect.left) - 1.0f;
    const float h = static_cast<float>(rect.bottom - rect.top) - 1.0f;
    const float d = std::min({static_cast<float>(radius * 2), w, h});
    if (d <= 0.0f) return;
    Gdiplus::GraphicsPath path;
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
    Gdiplus::SolidBrush brush(Color(fill));
    Gdiplus::Pen pen(Color(border), 1.0f);
    graphics.FillPath(&brush, &path);
    graphics.DrawPath(&pen, &path);
}

void Label(HDC dc, const std::wstring& value, RECT rect, HFONT font, COLORREF color, UINT flags)
{
    const int saved = SaveDC(dc);
    SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &rect, flags | DT_NOPREFIX);
    RestoreDC(dc, saved);
}

void Button(HDC dc, RECT rect, const std::wstring& caption, HFONT font, int radius,
            bool primary, bool cancel, bool enabled, bool hot, bool pressed, bool focus)
{
    COLORREF fill = primary ? Primary : RGB(237, 244, 234);
    COLORREF border = primary ? Primary : Border;
    COLORREF ink = primary ? RGB(255, 255, 255) : Ink;
    if (cancel) { fill = RGB(247, 236, 230); border = RGB(221, 193, 179); ink = Error; }
    if (hot && enabled) {
        fill = primary ? PrimaryHover : cancel ? RGB(241, 221, 209) : RGB(224, 236, 219);
        border = primary ? PrimaryHover : cancel ? RGB(195, 149, 125) : Moss;
    }
    if (pressed && enabled) fill = primary ? RGB(33, 80, 51) : RGB(207, 225, 202);
    if (!enabled) { fill = RGB(233, 238, 230); border = RGB(219, 226, 216); ink = RGB(148, 161, 146); }
    Round(dc, rect, radius, fill, border);
    if (focus && enabled) {
        RECT focusRect = rect;
        InflateRect(&focusRect, -3, -3);
        Round(dc, focusRect, std::max(2, radius - 2), fill,
            primary ? RGB(201, 225, 181) : Primary);
    }
    if (pressed) OffsetRect(&rect, 0, 1);
    Label(dc, caption, rect, font, ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

std::wstring Number(double value, int decimals)
{
    std::wostringstream out;
    out << std::fixed << std::setprecision(decimals) << value;
    std::wstring text = out.str();
    std::replace(text.begin(), text.end(), L'.', L',');
    return text;
}

std::wstring Integer(uint64_t value)
{
    std::wstring result = std::to_wstring(value);
    for (int i = static_cast<int>(result.size()) - 3; i > 0; i -= 3) result.insert(static_cast<size_t>(i), L" ");
    return result;
}

bool SavePng(const std::wstring& file, int width, int height, void* pixels)
{
    UINT count{}, size{};
    if (Gdiplus::GetImageEncodersSize(&count, &size) != Gdiplus::Ok || !size) return false;
    std::vector<BYTE> storage(size);
    auto encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
    if (Gdiplus::GetImageEncoders(count, size, encoders) != Gdiplus::Ok) return false;
    for (UINT i = 0; i < count; ++i) {
        if (wcscmp(encoders[i].MimeType, L"image/png") == 0) {
            Gdiplus::Bitmap bitmap(width, height, width * 4, PixelFormat32bppRGB,
                static_cast<BYTE*>(pixels));
            return bitmap.Save(file.c_str(), &encoders[i].Clsid, nullptr) == Gdiplus::Ok;
        }
    }
    return false;
}
}
