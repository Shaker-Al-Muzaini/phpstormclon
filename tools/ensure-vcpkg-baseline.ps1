[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$forgeRoot = Split-Path $PSScriptRoot -Parent
if (-not $env:VCPKG_ROOT) { $env:VCPKG_ROOT = $env:VCPKG_INSTALLATION_ROOT }
if (-not $env:VCPKG_ROOT -or -not (Test-Path (Join-Path $env:VCPKG_ROOT 'scripts/buildsystems/vcpkg.cmake'))) {
    throw 'Set VCPKG_ROOT to your existing vcpkg checkout before building.'
}
$forgeBaseline = (Get-Content (Join-Path $forgeRoot 'vcpkg.json') -Raw | ConvertFrom-Json).'builtin-baseline'
if ($forgeBaseline -notmatch '^[0-9a-f]{40}$') { throw 'vcpkg.json must contain a pinned registry commit.' }
$forgeBaselineObject = "${forgeBaseline}:versions/baseline.json"
$forgeObjectType = $forgeBaselineObject | & git -C $env:VCPKG_ROOT cat-file '--batch-check=%(objecttype)'
if ($LASTEXITCODE -ne 0) { throw 'Unable to inspect the existing vcpkg registry checkout.' }
if ($forgeObjectType -ne 'blob') {
    # Fetch registry history only: never checkout/reset/clean the installed toolchain.
    & git -C $env:VCPKG_ROOT fetch --no-tags https://github.com/microsoft/vcpkg.git $forgeBaseline
    if ($LASTEXITCODE -ne 0) { throw 'Unable to fetch the pinned official vcpkg registry history.' }
    & git -C $env:VCPKG_ROOT cat-file -e $forgeBaselineObject
    if ($LASTEXITCODE -ne 0) { throw 'The pinned vcpkg registry baseline is still unavailable.' }
}
Write-Host "vcpkg registry baseline available: $forgeBaseline"
