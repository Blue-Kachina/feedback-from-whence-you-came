<#
.SYNOPSIS
    Installs the developer toolchain and fetches the pinned VST3 SDK.

.DESCRIPTION
    Idempotent: anything already present is left alone, so it is safe to re-run
    and is also what CI runs.

      - Git and CMake (via winget, only if missing or too old)
      - Visual Studio 2022 C++ build tools + Windows SDK, as listed in .vsconfig
        (installs Build Tools if no VS 2022 exists; otherwise adds missing components)
      - The VST3 SDK submodule and the nested SDK modules the build needs

    Works in Windows PowerShell 5.1 and PowerShell 7+.

.PARAMETER SkipToolchain
    Only fetch submodules; don't check or install tools.
#>
[CmdletBinding()]
param(
    [switch]$SkipToolchain
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$VsConfig = Join-Path $RepoRoot '.vsconfig'
$MinCMake = [version]'3.25'
$VsRange  = '[17.0,18.0)'   # Visual Studio 2022, matches the generator in CMakePresets.json

# Nested modules of external/vst3sdk the build uses (skips the large doc/tutorials modules).
$SdkModules = @('base', 'cmake', 'pluginterfaces', 'public.sdk', 'vstgui4')

function Invoke-Native {
    param([Parameter(Mandatory)][string]$Exe, [string[]]$Arguments)
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "'$Exe $($Arguments -join ' ')' failed with exit code $LASTEXITCODE" }
}

function Update-SessionPath {
    $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user    = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$machine;$user"
}

function Install-WingetPackage {
    param([Parameter(Mandatory)][string]$Id, [string[]]$Extra = @())
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        throw "winget is required to install $Id. Install 'App Installer' from the Microsoft Store, or install $Id manually."
    }
    Write-Host "Installing $Id via winget..." -ForegroundColor Cyan
    Invoke-Native winget (@('install', '--id', $Id, '--exact', '--source', 'winget',
        '--accept-package-agreements', '--accept-source-agreements', '--disable-interactivity') + $Extra)
    Update-SessionPath
}

function Get-VsWhere {
    $path = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $path) { return $path }
    return $null
}

function Get-VsComponents {
    (Get-Content $VsConfig -Raw | ConvertFrom-Json).components
}

function Install-Git {
    if (Get-Command git -ErrorAction SilentlyContinue) {
        Write-Host "git: $((git --version) -replace 'git version ', '')" -ForegroundColor Green
        return
    }
    Install-WingetPackage 'Git.Git'
}

function Install-CMake {
    $cmake = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmake) {
        $ver = [version](((cmake --version | Select-Object -First 1) -split ' ')[2] -replace '-.*$', '')
        if ($ver -ge $MinCMake) {
            Write-Host "cmake: $ver" -ForegroundColor Green
            return
        }
        Write-Host "cmake $ver is older than $MinCMake; upgrading." -ForegroundColor Yellow
    }
    Install-WingetPackage 'Kitware.CMake'
}

function Install-VisualStudio {
    $components = Get-VsComponents
    $vswhere = Get-VsWhere

    if ($vswhere) {
        $ready = & $vswhere -products * -version $VsRange -requires $components -property installationPath |
            Select-Object -First 1
        if ($ready) {
            Write-Host "Visual Studio 2022 with required components: $ready" -ForegroundColor Green
            return
        }

        $existing = & $vswhere -products * -version $VsRange -property installationPath | Select-Object -First 1
        if ($existing) {
            Write-Host "Adding missing components (from .vsconfig) to $existing ..." -ForegroundColor Cyan
            Write-Host "(The Visual Studio Installer may request administrator rights.)"
            $installer = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\setup.exe'
            $proc = Start-Process -FilePath $installer -Wait -PassThru -Verb RunAs -ArgumentList @(
                'modify', '--installPath', "`"$existing`"", '--config', "`"$VsConfig`"",
                '--passive', '--norestart')
            # 3010 = success, reboot required
            if ($proc.ExitCode -notin 0, 3010) { throw "Visual Studio Installer failed with exit code $($proc.ExitCode)" }
            return
        }
    }

    Write-Host "Installing Visual Studio 2022 Build Tools (components from .vsconfig)..." -ForegroundColor Cyan
    Install-WingetPackage 'Microsoft.VisualStudio.2022.BuildTools' @(
        '--override', "--wait --passive --norestart --config `"$VsConfig`"")
}

function Initialize-Submodules {
    Write-Host "Fetching pinned VST3 SDK..." -ForegroundColor Cyan
    Push-Location $RepoRoot
    try {
        Invoke-Native git @('submodule', 'update', '--init', 'external/vst3sdk')
        Invoke-Native git (@('-C', 'external/vst3sdk', 'submodule', 'update', '--init', '--') + $SdkModules)
        $sdkVersion = git -C external/vst3sdk describe --tags 2>$null
        Write-Host "VST3 SDK: $sdkVersion" -ForegroundColor Green
    }
    finally {
        Pop-Location
    }
}

if (-not $SkipToolchain) {
    Install-Git
    Install-CMake
    Install-VisualStudio
}
Initialize-Submodules

Write-Host "`nSetup complete. Build with: .\scripts\build.ps1" -ForegroundColor Green
