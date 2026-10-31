# Build and launch the playable game on Windows (the run-macos.sh counterpart).
#   .\run-windows.ps1 [-Debug] [ASSETS_DIR] [rr-game args...]
#   run-windows.bat   [-Debug] [ASSETS_DIR] [rr-game args...]
# Default is an optimized Release build; -Debug builds the unoptimized Debug
# configuration (slow: the CPU renderer runs ~7x slower). Both live in
# build-windows-msvc\ (x64) or build-windows-msvc-arm64\ (ARM64), the same
# trees as the CMake presets.
# Needs Visual Studio 2022 (or its Build Tools) with "Desktop development with
# C++", Git and Python 3. The MSVC environment is entered automatically when
# cl.exe is not already on PATH; vcpkg (for OpenSSL) comes from
# VCPKG_INSTALLATION_ROOT, VCPKG_ROOT, Visual Studio's bundled copy, or is
# cloned into .tools\vcpkg on first run.
$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
$Type = 'Release'
$GameArgs = [System.Collections.Generic.List[string]]::new()
foreach ($a in $args) { $GameArgs.Add([string]$a) }
if ($GameArgs.Count -gt 0 -and $GameArgs[0] -in @('-Debug', '--debug')) {
    $Type = 'Debug'
    $GameArgs.RemoveAt(0)
}

function Fail([string]$Message) {
    Write-Host "run-windows.ps1: $Message" -ForegroundColor Red
    exit 1
}

$Arch = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'arm64' } else { 'x64' }
$Preset = if ($Arch -eq 'arm64') { 'windows-msvc-arm64' } else { 'windows-msvc' }
$Build = Join-Path $Root "build-$Preset"

foreach ($tool in 'git', 'python') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { Fail "$tool not found on PATH." }
}

# MSVC developer environment (cl, link, ninja, cmake from Visual Studio).
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { Fail 'Visual Studio 2022 or its Build Tools are not installed (https://visualstudio.microsoft.com/downloads/).' }
    $component = if ($Arch -eq 'arm64') { 'Microsoft.VisualStudio.Component.VC.Tools.ARM64' } else { 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' }
    $vs = & $vswhere -latest -products * -requires $component -property installationPath
    if (-not $vs) { Fail "no Visual Studio install has the C++ $Arch tools ('Desktop development with C++')." }
    $devShell = Join-Path $vs 'Common7\Tools\Launch-VsDevShell.ps1'
    $vsArch = if ($Arch -eq 'arm64') { 'arm64' } else { 'amd64' }
    & $devShell -Arch $vsArch -HostArch $vsArch -SkipAutomaticLocation | Out-Null
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { Fail 'cl.exe is still missing after entering the Visual Studio developer shell.' }
}
foreach ($tool in 'cmake', 'ninja') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { Fail "$tool not found; install the 'C++ CMake tools for Windows' Visual Studio component." }
}

# vcpkg: the presets read VCPKG_INSTALLATION_ROOT.
$vcpkg = @($env:VCPKG_INSTALLATION_ROOT, $env:VCPKG_ROOT, (Join-Path $Root '.tools\vcpkg')) |
    Where-Object { $_ -and (Test-Path (Join-Path $_ 'scripts\buildsystems\vcpkg.cmake')) } |
    Select-Object -First 1
if (-not $vcpkg) {
    $vcpkg = Join-Path $Root '.tools\vcpkg'
    Write-Host "Cloning vcpkg into $vcpkg (first run only)"
    git clone --quiet https://github.com/microsoft/vcpkg.git $vcpkg
    if ($LASTEXITCODE -ne 0) { Fail 'cloning vcpkg failed.' }
}
if (-not (Test-Path (Join-Path $vcpkg 'vcpkg.exe'))) {
    & (Join-Path $vcpkg 'bootstrap-vcpkg.bat') -disableMetrics
    if ($LASTEXITCODE -ne 0) { Fail 'bootstrapping vcpkg failed.' }
}
$env:VCPKG_INSTALLATION_ROOT = $vcpkg
# vcpkg refuses to run when a configured binary cache folder is missing.
if ($env:VCPKG_DEFAULT_BINARY_CACHE) { New-Item -ItemType Directory -Force -Path $env:VCPKG_DEFAULT_BINARY_CACHE | Out-Null }

Push-Location $Root
try {
    # Always configure: quick when nothing changed, and it retries after a
    # failed first configure (which leaves a CMakeCache.txt behind).
    cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) {
        $log = Join-Path $Build 'vcpkg-manifest-install.log'
        if (Test-Path $log) { Get-Content $log -Tail 40 }
        Fail 'CMake configure failed.'
    }
    cmake --build $Build --config $Type --parallel --target rr-game
    if ($LASTEXITCODE -ne 0) { Fail 'build failed.' }
} finally {
    Pop-Location
}

$Exe = Join-Path $Build "$Type\rr-game.exe"
$Assets = Join-Path $Root '..\cdrom\ROADRASH'
if ($GameArgs.Count -gt 0 -and -not $GameArgs[0].StartsWith('-')) {
    $Assets = $GameArgs[0]
    $GameArgs.RemoveAt(0)
}
if (Test-Path (Join-Path $Assets 'ROADRASH.EXE')) {
    $GameArgs.Insert(0, (Resolve-Path $Assets).Path)
    $GameArgs.Insert(0, '--assets')
}
# Otherwise: the first-run import flow (or the disc imported earlier).
# rr-game.exe is a GUI program, so wait for it explicitly; --console attaches
# its output to this terminal.
$quoted = $GameArgs | ForEach-Object { if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ } }
$start = @{ FilePath = $Exe; NoNewWindow = $true; Wait = $true; PassThru = $true }
if ($quoted) { $start.ArgumentList = $quoted }
$game = Start-Process @start
exit $game.ExitCode
