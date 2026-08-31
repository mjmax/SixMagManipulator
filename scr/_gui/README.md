# Qt 6 GUI — Manipulator View

This module contains the C++/Qt 6 desktop GUI. It provides:

- a circular, center-cropped live camera workspace;
- six top-view magnet dials and servo-limit visualization;
- low-rate object and trace rendering that does not throttle detection;
- compact **Actuators** and **Image Processing** tabs;
- detector status and a **Clear trace path** button.

## Integrated image processing

The native real-time tracker is kept in `scr/_imgproc/ImageTracker.h` and
`scr/_imgproc/ImageTracker.cpp`. It implements the same dark, approximately
circular object logic as the Python prototype without starting a Python process
or copying frames between applications.

The tracker runs on a high-priority worker thread and retains only the newest
unprocessed camera frame. Detection uses every available frame, while camera,
marker, status, and trace drawing default to 15 Hz. Detection is restricted to
the centered circular region that is actually visible in the workspace.

`ImageTracker::latestResult()` provides a thread-safe latest position for the
future control loop. `fastResultReady` is emitted after every processed frame;
`visualizationResultReady` is rate-limited for the GUI. Results are represented
as a collection and the detector can return up to two objects, although the
current application deliberately requests one object.

## Actuator communication

The actuator worker in `scr/_motors` communicates with AX-18A motors using
DYNAMIXEL Protocol 1.0. It scans the six IDs configured for M1 through M6,
acquires present positions on a high-priority thread, stores a thread-safe latest
snapshot for future control, and limits magnet-dial updates to 15 Hz. The
Actuators tab provides automatic COM-port discovery, a baud-rate selector
defaulting to 1,000,000, one connect/scan button, six status lights, and an
editable bus-ID field beneath each light. The default IDs are 1 through 6; valid
IDs are 0 through 253 and must be unique.

Real USB2Dynamixel hardware uses the Qt serial/VCP path with the FTDI latency
timer set to 1 ms. Run
`scr/_motors/configure_ftdi_latency.ps1 -PortName COM3` once per PC/adapter.
Hardware polling starts the next six-motor cycle immediately after the previous
cycle finishes; the independent GUI angle display remains limited to 15 Hz.
D2XX is not used. The overlaid **Poll Actuators** button measures 100 complete
six-motor position-read cycles on the existing worker, shows the running average,
and reports **Test Failed** if any read fails.

Run `scr/_motors/run_emulator.ps1` to test the same scan and acquisition path
without hardware, then select **Simulator (localhost)** before connecting. The
emulator is automatically terminated when the main GUI closes.

## Image Processing controls

- Dark-pixel threshold
- Minimum and maximum object area
- Minimum circularity
- GUI-only refresh rate
- Trace color palette
- Typed or arrow-adjustable trace line width

## Build and run

Qt 6.8.3, Qt Multimedia, Qt SerialPort, CMake, Ninja, and the matching MinGW compiler are
expected under `utilities/Qt`. From any PowerShell directory, use the scripts by
their appropriate relative or absolute paths. From the repository root:

```powershell
.\scr\_gui\build_gui.ps1
.\scr\_gui\run_gui.ps1
```

`run_gui.ps1` automatically builds when the executable is missing or older than
the sources. Force a clean build and launch with:

```powershell
.\scr\_gui\run_gui.ps1 -Rebuild
```

The build script refreshes cached absolute paths and safely closes a running GUI
before relinking. The launch script returns the PowerShell prompt immediately.
