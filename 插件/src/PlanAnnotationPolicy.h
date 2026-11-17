#pragma once
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <istream>
namespace bcpproduct {
struct PlanDimensionStylePolicy {
 std::wstring name{L"_TCH_ARCH"};
 double nominalPaperHeightMM{2.8};
};
struct PlanPaperPolicy {
 double start{18},startToThird{8},thirdToSecond{8},secondToTotal{8},totalToNear{8},circleDiameter{8};
 static constexpr double circleClearance=1, circleMaxShift=8;
 PlanDimensionStylePolicy dimensionStyle;
};
inline PlanPaperPolicy readPlanPaperPolicy(std::istream& input){
 std::string tag;PlanPaperPolicy p;
 if(!(input>>tag>>p.start>>p.startToThird>>p.thirdToSecond>>p.secondToTotal>>p.totalToNear>>p.circleDiameter)||
    tag!="BCPPLANPOLICY3")throw std::runtime_error("current executable perimeter policy (BCPPLANPOLICY3) required");
 for(double v:{p.start,p.startToThird,p.thirdToSecond,p.secondToTotal,p.totalToNear,p.circleDiameter})if(!std::isfinite(v)||v<=0)throw std::runtime_error("positive finite plan paper distances required");
 {
  std::string section,name;
  auto& dimension=p.dimensionStyle;
  if(!(input>>section>>name>>dimension.nominalPaperHeightMM)||section!="DIMSTYLE"||
     name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-$.")!=std::string::npos||
     !std::isfinite(dimension.nominalPaperHeightMM)||dimension.nominalPaperHeightMM<=0)
   throw std::runtime_error("invalid executable plan dimension style policy");
  dimension.name.assign(name.begin(),name.end());
 }
 std::string section,normal,roof,steps;
 if(!(input>>section>>normal>>roof>>steps)||section!="PERIMETER"||
    normal!="WALL_OR_OUTER_COLUMN"||roof!="EAVE_IF_INTERSECTS"||steps!="KEEP_AXIS_FOR_STEPS")
  throw std::runtime_error("invalid executable perimeter datum selection");
 std::string end;
 if(!(input>>end)||end!="END")throw std::runtime_error("invalid executable plan policy end marker");
 input>>std::ws;if(!input.eof())throw std::runtime_error("plan spacing policy trailing data");return p;
}
struct PlanEaveSegment {
 double normalStart, tangentStart, normalEnd, tangentEnd;
 std::string source;
};
struct PlanSideDatum {
 double coordinate;
 std::string eave;
};
inline PlanSideDatum resolvePerimeterDatum(double wallOrColumnAxis, int outwardSign,
        double tangentLow, double tangentHigh, double visibleStartOffset,
        const std::vector<PlanEaveSegment>& confirmedEaves) {
 PlanSideDatum selected{wallOrColumnAxis, {}};
 const double firstVisible = outwardSign * wallOrColumnAxis + visibleStartOffset;
 const double contactTolerance = visibleStartOffset * 1e-9;
 double outermost = firstVisible;
 for(const auto& edge : confirmedEaves) {
  if(std::max(edge.tangentStart,edge.tangentEnd)<tangentLow ||
     std::min(edge.tangentStart,edge.tangentEnd)>tangentHigh)continue;
  const double span=edge.tangentEnd-edge.tangentStart;
  const auto normalAt=[&](double fraction){return edge.normalStart+fraction*(edge.normalEnd-edge.normalStart);};
  const double clippedStart=span==0?0:std::clamp((tangentLow-edge.tangentStart)/span,0.,1.);
  const double clippedEnd=span==0?1:std::clamp((tangentHigh-edge.tangentStart)/span,0.,1.);
  const double outward=std::max(outwardSign*normalAt(clippedStart),outwardSign*normalAt(clippedEnd));
  if(outward<firstVisible-contactTolerance || (!selected.eave.empty()&&outward<=outermost))continue;
  selected={outwardSign*outward,edge.source};
  outermost=outward;
 }
 return selected;
}
struct PlanAnnotationLayout{double start{},third{},second{},total{},circleNear{},circleCentre{},circleDiameter{};};
inline std::optional<std::vector<double>> packPlanAxisCircles(
        const std::vector<double>& stations,double gap,double maxShift,
        double low=-std::numeric_limits<double>::infinity(),
        double high=std::numeric_limits<double>::infinity()) {
 if(stations.empty())return stations;
 const double tolerance=gap*1e-9;
 // Subtract each required inter-circle spacing to turn spacing into an
 // ordinary nondecreasing constraint. Work relative to the first station.
 const double origin=stations.front();
 low-=origin;high-=origin;
 std::vector<double> ordered;
 ordered.reserve(stations.size());
 double greatest=-std::numeric_limits<double>::infinity(), shift=0;
 for(std::size_t i=0;i<stations.size();++i){
  const double value=stations[i]-origin-i*gap;
  greatest=std::max(greatest,value);
  shift=std::max({shift,(greatest-value)/2,low-value,value-(high-(stations.size()-1)*gap)});
  ordered.push_back(value);
 }
 if(shift>maxShift+tolerance || low+(stations.size()-1)*gap>high+tolerance)return std::nullopt;
 if(shift<tolerance)return stations;
 std::vector<double> upper(ordered.size());
 double limit=high-(ordered.size()-1)*gap;
 for(std::size_t i=ordered.size();i-->0;){
  limit=std::min(limit,ordered[i]+shift);
  upper[i]=limit;
 }
 std::vector<double> packed;
 packed.reserve(ordered.size());
 for(std::size_t i=0;i<ordered.size();++i){
  low=std::max(low,ordered[i]-shift);
  if(low>upper[i]+tolerance)return std::nullopt;
  low=std::min(std::max(ordered[i],low),upper[i]);
  packed.push_back(origin+low+i*gap);
 }
 return packed;
}
inline PlanAnnotationLayout planAnnotationLayout(double scale,double drawingUnitMM,const PlanPaperPolicy& p=PlanPaperPolicy{}){
 if(!std::isfinite(scale)||!std::isfinite(drawingUnitMM)||scale<=0||drawingUnitMM<=0)throw std::invalid_argument("positive finite scale and millimetres per drawing unit required");
 const double unit=scale/drawingUnitMM,start=p.start*unit,third=start+p.startToThird*unit,second=third+p.thirdToSecond*unit,total=second+p.secondToTotal*unit,near=total+p.totalToNear*unit,diameter=p.circleDiameter*unit;
 return {start,third,second,total,near,near+diameter/2,diameter};
}
inline std::vector<double> floorElevationMetres(const std::vector<double>& storeyHeightsMM,double datumMetres){
 if(!std::isfinite(datumMetres))throw std::invalid_argument("finite floor datum required");std::vector<double> out;double value=datumMetres;
 for(const auto h:storeyHeightsMM){if(!std::isfinite(h)||h<=0)throw std::invalid_argument("positive actual storey height required");out.push_back(value);value+=h/1000;}return out;
}
inline std::wstring planFloorElevationLabel(double metres,bool firstFloor){
 if(!std::isfinite(metres))throw std::invalid_argument("finite floor elevation required");if(std::abs(metres)<0.0000001)return firstFloor?L"±0.00":L"±0.000";std::wostringstream text;text.imbue(std::locale::classic());text<<std::fixed<<std::setprecision(3)<<metres;return text.str();
}
inline bool interiorDoorJambFits(double availableDrawingUnits,double widthDrawingUnits,double drawingUnitMM){
 if(!std::isfinite(availableDrawingUnits)||!std::isfinite(widthDrawingUnits)||!std::isfinite(drawingUnitMM)||drawingUnitMM<=0||widthDrawingUnits<=0)return false;return availableDrawingUnits-widthDrawingUnits>=120/drawingUnitMM;
}
}
