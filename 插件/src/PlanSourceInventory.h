#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace bcpproduct {
enum class PlanCheck { pending, verified, failed, omitted };

struct PlanObligation {
    std::vector<std::string> rules;
    std::string basis, reason;
    std::vector<std::string> output;
    PlanCheck check{PlanCheck::pending};
    std::string requiredProjectInput;
};

struct PlanSourceInventory {
    // Panel 0 is reserved for unassigned objects or drawing-wide requirements.
    using Key = std::tuple<std::size_t, std::string, std::string>;
    std::map<Key, PlanObligation> items;
    bool sealed{};
    std::vector<std::pair<Key,std::string>> rejectedChecks;

    void expect(std::size_t panel, const std::string& scope, const std::string& source,
                std::vector<std::string> rules, std::string basis, std::string missing) {
        if (sealed) throw std::logic_error("Source plan inventory already sealed");
        items.emplace(Key{panel, scope, source},
                      PlanObligation{std::move(rules), std::move(basis), std::move(missing)});
    }

    void check(std::size_t panel, const std::string& scope, const std::string& source,
               PlanCheck state, std::vector<std::string> output, std::string reason) {
        const Key key{panel, scope, source};
        if (!sealed) { rejectedChecks.emplace_back(key,"source-inventory-not-sealed"); return; }
        const auto item = items.find(key);
        if (item == items.end()) { rejectedChecks.emplace_back(key,"source-obligation-not-registered"); return; }
        auto& row = item->second;
        if (row.check == PlanCheck::failed) return;
        row.check = state;
        row.output = std::move(output);
        row.reason = std::move(reason);
    }

    void needsProjectInput(std::size_t panel, const std::string& scope, const std::string& source,
                           std::string input) {
        const auto item = items.find(Key{panel, scope, source});
        if (item != items.end() && item->second.check == PlanCheck::pending)
            item->second.requiredProjectInput = std::move(input);
    }
};
}
