#define WIN32_LEAN_AND_MEAN
#include "WindowsTarget.h"
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <sddl.h>
#include <atomic>
#include <cmath>
#include <string>
#include "HardwareSensors.h"
#include "SensorProtocol.h"

namespace {
HANDLE stop = nullptr, thread = nullptr;
std::atomic<double> cpu{-1}, memory{-1};
std::atomic<double> clockAverage{-1}, clockMaximum{-1};
SRWLOCK clockLock = SRWLOCK_INIT;
std::atomic<TemperatureState> state{TemperatureState::PermissionRequired};
std::atomic<ULONGLONG> updated{0};
DWORD WINAPI Run(void*) {
    state = TemperatureState::Checking;
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) { state = TemperatureState::ReadError; return 0; }
    wchar_t nonce[40]{};
    swprintf_s(nonce, L"%08lx%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x", guid.Data1,
        guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    std::wstring name = L"MiniPulse-Sensors-" + std::wstring(nonce);
    HANDLE token = nullptr;
    DWORD bytes = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) { state = TemperatureState::ReadError; return 0; }
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    std::string storage(bytes, '\0');
    LPWSTR sid = nullptr;
    bool haveSid = GetTokenInformation(token, TokenUser, storage.data(), bytes, &bytes) &&
        ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(storage.data())->User.Sid, &sid);
    CloseHandle(token);
    if (!haveSid) { state = TemperatureState::ReadError; return 0; }
    std::wstring acl = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
        state = TemperatureState::ReadError; return 0;
    }
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    HANDLE pipe = CreateNamedPipeW((L"\\\\.\\pipe\\" + name).c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 64, 64, 0, &security);
    LocalFree(descriptor);
    if (pipe == INVALID_HANDLE_VALUE) { state = TemperatureState::ReadError; return 0; }
    wchar_t path[32768]{}; GetModuleFileNameW(nullptr, path, 32768);
    std::wstring directory(path); directory.resize(directory.find_last_of(L"\\/") + 1);
    const std::wstring exe = directory + L"sensors\\MiniPulse.Sensors.exe";
    const std::wstring args = name + L" " + std::to_wstring(GetCurrentProcessId());
    SHELLEXECUTEINFOW launch{};
    launch.cbSize = sizeof(launch);
    launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    launch.lpVerb = L"runas";
    launch.lpFile = exe.c_str(); launch.lpParameters = args.c_str(); launch.lpDirectory = directory.c_str(); launch.nShow = SW_HIDE;
    if (!ShellExecuteExW(&launch)) {
        state = GetLastError() == ERROR_CANCELLED ? TemperatureState::PermissionRequired : TemperatureState::ComponentMissing;
        CloseHandle(pipe); return 0;
    }
    bool connected = false, received = false;
    const ULONGLONG start = GetTickCount64();
    char buffer[24]{}; DWORD used = 0;
    while (WaitForSingleObject(stop, 100) == WAIT_TIMEOUT) {
        if (!connected) {
            connected = ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
            if (!connected) {
                if (GetTickCount64() - start > 15000 || WaitForSingleObject(launch.hProcess, 0) == WAIT_OBJECT_0) break;
                continue;
            }
        }
        DWORD count = 0;
        if (ReadFile(pipe, buffer + used, 24 - used, &count, nullptr)) {
            used += count;
            if (used == 24) {
                Temperatures decoded;
                used = 0;
                if (!DecodeSensorPacket(buffer, 24, decoded)) break;
                cpu = decoded.cpu; memory = decoded.memory;
                AcquireSRWLockExclusive(&clockLock);
                clockAverage = decoded.cpuClockAverageMHz; clockMaximum = decoded.cpuClockMaximumMHz;
                ReleaseSRWLockExclusive(&clockLock);
                state = decoded.cpuState == TemperatureState::SensorMissing ? TemperatureState::Ready : decoded.cpuState;
                updated = GetTickCount64(); received = true;
            }
        } else if (GetLastError() != ERROR_NO_DATA && GetLastError() != ERROR_PIPE_LISTENING) break;
        if (WaitForSingleObject(launch.hProcess, 0) == WAIT_OBJECT_0) break;
    }
    if (!received || state == TemperatureState::Ready) { cpu = memory = -1; state = TemperatureState::ReadError; }
    CloseHandle(pipe); // Broken pipe stops helper polling; no persistent elevated service.
    CloseHandle(launch.hProcess);
    return 0;
}
}
void StartHardwareSensors() {
    if (thread && WaitForSingleObject(thread, 0) != WAIT_OBJECT_0) return;
    if (thread) { CloseHandle(thread); thread = nullptr; }
    if (stop) CloseHandle(stop);
    stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop) return;
    cpu = memory = -1; clockAverage = clockMaximum = -1; updated = 0;
    thread = CreateThread(nullptr, 0, Run, nullptr, 0, nullptr);
}
void StopHardwareSensors() {
    if (stop) SetEvent(stop);
    if (thread && WaitForSingleObject(thread, 1500) == WAIT_OBJECT_0) {
        CloseHandle(thread); thread = nullptr; CloseHandle(stop); stop = nullptr;
    }
}
Temperatures ReadHardwareSensors() {
    TemperatureState status = state.load();
    if (updated && GetTickCount64() - updated > 5000 && status == TemperatureState::Ready) status = TemperatureState::Stale;
    if (status != TemperatureState::Ready) return {-1, -1, status, status};
    AcquireSRWLockShared(&clockLock);
    const double average = clockAverage.load(), maximum = clockMaximum.load();
    ReleaseSRWLockShared(&clockLock);
    return {cpu.load(), memory.load(), cpu >= 0 ? TemperatureState::Ready : TemperatureState::SensorMissing,
        memory >= 0 ? TemperatureState::Ready : TemperatureState::SensorMissing, average, maximum};
}
