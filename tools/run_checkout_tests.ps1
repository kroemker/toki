<#
.SYNOPSIS
Runs the tests that read a real HackerOoT checkout, against a copy of it on the Windows
filesystem.

.DESCRIPTION
Over \\wsl.localhost every file open costs milliseconds, and the checkout tests open
thousands. This copies what they read out of WSL as one tar stream, points the tests at
the copy, runs them, and resets TOKI_HACKEROOT afterwards, whether they pass or not.

The copy is a snapshot: run again after rebuilding or re-exporting in WSL.

.EXAMPLE
tools\run_checkout_tests.ps1 -Copy D:\Temp\toki-checkout
#>
param(
    # The WSL distribution the checkout lives in.
    [string]$Distro = 'Ubuntu',
    # The checkout, as a path inside the distribution.
    [string]$Checkout = '~/HackerOoT',
    # Where the copy goes. It holds about 850 MB.
    [string]$Copy = (Join-Path $env:TEMP 'toki-checkout'),
    [string]$Preset = 'release',
    # Reuse the copy as it is instead of refreshing it.
    [switch]$KeepCopy
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

function Update-Copy {
    if (Test-Path $Copy) {
        Remove-Item -Recurse -Force $Copy
    }
    New-Item -ItemType Directory -Force $Copy | Out-Null
    $files = 'Makefile .make_options.mk .make_hackeroot.mk spec include src assets extracted ' +
             'build/*/assets'
    $archive = "cd $Checkout && tar -cf - --ignore-failed-read $files 2>/dev/null"
    # cmd's pipe keeps the stream binary; PowerShell's would decode it as text.
    cmd /c "wsl -d $Distro -- bash -c `"$archive`" | `"$env:SystemRoot\System32\tar.exe`" -xf - -C `"$Copy`""
    if (-not (Test-Path (Join-Path $Copy 'Makefile'))) {
        throw "nothing was copied from $Checkout in $Distro"
    }
}

Import-MsvcEnvironment
if (-not $env:VCPKG_ROOT) {
    $env:VCPKG_ROOT = [Environment]::GetEnvironmentVariable('VCPKG_ROOT', 'Machine')
}

$watch = [Diagnostics.Stopwatch]::StartNew()
if (-not $KeepCopy -or -not (Test-Path $Copy)) {
    Write-Host "Copying $Checkout out of $Distro to $Copy"
    Update-Copy
    Write-Host ('Copied in {0:N0} s' -f $watch.Elapsed.TotalSeconds)
}

Push-Location $repository
try {
    $root = (Resolve-Path $Copy).Path -replace '\\', '/'
    Invoke-Native 'configuring' { cmake --preset $Preset "-DTOKI_HACKEROOT=$root" | Out-Null }
    Invoke-Native 'building the tests' { cmake --build --preset $Preset --target toki_tests }
    $watch.Restart()
    & (Join-Path 'build' "$Preset\tests\toki_tests.exe") '[checkout]' `
        "~make runs inside the checkout's distribution" --durations yes
    $code = $LASTEXITCODE
    Write-Host ('Checkout tests ran in {0:N0} s' -f $watch.Elapsed.TotalSeconds)
} finally {
    cmake --preset $Preset '-DTOKI_HACKEROOT=' | Out-Null
    Pop-Location
}
exit $code
