#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef UNICODE
#define UNICODE
#endif
#include "WindowsTarget.h"
#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <uiautomation.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
#include "TaskbarWidget.h"
#include "TaskbarLayout.h"
#include "WidgetHitTest.h"

#ifdef _MSC_VER
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#endif

namespace {
HWND gWindow = nullptr, gTaskbar = nullptr, gHostWindow = nullptr;
HINSTANCE gInstance = nullptr;
UINT gMenuMessage = 0;
WidgetPlacement gPlacement = WidgetPlacement::Taskbar;
Metrics gValues{};
bool gHasSpace = true;
bool gReadLayout = false;
std::vector<RECT> gObstacles;
std::wstring gSettings;
HANDLE gLayoutStop = nullptr, gLayoutThread = nullptr;
SRWLOCK gLayoutLock = SRWLOCK_INIT;
std::array<RECT, 256> gSnapshotObstacles{};
int gSnapshotCount = 0;
bool gSnapshotValid = false;
HWND gSnapshotTaskbar = nullptr;
RECT gSnapshotBar{};
ULONGLONG gSnapshotTime = 0;
TaskbarSlot gLastGoodSlot{};
ULONGLONG gLastGoodTime = 0;
HWND gLastGoodTaskbar = nullptr;
HWND gDetails = nullptr;
ULONG_PTR gGraphicsToken = 0;
int gHoverSection = -1, gDetailSection = -1;
int gShapeWidth = 0, gShapeHeight = 0;
constexpr UINT_PTR kDismissTimer = 19;
constexpr float kPopupRadius = 12;
constexpr float kPopupMargin = 12;
constexpr float kCardRadius = 8;
constexpr float kDetailsBaseHeight = 276;
std::array<std::deque<double>, 4> gHistory;
ULONGLONG gHistoryTick = 0;

using Gdiplus::Color;
Color Accent(int) {
    return Color(255, 143, 181, 222);
}
double Scale() {
    const UINT dpi = gTaskbar ? GetDpiForWindow(gTaskbar) : 96;
    return (dpi ? dpi : 96) / 96.0;
}
bool LightTheme() {
    DWORD light = 0, bytes = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &bytes);
    return light != 0;
}
void RoundedPath(Gdiplus::GraphicsPath& path, float x, float y, float width, float height, float radius) {
    const float diameter = std::min(radius * 2, std::min(width, height));
    path.AddArc(x, y, diameter, diameter, 180, 90);
    path.AddArc(x + width - diameter, y, diameter, diameter, 270, 90);
    path.AddArc(x + width - diameter, y + height - diameter, diameter, diameter, 0, 90);
    path.AddArc(x, y + height - diameter, diameter, diameter, 90, 90);
    path.CloseFigure();
}
void FillRound(Gdiplus::Graphics& graphics, float x, float y, float width, float height, float radius, Color color) {
    Gdiplus::GraphicsPath path;
    RoundedPath(path, x, y, width, height, radius);
    Gdiplus::SolidBrush brush(color);
    graphics.FillPath(&brush, &path);
}
void Text(Gdiplus::Graphics& graphics, const std::wstring& text, float x, float y, float width, float height,
          float size, Color color, bool bold = false, bool right = false, bool tight = false, bool center = false) {
    const bool ascii = std::all_of(text.begin(), text.end(), [](wchar_t ch) { return ch < 128; });
    Gdiplus::Font font(ascii ? L"Segoe UI" : L"Microsoft YaHei UI", size, bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush brush(color);
    Gdiplus::StringFormat format(tight ? Gdiplus::StringFormat::GenericTypographic() : Gdiplus::StringFormat::GenericDefault());
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsNoClip);
    format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    if (center) format.SetAlignment(Gdiplus::StringAlignmentCenter);
    else if (right) format.SetAlignment(Gdiplus::StringAlignmentFar);
    graphics.DrawString(text.c_str(), -1, &font, Gdiplus::RectF(x, y, width, height), &format, &brush);
}
std::wstring Amount(double value) {
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%.1f GiB", value);
    return buffer;
}
std::wstring FullRate(double value) {
    wchar_t buffer[40]{};
    if (value >= 1024.0 * 1024.0 * 1024.0) swprintf_s(buffer, L"%.1f GiB/s", value / (1024.0 * 1024.0 * 1024.0));
    else if (value >= 1024.0 * 1024.0) swprintf_s(buffer, L"%.1f MiB/s", value / (1024.0 * 1024.0));
    else swprintf_s(buffer, L"%.0f KiB/s", value / 1024.0);
    return buffer;
}
void HideDetails() {
    if (gDetails) ShowWindow(gDetails, SW_HIDE);
    gDetailSection = -1;
}

