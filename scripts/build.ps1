<#
.SYNOPSIS
    One-command build: configure, compile, validate, test, and optionally install.

.DESCRIPTION
    Uses the presets in CMakePresets.json. Steinberg's VST3 validator runs as a
    post-build step, so a successful build means the plug-in passed validation.

.PARAMETER Config
    Debug or Release (default Release).

.PARAMETER Preset
    Configure preset to use (default windows-x64; CI uses 'ci').

.PARAMETER Clean
    Delete the preset's build directory first.

.PARAMETER SkipTests
    Don't run the DSP unit tests.

.PARAMETER Install
    Copy the built .vst3 bundle into -InstallDir after building.

.PARAMETER InstallDir
    Destination for -Install. Defaults to the per-user VST3 folder that hosts scan
    (%LOCALAPPDATA%\Programs\Common\VST3), which needs no admin rights.

.EXAMPLE
    .\scripts\build.ps1
    .\scripts\build.ps1 -Config Debug -Install
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Release',
    [string]$Preset = 'windows-x64',
    [switch]$Clean,
    [switch]$SkipTests,
    [switch]$Install,
    [string]$InstallDir = (Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3')
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$PluginName = 'FeedbackFromWhenceYouCame'

function Invoke-Native {
    param([Parameter(Mandatory)][string]$Exe, [string[]]$Arguments)
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "'$Exe $($Arguments -join ' ')' failed with exit code $LASTEXITCODE" }
}

# Build/test preset names: '<config>' for the default preset, '<preset>-<config>' otherwise.
$stepPreset = if ($Preset -eq 'windows-x64') { $Config.ToLower() } else { "$Preset-$($Config.ToLower())" }
$buildDir = Join-Path $RepoRoot "build\$Preset"

if (-not (Test-Path (Join-Path $RepoRoot 'external\vst3sdk\pluginterfaces\base\funknown.h'))) {
    throw "VST3 SDK submodule is missing. Run .\scripts\setup.ps1 first."
}

Push-Location $RepoRoot
try {
    if ($Clean -and (Test-Path $buildDir)) {
        Write-Host "Removing $buildDir" -ForegroundColor Cyan
        Remove-Item -Recurse -Force $buildDir
    }

    Write-Host "`n== Configure ($Preset) ==" -ForegroundColor Cyan
    Invoke-Native cmake @('--preset', $Preset)

    Write-Host "`n== Build + validate ($Config) ==" -ForegroundColor Cyan
    Invoke-Native cmake @('--build', '--preset', $stepPreset, '--parallel')

    if (-not $SkipTests) {
        Write-Host "`n== Test ($Config) ==" -ForegroundColor Cyan
        Invoke-Native ctest @('--preset', $stepPreset)
    }

    $bundle = Join-Path $buildDir "VST3\$Config\$PluginName.vst3"
    if (-not (Test-Path $bundle)) { throw "Expected plug-in bundle not found: $bundle" }
    Write-Host "`nBuilt: $bundle" -ForegroundColor Green

    if ($Install) {
        $dest = Join-Path $InstallDir "$PluginName.vst3"
        New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
        if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
        Copy-Item -Recurse $bundle $dest
        Write-Host "Installed: $dest" -ForegroundColor Green
    }
}
finally {
    Pop-Location
}
