Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$MotorDirectory = [System.IO.Path]::GetFullPath($PSScriptRoot)
$ProjectRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $MotorDirectory "..\..")
)
$GuiDirectory = Join-Path $ProjectRoot "scr\_gui"
$BuildDirectory = Join-Path $GuiDirectory "build"
$Executable = Join-Path $BuildDirectory "SixMagMotorEmulator.exe"

$NeedsBuild = -not (Test-Path -LiteralPath $Executable -PathType Leaf)
if (-not $NeedsBuild) {
    $ExecutableTime = (Get-Item -LiteralPath $Executable).LastWriteTimeUtc
    $BuildInputs = @(
        (Join-Path $GuiDirectory "CMakeLists.txt")
    )
    $BuildInputs += Get-ChildItem -LiteralPath $MotorDirectory -File -Recurse |
        Where-Object { $_.Extension -in ".cpp", ".h" } |
        Select-Object -ExpandProperty FullName
    $NeedsBuild = $null -ne ($BuildInputs | Where-Object {
        (Get-Item -LiteralPath $_).LastWriteTimeUtc -gt $ExecutableTime
    } | Select-Object -First 1)
}

if ($NeedsBuild) {
    & (Join-Path $GuiDirectory "build_gui.ps1")
}

if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "The motor emulator executable was not created at '$Executable'."
}

$Process = Start-Process `
    -FilePath $Executable `
    -WorkingDirectory $BuildDirectory `
    -WindowStyle Hidden `
    -PassThru

Write-Host "Motor emulator started (process ID $($Process.Id))." -ForegroundColor Green

