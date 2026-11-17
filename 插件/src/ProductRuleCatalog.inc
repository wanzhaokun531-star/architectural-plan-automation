void productLoadRuleCatalog(ProductJob& job, bool required = false) {
    const auto folder = job.profile / L"plan-policy";
    const auto path = folder / L"rule-catalog.tsv";
    std::ifstream input(path);
    if (!input && !required) {
        job.detailPending.push_back({u8"统一规则入口", u8"项目", u8"缺当前规则目录，成果仅按兼容流程生成，需复核"});
        return;
    }
    require(bool(input), "Unified plan rule catalog is required");
    job.ruleCatalogHash = sha(path);
    job.ruleCatalog = bcpproduct::readPlanRuleCatalog(input);
    require(sha(folder / L"all-plan-rules.json") == job.ruleCatalog->sourceHash,
            "Rule catalog does not match the master compilation; synchronize the Skill before running");
    require(sha(path) == job.ruleCatalogHash, "Rule catalog changed during loading");
}

void productRecordRuleEvidence(ProductJob& job, const std::string& id, const std::string& evidence) {
    if (!job.ruleCatalog) return;
    auto& items = job.ruleEvidence[id];
    if (!items.empty()) items += ',';
    items += evidence;
}

void productCheckPlanSource(ProductJob& job, std::size_t panel, const std::string& scope,
                            const std::string& source, bcpproduct::PlanCheck check,
                            std::vector<std::string> output, std::string reason = {}) {
    // Compatibility revise/audit jobs do not claim full source-inventory coverage.
    if(!job.unifiedPlan&&!job.sourceInventory.sealed)return;
    const auto rejected=job.sourceInventory.rejectedChecks.size();
    job.sourceInventory.check(panel, scope, source, check, std::move(output), std::move(reason));
    if(job.sourceInventory.rejectedChecks.size()!=rejected)
        job.drawingChecks.record(panel,"inventory-binding",scope+"/"+source,{"QA09","QA10"},
            bcpproduct::PlanReleaseGate::reopened,bcpproduct::PlanCheck::failed,
            job.sourceInventory.rejectedChecks.back().second,u8"检查引用未登记的源应有项，覆盖关系须修复");
}

void productRecordPlanAxisFailure(ProductJob& job,std::size_t panel,const std::string& label,
                                  const std::string& handle,const std::string& error) {
    const auto reason = u8"该新轴圈未能生成或排版，保留独立轴线和有效测站。原因 " + error;
    job.detailPending.push_back({u8"轴圈局部失败",std::to_string(panel)+"/"+label+"/"+handle,reason});
    const auto row = "{\"panel\":" + std::to_string(panel) + ",\"scope\":\"axis-label-write\",\"object\":" +
        jsonUtf8(bcpproduct::decodeMessage(handle)) + ",\"label\":" + jsonUtf8(bcpproduct::decodeMessage(label)) +
        ",\"result\":\"failed-axis-label-write\",\"detail\":" + jsonUtf8(bcpproduct::decodeMessage(reason)) + "}";
    for (const auto* rule : {"A02","A03","A07","A12","A14","A28","A32"}) productRecordRuleEvidence(job,rule,row);
}

const char* productPlanCheckName(bcpproduct::PlanCheck check) {
    switch (check) {
    case bcpproduct::PlanCheck::verified: return "verified";
    case bcpproduct::PlanCheck::failed: return "failed";
    case bcpproduct::PlanCheck::omitted: return "omitted-with-evidence";
    default: return "pending";
    }
}

void productWritePlanInventory(ProductJob& job, const std::string& outputHash = {}) {
    if (!job.sourceInventory.sealed) return;
    const bool final = !outputHash.empty();
    std::ostringstream out;
    out.imbue(std::locale::classic());
    const auto q = [](const std::string& value) { return jsonUtf8(bcpproduct::decodeMessage(value)); };
    const auto array = [&](const std::vector<std::string>& values) {
        out << '[';
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (i) out << ',';
            out << q(values[i]);
        }
        out << ']';
    };
    out << "{\"schema\":\"plan.source-inventory.v1\",\"sourceSHA256\":\"" << job.sourceHash
        << "\",\"masterSHA256\":\"" << job.ruleCatalog->sourceHash << "\",\"savedReopened\":"
        << (final ? "true" : "false");
    if (final) out << ",\"outputSHA256\":\"" << outputHash << "\",\"sourceInventorySHA256\":\""
                   << job.planInventoryHash << "\",\"output\":"
                   << jsonUtf8(job.output.wstring());
    out << ",\"items\":[";
    bool first = true;
    for (const auto& [key, row] : job.sourceInventory.items) {
        const auto& [panel, scope, source] = key;
        if (!first) out << ',';
        first = false;
        out << "{\"panel\":" << panel << ",\"scope\":" << q(scope) << ",\"source\":" << q(source)
            << ",\"basis\":" << q(row.basis) << ",\"rules\":";
        array(row.rules);
        if (final) {
            out << ",\"status\":" << q(productPlanCheckName(row.check)) << ",\"objects\":";
            array(row.output);
            if(!row.requiredProjectInput.empty()&&row.check==bcpproduct::PlanCheck::pending)
                out << ",\"requiredProjectInput\":" << q(row.requiredProjectInput);
        }
        out << ",\"reason\":" << q(row.reason) << '}';
    }
    out << "]}";
    const auto path = job.root / (final ? L"plan-delivery-reconciliation.json" : L"plan-source-inventory.json");
    writeNew(path, out.str());
    if (!final) job.planInventoryHash = sha(path);
}

