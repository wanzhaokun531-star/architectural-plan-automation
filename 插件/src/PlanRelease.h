#pragma once
#include "PlanSourceInventory.h"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bcpproduct {

enum class PlanReleaseGate { mainPlan, reopened, files };

inline std::optional<PlanReleaseGate> planReleaseGateForSource(const std::string& scope) {
    if (scope == "source-dimension" || scope == "unreadable-object") return PlanReleaseGate::reopened;
    if (scope == "panel" || scope == "roof" || scope == "floor-datum" || scope == "story-height" ||
        scope == "net-domain" || scope == "room-area" || scope == "perimeter" || scope == "wall-height" ||
        scope == "wall-axis" || scope == "column-axis-X" || scope == "column-axis-Y" ||
        scope == "opening-number" || scope == "entrance" || scope == "name" || scope == "balcony" ||
        scope == "balcony-edge" || scope == "stair" || scope == "stair-flight") return PlanReleaseGate::mainPlan;
    return {};
}

struct PlanReleaseFinding {
    PlanReleaseGate gate;
    std::string scope, basis, reason, input;
    std::vector<std::string> rules;
};

struct PlanReleaseIssue {
    std::string location;
    std::vector<PlanReleaseFinding> findings;
};

struct PlanRelease {
    using Location = std::pair<std::size_t, std::string>;
    std::map<Location, PlanReleaseIssue> issues;

    void add(PlanReleaseGate gate, std::size_t panel, const std::string& source, const std::string& scope,
             const std::string& basis, const std::string& reason, const std::string& input,
             const std::string& location = {}, const std::vector<std::string>& rules = {}) {
        auto& issue = issues[{panel, source.empty() ? scope : source}];
        if (!location.empty()) issue.location = location;
        const auto duplicate = std::any_of(issue.findings.begin(), issue.findings.end(), [&](const auto& known) {
            return known.gate == gate && known.scope == scope && known.basis == basis &&
                   known.reason == reason && known.input == input && known.rules == rules;
        });
        if (!duplicate) issue.findings.push_back({gate, scope, basis, reason, input, rules});
    }

    bool passes(PlanReleaseGate gate) const {
        return std::none_of(issues.begin(), issues.end(), [&](const auto& item) {
            return std::any_of(item.second.findings.begin(), item.second.findings.end(),
                [&](const auto& finding) { return finding.gate == gate; });
        });
    }
};

struct PlanDrawingCheck {
 std::vector<std::string> rules;
 PlanReleaseGate gate;
 PlanCheck state;
 std::string basis,reason;
};

struct PlanDrawingChecks {
 using Key=PlanSourceInventory::Key;
 std::map<Key,PlanDrawingCheck> items;

 void record(std::size_t panel,const std::string& scope,const std::string& source,
             std::vector<std::string> rules,PlanReleaseGate gate,PlanCheck state,
             std::string basis,std::string reason) {
  items.insert_or_assign(Key{panel,scope,source},
   PlanDrawingCheck{std::move(rules),gate,state,std::move(basis),std::move(reason)});
 }

 bool hasOpenRule(const std::string& rule) const {
  return std::any_of(items.begin(),items.end(),[&](const auto& item){
   const auto& check=item.second;
   return (check.state==PlanCheck::pending||check.state==PlanCheck::failed)&&
    std::find(check.rules.begin(),check.rules.end(),rule)!=check.rules.end();
  });
 }

 void appendTo(PlanRelease& release) const {
  for(const auto& [key,check]:items){
   if(check.state!=PlanCheck::pending&&check.state!=PlanCheck::failed)continue;
   const auto& [panel,scope,source]=key;
   release.add(check.gate,panel,source,scope,check.basis,check.reason,"",{},check.rules);
  }
 }
};
}
