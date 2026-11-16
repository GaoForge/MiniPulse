#include "../CpuSpecification.h"
#include <cassert>
#include <cstdio>
int main() {
    assert(CpuBrandBaseMHz("Intel CPU @ 2.80GHz") == 2800);
    assert(CpuBrandBaseMHz("Intel CPU @ 2800MHz") == 2800);
    assert(CpuBrandBaseMHz("Intel CPU @ 2.80 ghz  ") == 2800);
    assert(CpuBrandBaseMHz("Intel CPU 7700HQ") < 0);
    assert(CpuBrandBaseMHz("Intel CPU @ GHz") < 0);
    assert(CpuBrandBaseMHz("Intel CPU @ 0GHz") < 0);
    assert(CpuBrandBaseMHz("Intel CPU @ -2GHz") < 0);
    assert(CpuBrandBaseMHz("Intel CPU @ NaNGHz") < 0);
    assert(CpuBrandBaseMHz("Intel CPU @ 100GHz") < 0);
    assert(CpuBrandBaseMHz("Intel CPU @ 2.8GHz garbage") < 0);
    assert(CpuBrandBaseMHz("Intel CPU @ 2.8") < 0);
    assert(CpuBrandBaseMHz("Intel CPU @ 2.8THz") < 0);
    const auto actual = ReadCpuSpecification();
    printf("12 CPU specification scenarios passed; live base=%.0f MHz maximum=%.0f MHz brand=%s\n", actual.baseMHz, actual.maximumMHz, actual.fromBrand ? "true" : "false");
}
