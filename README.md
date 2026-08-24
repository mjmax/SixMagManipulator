# Six-Magnet Manipulator Software

Desktop software for a six-magnet planar manipulator. The current project
contains a low-latency webcam sphere tracker and a Qt 6 operator-interface
prototype.

## Project layout

- `scr/_imgproc`: Python/OpenCV detection, webcam preview, and tests.
- `scr/_gui`: C++/Qt 6 GUI, build scripts, and GUI documentation.
- `utilities`: locally installed runtimes and dependencies. This directory is
  intentionally excluded from Git and must be created on each development PC.

## Set up the utilities folder

Run the following commands from the repository root in PowerShell.

### Image-processing environment

Install 64-bit Python 3.8 or later, then run:

```powershell
.\scr\_imgproc\setup.ps1
```

The setup script creates `utilities/venv` and installs the versions listed in
`scr/_imgproc/requirements.txt`. If Python is installed somewhere other than
`C:\Python38\python.exe`, provide its full path:

```powershell
.\scr\_imgproc\setup.ps1 -PythonExecutable "C:\Path\To\python.exe"
```

### Qt GUI toolchain

Install 64-bit Python 3.10 or later and use `aqtinstall` to recreate the local
Qt 6.8.3 toolchain:

```powershell
python -m venv .\utilities\qt_tools_venv
.\utilities\qt_tools_venv\Scripts\python.exe -m pip install --upgrade pip
.\utilities\qt_tools_venv\Scripts\python.exe -m pip install aqtinstall

$Aqt = ".\utilities\qt_tools_venv\Scripts\aqt.exe"
& $Aqt install-qt windows desktop 6.8.3 win64_mingw --modules qtmultimedia --outputdir .\utilities\Qt --internal
& $Aqt install-tool windows desktop tools_mingw1310 qt.tools.win64_mingw1310 --outputdir .\utilities\Qt --internal
& $Aqt install-tool windows desktop tools_cmake qt.tools.cmake --outputdir .\utilities\Qt --internal
& $Aqt install-tool windows desktop tools_ninja qt.tools.ninja --outputdir .\utilities\Qt --internal
```

The Qt downloads are large and can take several minutes. Once installation is
complete, build the GUI with:

```powershell
.\scr\_gui\build_gui.ps1
```

## Run the webcam tracker

```powershell
.\scr\_imgproc\run_tracker.ps1
```

Press **Q** in the preview to stop. Useful tuning options include:

```powershell
.\scr\_imgproc\run_tracker.ps1 --threshold 90 --min-area 30 --max-area 20000
```

- Raise `--threshold` if parts of the dark sphere are missing.
- Lower it if shadows or gray regions are detected.
- Use `--roi x,y,width,height` to restrict detection to the workspace.
- Use `--no-display --csv` for low-latency machine-readable output.
- Add `--mm-per-pixel` later when physical calibration is available.

Run the hardware-free detector tests with:

```powershell
.\utilities\venv\Scripts\python.exe .\scr\_imgproc\test_tracker.py
```

## Run the Qt GUI

After building, start the interface with:

```powershell
.\scr\_gui\run_gui.ps1
```

The GUI uses the default webcam when available. It currently visualizes the
circular workspace, six magnet orientations, configurable servo angle limits,
and saturation warnings. See `scr/_gui/README.md` for implementation details.

