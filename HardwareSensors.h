#pragma once
#include "TemperatureReader.h"
void StartHardwareSensors(bool elevated = false);
void StopHardwareSensors();
Temperatures ReadHardwareSensors();
