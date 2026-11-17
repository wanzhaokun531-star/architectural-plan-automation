#pragma once
#include "../../阶段2/src/PlanRules.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>
namespace bcpproduct {
struct GenericBounds {double xmin{},ymin{},xmax{},ymax{};};
struct GenericView {GenericBounds bounds;std::vector<std::size_t> walls;};
inline std::vector<GenericView> discoverGenericViews(const std::vector<bcp::Wall>& walls){
 if(walls.empty())return {};
 std::vector<std::vector<std::size_t>> groups(1);groups.front().resize(walls.size());std::iota(groups.front().begin(),groups.front().end(),0);
 // Alternating projection cuts separate both stacked and side-by-side sheets.
 // A numerical tolerance is not an architectural axis merge tolerance.
 bool changed=true;while(changed){changed=false;for(int direction=0;direction<2;++direction){std::vector<std::vector<std::size_t>> split;
  for(auto group:groups){const auto range=[&](std::size_t i){const auto& w=walls[i];double a=direction?w.start.y:w.start.x,b=direction?w.end.y:w.end.x;return std::pair<double,double>{std::min(a,b),std::max(a,b)};};
   std::sort(group.begin(),group.end(),[&](auto a,auto b){return range(a)<range(b);});double high=-std::numeric_limits<double>::infinity();std::vector<std::size_t> part;
   for(auto i:group){const auto span=range(i);if(!part.empty()&&span.first>high+1e-5){split.push_back(std::move(part));part.clear();changed=true;}part.push_back(i);high=std::max(high,span.second);}if(!part.empty())split.push_back(std::move(part));
  }groups=std::move(split);
 }}
 std::vector<GenericView> views;for(auto& group:groups){GenericBounds b{1e100,1e100,-1e100,-1e100};for(auto i:group){const auto& w=walls[i];b.xmin=std::min({b.xmin,w.start.x,w.end.x});b.xmax=std::max({b.xmax,w.start.x,w.end.x});b.ymin=std::min({b.ymin,w.start.y,w.end.y});b.ymax=std::max({b.ymax,w.start.y,w.end.y});}views.push_back({b,std::move(group)});}
 std::sort(views.begin(),views.end(),[](const auto& a,const auto& b){if(std::abs(a.bounds.ymax-b.bounds.ymax)>1e-5)return a.bounds.ymax>b.bounds.ymax;return a.bounds.xmin<b.bounds.xmin;});return views;
}
inline double genericDistanceSquared(const GenericBounds& b,bcp::Point p){const double dx=std::max({b.xmin-p.x,0.,p.x-b.xmax}),dy=std::max({b.ymin-p.y,0.,p.y-b.ymax});return dx*dx+dy*dy;}
inline std::size_t nearestGenericView(const std::vector<GenericView>& views,bcp::Point p){if(views.empty())throw std::invalid_argument("no current plan component");std::size_t best=0;for(std::size_t i=1;i<views.size();++i)if(genericDistanceSquared(views[i].bounds,p)<genericDistanceSquared(views[best].bounds,p))best=i;return best;}
}
