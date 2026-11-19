#pragma once

#include <cmath>
#include <istream>
#include <locale>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace bcpproduct {
struct PlanRule {
    std::string id, state, action, revision;
};

struct PlanRuleCatalog {
    std::string sourceHash;
    std::vector<PlanRule> rules;
    std::optional<double> stairExtensionPaperMM;
    std::optional<double> defaultStoreyHeightMM;
};

inline bool planRuleHash(const std::string& value) {
    return value.size() == 64 && value.find_first_not_of("0123456789ABCDEF") == std::string::npos;
}

inline PlanRuleCatalog readPlanRuleCatalog(std::istream& input) {
    input.imbue(std::locale::classic());
    PlanRuleCatalog catalog;
    std::string tag;
    if (!(input >> tag >> catalog.sourceHash) || tag != "PLANRULES1" || !planRuleHash(catalog.sourceHash))
        throw std::runtime_error("Invalid plan rule catalog header");
    std::set<std::string> ids;
    bool ended = false, usesStairParameter = false;
    while (input >> tag) {
        if (tag == "END") { ended = true; break; }
        if (tag == "PARAM") {
            std::string id, action;
            double value{};
            if (!(input >> id >> action >> value) || !std::isfinite(value) || value <= 0)
                throw std::runtime_error("Invalid plan rule parameter");
            if (id == "D23" && action == "stair-extension" && !catalog.stairExtensionPaperMM && value < 100)
                catalog.stairExtensionPaperMM = value;
            else if (id == "B04" && action == "storey-height-mm" && !catalog.defaultStoreyHeightMM)
                catalog.defaultStoreyHeightMM = value;
            else throw std::runtime_error("Invalid or duplicate plan rule parameter");
            continue;
        }
        PlanRule rule;
        if (tag != "RULE" || !(input >> rule.id >> rule.state >> rule.action >> rule.revision) ||
            !ids.insert(rule.id).second || !planRuleHash(rule.revision) ||
            (rule.state != "active" && rule.state != "retired" && rule.state != "paused"))
            throw std::runtime_error("Invalid or duplicate plan rule");
        usesStairParameter |= rule.id == "D23" && rule.state == "active" && rule.action == "stair-extension";
        catalog.rules.push_back(std::move(rule));
    }
    input >> std::ws;
    if (!ended || !input.eof() || catalog.rules.empty() || usesStairParameter != catalog.stairExtensionPaperMM.has_value())
        throw std::runtime_error("Incomplete plan rule catalog");
    return catalog;
}

