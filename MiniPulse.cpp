// MiniPulse - Windows 11 native taskbar performance monitor.
// Native UI: MIT; optional hardware helper dependencies retain their licenses.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "WindowsTarget.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <iphlpapi.h>
#include <ipifcons.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <map>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <cstdio>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include "MonitorMetrics.h"
#include "TaskbarWidget.h"
#include "TemperatureReader.h"
#include "HardwareSensors.h"
#include "CpuSpecification.h"
#include "AutoStart.h"

#ifdef _MSC_VER
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "psapi.lib")
#endif

namespace {
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kAutoStartResult = WM_APP + 2;
constexpr UINT_PTR kTimerId = 1;
constexpr UINT kTrayId = 7;
constexpr UINT kExitId = 101;

constexpr UINT kEmbeddedId = 103;
constexpr UINT kFloatingId = 104;
constexpr UINT kHideWidgetId = 105;
constexpr UINT kTemperatureHelpId = 106;
constexpr UINT kEnableSensorsId = 107;
constexpr UINT kAutoStartId = 108;
constexpr UINT kAutoStartDisableId = 109;
constexpr UINT kAutoStartErrorId = 110;


constexpr COLORREF kWhite = RGB(242, 247, 252);
constexpr COLORREF kCyan = RGB(75, 205, 236);

struct Counters { unsigned long long received = 0, sent = 0; };

HWND gHost = nullptr;

NOTIFYICONDATAW gTray{};
HICON gTrayIcon = nullptr;
UINT gTaskbarCreated = 0;
Metrics gMetrics{};
std::unordered_map<unsigned long long, Counters> gNetworkPrevious;
FILETIME gPrevIdle{}, gPrevKernel{}, gPrevUser{};
bool gHaveCpu = false;
ULONGLONG gPrevNetworkTick = 0;
PDH_HQUERY gDiskQuery = nullptr;
PDH_HCOUNTER gDiskRead = nullptr, gDiskWrite = nullptr;
PDH_HCOUNTER gDiskReads = nullptr, gDiskWrites = nullptr;
std::wstring gDiagnosticsPath;

int gPreviewSection = -1;

bool gPreviewShown = false;
std::wstring gRenderPreviewPath;
bool gRenderPreviewDone = false;
std::wstring gScreenCapturePath;
bool gScreenCaptureDone = false;
std::wstring gDetailCapturePath;
bool gDetailCaptureDone = false;
ULONGLONG gExitAfterMs = 0, gStartedAt = 0;
unsigned int gSampleCount = 0;

void ReadRuntimeOptions() {
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return;
    for (int i = 1; i < count; ++i) {
        const std::wstring argument(arguments[i]);

        if (argument.rfind(L"--preview-hover=", 0) == 0) {
            const std::wstring section = argument.substr(16);
            if (section == L"cpu") gPreviewSection = 0;
            else if (section == L"ram") gPreviewSection = 1;
            else if (section == L"disk") gPreviewSection = 2;
            else if (section == L"net") gPreviewSection = 3;

        }
        else if (argument.rfind(L"--diagnostics=", 0) == 0) gDiagnosticsPath = argument.substr(14);
        else if (argument.rfind(L"--render-preview=", 0) == 0) gRenderPreviewPath = argument.substr(17);
        else if (argument.rfind(L"--capture-widget=", 0) == 0) gScreenCapturePath = argument.substr(17);
        else if (argument.rfind(L"--capture-hover=", 0) == 0) gDetailCapturePath = argument.substr(16);
        else if (argument.rfind(L"--exit-after=", 0) == 0) {
            const unsigned long seconds = wcstoul(argument.c_str() + 13, nullptr, 10);
            if (seconds > 0 && seconds <= 300) gExitAfterMs = seconds * 1000ULL;
        }
    }
    LocalFree(arguments);
}

void WriteDiagnostics() {
    if (gDiagnosticsPath.empty()) return;
    const WidgetDiagnostics widget = GetWidgetDiagnostics();
    char report[2048]{};
    const int size = snprintf(report, sizeof(report),
        "{\"samples\":%u,\"cpu\":%.2f,\"memory\":%.2f,\"cpuTemperature\":%.2f,\"memoryTemperature\":%.2f,"
        "\"diskAvailable\":%s,\"diskReadBytesPerSec\":%.2f,\"diskWriteBytesPerSec\":%.2f,"
        "\"widgetExists\":%s,\"widgetVisible\":%s,\"widgetEmbedded\":%s,\"layoutAvailable\":%s,\"hasSpace\":%s,"
        "\"dpi\":%u,\"obstacles\":%d,\"bounds\":[%ld,%ld,%ld,%ld],\"trayBounds\":[%ld,%ld,%ld,%ld],\"detailsVisible\":%s,\"detailSection\":%d,\"hitRegionsReady\":%s,\"cpuTemperatureState\":\"%s\",\"memoryTemperatureState\":\"%s\"}\n",
        gSampleCount, gMetrics.cpu, gMetrics.memory, gMetrics.cpuTemperature, gMetrics.memoryTemperature,
        gMetrics.diskAvailable ? "true" : "false", gMetrics.diskReadBytesPerSec, gMetrics.diskWriteBytesPerSec,
        widget.exists ? "true" : "false", widget.visible ? "true" : "false", widget.embedded ? "true" : "false",
        widget.layoutAvailable ? "true" : "false", widget.hasSpace ? "true" : "false", widget.dpi, widget.obstacleCount,
        widget.bounds.left, widget.bounds.top, widget.bounds.right, widget.bounds.bottom,
        widget.trayBounds.left, widget.trayBounds.top, widget.trayBounds.right, widget.trayBounds.bottom,
        widget.detailsVisible ? "true" : "false", widget.detailSection, widget.hitRegionsReady ? "true" : "false",
        TemperatureStateName(gMetrics.cpuTemperatureState), TemperatureStateName(gMetrics.memoryTemperatureState));
    if (size <= 0 || size >= static_cast<int>(sizeof(report))) return;
    std::string output(report, size - 2);
    output += ",\"cpuClockAverageMHz\":" + std::to_string(gMetrics.cpuClockAverageMHz) +
        ",\"cpuClockMaximumMHz\":" + std::to_string(gMetrics.cpuClockMaximumMHz) +
        ",\"cpuClockPeakMHz\":" + std::to_string(gMetrics.cpuClockPeakMHz) +
        ",\"cpuBaseMHz\":" + std::to_string(gMetrics.cpuBaseMHz) +
        ",\"cpuRatedMaximumMHz\":" + std::to_string(gMetrics.cpuRatedMaximumMHz) +
        ",\"cpuBaseSource\":\"" + (gMetrics.cpuBaseFromBrand ? "brand" : gMetrics.cpuBaseMHz > 0 ? "cpuid16" : "unavailable") + "\"" +
        ",\"memoryUsedGiB\":" + std::to_string(gMetrics.memoryUsedGiB) +
        ",\"memoryTotalGiB\":" + std::to_string(gMetrics.memoryTotalGiB) +
        ",\"commitUsedGiB\":" + std::to_string(gMetrics.commitUsedGiB) +
        ",\"commitLimitGiB\":" + std::to_string(gMetrics.commitLimitGiB) +
        ",\"systemCacheGiB\":" + std::to_string(gMetrics.systemCacheGiB) + ",\"disks\":[";
    for (size_t i = 0; i < gMetrics.disks.size(); ++i) {
        const auto& disk = gMetrics.disks[i];
        if (i) output += ',';
        output += "{\"instance\":\"";
        for (wchar_t ch : disk.instance) {
            char escaped[8]{};
            snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned int>(ch));
            output += escaped;
        }
        output += "\",\"readBytesPerSec\":" + std::to_string(disk.readBytesPerSec) +
            ",\"writeBytesPerSec\":" + std::to_string(disk.writeBytesPerSec) + '}';
    }
    output += "]}\n";
    HANDLE file = CreateFileW(gDiagnosticsPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, output.data(), static_cast<DWORD>(output.size()), &written, nullptr);
        CloseHandle(file);
    }
}

