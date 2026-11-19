#pragma once
#include <cmath>
#include <istream>
#include <locale>
#include <map>
#include <stdexcept>
#include <string>

namespace bcpproduct {
struct PlanTypographySpec {
    std::string name, font, bigFont;
    double paperHeight{}, width{}, fixedHeight{};
};
struct PlanTypographyPolicy {
    std::string sourceHash;
    std::map<std::string, PlanTypographySpec> styles;
    const PlanTypographySpec& at(const std::string& name) const { return styles.at(name); }
};
inline PlanTypographyPolicy readPlanTypographyPolicy(std::istream& input) {
    input.imbue(std::locale::classic());
    PlanTypographyPolicy result;
    std::string tag;
    if (!(input >> tag >> result.sourceHash) || tag != "PLANFONTS1" ||
        result.sourceHash.size() != 64 || result.sourceHash.find_first_not_of("0123456789ABCDEF") != std::string::npos)
        throw std::runtime_error("Invalid current typography contract identity");
    bool end = false;
    while (input >> tag) {
        if (tag == "END") { end = true; break; }
        PlanTypographySpec spec;
        if (tag != "STYLE" || !(input >> spec.name >> spec.font >> spec.bigFont >> spec.paperHeight >> spec.width >> spec.fixedHeight) ||
            !std::isfinite(spec.paperHeight) || !std::isfinite(spec.width) || !std::isfinite(spec.fixedHeight) ||
            spec.paperHeight <= 0 || spec.width <= 0 || spec.fixedHeight < 0 ||
            !result.styles.emplace(spec.name, spec).second)
            throw std::runtime_error("Invalid or duplicate current typography style");
    }
    input >> std::ws;
    if (!end || !input.eof() || result.styles.size() != 3 || !result.styles.count("FY_HZ") ||
        !result.styles.count("_TCH_DIM") || !result.styles.count("_TCH_WINDOW"))
        throw std::runtime_error("Incomplete current typography contract; no legacy fallback");
    return result;
}
}
