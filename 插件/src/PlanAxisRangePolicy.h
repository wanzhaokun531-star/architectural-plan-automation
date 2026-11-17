#pragma once
#include <cmath>
#include <functional>
#include <istream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace bcpproduct {
enum class AxisBoundKind { minimumOutside, maximumOutside, point };
struct AxisRangeBound {AxisBoundKind kind{};std::string entity;int grip{};};
struct AxisRangeRule {std::string axis;int panel{};std::string role;AxisRangeBound lower,upper;};
struct AxisRangeSpan {double lower{},upper{};};

inline AxisRangeBound readAxisRangeBound(std::istream& input){
 std::string tag;AxisRangeBound b;
 if(!(input>>tag>>b.entity>>b.grip))throw std::runtime_error("incomplete axis range boundary");
 if(tag=="POINT"&&b.entity!="-"&&b.grip>=0)b.kind=AxisBoundKind::point;
 else if((tag=="MIN"||tag=="MAX")&&b.entity=="-"&&b.grip==0)b.kind=tag=="MIN"?AxisBoundKind::minimumOutside:AxisBoundKind::maximumOutside;
 else throw std::runtime_error("invalid axis range boundary");
 return b;
}
inline std::map<std::string,AxisRangeRule> readAxisRangeRules(std::istream& input){
 std::string tag;if(!(input>>tag)||tag!="BCPAXISRANGES1")throw std::runtime_error("axis range configuration header");
 const std::set<std::string> roles{"main","wall","column","local","retained"};std::map<std::string,AxisRangeRule> rules;bool complete=false;
 while(input>>tag){if(tag=="END"){complete=true;break;}AxisRangeRule r;if(tag!="AXIS"||!(input>>r.axis>>r.panel>>r.role)||r.panel<0||!roles.count(r.role))throw std::runtime_error("invalid axis service association");r.lower=readAxisRangeBound(input);r.upper=readAxisRangeBound(input);if(!rules.emplace(r.axis,r).second)throw std::runtime_error("duplicate axis range association");}
 input>>std::ws;if(!complete||!input.eof())throw std::runtime_error("incomplete axis range configuration or trailing records");return rules;
}
// Missing project references are a per-axis pending item. There is deliberately
// no fallback to the whole building envelope.
inline std::optional<AxisRangeSpan> resolveAxisRange(
 const std::optional<AxisRangeRule>& rule,double minimum,double maximum,double outside,
 const std::function<std::optional<double>(const std::string&,int)>& point){
 if(!rule)return {};
 if(!std::isfinite(minimum)||!std::isfinite(maximum)||!std::isfinite(outside)||minimum>=maximum||outside<0)throw std::invalid_argument("finite ordered axis frame required");
 const auto value=[&](const AxisRangeBound& b)->std::optional<double>{
  if(b.kind==AxisBoundKind::minimumOutside)return minimum-outside;
  if(b.kind==AxisBoundKind::maximumOutside)return maximum+outside;
  return point(b.entity,b.grip);
 };
 const auto a=value(rule->lower),b=value(rule->upper);
 if(!a||!b||!std::isfinite(*a)||!std::isfinite(*b)||*a>=*b)return {};
 return AxisRangeSpan{*a,*b};
}
}
