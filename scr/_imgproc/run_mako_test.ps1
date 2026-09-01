$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$PythonPath = Join-Path $ProjectRoot "utilities\vimba_venv\Scripts\python.exe"
$TransportPath = Join-Path $ProjectRoot "utilities\AlliedVision\VimbaX\cti"

if (-not (Test-Path -LiteralPath $PythonPath)) {
    throw "Mako dependencies are missing. Run .\scr\_imgproc\setup_mako.ps1 first."
}
if (-not (Test-Path -LiteralPath $TransportPath)) {
    throw "The Vimba X USB transport layer is missing under utilities\AlliedVision\VimbaX."
}

$env:GENICAM_GENTL64_PATH = $TransportPath
& $PythonPath (Join-Path $PSScriptRoot "mako_capture_test.py") @args
exit $LASTEXITCODE
