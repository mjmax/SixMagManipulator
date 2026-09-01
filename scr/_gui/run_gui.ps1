param(
    [switch]$Rebuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$GuiDirectory = [System.IO.Path]::GetFullPath($PSScriptRoot)
$BuildDirectory = Join-Path $GuiDirectory "build"
$Executable = Join-Path $BuildDirectory "SixMagManipulatorGui.exe"
$BuildScript = Join-Path $GuiDirectory "build_gui.ps1"
$ProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $GuiDirectory "..\.."))
$VimbaTransportPath = Join-Path $ProjectRoot "utilities\AlliedVision\VimbaX\cti"

$NeedsBuild = $Rebuild -or -not (Test-Path -LiteralPath $Executable -PathType Leaf)

if (-not $NeedsBuild) {
    $ExecutableTime = (Get-Item -LiteralPath $Executable).LastWriteTimeUtc
    $BuildInputs = @(
        (Join-Path $GuiDirectory "CMakeLists.txt")
    )
    $BuildInputs += Get-ChildItem -LiteralPath (Join-Path $GuiDirectory "src") -File -Recurse |
        Select-Object -ExpandProperty FullName
    $MotorDirectory = [System.IO.Path]::GetFullPath(
        (Join-Path $GuiDirectory "..\_motors")
    )
    $BuildInputs += Get-ChildItem -LiteralPath $MotorDirectory -File -Recurse |
        Where-Object { $_.Extension -in ".cpp", ".h" } |
        Select-Object -ExpandProperty FullName
    $ImageProcessingDirectory = [System.IO.Path]::GetFullPath(
        (Join-Path $GuiDirectory "..\_imgproc")
    )
    $BuildInputs += Get-ChildItem -LiteralPath $ImageProcessingDirectory -File -Recurse |
        Where-Object { $_.Extension -in ".cpp", ".h" } |
        Select-Object -ExpandProperty FullName

    $NeedsBuild = $null -ne ($BuildInputs | Where-Object {
        (Get-Item -LiteralPath $_).LastWriteTimeUtc -gt $ExecutableTime
    } | Select-Object -First 1)
}

if ($NeedsBuild) {
    Write-Host "The GUI is missing or out of date. Building it now..."
    & $BuildScript
}

if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "The GUI executable was not created at '$Executable'."
}
if (-not (Test-Path -LiteralPath $VimbaTransportPath -PathType Container)) {
    throw "The Vimba X transport layer is missing at '$VimbaTransportPath'. See README.md."
}
$env:GENICAM_GENTL64_PATH = $VimbaTransportPath

# Use the build folder as the working directory so Qt can always locate its
# deployed plugins and multimedia libraries. Start-Process returns the prompt
# immediately while keeping the GUI running in its own process.
$Process = Start-Process `
    -FilePath $Executable `
    -WorkingDirectory $BuildDirectory `
    -PassThru

Write-Host "GUI started (process ID $($Process.Id))." -ForegroundColor Green