std::wstring Percent(double value) {
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%.0f%%", value);
    return buffer;
}
std::wstring Temperature(double value) {
    if (value < 0.0) return L"—";
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%.0f°C", value);
    return buffer;
}
std::wstring Frequency(double mhz) {
    if (mhz <= 0) return L"—";
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%.2f GHz", mhz / 1000.0);
    return buffer;
}
std::wstring DiskRate(double value) {
    wchar_t buffer[32]{};
    if (value >= 1024.0 * 1024.0 * 1024.0) swprintf_s(buffer, L"%.1fG/s", value / (1024.0 * 1024.0 * 1024.0));
    else if (value >= 100.0 * 1024.0 * 1024.0) swprintf_s(buffer, L"%.0fM/s", value / (1024.0 * 1024.0));
    else if (value >= 1024.0 * 1024.0) swprintf_s(buffer, L"%.1fM/s", value / (1024.0 * 1024.0));
    else swprintf_s(buffer, L"%.0fK/s", value / 1024.0);
    return buffer;
}

bool RefreshObstacles(IUIAutomation* automation, HWND taskbar, std::vector<RECT>& obstacles) {
    bool success = false;
    IUIAutomationElement* root = nullptr;
    IUIAutomationCondition* condition = nullptr;
    IUIAutomationCacheRequest* cache = nullptr;
    IUIAutomationElementArray* elements = nullptr;
    VARIANT type;
    VariantInit(&type);
    type.vt = VT_I4;
    type.lVal = UIA_ButtonControlTypeId;
    if (SUCCEEDED(automation->ElementFromHandle(taskbar, &root)) &&
        SUCCEEDED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, type, &condition)) &&
        SUCCEEDED(automation->CreateCacheRequest(&cache)) &&
        SUCCEEDED(cache->AddProperty(UIA_BoundingRectanglePropertyId)) &&
        SUCCEEDED(root->FindAllBuildCache(TreeScope_Descendants, condition, cache, &elements))) {
        int length = 0;
        success = SUCCEEDED(elements->get_Length(&length));
        for (int i = 0; i < length; ++i) {
            IUIAutomationElement* element = nullptr;
            if (SUCCEEDED(elements->GetElement(i, &element))) {
                RECT rect{};
                if (SUCCEEDED(element->get_CachedBoundingRectangle(&rect)) &&
                    rect.right > rect.left && rect.bottom > rect.top) obstacles.push_back(rect);
                element->Release();
            }
        }
    }
    if (elements) elements->Release();
    if (cache) cache->Release();
    if (condition) condition->Release();
    if (root) root->Release();
    return success;
}

DWORD WINAPI LayoutThread(void*) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 0;
    IUIAutomation* automation = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IUIAutomation, reinterpret_cast<void**>(&automation)))) {
        do {
            const HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
            RECT bar{};
            std::vector<RECT> obstacles;
            const bool valid = taskbar && GetWindowRect(taskbar, &bar) && RefreshObstacles(automation, taskbar, obstacles);
            AcquireSRWLockExclusive(&gLayoutLock);
            gSnapshotCount = static_cast<int>(std::min(obstacles.size(), gSnapshotObstacles.size()));
            for (int i = 0; i < gSnapshotCount; ++i) gSnapshotObstacles[i] = obstacles[i];
            gSnapshotValid = valid && obstacles.size() <= gSnapshotObstacles.size();
            gSnapshotTaskbar = taskbar;
            gSnapshotBar = bar;
            gSnapshotTime = GetTickCount64();
            ReleaseSRWLockExclusive(&gLayoutLock);
        } while (WaitForSingleObject(gLayoutStop, 1200) == WAIT_TIMEOUT);
        automation->Release();
    }
    CoUninitialize();
    return 0;
}