void productWriteRuleLedger(ProductJob& job, const std::string& outputHash,
                            const std::vector<std::array<double, 4>>& panels) {
    if (!job.ruleCatalog) return;
    const auto folder = job.profile / L"plan-policy";
    require(sha(folder / L"all-plan-rules.json") == job.ruleCatalog->sourceHash &&
            sha(folder / L"rule-catalog.tsv") == job.ruleCatalogHash,
            "Rules changed during this drawing job");
    std::ostringstream out;
    out.imbue(std::locale::classic());
    using Counts = std::array<std::size_t, 3>;
    std::map<std::string, Counts> sourceChecks;
    for (const auto& [key, row] : job.sourceInventory.items) {
        for (const auto& id : row.rules) {
            auto& count = sourceChecks[id];
            ++count[0];
            if (row.check == bcpproduct::PlanCheck::pending || row.check == bcpproduct::PlanCheck::failed) ++count[1];
            if (row.check == bcpproduct::PlanCheck::failed) ++count[2];
        }
    }
    const auto ruleState = [&](const auto& rule) -> std::string {
        const std::string status = bcpproduct::planRuleStatus(rule);
        if(rule.state=="active"&&job.drawingChecks.hasOpenRule(rule.id))
            return "drawing-evidence-with-open-items";
        const auto evidence = job.ruleEvidence.find(rule.id);
        if (status != "awaiting-drawing-evidence" || evidence == job.ruleEvidence.end()) return status;
        const auto& rows = evidence->second;
        if (rule.action == "plan-generate-applicability" &&
            rows.find("\"result\":\"not-applicable-to-plan-generate\"") != std::string::npos)
            return "not-applicable-in-plan-generate";
        const auto counts = sourceChecks.find(rule.id);
        if ((counts != sourceChecks.end() && counts->second[1]) ||
            rows.find("\"result\":\"pending") != std::string::npos ||
            rows.find("\"result\":\"unreadable") != std::string::npos ||
            rows.find("\"result\":\"missing") != std::string::npos ||
            rows.find("\"result\":\"failed") != std::string::npos)
            return "drawing-evidence-with-open-items";
        if (rows.find("\"result\":\"not-applicable-") != std::string::npos &&
            rows.find("\"result\":\"verified-") == std::string::npos)
            return "no-trigger-in-reopened-drawing";
        return "drawing-evidence-recorded";
    };
    std::map<std::string, std::size_t> statusCounts;
    for (const auto& rule : job.ruleCatalog->rules) ++statusCounts[ruleState(rule)];
    out << std::setprecision(17) << "{\"schema\":\"plan.rule-ledger.v1\",\"mode\":\"" << job.planMode
        << "\",\"engine\":\"unified-1\",\"engineSHA256\":\"" << sha(fs::path(iniPath()).replace_extension(L".arx"))
        << "\",\"sourceSHA256\":\"" << job.sourceHash << "\",\"outputSHA256\":\"" << outputHash
        << "\",\"masterSHA256\":\"" << job.ruleCatalog->sourceHash << "\",\"catalogSHA256\":\""
        << job.ruleCatalogHash << "\",\"formalReady\":" << (job.formalReady?"true":"false")
        << ",\"ruleCount\":" << job.ruleCatalog->rules.size()
        << ",\"ruleEvidenceContract\":\"per-object-and-per-scope; pending evidence is not closure\"";
    if (job.sourceInventory.sealed)
        out << ",\"sourceInventorySHA256\":\"" << job.planInventoryHash
            << "\",\"reconciliation\":\"plan-delivery-reconciliation.json\"";
    out << ",\"ruleStatusCounts\":{";
    bool first = true;
    for (const auto& [status, count] : statusCounts) {
        if (!first) out << ',';
        first = false;
        out << '"' << status << "\":" << count;
    }
    out << "},\"panels\":[";
    for (std::size_t i = 0; i < panels.size(); ++i) {
        if (i) out << ',';
        const auto& b = panels[i];
        out << "{\"panel\":" << i + 1 << ",\"bounds\":[" << b[0] << ',' << b[1] << ',' << b[2] << ',' << b[3] << "]}";
    }
    out << "],\"rules\":[";
    first = true;
    for (const auto& rule : job.ruleCatalog->rules) {
        if (!first) out << ',';
        first = false;
        const auto evidence = job.ruleEvidence.find(rule.id);
        const std::string status = ruleState(rule);
        const bool hasEvidence = status == "drawing-evidence-recorded" || status == "drawing-evidence-with-open-items" ||
                                 status == "not-applicable-in-plan-generate" || status == "no-trigger-in-reopened-drawing";
        const auto counts = sourceChecks[rule.id];
        const char* reason =
            status == "retired" ? u8"现行汇编已退役" :
            status == "paused" ? u8"现行汇编已暂停" :
            status == "needs-rule-mapping" ? u8"新增或改订的规则尚未绑定现行执行或验收动作" :
            status == "unsupported-action" ? u8"规则动作尚无本入口支持的执行或验收实现" :
            status == "awaiting-drawing-evidence" ? u8"本次未取得该规则的结果，须核适用范围或补执行、读回证据" :
            status == "drawing-evidence-with-open-items" ? u8"应有项或专项证据有未闭合项，见图幅、源对象与具体原因" :
            status == "not-applicable-in-plan-generate" ? u8"本次整图模式不适用，见专项证据" :
            status == "no-trigger-in-reopened-drawing" ? u8"已检查的范围内未触发，见专项证据" :
            u8"已登记本次专项结果，按各对象证据范围判断";
        out << "{\"id\":\"" << rule.id << "\",\"state\":\"" << rule.state
            << "\",\"revision\":\"" << rule.revision << "\",\"action\":\"" << rule.action
            << "\",\"status\":\"" << status
            << "\",\"applicability\":\"" << (hasEvidence ? "objects-recorded" : rule.state == "active" ? "undetermined" : "inactive")
            << "\",\"sourceExpected\":" << counts[0] << ",\"sourceUnresolved\":" << counts[1]
            << ",\"sourceFailed\":" << counts[2] << ",\"reason\":"
            << jsonUtf8(bcpproduct::decodeMessage(reason)) << ",\"evidence\":[";
        // An open drawing check can set the rule status before a specialized
        // producer has written a per-rule evidence row.
        if (hasEvidence && evidence != job.ruleEvidence.end()) out << evidence->second;
        out << "]}";
    }
    out << "],\"drawingChecks\":[";
    first=true;
    for(const auto& [key,check]:job.drawingChecks.items){
        if(!first)out<<',';
        first=false;
        const auto& [panel,scope,source]=key;
        out<<"{\"panel\":"<<panel<<",\"scope\":"<<jsonUtf8(bcpproduct::decodeMessage(scope))
           <<",\"source\":"<<jsonUtf8(bcpproduct::decodeMessage(source))
           <<",\"status\":\""<<productPlanCheckName(check.state)<<"\",\"basis\":"
           <<jsonUtf8(bcpproduct::decodeMessage(check.basis))<<",\"reason\":"
           <<jsonUtf8(bcpproduct::decodeMessage(check.reason))
           <<",\"gate\":\""<<(check.gate==bcpproduct::PlanReleaseGate::mainPlan?"mainPlan":
                 check.gate==bcpproduct::PlanReleaseGate::reopened?"reopened":"files")<<"\",\"rules\":[";
        for(std::size_t i=0;i<check.rules.size();++i){if(i)out<<',';out<<jsonUtf8(bcpproduct::decodeMessage(check.rules[i]));}
        out<<"]}";
    }
    out << "],\"issues\":[";
    first = true;
    for (const auto& issue : job.detailPending) {
        if (!first) out << ',';
        first = false;
        out << '[' << jsonUtf8(bcpproduct::decodeMessage(issue[0])) << ','
            << jsonUtf8(bcpproduct::decodeMessage(issue[1])) << ','
            << jsonUtf8(bcpproduct::decodeMessage(issue[2])) << ']';
    }
    out << "]}";
    writeNew(job.root / L"plan-rule-ledger.json", out.str());
}

template <typename Panel>
void productRecordRulePanels(ProductJob& job, const std::vector<Panel>& views) {
    for (const auto& view : views)
        job.rulePanels.push_back({view.bounds.xmin, view.bounds.ymin, view.bounds.xmax, view.bounds.ymax});
}
