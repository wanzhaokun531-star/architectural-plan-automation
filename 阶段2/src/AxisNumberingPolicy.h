#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace bcp {

// Assign supplemental numbers only within an established pair of primary axes.
inline std::vector<std::string> orderedSupplementalAxisNumbers(
    double lower, double upper, const std::string& primary,
    const std::vector<double>& stations, double precision) {
    if(!std::isfinite(lower) || !std::isfinite(upper) || lower>=upper ||
       primary.empty() || !std::isfinite(precision) || precision<=0)
        throw std::invalid_argument("invalid primary axis interval");
    std::vector<std::pair<double,std::size_t>> ordered;
    for(std::size_t i=0;i<stations.size();++i){
        if(!std::isfinite(stations[i]) || stations[i]<=lower+precision || stations[i]>=upper-precision)
            throw std::invalid_argument("supplemental axis falls outside its primary interval");
        ordered.emplace_back(stations[i],i);
    }
    std::sort(ordered.begin(),ordered.end());
    std::vector<std::string> labels(stations.size());
    double previous=lower;
    for(std::size_t i=0;i<ordered.size();++i){
        if(ordered[i].first-previous<=precision)
            throw std::invalid_argument("coincident axes must resolve their logical relationship before numbering");
        labels[ordered[i].second]=std::to_string(i+1)+"/"+primary;
        previous=ordered[i].first;
    }
    return labels;
}

}
