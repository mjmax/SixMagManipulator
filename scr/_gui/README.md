# Qt 6 GUI — Manipulator View

This module contains the C++/Qt 6 desktop GUI. The first view provides:

- a circular, center-cropped live camera viewport;
- six top-view magnet dials positioned around the workspace;
- metallic red north halves and blue south halves;
- zero-angle north-pole orientation toward the workspace center;
- positive counterclockwise magnet rotation;
- `setMagnetAngle(index, degrees)` slots for future servo feedback.

Magnet indices start at zero in the C++ interface. The initial angles are all
zero until live servo data is connected. The visible dial numbers are one-based
servo identifiers, arranged counterclockwise from the right-hand magnet.

## Build and run

Qt 6.8.3, Qt Multimedia, CMake, Ninja, and the matching MinGW compiler are
installed locally under `utilities/Qt`.

From the workspace root:

```powershell
.\scr\_gui\build_gui.ps1
.\scr\_gui\run_gui.ps1
```

The build script compiles the release executable and copies the required Qt
runtime libraries into `scr/_gui/build`. The GUI uses the default webcam when
one is available and displays a dark circular placeholder otherwise.

