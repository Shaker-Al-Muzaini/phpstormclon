[CmdletBinding()]
param(
    [string]$QtRoot,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [ValidateRange(1,32)][int]$Jobs = 2,
    [switch]$Run,
    [switch]$Fast,
    [switch]$SetupPhpTools,
    [switch]$Package
)
$ErrorActionPreference = 'Stop'
$forgeRoot = Split-Path $PSScriptRoot -Parent
Push-Location $forgeRoot
try {
    & (Join-Path $PSScriptRoot 'msvc-env.ps1')
    if (-not $QtRoot) { $QtRoot = $env:QT_ROOT_DIR }
    if (-not $QtRoot) { $QtRoot = $env:QT_ROOT }
    if (-not $QtRoot -and $env:Qt6_DIR) {
        if (Test-Path (Join-Path $env:Qt6_DIR 'bin\qmake.exe')) { $QtRoot = $env:Qt6_DIR }
        else { $QtRoot = [IO.Path]::GetFullPath((Join-Path $env:Qt6_DIR '..\..\..')) }
    }
    if (-not $QtRoot -and (Test-Path 'C:\Qt')) {
        $forgeQtCandidate = Get-ChildItem 'C:\Qt' -Directory -Filter '6.8.*' | Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName 'msvc2022_64' } | Where-Object { Test-Path (Join-Path $_ 'bin\qmake.exe') } | Select-Object -First 1
        $QtRoot = $forgeQtCandidate
    }
    if (-not $QtRoot -or -not (Test-Path (Join-Path $QtRoot 'bin\qmake.exe'))) { throw 'Pass -QtRoot with your existing Qt 6.8 MSVC x64 folder (for example C:\Qt\6.8.3\msvc2022_64).' }
    $forgePreset = 'windows-' + $Configuration.ToLowerInvariant()
    $env:PATH = (Join-Path $QtRoot 'bin') + ';' + $env:PATH
    function Invoke-ForgeStep([string]$Executable, [string[]]$Arguments) {
        & $Executable @Arguments
        if ($LASTEXITCODE -ne 0) { throw "$Executable failed with exit code $LASTEXITCODE" }
    }
    & (Join-Path $PSScriptRoot 'ensure-vcpkg-baseline.ps1')
    Invoke-ForgeStep 'cmake' @('--preset', $forgePreset, "-DCMAKE_PREFIX_PATH=$QtRoot")
    if ($Fast) {
        # Reuse the existing build and compile the app plus its required services only.
        Invoke-ForgeStep 'cmake' @('--build', '--preset', $forgePreset, '--target', 'forge_app', '--parallel', "$Jobs")
    } else {
        Invoke-ForgeStep 'cmake' @('--build', '--preset', $forgePreset, '--parallel', "$Jobs")
        Invoke-ForgeStep 'ctest' @('--preset', $forgePreset, '--output-on-failure')
        Invoke-ForgeStep 'cargo' @('test', '--all', '--locked')
    }
    $forgeBin = Join-Path $forgeRoot "build\$forgePreset\bin"
    $forgeExe = Join-Path $forgeBin 'ForgeIDE.exe'
    Invoke-ForgeStep (Join-Path $QtRoot 'bin\windeployqt.exe') @('--no-translations', $forgeExe)
    if ($Package) {
        $forgeZip = Join-Path $forgeRoot "build\ForgeIDE-$Configuration-windows-x64.zip"
        $forgePackage = Join-Path $forgeRoot "build\$forgePreset\portable"
        New-Item -ItemType Directory -Force $forgePackage | Out-Null
        Get-ChildItem $forgeBin | Where-Object { $_.Name -ne 'forge_tests.exe' -and $_.Name -ne 'forge_workspace_tests.exe' } | Copy-Item -Destination $forgePackage -Recurse -Force
        Copy-Item 'LICENSE', 'THIRD_PARTY_NOTICES.md' $forgePackage
        Copy-Item 'docs\WINDOWS.md' (Join-Path $forgePackage 'RUNNING.md')
        Compress-Archive -Path (Join-Path $forgePackage '*') -DestinationPath $forgeZip -Force
        Write-Host "Portable build: $forgeZip"
    }
    if ($SetupPhpTools) { & (Join-Path $PSScriptRoot 'setup-php-intelligence.ps1') }
    if ($Run) { Start-Process -FilePath $forgeExe -WorkingDirectory $forgeRoot }
} finally { Pop-Location }
