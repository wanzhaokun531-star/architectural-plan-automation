#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace planradial {

// A 45-degree radial leader can start outside the circle only when its
// lateral displacement exceeds radius/sqrt(2). Zero displacement needs no bend.
// Solve all circles on one side together; retain true stations separately.
inline std::optional<std::vector<double>> packPlanRadialAxisCircles(
    const std::vector<double>& stations, double gap, double maximumShift,
    double minimumBentShift, double low, double high) {
    constexpr double tolerance = 1e-6;
    if (stations.empty()) return std::vector<double>{};
    if (!(gap > 0) || !(maximumShift >= 0) || !(minimumBentShift > 0) ||
        low > high || !std::isfinite(gap) || !std::isfinite(maximumShift) ||
        !std::isfinite(minimumBentShift)) return std::nullopt;
    for (std::size_t i = 0; i < stations.size(); ++i)
        if (!std::isfinite(stations[i]) || (i && stations[i] <= stations[i - 1]))
            return std::nullopt;

    const double origin = stations.front();
    low -= origin;
    high -= origin;
    std::vector<double> values;
    const auto add = [&](double value) { if (std::isfinite(value)) values.push_back(value); };
    for (std::size_t i = 0; i < stations.size(); ++i) {
        const double straight = stations[i] - origin - i * gap;
        for (const double offset : {0., -minimumBentShift, minimumBentShift, -maximumShift, maximumShift})
            add(straight + offset);
        add(low - i * gap);
        add(high - i * gap);
    }
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end(), [=](double a, double b) {
        return std::abs(a - b) <= tolerance;
    }), values.end());
    const double infinity = std::numeric_limits<double>::infinity();
    std::vector<double> previous(values.size(), infinity), current(values.size(), infinity);
    std::vector<std::vector<int>> parents(stations.size(), std::vector<int>(values.size(), -1));
    for (std::size_t i = 0; i < stations.size(); ++i) {
        std::fill(current.begin(), current.end(), infinity);
        double best = i ? infinity : 0.;
        int parent = -1;
        for (std::size_t j = 0; j < values.size(); ++j) {
            if (i && previous[j] < best) { best = previous[j]; parent = static_cast<int>(j); }
            const double position = values[j] + i * gap;
            const double shift = position - (stations[i] - origin);
            if (position < low - tolerance || position > high + tolerance ||
                std::abs(shift) > maximumShift + tolerance ||
                (std::abs(shift) > tolerance && std::abs(shift) < minimumBentShift - tolerance) ||
                !std::isfinite(best)) continue;
            current[j] = best + shift * shift;
            parents[i][j] = parent;
        }
        previous.swap(current);
    }
    const auto best = std::min_element(previous.begin(), previous.end());
    if (best == previous.end() || !std::isfinite(*best)) return std::nullopt;
    int index = static_cast<int>(best - previous.begin());
    std::vector<double> result(stations.size());
    for (std::size_t i = stations.size(); i-- > 0;) {
        result[i] = origin + values[static_cast<std::size_t>(index)] + i * gap;
        index = parents[i][static_cast<std::size_t>(index)];
    }
    return result;
}

} // namespace planradial
