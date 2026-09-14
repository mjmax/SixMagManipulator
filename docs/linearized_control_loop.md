# First computational control loop: fixed-origin linear feedback

The implementation is in `scr/_control/LinearizedPositionControl.{h,cpp}` and
`scr/_control/ControlLoop.{h,cpp}`, built into `SixMag::Control`. It starts from
the active statements in the supplied `__refmod/control.m`.

The native `ControlLoop` remains a caller-driven computation with no device I/O.
`FastControlRuntime` now calls it directly for each completed camera detection.
The GUI converts model-frame camera positions to SI metres, supplies its K_r
setting, and queues the latest six bias-free angle goals for the motor worker.
Protocol 1.0 goal writes are currently restricted to the standalone localhost
simulator. The other GUI controller combinations are not implemented yet.

## Exact implemented law

Let G0 = Gth(r = [0;0], theta = zeros(6,1)). The controller implements

    requestedAngles = -K_r * pinv(G0) * position

The native library default is K_r = 4000; the GUI Proportional default is
K_r = 3000 and can be edited before Start.

The target is the workspace origin. Inputs are in metres and outputs are
absolute, unbiased magnet angles in radians, not angle increments, velocities,
or raw servo positions. Gain k has units 1/s^2 and defaults to 4000. All units
at this interface are SI.

The current law intentionally ignores measured magnet angles. It does not
implement the commented-out `theta + pinv(Gth)*(yd-gf)` alternative, subtract
Gr*r, or evaluate a moving linearization. The native model and pseudoinverse
are evaluated once when constructing the controller, not on each update.
The resulting 6-by-2 matrix makes the hot path a matrix-vector multiplication.

For this full-row-rank 2-by-6 matrix, the pseudoinverse is computed as
G0' * inv(G0*G0'). The small Gram matrix is normalized before inversion.
Initialization rejects a zero or poorly conditioned mapping instead of
silently dropping an axis. In particular, singular-value ratios at or below
approximately 1e-6 are rejected. This differs deliberately from MATLAB pinv's
truncated solution in rank-deficient cases; the supplied model is full rank.

`LinearizedPositionControl::evaluate` returns the unrestricted reference-law
output. `ControlLoop::step` retains that output and also produces separately
clipped commands, with one saturation flag per motor.

## Loop behavior and future selection

`ControlMode` currently contains `Disabled` and `LinearizedOrigin`. The loop
starts disabled. An unsupported mode request disables it and reports failure.
The input structure already has measured-angle data and a validity flag for
future theta-feedback laws. Additional algorithms can be added to this module
and selected through this enum when GUI integration is implemented.

Each step takes one feedback snapshot and a current monotonic timestamp:

- Accept a valid new object position in the fixed model coordinate system.
- Reject nonfinite, missing, future-dated, stale, duplicate, or out-of-order
  position data as appropriate.
- Evaluate the selected law and reject nonfinite calculations.
- Return requested angles, clipped commanded angles, and saturation flags.
- Set `commandReady` only for a successfully computed fresh command.

Rejected commands contain NaN commanded angles and `commandReady == false`.
This means **do not issue a new command**; it does not stop a physical motor
already moving toward a previous goal. Emergency stopping, feedback-loss
behavior, watchdogs, transport failures, and arming must be handled by the
future hardware layer. There is no claim that these guards alone establish
safe closed-loop hardware operation.

`ControlLoopOptions` exposes gain, feedback-age limit, and six independent
lower/upper angle limits. Defaults are 4000, 0.1 s, and +/-2.61799387799 rad
(+/-150 degrees). The 0.1 s freshness limit is a configurable starting value,
not a hardware-validated deadline. Update it for the actual acquisition rate.

The motor limits are in unbiased model radians. When linking to the existing
actuator configuration, derive them from each motor's permitted raw range minus
its bias. With a raw 0..300 degree range and bias b, for example, the limits are
[-b, 300-b] converted to radians. The eventual actuator boundary must add bias
exactly once and convert to servo units. This computation layer does neither.

## Saturation and locality

With the supplied model, k=4000 and +/-150 degree limits, the largest centered
circle guaranteed to avoid angle clipping has radius about **0.00384048 m**.
Positions outside it may or may not saturate depending on direction. In the
265-case test set spanning a 0.030 m radius, 255 cases saturated at least one
motor. This is expected for this gain, not an implementation discrepancy.

