# build-rust.ps1 — builds the Rust workspace with MSVC environment configured.
# Usage: powershell -File tools\build-rust.ps1 [cargo args...]
param([Parameter(ValueFromRemainingArguments)][string[]]$CargoArgs = @("build", "--all"))

$ErrorActionPreference = "Stop"

# Locate MSVC
$vsBase = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
if (-not (Test-Path $vsBase)) { Write-Error "VS Build Tools not found"; exit 1 }

$msvcBase    = Join-Path $vsBase "VC\Tools\MSVC"
$msvcVersion = (Get-ChildItem $msvcBase | Sort-Object Name -Descending | Select-Object -First 1).Name
$msvcBin     = Join-Path $msvcBase "$msvcVersion\bin\Hostx64\x64"
$msvcInc     = Join-Path $msvcBase "$msvcVersion\include"
$msvcLib     = Join-Path $msvcBase "$msvcVersion\lib\x64"

if (-not (Test-Path (Join-Path $msvcBin "link.exe"))) {
    Write-Error "link.exe not found in $msvcBin"; exit 1
}

# Locate Windows SDK
$wkitsBase  = "C:\Program Files (x86)\Windows Kits\10"
$sdkVersion = (Get-ChildItem (Join-Path $wkitsBase "Include") -ErrorAction SilentlyContinue |
               Sort-Object Name -Descending | Select-Object -First 1).Name
if (-not $sdkVersion) { $sdkVersion = "10.0.22621.0" }

$sdkBin = Join-Path $wkitsBase "bin\$sdkVersion\x64"
$sdkInc = Join-Path $wkitsBase "Include\$sdkVersion"
$sdkLib = Join-Path $wkitsBase "Lib\$sdkVersion"

# Set environment
$cargoBin        = Join-Path $env:USERPROFILE ".cargo\bin"
$env:PATH        = "$msvcBin;$sdkBin;$cargoBin;$env:PATH"
$ucrtInc         = Join-Path $sdkInc "ucrt"
$umInc           = Join-Path $sdkInc "um"
$sharedInc       = Join-Path $sdkInc "shared"
$env:INCLUDE     = "$msvcInc;$ucrtInc;$umInc;$sharedInc"
$ucrtLib         = Join-Path $sdkLib "ucrt\x64"
$umLib           = Join-Path $sdkLib "um\x64"
$env:LIB         = "$msvcLib;$ucrtLib;$umLib"
$env:LIBPATH     = $env:LIB

Write-Host "=== Forge IDE Rust Build ===" -ForegroundColor Cyan
Write-Host "MSVC : $msvcVersion" -ForegroundColor Gray
Write-Host "SDK  : $sdkVersion"  -ForegroundColor Gray
Write-Host "CMD  : cargo $CargoArgs" -ForegroundColor Gray

& cargo @CargoArgs
exit $LASTEXITCODE
