#pragma once
#include <array>
#include <cmath>
#include <istream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace bcpproduct {
using PlanDimensionPoint=std::array<double,2>;
struct PlanConfirmedDimension {
 int panel{};
 PlanDimensionPoint first{},second{};
 double offset{};
};
struct PlanConfirmedDimensions {
 bool sourceBound{};
 std::vector<PlanConfirmedDimension> rows;
};
inline PlanConfirmedDimensions readPlanConfirmedDimensions(std::istream& input,
 const std::string& sourceHash,double unit) {
 const auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
 require(std::isfinite(unit)&&unit>0,"invalid dimension input unit");
 std::string token;
 require(bool(input>>token),"empty confirmed dimension input");
 PlanConfirmedDimensions result;
 const auto read=[&](std::istream& row){
  PlanConfirmedDimension value;
  require(bool(row>>value.panel>>value.first[0]>>value.first[1]>>value.second[0]>>value.second[1]>>value.offset),
   "new dimension record needs panel, four coordinates and offset");
  require(value.panel>0&&std::isfinite(value.first[0])&&std::isfinite(value.first[1])&&
   std::isfinite(value.second[0])&&std::isfinite(value.second[1])&&std::isfinite(value.offset),
   "new dimension must use a positive panel and finite coordinates");
  const auto dx=std::abs(value.second[0]-value.first[0])*unit;
  const auto dy=std::abs(value.second[1]-value.first[1])*unit;
  require(std::isfinite(dx)&&std::isfinite(dy)&&std::hypot(dx,dy)>.01&&(dx<.01||dy<.01),
   "new dimension must use distinct horizontal or vertical stations");
  result.rows.push_back(value);
 };
 if(token=="PLANDIMENSIONS1"){
  std::string digest;
  require(bool(input>>digest)&&digest==sourceHash,"confirmed dimensions must match the exact saved source DWG");
  result.sourceBound=true;
  bool ended=false;
  while(input>>token){
   if(token=="END"){ended=true;break;}
   require(token=="ROW","unknown confirmed dimension record");
   read(input);
  }
  require(ended&&!(input>>token),"complete confirmed dimension input required");
 }else{
  std::ostringstream contents;
  contents<<token<<' '<<input.rdbuf();
  std::istringstream legacy(contents.str());
  while(legacy>>std::ws&&!legacy.eof())read(legacy);
 }
 require(!result.rows.empty(),"confirmed dimension input has no stations");
 return result;
}

inline bool planSameDimensionStations(const PlanDimensionPoint& first,const PlanDimensionPoint& second,
 const PlanDimensionPoint& direction,const PlanDimensionPoint& requestedFirst,
 const PlanDimensionPoint& requestedSecond,double unit) {
 const auto close=[unit](const auto& a,const auto& b){return std::hypot(a[0]-b[0],a[1]-b[1])*unit<.01;};
 const double dx=requestedSecond[0]-requestedFirst[0],dy=requestedSecond[1]-requestedFirst[1];
 const double length=std::hypot(dx,dy)*std::hypot(direction[0],direction[1]);
 return std::isfinite(length)&&length>0&&std::abs(dx*direction[1]-dy*direction[0])/length<1e-8&&
  ((close(first,requestedFirst)&&close(second,requestedSecond))||
   (close(first,requestedSecond)&&close(second,requestedFirst)));
}
}
