param(
    [string]$PythonExecutable = "C:\Python38\python.exe"
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$EnvironmentPath = Join-Path $ProjectRoot "utilities\venv"
$RequirementsPath = Join-Path $PSScriptRoot "requirements.txt"

if (-not (Test-Path -LiteralPath $PythonExecutable)) {
    throw "Python was not found at '$PythonExecutable'. Pass its full path with -PythonExecutable."
}

if (-not (Test-Path -LiteralPath $EnvironmentPath)) {
    & $PythonExecutable -m venv $EnvironmentPath
    if ($LASTEXITCODE -ne 0) {
        throw "Could not create the local Python environment."
    }
}

$PythonPath = Join-Path $EnvironmentPath "Scripts\python.exe"

& $PythonPath -m pip install --upgrade pip==24.3.1
if ($LASTEXITCODE -ne 0) {
    throw "Could not install the package manager."
}

& $PythonPath -m pip install -r $RequirementsPath
if ($LASTEXITCODE -ne 0) {
    throw "Could not install the tracker dependencies."
}

& $PythonPath -c "import cv2, numpy; print('OpenCV', cv2.__version__, '| NumPy', numpy.__version__)"
if ($LASTEXITCODE -ne 0) {
    throw "The dependency verification failed."
}

Write-Host "Dependencies are ready under utilities\venv."

