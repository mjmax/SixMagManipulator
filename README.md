# Six-Magnet Manipulator Software

Desktop software for a six-magnet planar manipulator. The current project
contains low-latency webcam and Allied Vision camera acquisition tools, a
sphere tracker, and a Qt 6 operator-interface prototype.

## Project layout

- `scr/_imgproc`: Python/OpenCV detection, webcam preview, and tests.
- `scr/_gui`: C++/Qt 6 GUI, build scripts, and GUI documentation.
- `scr/_motors`: Protocol 1.0 motor communication and hardware emulator.
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
& $Aqt install-qt windows desktop 6.8.3 win64_mingw --modules qtmultimedia qtserialport --outputdir .\utilities\Qt --internal
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

## Configure the Allied Vision Mako U-130B

The Mako U-130B is a USB3 Vision camera. At full 1280 x 1024 resolution with
`Mono8`, its specified maximum rate is 168 FPS. Windows requires Allied
Vision's USB camera driver and USB GenTL transport layer; the Python package
alone cannot communicate with the camera.

1. Connect the camera directly to a USB 3.x port.
2. Download the current 64-bit **Vimba X for Windows** installer from Allied
   Vision's official Vimba X download page. Allied Vision requires the user to
   accept its download terms.
3. Run the installer and select the smallest installation containing the
   **USB Transport Layer** and **Allied Vision USB camera driver**. The Viewer,
   examples, GigE, CSI, and other camera transports are not required by this
   project. Keep the camera connected during installation so the driver can be
   assigned automatically. A Windows driver is necessarily system-installed;
   the project-local Python API remains under `utilities`.
4. If the automatic driver assignment does not succeed, use the Vimba X Driver
   Installer or Windows Device Manager to assign the Allied Vision driver to
   **USB3 Vision Device**, not to **USB Composite Device**.
5. Reconnect the camera after installation. Reboot Windows if the camera is
   still not discovered.

Create the minimal, isolated Python acquisition environment:

```powershell
.\scr\_imgproc\setup_mako.ps1
```

This creates `utilities/vimba_venv` with 64-bit VmbPy, NumPy, and OpenCV. If a
different 64-bit Python 3.10+ installation is preferred, pass its full path:

```powershell
.\scr\_imgproc\setup_mako.ps1 -PythonExecutable "C:\Path\To\python.exe"
```

Start asynchronous full-resolution acquisition with a 30 FPS preview:

```powershell
.\scr\_imgproc\run_mako_test.ps1
```

The preview retains only the newest image and does not limit or queue the
high-rate acquisition stream. Press **Q** to stop. For an acquisition-only
measurement without display copies, run:

```powershell
.\scr\_imgproc\run_mako_test.ps1 --duration 10 --no-display
```

The test requests 168 FPS, `Mono8`, continuous free-running acquisition, a
3,000 microsecond exposure, and 32 announced stream buffers. Use
`--exposure-us`, `--fps`, `--preview-fps`, or `--buffers` to test other values.
The measured capture rate and incomplete-frame count are reported every second
and once more at shutdown.

## Configure low-latency USB2Dynamixel communication

The supported hardware path uses the FTDI Virtual COM Port driver at 1 ms
latency. Run this one-time setup for the USB2Dynamixel COM port:

```powershell
.\scr\_motors\configure_ftdi_latency.ps1 -PortName COM3
```

The script verifies that the selected port is an FTDI device, requests
administrator permission only when a change is required, and restarts that
adapter so the new value takes effect. Run it again after reinstalling the FTDI
driver or when using a different PC or adapter. This setting does not change any
DYNAMIXEL control-table value.

## Run the motor emulator

For hardware-free actuator testing, start the standalone six-motor emulator:

```powershell
.\scr\_motors\run_emulator.ps1
```

Keep the emulator process running, select **Simulator (localhost)** in the
Actuators tab, leave the baud selector at 1,000,000, and press **Connect**.
Closing the main GUI also terminates the emulator process if it is running.

## Run the Qt GUI

After building, start the interface with:

```powershell
.\scr\_gui\run_gui.ps1
```

The GUI uses the default webcam when available. It currently visualizes the
circular workspace, six magnet orientations, configurable servo angle limits,
and saturation warnings. See `scr/_gui/README.md` for implementation details.
