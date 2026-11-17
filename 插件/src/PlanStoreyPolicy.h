#pragma once
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace bcpproduct {
struct PlanLevelInput {
    std::optional<int> floor;
    std::optional<double> explicitHeightMM, finishedMetres;
    bool heightConflict{}, datumConflict{};
};

struct PlanLevelResult {
    std::optional<double> heightMM, finishedMetres;
    std::string heightBasis, datumBasis;
};

inline std::vector<PlanLevelResult> resolvePlanLevels(const std::vector<PlanLevelInput>& input,
                                                      std::optional<double> defaultHeightMM) {
    std::vector<PlanLevelResult> result(input.size());
    std::map<int, std::vector<std::size_t>> floors;
    for (std::size_t i = 0; i < input.size(); ++i) {
        const auto& row = input[i];
        if (row.floor) floors[*row.floor].push_back(i);
        if (!row.datumConflict && row.finishedMetres) {
            result[i].finishedMetres = row.finishedMetres;
            result[i].datumBasis = "explicit-finished-floor";
        }
    }
    const auto adjacent = [&](int floor) -> std::optional<std::size_t> {
        const auto found = floors.find(floor);
        if (found == floors.end() || found->second.size() != 1) return {};
        return found->second.front();
    };
    if (const auto first = adjacent(1); first && !input[*first].datumConflict && !result[*first].finishedMetres) {
        result[*first].finishedMetres = 0.;
        result[*first].datumBasis = "first-floor-project-zero";
    }
    for (std::size_t i = 0; i < input.size(); ++i) {
        const auto& row = input[i];
        auto& resolved = result[i];
        if (row.heightConflict || row.datumConflict) {
            resolved.heightBasis = "conflicting-source";
            continue;
        }
        std::optional<double> difference;
        if (row.floor && adjacent(*row.floor) == i && resolved.finishedMetres) {
            if (const auto upper = adjacent(*row.floor + 1)) {
                if (input[*upper].datumConflict && !row.explicitHeightMM) {
                    resolved.heightBasis = "conflicting-source";
                    continue;
                }
                if (result[*upper].finishedMetres)
                    difference = (*result[*upper].finishedMetres - *resolved.finishedMetres) * 1000.;
            }
        }
        if (difference && (*difference <= 0 ||
                           (row.explicitHeightMM && std::abs(*row.explicitHeightMM - *difference) > .01))) {
            resolved.heightBasis = "conflicting-source";
            continue;
        }
        if (difference) {
            resolved.heightMM = difference;
            resolved.heightBasis = "adjacent-finished-floor-difference";
        } else if (row.explicitHeightMM) {
            resolved.heightMM = row.explicitHeightMM;
            resolved.heightBasis = "explicit-storey-height";
        } else if (defaultHeightMM) {
            resolved.heightMM = defaultHeightMM;
            resolved.heightBasis = "rule-default";
        } else {
            resolved.heightBasis = "missing-rule-default";
        }
    }
    // Propagate through confirmed consecutive floor identities only. Check
    // both directions so a known upper datum can also anchor lower floors.
    for (const auto& [floor, panels] : floors) {
        const auto lower = adjacent(floor), upper = adjacent(floor + 1);
        if (!lower || !upper || input[*upper].datumConflict) continue;
        const auto& base = result[*lower];
        auto& next = result[*upper];
        if (!next.finishedMetres && base.finishedMetres && base.heightMM) {
            next.finishedMetres = *base.finishedMetres + *base.heightMM / 1000.;
            next.datumBasis = "accumulated-from-lower-floor";
        }
    }
    for (auto it = floors.rbegin(); it != floors.rend(); ++it) {
        const auto lower = adjacent(it->first), upper = adjacent(it->first + 1);
        if (!lower || !upper || input[*lower].datumConflict) continue;
        auto& base = result[*lower];
        const auto& next = result[*upper];
        if (!base.finishedMetres && next.finishedMetres && base.heightMM) {
            base.finishedMetres = *next.finishedMetres - *base.heightMM / 1000.;
            base.datumBasis = "accumulated-from-upper-floor";
        }
    }
    for (const auto& [floor, panels] : floors) {
        const auto lower = adjacent(floor), upper = adjacent(floor + 1);
        if (!lower || !upper) continue;
        auto& base = result[*lower];
        const auto& next = result[*upper];
        if (base.heightMM && base.finishedMetres && next.finishedMetres &&
            std::abs((*next.finishedMetres - *base.finishedMetres) * 1000. - *base.heightMM) > .01) {
            base.heightMM.reset();
            base.heightBasis = "conflicting-source";
        }
    }
    return result;
}
}
