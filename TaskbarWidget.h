#pragma once
#include <windows.h>
#include "MonitorMetrics.h"

enum class WidgetPlacement { Taskbar = 0, AboveTaskbar = 1, Hidden = 2 };
void StartTaskbarWidget(HINSTANCE instance, HWND host, UINT menuMessage);
void UpdateTaskbarWidget(const Metrics& metrics);
void SetWidgetPlacement(WidgetPlacement placement);
WidgetPlacement GetWidgetPlacement();
bool WidgetHasSpace();
void PreviewWidgetDetails(int section);
bool ExportWidgetPreview(const wchar_t* path);
bool CaptureWidgetOnScreen(const wchar_t* path);
bool CaptureDetailsOnScreen(const wchar_t* path);
struct WidgetDiagnostics {
    bool exists = false, visible = false, embedded = false, layoutAvailable = false, hasSpace = false;
    RECT bounds{}, trayBounds{};
    UINT dpi = 96;
    int obstacleCount = 0;
    bool detailsVisible = false;
    int detailSection = -1;
    bool hitRegionsReady = false;
};
WidgetDiagnostics GetWidgetDiagnostics();
void StopTaskbarWidget();
