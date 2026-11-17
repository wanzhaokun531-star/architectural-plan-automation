#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

struct PlanVisibleWitness {
    double baseline, station, startOffset;
    bool suppressed;

    double visibleLength() const { return std::max(0.,std::abs(baseline-station)-startOffset); }
};

inline bool planWitnessesVisible(const std::vector<PlanVisibleWitness>& lines,std::size_t segments,double unit) {
    return segments>0&&lines.size()==2*segments&&std::all_of(lines.begin(),lines.end(),[&](const auto& line){
        return !line.suppressed&&std::isfinite(line.baseline)&&std::isfinite(line.station)&&
            std::isfinite(line.startOffset)&&line.visibleLength()*unit>.01;
    });
}