Clipping changes the requested minimum-norm solution: G0*commandedAngles no
longer necessarily equals -4000*position. The law is a local linear controller;
neither unrestricted commands nor clipped commands imply reliable convergence
from the entire workspace. Gain tuning, finite actuator response, nonlinear
fields, noise, calibration, and delays need later simulation/hardware review.

## Calling contract

Construct the model and loop outside the time-critical update path:

```cpp
#include "ControlLoop.h"
using namespace sixmag::control;

MagneticModel model;
ControlLoopOptions options;
ControlLoop loop(model, options); // disabled
if (!loop.selectMode(ControlMode::LinearizedOrigin)) {
    // Leave the controller disabled.
}

// Future worker: provide a snapshot for each fresh object detection.
ControlFeedback input;
input.position = positionMetersInModelFrame;
input.positionValid = objectDetectedAndPositionUsable;
input.positionSequence = detectionSequence;
input.positionTimeSeconds = captureTimeSeconds;
input.measuredAngles = unbiasedMagnetAnglesRadians; // unused by this first law
input.anglesValid = motorFeedbackValid;
ControlStep result = loop.step(input, nowSeconds);
if (result.commandReady) {
    // Candidate unbiased goals are in result.commandedAngles.
    // Hardware authorization and transport integration are still required.
}
```

The timestamp and `nowSeconds` must use the same monotonic clock epoch. Sequence
numbers must increase with acquisition. Disable the loop and reset its history
when restarting acquisition or changing camera sources; explicitly re-enable
only when the new feedback is usable. Selecting a different supported mode
resets sequence history. Selecting the same mode does not resend an old frame.

The pure control law is const/reentrant. The stateful loop requires one owner
thread to serialize `step`, `selectMode`, and `reset`. Future GUI changes should
be delivered to that owner, not applied concurrently. Invoke the step from a
fast feedback worker rather than the GUI refresh timer. Reconstruct the loop
outside the hot path when model parameters, gain, or angle limits change.

## Verification and benchmarks

The existing build command now includes both model and controller tests:

```powershell
.\scr\_control\build_control.ps1 -Benchmark
```

To regenerate controller comparisons, run MATLAB from the repository root:

```matlab
addpath(fullfile(pwd, 'scr', '_control', 'tests'));
export_control_reference();
```

The exporter executes a renamed temporary copy of the original `control.m`.
An earlier version used `pr` instead of `par`; the exporter can correct that
specific typo in the isolated copy and records whether it did so. The current
reference already uses `par`, so no typo repair was needed for the recorded
tests. It never rewrites the original control law or substitutes `__impmod`.

The 265 fixtures include random positions within 0.030 m, the origin, small
axis displacements, and workspace-boundary axis points, with varied theta.
Every unrestricted output is compared to original MATLAB control.m using
`abs(error) <= 1e-10 + 2e-10*abs(reference)` per component.

Both Release and Debug tests passed on this PC:

- All 265 original-controller cases; maximum angle error **6.39488e-14 rad**.
- Pseudoinverse identity, configurable gain and independent motor limits.
- Disabled/unsupported modes, duplicate/stale/out-of-order data, invalid time,
  invalid position, and overflowing calculations.
- Missing angle feedback is accepted by this position-only law, as intended.
- Zero-rank initialization and invalid options are rejected.
- 4096 control steps with zero observed C++ new/new[] allocations.
- An offline linear-plant smoke test converged from [0.001, -0.00075] m to a
  radius of 9.80e-9 m after 2 s. This test assumes instantaneous angle actuation,
  the reference viscous damping, a 0.001 s step, and the linearized plant
  acceleration Gr0*r + Gth0*theta. It is not hardware or nonlinear validation.
- The existing 563-case native magnetic model tests continue to pass.

Release GCC/MinGW 13.1.0 timings on 13 September 2026, median of seven warmed
100,000-update batches:

| Computation | Average time per update within the median batch |
| --- | ---: |
| Fixed-origin law only | 0.006529 microseconds |
| Full computational step including guards and limits | 0.030598 microseconds |

Step batch averages ranged from 0.026370 to 0.061522 microseconds. These figures
include input preparation, loop and function-call overhead; they exclude
initialization, camera processing, actuator I/O and all real transport delays.
They are not per-call worst-case bounds or achievable hardware loop periods.
The fixed controller is much cheaper than a fresh nonlinear model evaluation
because its Jacobian and pseudoinverse are cached once. Future nonlinear laws
will need a different amount of work per update.
