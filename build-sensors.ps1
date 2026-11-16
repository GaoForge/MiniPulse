$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (!(Test-Path 'sensors\LibreHardwareMonitorLib.dll')) { throw 'Missing sensors dependencies. See sensors/THIRD-PARTY.txt.' }
& $csc /nologo /target:winexe /platform:x64 /optimize+ /win32manifest:MiniPulse.manifest /out:sensors\MiniPulse.Sensors.exe /reference:sensors\LibreHardwareMonitorLib.dll Sensors.cs
if ($LASTEXITCODE -ne 0) { throw 'Hardware helper compilation failed.' }
