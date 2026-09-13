#pragma once
#include <array>
#include <cstddef>
namespace sixmag::control {
inline constexpr std::size_t MagnetCount = 6;
inline constexpr std::size_t SourceCount = 144;
using Vector2 = std::array<double, 2>;
using MagnetAngles = std::array<double, MagnetCount>;
using Matrix22 = std::array<Vector2, 2>;
using Matrix26 = std::array<MagnetAngles, 2>;

// Initialization data only. SI units; fitted coefficients have units T*m^2.
struct ModelParameters {
    std::array<double, SourceCount> sourceX{}, sourceY{}, sourceZ{}, coefficients{};
    MagnetAngles magnetX{}, magnetY{}, placementAngles{};
    double kg{};
};
struct ModelEvaluation {
    Vector2 acceleration{};              // gn_field: m/s^2
    Matrix22 positionJacobian{};          // Gr: 1/s^2; [component][position]
    Matrix26 angleJacobian{};             // Gth: m/s^2/rad; [component][magnet]
    Vector2 scaledField{};                // hn_field: sqrt(2*kg) times planar B
    Matrix22 scaledFieldJacobian{};       // Hn: derivative of scaledField
};
enum class EvaluationStatus { Ok, InvalidInput, SingularSource, NonFiniteResult };

class MagneticModel final {
public:
    static ModelParameters referenceParameters();
    MagneticModel();
    // Validates and prepares parameters once; may throw invalid_argument.
    explicit MagneticModel(const ModelParameters& parameters);
    // SI inputs: metres and radians. Zero points radially outward; CCW positive.
    // No GUI/servo unit, axis or bias conversions. Reentrant and allocation-free.
    // No I/O, locks, or exceptions. On failure ALL outputs are NaN.
    // Callers MUST check status before using any output.
    [[nodiscard]] EvaluationStatus evaluate(
        const Vector2& position, const MagnetAngles& angles,
        ModelEvaluation& output) const noexcept;
private:
    alignas(64) std::array<double, SourceCount> sourceX_{}, sourceY_{}, sourceZ2_{}, weights_{};
    MagnetAngles magnetX_{}, magnetY_{}, placementAngles_{};
};
} // namespace sixmag::control
