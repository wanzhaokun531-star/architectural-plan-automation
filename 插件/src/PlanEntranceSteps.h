#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace bcpproduct {

struct PlanStepPoint { double x{}, y{}; };

enum class PlanStepEntranceKind { mainEntrance, otherExit };

struct PlanStepSwing {
    PlanStepPoint hinge;
    PlanStepPoint closedTip;
    PlanStepPoint openTip;
    bool counterclockwise{};
};

struct PlanStepInput {
    PlanStepEntranceKind kind{PlanStepEntranceKind::otherExit};
    PlanStepPoint firstAtWall;
    PlanStepPoint secondAtWall;
    PlanStepPoint outwardDirection;
    double unitMM{1};
    std::optional<double> platformDepth;
    std::optional<double> treadDepth;
    std::vector<PlanStepSwing> outwardSwings;
};

struct PlanStepLayout {
    std::array<PlanStepPoint, 4> platformCorners;
    std::array<std::array<PlanStepPoint, 2>, 2> nosingEdges;
    double platformDepth{};
    double treadDepth{};
    double totalDepth{};
    bool platformEnlarged{};
    bool usesDefaultTreads{};
    bool usesDefaultPlatform{};
};

namespace plan_step_detail {

inline bool isFinite(const PlanStepPoint& point) {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

inline bool positive(double value) {
    return std::isfinite(value) && value > 0;
}

inline PlanStepPoint difference(const PlanStepPoint& first, const PlanStepPoint& second) {
    return {first.x - second.x, first.y - second.y};
}

inline double dot(const PlanStepPoint& first, const PlanStepPoint& second) {
    return first.x * second.x + first.y * second.y;
}

struct Frame {
    PlanStepPoint tangent;
    PlanStepPoint normal;
    double width;
};

inline std::optional<Frame> wallFrame(const PlanStepInput& input) {
    if (!isFinite(input.firstAtWall) || !isFinite(input.secondAtWall) ||
        !isFinite(input.outwardDirection) || !positive(input.unitMM)) return {};
    const auto wallEdge = difference(input.secondAtWall, input.firstAtWall);
    const double width = std::hypot(wallEdge.x, wallEdge.y);
    const double directionLength = std::hypot(input.outwardDirection.x, input.outwardDirection.y);
    if (!positive(width) || !positive(directionLength)) return {};
    const PlanStepPoint tangent{wallEdge.x / width, wallEdge.y / width};
    PlanStepPoint normal{-tangent.y, tangent.x};
    const double outward = dot(normal, input.outwardDirection) / directionLength;
    if (std::abs(outward) <= 1e-9) return {};
    if (outward < 0) normal = {-normal.x, -normal.y};
    return Frame{tangent, normal, width};
}

inline double positiveAngle(double angle) {
    constexpr double fullTurn = 6.28318530717958647693;
    const double wrapped = std::fmod(angle, fullTurn);
    return wrapped < 0 ? wrapped + fullTurn : wrapped;
}

struct Interval { double low, high; };

inline Interval arcProjection(const PlanStepPoint& hinge, double radius, double from, double sweep,
        double direction, const PlanStepPoint& axis) {
    const double centre = dot(hinge, axis);
    const double axisAngle = std::atan2(axis.y, axis.x);
    const double end = from + direction * sweep;
    const double atStart = centre + radius * std::cos(from - axisAngle);
    const double atEnd = centre + radius * std::cos(end - axisAngle);
    Interval result{std::min({centre, atStart, atEnd}), std::max({centre, atStart, atEnd})};
    // Extrema at a projection axis can lie strictly inside the actual sweep.
    const auto onSweep = [&](double angle) {
        return positiveAngle(direction * (angle - from)) <= sweep + 1e-12;
    };
    if (onSweep(axisAngle)) result.high = centre + radius;
    if (onSweep(axisAngle + 3.14159265358979323846)) result.low = centre - radius;
    return result;
}

inline std::optional<double> requiredSwingDepth(const PlanStepInput& input, const Frame& frame) {
    const double epsilon = .01 / input.unitMM;
    double depth = 0;
    for (const auto& swing : input.outwardSwings) {
        if (!isFinite(swing.hinge) || !isFinite(swing.closedTip) || !isFinite(swing.openTip)) return {};
        const auto hinge = difference(swing.hinge, input.firstAtWall);
        const auto start = difference(swing.closedTip, swing.hinge);
        const auto end = difference(swing.openTip, swing.hinge);
        const double radius = std::hypot(start.x, start.y);
        const double endRadius = std::hypot(end.x, end.y);
        if (!positive(radius) || !positive(endRadius) || std::abs(radius - endRadius) > epsilon) return {};
        const double from = std::atan2(start.y, start.x);
        const double direction = swing.counterclockwise ? 1. : -1.;
        const double sweep = positiveAngle(direction * (std::atan2(end.y, end.x) - from));
        if (sweep <= 1e-12) return {};
        const auto across = arcProjection(hinge, radius, from, sweep, direction, frame.tangent);
        const auto along = arcProjection(hinge, radius, from, sweep, direction, frame.normal);
        if (across.low < -epsilon || across.high > frame.width + epsilon || along.low < -epsilon) return {};
        depth = std::max(depth, along.high);
    }
    return depth;
}

inline PlanStepPoint atDepth(const PlanStepPoint& point, const PlanStepPoint& normal, double depth) {
    return {point.x + normal.x * depth, point.y + normal.y * depth};
}

} // namespace plan_step_detail

// All input/output coordinates and explicit depths are in drawing units.
// The straight back edge must already span the verified platform width.
inline bool planStepPlatformContainsSwings(const PlanStepInput& input, double platformDepth) {
    using namespace plan_step_detail;
    const auto frame = wallFrame(input);
    if (!frame || !positive(platformDepth)) return false;
    const auto depth = requiredSwingDepth(input, *frame);
    return depth && *depth <= platformDepth + .01 / input.unitMM;
}

inline std::optional<PlanStepLayout> planEntranceStep(const PlanStepInput& input) {
    using namespace plan_step_detail;
    const auto frame = wallFrame(input);
    if (!frame || (input.platformDepth && !positive(*input.platformDepth)) ||
        (input.treadDepth && !positive(*input.treadDepth))) return {};
    const auto swingDepth = requiredSwingDepth(input, *frame);
    if (!swingDepth) return {};
    const bool main = input.kind == PlanStepEntranceKind::mainEntrance;
    const double nominalDepth = input.platformDepth.value_or(main ? 0. : 1000. / input.unitMM);
    PlanStepLayout result{};
    result.platformDepth = std::max(nominalDepth, *swingDepth);
    result.treadDepth = input.treadDepth.value_or((main ? 300. : 260.) / input.unitMM);
    result.totalDepth = result.platformDepth + 2 * result.treadDepth;
    if (!positive(result.platformDepth) || !positive(result.treadDepth) || !positive(result.totalDepth)) return {};
    result.platformEnlarged = nominalDepth > 0 && result.platformDepth - nominalDepth > .01 / input.unitMM;
    result.usesDefaultTreads = !input.treadDepth;
    result.usesDefaultPlatform = !main && !input.platformDepth;
    const auto& a = input.firstAtWall;
    const auto& b = input.secondAtWall;
    const auto& outward = frame->normal;
    result.platformCorners = {a, b, atDepth(b, outward, result.platformDepth),
                                   atDepth(a, outward, result.platformDepth)};
    for (std::size_t i = 0; i < result.nosingEdges.size(); ++i) {
        const double depth = result.platformDepth + (i + 1) * result.treadDepth;
        result.nosingEdges[i] = {atDepth(a, outward, depth), atDepth(b, outward, depth)};
    }
    return result;
}

} // namespace bcpproduct
