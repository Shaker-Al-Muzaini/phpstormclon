# Configure this PowerShell process only; never modify the user's system PATH.
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'Visual Studio Installer / vswhere.exe was not found.' }
$forgeVsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $forgeVsPath) { throw 'Install the Visual Studio 2022 Desktop development with C++ workload.' }
$forgeDevShell = Join-Path $forgeVsPath 'Common7\Tools\Launch-VsDevShell.ps1'
if (-not (Test-Path $forgeDevShell)) { throw "Developer PowerShell was not found: $forgeDevShell" }
& $forgeDevShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'MSVC environment initialization did not expose cl.exe.' }
if ($env:GITHUB_ENV) {
    foreach ($forgeName in @('PATH', 'INCLUDE', 'LIB', 'LIBPATH', 'VCToolsInstallDir', 'VCINSTALLDIR', 'VSINSTALLDIR', 'WindowsSdkDir', 'WindowsSDKVersion', 'VSCMD_VER', 'VSCMD_ARG_TGT_ARCH', 'VSCMD_ARG_HOST_ARCH')) {
        $forgeValue = [Environment]::GetEnvironmentVariable($forgeName, 'Process')
        if ($forgeValue) { "$forgeName=$forgeValue" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append }
    }
}
