#pragma once
#include <cmath>
#include <vector>

namespace bcpproduct {
struct PlanGuardPoint { double x,y; };
inline bool planGuardPairMatches(std::vector<PlanGuardPoint> inner,std::vector<PlanGuardPoint> outer,
                                bool closed,double width,double tolerance) {
 const auto length=[](PlanGuardPoint a,PlanGuardPoint b){return std::hypot(a.x-b.x,a.y-b.y);};
 const auto trim=[&](auto& path){if(path.size()>2&&length(path.front(),path.back())<=tolerance)path.pop_back();};
 trim(inner);trim(outer);
 if(!std::isfinite(width)||width<=tolerance||inner.size()<2||inner.size()!=outer.size())return false;
 const auto count=closed?inner.size():inner.size()-1;
 double side=0;
 for(std::size_t i=0;i<count;++i){
  const auto a=inner[i],b=inner[(i+1)%inner.size()],p=outer[i],q=outer[(i+1)%outer.size()];
  const auto span=length(a,b),other=length(p,q);
  if(span<=tolerance||other<=tolerance)return false;
  const double ux=(b.x-a.x)/span,uy=(b.y-a.y)/span;
  const auto distance=[&](PlanGuardPoint r){return ux*(r.y-a.y)-uy*(r.x-a.x);};
  const auto station=[&](PlanGuardPoint r){return ux*(r.x-a.x)+uy*(r.y-a.y);};
  const auto offset=distance(p);
  if(!std::isfinite(offset)||std::abs(std::abs(offset)-width)>tolerance||
     std::abs(distance(q)-offset)>tolerance||station(p)>tolerance||station(q)<span-tolerance||
     (side&&side*offset<=0))return false;
  side=offset;
 }
 return true;
}
}