#include "WidgetRendering.inl"

LRESULT CALLBACK WidgetProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_PAINT: Paint(window); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_MOUSEMOVE: {
        KillTimer(window, kDismissTimer);
        RECT area{};
        GetClientRect(window, &area);
        const int section = WidgetSectionAt(static_cast<short>(LOWORD(lp)), area.right);
        if (section != gHoverSection) {
            gHoverSection = section;
            InvalidateRect(window, nullptr, FALSE);
            if (gDetails && IsWindowVisible(gDetails)) ShowDetails(section);
        }
        BeginTracking(window);
        return 0;
    }
    case WM_MOUSEHOVER: ShowDetails(gHoverSection); return 0;
    case WM_MOUSELEAVE:
        gHoverSection = -1;
        InvalidateRect(window, nullptr, FALSE);
        SetTimer(window, kDismissTimer, 160, nullptr);
        return 0;
    case WM_TIMER:
        if (wp == kDismissTimer) {
            KillTimer(window, kDismissTimer);
            POINT cursor{};
            RECT strip{}, details{};
            GetCursorPos(&cursor);
            GetWindowRect(window, &strip);
            if (gDetails) GetWindowRect(gDetails, &details);
            if (!PtInRect(&strip, cursor) && (!gDetails || !IsWindowVisible(gDetails) || !PtInRect(&details, cursor))) HideDetails();
        }
        return 0;
    case WM_SHOWWINDOW:
        if (!wp) HideDetails();
        break;
    case WM_LBUTTONUP:
        return 0;
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
        HideDetails();
        PostMessageW(gHostWindow, gMenuMessage, 0, WM_CONTEXTMENU);
        return 0;
    case WM_DESTROY:
        KillTimer(window, kDismissTimer);
        HideDetails();
        gWindow = nullptr;
        gHoverSection = -1;
        gShapeWidth = gShapeHeight = 0;
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}

void EnsureWindow(HWND taskbar) {
    if (gWindow && IsWindow(gWindow) && gTaskbar == taskbar) return;
    if (gWindow && IsWindow(gWindow)) DestroyWindow(gWindow);
    gTaskbar = taskbar;
    const bool embedded = gPlacement == WidgetPlacement::Taskbar;
    gWindow = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | (embedded ? 0 : WS_EX_TOPMOST),
                             L"MiniPulseWidgetClass", L"MiniPulse 指标栏",
                             embedded ? WS_CHILD : WS_POPUP, 0, 0, 288, 36,
                             embedded ? taskbar : nullptr, nullptr, gInstance, nullptr);
}

void ShapeStrip(int width, int height, double scale) {
    if (gShapeWidth == width && gShapeHeight == height) return;
    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, static_cast<int>(16 * scale), static_cast<int>(16 * scale));
    if (region && !SetWindowRgn(gWindow, region, TRUE)) DeleteObject(region);
    gShapeWidth = width;
    gShapeHeight = height;
}

