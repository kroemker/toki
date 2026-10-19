<#
.SYNOPSIS
Builds Toki and starts it.

.DESCRIPTION
Imports the MSVC environment and VCPKG_ROOT when the shell has neither, configures the
preset the first time, builds only the app, and starts it. Arguments after the parameters
are passed on to toki.exe.

.EXAMPLE
tools\run.ps1
tools\run.ps1 -Preset release
tools\run.ps1 -NoBuild
#>
param(
    [ValidateSet('debug', 'release')]
    [string]$Preset = 'debug',
    # Start what was built last without building.
    [switch]$NoBuild,
    # Wait for Toki to close, and return its exit code.
    [switch]$Wait,
    [Parameter(ValueFromRemainingArguments)]
    [string[]]$AppArguments
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot

function Import-MsvcEnvironment {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) {
        return
    }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $installation = & $vswhere -latest -products * -property installationPath `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64
    if (-not $installation) {
        throw 'no Visual Studio with the C++ tools was found'
    }
    $vcvars = Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
    cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2])
        }
    }
}

function Invoke-Native([string]$What, [scriptblock]$Command) {
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$What failed with exit code $LASTEXITCODE"
    }
}

$binary = Join-Path $repository "build\$Preset"
$executable = Join-Path $binary 'toki.exe'

if (-not $NoBuild) {
    foreach ($tool in 'cmake', 'ninja') {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
            throw "$tool is not on PATH"
        }
    }
    Import-MsvcEnvironment
    if (-not $env:VCPKG_ROOT) {
        $env:VCPKG_ROOT = [Environment]::GetEnvironmentVariable('VCPKG_ROOT', 'Machine')
    }
    if (-not $env:VCPKG_ROOT) {
        throw 'VCPKG_ROOT is not set'
    }
    Push-Location $repository
    try {
        if (-not (Test-Path (Join-Path $binary 'build.ninja'))) {
            Invoke-Native 'configure' { cmake --preset $Preset }
        }
        Invoke-Native 'build' { cmake --build --preset $Preset --target toki }
    } finally {
        Pop-Location
    }
}

if (-not (Test-Path $executable)) {
    throw "$executable does not exist; run without -NoBuild"
}
$started = @{ FilePath = $executable; WorkingDirectory = $binary; PassThru = $true }
if ($AppArguments) {
    $started.ArgumentList = $AppArguments
}
$process = Start-Process @started
if ($Wait) {
    $process.WaitForExit()
    exit $process.ExitCode
}
