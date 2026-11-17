#pragma once

#include "PlanEntranceSteps.h"

namespace bcpproduct {
struct PlanStairTread { PlanStepPoint first, second; };
struct PlanStairFlight {
    PlanStepPoint first, across, travel;
    double width, pitch;
    std::size_t visibleTreads;
};

inline std::optional<PlanStairFlight> planStairFlight(const std::vector<PlanStairTread>& treads, double tolerance) {
    using namespace plan_step_detail;
    if (treads.size() < 3 || !positive(tolerance)) return {};
    auto vector = difference(treads.front().second, treads.front().first);
    const double width = std::hypot(vector.x, vector.y);
    if (!positive(width) || width <= tolerance) return {};
    if (vector.x < -tolerance || (std::abs(vector.x) <= tolerance && vector.y < 0))
        vector = {-vector.x, -vector.y};
    const PlanStepPoint across{vector.x / width, vector.y / width};
    const PlanStepPoint travel{-across.y, across.x};
    const auto origin = treads.front().first;
    std::vector<std::pair<double, PlanStepPoint>> stations;
    std::optional<double> nearEdge;
    for (const auto& tread : treads) {
        if (!isFinite(tread.first) || !isFinite(tread.second)) return {};
        auto a = tread.first, b = tread.second;
        if (dot(difference(b, a), across) < 0) std::swap(a, b);
        const auto segment = difference(b, a);
        if (std::abs(dot(segment, travel)) > tolerance || std::abs(dot(segment, across) - width) > tolerance) return {};
        const auto relative = difference(a, origin);
        const double edge = dot(relative, across);
        if (!nearEdge) nearEdge = edge;
        if (std::abs(edge - *nearEdge) > tolerance) return {};
        stations.emplace_back(dot(relative, travel), a);
    }
    std::sort(stations.begin(), stations.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    const double pitch = (stations.back().first - stations.front().first) / (stations.size() - 1);
    if (pitch <= tolerance) return {};
    for (std::size_t i = 1; i < stations.size(); ++i)
        if (std::abs(stations[i].first - stations.front().first - i * pitch) > tolerance) return {};
    return PlanStairFlight{stations.front().second, across, travel, width, pitch, treads.size()};
}
}
