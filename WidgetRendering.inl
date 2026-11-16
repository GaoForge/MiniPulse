// Included inside TaskbarWidget's private namespace; all coordinates are DIPs.
void DrawStrip(Gdiplus::Graphics& graphics, float width, float height) {
    const bool light = LightTheme();
    const Color text = light ? Color(255, 8, 13, 22) : Color(255, 250, 252, 255);
    const Color secondary = light ? Color(255, 20, 28, 40) : Color(255, 233, 240, 250);
    Gdiplus::SolidBrush hitArea(light ? Color(210, 255, 255, 255) : Color(210, 12, 17, 25));
    graphics.FillRectangle(&hitArea, 0.0f, 0.0f, width, height);
    const float cell = width / 4;
    for (int i = 0; i < 4; ++i) {
        const float x = i * cell;
        if (gHoverSection == i) FillRound(graphics, x + 1, 1, cell - 2, height - 2, 5,
            light ? Color(32, 119, 165, 225) : Color(28, 160, 192, 240));
        if (i < 2) {
            const float size = cell < 70 ? 10.5f : 12.0f;
            Text(graphics, std::wstring(i == 0 ? L"CPU " : L"RAM ") + Percent(i == 0 ? gValues.cpu : gValues.memory),
                x + 4, 0, cell - 8, 18, size, text, true, false, true, true);
            wchar_t available[32]{};
            swprintf_s(available, L"余 %.1fG", std::max(0.0, gValues.memoryTotalGiB - gValues.memoryUsedGiB));
            std::wstring detail = i == 0 ? Temperature(gValues.cpuTemperature) : std::wstring(available);
            if (i == 0 && gValues.cpuClockMaximumMHz > 0) {
                wchar_t compact[32]{};
                if (gValues.cpuTemperature >= 0) swprintf_s(compact, L"%.1fG %.0f°", gValues.cpuClockMaximumMHz / 1000, gValues.cpuTemperature);
                else swprintf_s(compact, L"%.2fGHz", gValues.cpuClockMaximumMHz / 1000);
                detail = compact;
            }
            Text(graphics, detail, x + 4, 18, cell - 8, 18, i == 0 && gValues.cpuClockMaximumMHz > 0 ? 11.5f : 12.0f, secondary, true, false, true, true);
        } else {
            const bool disk = i == 2;
            const std::wstring first = disk ? (gValues.diskAvailable ? DiskRate(gValues.diskReadBytesPerSec) : L"—") : DiskRate(gValues.downBytesPerSec);
            const std::wstring second = disk ? (gValues.diskAvailable ? DiskRate(gValues.diskWriteBytesPerSec) : L"—") : DiskRate(gValues.upBytesPerSec);
            const float size = cell < 70 ? 10.5f : 11.5f;
            Text(graphics, std::wstring(disk ? L"读 " : L"↓ ") + first, x + 4, 0, cell - 8, 18, size, text, true, false, true, true);
            Text(graphics, std::wstring(disk ? L"写 " : L"↑ ") + second, x + 4, 18, cell - 8, 18, size, secondary, true, false, true, true);
        }
        if (i > 0) {
            const float scale = static_cast<float>(Scale());
            const float divider = (std::floor(x * scale) + 0.5f) / scale;
            Gdiplus::Pen line(light ? Color(48, 95, 122, 157) : Color(48, 184, 202, 224), 1.0f / scale);
            graphics.DrawLine(&line, divider, 6.0f, divider, height - 6);
        }
    }
}
#include "DetailRendering.inl"

void DrawDetailsSurface(Gdiplus::Graphics& graphics) {
    const float height = kDetailsBaseHeight + DetailExtraHeight(gDetailSection);
    for (int spread = 14; spread >= 1; --spread) {
        const int alpha = static_cast<int>(std::round(22.0 * (1.0 - (spread - 1) / 13.0)));
        FillRound(graphics, kPopupMargin - spread, kPopupMargin - spread + 6,
            332 + 2.0f * spread, height + 2.0f * spread, kPopupRadius + spread, Color(static_cast<BYTE>(alpha), 0, 0, 0));
    }
    const auto saved = graphics.Save();
    graphics.TranslateTransform(kPopupMargin, kPopupMargin);
    DrawDetails(graphics);
    graphics.Restore(saved);
}

