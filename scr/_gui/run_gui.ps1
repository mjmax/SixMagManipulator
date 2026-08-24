$ErrorActionPreference = "Stop"

$Executable = Join-Path $PSScriptRoot "build\SixMagManipulatorGui.exe"
if (-not (Test-Path -LiteralPath $Executable)) {
    throw "The GUI has not been built. Run .\scr\_gui\build_gui.ps1 first."
}

& $Executable

