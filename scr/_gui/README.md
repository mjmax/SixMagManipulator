# Qt 6 GUI — Manipulator View

This module contains the C++/Qt 6 desktop GUI. It provides:

- a circular, center-cropped live camera workspace;
- six top-view magnet dials and servo-limit visualization;
- low-rate object and trace rendering that does not throttle detection;
- compact settings pages selected through the three-dot panel menu;
- detector status and a **Clear trace path** button.

The three-dot button at the top right of the control panel selects **Control**,
**Actuators**, **Image Processing**, or **Pole Calibration**. A dot marks the
selected page, and the heading changes to `CONTROL PANEL : <page name>`.
The GUI opens on **Control**. The existing settings in the other pages are unchanged.
The Control page has a **Control Select** menu with **Linear Two Norm Min** and
**Nonlinear Feedback Linearize**; the first option is selected initially.
At the top right, **Control Type** offers **Proportional**, **Proportional
Integral**, **LQR**, and **LQR With Integral**, defaulting to Proportional.
The gain fields below it change with the selected type and keep edits while
switching types. Proportional starts with K_r = 3000; Proportional Integral
with K_r = 2000 and K_i = 10; LQR with K_r = 3000, K_v = 1,
K_yhat = 0.06, and K_yhat1 = 1.5e-4; LQR With Integral with K_r = 1500,
K_v = 0.1, K_yhat = 0.01, K_yhat1 = 1e-4, and K_q = 1100.
Integral types show a **Reset Int** button. The GUI emits a reset request
from it; the native loop does not yet have an integral state to reset.

Two square buttons below **Control Select** provide **Start/Stop** and **Reset**.
Start turns green and becomes Stop while the controller is active. Only
**Linear Two Norm Min + Proportional** is currently executable; it uses the
current K_r field instead of the native library's 4000 default. All other
controller combinations leave Start disabled. The goal-write path is currently
restricted to **Simulator (localhost)**; a physical COM connection remains
read-only for this control feature. Stop ceases new goal writes and leaves the
last motor targets in place. Reset is available only while stopped and sends
all six motors to their configured raw bias positions.

Each completed detection drives the native controller directly on the image
worker thread. Its position is converted from calibrated millimetres to model
metres, with fixed model axes even if the displayed axes are flipped. The
motor worker retains only the latest command, adds each motor's bias at the
write boundary, and sends one Protocol 1.0 synchronized goal packet after a
healthy position-poll cycle. GUI refresh rate does not schedule the loop.
Completed frames without a detected object use (0, 0) metres as control feedback.
For the current proportional controller this commands zero unbiased angles
(raw bias positions). Missing/stale frames do not synthesize new feedback.
Changing the camera view or
calibration, or losing the actuator connection, stops the loop.

## Integrated image processing

System Status reports three control timings, refreshed at the selected GUI rate:

- **Loop time:** interval between completed motor-command writes (the effective
  actuator update period), including the effect of six-motor polling, camera
  processing and command output. It appears after two completed writes.
- **End-to-end delay:** elapsed time from the earlier of the selected frame's
  arrival at the image tracker or the associated six-motor poll start, through
  detection, control calculation, queueing and completed command output.
  Camera and polling work overlap, rather than having their durations added.
- **Control law:** native control-step evaluation time for the transmitted
  command, in milliseconds with six decimal places.

These are host software measurements, not camera exposure-to-motion latency.
Broadcast goal writes have no motor acknowledgement: completion means the
application's output queue drained, not that motors reached their targets.
Only successfully written commands update the timing snapshot; while waiting
for the next update the last measurement remains displayed. Stopping control
clears the display, and each new run starts a fresh timing session.

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

`ImageTracker::latestResult()` provides a thread-safe latest position for other
consumers. `fastResultReady` is emitted after every processed frame;
`visualizationResultReady` is rate-limited for the GUI. Results are represented
as a collection and the detector can return up to two objects, although the
current application deliberately requests one object.
Each full-rate `TrackedObject` includes `positionMillimeters` for display and
`modelPositionMillimeters` for control, both computed from the active camera's
distance calibration. The latter keeps fixed mathematical axes.
Changing the calibration, camera view, or axis direction updates the worker's
lightweight coordinate transform; it does not change the GUI refresh limit.

## Actuator communication

