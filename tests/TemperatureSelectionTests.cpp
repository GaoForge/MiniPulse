#include "../TemperatureSelection.h"
#include <iostream>
#include <limits>
#include <stdexcept>

void Check(const TemperatureSelection& selection, double cpu, double memory) {
    const auto values = selection.Values();
    if (values.cpu != cpu || values.memory != memory) throw std::runtime_error("Incorrect temperature source selection");
}

int main() {
    TemperatureSelection readings;
    Check(readings, -1, -1);
    readings.Add(L"/mainboard/temperature/0", L"system", 32);
    readings.Add(L"/nvidiagpu/0/temperature/1", L"gpu memory junction", 72);
    readings.Add(L"/intelgpu/0/temperature/1", L"memory", 60);
    Check(readings, -1, -1);
    readings.Add(L"/intelcpu/0/temperature/0", L"cpu core #1", 51);
    readings.Add(L"/intelcpu/0/temperature/1", L"cpu core #2", 57);
    Check(readings, 57, -1);
    readings.Add(L"/intelcpu/0/temperature/2", L"cpu package", 54);
    Check(readings, 54, -1);
    readings.Add(L"/mainboard/temperature/3", L"dimm a1", 38);
    readings.Add(L"/ram/1/temperature/0", L"temperature", 41);
    Check(readings, 54, 41);
    readings.Add(L"/intelcpu/0/temperature/3", L"memory controller", 65);
    readings.Add(L"/ram/1/temperature/1", L"temperature", -1);
    readings.Add(L"/ram/1/temperature/2", L"temperature", 200);
    readings.Add(L"/ram/1/temperature/3", L"temperature", std::numeric_limits<double>::quiet_NaN());
    Check(readings, 54, 41);
    TemperatureSelection amd;
    amd.Add(L"/amdcpu/0/temperature/0", L"cpu (tctl/tdie)", 62);
    Check(amd, 62, -1);
    std::cout << "7 temperature source scenarios passed (missing sensors, CPU package/core, RAM, GPU exclusion, invalid values).\n";
}
