$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
& (Join-Path $PSScriptRoot 'build.ps1')
$innoCompiler = Join-Path $PSScriptRoot '.tools\inno6\ISCC.exe'
if (!(Test-Path -LiteralPath $innoCompiler)) {
    $installedCompiler = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($installedCompiler) { $innoCompiler = $installedCompiler.Source }
    else { throw 'Inno Setup compiler not found at .tools\inno6\ISCC.exe or in PATH.' }
}
& $innoCompiler (Join-Path $PSScriptRoot 'installer.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
Write-Host "Installer: $(Join-Path $PSScriptRoot 'dist\MiniPulse-Setup-1.6.3-x64.exe')"
Compress-Archive -Path MiniPulse.exe,LICENSE,INSTALL-NOTES.txt,sensors -DestinationPath 'dist\MiniPulse-1.6.3-x64-portable.zip' -Force
$releaseFiles = 'dist\MiniPulse-Setup-1.6.3-x64.exe','dist\MiniPulse-1.6.3-x64-portable.zip','MiniPulse.exe'
$hashLines = foreach ($releaseFile in $releaseFiles) {
    $hash = Get-FileHash -LiteralPath $releaseFile -Algorithm SHA256
    '{0}  {1}' -f $hash.Hash.ToLowerInvariant(), [IO.Path]::GetFileName($releaseFile)
}
[IO.File]::WriteAllLines((Join-Path $PSScriptRoot 'dist\SHA256SUMS.txt'), $hashLines)
