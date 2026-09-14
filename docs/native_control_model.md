# Native analytical model for the control loop

The C++17 library in `scr/_control` evaluates the verified complex magnetic
model directly from the equations and data in `__refmod`. It does not call
MATLAB, use `__impmod`, calculate a control law itself, or communicate with hardware.
Both MATLAB implementations are unchanged.

The library also now includes the separate [linearized computational control
loop](linearized_control_loop.md). Live GUI and motor integration remain pending.

One native call returns magnetic acceleration, its position Jacobian, and its
magnet-angle Jacobian together. The scaled field and its spatial derivative
are also returned because they are already available in that computation.

## API and units

Public interface: `scr/_control/MagneticModel.h`, namespace `sixmag::control`.

| Input/output | Size | Meaning and SI units |
| --- | --- | --- |
| position | 2 | Model-frame position, m |
| angles | 6 | Magnet orientations, rad |
| acceleration | 2 | MATLAB gn_field, m/s^2 |
| positionJacobian | 2 x 2 | MATLAB Gr, 1/s^2 |
| angleJacobian | 2 x 6 | MATLAB Gth, m/s^2/rad |
| scaledField | 2 | MATLAB hn_field, sqrt(2 kg) times planar B, m/s |
| scaledFieldJacobian | 2 x 2 | MATLAB Hn, 1/s |

Matrices use `[output component][input component]` indexing. Columns of the
angle Jacobian correspond to M1 through M6, indexed 0 through 5 in C++.
The model's first coordinate points toward M1; its second points between M2
and M3. At zero magnet angle, local positive x points radially outward.
Positive magnet rotations are counterclockwise. All computations use double.

The existing GUI's axis flips, camera transforms, millimetres, and servo degrees
must be converted to this fixed SI model frame before evaluation. Servo bias
belongs at the actuator boundary: subtract it from measured raw servo angles;
add it when converting a future commanded model angle back to a raw servo goal.
The evaluator neither adds bias nor imposes motor limits.

Construct the model once outside the feedback loop:

```cpp
#include "MagneticModel.h"
using namespace sixmag::control;

MagneticModel model; // Loads compiled reference constants, not a disk file.

// For each future control iteration; position is in metres, angles in radians:
ModelEvaluation values;
EvaluationStatus status = model.evaluate(position, angles, values);
if (status == EvaluationStatus::Ok) {
    // Use values.acceleration, values.positionJacobian, values.angleJacobian
    // in the control law that will be implemented separately.
} else {
    // Reject this evaluation. Never generate a motor command from its outputs.
}
```

`evaluate` is const, noexcept, reentrant and uses fixed-size storage. There are
no application heap allocations, file reads, locks, Qt calls, or MATLAB calls
inside it. Use a separate output object for each caller/thread. Do not modify
or replace a shared model while another thread is evaluating it.

## Parameters, assumptions, and failures

The present hardware model has exactly six magnets and 144 fitted sources per
magnet. `ModelParameters` contains only source coordinates/coefficients, magnet
centres, placement angles, and kg. The default constants are generated from
`__refmod/parametersGen.m` and its MAT files, using 17 decimal digits to preserve
double values. The generated header is `data/ReferenceParameters.h`.

For new calibrated parameters, start with `MagneticModel::referenceParameters()`,
edit a copy, and construct a new `MagneticModel(parameters)` outside the hot loop.
Invalid initialization throws `std::invalid_argument`. There is no stale
persistent cache: an instance owns its prepared parameters. No dipole branch
is implemented or selected implicitly.

`evaluate` explicitly distinguishes invalid nonfinite inputs, an exact source
singularity, and nonfinite/overflowing results. On any failure, **every output
is NaN**. The return status must be checked. There is no silent fallback to a
zero acceleration, no clipping, and no regularization of the reference model.

Finite evaluation positions are not restricted to a circle: an optimizer may
evaluate points outside the visible workspace. The future controller must
enforce its physical workspace, valid calibration, fresh sensor measurements,
actuator limits, and safe behavior on failed evaluations. This library alone
does not establish hardware safety or experimentally validate the field model.

## Implementation

Each source distance and its inverse powers are evaluated once. Source heights
are retained in the 3D distance. All required analytical derivatives are
accumulated in the same pass; no numerical differences enter the runtime model.
Only the independent planar symmetric components are accumulated. Small
explicit contractions replace diagonal matrices and Kronecker products.
The constant field scaling is incorporated into source weights during
initialization. No per-call parameter processing or model-data loading remains.

The default build uses normal Release optimization without fast-math or
machine-specific instruction requirements. Finite checks and NaN failure
semantics are retained. No new package installation was needed on this PC.

## Build and test on this PC

From the repository root in PowerShell:

```powershell
.\scr\_control\build_control.ps1 -Benchmark
```

