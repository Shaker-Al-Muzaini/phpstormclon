[CmdletBinding()]
param([string]$InstallDirectory = (Join-Path $env:LOCALAPPDATA 'ForgeIDE\Forge IDE\language-servers'))
$ErrorActionPreference = 'Stop'
$forgeNode = Get-Command node -ErrorAction Stop
$forgeNpm = Get-Command npm -ErrorAction Stop
$forgeNodeVersion = & $forgeNode.Source --version
if ($LASTEXITCODE -ne 0 -or [int](($forgeNodeVersion -replace '^v','').Split('.')[0]) -lt 20) {
    throw 'PHP intelligence requires Node.js 20 or newer. Your existing installation was not changed.'
}
Write-Host 'Installing Intelephense 1.18.5 locally (separate proprietary server licence; free completion/definitions/diagnostics/formatting).'
& $forgeNpm.Source install --prefix $InstallDirectory --ignore-scripts --save-exact --no-audit --no-fund intelephense@1.18.5
if ($LASTEXITCODE -ne 0) { throw 'Local PHP server installation failed.' }
Write-Host "Server installed: $InstallDirectory\node_modules\intelephense\lib\intelephense.js"
Write-Host 'Open a PHP project in Forge IDE. Tools > PHP Intelligence allows an existing Node/server path instead.'
