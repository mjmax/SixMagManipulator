#include "MagneticModel.h"
#include "data/ReferenceParameters.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sixmag::control {
namespace {
EvaluationStatus fail(ModelEvaluation& out, EvaluationStatus status) noexcept
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    out.acceleration.fill(nan);
    out.scaledField.fill(nan);
    for (auto& row : out.positionJacobian) row.fill(nan);
    for (auto& row : out.angleJacobian) row.fill(nan);
    for (auto& row : out.scaledFieldJacobian) row.fill(nan);
    return status;
}
bool finite(const ModelEvaluation& out) noexcept
{
    for (std::size_t i = 0; i < 2; ++i) {
        if (!std::isfinite(out.acceleration[i]) || !std::isfinite(out.scaledField[i]))
            return false;
        for (std::size_t j = 0; j < 2; ++j)
            if (!std::isfinite(out.positionJacobian[i][j]) ||
                !std::isfinite(out.scaledFieldJacobian[i][j])) return false;
        for (double v : out.angleJacobian[i])
            if (!std::isfinite(v)) return false;
    }
    return true;
}
} // namespace
ModelParameters MagneticModel::referenceParameters() { return detail::referenceParameters(); }
MagneticModel::MagneticModel() : MagneticModel(referenceParameters()) {}
MagneticModel::MagneticModel(const ModelParameters& p)
    : magnetX_(p.magnetX), magnetY_(p.magnetY), placementAngles_(p.placementAngles)
{
    const double scale = std::sqrt(2 * p.kg);
    if (!(p.kg > 0) || !std::isfinite(scale))
        throw std::invalid_argument("Magnetic model kg must be positive and finite.");
    for (std::size_t k = 0; k < MagnetCount; ++k)
        if (!std::isfinite(magnetX_[k]) || !std::isfinite(magnetY_[k]) ||
            !std::isfinite(placementAngles_[k]))
            throw std::invalid_argument("Nonfinite magnet geometry.");
    for (std::size_t j = 0; j < SourceCount; ++j) {
        sourceX_[j] = p.sourceX[j];
        sourceY_[j] = p.sourceY[j];
        sourceZ2_[j] = p.sourceZ[j] * p.sourceZ[j];
        weights_[j] = scale * p.coefficients[j];
        if (!std::isfinite(sourceX_[j]) || !std::isfinite(sourceY_[j]) ||
            !std::isfinite(sourceZ2_[j]) || !std::isfinite(weights_[j]))
            throw std::invalid_argument("Nonfinite or overflowing source parameters.");
    }
}
EvaluationStatus MagneticModel::evaluate(
    const Vector2& r, const MagnetAngles& theta, ModelEvaluation& output) const noexcept
{
    if (!std::isfinite(r[0]) || !std::isfinite(r[1]))
        return fail(output, EvaluationStatus::InvalidInput);
    for (double v : theta)
        if (!std::isfinite(v)) return fail(output, EvaluationStatus::InvalidInput);
    double hx = 0, hy = 0, hxx = 0, hxy = 0, hyy = 0;
    double txxx = 0, txxy = 0, txyy = 0, tyyy = 0;
    MagnetAngles dhx{}, dhy{}, qxx{}, qxy{}, qyy{};
    for (std::size_t k = 0; k < MagnetCount; ++k) {
        const double angle = theta[k] + placementAngles_[k];
        if (!std::isfinite(angle)) return fail(output, EvaluationStatus::NonFiniteResult);
        const double c = std::cos(angle), s = std::sin(angle);
        const double px = r[0] - magnetX_[k], py = r[1] - magnetY_[k];
        const double wx = c * px + s * py, wy = -s * px + c * py;
        double fx = 0, fy = 0, axx = 0, axy = 0, ayy = 0;
        double t1 = 0, t2 = 0, t3 = 0, t4 = 0;
        // Planar derivatives of sum(a*q/|q|^3), retaining full 3D distances.
        // The coefficients already include the reference sqrt(2kg) scaling.
        for (std::size_t j = 0; j < SourceCount; ++j) {
            const double x = wx - sourceX_[j], y = wy - sourceY_[j];
            const double x2 = x * x, y2 = y * y;
            const double distance2 = x2 + y2 + sourceZ2_[j];
            if (distance2 == 0) return fail(output, EvaluationStatus::SingularSource);
            if (!std::isfinite(distance2))
                return fail(output, EvaluationStatus::NonFiniteResult);
            const double inverse2 = 1 / distance2;
            const double a3 = weights_[j] * inverse2 * std::sqrt(inverse2);
            const double a5 = a3 * inverse2, a7 = a5 * inverse2;
            fx += x * a3;
            fy += y * a3;
            axx += a3 - 3 * x2 * a5;
            axy -= 3 * x * y * a5;
            ayy += a3 - 3 * y2 * a5;
            t1 += 15 * x2 * x * a7 - 9 * x * a5;
            t2 += 15 * x2 * y * a7 - 3 * y * a5;
            t3 += 15 * x * y2 * a7 - 3 * x * a5;
            t4 += 15 * y2 * y * a7 - 9 * y * a5;
        }
        const double gx = c * fx - s * fy, gy = s * fx + c * fy;
        hx += gx; hy += gy;
        const double c2 = c * c, s2 = s * s, cs = c * s;
        const double xx = c2 * axx - 2 * cs * axy + s2 * ayy;
        const double xy = cs * (axx - ayy) + (c2 - s2) * axy;
        const double yy = s2 * axx + 2 * cs * axy + c2 * ayy;
        hxx += xx; hxy += xy; hyy += yy;
        // Four independent components of the rotated symmetric third tensor.
        const double c3 = c2*c, s3 = s2*s, c2s = c2*s, cs2 = c*s2;
        txxx += c3*t1 - 3*c2s*t2 + 3*cs2*t3 - s3*t4;
        txxy += c2s*t1 + (c3-2*cs2)*t2 + (s3-2*c2s)*t3 + cs2*t4;
        txyy += cs2*t1 + (2*c2s-s3)*t2 + (c3-2*cs2)*t3 - c2s*t4;
        tyyy += s3*t1 + 3*cs2*t2 + 3*c2s*t3 + c3*t4;
        // Angle chain rule: dw/dtheta = [wy;-wx], d(R')/dtheta = J*R'.
        const double u = axx*wy-axy*wx, v = axy*wy-ayy*wx;
        dhx[k] = -gy+c*u-s*v;
        dhy[k] = gx+s*u+c*v;
        const double tx = t1*wy-t2*wx, txy = t2*wy-t3*wx, ty = t3*wy-t4*wx;
        qxx[k] = -2*xy+c2*tx-2*cs*txy+s2*ty;
        qxy[k] = xx-yy+cs*(tx-ty)+(c2-s2)*txy;
        qyy[k] = 2*xy+s2*tx+2*cs*txy+c2*ty;
    }
    ModelEvaluation out;
    out.scaledField = {hx, hy};
    out.scaledFieldJacobian = {{{hxx, hxy}, {hxy, hyy}}};
    out.acceleration = {hxx*hx+hxy*hy, hxy*hx+hyy*hy};
    const double cross = hxy*(hxx+hyy)+txxy*hx+txyy*hy;
    out.positionJacobian = {{{hxx*hxx+hxy*hxy+txxx*hx+txxy*hy, cross},
                             {cross, hxy*hxy+hyy*hyy+txyy*hx+tyyy*hy}}};
    for (std::size_t k = 0; k < MagnetCount; ++k) {
        out.angleJacobian[0][k] = hxx*dhx[k]+hxy*dhy[k]+qxx[k]*hx+qxy[k]*hy;
        out.angleJacobian[1][k] = hxy*dhx[k]+hyy*dhy[k]+qxy[k]*hx+qyy[k]*hy;
    }
    if (!finite(out)) return fail(output, EvaluationStatus::NonFiniteResult);
    output = out;
    return EvaluationStatus::Ok;
}
} // namespace sixmag::control