Omit `-Benchmark` to build and run correctness tests only. The script resolves
all paths relative to itself, uses the existing compiler/CMake/Ninja under
`utilities/Qt/Tools`, and restores its process PATH afterward. It does not open
or stop the GUI, camera, emulator, or motor connection. Generated binaries and
caches stay under `scr/_control/build`, which is ignored by Git.

The library itself is independent of Qt. With an available C++17 compiler on
another platform, the standalone build is:

```sh
cmake -S scr/_control -B scr/_control/build -DCMAKE_BUILD_TYPE=Release
cmake --build scr/_control/build --config Release
ctest --test-dir scr/_control/build -C Release --output-on-failure
```

Only Windows/MinGW has been built and tested here; other platforms need their
own build verification. CMake exposes `SixMag::Control` for future consumers.
When this directory is added as a subdirectory, test targets default to off:

```cmake
add_subdirectory(path/to/scr/_control control-build)
target_link_libraries(YourControlTarget PRIVATE SixMag::Control)
```

No GUI CMake changes were made yet. The control-law consumer will link this
target when its implementation begins.

## Regenerate the original MATLAB comparison data

MATLAB is needed only to regenerate the constants/fixtures, not to build or run
the native evaluator. From the repository root in MATLAB:

```matlab
addpath(fullfile(pwd, 'scr', '_control', 'tests'));
export_native_reference(true);  % also times reference g + both Jacobians
```

This script deliberately selects and checks every reference function path
under `__refmod`. It restores the prior working directory, path, and RNG state.
It writes the generated parameter header and `tests/reference_cases.csv`.
Regenerate and rebuild whenever the reference model or its parameters change;
otherwise the compiled constants and fixtures still represent the old model.
The fixtures and generated parameter header are versionable source/test data,
not temporary build products. The original MATLAB files are never rewritten.

The native executable reads the fixtures only during testing. They contain
563 deterministic states: 512 random positions within 30 mm, three symmetric
center configurations, and 48 configurations on the 34.3 mm reference boundary.
The random seed is 2909. Expected g, Gr, Gth, hn and Hn come directly from the
original MATLAB functions. Matrices are serialized explicitly in row-major
order, independent of MATLAB's default column-major storage.

## Verification and measured latency, 13 September 2026

Release and Debug builds passed using GCC/MinGW 13.1.0. MATLAB R2021b generated
the reference cases. The tests include:

- All five outputs against all 563 original MATLAB cases.
- 32 independent central-difference and angle-periodicity cases.
- Initialization changes and invalid parameter/input checks.
- Singular-source and overflow rejection, with all outputs invalidated.
- 4,096 evaluations with zero observed C++ new/new[] allocations.
- Concurrent deterministic calls on one shared model from four threads.

Each reference-output group must satisfy a Frobenius/vector norm bound of
`error <= 1e-10 + 2e-10 * norm(reference)`. This is a mixed absolute/relative
criterion, not a claim of bit-identical floating-point arithmetic.

| Output | Largest absolute norm error | Largest error / max(1, reference norm) |
| --- | ---: | ---: |
| g | 1.16e-10 | 9.44e-15 |
| Gr | 3.33e-7 | 4.07e-15 |
| Gth | 2.97e-9 | 3.59e-14 |
| hn | 6.52e-15 | 1.85e-15 |
| Hn | 2.23e-11 | 1.35e-14 |

Large absolute Gr values near the workspace edge make its absolute error
larger; it still satisfies the relative tolerance. Symmetric near-zero fields
should not be judged by relative percentage alone. Independent finite
differences had a maximum componentwise scaled discrepancy of 4.26e-8.

The Release benchmark uses the same first 128 reference inputs, warms the model,
and excludes construction, CSV loading, I/O, GUI, and hardware communication.
It consumes outputs so the compiler cannot discard evaluations.

| Measurement | Time per combined evaluation |
| --- | ---: |
| Original MATLAB, median of seven warmed batches | 5.016767 ms |
| Native, median of 31 warmed batches | 8.042969 microseconds (0.008043 ms) |
| Native individual-call median | 8.8 microseconds |
| Native individual-call 95th percentile | 11.5 microseconds |
| Native individual-call 99th percentile | 14.7 microseconds |
| Native maximum observed in 10,000 timed calls | 66.4 microseconds |

The original MATLAB batch-average range was 3.795108 to 6.109588 ms/call.
The native batch-average range was 7.999219 to 15.617188 microseconds/call.
Individual-call measurements include timing overhead and OS interruptions.
These separate-process measurements are local observations, not controlled
machine-independent speed guarantees. The native batch median was roughly
624 times faster than the reference batch median in this run; loading and
initialization costs are excluded on both sides.

Windows scheduling can create longer delays than those observed here. Neither
the percentile measurements nor the largest observed delay is a worst-case
execution-time bound or hard real-time guarantee. This is model evaluation
latency, not the future control-loop period or motor-bus update rate.
