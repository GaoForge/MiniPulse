#include "../SensorProtocol.h"
#include <cassert>
#include <limits>
#include <cstdio>
int main() {
    struct Packet { uint32_t magic; float cpu, memory; uint32_t status; float average, maximum; } p{0x3254504d, 63.5f, 42.f, 0, 3200.f, 3600.f};
    Temperatures t;
    assert(DecodeSensorPacket(&p, sizeof(p), t) && t.cpu == 63.5 && t.memory == 42 && t.cpuState == TemperatureState::Ready);
    assert(!DecodeSensorPacket(&p, 23, t));
    p.magic = 0; assert(!DecodeSensorPacket(&p, 24, t)); p.magic = 0x3254504d;
    p.status = 4; assert(!DecodeSensorPacket(&p, 24, t));
    p.status = 1; assert(DecodeSensorPacket(&p, 24, t) && t.cpu < 0 && t.cpuState == TemperatureState::PermissionRequired);
    p.status = 2; assert(DecodeSensorPacket(&p, 24, t) && t.memoryState == TemperatureState::DriverMissing);
    p.status = 3; assert(DecodeSensorPacket(&p, 24, t) && t.cpuState == TemperatureState::ReadError);
    p.status = 0; p.cpu = std::numeric_limits<float>::quiet_NaN(); p.memory = 151;
    assert(DecodeSensorPacket(&p, 24, t) && t.cpu < 0 && t.memory < 0 && t.cpuState == TemperatureState::SensorMissing);
    p.cpu = std::numeric_limits<float>::infinity(); p.memory = -1;
    assert(DecodeSensorPacket(&p, 24, t) && t.cpu < 0 && t.memory < 0);
    p.cpu = 0; p.memory = 150;
    assert(DecodeSensorPacket(&p, 24, t) && t.cpu < 0 && t.memory == 150 && t.memoryState == TemperatureState::Ready);
    assert(t.cpuClockAverageMHz == 3200 && t.cpuClockMaximumMHz == 3600);
    p.average = 4000; p.maximum = 3600; assert(DecodeSensorPacket(&p, 24, t) && t.cpuClockMaximumMHz < 0);
    p.average = std::numeric_limits<float>::quiet_NaN(); assert(DecodeSensorPacket(&p, 24, t) && t.cpuClockAverageMHz < 0);
    p.average = 3000; p.maximum = 20001; assert(DecodeSensorPacket(&p, 24, t) && t.cpuClockMaximumMHz < 0);
    p.maximum = 3800; p.status = 1; assert(DecodeSensorPacket(&p, 24, t) && t.cpuClockMaximumMHz < 0);
    assert(!DecodeSensorPacket(&p, 16, t));
    puts("SensorProtocolTests: 16 scenarios passed");
}
