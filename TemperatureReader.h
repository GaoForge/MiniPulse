#pragma once

enum class TemperatureState { Checking, Ready, SourceMissing, PermissionDenied, ReadError, SensorMissing, Stale, PermissionRequired, DriverMissing, ComponentMissing };

inline const char* TemperatureStateName(TemperatureState state) {
    switch (state) {
    case TemperatureState::Ready: return "ready";
    case TemperatureState::SourceMissing: return "source_missing";
    case TemperatureState::PermissionDenied: return "permission_denied";
    case TemperatureState::ReadError: return "read_error";
    case TemperatureState::SensorMissing: return "sensor_missing";
    case TemperatureState::Stale: return "stale";
    case TemperatureState::PermissionRequired: return "permission_required";
    case TemperatureState::DriverMissing: return "driver_missing";
    case TemperatureState::ComponentMissing: return "component_missing";
    default: return "checking";
    }
}

inline const wchar_t* TemperatureStateText(TemperatureState state) {
    switch (state) {
    case TemperatureState::Ready: return L"温度数据正常";
    case TemperatureState::SourceMissing: return L"未检测到温度数据源 · 需运行监控软件";
    case TemperatureState::PermissionDenied: return L"温度数据源访问被拒绝 · 请检查权限";
    case TemperatureState::ReadError: return L"温度数据源读取失败 · 请检查监控软件";
    case TemperatureState::SensorMissing: return L"已连接数据源 · 未提供此温度传感器";
    case TemperatureState::Stale: return L"温度数据已过期 · 等待数据源响应";
    case TemperatureState::PermissionRequired: return L"右键菜单 → 启用硬件采温 · 需要管理员授权";
    case TemperatureState::DriverMissing: return L"未安装 PawnIO 驱动 · 请运行安装包启用硬件采温";
    case TemperatureState::ComponentMissing: return L"采温组件缺失 · 请重新安装 MiniPulse";
    default: return L"正在检查温度数据源…";
    }
}

struct Temperatures {
    double cpu = -1.0;
    double memory = -1.0;
    TemperatureState cpuState = TemperatureState::Checking;
    TemperatureState memoryState = TemperatureState::Checking;
    double cpuClockAverageMHz = -1;
    double cpuClockMaximumMHz = -1;
};

// External WMI providers remain a fallback for the separate hardware helper.
void StartTemperatureReader();
Temperatures ReadTemperatures();
void StopTemperatureReader();
