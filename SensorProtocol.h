#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include "TemperatureReader.h"

// Version 2: fixed 24-byte little-endian packet. No paths or commands.
inline bool DecodeSensorPacket(const void* bytes, size_t length, Temperatures& result) {
    struct Packet { uint32_t magic; float cpu, memory; uint32_t status; float average, maximum; } packet{};
    static_assert(sizeof(packet) == 24, "sensor protocol size");
    if (length != sizeof(packet)) return false;
    std::memcpy(&packet, bytes, sizeof(packet));
    if (packet.magic != 0x3254504d || packet.status > 3) return false;
    auto value = [](float v) -> double { return std::isfinite(v) && v > 0 && v <= 150 ? v : -1; };
    TemperatureState state = packet.status == 1 ? TemperatureState::PermissionRequired :
        packet.status == 2 ? TemperatureState::DriverMissing : packet.status == 3 ? TemperatureState::ReadError : TemperatureState::Ready;
    result = {-1, -1, state, state};
    if (state == TemperatureState::Ready) {
        result.cpu = value(packet.cpu); result.memory = value(packet.memory);
        result.cpuState = result.cpu > 0 ? TemperatureState::Ready : TemperatureState::SensorMissing;
        result.memoryState = result.memory > 0 ? TemperatureState::Ready : TemperatureState::SensorMissing;
        if (std::isfinite(packet.average) && std::isfinite(packet.maximum) && packet.average > 0 &&
            packet.maximum >= packet.average && packet.maximum <= 20000) {
            result.cpuClockAverageMHz = packet.average;
            result.cpuClockMaximumMHz = packet.maximum;
        }
    }
    return true;
}