void InitializeDiskCounters() {
    if (PdhOpenQueryW(nullptr, 0, &gDiskQuery) != ERROR_SUCCESS) return;
    if (PdhAddEnglishCounterW(gDiskQuery, L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec", 0, &gDiskRead) != ERROR_SUCCESS ||
        PdhAddEnglishCounterW(gDiskQuery, L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", 0, &gDiskWrite) != ERROR_SUCCESS) {
        PdhCloseQuery(gDiskQuery);
        gDiskQuery = nullptr;
        return;
    }
    PdhAddEnglishCounterW(gDiskQuery, L"\\PhysicalDisk(*)\\Disk Read Bytes/sec", 0, &gDiskReads);
    PdhAddEnglishCounterW(gDiskQuery, L"\\PhysicalDisk(*)\\Disk Write Bytes/sec", 0, &gDiskWrites);
}

bool DiskCounterValue(PDH_HCOUNTER counter, double& output) {
    PDH_FMT_COUNTERVALUE value{};
    if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, nullptr, &value) != ERROR_SUCCESS ||
        (value.CStatus != PDH_CSTATUS_VALID_DATA && value.CStatus != PDH_CSTATUS_NEW_DATA)) return false;
    output = std::max(0.0, value.doubleValue);
    return true;
}

std::map<std::wstring, double> DiskCounterArray(PDH_HCOUNTER counter) {
    std::map<std::wstring, double> result;
    if (!counter) return result;
    DWORD bytes = 0, count = 0;
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bytes, &count, nullptr) != static_cast<PDH_STATUS>(PDH_MORE_DATA) || bytes > 1024 * 1024) return result;
    std::vector<BYTE> data(bytes);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(data.data());
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bytes, &count, items) != ERROR_SUCCESS) return result;
    for (DWORD i = 0; i < count; ++i) {
        const auto& item = items[i];
        if (!item.szName || wcscmp(item.szName, L"_Total") == 0) continue;
        const auto& value = item.FmtValue;
        if ((value.CStatus == PDH_CSTATUS_VALID_DATA || value.CStatus == PDH_CSTATUS_NEW_DATA) && std::isfinite(value.doubleValue))
            result[item.szName] = std::max(0.0, value.doubleValue);
    }
    return result;
}

