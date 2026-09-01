param(
    [string]$PythonExecutable = "C:\Users\User\AppData\Local\Programs\Python\Python310\python.exe"
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$EnvironmentPath = Join-Path $ProjectRoot "utilities\vimba_venv"
$RequirementsPath = Join-Path $PSScriptRoot "requirements_mako.txt"

if (-not (Test-Path -LiteralPath $PythonExecutable)) {
    throw "64-bit Python 3.10 or newer was not found at '$PythonExecutable'. Pass its full path with -PythonExecutable."
}

$PythonInfo = & $PythonExecutable -c 'import struct,sys; print(f"{sys.version_info.major}.{sys.version_info.minor},{struct.calcsize(''P'') * 8}")'
if ($LASTEXITCODE -ne 0) {
    throw "Could not inspect the selected Python runtime."
}
$Version, $Bits = $PythonInfo.Trim().Split(',')
if ([int]$Bits -ne 64 -or [version]$Version -lt [version]"3.10") {
    throw "VmbPy requires 64-bit Python 3.10 or newer. Selected runtime: Python $Version ($Bits-bit)."
}

if (-not (Test-Path -LiteralPath $EnvironmentPath)) {
    & $PythonExecutable -m venv $EnvironmentPath
    if ($LASTEXITCODE -ne 0) {
        throw "Could not create utilities\vimba_venv."
    }
}

$PythonPath = Join-Path $EnvironmentPath "Scripts\python.exe"
& $PythonPath -m pip install --upgrade pip
if ($LASTEXITCODE -ne 0) {
    throw "Could not update pip in the Mako environment."
}
& $PythonPath -m pip install -r $RequirementsPath
if ($LASTEXITCODE -ne 0) {
    throw "Could not install the Mako camera dependencies."
}

& $PythonPath -c "import cv2,numpy,vmbpy; print('VmbPy', vmbpy.__version__, '| OpenCV', cv2.__version__, '| NumPy', numpy.__version__)"
if ($LASTEXITCODE -ne 0) {
    throw "The Mako Python dependency verification failed."
}

Write-Host "Mako Python dependencies are ready under utilities\vimba_venv."
Write-Host "The Allied Vision USB transport layer and Windows driver must also be installed from Vimba X."
