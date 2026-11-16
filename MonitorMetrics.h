#pragma once
#include "TemperatureReader.h"
#include <string>
#include <vector>

struct DiskMetrics {
    std::wstring instance;
    double readBytesPerSec = -1;
    double writeBytesPerSec = -1;
};

struct Metrics {
    double cpu = 0.0;
    double memory = 0.0;
    double memoryUsedGiB = 0.0;
    double memoryTotalGiB = 0.0;
    double commitUsedGiB = 0.0;
    double commitLimitGiB = 0.0;
    double systemCacheGiB = 0.0;
    bool memoryDetailsAvailable = false;
    std::vector<DiskMetrics> disks;
    double downBytesPerSec = 0.0;
    double upBytesPerSec = 0.0;
    double cpuTemperature = -1.0;
    double cpuClockAverageMHz = -1;
    double cpuClockMaximumMHz = -1;
    double cpuClockPeakMHz = -1;
    double cpuBaseMHz = -1;
    double cpuRatedMaximumMHz = -1;
    bool cpuBaseFromBrand = false;
    double memoryTemperature = -1.0;
    double diskReadBytesPerSec = 0.0;
    double diskWriteBytesPerSec = 0.0;
    bool diskAvailable = false;
    TemperatureState cpuTemperatureState = TemperatureState::Checking;
    TemperatureState memoryTemperatureState = TemperatureState::Checking;
};