void Position() {
    const HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!taskbar || gPlacement == WidgetPlacement::Hidden) {
        if (gWindow && IsWindow(gWindow)) ShowWindow(gWindow, SW_HIDE);
        return;
    }
    EnsureWindow(taskbar);
    if (!gWindow) return;
    RECT bar{}, tray{};
    GetWindowRect(taskbar, &bar);
    const UINT dpi = GetDpiForWindow(taskbar);
    const double scale = (dpi ? dpi : 96) / 96.0;
    const int padding = std::max(2, static_cast<int>(4 * scale));
    const int preferredWidth = static_cast<int>(360 * scale);
    const int minimumWidth = static_cast<int>(320 * scale);
    const int height = std::min(static_cast<int>(36 * scale), static_cast<int>(bar.bottom - bar.top) - 2 * padding);
    if (height < 20 || bar.right - bar.left < preferredWidth) {
        gHasSpace = false;
        ShowWindow(gWindow, SW_HIDE);
        return;
    }
    const HWND trayWindow = FindWindowExW(taskbar, nullptr, L"TrayNotifyWnd", nullptr);
    const bool trayKnown = trayWindow && GetWindowRect(trayWindow, &tray);
    // No guessed notification area: hide embedded placement if Explorer's layout
    // cannot be identified, while the tray menu still offers a floating placement.
    if (!trayKnown && gPlacement == WidgetPlacement::Taskbar) {
        gHasSpace = false;
        ShowWindow(gWindow, SW_HIDE);
        return;
    }
    if (gPlacement == WidgetPlacement::AboveTaskbar) {
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        GetMonitorInfoW(MonitorFromWindow(taskbar, MONITOR_DEFAULTTONEAREST), &monitor);
        const int right = trayKnown ? tray.left : bar.right - padding;
        const int x = std::clamp(right - preferredWidth - padding, static_cast<int>(monitor.rcWork.left),
                                 std::max(static_cast<int>(monitor.rcWork.left), static_cast<int>(monitor.rcWork.right) - preferredWidth));
        int y = bar.top - height - padding;
        if (bar.top <= monitor.rcMonitor.top + padding) y = bar.bottom + padding;
        gHasSpace = true;
        SetWindowPos(gWindow, HWND_TOPMOST, x, y, preferredWidth, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ShapeStrip(preferredWidth, height, scale);
        return;
    }

    AcquireSRWLockShared(&gLayoutLock);
    gReadLayout = gSnapshotValid && gSnapshotTaskbar == taskbar &&
                  EqualRect(&gSnapshotBar, &bar) && GetTickCount64() - gSnapshotTime < 10000;
    gObstacles.assign(gSnapshotObstacles.begin(), gSnapshotObstacles.begin() + gSnapshotCount);
    ReleaseSRWLockShared(&gLayoutLock);
    if (!gReadLayout) {
        // Keep the last good slot briefly while the layout thread refreshes,
        // so transient Explorer/button changes do not flash the widget away.
        if (gLastGoodTaskbar == taskbar && gLastGoodTime &&
            GetTickCount64() - gLastGoodTime < 3000 && gLastGoodSlot.width > 0) {
            gHasSpace = true;
            POINT point{gLastGoodSlot.left, bar.top + (bar.bottom - bar.top - height) / 2};
            ScreenToClient(taskbar, &point);
            SetWindowPos(gWindow, HWND_TOP, point.x, point.y, gLastGoodSlot.width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
            ShapeStrip(gLastGoodSlot.width, height, scale);
            return;
        }
        gHasSpace = false;
        ShowWindow(gWindow, SW_HIDE);
        return;
    }
    const int leftLimit = static_cast<int>(bar.left) + padding;
    const int rightLimit = static_cast<int>(tray.left) - padding;
    std::vector<std::pair<int, int>> occupied;
    for (const RECT& item : gObstacles) {
        if (item.bottom <= bar.top || item.top >= bar.bottom || item.right <= leftLimit || item.left >= rightLimit) continue;
        occupied.emplace_back(std::max(leftLimit, static_cast<int>(item.left) - padding),
                              std::min(rightLimit, static_cast<int>(item.right) + padding));
    }
    const TaskbarSlot slot = FindTaskbarSlot(leftLimit, rightLimit, minimumWidth, preferredWidth, occupied);
    gHasSpace = slot.width > 0;
    if (!gHasSpace) { ShowWindow(gWindow, SW_HIDE); return; }
    gLastGoodSlot = slot;
    gLastGoodTaskbar = taskbar;
    gLastGoodTime = GetTickCount64();
    POINT point{slot.left, bar.top + (bar.bottom - bar.top - height) / 2};
    ScreenToClient(taskbar, &point);
    SetWindowPos(gWindow, HWND_TOP, point.x, point.y, slot.width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ShapeStrip(slot.width, height, scale);
}
} // namespace

void StartTaskbarWidget(HINSTANCE instance, HWND host, UINT menuMessage) {
    gInstance = instance;
    gHostWindow = host;
    gMenuMessage = menuMessage;
    Gdiplus::GdiplusStartupInput graphicsStartup;
    if (Gdiplus::GdiplusStartup(&gGraphicsToken, &graphicsStartup, nullptr) != Gdiplus::Ok) return;
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WidgetProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = L"MiniPulseWidgetClass";
    if (!RegisterClassExW(&windowClass)) return;
    WNDCLASSEXW detailsClass = windowClass;
    detailsClass.lpfnWndProc = DetailsProc;
    detailsClass.lpszClassName = L"MiniPulseHoverDetailsClass";
    if (!RegisterClassExW(&detailsClass)) return;
    gDetails = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_LAYERED, detailsClass.lpszClassName,
                              L"MiniPulse · 指标详情", WS_POPUP, 0, 0, 332, 288, host, nullptr, instance, nullptr);
    gLayoutStop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (gLayoutStop) {
        gLayoutThread = CreateThread(nullptr, 0, LayoutThread, nullptr, 0, nullptr);
        if (!gLayoutThread) { CloseHandle(gLayoutStop); gLayoutStop = nullptr; }
    }
    wchar_t appData[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", appData, 32768);
    if (length > 0 && length < 32768) {
        std::wstring directory = std::wstring(appData) + L"\\MiniPulse";
        CreateDirectoryW(directory.c_str(), nullptr);
        gSettings = directory + L"\\settings.ini";
        const int saved = GetPrivateProfileIntW(L"Widget", L"Placement", 0, gSettings.c_str());
        if (saved >= 0 && saved <= 2) gPlacement = static_cast<WidgetPlacement>(saved);
    }
    Position();
}

void UpdateTaskbarWidget(const Metrics& metrics) {
    gValues = metrics;
    const ULONGLONG now = GetTickCount64();
    if (!gHistoryTick || now - gHistoryTick >= 700) {
        const double points[] = {metrics.cpu, metrics.memory, metrics.diskReadBytesPerSec + metrics.diskWriteBytesPerSec,
                                 metrics.downBytesPerSec + metrics.upBytesPerSec};
        for (int i = 0; i < 4; ++i) {
            if (i == 2 && !metrics.diskAvailable) continue;
            gHistory[i].push_back(points[i]);
            if (gHistory[i].size() > 45) gHistory[i].pop_front();
        }
        gHistoryTick = now;
    }
    Position();
    if (gWindow && IsWindowVisible(gWindow)) InvalidateRect(gWindow, nullptr, FALSE);
    if (gDetails && IsWindowVisible(gDetails)) ShowDetails(gDetailSection);
}

void SetWidgetPlacement(WidgetPlacement placement) {
    if (placement != gPlacement && gWindow && IsWindow(gWindow)) DestroyWindow(gWindow);
    gPlacement = placement;
    if (!gSettings.empty()) {
        const std::wstring value = std::to_wstring(static_cast<int>(placement));
        WritePrivateProfileStringW(L"Widget", L"Placement", value.c_str(), gSettings.c_str());
    }
    Position();
}
WidgetPlacement GetWidgetPlacement() { return gPlacement; }
bool WidgetHasSpace() { return gHasSpace; }
void PreviewWidgetDetails(int section) {
    if (!gWindow || section < 0 || section >= 4) return;
    RECT area{};
    GetClientRect(gWindow, &area);
    const int x = area.right * (2 * section + 1) / 8;
    SendMessageW(gWindow, WM_MOUSEMOVE, 0, MAKELPARAM(x, area.bottom / 2));
    SendMessageW(gWindow, WM_MOUSEHOVER, 0, MAKELPARAM(x, area.bottom / 2));
}
bool ExportWidgetPreview(const wchar_t* path) {
    if (!gGraphicsToken || gDetailSection < 0) return false;
    const float extra = DetailExtraHeight(gDetailSection);
    Gdiplus::Bitmap bitmap(600, static_cast<INT>(428 + extra), PixelFormat32bppARGB);
    {
        Gdiplus::Graphics graphics(&bitmap);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        graphics.Clear(Color(255, 229, 237, 248));
        graphics.TranslateTransform(134 - kPopupMargin, 20 - kPopupMargin);
        DrawDetailsSurface(graphics);
        graphics.ResetTransform();
        Text(graphics, L"悬停详情 · 实时数据", 80, 308 + extra, 440, 20, 12, Color(255, 55, 73, 97));
        graphics.TranslateTransform(120, 342 + extra);
        Gdiplus::GraphicsPath stripClip;
        RoundedPath(stripClip, 0, 0, 360, 36, 6);
        graphics.SetClip(&stripClip);
        DrawStrip(graphics, 360, 36);
    }
    UINT count = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&count, &size);
    if (!size) return false;
    std::vector<BYTE> storage(size);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
    if (Gdiplus::GetImageEncoders(count, size, encoders) != Gdiplus::Ok) return false;
    for (UINT i = 0; i < count; ++i) {
        if (encoders[i].MimeType && wcscmp(encoders[i].MimeType, L"image/png") == 0) {
            return bitmap.Save(path, &encoders[i].Clsid, nullptr) == Gdiplus::Ok;
        }
    }
    return false;
}
static bool CaptureMonitorWindow(HWND window, const wchar_t* path) {
    if (!window || !IsWindowVisible(window)) return false;
    UpdateWindow(window);
    DwmFlush(); // Capture the freshly painted sample, not the previous composition.
    RECT bounds{};
    GetWindowRect(window, &bounds);
    const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0) return false;
    Gdiplus::Bitmap bitmap(width, height, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics graphics(&bitmap);
        HDC target = graphics.GetHDC(), desktop = GetDC(nullptr);
        BitBlt(target, 0, 0, width, height, desktop, bounds.left, bounds.top, SRCCOPY | CAPTUREBLT);
        ReleaseDC(nullptr, desktop);
        graphics.ReleaseHDC(target);
    }
    UINT count = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&count, &size);
    if (!size) return false;
    std::vector<BYTE> storage(size);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
    Gdiplus::GetImageEncoders(count, size, encoders);
    for (UINT i = 0; i < count; ++i) if (wcscmp(encoders[i].MimeType, L"image/png") == 0) {
        return bitmap.Save(path, &encoders[i].Clsid, nullptr) == Gdiplus::Ok;
    }
    return false;
}
bool CaptureWidgetOnScreen(const wchar_t* path) { return CaptureMonitorWindow(gWindow, path); }
bool CaptureDetailsOnScreen(const wchar_t* path) { return CaptureMonitorWindow(gDetails, path); }

