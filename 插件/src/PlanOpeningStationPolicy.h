#pragma once
#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace bcpproduct {

struct OpeningStationWall {
    bool vertical;
    double normal, lower, upper, thickness;
};

// Match the two terminal wall segments around a source-confirmed entry.
// Small drafting differences can identify the jambs without moving either
// wall or using its endpoint instead of the actual door edge in dimensions.
inline std::optional<std::pair<std::size_t,std::size_t>> openingSplitWallJambs(
    const std::vector<OpeningStationWall>& walls, bool vertical, double normal,
    double lower, double upper, double tolerance) {
    if (!(upper>lower) || !(tolerance>0)) return {};
    std::optional<std::size_t> before, after;
    const double middle=lower+(upper-lower)/2;
    for (std::size_t i=0;i<walls.size();++i) {
        const auto& wall=walls[i];
        if (wall.vertical!=vertical || std::abs(wall.normal-normal)>tolerance ||
            !(wall.thickness>0) || !(wall.upper>wall.lower)) continue;
        const double allowance=std::min(wall.thickness,upper-lower)/10;
        if (wall.lower<lower-tolerance && wall.upper<middle &&
            std::abs(wall.upper-lower)<=allowance) {
            if (before) return {};
            before=i;
        }
        if (wall.upper>upper+tolerance && wall.lower>middle &&
            std::abs(wall.lower-upper)<=allowance) {
            if (after) return {};
            after=i;
        }
    }
    if (!before || !after || std::abs(walls[*before].thickness-walls[*after].thickness)>tolerance)
        return {};
    return std::make_pair(*before,*after);
}

// A modelling split can be removed only after both adjacent hosts and an
// effective transverse reference have been checked against actual geometry.
inline std::optional<double> openingStationAtWallJoin(
    const OpeningStationWall& first, const OpeningStationWall& second,
    const std::vector<double>& actualOpeningEdges,
    const std::vector<OpeningStationWall>& walls,
    const std::vector<double>& effectiveAxes, double tolerance) {
    const auto& left=first.lower<second.lower?first:second;
    const auto& right=first.lower<second.lower?second:first;
    const auto close=[=](double a,double b){return std::abs(a-b)<=tolerance;};
    if(left.vertical!=right.vertical||!close(left.normal,right.normal)||
       !close(left.thickness,right.thickness)||!close(left.upper,right.lower))return {};
    const auto split=left.upper;
    if(std::any_of(actualOpeningEdges.begin(),actualOpeningEdges.end(),[&](double edge){return close(edge,split);}))return {};
    auto before=actualOpeningEdges.end(),after=actualOpeningEdges.end();
    for(auto it=actualOpeningEdges.begin();it!=actualOpeningEdges.end();++it){
        if(*it<split&&(before==actualOpeningEdges.end()||*it>*before))before=it;
        if(*it>split&&(after==actualOpeningEdges.end()||*it<*after))after=it;
    }
    if(before==actualOpeningEdges.end()||after==actualOpeningEdges.end()||
       *before<left.lower||*after>right.upper)return {};
    std::optional<double> reference;
    for(const auto& cross:walls){
        if(cross.vertical==left.vertical||cross.normal<=*before+tolerance||cross.normal>=*after-tolerance||
           !(close(cross.lower,left.normal)||close(cross.upper,left.normal)))continue;
        if(std::none_of(effectiveAxes.begin(),effectiveAxes.end(),[&](double axis){return close(axis,cross.normal);}))continue;
        if(reference&&!close(*reference,cross.normal))return {};
        reference=cross.normal;
    }
    return reference;
}

}
