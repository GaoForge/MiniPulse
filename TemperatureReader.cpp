#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "WindowsTarget.h"
#include <windows.h>
#include <wbemidl.h>
#include <oleauto.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwctype>
#include <memory>
#include <string>
#include "TemperatureReader.h"
#include "TemperatureSelection.h"
#include "HardwareSensors.h"

#ifdef _MSC_VER
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#endif

namespace {
std::atomic<double> gCpu{-1.0}, gMemory{-1.0};
std::atomic<TemperatureState> gCpuState{TemperatureState::Checking}, gMemoryState{TemperatureState::Checking};
std::atomic<ULONGLONG> gUpdated{0};
HANDLE gStop = nullptr;
HANDLE gThread = nullptr;

struct ComRelease {
    template<typename T> void operator()(T* object) const { if (object) object->Release(); }
};
template<typename T> using ComPtr = std::unique_ptr<T, ComRelease>;

struct BString {
    BSTR value;
    explicit BString(const wchar_t* text) : value(SysAllocString(text)) {}
    ~BString() { SysFreeString(value); }
};

std::wstring PropertyText(IWbemClassObject* object, const wchar_t* name) {
    VARIANT value;
    VariantInit(&value);
    std::wstring result;
    if (SUCCEEDED(object->Get(name, 0, &value, nullptr, nullptr)) && value.vt == VT_BSTR && value.bstrVal) {
        result = value.bstrVal;
    }
    VariantClear(&value);
    std::transform(result.begin(), result.end(), result.begin(), [](wchar_t c) { return std::towlower(c); });
    return result;
}

double PropertyNumber(IWbemClassObject* object, const wchar_t* name) {
    VARIANT value, converted;
    VariantInit(&value);
    VariantInit(&converted);
    double result = -1.0;
    if (SUCCEEDED(object->Get(name, 0, &value, nullptr, nullptr)) &&
        value.vt != VT_NULL && value.vt != VT_EMPTY &&
        SUCCEEDED(VariantChangeType(&converted, &value, 0, VT_R8))) {
        if (std::isfinite(converted.dblVal) && converted.dblVal > 0.0 && converted.dblVal <= 150.0) {
            result = converted.dblVal;
        }
    }
    VariantClear(&converted);
    VariantClear(&value);
    return result;
}

Temperatures QueryProvider(IWbemLocator* locator, const wchar_t* namespaceName) {
    Temperatures result;
    result.cpuState = result.memoryState = TemperatureState::ReadError;
    BString namespaceString(namespaceName);
    IWbemServices* rawServices = nullptr;
    if (!namespaceString.value) return result;
    const HRESULT connect = locator->ConnectServer(namespaceString.value, nullptr, nullptr, nullptr,
            WBEM_FLAG_CONNECT_USE_MAX_WAIT, nullptr, nullptr, &rawServices);
    if (FAILED(connect)) {
        if (connect == static_cast<HRESULT>(WBEM_E_INVALID_NAMESPACE)) result.cpuState = result.memoryState = TemperatureState::SourceMissing;
        else if (connect == E_ACCESSDENIED || connect == static_cast<HRESULT>(WBEM_E_ACCESS_DENIED)) result.cpuState = result.memoryState = TemperatureState::PermissionDenied;
        return result;
    }
    ComPtr<IWbemServices> services(rawServices);
    if (FAILED(CoSetProxyBlanket(services.get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
            RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE))) return result;

    BString language(L"WQL");
    BString query(L"SELECT Identifier, Name, Value FROM Sensor WHERE SensorType = 'Temperature'");
    IEnumWbemClassObject* rawEnumerator = nullptr;
    if (!language.value || !query.value || FAILED(services->ExecQuery(language.value, query.value,
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &rawEnumerator))) return result;
    ComPtr<IEnumWbemClassObject> enumerator(rawEnumerator);
    TemperatureSelection selection;
    bool failed = false;
    for (int i = 0; i < 256 && WaitForSingleObject(gStop, 0) != WAIT_OBJECT_0; ++i) {
        IWbemClassObject* rawSensor = nullptr;
        ULONG count = 0;
        const HRESULT status = enumerator->Next(250, 1, &rawSensor, &count);
        ComPtr<IWbemClassObject> sensor(rawSensor);
        if (FAILED(status) || status == WBEM_S_TIMEDOUT) { failed = true; break; }
        if (count == 0 || !sensor) break;
        const std::wstring id = PropertyText(sensor.get(), L"Identifier");
        const std::wstring name = PropertyText(sensor.get(), L"Name");
        const double value = PropertyNumber(sensor.get(), L"Value");
        selection.Add(id, name, value);
    }
    result = selection.Values();
    const TemperatureState missing = failed ? TemperatureState::ReadError : TemperatureState::SensorMissing;
    result.cpuState = result.cpu >= 0.0 ? TemperatureState::Ready : missing;
    result.memoryState = result.memory >= 0.0 ? TemperatureState::Ready : missing;
    return result;
}

void MergeReading(double& value, TemperatureState& state, double fallback, TemperatureState fallbackState) {
    if (value >= 0.0) return;
    if (fallback >= 0.0 || state == TemperatureState::SourceMissing || state == TemperatureState::Checking) {
        value = fallback;
        state = fallbackState;
    }
}

DWORD WINAPI ReaderThread(void*) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 0;
    CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
                         RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
    IWbemLocator* rawLocator = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IWbemLocator, reinterpret_cast<void**>(&rawLocator)))) {
        ComPtr<IWbemLocator> locator(rawLocator);
        do {
            Temperatures values = QueryProvider(locator.get(), L"ROOT\\LibreHardwareMonitor");
            if (values.cpu < 0.0 || values.memory < 0.0) {
                const Temperatures fallback = QueryProvider(locator.get(), L"ROOT\\OpenHardwareMonitor");
                MergeReading(values.cpu, values.cpuState, fallback.cpu, fallback.cpuState);
                MergeReading(values.memory, values.memoryState, fallback.memory, fallback.memoryState);
            }
            gCpu.store(values.cpu);
            gMemory.store(values.memory);
            gCpuState.store(values.cpuState);
            gMemoryState.store(values.memoryState);
            gUpdated.store(GetTickCount64());
        } while (WaitForSingleObject(gStop, 5000) == WAIT_TIMEOUT);
    }
    CoUninitialize();
    return 0;
}
} // namespace

void StartTemperatureReader() {
    gStop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!gStop) return;
    gThread = CreateThread(nullptr, 0, ReaderThread, nullptr, 0, nullptr);
    if (!gThread) { CloseHandle(gStop); gStop = nullptr; }
}

Temperatures ReadTemperatures() {
    Temperatures hardware = ReadHardwareSensors();
    const ULONGLONG updated = gUpdated.load();
    if (updated && GetTickCount64() - updated <= 15000) {
        if (hardware.cpu < 0 && gCpu.load() >= 0) { hardware.cpu = gCpu; hardware.cpuState = TemperatureState::Ready; }
        if (hardware.memory < 0 && gMemory.load() >= 0) { hardware.memory = gMemory; hardware.memoryState = TemperatureState::Ready; }
    }
    return hardware;
}

void StopTemperatureReader() {
    if (gStop) SetEvent(gStop);
    if (gThread) {
        // A broken external WMI provider must not indefinitely block application exit.
        const DWORD result = WaitForSingleObject(gThread, 1500);
        CloseHandle(gThread);
        gThread = nullptr;
        if (result == WAIT_OBJECT_0) { CloseHandle(gStop); gStop = nullptr; }
        // If still inside COM, keep the event valid until process teardown.
    }
}
