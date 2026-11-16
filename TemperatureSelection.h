#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include "TemperatureReader.h"

inline bool SensorContains(const std::wstring& text, const wchar_t* word) {
    return text.find(word) != std::wstring::npos;
}

// Inputs are normalized to lowercase by the WMI reader.
struct TemperatureSelection {
    double package = -1.0, cores = -1.0, memory = -1.0;
    void Add(const std::wstring& id, const std::wstring& name, double value) {
        if (!std::isfinite(value) || value <= 0.0 || value > 150.0) return;
        const bool cpu = SensorContains(id, L"/intelcpu/") || SensorContains(id, L"/amdcpu/") || SensorContains(id, L"/cpu/");
        if (cpu) {
            if (SensorContains(name, L"package") || SensorContains(name, L"tctl") || name == L"cpu core") package = std::max(package, value);
            else cores = std::max(cores, value);
        }
        const bool excluded = cpu || SensorContains(id, L"gpu") || SensorContains(name, L"gpu") ||
                              SensorContains(name, L"vram") || SensorContains(name, L"graphics") ||
                              SensorContains(name, L"controller");
        if (!excluded && (SensorContains(id, L"/ram/") || SensorContains(id, L"/memory/") || SensorContains(id, L"/dimm/") ||
            SensorContains(name, L"dimm") || SensorContains(name, L"dram") || SensorContains(name, L"ddr") || SensorContains(name, L"memory"))) {
            memory = std::max(memory, value);
        }
    }
    Temperatures Values() const { return {package >= 0.0 ? package : cores, memory}; }
};