The actuator worker in `scr/_motors` communicates with AX-18A motors using
DYNAMIXEL Protocol 1.0. It scans the six IDs configured for M1 through M6,
acquires present positions on a high-priority thread, stores a thread-safe latest
snapshot for future control, and limits magnet-dial updates to 15 Hz. The
Actuators tab provides automatic COM-port discovery, a baud-rate selector
defaulting to 1,000,000, one connect/scan button, six status lights, and an
editable bus-ID field beneath each light. The default IDs are 1 through 6; valid
IDs are 0 through 253 and must be unique.

Each motor also has a saved **Bias** field from 0° to 300°, defaulting to
150°. The worker subtracts this bias from every 0°–300° present-position
reading, so `MotorController::latestAngles()` gives full-rate, signed angles
relative to each magnet's zero position (positive counterclockwise). Magnet
dials and labels use those corrected angles and update at the selected GUI
refresh rate. The bias is associated with M1–M6, not the editable bus ID.

The control loop keeps all measured and commanded magnet angles in
this bias-free coordinate system. At the motor-command boundary only, add the
corresponding M1–M6 bias to each commanded angle, validate the resulting servo
angle against its 0°–300° physical range, and then encode the goal position.
Do not add or subtract the bias again inside the control law.

Real USB2Dynamixel hardware uses the Qt serial/VCP path with the FTDI latency
timer set to 1 ms. Run
`scr/_motors/configure_ftdi_latency.ps1 -PortName COM3` once per PC/adapter.
Hardware polling starts the next six-motor cycle immediately after the previous
cycle finishes; the independent GUI angle display remains limited to 15 Hz.
D2XX is not used. The overlaid **Poll Actuators** button measures 100 complete
six-motor position-read cycles on the existing worker, shows the running average,
and reports **Test Failed** if any read fails.

Run `scr/_motors/run_emulator.ps1` to test scanning, acquisition, Start/Stop,
and Reset without hardware, then select **Simulator (localhost)** before
connecting. The emulator is automatically terminated when the main GUI closes.
The simulator now follows goal positions subject to its configured speed limit.
The end-to-end test can be built with `-DSIXMAG_GUI_BUILD_TESTS=ON` and run as
`SimulatorControlIntegrationTests.exe` from the GUI build directory.

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
- Show Axis toggle with x/y buttons to reverse each positive workspace direction
- Real Distance calibration with a Distance field (60 mm by default)

The selected positive x/y directions are saved and restored after restarting
the application. The axes remain hidden until **Show Axis** is enabled.

Turn on **Real Distance** to show the P1/P2 green cross markers. Right-click
inside the circular workspace to place P1, then right-click again to place P2;
further clicks continue alternating. Enter the known P1-to-P2 separation in
the enabled **Distance** field. The default P1/P2 markers and 60 mm value
provide an active initial calibration even before this button is used; changes
to points or distance take effect immediately. The System Status position is
shown in millimetres relative
to the workspace centre and the current x/y axis directions. The markers and
distance are stored separately for each camera source and restored when that
source is selected again, including after restarting the application. Turn
**Real Distance** on to inspect or revise them.

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

The **Actuators** tab has a motor speed-limit box beneath the **Poll Actuators**
result, labeled **Speed (RPM)**.
It is in RPM (35 RPM by default) and is saved across launches. On connection
and after edits while connected, the application converts RPM to the AX-18A
Protocol 1.0 Moving Speed register (address 32, two bytes) using approximately
0.111 RPM per step and writes the value to all six configured motor IDs.
The supported range is 0.11–97 RPM. Register value zero, which means
unrestricted speed in joint mode, is never sent for a positive limit. See the
[AX-18A control table](https://emanual.robotis.com/docs/en/dxl/ax/ax-18a/).

Qt 6.8.3, Qt Multimedia, Qt SerialPort, CMake, Ninja, and the matching MinGW
compiler are expected under `utilities/Qt`. Vimba X is expected under
`utilities/AlliedVision/VimbaX`; see the root README for its driver and API
setup. The GUI uses `QPainter`, not Vulkan, so the Vulkan SDK is not required.
The CMake configuration keeps Qt's optional Vulkan-header check quiet. If a
future feature uses Vulkan, add `find_package(Vulkan REQUIRED)` to
`CMakeLists.txt` and install the Vulkan SDK; a missing SDK will then stop the
build with a clear error.

From any PowerShell directory, use the scripts by their appropriate
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