unsigned long long FileTimeValue(const FILETIME& v) {
    return (static_cast<unsigned long long>(v.dwHighDateTime) << 32) | v.dwLowDateTime;
}

std::wstring RateText(double bytesPerSec) {
    wchar_t str[48]{};
    if (bytesPerSec >= 1024.0 * 1024.0) {
        swprintf_s(str, L"%.1f MB/s", bytesPerSec / (1024.0 * 1024.0));
    } else if (bytesPerSec >= 1024.0) {
        swprintf_s(str, L"%.0f KB/s", bytesPerSec / 1024.0);
    } else {
        swprintf_s(str, L"%.0f B/s", bytesPerSec);
    }
    return str;
}

void SampleMetrics() {
    ++gSampleCount;
    const ULONGLONG now = GetTickCount64();

    FILETIME idle{}, kernel{}, user{};
    if (GetSystemTimes(&idle, &kernel, &user)) {
        if (gHaveCpu) {
            const auto idleDelta = FileTimeValue(idle) - FileTimeValue(gPrevIdle);
            const auto kernelDelta = FileTimeValue(kernel) - FileTimeValue(gPrevKernel);
            const auto userDelta = FileTimeValue(user) - FileTimeValue(gPrevUser);
            const auto total = kernelDelta + userDelta;
            if (total > 0 && idleDelta <= total) {
                gMetrics.cpu = std::clamp(100.0 * (total - idleDelta) / total, 0.0, 100.0);
            }
        }
        gPrevIdle = idle;
        gPrevKernel = kernel;
        gPrevUser = user;
        gHaveCpu = true;
    }

    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem) && mem.ullTotalPhys > 0) {
        gMetrics.memory = static_cast<double>(mem.dwMemoryLoad);
        gMetrics.memoryUsedGiB = (mem.ullTotalPhys - mem.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
        gMetrics.memoryTotalGiB = mem.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
    }
    PERFORMANCE_INFORMATION performance{};
    performance.cb = sizeof(performance);
    gMetrics.memoryDetailsAvailable = GetPerformanceInfo(&performance, sizeof(performance)) != FALSE;
    if (gMetrics.memoryDetailsAvailable) {
        const double gibPerPage = performance.PageSize / (1024.0 * 1024.0 * 1024.0);
        gMetrics.commitUsedGiB = performance.CommitTotal * gibPerPage;
        gMetrics.commitLimitGiB = performance.CommitLimit * gibPerPage;
        gMetrics.systemCacheGiB = performance.SystemCache * gibPerPage;
    }

    PMIB_IF_TABLE2 table = nullptr;
    double bytesIn = 0.0;
    double bytesOut = 0.0;
    if (GetIfTable2(&table) == NO_ERROR && table) {
        const double elapsed = gPrevNetworkTick ? (now - gPrevNetworkTick) / 1000.0 : 0.0;
        gPrevNetworkTick = now;
        std::unordered_map<unsigned long long, Counters> current;
        current.reserve(table->NumEntries);
        for (ULONG i = 0; i < table->NumEntries; ++i) {
            const auto& nic = table->Table[i];
            // Count physical connected Ethernet / Wi-Fi only. Avoid VPN/TUN double counting.
            if (nic.OperStatus != IfOperStatusUp ||
                !nic.InterfaceAndOperStatusFlags.HardwareInterface ||
                (nic.Type != IF_TYPE_ETHERNET_CSMACD && nic.Type != IF_TYPE_IEEE80211)) {
                continue;
            }
            const auto id = static_cast<unsigned long long>(nic.InterfaceLuid.Value);
            const Counters sample{nic.InOctets, nic.OutOctets};
            auto previous = gNetworkPrevious.find(id);
            if (previous != gNetworkPrevious.end() && elapsed > 0.0) {
                if (sample.received >= previous->second.received) {
                    bytesIn += static_cast<double>(sample.received - previous->second.received);
                }
                if (sample.sent >= previous->second.sent) {
                    bytesOut += static_cast<double>(sample.sent - previous->second.sent);
                }
            }
            current[id] = sample;
        }
        gNetworkPrevious.swap(current);
        FreeMibTable(table);
        if (elapsed > 0.0) {
            gMetrics.downBytesPerSec = bytesIn / elapsed;
            gMetrics.upBytesPerSec = bytesOut / elapsed;
        }
    }

    gMetrics.diskAvailable = false;
    gMetrics.disks.clear();
    if (gDiskQuery && PdhCollectQueryData(gDiskQuery) == ERROR_SUCCESS) {
        gMetrics.diskAvailable = DiskCounterValue(gDiskRead, gMetrics.diskReadBytesPerSec) &&
                                 DiskCounterValue(gDiskWrite, gMetrics.diskWriteBytesPerSec);
        const auto reads = DiskCounterArray(gDiskReads), writes = DiskCounterArray(gDiskWrites);
        std::map<std::wstring, DiskMetrics> disks;
        for (const auto& entry : reads) { auto& disk = disks[entry.first]; disk.instance = entry.first; disk.readBytesPerSec = entry.second; }
        for (const auto& entry : writes) { auto& disk = disks[entry.first]; disk.instance = entry.first; disk.writeBytesPerSec = entry.second; }
        for (const auto& entry : disks) gMetrics.disks.push_back(entry.second);
    }
    const Temperatures temperatures = ReadTemperatures();
    static const CpuSpecification specification = ReadCpuSpecification();
    gMetrics.cpuBaseMHz = specification.baseMHz;
    gMetrics.cpuRatedMaximumMHz = specification.maximumMHz;
    gMetrics.cpuBaseFromBrand = specification.fromBrand;
    gMetrics.cpuClockAverageMHz = temperatures.cpuClockAverageMHz;
    gMetrics.cpuClockMaximumMHz = temperatures.cpuClockMaximumMHz;
    if (temperatures.cpuClockMaximumMHz > 0) gMetrics.cpuClockPeakMHz = std::max(gMetrics.cpuClockPeakMHz, temperatures.cpuClockMaximumMHz);
    gMetrics.cpuTemperature = temperatures.cpu;
    gMetrics.memoryTemperature = temperatures.memory;
    gMetrics.cpuTemperatureState = temperatures.cpuState;
    gMetrics.memoryTemperatureState = temperatures.memoryState;

}

