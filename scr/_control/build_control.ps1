param([switch]$Benchmark)
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ControlDirectory = [IO.Path]::GetFullPath($PSScriptRoot)
$ProjectDirectory = [IO.Path]::GetFullPath((Join-Path $ControlDirectory "..\.."))
$ToolsDirectory = Join-Path $ProjectDirectory "utilities\Qt\Tools"
$CompilerDirectory = Join-Path $ToolsDirectory "mingw1310_64\bin"
$Compiler = Join-Path $CompilerDirectory "g++.exe"
$CMake = Join-Path $ToolsDirectory "CMake_64\bin\cmake.exe"
$CTest = Join-Path $ToolsDirectory "CMake_64\bin\ctest.exe"
$Ninja = Join-Path $ToolsDirectory "Ninja\ninja.exe"
$BuildDirectory = Join-Path $ControlDirectory "build"
foreach ($RequiredFile in @($Compiler, $CMake, $CTest, $Ninja)) {
    if (-not (Test-Path -LiteralPath $RequiredFile -PathType Leaf)) {
        throw "Missing build tool '$RequiredFile'. Restore utilities as described in README.md."
    }
}
$PreviousPath = $env:PATH
try {
    $env:PATH = "$CompilerDirectory;$PreviousPath"
    & $CMake --fresh -S $ControlDirectory -B $BuildDirectory -G Ninja "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DCMAKE_CXX_COMPILER=$Compiler" -DCMAKE_BUILD_TYPE=Release -DSIXMAG_CONTROL_BUILD_TESTS=ON
    if ($LASTEXITCODE -ne 0) { throw "Control model configuration failed." }
    & $CMake --build $BuildDirectory --parallel
    if ($LASTEXITCODE -ne 0) { throw "Control model compilation failed." }
    & $CTest --test-dir $BuildDirectory --output-on-failure -C Release
    if ($LASTEXITCODE -ne 0) { throw "Control model tests failed." }
    if ($Benchmark) {
        & (Join-Path $BuildDirectory "SixMagControlBenchmark.exe") (Join-Path $ControlDirectory "tests\reference_cases.csv")
        if ($LASTEXITCODE -ne 0) { throw "Control model benchmark failed." }
        & (Join-Path $BuildDirectory "SixMagControllerTests.exe") (Join-Path $ControlDirectory "tests\control_reference_cases.csv") --benchmark
        if ($LASTEXITCODE -ne 0) { throw "Control loop benchmark failed." }
    }
} finally {
    $env:PATH = $PreviousPath
}
