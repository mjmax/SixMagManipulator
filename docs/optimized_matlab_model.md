# Optimized MATLAB model for simulation

The compatible MATLAB implementation is in `scr/_control/__impmod`, alongside
the unchanged `scr/_control/__refmod` reference supplied by Janaka. This is a
MATLAB simulator optimization, not the later native hardware-control backend.
It requires no extra toolbox, compiled extension, driver, or hardware connection.

## Existing calls remain valid

All nine public reference function names are provided:

- `parametersGen()` returns the same complete structure, including simulator
  and controller settings. All 68 fields were compared exactly.
- `R(phi)` returns the same global-to-local rotation.
- `[h, hnorm] = h_c(par, p)` accepts a three-component local point and retains
  all three field components, including nonzero local height.
- `Hc(par, w)` and `dvecHcdw(par, w)` take a two-component local point and return
  the same 2-by-2 and column-vectorized 4-by-2 analytical derivatives.
- `hn_field(par, r, theta_v)` and `Hn(par, r, theta_v)` retain their field scaling.
- `g = gn_field(par, r, theta_v)` returns a 2-by-1 acceleration vector.
- `[Gr, Gth] = gan_linear(par, r, theta_v)` returns 2-by-2 and 2-by-n Jacobians.

Positions are in metres and angles in radians. The model zero points local
positive x radially outward, and positive rotation is counterclockwise.
The literature's h notation denotes the underlying magnetic flux density B;
`hn_field` retains the reference's additional sqrt(2*kg) scaling.

Only `par.model_sel = 0` is supported. Selecting another model raises
`SixMag:ComplexModelOnly`; it never silently substitutes the complex model.
The known-incorrect reference dipole derivatives remain unchanged. Both MAT
files are included because the complete parameter structure still contains
the dipole coefficient; copying that data does not enable the dipole branch.

## Use in the simulator

Keep all files in `__impmod` together, including the two MAT files and the
shared helpers `imp_local.m` and `imp_evaluate.m`. All functions are at the
same directory level; no helper subfolder is needed.
Select this folder instead of the reference implementation on the MATLAB path.
For this checkout, run from the repository root or your simulator directory:

```matlab
projectRoot = 'D:/Work/SixMagManipulator'; % adjust for another computer
addpath(fullfile(projectRoot, 'scr', '_control', '__impmod'), '-begin');
clear parametersGen R h_c Hc dvecHcdw hn_field Hn gn_field gan_linear
which gn_field -all
which gan_linear -all
par = parametersGen();

g = gn_field(par, r, theta_v);
[Gr, Gth] = gan_linear(par, r, theta_v);
```

MATLAB gives the current directory priority over the search path. Do not run
from `__refmod`, and do not leave old copies of these functions in your simulator's
current directory. Check that `which` selects `__impmod` for every model function.
Adding both trees recursively with `genpath` can create ambiguous selection.
The optimized parameter loader resolves MAT files beside its own source, not
against the current directory. Existing compatible `par` structures also work.

### Optional combined call

For a control iteration that needs all three quantities, this additional form
avoids calculating the field and its derivatives twice:

```matlab
[g, Gr, Gth] = gn_field(par, r, theta_v);
```

This is optional. Existing one-output calls and the separate `gan_linear` call
continue to work. The combined outputs matched the separate optimized calls
exactly in the regression tests.

## What changed internally

- Evaluate source offsets and inverse-distance powers once for all magnets.
- Replace 144-by-144 diagonal matrices with direct weighted sums.
- Use only the planar symmetric derivative components instead of computing
  a full 3D tensor and projecting it afterward. Source z coordinates still
  contribute to every distance; the field geometry is not flattened.
- Replace Kronecker products with explicit small analytical contractions.
- Share the field, its first derivatives, and its second derivatives in
  acceleration/Jacobian evaluation.
- Calculate the optional local field norm only if it is requested.

There are no finite differences in the optimized model and no persistent
result or parameter caches. Changes to `par` are applied immediately. The
working arrays scale with source count times magnet count, not with simulation
duration. Consistent configurations with other magnet/source counts are also
covered by the tests. Singular evaluation exactly on a fitted source remains
outside the model's valid domain; no clipping or regularization was introduced.

## Reproduce verification and timing

```matlab
addpath(fullfile(projectRoot, 'scr', '_control', '__impmod', 'tests'));
results = verify_impmod(true);  % correctness plus timings
% results = verify_impmod(false); % correctness only
```

The test switches implementations explicitly, verifies resolved function paths,
and restores the previous MATLAB path, working directory, and random-generator
state. It compares all nine public functions and both outputs of `h_c`, checks
parameter structure equality, and tests rejection of unsupported models.
Reference and optimized functions are evaluated on identical inputs.

The deterministic suite uses seed 1709 and contains:

- 311 workspace cases: 256 random states within 30 mm, symmetric center states,
  48 states at the 34.3 mm reference workspace radius, and parameter variants.
- Variants for changed coefficients, source positions, geometry, scaling,
  one/three magnets, and a reduced source count.
- 128 local cases with nonzero z for the 3D field and norm, plus planar helper
  derivatives and rotation matrices.
- Independent central differences at 16 workspace states, used only in tests.

Every direct comparison requires matching shapes, finite values, and
`norm(new-reference) <= 1e-10 + 2e-10*norm(reference)`. Matrix norms in this test
are Frobenius norms (the norm of the flattened array).

## Measured results: 13 September 2026

All checks passed on this PC with MATLAB R2021b, version 9.11.0.1769968, PCWIN64.
The maximum scaled discrepancy, `norm(new-reference)/max(1,norm(reference))`,
over all tested outputs was about **1.62e-14**. Symmetric near-zero fields can
have a large relative percentage despite negligible absolute differences;
use the mixed tolerance and scaled discrepancy to interpret those cases.
The largest raw absolute Gr discrepancy was 3.30e-7 in the high-gradient edge
tests and satisfied the stated relative tolerance. The maximum independent
finite-difference relative discrepancies were **2.79e-10 for Gr** and
**1.09e-9 for Gth**.

The following are median per-call times from seven warmed batches of 128
distinct inputs each. Ordinary MATLAB loop, indexing, and call overhead is
included equally on both sides. Initialization, disk loading, plots, GUI,
camera acquisition, and actuator communication are excluded.

| Operation | Reference (ms) | Optimized (ms) | Speedup |
| --- | ---: | ---: | ---: |
| h_c | 0.048650 | 0.016330 | 2.98x |
| Hc | 0.117712 | 0.033194 | 3.55x |
| dvecHcdw | 0.112112 | 0.024080 | 4.66x |
| hn_field | 0.274276 | 0.044015 | 6.23x |
| Hn | 0.520282 | 0.033641 | 15.47x |
| gn_field, acceleration only | 0.650334 | 0.029999 | 21.68x |
| gan_linear, both Jacobians | 2.164392 | 0.057114 | 37.90x |
| Separate acceleration and Jacobian calls | 2.830048 | 0.111797 | 25.31x |

The optional combined `[g, Gr, Gth] = gn_field(...)` call took **0.076720 ms**,
or **36.89x faster** than separate reference calls. Each row is measured
independently, so timings are not necessarily additive.

These are local benchmark results, not machine-independent or real-time
guarantees. JIT warmup, CPU load, clock frequency, and MATLAB version affect
them. The user's earlier approximately 7 ms measurement was on a different
setup; the speedups here compare measurements made on the same PC. Overall
simulation speedup also depends on time spent outside these model functions.

No GUI, actuator behavior, or reference MATLAB file was changed.