void DrawTextAt(HDC dc, const wchar_t* str, int x, int y, int width, int height,
                HFONT font, COLORREF color, UINT format = DT_LEFT) {
    const HFONT oldFont = static_cast<HFONT>(SelectObject(dc, font));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    RECT rect{x, y, x + width, y + height};
    DrawTextW(dc, str, -1, &rect, format | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(dc, oldFont);
}

HICON CreateCpuIcon(double cpu) {
    constexpr int kSize = 32;
    HDC screen = GetDC(nullptr);
    if (!screen) return nullptr;
    HDC buffer = CreateCompatibleDC(screen);
    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = kSize;
    bitmapInfo.bmiHeader.biHeight = -kSize;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP color = CreateDIBSection(screen, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
    HBITMAP mask = CreateBitmap(kSize, kSize, 1, 1, nullptr);
    if (!buffer || !color || !mask) {
        if (buffer) DeleteDC(buffer);
        if (color) DeleteObject(color);
        if (mask) DeleteObject(mask);
        ReleaseDC(nullptr, screen);
        return nullptr;
    }
    HGDIOBJ old = SelectObject(buffer, color);
    RECT all{0, 0, kSize, kSize};
    HBRUSH b = CreateSolidBrush(RGB(19, 28, 39));
    FillRect(buffer, &all, b);
    DeleteObject(b);
    const COLORREF tint = cpu > 80 ? RGB(255, 128, 113) : kCyan;
    HPEN pen = CreatePen(PS_SOLID, 2, tint);
    HGDIOBJ oldPen = SelectObject(buffer, pen);
    HGDIOBJ oldBrush = SelectObject(buffer, GetStockObject(NULL_BRUSH));
    RoundRect(buffer, 1, 1, 31, 31, 10, 10);
    SelectObject(buffer, oldBrush);
    SelectObject(buffer, oldPen);
    DeleteObject(pen);
    HFONT font = CreateFontW(-17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    wchar_t text[8]{};
    swprintf_s(text, L"%.0f", std::clamp(cpu, 0.0, 100.0));
    DrawTextAt(buffer, text, 2, 4, 28, 23, font, kWhite, DT_CENTER);
    DeleteObject(font);
    SelectObject(buffer, old);
    // A 32-bit icon needs explicit alpha; otherwise GDI may leave it fully transparent.
    GdiFlush();
    if (pixels) {
        auto* argb = static_cast<std::uint32_t*>(pixels);
        for (int i = 0; i < kSize * kSize; ++i) argb[i] |= 0xFF000000u;
    }

    HDC maskDc = CreateCompatibleDC(screen);
    if (maskDc) {
        HGDIOBJ oldMask = SelectObject(maskDc, mask);
        PatBlt(maskDc, 0, 0, kSize, kSize, BLACKNESS); // Black mask = opaque icon.
        SelectObject(maskDc, oldMask);
        DeleteDC(maskDc);
    }
    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmColor = color;
    info.hbmMask = mask;
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(color);
    DeleteObject(mask);
    DeleteDC(buffer);
    ReleaseDC(nullptr, screen);
    return icon;
}

void UpdateTray(bool add = false) {
    HICON next = CreateCpuIcon(gMetrics.cpu);
    if (next) {
        HICON previous = gTrayIcon;
        gTrayIcon = next;
        gTray.hIcon = gTrayIcon;
        if (previous) DestroyIcon(previous);
    }
    swprintf_s(gTray.szTip, L"MiniPulse | CPU %.0f%% | RAM %.0f%% | \u2193 %ls | \u2191 %ls",
               gMetrics.cpu, gMetrics.memory, RateText(gMetrics.downBytesPerSec).c_str(),
               RateText(gMetrics.upBytesPerSec).c_str());
    Shell_NotifyIconW(add ? NIM_ADD : NIM_MODIFY, &gTray);
}

AutoStartResult gAutoStart{AutoStartState::Unknown};
bool gAutoStartBusy = false;
AutoStartAction gAutoStartRunning = AutoStartAction::Query;
AutoStartAction gAutoStartPending = AutoStartAction::Query;

void StartAutoStartAction(AutoStartAction action) {
    if (gAutoStartBusy) {
        if (gAutoStartRunning == AutoStartAction::Query) gAutoStartPending = action;
        return;
    }
    gAutoStartBusy = true;
    gAutoStartRunning = action;
    try {
        std::thread([window = gHost, action] {
            const auto result = ManageAutoStart(action);
            PostMessageW(window, kAutoStartResult, static_cast<WPARAM>(result.state), result.error);
        }).detach();
    } catch (const std::system_error&) {
        PostMessageW(gHost, kAutoStartResult, static_cast<WPARAM>(AutoStartState::Error), E_FAIL);
    }
}

void AppendAutoStartMenu(HMENU menu) {
    using State = AutoStartState;
    if (gAutoStartBusy && gAutoStartRunning != AutoStartAction::Query) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"正在更新登录自启…");
        return;
    }
    switch (gAutoStart.state) {
    case State::On:
    case State::Legacy:
        AppendMenuW(menu, MF_STRING | MF_CHECKED, kAutoStartDisableId, L"登录时启动 MiniPulse");
        break;
    case State::Off:
        AppendMenuW(menu, MF_STRING, kAutoStartId, L"登录时启动 MiniPulse");
        break;
    case State::NeedsRepair:
        AppendMenuW(menu, MF_STRING, kAutoStartId, L"修复登录自启");
        AppendMenuW(menu, MF_STRING, kAutoStartDisableId, L"关闭登录自启");
        break;
    case State::Conflict:
        AppendMenuW(menu, MF_STRING, kAutoStartErrorId, L"登录自启：任务冲突");
        break;
    case State::Error:
        AppendMenuW(menu, MF_STRING, kAutoStartErrorId, L"登录自启：查看错误");
        break;
    case State::Unknown:
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"正在检查登录自启…");
        break;
    }
}

void ShowContextMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    const WidgetPlacement placement = GetWidgetPlacement();
    AppendMenuW(menu, MF_STRING | (placement == WidgetPlacement::Taskbar ? MF_CHECKED : 0), kEmbeddedId, L"指标栏：任务栏内部（自动避让）");
    AppendMenuW(menu, MF_STRING | (placement == WidgetPlacement::AboveTaskbar ? MF_CHECKED : 0), kFloatingId, L"指标栏：任务栏上方");
    AppendMenuW(menu, MF_STRING | (placement == WidgetPlacement::Hidden ? MF_CHECKED : 0), kHideWidgetId, L"隐藏指标栏");
    if (placement == WidgetPlacement::Taskbar && !WidgetHasSpace()) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"布局不可用或空间不足，可切换到上方");
    }
    AppendMenuW(menu, MF_STRING, kEnableSensorsId, L"重试硬件采温");
    AppendMenuW(menu, MF_STRING, kTemperatureHelpId, L"温度数据说明");
    AppendAutoStartMenu(menu);
    if (!gAutoStartBusy) StartAutoStartAction(AutoStartAction::Query);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kExitId, L"退出 MiniPulse");
    POINT p{};
    GetCursorPos(&p);
    SetForegroundWindow(gHost);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, p.x, p.y, 0, gHost, nullptr);
    PostMessageW(gHost, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

LRESULT CALLBACK HostProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == gTaskbarCreated && gTaskbarCreated) {
        UpdateTray(true); // Explorer restarted: re-register the tray icon.
        UpdateTaskbarWidget(gMetrics);
        return 0;
    }
    switch (msg) {
    case kAutoStartResult: {
        gAutoStartBusy = false;
        gAutoStart = {static_cast<AutoStartState>(wp), static_cast<long>(lp)};
        const auto pending = std::exchange(gAutoStartPending, AutoStartAction::Query);
        if (pending != AutoStartAction::Query) StartAutoStartAction(pending);
        else if (gAutoStart.error && gAutoStartRunning != AutoStartAction::Query) {
            MessageBoxW(hwnd, AutoStartErrorText(gAutoStart.error), L"MiniPulse · 登录自启", MB_OK | MB_ICONWARNING);
        }
        return 0;
    }
    case WM_TIMER:
        if (wp == kTimerId) {
            SampleMetrics();
            UpdateTray();
            UpdateTaskbarWidget(gMetrics);
            const bool cpuPreviewReady = gMetrics.cpuClockMaximumMHz > 0 || gSampleCount >= 30;
            if (!gPreviewShown && gPreviewSection >= 0 && gSampleCount >= 3 && cpuPreviewReady && GetWidgetDiagnostics().visible) {
                PreviewWidgetDetails(gPreviewSection);
                gPreviewShown = true;
            }
            if (!gRenderPreviewDone && !gRenderPreviewPath.empty() && gSampleCount >= 3 && cpuPreviewReady) {
                gRenderPreviewDone = ExportWidgetPreview(gRenderPreviewPath.c_str());
            }
            if (!gDetailCaptureDone && !gDetailCapturePath.empty() && gPreviewShown) {
                gDetailCaptureDone = CaptureDetailsOnScreen(gDetailCapturePath.c_str());
            }
            WriteDiagnostics();
            if (!gScreenCaptureDone && !gScreenCapturePath.empty() && gSampleCount >= 5 &&
                cpuPreviewReady) {
                GdiFlush();
                gScreenCaptureDone = CaptureWidgetOnScreen(gScreenCapturePath.c_str());
            }

            if (gExitAfterMs && GetTickCount64() - gStartedAt >= gExitAfterMs) DestroyWindow(hwnd);
        }
        return 0;
    case kTrayMessage:
        if (lp == WM_LBUTTONUP) ShowContextMenu();
        else if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) ShowContextMenu();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {

        case kEmbeddedId: SetWidgetPlacement(WidgetPlacement::Taskbar); return 0;
        case kFloatingId: SetWidgetPlacement(WidgetPlacement::AboveTaskbar); return 0;
        case kHideWidgetId: SetWidgetPlacement(WidgetPlacement::Hidden); return 0;
        case kEnableSensorsId: StartHardwareSensors(); return 0;
        case kTemperatureHelpId: {
            const std::wstring status = L"CPU：" + std::wstring(TemperatureStateText(gMetrics.cpuTemperatureState)) +
                L"\n内存：" + TemperatureStateText(gMetrics.memoryTemperatureState) + L"\n\n" +
                L"独立采温采用 LibreHardwareMonitor + PawnIO。请先通过安装包安装驱动，程序启动后会自动启用硬件采温。\n\n"
                        L"MiniPulse 启动时自动请求管理员权限，采温组件继承权限，无需单独授权；采温进程随 MiniPulse 退出，也支持已有监控软件的 WMI 数据作为备用。\n\n"
                        L"CPU 优先显示封装温度，否则显示最热核心；内存只使用明确标注的内存温度传感器。\n"
                        L"没有可用数据或硬件不支持时显示“—”，不会用主板或 ACPI 温度代替。\n\n"
                        L"磁盘读写为全部物理磁盘的合计吞吐量。指标栏 K/s 和 M/s 分别为 KiB/s 和 MiB/s。";
            MessageBoxW(gHost, status.c_str(),
                        L"MiniPulse · 数据说明", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        case kAutoStartId: StartAutoStartAction(AutoStartAction::Enable); return 0;
        case kAutoStartErrorId:
            if (gAutoStart.error) MessageBoxW(hwnd, AutoStartErrorText(gAutoStart.error), L"MiniPulse · 登录自启", MB_OK | MB_ICONWARNING);
            return 0;
        case kAutoStartDisableId: StartAutoStartAction(AutoStartAction::Disable); return 0;
        case kExitId: DestroyWindow(hwnd); return 0;
        }
        break;
    case WM_DESTROY:
        KillTimer(hwnd, kTimerId);
        StopTaskbarWidget();
        StopHardwareSensors();
        StopTemperatureReader();
        if (gDiskQuery) { PdhCloseQuery(gDiskQuery); gDiskQuery = nullptr; }
        Shell_NotifyIconW(NIM_DELETE, &gTray);
        if (gTrayIcon) { DestroyIcon(gTrayIcon); gTrayIcon = nullptr; }

        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    const int autoStartExit = RunAutoStartCommand(commandLine);
    if (autoStartExit != -1) return autoStartExit;
    ReadRuntimeOptions();
    gStartedAt = GetTickCount64();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\MiniPulseTraySingleton_1014");
    if (!single || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (single) CloseHandle(single);
        return 0;
    }
    gTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSEXW hostClass{};
    hostClass.cbSize = sizeof(hostClass);
    hostClass.lpfnWndProc = HostProc;
    hostClass.hInstance = instance;
    hostClass.lpszClassName = L"MiniPulseHostClass";
    if (!RegisterClassExW(&hostClass)) { CloseHandle(single); return 1; }
    gHost = CreateWindowExW(0, hostClass.lpszClassName, L"MiniPulse",
                            WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!gHost) { CloseHandle(single); return 1; }
    StartAutoStartAction(AutoStartAction::Query);
    gTray.cbSize = sizeof(gTray);
    gTray.hWnd = gHost;
    gTray.uID = kTrayId;
    gTray.uCallbackMessage = kTrayMessage;
    gTray.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    StartTaskbarWidget(instance, gHost, kTrayMessage);
    StartTemperatureReader();
    StartHardwareSensors();
    InitializeDiskCounters();
    SampleMetrics();
    UpdateTray(true);
    UpdateTaskbarWidget(gMetrics);
    WriteDiagnostics();
    SetTimer(gHost, kTimerId, 1000, nullptr);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    ReleaseMutex(single);
    CloseHandle(single);
    return static_cast<int>(msg.wParam);
}
