$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$QtRoot = Join-Path $ProjectRoot "utilities\Qt"
$QtPrefix = Join-Path $QtRoot "6.8.3\mingw_64"
$CompilerBin = Join-Path $QtRoot "Tools\mingw1310_64\bin"
$CMake = Join-Path $QtRoot "Tools\CMake_64\bin\cmake.exe"
$Ninja = Join-Path $QtRoot "Tools\Ninja\ninja.exe"
$BuildDirectory = Join-Path $PSScriptRoot "build"

$RequiredPaths = @(
    $CMake,
    $Ninja,
    (Join-Path $CompilerBin "gcc.exe"),
    (Join-Path $CompilerBin "g++.exe"),
    (Join-Path $QtPrefix "bin\windeployqt.exe")
)

foreach ($RequiredPath in $RequiredPaths) {
    if (-not (Test-Path -LiteralPath $RequiredPath)) {
        throw "Required Qt build component is missing: $RequiredPath"
    }
}

& $CMake `
    -S $PSScriptRoot `
    -B $BuildDirectory `
    -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_MAKE_PROGRAM=$Ninja" `
    "-DCMAKE_PREFIX_PATH=$QtPrefix" `
    "-DCMAKE_C_COMPILER=$(Join-Path $CompilerBin 'gcc.exe')" `
    "-DCMAKE_CXX_COMPILER=$(Join-Path $CompilerBin 'g++.exe')"
if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed."
}

& $CMake --build $BuildDirectory
if ($LASTEXITCODE -ne 0) {
    throw "GUI compilation failed."
}

$Executable = Join-Path $BuildDirectory "SixMagManipulatorGui.exe"
& (Join-Path $QtPrefix "bin\windeployqt.exe") `
    --release `
    --no-translations `
    --compiler-runtime `
    $Executable
if ($LASTEXITCODE -ne 0) {
    throw "Qt runtime deployment failed."
}

Write-Host "GUI build ready: $Executable"

