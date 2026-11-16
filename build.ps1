$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
New-Item -ItemType Directory -Force build | Out-Null
& (Join-Path $PSScriptRoot 'build-sensors.ps1')

$compiler = Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot '.tools') -Directory -Filter 'llvm-mingw-*' -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending | Select-Object -First 1
if (!$compiler) {
    throw 'Portable LLVM-MinGW not found in .tools. Alternatively, run build.bat from an MSVC x64 developer command prompt.'
}
$compilerBin = Join-Path $compiler.FullName 'bin'
& (Join-Path $compilerBin 'x86_64-w64-mingw32-windres.exe') 'resources.rc' '-O' 'coff' '-o' 'build\resources.o'
if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }
& (Join-Path $compilerBin 'x86_64-w64-mingw32-clang++.exe') '-std=c++17' '-O2' '-Wall' '-Wextra' '-static' '-municode' '-mwindows' 'MiniPulse.cpp' 'AutoStart.cpp' 'TemperatureReader.cpp' 'HardwareSensors.cpp' 'TaskbarWidget.cpp' 'build\resources.o' '-o' 'MiniPulse.exe' '-lshell32' '-luser32' '-lgdi32' '-liphlpapi' '-ldwmapi' '-lpdh' '-ladvapi32' '-lwbemuuid' '-lole32' '-loleaut32' '-luuid' '-lgdiplus' '-ltaskschd'
if ($LASTEXITCODE -ne 0) { throw 'C++ compilation failed.' }
Write-Host "Built: $(Join-Path $PSScriptRoot 'MiniPulse.exe')"
