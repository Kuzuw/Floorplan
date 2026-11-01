<#
.SYNOPSIS
    Copies a local build of Floorplan.vst3 into the Windows VST3 folder.

.DESCRIPTION
    Installs build\Floorplan_artefacts\<Config>\VST3\Floorplan.vst3 to
    C:\Program Files\Common Files\VST3, the folder FL Studio, Ableton Live and
    other VST3 hosts scan by default. That folder needs administrator rights, so
    run this from an elevated PowerShell. An existing Floorplan.vst3 is replaced;
    close every host that has Floorplan loaded first.

.EXAMPLE
    # From the repository root, in an administrator PowerShell:
    powershell -ExecutionPolicy Bypass -File scripts\install-vst3.ps1

.EXAMPLE
    # Copy somewhere else instead (no admin rights needed), e.g. a custom VST3 folder:
    powershell -ExecutionPolicy Bypass -File scripts\install-vst3.ps1 -Destination D:\Plugins\VST3
#>
param(
    [string] $BuildDir = (Join-Path $PSScriptRoot "..\build"),
    [string] $Config = "Release",
    [string] $Destination = ""
)

$ErrorActionPreference = "Stop"

if ($Destination -eq "") {
    # The 64-bit Common Files folder, even from a 32-bit PowerShell.
    $common = if ($env:CommonProgramW6432) { $env:CommonProgramW6432 } else { $env:CommonProgramFiles }
    $Destination = Join-Path $common "VST3"
}

$source = Join-Path $BuildDir "Floorplan_artefacts\$Config\VST3\Floorplan.vst3"
if (-not (Test-Path $source)) {
    Write-Error "No VST3 build at $source. Build first: cmake --build build --config $Config"
}

$target = Join-Path $Destination "Floorplan.vst3"
try {
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    if (Test-Path $target) {
        Remove-Item -Recurse -Force $target
    }
    Copy-Item -Recurse $source $target
}
catch [System.UnauthorizedAccessException] {
    Write-Error "Access denied to $Destination. Run this script from an administrator PowerShell, or pass -Destination."
}
catch {
    Write-Error "Could not install to ${target}: $($_.Exception.Message). If a host has Floorplan loaded, close it and try again."
}

$binary = Join-Path $target "Contents\x86_64-win\Floorplan.vst3"
if (-not (Test-Path $binary)) {
    Write-Error "The copy at $target is incomplete: $binary is missing."
}

Write-Host "Installed $target"
Write-Host "Now rescan plugins in your host (see docs/HOSTS.md)."
