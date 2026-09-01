Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# Resolve everything relative to this script so it works from any PowerShell
# directory and after the repository is moved to another drive or folder.
$GuiDirectory = [System.IO.Path]::GetFullPath($PSScriptRoot)
$ProjectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $GuiDirectory "..\..")
)
$QtRoot = Join-Path $ProjectRoot "utilities\Qt"
$QtPrefix = Join-Path $QtRoot "6.8.3\mingw_64"
$CompilerBin = Join-Path $QtRoot "Tools\mingw1310_64\bin"
$CMake = Join-Path $QtRoot "Tools\CMake_64\bin\cmake.exe"
$Ninja = Join-Path $QtRoot "Tools\Ninja\ninja.exe"
$CxxCompiler = Join-Path $CompilerBin "g++.exe"
$DeployQt = Join-Path $QtPrefix "bin\windeployqt.exe"
$QtConfig = Join-Path $QtPrefix "lib\cmake\Qt6\Qt6Config.cmake"
$QtSerialPortConfig = Join-Path $QtPrefix "lib\cmake\Qt6SerialPort\Qt6SerialPortConfig.cmake"
$VimbaRoot = Join-Path $ProjectRoot "utilities\AlliedVision\VimbaX"
$VimbaHeader = Join-Path $VimbaRoot "api\include\VmbC\VmbC.h"
$VimbaLibrary = Join-Path $VimbaRoot "api\lib\VmbC.lib"
$VimbaRuntime = Join-Path $VimbaRoot "api\bin\VmbC.dll"
$VimbaUsbTransport = Join-Path $VimbaRoot "cti\VimbaUSBTL.cti"
$BuildDirectory = Join-Path $GuiDirectory "build"
$Executable = Join-Path $BuildDirectory "SixMagManipulatorGui.exe"

$RequiredPaths = [ordered]@{
    "CMake" = $CMake
    "Ninja" = $Ninja
    "MinGW C++ compiler" = $CxxCompiler
    "Qt deployment tool" = $DeployQt
    "Qt 6 package configuration" = $QtConfig
    "Qt SerialPort module" = $QtSerialPortConfig
    "Vimba X C API header" = $VimbaHeader
    "Vimba X C API library" = $VimbaLibrary
    "Vimba X C API runtime" = $VimbaRuntime
    "Vimba X USB transport" = $VimbaUsbTransport
}

foreach ($Component in $RequiredPaths.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $Component.Value -PathType Leaf)) {
        throw "$($Component.Key) is missing at '$($Component.Value)'. Recreate the utilities folder using the instructions in README.md."
    }
}

# Windows locks a running executable. The process name is unique to this GUI,
# so close existing instances even if the same folder is reached through a
# different drive alias (for example W: instead of its physical D: path).
$RunningInstances = @(
    Get-Process -Name "SixMagManipulatorGui", "SixMagMotorEmulator" -ErrorAction SilentlyContinue
)
if ($RunningInstances.Count -gt 0) {
    Write-Host "Closing the running GUI before rebuilding..."
    $RunningInstances | Stop-Process -Force
    $RunningInstances | Wait-Process -Timeout 10 -ErrorAction SilentlyContinue
}

New-Item -ItemType Directory -Path $BuildDirectory -Force | Out-Null

# --fresh discards only CMake's generated cache. This prevents cached absolute
# paths from breaking the build when the repository moves (for example W: to D:).
$ConfigureArguments = @(
    "--fresh"
    "-S", $GuiDirectory
    "-B", $BuildDirectory
    "-G", "Ninja"
    "-DCMAKE_BUILD_TYPE=Release"
    "-DCMAKE_MAKE_PROGRAM=$Ninja"
    "-DCMAKE_PREFIX_PATH=$QtPrefix"
    "-DQt6_DIR=$(Split-Path -Parent $QtConfig)"
    "-DCMAKE_CXX_COMPILER=$CxxCompiler"
    "-DVIMBA_X_ROOT=$VimbaRoot"
)

Write-Host "Configuring the GUI..."
& $CMake @ConfigureArguments
if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed with exit code $LASTEXITCODE."
}

Write-Host "Building the GUI..."
& $CMake --build $BuildDirectory --parallel
if ($LASTEXITCODE -ne 0) {
    throw "GUI compilation failed with exit code $LASTEXITCODE."
}

if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "The build completed without producing '$Executable'."
}

Write-Host "Preparing the Qt runtime..."
& $DeployQt `
    --release `
    --no-translations `
    --compiler-runtime `
    $Executable
if ($LASTEXITCODE -ne 0) {
    throw "Qt runtime deployment failed with exit code $LASTEXITCODE."
}

Write-Host "GUI build ready: $Executable" -ForegroundColor Green
