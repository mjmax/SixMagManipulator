#include "LinearizedPositionControl.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sixmag::control {
LinearizedPositionControl::LinearizedPositionControl(const MagneticModel& model, double gain)
{
    if (!std::isfinite(gain) || gain <= 0)
        throw std::invalid_argument("Control gain must be positive and finite (1/s^2).");
    ModelEvaluation atOrigin;
    if (model.evaluate(Vector2{}, MagnetAngles{}, atOrigin) != EvaluationStatus::Ok)
        throw std::invalid_argument("The control linearization could not be evaluated.");
    const auto& g = atOrigin.angleJacobian;
    double scale = 0;
    for (const auto& row : g)
        for (double v : row) scale = std::max(scale, std::abs(v));
    if (!(scale > 0))
        throw std::invalid_argument("Zero-rank angle Jacobian at the control origin.");

    // Full-row-rank 2x6 pseudoinverse G' * inv(G*G'). Normalize before forming
    // the Gram matrix. Reject rank loss/poor conditioning instead of silently
    // using a truncated inverse for a controller that requires both axes.
    double a = 0, b = 0, d = 0;
    for (std::size_t j = 0; j < MagnetCount; ++j) {
        const double x = g[0][j]/scale, y = g[1][j]/scale;
        a += x*x; b += x*y; d += y*y;
    }
    const double determinant = a*d-b*b;
    const double largest = (a+d+std::hypot(a-d,2*b))/2;
    if (!(determinant > 1e-12*largest*largest))
        throw std::invalid_argument("Rank-deficient or ill-conditioned control linearization.");
    for (std::size_t j = 0; j < MagnetCount; ++j) {
        const double x = g[0][j]/scale, y = g[1][j]/scale;
        matrix_[j] = {-gain*(d*x-b*y)/(scale*determinant),
                      -gain*(a*y-b*x)/(scale*determinant)};
        if (!std::isfinite(matrix_[j][0]) || !std::isfinite(matrix_[j][1]))
            throw std::invalid_argument("Nonfinite control mapping.");
    }
}

ControlLawStatus LinearizedPositionControl::evaluate(
    const Vector2& r, MagnetAngles& output) const noexcept
{
    if (!std::isfinite(r[0]) || !std::isfinite(r[1])) {
        output.fill(std::numeric_limits<double>::quiet_NaN());
        return ControlLawStatus::InvalidPosition;
    }
    MagnetAngles values;
    for (std::size_t j = 0; j < MagnetCount; ++j) {
        values[j] = matrix_[j][0]*r[0]+matrix_[j][1]*r[1];
        if (!std::isfinite(values[j])) {
            output.fill(std::numeric_limits<double>::quiet_NaN());
            return ControlLawStatus::NonFiniteCommand;
        }
    }
    output = values;
    return ControlLawStatus::Ok;
}
} // namespace sixmag::control
