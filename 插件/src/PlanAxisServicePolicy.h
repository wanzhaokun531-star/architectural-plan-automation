#pragma once
#include "PlanAxisRangePolicy.h"
#include <algorithm>
#include <limits>
#include <string_view>
#include <tuple>
#include <vector>
namespace bcpproduct {
// Coordinates are current native geometry, not project labels or handles.
struct AxisServiceWall {bool vertical{};double coordinate{},lower{},upper{},halfThickness{};};
struct AxisServiceColumn {double x{},y{},halfX{},halfY{};};
struct AxisServiceRange {std::string role;AxisRangeSpan span;};
struct AxisSideService {
 AxisRangeSpan span;
 bool negative{},positive{};
};

struct AxisRegistrationPoint {double x{},y{};};
struct AxisFloorRegistration {AxisRegistrationPoint shift;std::size_t matches{};double maxResidual{};};

inline std::optional<int> explicitPlanFloor(std::wstring_view title){
 const auto first=title.find_first_not_of(L" \t\r\n　");
 if(first==std::wstring_view::npos)return {};
 const auto last=title.find_last_not_of(L" \t\r\n　");
 title=title.substr(first,last-first+1);
 if(title==L"首层平面图")return 1;
 constexpr std::wstring_view suffix=L"层平面图";
 if(title.size()<=suffix.size()||title.substr(title.size()-suffix.size())!=suffix)return {};
 const auto ordinal=title.substr(0,title.size()-suffix.size());
 constexpr std::wstring_view numerals=L"一二三四五六七八九十";
 if(ordinal.size()==1){
  const auto match=numerals.find(ordinal.front());
  if(match!=std::wstring_view::npos)return static_cast<int>(match)+1;
 }
 int value=0;
 for(const auto digit:ordinal){
  if(digit<L'0'||digit>L'9'||value>99)return {};
  value=value*10+static_cast<int>(digit-L'0');
 }
 if(value>0)return value;
 return {};
}

inline std::vector<AxisRegistrationPoint> axisFloorIntersections(const std::vector<AxisServiceWall>& walls){
 constexpr double tolerance=1e-5;
 std::vector<AxisRegistrationPoint> points;
 for(const auto& vertical:walls){
  if(!vertical.vertical)continue;
  for(const auto& horizontal:walls){
   if(horizontal.vertical)continue;
   // Two actual joined wall ends are a stronger tie than unrelated projected
   // axes or the bounding rectangle of a floor.
   const auto point=AxisRegistrationPoint{vertical.coordinate,horizontal.coordinate};
   if(std::min(std::abs(point.x-horizontal.lower),std::abs(point.x-horizontal.upper))>tolerance||
      std::min(std::abs(point.y-vertical.lower),std::abs(point.y-vertical.upper))>tolerance)continue;
   if(std::none_of(points.begin(),points.end(),[&](const auto& known){
    return std::abs(known.x-point.x)<=tolerance&&std::abs(known.y-point.y)<=tolerance;
   }))points.push_back(point);
  }
 }
 return points;
}

inline std::optional<AxisFloorRegistration> registerAxisFloor(
 const std::vector<AxisRegistrationPoint>& lower,std::vector<AxisRegistrationPoint> upper,double tolerance){
 if(lower.size()<3||upper.size()<3||!std::isfinite(tolerance)||tolerance<=0)return {};
 std::sort(upper.begin(),upper.end(),[](const auto& a,const auto& b){return std::tie(a.x,a.y)<std::tie(b.x,b.y);});
 std::optional<AxisFloorRegistration> best;
 bool ambiguous=false;
 const auto matching=[&](AxisRegistrationPoint shift){
  std::vector<std::pair<AxisRegistrationPoint,AxisRegistrationPoint>> matches;
  for(const auto& point:lower){
   const auto x=point.x+shift.x;
   const auto y=point.y+shift.y;
   auto match=std::lower_bound(upper.begin(),upper.end(),x-tolerance,
                              [](const auto& target,double limit){return target.x<limit;});
   auto closest=upper.end();
   double bestDistance=tolerance;
   for(;match!=upper.end()&&match->x<=x+tolerance;++match){
    const auto distance=std::hypot(match->x-x,match->y-y);
    if(distance<=bestDistance){closest=match;bestDistance=distance;}
   }
   if(closest!=upper.end()&&std::none_of(matches.begin(),matches.end(),[&](const auto& pair){
    return pair.second.x==closest->x&&pair.second.y==closest->y;
   }))matches.emplace_back(point,*closest);
  }
  return matches;
 };
 for(const auto& source:lower)for(const auto& target:upper){
  const AxisRegistrationPoint proposed{target.x-source.x,target.y-source.y};
  if(best&&std::hypot(proposed.x-best->shift.x,proposed.y-best->shift.y)<=1e-5)continue;
  const auto matches=matching(proposed);
  if(matches.size()<3||(best&&matches.size()<best->matches))continue;
  const auto& first=matches.front().first;
  const auto farthest=std::max_element(matches.begin(),matches.end(),[&](const auto& a,const auto& b){
   return std::hypot(a.first.x-first.x,a.first.y-first.y)<std::hypot(b.first.x-first.x,b.first.y-first.y);
  });
  const auto dx=farthest->first.x-first.x,dy=farthest->first.y-first.y;
  const auto length=std::hypot(dx,dy);
  if(length<=tolerance||std::none_of(matches.begin(),matches.end(),[&](const auto& pair){
   return std::abs(dx*(pair.first.y-first.y)-dy*(pair.first.x-first.x))>tolerance*length;
  }))continue;
  AxisFloorRegistration candidate;
  candidate.matches=matches.size();
  for(const auto& pair:matches){
   candidate.shift.x+=(pair.second.x-pair.first.x)/candidate.matches;
   candidate.shift.y+=(pair.second.y-pair.first.y)/candidate.matches;
  }
  for(const auto& pair:matches){
   candidate.maxResidual=std::max(candidate.maxResidual,
       std::hypot(pair.second.x-pair.first.x-candidate.shift.x,pair.second.y-pair.first.y-candidate.shift.y));
  }
  if(candidate.maxResidual>tolerance)continue;
  if(!best||candidate.matches>best->matches){best=candidate;ambiguous=false;}
  else if(std::hypot(candidate.shift.x-best->shift.x,candidate.shift.y-best->shift.y)>tolerance)ambiguous=true;
 }
 return ambiguous?std::nullopt:best;
}

inline std::optional<AxisSideService> resolveAxisSideService(bool vertical,double station,
 const std::vector<AxisServiceWall>& walls,const std::vector<AxisServiceColumn>& columns){
 constexpr double tolerance=1e-5;
 const auto infinity=std::numeric_limits<double>::infinity();
 double lower=infinity,upper=-infinity;
 double columnLower=infinity,columnUpper=-infinity;
 double negativeFace=infinity,positiveFace=-infinity;
 double negativeHalf=0,positiveHalf=0;
 for(const auto& wall:walls){
  if(wall.vertical==vertical){
   if(std::abs(wall.coordinate-station)<=tolerance){
    lower=std::min(lower,wall.lower);
    upper=std::max(upper,wall.upper);
   }
   continue;
  }
  if(station<wall.lower-tolerance||station>wall.upper+tolerance)continue;
  if(wall.coordinate<negativeFace-tolerance){
   negativeFace=wall.coordinate;
   negativeHalf=wall.halfThickness;
  }else if(std::abs(wall.coordinate-negativeFace)<=tolerance){
   negativeHalf=std::max(negativeHalf,wall.halfThickness);
  }
  if(wall.coordinate>positiveFace+tolerance){
   positiveFace=wall.coordinate;
   positiveHalf=wall.halfThickness;
  }else if(std::abs(wall.coordinate-positiveFace)<=tolerance){
   positiveHalf=std::max(positiveHalf,wall.halfThickness);
  }
 }
 for(const auto& column:columns){
  if(std::abs((vertical?column.x:column.y)-station)>tolerance)continue;
  const auto centre=vertical?column.y:column.x;
  const auto half=vertical?column.halfY:column.halfX;
  lower=std::min(lower,centre);
  upper=std::max(upper,centre);
  columnLower=std::min(columnLower,centre-half);
  columnUpper=std::max(columnUpper,centre+half);
 }
 if(lower>upper)return {};
 AxisSideService result{{lower,upper}};
 if(upper-lower<=tolerance){
  result.span={columnLower,columnUpper};
 }
 // The nearest real outer crossing at this station may be a recessed wall.
 // An axis can meet that wall within its thickness or serve a column beyond
 // it. A short interior axis does not acquire that side through proximity.
 if(positiveFace-negativeFace>tolerance){
  result.negative=lower<=negativeFace+negativeHalf+tolerance;
  result.positive=upper>=positiveFace-positiveHalf-tolerance;
 }
 return result;
}

inline std::optional<AxisServiceRange> inferAxisService(bool vertical,double coordinate,
 double minimum,double maximum,double outside,const std::vector<AxisServiceWall>& walls,
 const std::vector<AxisServiceColumn>& columns){
 constexpr double eps=1e-5;
 if(!std::isfinite(coordinate)||!std::isfinite(minimum)||!std::isfinite(maximum)||!std::isfinite(outside)||minimum>=maximum||outside<0)return {};
 double lo=std::numeric_limits<double>::infinity(),hi=-lo;
 bool hasWall=false,hasColumn=false;double crossHalf=0;int columnCount=0;
 for(const auto& w:walls)if(w.vertical==vertical&&std::abs(w.coordinate-coordinate)<eps&&w.upper>w.lower){
  lo=std::min(lo,w.lower);hi=std::max(hi,w.upper);hasWall=true;
 }
 for(const auto& c:columns)if(std::abs((vertical?c.x:c.y)-coordinate)<eps){
  const auto point=vertical?c.y:c.x;lo=std::min(lo,point);hi=std::max(hi,point);hasColumn=true;
  crossHalf=std::max(crossHalf,vertical?c.halfX:c.halfY);++columnCount;
 }
 if(!hasWall&&!hasColumn)return {};
 // Eccentric column axes connect to an actual transverse wall at the junction.
 // The real half-section can meet its endpoint without merging logical axes.
 if(!hasWall){
  double below=-std::numeric_limits<double>::infinity(),above=-below;
  for(const auto& w:walls)if(w.vertical!=vertical&&coordinate>=w.lower-crossHalf-eps&&coordinate<=w.upper+crossHalf+eps){
   if(w.coordinate<lo-eps)below=std::max(below,w.coordinate);
   if(w.coordinate>hi+eps)above=std::min(above,w.coordinate);
  }
  if(hi<=minimum+eps&&std::isfinite(above))hi=above;
  else if(lo>=maximum-eps&&std::isfinite(below))lo=below;
  else if(hi-lo<eps){
   // An interior column still has an actual local setting-out axis. Extend
   // toward the nearest existing transverse wall, never the whole envelope.
   if(std::isfinite(below)&&std::isfinite(above)){if(lo-below<=above-hi)lo=below;else hi=above;}
   else if(std::isfinite(below))lo=below;
   else if(std::isfinite(above))hi=above;
   else return {};
  }
 }
 if(hi-lo<eps)return {};
 // A wall confined to one outer half projects outward from its inner service
 // boundary. It must not leave a geometric gap before the external short lead.
 // The opposite inner boundary stays at the actual wall junction.
 const double middle=(minimum+maximum)/2;
 if(hasWall&&lo>middle)hi=maximum;
 if(hasWall&&hi<middle&&lo<=minimum+eps)lo=minimum;
 // An established row through multiple actual columns is a shared setting-out
 // axis, distinct from a single porch-column axis ending at the body junction.
 if(!hasWall&&columnCount>1){lo=minimum;hi=maximum;}
 if(lo<=minimum+eps)lo=minimum-outside;
 if(hi>=maximum-eps)hi=maximum+outside;
 return AxisServiceRange{hasWall?(lo<minimum&&hi>maximum?"main":"wall"):"column",{lo,hi}};
}
}
