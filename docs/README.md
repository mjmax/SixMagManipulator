# Magnetic model review

For the first selectable, fixed-origin computational control loop, see
[Linearized control loop](linearized_control_loop.md).

For the native SI-unit evaluator intended for the future control loop, see
[Native control model](native_control_model.md).

For the compatible faster simulator functions, usage instructions, and measured
verification results, see [Optimized MATLAB model](optimized_matlab_model.md).

The self-contained LaTeX source in this directory describes the supplied MATLAB
reference model, its analytical Jacobians, the numerical verification results,
implementation findings, and questions to resolve before native control-loop
integration. It does not change the MATLAB model or implement a controller.

Source: magnetic_acceleration_model_review.tex

PDF output from the repository root: output/pdf/magnetic_acceleration_model_review.pdf

## Rebuild the PDF

Use a LaTeX distribution with pdfLaTeX, such as the existing MiKTeX installation.
No MATLAB execution is required to typeset the report. Run the following in
PowerShell from the SixMagManipulator repository root. If pdfLaTeX is not on
PATH, replace its name with the full executable path.

    New-Item -ItemType Directory -Force -Path tmp/pdfs/model_review, output/pdf | Out-Null
    pdflatex -interaction=nonstopmode -halt-on-error -file-line-error -output-directory=tmp/pdfs/model_review docs/magnetic_acceleration_model_review.tex
    if ($LASTEXITCODE -ne 0) { throw "First LaTeX pass failed." }
    pdflatex -interaction=nonstopmode -halt-on-error -file-line-error -output-directory=tmp/pdfs/model_review docs/magnetic_acceleration_model_review.tex
    if ($LASTEXITCODE -ne 0) { throw "Second LaTeX pass failed." }
    Copy-Item -LiteralPath tmp/pdfs/model_review/magnetic_acceleration_model_review.pdf -Destination output/pdf/magnetic_acceleration_model_review.pdf

Temporary build files under tmp/pdfs/ are ignored by Git. Standard LaTeX
auxiliary files are also ignored if compilation is run directly inside docs/.
Generated PDFs under output/pdf/ and docs/ are also ignored. The editable
LaTeX source remains eligible for commits. Ignoring generated files does not
delete them from disk.

Two passes resolve equation and section references. The source uses common
LaTeX packages: fontenc, lmodern, geometry, amsmath, amssymb, bm, mathtools,
booktabs, array, xcolor, fancyhdr, and hyperref. The PDF was built with the
installed MiKTeX 21.6 distribution without installing additional packages.

## Reuse

Sections on geometry, field equations, acceleration, and analytical Jacobians
can be incorporated into the future program description. Preserve the equation
labels and the distinction between raw and scaled fields. Update the verification
and open-decision sections when the native implementation and physical calibration
are available. The numerical checks document the supplied model; they are not a
hardware validation or a performance benchmark.