WidgetDiagnostics GetWidgetDiagnostics() {
    WidgetDiagnostics result;
    result.exists = gWindow && IsWindow(gWindow);
    result.visible = result.exists && IsWindowVisible(gWindow);
    result.embedded = result.exists && GetParent(gWindow) == gTaskbar;
    result.layoutAvailable = gReadLayout;
    result.hasSpace = gHasSpace;
    result.obstacleCount = static_cast<int>(gObstacles.size());
    result.detailsVisible = gDetails && IsWindowVisible(gDetails);
    result.detailSection = gDetailSection;
    if (result.exists) GetWindowRect(gWindow, &result.bounds);
    if (result.visible) {
        result.hitRegionsReady = true;
        for (int i = 0; i < 4; ++i) {
            POINT point{result.bounds.left + (result.bounds.right - result.bounds.left) * (2 * i + 1) / 8,
                        result.bounds.top + (result.bounds.bottom - result.bounds.top) / 2};
            if (WindowFromPoint(point) != gWindow) result.hitRegionsReady = false;
        }
    }
    if (gTaskbar && IsWindow(gTaskbar)) {
        result.dpi = GetDpiForWindow(gTaskbar);
        const HWND tray = FindWindowExW(gTaskbar, nullptr, L"TrayNotifyWnd", nullptr);
        if (tray) GetWindowRect(tray, &result.trayBounds);
    }
    return result;
}
void StopTaskbarWidget() {
    if (gWindow && IsWindow(gWindow)) DestroyWindow(gWindow);
    if (gDetails) DestroyWindow(gDetails);
    if (gLayoutStop) SetEvent(gLayoutStop);
    if (gLayoutThread) {
        const DWORD result = WaitForSingleObject(gLayoutThread, 1500);
        CloseHandle(gLayoutThread);
        gLayoutThread = nullptr;
        if (result == WAIT_OBJECT_0) { CloseHandle(gLayoutStop); gLayoutStop = nullptr; }
    }
    if (gGraphicsToken) { Gdiplus::GdiplusShutdown(gGraphicsToken); gGraphicsToken = 0; }
}
