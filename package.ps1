$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
$version = [regex]::Match((Get-Content -LiteralPath 'installer.iss' -Raw), '(?m)^#define AppVersion "([^"]+)"').Groups[1].Value
if (!$version) { throw 'AppVersion not found in installer.iss.' }
& (Join-Path $PSScriptRoot 'build.ps1')
$innoCompiler = Join-Path $PSScriptRoot '.tools\inno6\ISCC.exe'
if (!(Test-Path -LiteralPath $innoCompiler)) {
    $installedCompiler = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($installedCompiler) { $innoCompiler = $installedCompiler.Source }
    else { throw 'Inno Setup compiler not found at .tools\inno6\ISCC.exe or in PATH.' }
}
& $innoCompiler (Join-Path $PSScriptRoot 'installer.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
$setup = "dist\MiniPulse-Setup-$version-x64.exe"
$portable = "dist\MiniPulse-$version-x64-portable.zip"
Write-Host "Installer: $(Join-Path $PSScriptRoot $setup)"
Compress-Archive -Path MiniPulse.exe,LICENSE,INSTALL-NOTES.txt,sensors -DestinationPath $portable -Force
$releaseFiles = $setup,$portable,'MiniPulse.exe'
$hashLines = foreach ($releaseFile in $releaseFiles) {
    $hash = Get-FileHash -LiteralPath $releaseFile -Algorithm SHA256
    '{0}  {1}' -f $hash.Hash.ToLowerInvariant(), [IO.Path]::GetFileName($releaseFile)
}
[IO.File]::WriteAllLines((Join-Path $PSScriptRoot 'dist\SHA256SUMS.txt'), $hashLines)