bool PresentLayeredContent(HWND window, int width, int height, bool details) {
    if (width <= 0 || height <= 0) return false;
    Gdiplus::Bitmap rendered(width, height, PixelFormat32bppPARGB);
    {
        Gdiplus::Graphics graphics(&rendered);
        const float scale = static_cast<float>(Scale());
        graphics.Clear(Color(0, 0, 0, 0));
        graphics.ScaleTransform(scale, scale);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        if (details) DrawDetailsSurface(graphics);
        else DrawStrip(graphics, width / scale, height / scale);
    }
    Gdiplus::BitmapData bits{};
    Gdiplus::Rect rect(0, 0, width, height);
    if (rendered.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &bits) != Gdiplus::Ok) return false;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HDC desktop = GetDC(nullptr);
    HDC buffer = CreateCompatibleDC(desktop);
    HBITMAP bitmap = CreateDIBSection(desktop, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    bool success = false;
    if (buffer && bitmap && pixels) {
        for (int row = 0; row < height; ++row) {
            std::memcpy(static_cast<BYTE*>(pixels) + row * width * 4,
                        static_cast<const BYTE*>(bits.Scan0) + row * bits.Stride, width * 4);
        }
        HGDIOBJ old = SelectObject(buffer, bitmap);
        POINT source{0, 0};
        SIZE size{width, height};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        success = UpdateLayeredWindow(window, desktop, nullptr, &size, buffer, &source, 0, &blend, ULW_ALPHA) != FALSE;
        SelectObject(buffer, old);
    }
    rendered.UnlockBits(&bits);
    if (bitmap) DeleteObject(bitmap);
    if (buffer) DeleteDC(buffer);
    ReleaseDC(nullptr, desktop);
    return success;
}

void Paint(HWND window) {
    PAINTSTRUCT paint{};
    BeginPaint(window, &paint);
    RECT area{};
    GetClientRect(window, &area);
    PresentLayeredContent(window, area.right, area.bottom, window == gDetails);
    EndPaint(window, &paint);
}

void ShowDetails(int section) {
    if (!gDetails || !gWindow || !IsWindowVisible(gWindow) || section < 0 || section >= 4) return;
    gDetailSection = section;
    RECT strip{};
    GetWindowRect(gWindow, &strip);
    const double scale = Scale();
    const int width = static_cast<int>(std::ceil((332 + 2 * kPopupMargin) * scale)),
        height = static_cast<int>(std::ceil((kDetailsBaseHeight + DetailExtraHeight(section) + 2 * kPopupMargin) * scale)), gap = static_cast<int>(2 * scale);
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromWindow(gWindow, MONITOR_DEFAULTTONEAREST), &monitor);
    const int center = strip.left + (strip.right - strip.left) * (2 * section + 1) / 8;
    const int x = std::clamp(center - width / 2, static_cast<int>(monitor.rcWork.left) + gap,
                           std::max(static_cast<int>(monitor.rcWork.left) + gap, static_cast<int>(monitor.rcWork.right) - width - gap));
    int y = strip.top - height - gap;
    if (y < monitor.rcWork.top) y = strip.bottom + gap;
    y = std::clamp(y, static_cast<int>(monitor.rcWork.top), std::max(static_cast<int>(monitor.rcWork.top), static_cast<int>(monitor.rcWork.bottom) - height));
    const wchar_t* titles[] = {L"MiniPulse · CPU 详情", L"MiniPulse · 内存详情", L"MiniPulse · 磁盘详情", L"MiniPulse · 网络详情"};
    SetWindowTextW(gDetails, titles[section]);
    RECT previous{};
    GetWindowRect(gDetails, &previous);
    if (!IsWindowVisible(gDetails) || previous.left != x || previous.top != y ||
        previous.right - previous.left != width || previous.bottom - previous.top != height)
        SetWindowPos(gDetails, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(gDetails, nullptr, FALSE);
    UpdateWindow(gDetails);
}

void BeginTracking(HWND window) {
    TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE | TME_HOVER, window, 250};
    TrackMouseEvent(&tracking);
}

LRESULT CALLBACK DetailsProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_PAINT: Paint(window); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_MOUSEMOVE:
        if (gWindow) KillTimer(gWindow, kDismissTimer);
        BeginTracking(window);
        return 0;
    case WM_MOUSELEAVE:
        if (gWindow) SetTimer(gWindow, kDismissTimer, 160, nullptr);
        return 0;
    case WM_LBUTTONUP:
        return 0;
    case WM_RBUTTONUP:
        HideDetails();
        PostMessageW(gHostWindow, gMenuMessage, 0, WM_CONTEXTMENU);
        return 0;
    case WM_DESTROY: gDetails = nullptr; return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}
