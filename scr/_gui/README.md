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

The camera selector defaults to the webcam and also lists physical Allied
Vision cameras discovered through Vimba X. VimbaCameraSource uses the native
VmbC API with 32 reusable acquisition buffers. The camera and detector run as
fast as possible; only the workspace image and status updates are limited by
the **GUI refresh rate**. Selecting a Vimba camera starts **Exposure time** at
5000 microseconds and enables editing it together with the available **Gain**,
**Black level**, and **Gamma** controls.

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

The **Pole Calibration** tab connects to an Arduino Mega sensor stream. Select
the Arduino COM port and baud rate (115200 by default), then connect. The
button turns green only after a complete six-value comma-separated record is
received. Partial or malformed startup data is discarded, and the six raw Hall
sensor readings are displayed as M1 through M6. The serial port is released
automatically when the GUI closes.

## Image Processing controls

- Dark-pixel threshold
- Camera rotation (-360° to +360°) for the displayed image, trace, and detection overlays; selecting Webcam defaults to 0°, and selecting a Mako/Vimba camera defaults to -30°
- Minimum and maximum object area
- Minimum circularity
- GUI-only refresh rate
- Webcam/Vimba camera source
- Editable Mako exposure with a 5000-microsecond startup default
- Gain, black-level, and gamma controls
- Trace color palette
- Typed or arrow-adjustable trace line width

While **Maximum object area** is being edited, a translucent red circle with
the configured pixel area is drawn over the detected object. The preview is
removed as soon as focus leaves that field.

While **Minimum circularity** is being edited, the measured circularity is
shown beside the object. A green ring means it meets the selected minimum; a
red ring means it falls below the selected minimum. The last valid measurement
remains visible during editing so a stricter trial value can be compared.

The compact workspace controls provide lock/unlock, 10% zoom steps, an
editable zoom percentage, and left-button image panning while unlocked. The
default view is centered at 100%. Zoom and pan are saved independently for
each camera source and restored after source changes or application restarts.

## Build and run

Qt 6.8.3, Qt Multimedia, Qt SerialPort, CMake, Ninja, and the matching MinGW
compiler are expected under `utilities/Qt`. Vimba X is expected under
`utilities/AlliedVision/VimbaX`; see the root README for its driver and API
setup. From any PowerShell directory, use the scripts by their appropriate
relative or absolute paths. From the repository root:

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
