#pragma once
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cctype>
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <cpuid.h>
#endif

struct CpuSpecification { double baseMHz = -1, maximumMHz = -1; bool fromBrand = false; };
inline double CpuBrandBaseMHz(const char* brand) {
    const char* at = std::strchr(brand, '@');
    if (!at) return -1;
    char* units = nullptr;
    const double value = std::strtod(at + 1, &units);
    if (!std::isfinite(value) || value <= 0 || units == at + 1) return -1;
    while (std::isspace(static_cast<unsigned char>(*units))) ++units;
    if (std::strlen(units) < 3) return -1;
    const bool ghz = std::tolower(static_cast<unsigned char>(units[0])) == 'g';
    const bool mhz = std::tolower(static_cast<unsigned char>(units[0])) == 'm';
    if ((!ghz && !mhz) || std::tolower(static_cast<unsigned char>(units[1])) != 'h' ||
        std::tolower(static_cast<unsigned char>(units[2])) != 'z') return -1;
    const char* tail = units + 3;
    while (std::isspace(static_cast<unsigned char>(*tail))) ++tail;
    const double frequency = value * (ghz ? 1000 : 1);
    return !*tail && frequency <= 20000 ? frequency : -1;
}
inline CpuSpecification ReadCpuSpecification() {
    unsigned int a = 0, b = 0, c = 0, d = 0;
#ifdef _MSC_VER
    int data[4]{};
    __cpuidex(data, 0, 0);
    a = data[0]; b = data[1]; c = data[2]; d = data[3];
#else
    __cpuid_count(0, 0, a, b, c, d);
#endif
    // Leaf 16h is defined by Intel. These are specifications, never live clocks.
    if (b != 0x756e6547 || d != 0x49656e69 || c != 0x6c65746e) return {};
    CpuSpecification result;
    if (a >= 0x16) {
#ifdef _MSC_VER
    __cpuidex(data, 0x16, 0); a = data[0]; b = data[1];
#else
    __cpuid_count(0x16, 0, a, b, c, d);
#endif
    if ((a & 0xffff) > 0 && (a & 0xffff) <= 20000) result.baseMHz = a & 0xffff;
    if ((b & 0xffff) >= (a & 0xffff) && (b & 0xffff) > 0 && (b & 0xffff) <= 20000) result.maximumMHz = b & 0xffff;
    }
    if (result.baseMHz <= 0) {
#ifdef _MSC_VER
        __cpuidex(data, static_cast<int>(0x80000000), 0); a = data[0];
#else
        __cpuid_count(0x80000000, 0, a, b, c, d);
#endif
        if (a >= 0x80000004) {
            char brand[49]{};
            for (unsigned int i = 0; i < 3; ++i) {
#ifdef _MSC_VER
                __cpuidex(data, static_cast<int>(0x80000002 + i), 0);
                std::memcpy(brand + 16 * i, data, 16);
#else
                __cpuid_count(0x80000002 + i, 0, a, b, c, d);
                unsigned int words[] = {a, b, c, d};
                std::memcpy(brand + 16 * i, words, 16);
#endif
            }
            result.baseMHz = CpuBrandBaseMHz(brand);
            result.fromBrand = result.baseMHz > 0;
        }
    }
    return result;
}