inline const char* planRuleStatus(const PlanRule& rule) {
    if (rule.state == "retired") return "retired";
    if (rule.state == "paused") return "paused";
    if (rule.action == "unclassified" || rule.action == "changed") return "needs-rule-mapping";
    if (rule.action == "current-plan-final-readback" &&
        std::set<std::string>{"G09","N06","L08","Y10","D23","S01","A28","O09","D31","QA11"}.count(rule.id))
        return "awaiting-drawing-evidence";
    if ((rule.id=="D30"&&rule.action=="source-dimension-replacement")||
        (rule.id=="Y10"&&rule.action=="dimension-text-style")) return "awaiting-drawing-evidence";
    if ((rule.action=="plan-host-verification"&&(rule.id=="T03"||rule.id=="Q09"))||
        (rule.action=="plan-command-evidence"&&(rule.id=="T02"||rule.id=="T04"))||
        (rule.action=="plan-diagnostics"&&rule.id=="T06")||
        (rule.action=="plan-delivery-reconciliation"&&(rule.id=="Q06"||rule.id=="Q10"||rule.id=="QA01"||
         rule.id=="QA02"||rule.id=="QA03"||rule.id=="QA06"||rule.id=="QA07"))||
        (rule.action=="detail-note-readback"&&(rule.id=="B02"||rule.id=="B03"||rule.id=="B09"||rule.id=="B10"||rule.id=="Q08"))||
        (rule.action=="deferred-project-input"&&(rule.id=="C19"||rule.id=="X01"||rule.id=="X05")))
        return "awaiting-drawing-evidence";
    if ((rule.action=="interior-net-dimension"&&rule.id=="D12")||
        (rule.action=="source-geometry-regularization"&&(rule.id=="R01"||rule.id=="R02"||rule.id=="R03"||rule.id=="R04"||rule.id=="R05"))||
        (rule.action=="source-bound-drawing-audit"&&rule.id=="TL07")||
        (rule.action=="linework-dimension-source"&&rule.id=="TL09")||
        (rule.action=="vertical-core-native"&&(rule.id=="SE04"||rule.id=="SE05"||rule.id=="SE09"||rule.id=="SE12"||
         rule.id=="SE17"||rule.id=="SE20"||rule.id=="SE21"))||
        (rule.action=="native-source-cleanup"&&(rule.id=="NC05"||rule.id=="NC06"||rule.id=="NC07"||rule.id=="NC09"||
         rule.id=="NC19"||rule.id=="NC20"))) return "awaiting-drawing-evidence";
    if ((rule.action=="axis-paper-lead-readback"&&rule.id=="A13")||
        (rule.action=="source-bound-axis-details"&&(rule.id=="A15"||rule.id=="A18"||rule.id=="A19"||rule.id=="A21"||
         rule.id=="A22"||rule.id=="A30"||rule.id=="D07"))||
        (rule.action=="opening-axis-terminal-readback"&&rule.id=="TL06")) return "awaiting-drawing-evidence";
    if ((rule.action == "publication-readback" &&
         (rule.id=="G01"||rule.id=="G02"||rule.id=="G08"||rule.id=="G09"||rule.id=="T01"||rule.id=="T05"||
          rule.id=="P04"||rule.id=="P06"||rule.id=="P07"||rule.id=="Q01")) ||
        (rule.action == "plan-input-readback" &&
         (rule.id=="G03"||rule.id=="G05"||rule.id=="P01"||rule.id=="P02"||rule.id=="T07"||rule.id=="Y03"||rule.id=="Q02")) ||
        (rule.action == "detail-input-inventory" &&
         (rule.id=="P03"||rule.id=="P04"||rule.id=="Y05"||rule.id=="B01"||rule.id=="B05"||rule.id=="B07"||
          rule.id=="B08"||rule.id=="B11"||rule.id=="C05"||rule.id=="C06"||rule.id=="C07"||rule.id=="C09"||rule.id=="C10"||rule.id=="C12"||
          rule.id=="C13"||rule.id=="C14"||rule.id=="C15"||rule.id=="C16"||rule.id=="C17"||rule.id=="C18")) ||
        (rule.action == "balcony-edge-readback" && (rule.id=="D29"||rule.id=="Q07")) ||
        (rule.action == "floor-datum-readback" && (rule.id=="B04"||rule.id=="SE02")) ||
        (rule.action == "annotation-root-readback" && rule.id=="Y02") ||
        (rule.action == "plan-generate-applicability" && (rule.id=="B06"||rule.id=="P08"||rule.id=="P09"||rule.id=="P10"||
         rule.id=="A06"||rule.id=="O08"||rule.id=="S07"||rule.id=="X02"||rule.id=="X03"||rule.id=="X04"||rule.id=="QA08"||rule.id=="G07")))
        return "awaiting-drawing-evidence";
    if ((rule.action == "interior-duplicate-omission" && rule.id == "D15") ||
        (rule.action == "column-section-dimension" && rule.id == "D28") ||
        (rule.action == "stair-flight" && (rule.id=="D20"||rule.id=="SE01"||rule.id=="SE03"||rule.id=="SE06"||
          rule.id=="SE07"||rule.id=="SE08"||rule.id=="SE10"||rule.id=="SE11"||rule.id=="SE13"||rule.id=="SE14"||
          rule.id=="SE15"||rule.id=="SE16"||rule.id=="SE18"||rule.id=="SE19")) ||
        (rule.action == "entrance-step" && (rule.id=="SE22"||rule.id=="C02"||rule.id=="C03"||rule.id=="C04"||
          rule.id=="C08"||rule.id=="C11"||rule.id=="C20"||rule.id=="TL01"||rule.id=="TL03")) ||
        (rule.action == "interior-dimension-measurement" && (rule.id=="D22"||rule.id=="Q03"||rule.id=="TL08"||rule.id=="TL10")) ||
        (rule.action == "facility-dimension-input" && rule.id=="D21") ||
        (rule.action == "roof-plan-native" && rule.id=="A29") ||
        (rule.action == "roof-perimeter-native" && rule.id=="D27") ||
        (rule.action == "perimeter-source-reconciliation" && (rule.id=="QA10"||rule.id=="D04"||rule.id=="D06")) ||
        (rule.action == "plan-mask-readback" && rule.id=="D19")) return "awaiting-drawing-evidence";
    if ((rule.action=="room-net-dimension-readback"&&rule.id=="D12")||
        (rule.action=="room-group-baseline-readback"&&rule.id=="D16")) return "awaiting-drawing-evidence";
    if ((rule.action == "axis-source-service-readback" && (rule.id=="A01"||rule.id=="A04"||rule.id=="A16"||
          rule.id=="A17"||rule.id=="A23"||rule.id=="A24"||rule.id=="A25"||rule.id=="A31")) ||
        (rule.action == "axis-side-readback" && (rule.id=="A05"||rule.id=="A20")) ||
        (rule.action == "axis-source-geometry-review" && rule.id=="A08") ||
        (rule.action == "axis-occupancy-readback" && rule.id=="A26") ||
        (rule.action == "floor-axis-registration" && rule.id=="A27")) return "awaiting-drawing-evidence";
    if (rule.action=="opening-combination-readback" &&
        (rule.id=="NC01"||rule.id=="NC02"||rule.id=="NC03"||rule.id=="NC04"||rule.id=="C09"||rule.id=="C10")) return "awaiting-drawing-evidence";
    if ((rule.action=="source-annotation-readback"&&(rule.id=="G06"||rule.id=="Y06"))||
        (rule.action=="dimension-style"&&rule.id=="G04")) return "awaiting-drawing-evidence";
    if ((rule.id == "D23" && rule.action == "stair-extension") ||
        (rule.id == "SE23" && rule.action == "stair-flight") ||
        ((rule.id=="N01"||rule.id=="N02"||rule.id=="N03"||rule.id=="N04"||rule.id=="N05"||rule.id=="TL05"||rule.id=="L06") && rule.action=="plan-names") ||
        (rule.id == "P05" && rule.action == "area-summary-font") ||
        ((rule.id=="S01"||rule.id=="S02"||rule.id=="S03"||rule.id=="S04"||rule.id=="S05"||rule.id=="S06"||rule.id=="S08"||
          rule.id=="S09"||rule.id=="S10"||rule.id=="Q05") && rule.action=="net-area") ||
        (rule.id == "D25" && rule.action == "room-dimension-layout") ||
        (rule.id == "D26" && rule.action == "projection-dimension") ||
        ((rule.id == "A14" || rule.id == "D09") && rule.action == "perimeter-placement") ||
        (rule.id == "D17" && rule.action == "dimension-text-placement") ||
        (rule.id == "D18" && rule.action == "dimension-clearance") ||
        (rule.id == "D24" && rule.action == "opening-stations") ||
        (rule.id == "Y04" && rule.action == "dimension-style") ||
        ((rule.id == "Y07" || rule.id == "Y08") && rule.action == "projection-frame") ||
        (rule.id == "QA09" && rule.action == "floor-acceptance") ||
        ((rule.id == "A02" || rule.id == "A03" || rule.id == "A07" || rule.id == "A12" ||
          rule.id == "A28") && rule.action == "axis-label-readback") ||
        ((rule.id == "A09" || rule.id == "A10" || rule.id == "A11") && rule.action == "axis-line-readback") ||
        ((rule.id == "D01" || rule.id == "D02" || rule.id == "D03" || rule.id == "D05" ||
          rule.id == "D08" || rule.id == "D10" || rule.id == "D11") && rule.action == "perimeter-chain-readback") ||
        ((rule.id == "O06" || rule.id == "TL02" || rule.id == "L04" || rule.id == "TL04" ||
          rule.id=="O01"||rule.id=="O02"||rule.id=="O03"||rule.id=="O04"||rule.id=="O05"||rule.id=="O07"||
          rule.id=="L01"||rule.id=="L02"||rule.id=="L03"||rule.id=="L05"||rule.id=="L07"||rule.id=="Y01"||rule.id=="Q04") &&
          rule.action == "opening-caption-readback") ||
        (rule.id == "A32" && rule.action == "floor-axis-registration") ||
        (rule.id == "D14" && rule.action == "bathroom-door-suppression") ||
        ((rule.id == "D13" || rule.id == "TL11") && rule.action == "door-jamb-assessment") ||
        ((rule.id == "NC08" || rule.id == "NC15" || rule.id == "NC17" || rule.id == "Y09" ||
          rule.id=="NC10"||rule.id=="NC11"||rule.id=="NC12"||rule.id=="NC13"||rule.id=="NC14"||rule.id=="NC16"||rule.id=="NC18"||
          rule.id=="QA04"||rule.id=="QA05") &&
         rule.action == "furniture-normalization") ||
        (rule.id == "C01" && rule.action == "entrance-inventory")) return "awaiting-drawing-evidence";
    return "unsupported-action";
}
}
