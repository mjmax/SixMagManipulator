$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$PythonPath = Join-Path $ProjectRoot "utilities\venv\Scripts\python.exe"

if (-not (Test-Path $PythonPath)) {
    throw "Dependencies are missing. Run .\scr\_imgproc\setup.ps1 first."
}

& $PythonPath (Join-Path $PSScriptRoot "webcam_tracker.py") @args

