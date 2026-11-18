#include "Engine.h"
#include "UiTheme.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cwchar>
#include <stdexcept>
#include <thread>

namespace {
namespace Ui = ForestUi;
constexpr UINT DoneMessage = WM_APP + 1;
constexpr int IdCpp = 101, IdAsm = 102, IdCompare = 103, IdReset = 104, IdCancel = 105;
constexpr int FirstEdit = 200, SidebarWidth = 306, SidebarContent = 650;
constexpr int ButtonIds[]{IdCpp, IdAsm, IdCompare, IdReset, IdCancel};
const wchar_t* ButtonNames[]{L"Generuj C++", L"Generuj MASM", L"Porównaj metody", L"Domyślne", L"Przerwij"};

struct AppState {
    Ui::Resources theme;
    double scale;
    HWND window{}, sidebar{}, edits[8]{}, buttons[5]{}, hotButton{};
    RECT fields[8]{}, buttonRects[5]{}, canvas{}, imagePanel{}, metrics[3]{};
    std::wstring status{L"Gotowe do obliczeń."};
    std::wstring previewFile;
    std::thread worker;
    volatile LONG cancel{};
    bool busy{}, closing{}, smoke{}, smokeFailed{}, layingOut{};
    int smokeStep{}, scrollY{};
    std::unique_ptr<Result> image;
    explicit AppState(int dpi) : theme(dpi), scale(static_cast<double>(dpi) / 96.0) {}
};

int S(const AppState& s, int value) { return static_cast<int>(value * s.scale + 0.5); }

RECT R(const AppState& s, int x, int y, int w, int h)
{
    return {S(s, x), S(s, y), S(s, x + w), S(s, y + h)};
}

void LayoutSidebar(AppState& s);
void Layout(AppState& s);
void Start(AppState& s, RenderMode mode);
void Finish(AppState& s, std::unique_ptr<Result> result);
LRESULT CALLBACK SidebarProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ControlProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

std::wstring Text(HWND control)
{
    wchar_t buffer[128]{};
    GetWindowTextW(control, buffer, 128);
    std::wstring value(buffer);
    const auto begin = value.find_first_not_of(L" \t\r\n");
    if (begin == std::wstring::npos) return {};
    const auto end = value.find_last_not_of(L" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

void Status(AppState& s, const std::wstring& value)
{
    s.status = value;
    InvalidateRect(s.window, nullptr, FALSE);
}

void Place(HWND child, const RECT& rect)
{
    MoveWindow(child, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, TRUE);
}

HWND Child(AppState& s, const wchar_t* type, const wchar_t* value, DWORD style, int id)
{
    HWND child = CreateWindowExW(0, type, value, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
        0, 0, 1, 1, s.sidebar, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    if (!child) throw std::runtime_error("Nie można utworzyć kontrolki.");
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(s.theme.control), FALSE);
    if (!SetWindowSubclass(child, ControlProc, 1, reinterpret_cast<DWORD_PTR>(&s)))
        throw std::runtime_error("Nie można przygotować obsługi kontrolki.");
    return child;
}

void LayoutSidebar(AppState& s)
{
    if (!s.sidebar || s.layingOut) return;
    s.layingOut = true;
    RECT client{};
    GetClientRect(s.sidebar, &client);
    const int page = std::max(1, static_cast<int>((client.bottom - client.top) / s.scale));
    s.scrollY = std::clamp(s.scrollY, 0, std::max(0, SidebarContent - page));
    SCROLLINFO info{sizeof(SCROLLINFO)};
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0; info.nMax = SidebarContent - 1;
    info.nPage = static_cast<UINT>(page); info.nPos = s.scrollY;
    SetScrollInfo(s.sidebar, SB_VERT, &info, TRUE);
    GetClientRect(s.sidebar, &client);
    const int width = static_cast<int>(client.right / s.scale);
    const int half = (width - 52) / 2, right = 32 + half, full = width - 40;
    const int yy = s.scrollY;
    s.fields[0] = R(s, 20, 80 - yy, half, 34);
    s.fields[1] = R(s, right, 80 - yy, half, 34);
    s.fields[2] = R(s, 20, 152 - yy, full, 34);
    s.fields[3] = R(s, 20, 270 - yy, half, 34);
    s.fields[4] = R(s, right, 270 - yy, half, 34);
    s.fields[5] = R(s, 20, 342 - yy, half, 34);
    s.fields[6] = R(s, right, 342 - yy, half, 34);
    s.fields[7] = R(s, right, 432 - yy, half, 34);
    for (int i = 0; i < 8; ++i) {
        RECT edit = s.fields[i];
        edit.left += S(s, 10); edit.right -= S(s, 8);
        edit.top += S(s, 7); edit.bottom -= S(s, 5);
        if (s.edits[i]) Place(s.edits[i], edit);
    }
    s.buttonRects[0] = R(s, 20, 490 - yy, half, 38);
    s.buttonRects[1] = R(s, right, 490 - yy, half, 38);
    s.buttonRects[2] = R(s, 20, 540 - yy, full, 42);
    s.buttonRects[3] = R(s, 20, 594 - yy, half, 32);
    s.buttonRects[4] = R(s, right, 594 - yy, half, 32);
    for (int i = 0; i < 5; ++i) if (s.buttons[i]) Place(s.buttons[i], s.buttonRects[i]);
    s.layingOut = false;
    InvalidateRect(s.sidebar, nullptr, FALSE);
}

void Layout(AppState& s)
{
    RECT client{};
    GetClientRect(s.window, &client);
    const int width = static_cast<int>(client.right / s.scale);
    const int height = static_cast<int>(client.bottom / s.scale);
    if (width < 1 || height < 1) return;
    Place(s.sidebar, R(s, 24, 110, SidebarWidth, std::max(120, height - 146)));
    const int left = 354, available = std::max(360, width - left - 24);
    const int card = (available - 24) / 3;
    for (int i = 0; i < 3; ++i) s.metrics[i] = R(s, left + i * (card + 12), 110, card, 104);
    s.imagePanel = R(s, left, 270, available, std::max(150, height - 308));
    s.canvas = s.imagePanel;
    s.canvas.left += S(s, 16); s.canvas.right -= S(s, 16);
    s.canvas.top += S(s, 54); s.canvas.bottom -= S(s, 16);
    LayoutSidebar(s);
    InvalidateRect(s.window, nullptr, FALSE);
}

void Defaults(AppState& s)
{
    const wchar_t* values[]{L"800", L"600", L"500", L"-2.5", L"1.0", L"-1.25", L"1.25", L"5"};
    for (int i = 0; i < 8; ++i) SetWindowTextW(s.edits[i], values[i]);
}

bool Unsigned(HWND control, uint32_t& value)
{
    const std::wstring text = Text(control);
    if (text.empty() || text.front() == L'-') return false;
    wchar_t* end{};
    errno = 0;
    const unsigned long parsed = wcstoul(text.c_str(), &end, 10);
    if (errno == ERANGE || end == text.c_str() || *end || parsed > UINT32_MAX) return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

bool Real(HWND control, double& value)
{
    std::wstring text = Text(control);
    if (text.empty()) return false;
    std::replace(text.begin(), text.end(), L',', L'.');
    wchar_t* end{};
    errno = 0;
    value = wcstod(text.c_str(), &end);
    return errno != ERANGE && end != text.c_str() && !*end && std::isfinite(value);
}

bool ReadRequest(AppState& s, Request& request, std::wstring& error)
{
    auto& p = request.params;
    uint32_t repetitions{};
    if (!Unsigned(s.edits[0], p.width) || !Unsigned(s.edits[1], p.height) ||
        !Unsigned(s.edits[2], p.maxIterations) || !Unsigned(s.edits[7], repetitions)) {
        error = L"Wymiary, limit iteracji i liczba prób muszą być dodatnimi liczbami całkowitymi.";
        return false;
    }
    if (!Real(s.edits[3], p.minRe) || !Real(s.edits[4], p.maxRe) ||
        !Real(s.edits[5], p.minIm) || !Real(s.edits[6], p.maxIm)) {
        error = L"Podaj poprawne granice obszaru. Możesz użyć kropki albo przecinka dziesiętnego.";
        return false;
    }
    uint32_t dummy{};
    if (Validate(&p, &dummy, UINT64_MAX) != MB_STATUS_OK || repetitions < 1 || repetitions > 100) {
        error = L"Wymiary: 1-8192, łącznie do 16 777 216 pikseli.\n"
                L"Iteracje: 1-100 000. Powtórzenia: 1-100.\n"
                L"Na każdej osi minimum musi być mniejsze od maksimum.\n"
                L"Granice obszaru: od -1 000 000 do 1 000 000.";
        return false;
    }
    request.repetitions = static_cast<int>(repetitions);
    return true;
}

void Busy(AppState& s, bool busy)
{
    s.busy = busy;
    for (HWND edit : s.edits) EnableWindow(edit, !busy);
    for (int i = 0; i < 4; ++i) EnableWindow(s.buttons[i], !busy);
    EnableWindow(s.buttons[4], busy && !s.closing);
    InvalidateRect(s.sidebar, nullptr, FALSE);
    for (HWND button : s.buttons) InvalidateRect(button, nullptr, FALSE);
}

void Start(AppState& s, RenderMode mode)
{
    if (s.busy) return;
    Request request;
    request.mode = mode;
    std::wstring error;
    if (!ReadRequest(s, request, error)) {
        Status(s, L"Popraw parametry obrazu.");
        if (!s.smoke) MessageBoxW(s.window, error.c_str(), L"Sprawdź parametry", MB_OK | MB_ICONWARNING);
        return;
    }
    InterlockedExchange(&s.cancel, 0);
    Busy(s, true);
    Status(s, mode == RenderMode::Compare ? L"Porównywanie metod..." : L"Generowanie obrazu...");
    try {
        s.worker = std::thread([&s, request] {
            std::unique_ptr<Result> result;
            try { result = Compute(request, &s.cancel); }
            catch (const std::exception&) {
                result = std::make_unique<Result>();
                result->request = request;
                result->status = MB_INVALID_ARGUMENT;
                result->error = L"Nie udało się wykonać obliczeń. Sprawdź parametry i dostępną pamięć.";
            }
            Result* transferred = result.release();
            if (!PostMessageW(s.window, DoneMessage, 0, reinterpret_cast<LPARAM>(transferred)))
                delete transferred;
        });
    } catch (const std::exception&) {
        Busy(s, false);
        Status(s, L"Nie można uruchomić obliczeń.");
        if (s.smoke || !s.previewFile.empty()) {
            s.smokeFailed = true;
            PostMessageW(s.window, WM_CLOSE, 0, 0);
        }
    }
}

void DrawSidebar(AppState& s, HDC dc)
{
    RECT client{};
    GetClientRect(s.sidebar, &client);
    Ui::Fill(dc, client, Ui::Background);
    Ui::Round(dc, client, S(s, 14), Ui::Panel, Ui::Border);
    const int width = static_cast<int>(client.right / s.scale);
    const int half = (width - 52) / 2, right = 32 + half;
    const int yy = s.scrollY;
    const auto label = [&](const wchar_t* value, int x, int y, int w, bool strong = false) {
        Ui::Label(dc, value, R(s, x, y - yy, w, 24),
            strong ? s.theme.label : s.theme.body, strong ? Ui::Ink : Ui::Muted);
    };
    label(L"Obraz", 20, 20, width - 40, true);
    label(L"Szerokość", 20, 53, half);
    label(L"Wysokość", right, 53, half);
    label(L"Limit iteracji", 20, 125, width - 40);
    label(L"Obszar", 20, 210, width - 40, true);
    label(L"Oś rzeczywista: od / do", 20, 242, width - 40);
    label(L"Oś urojona: od / do", 20, 314, width - 40);
    label(L"Pomiar", 20, 400, width - 40, true);
    label(L"Powtórzenia", 20, 436, half);
    for (int i = 0; i < 8; ++i) {
        const bool focus = GetFocus() == s.edits[i];
        Ui::Round(dc, s.fields[i], S(s, 7), RGB(255, 255, 255),
            focus ? Ui::Primary : Ui::Border);
    }
}

void DrawMetric(AppState& s, HDC dc, int index, const wchar_t* title,
                const std::wstring& value, const std::wstring& caption, bool hasValue)
{
    RECT card = s.metrics[index];
    Ui::Round(dc, card, S(s, 13), Ui::Panel, Ui::Border);
    RECT label = card;
    label.left += S(s, 16); label.right -= S(s, 16);
    label.top += S(s, 11); label.bottom = label.top + S(s, 22);
    Ui::Label(dc, title, label, s.theme.label, Ui::Muted);
    RECT number = label;
    number.top = card.top + S(s, 32); number.bottom = card.top + S(s, 75);
    const bool compact = value.size() > 12 || card.right - card.left < S(s, 210);
    Ui::Label(dc, value, number,
        hasValue ? compact ? s.theme.metricSmall : s.theme.metric : s.theme.label,
        hasValue ? Ui::Ink : Ui::Muted);
    RECT note = label;
    note.top = card.top + S(s, 77); note.bottom = card.bottom - S(s, 8);
    Ui::Label(dc, caption, note, s.theme.captionFont, Ui::Muted);
}

void DrawScene(AppState& s, HDC dc)
{
    RECT client{};
    GetClientRect(s.window, &client);
    const int width = static_cast<int>(client.right / s.scale);
    const int height = static_cast<int>(client.bottom / s.scale);
    Ui::Fill(dc, client, Ui::Background);
    Ui::Fill(dc, R(s, 0, 0, width, 88), Ui::Header);
    Ui::Label(dc, L"Mandelbrot", R(s, 27, 14, width - 54, 61), s.theme.title,
        RGB(243, 248, 234));
    const Result* result = s.image.get();
    const bool compare = result && result->request.mode == RenderMode::Compare;
    const bool cpp = result && result->cppMs >= 0;
    const bool asmReady = result && result->asmMs >= 0;
    const std::wstring method = compare ?
        L"Mediana · " + std::to_wstring(result->request.repetitions) + L" prób" : L"Jedno generowanie";
    DrawMetric(s, dc, 0, L"C++", cpp ? Ui::Number(result->cppMs) + L" ms" : L"Brak pomiaru",
        cpp ? method : L"Wybierz Generuj C++", cpp);
    DrawMetric(s, dc, 1, L"MASM", asmReady ? Ui::Number(result->asmMs) + L" ms" : L"Brak pomiaru",
        asmReady ? method : L"Wybierz Generuj MASM", asmReady);
    std::wstring ratio = L"Brak porównania", verdict = L"Wybierz Porównaj metody";
    bool validRatio = compare && result->mismatches == 0 && result->cppMs > 0.0 && result->asmMs > 0.0;
    if (validRatio) {
        const double value = result->cppMs / result->asmMs;
        ratio = Ui::Number(value) + L"×";
        verdict = value > 1.0 ? L"MASM: krótszy czas" :
                  value < 1.0 ? L"C++: krótszy czas" : L"Równe czasy";
    }
    DrawMetric(s, dc, 2, L"Czas C++ / czas MASM", ratio, verdict, validRatio);
    const int left = 354, available = std::max(360, width - left - 24);
    if (compare) {
        const bool same = result->mismatches == 0;
        const std::wstring agreement = same ? L"Wyniki zgodne" :
            L"Rozbieżności: " + Ui::Integer(result->mismatches);
        const int pillWidth = same ? 153 : std::min(available, 290);
        RECT pill = R(s, left, 229, pillWidth, 29);
        Ui::Round(dc, pill, S(s, 14), same ? RGB(216, 232, 204) : RGB(247, 228, 213),
            same ? RGB(190, 213, 176) : RGB(226, 191, 169));
        Ui::Label(dc, agreement, pill, s.theme.label, same ? Ui::Primary : Ui::Error,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        Ui::Label(dc, same ? L"Porównano wszystkie piksele." : L"Sprawdź różnice przed oceną czasów.",
            R(s, left + pillWidth + 14, 230, std::max(20, available - pillWidth - 14), 26),
            s.theme.captionFont, Ui::Muted);
    } else {
        Ui::Label(dc, L"Porównaj metody, aby sprawdzić czas i zgodność.",
            R(s, left, 229, available, 29), s.theme.body, Ui::Muted);
    }

    Ui::Round(dc, s.imagePanel, S(s, 14), Ui::Panel, Ui::Border);
    Ui::Label(dc, L"Podgląd", R(s, left + 18, 282, 140, 27), s.theme.label, Ui::Ink);
    if (result) {
        const auto& p = result->request.params;
        const std::wstring mode = result->request.mode == RenderMode::Cpp ? L"C++" :
            result->request.mode == RenderMode::Asm ? L"MASM" : L"Porównanie";
        const std::wstring details = mode + L" · " + Ui::Integer(p.width) + L" × " +
            Ui::Integer(p.height) + L" px · " + Ui::Integer(p.maxIterations) + L" iteracji";
        Ui::Label(dc, details, R(s, left + 160, 282, std::max(80, available - 178), 27),
            s.theme.captionFont, Ui::Muted,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    Ui::Round(dc, s.canvas, S(s, 10), Ui::Canvas, Ui::Canvas);
    if (result && !result->pixels.empty()) {
        const auto& p = result->request.params;
        const int availableWidth = s.canvas.right - s.canvas.left;
        const int availableHeight = s.canvas.bottom - s.canvas.top;
        const double factor = std::min(static_cast<double>(availableWidth) / p.width,
                                      static_cast<double>(availableHeight) / p.height);
        const int imageWidth = std::max(1, static_cast<int>(p.width * factor));
        const int imageHeight = std::max(1, static_cast<int>(p.height * factor));
        const int x = s.canvas.left + (availableWidth - imageWidth) / 2;
        const int y = s.canvas.top + (availableHeight - imageHeight) / 2;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = static_cast<LONG>(p.width);
        info.bmiHeader.biHeight = -static_cast<LONG>(p.height);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        const int saved = SaveDC(dc);
        HRGN clip = CreateRoundRectRgn(s.canvas.left, s.canvas.top, s.canvas.right,
            s.canvas.bottom, S(s, 20), S(s, 20));
        ExtSelectClipRgn(dc, clip, RGN_AND);
        DeleteObject(clip);
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, nullptr);
        StretchDIBits(dc, x, y, imageWidth, imageHeight, 0, 0, static_cast<int>(p.width),
            static_cast<int>(p.height), result->pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
        RestoreDC(dc, saved);
    } else {
        RECT hint = s.canvas;
        InflateRect(&hint, -S(s, 24), -S(s, 24));
        Ui::Label(dc, s.busy ? L"Generowanie obrazu..." : L"Wybierz Generuj albo Porównaj metody.",
            hint, s.theme.label, RGB(199, 219, 192), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    Ui::Label(dc, s.status, R(s, 28, height - 28, width - 56, 22),
        s.theme.captionFont, Ui::Muted);
}

void Paint(AppState& s, HWND window, bool sidebar)
{
    PAINTSTRUCT ps{};
    HDC destination = BeginPaint(window, &ps);
    RECT client{};
    GetClientRect(window, &client);
    if (client.right > 0 && client.bottom > 0) {
        HDC buffer = CreateCompatibleDC(destination);
        HBITMAP bitmap = CreateCompatibleBitmap(destination, client.right, client.bottom);
        HGDIOBJ old = SelectObject(buffer, bitmap);
        if (sidebar) DrawSidebar(s, buffer);
        else DrawScene(s, buffer);
        BitBlt(destination, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY);
        SelectObject(buffer, old); DeleteObject(bitmap); DeleteDC(buffer);
    }
    EndPaint(window, &ps);
}

// Podgląd jest renderowany z własnego modelu do pamięci. Nie odczytuje pulpitu ani ekranu.
bool SavePreview(AppState& s)
{
    RECT client{};
    GetClientRect(s.window, &client);
    const int width = client.right, height = client.bottom;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void* pixels{};
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!dc || !bitmap || !pixels) {
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        return false;
    }
    HGDIOBJ old = SelectObject(dc, bitmap);
    DrawScene(s, dc);
    POINT origin{};
    MapWindowPoints(s.sidebar, s.window, &origin, 1);
    const int saved = SaveDC(dc);
    SetViewportOrgEx(dc, origin.x, origin.y, nullptr);
    RECT pane{};
    GetClientRect(s.sidebar, &pane);
    IntersectClipRect(dc, 0, 0, pane.right, pane.bottom);
    DrawSidebar(s, dc);
    for (int i = 0; i < 8; ++i) {
        RECT field = s.fields[i]; field.left += S(s, 10); field.right -= S(s, 8);
        Ui::Label(dc, Text(s.edits[i]), field, s.theme.control, Ui::Ink);
    }
    for (int i = 0; i < 5; ++i) {
        Ui::Button(dc, s.buttonRects[i], ButtonNames[i], s.theme.control, S(s, 8),
            i == 2, i == 4, IsWindowEnabled(s.buttons[i]) != FALSE, false, false, false);
    }
    RestoreDC(dc, saved);
    const bool ok = Ui::SavePng(s.previewFile, width, height, pixels);
    SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc);
    return ok;
}

void SmokeFailure(AppState& s, const char* message)
{
    s.smokeFailed = true;
    Log(std::string("FAIL GUI: ") + message);
    PostMessageW(s.window, WM_CLOSE, 0, 0);
}

void SmokeContinue(AppState& s)
{
    if (s.image->mismatches || s.image->pixels.empty() || !IsWindowEnabled(s.buttons[2])) {
        SmokeFailure(s, "Nieprawidłowy wynik albo stan kontrolek.");
        return;
    }
    Log("OK GUI: przycisk " + std::to_string(s.smokeStep + 1) + ", wynik i odblokowanie kontrolek.");
    ++s.smokeStep;
    if (s.smokeStep == 1) { PostMessageW(s.window, WM_COMMAND, IdAsm, 0); return; }
    if (s.smokeStep == 2) { PostMessageW(s.window, WM_COMMAND, IdCompare, 0); return; }
    SendMessageW(s.window, WM_COMMAND, IdReset, 0);
    Request request;
    std::wstring error;
    if (!ReadRequest(s, request, error) || request.params.width != 800 ||
        request.params.maxIterations != 500 || s.image) {
        SmokeFailure(s, "Przywracanie parametrów.");
        return;
    }
    SetWindowTextW(s.edits[0], L"0");
    if (ReadRequest(s, request, error)) { SmokeFailure(s, "Walidacja zerowego wymiaru."); return; }
    Defaults(s);
    SetWindowTextW(s.edits[3], L"-2,5");
    if (!ReadRequest(s, request, error) || request.params.minRe != -2.5) {
        SmokeFailure(s, "Przecinek dziesiętny."); return;
    }
    wchar_t title[64]{};
    GetWindowTextW(s.window, title, 64);
    if (wcscmp(title, L"Mandelbrot") != 0 ||
        (GetWindowLongPtrW(s.buttons[2], GWL_STYLE) & BS_TYPEMASK) != BS_OWNERDRAW) {
        SmokeFailure(s, "Nagłówek albo styl przycisku."); return;
    }
    SetWindowPos(s.window, nullptr, 0, 0, S(s, 1080), S(s, 700), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    SendMessageW(s.sidebar, WM_VSCROLL, SB_BOTTOM, 0);
    RECT pane{};
    GetClientRect(s.sidebar, &pane);
    if (s.buttonRects[4].bottom > pane.bottom || s.canvas.bottom <= s.canvas.top) {
        SmokeFailure(s, "Małe okno i przewijanie parametrów."); return;
    }
    SendMessageW(s.sidebar, WM_VSCROLL, SB_TOP, 0);
    Log("OK GUI: leśny motyw, przyciski, reset, walidacja, separator dziesiętny i przewijanie.");
    SetWindowTextW(s.edits[0], L"512");
    SetWindowTextW(s.edits[1], L"512");
    SetWindowTextW(s.edits[2], L"100000");
    PostMessageW(s.window, WM_COMMAND, IdCpp, 0);
    PostMessageW(s.window, WM_CLOSE, 0, 0);
}

void Finish(AppState& s, std::unique_ptr<Result> result)
{
    if (s.worker.joinable()) s.worker.join();
    Busy(s, false);
    if (s.closing) {
        if (s.smoke) {
            if (result->status != MB_CANCELLED && result->status != MB_STATUS_OK) s.smokeFailed = true;
            Log(s.smokeFailed ? "FAIL GUI: zamykanie." : "PASS GUI: anulowanie i bezpieczne zamknięcie.");
        }
        DestroyWindow(s.window);
        return;
    }
    if (result->status != MB_STATUS_OK) {
        Status(s, result->status == MB_CANCELLED ? L"Obliczenia przerwane." : L"Obliczenia nie powiodły się.");
        if (s.smoke || !s.previewFile.empty()) SmokeFailure(s, "Błąd wykonania obliczeń.");
        else if (result->status != MB_CANCELLED)
            MessageBoxW(s.window, result->error.empty() ? L"Sprawdź parametry i dostępną pamięć." : result->error.c_str(),
                L"Błąd obliczeń", MB_OK | MB_ICONERROR);
        return;
    }
    Status(s, result->mismatches ? L"Wyniki różnią się. Sprawdź rozbieżności." :
        result->request.mode == RenderMode::Compare ? L"Porównanie zakończone. Wyniki są zgodne." :
        L"Obraz wygenerowany.");
    if (result->mismatches && !s.smoke && s.previewFile.empty())
        MessageBoxW(s.window, result->differences.c_str(), L"Różnice wyników", MB_OK | MB_ICONWARNING);
    s.image = std::move(result);
    InvalidateRect(s.window, nullptr, FALSE);
    if (!s.previewFile.empty()) {
        if (s.image->mismatches || !SavePreview(s)) {
            s.smokeFailed = true; Log("FAIL: renderowanie podglądu interfejsu.");
        } else Log("OK: podgląd interfejsu wyrenderowany z rzeczywistymi wynikami porównania.");
        PostMessageW(s.window, WM_CLOSE, 0, 0);
    } else if (s.smoke) SmokeContinue(s);
}

LRESULT CALLBACK ControlProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                             UINT_PTR id, DWORD_PTR reference)
{
    auto& s = *reinterpret_cast<AppState*>(reference);
    const bool button = GetDlgCtrlID(window) >= IdCpp && GetDlgCtrlID(window) <= IdCancel;
    if (message == WM_MOUSEMOVE && button && IsWindowEnabled(window)) {
        if (s.hotButton != window) {
            HWND old = s.hotButton; s.hotButton = window;
            if (old) InvalidateRect(old, nullptr, FALSE);
            InvalidateRect(window, nullptr, FALSE);
            TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0};
            TrackMouseEvent(&track);
        }
    } else if (message == WM_MOUSELEAVE && s.hotButton == window) {
        s.hotButton = nullptr; InvalidateRect(window, nullptr, FALSE);
    } else if (message == WM_SETCURSOR && button && IsWindowEnabled(window)) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE;
    } else if (message == WM_SETFOCUS) {
        RECT rect{}, pane{};
        GetClientRect(window, &rect);
        MapWindowPoints(window, s.sidebar, reinterpret_cast<POINT*>(&rect), 2);
        GetClientRect(s.sidebar, &pane);
        if (rect.top < S(s, 12)) s.scrollY += static_cast<int>(rect.top / s.scale) - 12;
        else if (rect.bottom > pane.bottom - S(s, 12))
            s.scrollY += static_cast<int>((rect.bottom - pane.bottom) / s.scale) + 12;
        LayoutSidebar(s);
        InvalidateRect(s.sidebar, nullptr, FALSE);
    } else if (message == WM_KILLFOCUS) {
        InvalidateRect(s.sidebar, nullptr, FALSE);
    } else if (message == WM_MOUSEWHEEL) {
        SendMessageW(s.sidebar, message, wParam, lParam); return 0;
    } else if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, ControlProc, id);
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK SidebarProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto s = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        s = static_cast<AppState*>(create->lpCreateParams);
        s->sidebar = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
    }
    if (!s) return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
    case WM_COMMAND:
        return SendMessageW(s->window, message, wParam, lParam);
    case WM_SIZE:
        LayoutSidebar(*s); return 0;
    case WM_VSCROLL: {
        SCROLLINFO info{sizeof(SCROLLINFO)};
        info.fMask = SIF_ALL;
        GetScrollInfo(window, SB_VERT, &info);
        switch (LOWORD(wParam)) {
        case SB_TOP: s->scrollY = 0; break;
        case SB_BOTTOM: s->scrollY = SidebarContent; break;
        case SB_LINEUP: s->scrollY -= 24; break;
        case SB_LINEDOWN: s->scrollY += 24; break;
        case SB_PAGEUP: s->scrollY -= static_cast<int>(info.nPage); break;
        case SB_PAGEDOWN: s->scrollY += static_cast<int>(info.nPage); break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK: s->scrollY = info.nTrackPos; break;
        }
        LayoutSidebar(*s); return 0;
    }
    case WM_MOUSEWHEEL:
        s->scrollY -= GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * 48;
        LayoutSidebar(*s); return 0;
    case WM_DRAWITEM: {
        const auto draw = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        const int index = static_cast<int>(draw->CtlID) - IdCpp;
        if (index >= 0 && index < 5) {
            const bool focus = (draw->itemState & ODS_FOCUS) && !(draw->itemState & ODS_NOFOCUSRECT);
            Ui::Button(draw->hDC, draw->rcItem, ButtonNames[index], s->theme.control, S(*s, 8),
                index == 2, index == 4, !(draw->itemState & ODS_DISABLED),
                s->hotButton == draw->hwndItem, (draw->itemState & ODS_SELECTED) != 0, focus);
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, RGB(255, 255, 255));
        SetTextColor(dc, message == WM_CTLCOLOREDIT ? Ui::Ink : Ui::Muted);
        return reinterpret_cast<LRESULT>(s->theme.field);
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Paint(*s, window, true); return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto s = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        s = static_cast<AppState*>(create->lpCreateParams);
        s->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
    }
    if (!s) return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
    case WM_CREATE:
        try {
            s->sidebar = CreateWindowExW(WS_EX_CONTROLPARENT, L"MandelbrotForestSidebar", L"Parametry",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN, 0, 0, 1, 1,
                window, nullptr, GetModuleHandleW(nullptr), s);
            if (!s->sidebar) return -1;
            for (int i = 0; i < 8; ++i) {
                s->edits[i] = Child(*s, L"EDIT", L"", ES_AUTOHSCROLL, FirstEdit + i);
                SendMessageW(s->edits[i], EM_SETLIMITTEXT, 40, 0);
                SendMessageW(s->edits[i], EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
            }
            for (int i = 0; i < 5; ++i)
                s->buttons[i] = Child(*s, L"BUTTON", ButtonNames[i], BS_OWNERDRAW, ButtonIds[i]);
            Defaults(*s); Busy(*s, false); Layout(*s);
        } catch (const std::exception&) { return -1; }
        return 0;
    case WM_SIZE:
        if (s->sidebar) Layout(*s);
        return 0;
    case WM_GETMINMAXINFO: {
        const auto info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize = {S(*s, 1000), S(*s, 650)};
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Paint(*s, window, false); return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IdCpp: Start(*s, RenderMode::Cpp); break;
        case IdAsm: Start(*s, RenderMode::Asm); break;
        case IdCompare: Start(*s, RenderMode::Compare); break;
        case IdReset:
            if (!s->busy) {
                Defaults(*s);
                s->image.reset();
                Status(*s, L"Przywrócono domyślne parametry.");
            }
            break;
        case IdCancel:
            if (s->busy) {
                InterlockedExchange(&s->cancel, 1);
                EnableWindow(s->buttons[4], FALSE);
                Status(*s, L"Przerywanie obliczeń...");
            }
            break;
        }
        return 0;
    case DoneMessage:
        Finish(*s, std::unique_ptr<Result>(reinterpret_cast<Result*>(lParam))); return 0;
    case WM_CLOSE:
        if (s->busy) {
            s->closing = true;
            InterlockedExchange(&s->cancel, 1);
            EnableWindow(s->buttons[4], FALSE);
            Status(*s, L"Kończenie obliczeń...");
        } else DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(s->smokeFailed ? 1 : 0); return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int RunGuiInternal(HINSTANCE instance, int showCommand, bool smokeTest, const std::wstring& previewFile)
{
    SetProcessDPIAware();
    const int dpi = static_cast<int>(GetDpiForSystem());
    AppState s(dpi > 0 ? dpi : 96);
    s.smoke = smokeTest;
    s.previewFile = previewFile;
    INITCOMMONCONTROLSEX common{sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&common);
    WNDCLASSEXW pane{sizeof(WNDCLASSEXW)};
    pane.lpfnWndProc = SidebarProc; pane.hInstance = instance;
    pane.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    pane.lpszClassName = L"MandelbrotForestSidebar";
    if (!RegisterClassExW(&pane)) return 1;
    WNDCLASSEXW type{sizeof(WNDCLASSEXW)};
    type.lpfnWndProc = WindowProc; type.hInstance = instance;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.lpszClassName = L"MandelbrotForestWindow";
    if (!RegisterClassExW(&type)) {
        UnregisterClassW(pane.lpszClassName, instance); return 1;
    }
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int width = previewFile.empty() ? std::min(S(s, 1320), static_cast<int>(work.right - work.left) - S(s, 32)) : S(s, 1320);
    const int height = previewFile.empty() ? std::min(S(s, 900), static_cast<int>(work.bottom - work.top) - S(s, 32)) : S(s, 900);
    HWND window = CreateWindowExW(WS_EX_CONTROLPARENT, type.lpszClassName, L"Mandelbrot",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
        std::max(S(s, 1000), width), std::max(S(s, 650), height),
        nullptr, nullptr, instance, &s);
    int exitCode = 1;
    if (window) {
        const COLORREF caption = Ui::Header, text = RGB(244, 249, 236);
        DwmSetWindowAttribute(window, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
        DwmSetWindowAttribute(window, DWMWA_TEXT_COLOR, &text, sizeof(text));
        if (smokeTest) {
            SetWindowTextW(s.edits[0], L"64");
            SetWindowTextW(s.edits[1], L"48");
            SetWindowTextW(s.edits[2], L"80");
            SetWindowTextW(s.edits[7], L"2");
            Log("TEST GUI: kontrolki WinAPI, motyw leśny i wątek roboczy.");
            PostMessageW(window, WM_COMMAND, IdCpp, 0);
        } else if (!previewFile.empty()) {
            Log("Renderowanie własnego interfejsu do pliku PNG.");
            PostMessageW(window, WM_COMMAND, IdCompare, 0);
        } else {
            ShowWindow(window, showCommand);
            UpdateWindow(window);
            PostMessageW(window, WM_COMMAND, IdCpp, 0);
        }
        MSG message{};
        BOOL received{};
        while ((received = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            if (!IsDialogMessageW(window, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        exitCode = received < 0 ? 1 : static_cast<int>(message.wParam);
    }
    if (s.worker.joinable()) s.worker.join();
    UnregisterClassW(type.lpszClassName, instance);
    UnregisterClassW(pane.lpszClassName, instance);
    return exitCode;
}
}

int RunGui(HINSTANCE instance, int showCommand, bool smokeTest)
{
    try { return RunGuiInternal(instance, showCommand, smokeTest, L""); }
    catch (const std::exception&) {
        Log("Nie można przygotować interfejsu aplikacji.");
        return 1;
    }
}

int RenderGuiPreview(HINSTANCE instance, const std::wstring& file)
{
    try { return RunGuiInternal(instance, SW_HIDE, false, file); }
    catch (const std::exception&) {
        Log("Nie można wyrenderować podglądu interfejsu.");
        return 1;
    }
}
