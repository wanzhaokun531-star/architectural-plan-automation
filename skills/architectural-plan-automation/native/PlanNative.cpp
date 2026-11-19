#define acrxEntryPoint unusedPlanProductEntry
#define PLAN_STANDALONE_ENTRY
#include <AcPlPlotFactory.h>
#include <AcPlPlotEngine.h>
#include <AcPlPlotInfo.h>
#include <AcPlPlotInfoValidator.h>
#include <AcPlPlotConfigMgr.h>
#include <dbplotsettings.h>
#include <dblayout.h>
#include <dbplotsetval.h>
#include <AcDbLMgr.h>
#include <cwchar>
#include <cctype>
#include <textengine.h>
#include <acgiutil.h>
#include <dbtable.h>
#include "../../../插件/src/BuildingPlanPlugin2020.cpp"
#undef acrxEntryPoint
#undef PLAN_STANDALONE_ENTRY
#include "../../../插件/src/PlanConfirmedDimensions.h"

namespace {
using InkSegment = std::pair<AcGePoint3d, AcGePoint3d>;

void readDrawing(AcDbDatabase& db, const fs::path& path) {
    status(db.readDwgFile(path.c_str(), AcDbDatabase::kForReadAndAllShare, false), "open saved DWG");
    status(db.closeInput(true), "close DWG input");
}

// Exercise the installed Tianzheng commands on an owned, freshly opened copy.
// The trial drawing is closed without saving any edits.
bool bcpLoaded();

void verifyAxisEditing(const fs::path& source,const fs::path& report,const fs::path& expectation){
    isolated(ini(iniPath(),L"nonce"));
    require(curDoc()&&fs::equivalent(source,curDoc()->fileName())&&!bcpLoaded(),
            "axis command trial requires the owned, freshly opened drawing copy");
    auto& db=*curDoc()->database();
    Working working(&db);
    ApprovedCommandVars vars;
    const auto original=sha(source);
    struct Placement{int panel;double low,high;};
    std::map<std::string,Placement> placements;
    if(!expectation.empty()){
        std::ifstream input(expectation);
        std::string digest,handle;
        require(bool(input>>digest)&&digest==original,"axis edit expectations must match the reopened DWG");
        Placement row;
        while(input>>handle>>row.panel>>row.low>>row.high)placements.emplace(handle,row);
        require(input.eof()&&!placements.empty(),"axis edit expectation rows");
    }
    const auto module=fs::path(productSetting(L"tangentDirectory",L"C:\\Tangent\\TArchT20V8\\SYS23x64"))/L"tch_kerncmd.arx";
    if(!acedRegCmds->lookupGlobalCmd(L"TReAxisNo"))
        require(acrxDynamicLinker->loadModule(module.c_str(),false,false),"load native axis commands for edit trial");
    struct Label{std::string handle,number;bool vertical;};
    std::vector<Label> labels;
    std::set<std::string> labelHandles;
    const auto numbers=productAxisOccupancy(db);
    for(const auto& [raw,type]:modelTypes(db)){
        if(type!=L"TDbAxisLabelSet")continue;
        const auto handle=ascii(raw);
        const auto id=find(db,handle);
        const auto entity=open<AcDbEntity>(id);
        if(!productPlanDimensionVisible(*entity))continue;
        const auto grips=chainGrips(id);
        const auto number=std::find_if(numbers.begin(),numbers.end(),[&](const auto& row){return row.handle==handle;});
        if((grips.size()!=4&&grips.size()!=8)||number==numbers.end()||number->numbers.size()!=1)continue;
        const auto& first=grips[0];
        const auto& centre=grips[1];
        labels.push_back({handle,*number->numbers.begin(),std::abs(centre.x-first.x)<1e-5});
        labelHandles.insert(handle);
    }
    const auto protectedBefore=wholeProtected(db,labelHandles);
    std::ostringstream output;
    output<<"{\"savedReopened\":true,\"bcpLoaded\":false,\"cases\":[";
    bool first=true;
    for(const auto& label:labels){
        const auto id=find(db,label.handle);
        const auto before=chainGrips(id);
        const auto contents=[&](const std::string& number){
            int text=0,circle=0;
            const auto entity=open<AcDbEntity>(id);
            readAxisLeaves(*entity,number,0,text,circle);
            return text==static_cast<int>(chainGrips(id).size()/4)&&circle==text;
        };
        const auto command=[&](const wchar_t* name,const wchar_t* value,std::size_t end=0){
            const auto grips=chainGrips(id);
            const auto radius=grips.at(end+1).distanceTo(grips.at(end+2));
            const auto pick=grips[end+1]+(label.vertical?AcGeVector3d(radius,0,0):AcGeVector3d(0,radius,0));
            approvedCommandPoint(name,pick,value);
            require(acedCommandS(RTSTR,L"_.REGEN",RTNONE)==RTNORM,"refresh axis command");
        };
        const std::string temporary=label.vertical?(label.number=="91"?"92":"91"):(label.number=="Y"?"X":"Y");
        const auto temp=bcpproduct::decodeMessage(temporary),restore=bcpproduct::decodeMessage(label.number);
        bool changed=false,restored=false,cycled=false,geometryRestored=false,placementVerified=false;
        std::vector<std::vector<AcGePoint3d>> modes;
        std::wstring error;
        try{
            command(L"TReAxisNo",temp.c_str(),before.size()==8?4:0);
            changed=contents(temporary)&&approvedgrips::same(chainGrips(id),before);
            command(L"TReAxisNo",restore.c_str());
            restored=contents(label.number)&&approvedgrips::same(chainGrips(id),before);
            for(int count=0;count<4;++count){
                command(L"TObjEdit",L"M");
                modes.push_back(chainGrips(id));
                if(approvedgrips::same(modes.back(),before)){
                    geometryRestored=contents(label.number);
                    break;
                }
                cycled|=modes.back().size()!=before.size()||
                        modes.back().at(1).distanceTo(before.at(1))>1e-5;
            }
        }catch(const std::exception& exception){error=bcpproduct::decodeMessage(exception.what());}
        const auto found=placements.find(label.handle);
        if(found!=placements.end()){
            const auto full=std::find_if(modes.begin(),modes.end(),[](const auto& state){return state.size()==8;});
            if(full!=modes.end()){
                placementVerified=true;
                for(std::size_t i=0;i<8;i+=4){
                    const auto lead=productAxisNormal((*full)[i],label.vertical);
                    const auto centre=productAxisNormal((*full)[i+1],label.vertical);
                    const double sign=centre>lead?1.:-1.;
                    const auto datum=sign>0?found->second.high:found->second.low;
                    placementVerified&=std::abs(centre-(datum+sign*5400.))<.01&&
                        std::abs(lead-(datum+sign*1800.))<.01&&
                        std::abs(productAxisStation((*full)[i],label.vertical)-
                                 productAxisStation(before[0],label.vertical))<.01;
                }
            }
        }
        if(!first)output<<',';
        first=false;
        output<<"{\"object\":\""<<label.handle<<"\",\"label\":\""<<label.number
              <<"\",\"panel\":"<<(found==placements.end()?0:found->second.panel)
              <<",\"bothEndPlacementVerified\":"<<(placementVerified?"true":"false")
              <<",\"initialEnds\":"<<before.size()/4<<",\"renumbered\":"<<(changed?"true":"false")
              <<",\"numberRestored\":"<<(restored?"true":"false")<<",\"sideCycled\":"<<(cycled?"true":"false")
              <<",\"geometryRestored\":"<<(geometryRestored?"true":"false")<<",\"before\":"<<wholePoints(before)
              <<",\"modes\":[";
        for(std::size_t i=0;i<modes.size();++i){if(i)output<<',';output<<wholePoints(modes[i]);}
        output<<"],\"error\":"<<jsonUtf8(error)<<'}';
    }
    const bool preserved=bcpproduct::samePublicGeometry(protectedBefore,wholeProtected(db,labelHandles));
    require(sha(source)==original&&!bcpLoaded(),"owned axis edit changed saved input or loaded BCP");
    writeNew(report/L"axis-edit-readback.json",output.str()+"],\"protectedGeometryPreserved\":"+
        (preserved?"true":"false")+",\"savedInputUnchanged\":true}");
}

void writePlanRegressionFixture(const fs::path& source,const fs::path& output,const fs::path& report,const fs::path& previousDwg){
    isolated(ini(iniPath(),L"nonce"));
    require(curDoc()&&!fs::exists(output)&&fs::equivalent(source,curDoc()->fileName())&&
            !GetModuleHandleW(L"BuildingPlanPlugin2020.arx")&&!acedRegCmds->lookupGlobalCmd(L"BCP"),
            "fixture must use the open, owned input copy and a fresh output");
    const auto original=sha(source);
    AcDbDatabase db(false,true);
    readDrawing(db,source);
    Working working(&db);
    const auto plain=productAppendPlanDetail(db,std::make_unique<AcDbRotatedDimension>(0.,
       AcGePoint3d(280000,18500,0),AcGePoint3d(281200,18500,0),AcGePoint3d(280000,19000,0),nullptr,db.dimstyle()));
    const auto overridden=productAppendPlanDetail(db,std::make_unique<AcDbRotatedDimension>(0.,
       AcGePoint3d(280000,19050,0),AcGePoint3d(281200,19050,0),AcGePoint3d(280000,19600,0),L"900",db.dimstyle()));
    const auto makeLine=[&](){
        auto line=std::make_unique<AcDbLine>(AcGePoint3d(276600,14250,0),AcGePoint3d(277100,14250,0));
        line->setDatabaseDefaults(&db);
        return productAppendPlanDetail(db,std::move(line));
    };
    const auto keeper=makeLine(),duplicate=makeLine();
    auto lift=std::make_unique<AcDbText>();
    lift->setDatabaseDefaults(&db);
    status(lift->setPosition({284500,18500,0}),"place isolated lift test label");
    status(lift->setHeight(200),"lift test label height");
    status(lift->setTextString(L"电梯"),"lift test label content");
    const auto liftLabel=productAppendPlanDetail(db,std::move(lift));
    std::string duplicateDimension;
    if(!previousDwg.empty()){
        AcDbDatabase donor(false,true);
        readDrawing(donor,previousDwg);
        const auto sample=find(donor,"A175");
        require(std::wstring(open<AcDbEntity>(sample)->isA()->name())==L"TDbDimension2","fixture requires a native dimension chain");
        duplicateDimension=axisHandle(cloneOne(donor,sample,db));
    }
    status(db.saveAs(output.c_str(),false,AcDb::kDHL_CURRENT),"save owned positive fixture");
    AcDbDatabase saved(false,true);
    readDrawing(saved,output);
    const auto check=[&](AcDbObjectId id){
        const auto reopened=find(saved,axisHandle(id));
        require(!reopened.isNull(),"fixture actual object did not reopen");
        return axisHandle(id);
    };
    require(sha(source)==original,"fixture source changed");
    writeNew(report/L"plan-native-result.json","{\"completed\":true,\"reopened\":true,\"bcpLoadedBefore\":false,\"bcpLoadedAfter\":false,\"outputSHA256\":\""+
      sha(output)+"\",\"ordinary\":\""+check(plain)+"\",\"overridden\":\""+check(overridden)+
      "\",\"keeper\":\""+check(keeper)+"\",\"duplicate\":\""+check(duplicate)+"\",\"liftLabel\":\""+check(liftLabel)+
      "\",\"duplicateDimension\":\""+duplicateDimension+"\"}");
}

bool bcpLoaded() {
    return GetModuleHandleW(L"BuildingPlanPlugin2020.arx") ||
           acedRegCmds->lookupGlobalCmd(L"BCP") || acedRegCmds->lookupGlobalCmd(L"BCP_BUILD");
}

void appendInk(const AcDbEntity& entity, std::vector<InkSegment>& strokes, int depth = 0) {
    if (depth > 7 || entity.visibility() != AcDb::kVisible || AcDbText::cast(&entity) ||
        AcDbMText::cast(&entity) || AcDbHatch::cast(&entity)) return;
    if (const auto* curve = AcDbCurve::cast(&entity)) {
        double lo{}, hi{};
        AcGePoint3d a;
        if (curve->getStartParam(lo) != Acad::eOk || curve->getEndParam(hi) != Acad::eOk ||
            curve->getPointAtParam(lo, a) != Acad::eOk) return;
        const int pieces = AcDbLine::cast(&entity) ? 1 : 32;
        for (int i = 1; i <= pieces; ++i) {
            AcGePoint3d b;
            if (curve->getPointAtParam(lo + (hi - lo) * i / pieces, b) != Acad::eOk) break;
            strokes.emplace_back(a, b);
            a = b;
        }
        return;
    }
    TemplateParts parts(&entity);
    if (entity.explode(parts.values) != Acad::eOk) return;
    for (int i = 0; i < parts.values.length(); ++i)
        if (const auto* child = AcDbEntity::cast(static_cast<AcRxObject*>(parts.values[i])))
            appendInk(*child, strokes, depth + 1);
}

bool inPanel(const AcGePoint3d& point, const ProductPlanBounds& box) {
    return point.x >= box.xmin && point.x <= box.xmax && point.y >= box.ymin && point.y <= box.ymax;
}

#include "PlanNativeRooms.inc"
#include "PlanNativeProjection.inc"

struct PlanNativeDimension {
    std::string handle;
    int panel;
    bool vertical;
    bool stock;
    bool added;
    std::vector<AcGePoint3d> grips;
    std::vector<double> values;
    std::string sourceHandle;
    std::optional<PlanProjection> projection;
    bool lockBaseline{};
    bool alternateStationSide{};
};

#include "PlanNativeConfirmedDimensions.inc"
#include "../../../插件/src/PlanDimensionWitness.h"
std::vector<PlanVisibleWitness> readPlanVisibleWitnesses(AcDbObjectId,bool);
#include "PlanNativeLayout.inc"
#include "PlanNativeFullDimensions.inc"

void verifyRetainedPlanAxes(const fs::path& sourcePath, AcDbDatabase& saved,
    const std::vector<bcpproduct::GenericView>& views, const fs::path& report) {
    struct AxisRecord {
        std::wstring kind, layer;
        AcDb::Visibility visibility;
        std::vector<AcGePoint3d> points;
        std::vector<std::wstring> text;
    };
    const auto snapshot=[](AcDbDatabase& drawing) {
        Working working(&drawing);
        std::map<std::string,AxisRecord> result;
        for(const auto& [handle,type]:modelTypes(drawing)){
            if(type!=L"AcDbLine" && type!=L"TDbAxisLabelSet")continue;
            const auto id=find(drawing,ascii(handle));
            const auto entity=open<AcDbEntity>(id);
            const auto layer=symbolName(entity->layerId());
            std::vector<AcGePoint3d> points;
            std::vector<std::wstring> text;
            if(const auto* line=AcDbLine::cast(entity.get())){
                if(layer!=L"DOTE" && layer!=L"AXIS")continue;
                points={line->startPoint(),line->endPoint()};
            }else{
                points=chainGrips(id);
                productOpeningTexts(*entity,text);
            }
            result.emplace(ascii(handle),AxisRecord{type,layer,entity->visibility(),std::move(points),std::move(text)});
        }
        return result;
    };
    AcDbDatabase source(false,true);
    readDrawing(source,sourcePath);
    const auto before=snapshot(source), after=snapshot(saved);
    require(before.size()==after.size(),"axis root inventory changed during existing-plan refinement");
    std::vector<std::pair<int,int>> counts(views.size());
    int unlocated{};
    std::ostringstream evidence;
    evidence<<std::setprecision(17)<<"{\"reopened\":true,\"objects\":[";
    bool first=true;
    for(const auto& [handle,axis]:before){
        const auto found=after.find(handle);
        require(found!=after.end(),"existing plan axis disappeared during refinement");
        const auto& actual=found->second;
        require(axis.kind==actual.kind && axis.layer==actual.layer && axis.visibility==actual.visibility &&
            axis.text==actual.text && axis.points.size()==actual.points.size() &&
            std::equal(axis.points.begin(),axis.points.end(),actual.points.begin(),
                [](const auto& a,const auto& b){return a.distanceTo(b)<1e-5;}),
            "existing axis range or side changed during refinement");
        std::size_t view=views.size();
        if(axis.points.empty())++unlocated;
        else{
            auto anchor=axis.points.front();
            if(axis.kind==L"AcDbLine")anchor=anchor+(axis.points.back()-anchor)*.5;
            view=bcpproduct::nearestGenericView(views,{anchor.x,anchor.y});
            auto& count=counts[view];
            if(axis.kind==L"AcDbLine")++count.first;else ++count.second;
        }
        if(!first)evidence<<',';
        first=false;
        evidence<<"{\"handle\":\""<<handle<<"\",\"kind\":"<<jsonUtf8(axis.kind)
            <<",\"panel\":"<<(view==views.size()?0:view+1)<<",\"visible\":"
            <<(axis.visibility==AcDb::kVisible?"true":"false")<<",\"points\":"<<wholePoints(axis.points)<<'}';
    }
    evidence<<"],\"unlocated\":"<<unlocated<<",\"panels\":[";
    for(std::size_t i=0;i<counts.size();++i){
        if(i)evidence<<',';
        evidence<<"{\"panel\":"<<i+1<<",\"lines\":"<<counts[i].first<<",\"labels\":"<<counts[i].second<<'}';
    }
    evidence<<"]}";
    writeNew(report/L"axis-retention.json",evidence.str());
}

#include "PlanNativeJointRepair.inc"
#include "PlanNativeWallRevision.inc"
#include "PlanNativeDimensionRevision.inc"
#include "PlanNativeFurniture.inc"
#include "PlanNativeTypography.inc"
#include "PlanNativeTemplate.inc"
#include "PlanNativeFinalProbe.inc"
#include "PlanNativeTypographyWidthProbe.inc"

void runNativePlan(const fs::path& config) {
    const auto setting = [&](const wchar_t* key) {
        wchar_t value[32768]{};
        GetPrivateProfileStringW(L"plan", key, L"", value, 32768, config.c_str());
        return std::wstring(value);
    };
    const fs::path sourceInput=setting(L"source");
    require(!sourceInput.empty()&&fs::is_regular_file(sourceInput),u8"E_INPUT_PATH：找不到作业指定的源 DWG，请核对文件路径。");
    const auto source = fs::canonical(sourceInput);
    const auto report = fs::weakly_canonical(setting(L"report"));
    const auto mode = setting(L"mode");
    require(mode.empty() || mode == L"generate" || mode == L"revise" || mode == L"audit" ||
            ((mode==L"fixture"||mode==L"axis-edit-trial"||mode==L"joint-annotations"||mode==L"wall-height"||
              mode==L"dimension-rebuild"||mode==L"furniture-normalize"||mode==L"typography-finalize"||mode==L"template-finalize"||mode==L"control-frames"||mode==L"owned-cleanup"||mode==L"design-source-probe"||mode==L"design-source-audit"||mode==L"final-regression-fixture"||mode==L"table-visibility-probe"||mode==L"typography-width-probe")&&
             !ini(iniPath(),L"nonce").empty()), "Unknown plan job mode");
    const fs::path resourceInput=setting(L"resources");
    require(!resourceInput.empty()&&fs::is_directory(resourceInput),u8"E_PLAN_RESOURCE：作业资源目录不存在，请核对本机 Skill 或项目资源路径。");
    const auto resources = fs::canonical(resourceInput);
    require(fs::is_directory(report) && !fs::exists(report / L"plan-rule-ledger.json") &&
            !fs::exists(report / L"plan-native-result.json") && !fs::exists(report / L"plan-native-audit.json"),
            "use a fresh report folder");
    if(mode==L"axis-edit-trial"){
        verifyAxisEditing(source,report,setting(L"axisEditExpectation"));
        return;
    }
    if(mode==L"design-source-probe"){
        isolated(ini(iniPath(),L"nonce"));
        productRunDesignSourcesProbe(source,setting(L"output"),report,resources,setting(L"designSources"));return;
    }
    if(mode==L"design-source-audit"){
        isolated(ini(iniPath(),L"nonce"));
        productRunDesignSourcesAudit(source,report,setting(L"designSourceAudit"));return;
    }
    if(mode==L"final-regression-fixture"){
        isolated(ini(iniPath(),L"nonce"));
        productWriteFinalRegressionFixture(source,setting(L"output"),report,resources,setting(L"typographySheets"));return;
    }
    if(mode==L"table-visibility-probe"){
        isolated(ini(iniPath(),L"nonce"));
        productProbeTableVisibility(source,report,resources,setting(L"typographySheets"));return;
    }
    if(mode==L"typography-width-probe"){
        isolated(ini(iniPath(),L"nonce"));
        productProbeTypographyWidth(source,setting(L"output"),report,resources);return;
    }
    if(mode==L"owned-cleanup"){
        isolated(ini(iniPath(),L"nonce"));
        acDocManager->executeInApplicationContext(finishPlanOwnedCleanup,new std::pair<fs::path,fs::path>(source,report));return;
    }
    if(mode==L"template-finalize"||mode==L"control-frames"){
        isolated(ini(iniPath(),L"nonce"));const fs::path output=setting(L"output");
        require(!output.empty()&&!fs::exists(output)&&output!=source,"template finalizer requires a new output");
        auto task=std::make_unique<PlanTemplateRequest>();task->source=source;task->output=output;task->report=report;
        task->framesOnly=mode==L"control-frames";task->frameDatums=setting(L"frameDatums");
        task->resources=resources;task->sheets=setting(L"typographySheets");executePlanTemplate(std::move(task));return;
    }
    if(mode==L"typography-finalize"){
        isolated(ini(iniPath(),L"nonce"));
        const fs::path output=setting(L"output");
        require(!output.empty()&&!fs::exists(output)&&output!=source,"typography correction requires a new drawing");
        auto task=std::make_unique<PlanTypographyRequest>(PlanTypographyRequest{source,output,report,resources,setting(L"typographySheets"),setting(L"typographyInspect")==L"1"});
        acDocManager->executeInApplicationContext(executePlanTypography,task.release());return;
    }
    if(mode==L"furniture-normalize"){
        isolated(ini(iniPath(),L"nonce"));
        const fs::path output=setting(L"output");
        require(!output.empty()&&!fs::exists(output)&&output!=source,"furniture normalization requires a new copy");
        auto job=std::make_unique<PlanFurnitureJob>(PlanFurnitureJob{source,output,report,setting(L"furnitureSheets"),setting(L"furnitureFixture")==L"1"});
        acDocManager->executeInApplicationContext(executePlanFurnitureJob,job.release());
        return;
    }
    if(mode==L"joint-annotations"){
        isolated(ini(iniPath(),L"nonce"));
        require(curDoc()&&fs::equivalent(source,curDoc()->fileName()),"joint annotation operation needs its owned input drawing");
        const fs::path output=setting(L"jointInspect")==L"1"?fs::path{}:fs::path(setting(L"output"));
        require(output.empty()||(!fs::exists(output)&&output!=source),"joint annotation candidate must be a new DWG");
        auto job=std::make_unique<JointPaperJob>(readJointPaperJob(source,setting(L"jointRequest"),output,report));
        acDocManager->executeInApplicationContext(executeJointPaperJob,job.release());
        return;
    }
    if(mode==L"wall-height"){
        isolated(ini(iniPath(),L"nonce"));
        const fs::path output=setting(L"wallInspect")==L"1"?fs::path{}:fs::path(setting(L"output"));
        require(output.empty()||(!fs::exists(output)&&output!=source),"wall revision must write a new DWG");
        auto sheetJob=readJointPaperJob(source,setting(L"wallSheets"),output,report,setting(L"wallSkipPrint")==L"1");
        runNativeWallRevision({std::move(sheetJob),resources,setting(L"wallScope"),setting(L"floorLevels"),{},setting(L"wallTargets"),setting(L"wallSkipPrint")==L"1"});
        return;
    }
    if(mode==L"dimension-rebuild"){
        isolated(ini(iniPath(),L"nonce"));
        require(curDoc()&&fs::equivalent(source,curDoc()->fileName()),"dimension replacement needs its owned source");
        const fs::path output=setting(L"output");
        require(!output.empty()&&!fs::exists(output)&&output!=source,"dimension revision requires a new DWG");
        auto sheets=std::make_unique<JointPaperJob>(readJointPaperJob(source,setting(L"dimensionSheets"),output,report));
        require(fs::copy_file(resources/L"reference.dwg",report/L"dimension-reference.dwg"),"bind approved dimension resources");
        acDocManager->executeInApplicationContext(executePlanDimensionRevision,sheets.release());
        return;
    }
    if(mode==L"fixture"){
        const auto output=setting(L"output");
        require(!output.empty(),"fixture output path required");
        writePlanRegressionFixture(source,output,report,setting(L"fixturePriorDwg"));
        return;
    }
    std::map<std::wstring,fs::path> projectFiles;
    const fs::path projectParameters=setting(L"projectParameters");
    if(!projectParameters.empty()){
        const std::set<std::wstring> supported{L"entranceSteps",L"indoorStairs",L"planDetailConfirmations",
            L"furnitureComponents",L"roofPlan",L"floorLevels",L"panelScope",L"sheetChecks",L"roomNameReference",
            L"confirmedRooms",L"newDimensions",L"designSources"};
        std::ifstream profile(projectParameters);
        std::string tag,digest,key,file;
        require(bool(profile>>tag>>digest)&&tag=="PLANPROJECT1"&&digest==sha(source),
                "project parameters must match the exact saved source DWG");
        while(profile>>tag&&tag=="FILE"){
            require(bool(profile>>key>>std::quoted(file))&&!file.empty(),"invalid project parameter file entry");
            const auto name=bcpproduct::decodeMessage(key);
            const auto path=projectParameters.parent_path()/fs::u8path(file);
            require(supported.count(name)&&fs::is_regular_file(path)&&projectFiles.emplace(name,fs::canonical(path)).second,
                    "unknown, missing or duplicate project parameter input");
        }
        require(tag=="END"&&!(profile>>tag),"complete project parameters file");
    }
    const auto projectFile=[&](const wchar_t* key){
        const fs::path explicitValue=setting(key);
        const auto found=projectFiles.find(key);
        require(explicitValue.empty()||found==projectFiles.end()||fs::equivalent(explicitValue,found->second),
                "conflicting project parameter inputs");
        return found==projectFiles.end()?explicitValue:found->second;
    };
    const bool formalRevision=mode==L"revise"&&!setting(L"resumePreviousDWG").empty()&&setting(L"delivery")==L"formal";
    const fs::path priorDrawing=setting(L"resumePreviousDWG");
    const auto priorDrawingHash=ascii(setting(L"resumePreviousSHA256"));
    require(formalRevision||priorDrawing.empty(),"revision target only applies to formal Revise");
    if(formalRevision){
        require(!priorDrawing.empty()&&fs::is_regular_file(priorDrawing)&&
            !fs::equivalent(source,priorDrawing)&&priorDrawingHash==sha(priorDrawing)&&
            !setting(L"previousPlanLedger").empty(),
            "formal revision needs an independently unchanged prior result and its rule ledger");
    }
    if (mode == L"generate"||formalRevision) {
        const auto target = fs::weakly_canonical(setting(L"output"));
        require(!setting(L"output").empty(), "Choose a new drawing for the generation result");
        const double scale=std::stod(setting(L"scale")), unit=std::stod(setting(L"unitMM"));
        require(scale==100. && unit==1., "Full-plan native generation currently supports 1:100 model millimeters");
        auto confirmed=readConfirmedPlanRooms(projectFile(L"confirmedRooms"),sha(source),unit);
        const auto newDimensions=projectFile(L"newDimensions");
        require(newDimensions.empty()||fs::is_regular_file(newDimensions),"confirmed dimension input missing");
        readConfirmedPlanDimensionFile(newDimensions,sha(source),unit);
        require(fs::is_regular_file(resources/L"axes.dwg") && fs::is_regular_file(resources/L"axes.tsv"),
                "Full-plan generation requires the native axis style resources");
        productDiagnostic = {};
        require(fs::equivalent(productCheckDocument(true),source),u8"E_SOURCE_BINDING：当前打开的图纸与作业源 DWG 不同，请打开作业源图副本。");
        const auto preview=setting(L"printReview");
        require(preview.empty()||preview==L"1","printReview must be 1 or empty");
        const auto delivery=setting(L"delivery");
        require(delivery.empty()||delivery==L"formal","delivery must be formal or empty");
        const auto acceptance=setting(L"acceptance");
        require(acceptance.empty()||(acceptance==L"architectural-plan"&&delivery==L"formal"),
                "architectural-plan acceptance applies to formal drawing delivery");
        const auto postcheck=setting(L"postcheckMode");
        require(postcheck.empty()||postcheck==L"off"||postcheck==L"audit"||postcheck==L"repair",
                "postcheckMode must be off, audit or repair");
        const fs::path inkAnalyzer=setting(L"plotInkAnalyzer"),inkPython=setting(L"plotInkPython");
        if(!inkAnalyzer.empty()||!inkPython.empty()){
            const auto expected=resources.parent_path()/L"tools/check_plan_print_ink.py";
            require(!ini(iniPath(),L"nonce").empty()&&acceptance==L"architectural-plan"&&preview==L"1"&&
                    fs::is_regular_file(inkAnalyzer)&&fs::is_regular_file(inkPython)&&fs::equivalent(inkAnalyzer,expected),
                    "printed dimension repair requires the current Skill tool and its owned runner");
        }
        productStart(source,target,ini(iniPath(),L"nonce").empty()?fs::path{}:report,resources,true,scale,unit,
                     projectFile(L"entranceSteps"),projectFile(L"indoorStairs"),report,
                     projectFile(L"planDetailConfirmations"),projectFile(L"furnitureComponents"),projectFile(L"roofPlan"),
                     setting(L"previousPlanLedger"),projectFile(L"floorLevels"),projectFile(L"panelScope"),
                     preview==L"1",setting(L"plotStyle"),projectParameters,projectFile(L"sheetChecks"),delivery==L"formal",
                     projectFile(L"roomNameReference"),acceptance==L"architectural-plan");
        if(productJob){
            productJob->templatePreparation=setting(L"templatePreparation")==L"1"&&!ini(iniPath(),L"nonce").empty();
            productJob->allowRoomTextConversion=setting(L"roomTextConversion")==L"1";
            productJob->confirmedRooms=std::move(confirmed);
            productJob->unifiedNewDimensions=newDimensions;
            productJob->unifiedDesignSources=projectFile(L"designSources");
            productJob->planInkAnalyzer=inkAnalyzer;
            productJob->planInkPython=inkPython;
            if(!postcheck.empty())productJob->planPostcheckMode=postcheck;
            if(formalRevision){
                productJob->planMode="revise";
                productJob->planResumeOutput=fs::canonical(priorDrawing);
                productJob->planResumeOutputHash=priorDrawingHash;
            }
        }
        return;
    }
    for(const auto& [key,path]:projectFiles)
        require(mode!=L"audit"&&(key==L"confirmedRooms"||key==L"newDimensions"||
            key==L"entranceSteps"||key==L"indoorStairs"||key==L"furnitureComponents"||key==L"floorLevels"),
            "this project parameter is not implemented by the selected Revise/Audit mode");
    const auto output = mode == L"audit" ? fs::path{} : fs::weakly_canonical(setting(L"output"));
    require(mode == L"audit" || (source != output && !fs::exists(output)), "use a new DWG path");
    require(acrxClassDictionary->at(L"TDbWall") && acrxClassDictionary->at(L"TDbDimension2"),
            "load compatible installed Tianzheng native components");
    const bool beforeBcp = bcpLoaded();
    const auto originalHash = sha(source);
    ProductJob job;
    job.ownedDocument = curDoc();
    job.root = report;
    job.profile = resources;
    job.sourceHash = originalHash;
    job.planMode = mode == L"audit" ? "audit" : "revise";
    productLoadRuleCatalog(job, !mode.empty());
    if (mode == L"audit") {
        AcDbDatabase audited(false,true);
        readDrawing(audited, source);
        Working working(&audited);
        const auto inventory = productInventory(audited);
        const auto roots = modelTypes(audited);
        const auto layoutAudit=setting(L"layoutAudit");
        if(!layoutAudit.empty()){
            std::ifstream input(fs::canonical(layoutAudit));
            std::string tag,digest;
            ProductPlanLayoutConfig perimeter;
            perimeter.projectAssociations=false;
            require(bool(input>>tag>>digest>>perimeter.scale>>perimeter.unitMM)&&tag=="PLANLAYOUTAUDIT1"&&
                digest==originalHash&&std::isfinite(perimeter.scale)&&perimeter.scale>0&&
                std::isfinite(perimeter.unitMM)&&perimeter.unitMM>0,
                "layout audit profile must match the saved drawing and give a positive scale and unit");
            perimeter.paper=productLoadPlanPaperPolicy(job,perimeter.scale,perimeter.unitMM);
            const double paper=perimeter.scale/perimeter.unitMM;
            const auto expectedPaper=job.ruleCatalog?job.ruleCatalog->stairExtensionPaperMM:std::optional<double>{5.5};
            const double expected=expectedPaper.value_or(5.5)*perimeter.scale;
            std::vector<PlanNativeDimension> dimensions;
            std::vector<ProductStairRow> stairs;
            std::set<std::string> listedStairs;
            std::unique_ptr<AcDbDatabase> stairSource;
            const auto stairSourcePath=setting(L"layoutAuditStairSource");
            if(!stairSourcePath.empty()){
                require(sha(stairSourcePath)==ascii(setting(L"layoutAuditStairSourceHash")),"layout audit stair source hash");
                stairSource=std::make_unique<AcDbDatabase>(false,true);
                readDrawing(*stairSource,stairSourcePath);
            }
            std::set<std::string> listed;
            std::set<std::pair<int,int>> listedDatums;
            bool ended=false;
            while(input>>tag){
                if(tag=="END"){ended=true;break;}
                int panel{};
                require(bool(input>>panel)&&panel>=1,"layout audit panel");
                if(tag=="PANEL"){
                    ProductPlanBounds bounds;
                    require(static_cast<std::size_t>(panel)==perimeter.panels.size()+1&&
                        bool(input>>bounds.xmin>>bounds.ymin>>bounds.xmax>>bounds.ymax)&&
                        std::isfinite(bounds.xmin)&&std::isfinite(bounds.ymin)&&
                        std::isfinite(bounds.xmax)&&std::isfinite(bounds.ymax)&&
                        bounds.xmin<bounds.xmax&&bounds.ymin<bounds.ymax,"layout audit panel bounds");
                    perimeter.panels.push_back(bounds);
                    perimeter.sidePlacement.emplace_back();
                    continue;
                }
                require(static_cast<std::size_t>(panel)<=perimeter.panels.size(),"layout audit panel is not defined");
                if(tag=="DATUM"){
                    int side{};
                    double coordinate{};
                    require(bool(input>>side>>coordinate)&&side>=0&&side<4&&std::isfinite(coordinate),
                        "layout audit side datum");
                    require(listedDatums.emplace(panel-1,side).second,"layout audit side datum is duplicated");
                    perimeter.sidePlacement[panel-1][side].coordinate=coordinate;
                    continue;
                }
                if(tag=="STAIR"){
                    ProductStairRow row;
                    require(stairSource&&bool(input>>row.source)&&listedStairs.insert(row.source).second,
                        "layout audit stair source handle must be unique");
                    row.panel=panel;
                    row.key=row.object=row.source;
                    row.state="native-preserved";
                    row.issue=u8"源图与最终图的可见平面表达已对照；逐跑实际级数、级高、完成面及平台衔接待核";
                    {
                        Working workingSource(stairSource.get());
                        const auto id=find(*stairSource,row.source);
                        const auto entity=open<AcDbEntity>(id);
                        const auto layer=open<AcDbLayerTableRecord>(entity->layerId());
                        row.nativeType=entity->isA()->name();
                        row.nativeLayer=symbolName(entity->layerId());
                        require(row.nativeType.rfind(L"TDb",0)==0&&row.nativeType.find(L"Stair")!=std::wstring::npos&&
                            !entity->isAProxy()&&entity->visibility()==AcDb::kVisible&&!layer->isOff()&&!layer->isFrozen(),
                            "layout audit source stair must be an actual visible native entity");
                        row.grips=chainGrips(id);
                        row.visibleStrokes=productStairVisibleStrokes(*entity,.01/perimeter.unitMM);
                    }
                    stairs.push_back(std::move(row));
                    continue;
                }
                ProductPlanRole role;
                role.panel=panel-1;
                if(tag=="PERIMETER")
                    require(bool(input>>role.side>>role.row)&&role.side>=0&&role.side<4&&
                        role.row>=1&&role.row<=3,"layout audit perimeter role");
                else require(tag=="INTERIOR","unknown layout audit record");
                require(bool(input>>role.handle)&&listed.insert(role.handle).second,
                    "layout audit dimension handle must be unique");
                const auto id=find(audited,role.handle);
                const auto object=open<AcDbEntity>(id);
                require(productPlanDimensionVisible(*object),"layout audit listed a nonvisible dimension");
                if(tag=="PERIMETER"){
                    require(std::wstring(object->isA()->name())==L"TDbDimension2","layout audit perimeter native chain");
                    perimeter.dimensions.push_back(std::move(role));
                    continue;
                }
                if(AcDbDimension::cast(object.get())){
                    auto projection=readPlanProjection(id);
                    require(projection.has_value(),"layout audit interior ordinary dimension projection");
                    dimensions.push_back({role.handle,role.panel,std::abs(projection->direction.x)<1e-5,
                        true,false,{},productDimensionMeasurements(*object,false),role.handle,std::move(projection)});
                    continue;
                }
                require(std::wstring(object->isA()->name())==L"TDbDimension2","layout audit interior dimension");
                const auto grips=chainGrips(id);
                const auto values=chainValues(id);
                require(!values.empty()&&grips.size()==2*values.size()+6&&grips[0].distanceTo(grips[1])>1e-5,
                    "layout audit native grip topology");
                dimensions.push_back({role.handle,role.panel,std::abs(grips[0].x-grips[1].x)<1e-5,
                    false,false,grips,values,role.handle,{}});
            }
            input>>std::ws;
            require(ended&&input.eof()&&!perimeter.panels.empty(),"layout audit profile is incomplete");
            for(std::size_t panel=0;panel<perimeter.panels.size();++panel)
                for(int side=0;side<4;++side)
                    if(!listedDatums.count({static_cast<int>(panel),side}))
                        perimeter.sidePlacement[panel][side].coordinate=productPlanDatum(perimeter.panels[panel],side);
            for(const auto& bounds:perimeter.panels)
                job.rulePanels.push_back({bounds.xmin,bounds.ymin,bounds.xmax,bounds.ymax});
            const std::vector<std::optional<ProductRoomGrid>> grids(perimeter.panels.size());
            std::vector<PlanLayoutGroup> groups(perimeter.panels.size());
            for(std::size_t panel=0;panel<groups.size();++panel)groups[panel].panel=static_cast<int>(panel);
            for(std::size_t i=0;i<dimensions.size();++i)groups[dimensions[i].panel].members.push_back(i);
            auditPlanLayout(job,audited,dimensions,grids,{},paper,groups);
            productGenericPerimeterTextClearance(job,audited,perimeter,true);
            std::ostringstream proof;
            proof<<std::setprecision(17)<<"{\"drawingSHA256\":\""<<originalHash
                 <<"\",\"requiredVisibleMM\":"<<expected<<",\"chains\":[";
            bool first=true;
            for(const auto& dim:dimensions){
                if(!first)proof<<',';
                first=false;
                const auto witness=readPlanVisibleWitnesses(find(audited,dim.handle),dim.vertical);
                const bool verified=planWitnessLengthMatches(witness,dim.values.size(),expected,perimeter.unitMM);
                const auto stations=dim.projection?std::vector<AcGePoint3d>{dim.projection->a,dim.projection->b}:
                    std::vector<AcGePoint3d>(dim.grips.begin()+2,dim.grips.begin()+dim.values.size()+3);
                proof<<"{\"handle\":\""<<dim.handle<<"\",\"panel\":"<<dim.panel+1
                    <<",\"stations\":"<<wholePoints(stations)<<",\"values\":[";
                for(std::size_t i=0;i<dim.values.size();++i){
                    if(i)proof<<',';
                    proof<<dim.values[i];
                }
                proof<<"],\"witnessVerified\":"<<(verified?"true":"false")<<",\"visibleWitnessesMM\":[";
                bool firstWitness=true;
                for(const auto& line:witness){
                    if(!firstWitness)proof<<',';
                    firstWitness=false;
                    if(line.suppressed)proof<<"null";else proof<<line.visibleLength()*perimeter.unitMM;
                }
                proof<<"]}";
                if(!verified)job.detailPending.push_back({u8"最终室内尺寸界线",dim.handle,u8"最终文件重开的可见界线不满足规则"});
                const auto record="{\"panel\":"+std::to_string(dim.panel+1)+",\"object\":\""+dim.handle+
                    "\",\"savedReopened\":true,\"report\":\"final-layout-audit.json\",\"result\":\""+
                    (verified?"verified-visible-extension":"pending")+"\"}";
                productRecordRuleEvidence(job,"D23",record);
                productRecordRuleEvidence(job,"QA09",record);
            }
            proof<<"],\"groups\":[";
            first=true;
            for(const auto& group:groups){
                if(!first)proof<<',';
                first=false;
                std::ostringstream record;
                record<<"{\"panel\":"<<group.panel+1<<",\"chains\":"<<group.members.size()
                    <<",\"conflicts\":"<<group.after<<",\"unreadable\":"<<group.unreadable
                    <<",\"sideIssues\":"<<group.sideIssues<<",\"segmentAssociationIssues\":"<<group.segmentAssociationIssues
                    <<",\"savedReopened\":true,\"report\":\"final-layout-audit.json\",\"result\":\""
                    <<(group.after||group.unreadable||group.sideIssues||group.segmentAssociationIssues?
                        "pending":"layout-verified")<<"\"}";
                proof<<record.str();
                productRecordRuleEvidence(job,"D25",record.str());
                productRecordRuleEvidence(job,"QA09",record.str());
            }
            proof<<"],\"unlistedVisibleDimensions\":[";
            first=true;
            for(const auto& [handle,type]:roots){
                const auto key=ascii(handle);
                if(listed.count(key))continue;
                const auto object=open<AcDbEntity>(find(audited,key));
                if((type!=L"TDbDimension2"&&!AcDbDimension::cast(object.get()))||!productPlanDimensionVisible(*object))continue;
                if(!first)proof<<',';
                first=false;
                proof<<'"'<<key<<'"';
                job.detailPending.push_back({u8"最终尺寸核验范围",key,u8"最终可见尺寸未列入指定室内或外围核验，需独立核用途及样式"});
            }
            proof<<"]}";
            writeNew(report/L"final-layout-audit.json",proof.str());
            if(!stairs.empty())productVerifyIndoorStairs(job,audited,stairs,perimeter.unitMM,report/L"final-stair-audit.json");
        }
        require(sha(source) == originalHash,"source changed during read-only audit");
        writeNew(report/L"plan-native-audit.json", "{\"drawingWritten\":false,\"sourceSHA256\":\"" + originalHash +
                 "\",\"modelRoots\":" + std::to_string(roots.size()) + ",\"input\":" + productInventoryJson(inventory) + "}");
        productWriteRuleLedger(job,"",job.rulePanels);
        return;
    }
    const double scale = std::stod(setting(L"scale")), unit = std::stod(setting(L"unitMM"));
    require(std::isfinite(scale) && scale > 0 && std::isfinite(unit) && unit > 0, "invalid scale or drawing unit");
    readConfirmedPlanDimensionFile(projectFile(L"newDimensions"),originalHash,unit);
    job.currentPlanRules=true;
    job.unifiedScale=scale;job.unifiedUnitMM=unit;
    job.unifiedFurnitureComponents=projectFile(L"furnitureComponents");
    job.allowRoomTextConversion=setting(L"roomTextConversion")==L"1";
    productLoadPlanPaperPolicy(job, scale, unit);
    const auto confirmed=readConfirmedPlanRooms(projectFile(L"confirmedRooms"),originalHash,unit);
    const auto materialSource = setting(L"prepareMaterialFrom");
    if (!materialSource.empty()) {
        const auto destination = job.profile / L"room-material.dwg";
        require(!fs::exists(destination), "room material bank already exists");
        AcDbDatabase donor(false,true), bank(true,true);
        readDrawing(donor, materialSource);
        const auto pair=productRoomAreaPair(donor);
        require(pair.has_value(), "no verified area text prototype pair");
        const auto style=productRoomVerifiedStyle(donor,false);
        require(!style.isNull(), "material source lacks confirmed FY_HZ");
        AcDbObjectIdArray styles;
        styles.append(style);
        AcDbIdMapping mapping;
        status(donor.wblockCloneObjects(styles,bank.textStyleTableId(),mapping,AcDb::kDrcIgnore,false),
               "copy portable FY_HZ definition");
        cloneOne(donor,pair->body,bank);
        cloneOne(donor,pair->exponent,bank);
        status(bank.saveAs(destination.c_str(),false,AcDb::kDHL_CURRENT),"save two-piece portable room material");
    }

    AcDbDatabase db(false, true), material(false, true);
    readDrawing(db, source);
    readDrawing(material, job.profile / L"reference.dwg");
    Working working(&db);
    require(!productJob, "another operation owns the property reader");
    productJob = std::make_unique<ProductJob>();
    productJob->ownedDocument = curDoc();
    GenericNativeInput input;
    try {
        input = productReadGenericInput(job, db);
    } catch (...) {
        productJob.reset();
        throw;
    }
    productJob.reset();
    const auto roots = modelTypes(db);
    std::vector<bcp::Wall> walls;
    for (const auto& wall : input.walls) walls.push_back(wall.wall);
    const auto views = bcpproduct::discoverGenericViews(walls);
    require(!views.empty(), "no identifiable native wall plans");
    productRecordRulePanels(job,views);
    auto ownership=productIdentifyPlanPanels(input,views,unit);
    productCaptureOldAnnotations(job,db,views,ownership,productPerimeterSourceWitnesses(db,job));
    auto names = productPlanNameInventory(db, input, views, roots,true);
    std::vector<ProductPlanBounds> panels;
    std::vector<bool> active;
    std::vector<std::optional<ProductRoomGrid>> roomGrids(views.size());
    std::vector<ProductRoomWriteReceipt> writtenAreas;
    for (std::size_t i = 0; i < views.size(); ++i) {
        const auto& view = views[i];
        const ProductPlanBounds panel{view.bounds.xmin,view.bounds.ymin,view.bounds.xmax,view.bounds.ymax};
        panels.push_back(panel);
        const bool titled = input.titles.empty() || std::any_of(input.titles.begin(), input.titles.end(), [&](const auto& title) {
            return bcpproduct::nearestGenericView(views, {title.anchor.x, title.anchor.y}) == i;
        });
        const bool namedRoom=std::any_of(names.begin(),names.end(),[&](const auto& name) {
            return name.view==i && name.pendingReason.empty() && name.anchor && inPanel(*name.anchor,panel);
        });
        active.push_back(titled||namedRoom);
        if (active.back()) productGenericRooms(job, db, input, views, i, panel,
            productGenericProjectionSurfaces(db, input, views, i, panel), static_cast<int>(i), material, scale, unit,
            names, true, &writtenAreas, confirmed, &roomGrids[i]);
    }
    const auto furniture=productApplyPlanFurniture(job,db,{});
    productApplyPlanOmissions(job,db,input,views,active,roomGrids,roots);
    std::vector<std::pair<AcDbObjectId,bool>> recovered;
    productResolveLooseDimensions(job,db,material,views,active,recovered);
    const auto entranceSteps=productEntranceSteps(job,db,input,views,unit,projectFile(L"entranceSteps"),originalHash,&material,scale);
    const auto indoorStairs=productIndoorStairs(job,db,views,&material,unit,scale,projectFile(L"indoorStairs"),originalHash);
    productApplyPlanDimensionStyle(job, db, scale, unit);
    const auto candidate = report / L"revision-before-publication.dwg";
    require(!fs::exists(candidate), "candidate already exists");
    productArrangeEntranceDimensions(job,db,entranceSteps,scale,unit);
    productArrangeIndoorStairDimensions(job,db,indoorStairs,scale,unit);
    status(db.saveAs(candidate.c_str(), false, AcDb::kDHL_CURRENT), "save separate result");
    AcDbDatabase saved(false, true);
    readDrawing(saved, candidate);
    productVerifyEntranceSteps(job,saved,entranceSteps,report/L"exterior-entrance-steps.json");
    productVerifyIndoorStairs(job,saved,indoorStairs,unit,report/L"indoor-stairs.json");
    Working readback(&saved);
    productVerifyPlanFurniture(job,saved,furniture,{});
    productVerifyPlanOmissions(job,saved);
    verifyRetainedPlanAxes(source,saved,views,report);
    productVerifyPlanNames(job, saved, names);
    productVerifyPlanDimensionStyle(job, saved, scale, unit);
    productAuditPlanDimensionText(job, saved);
    std::ostringstream proof;
    proof << std::setprecision(17) << "{\"reopened\":true,\"modelRoots\":" << modelTypes(saved).size()
          << ",\"bcpLoadedBefore\":" << (beforeBcp ? "true" : "false")
          << ",\"bcpLoadedAfter\":" << (bcpLoaded() ? "true" : "false") << ",\"sourceSHA256\":\"" << originalHash
          << "\",\"panels\":[";
    for (std::size_t i=0;i<panels.size();++i) {
        if(i)proof<<',';
        const auto& p=panels[i];
        proof<<"{\"panel\":"<<i+1<<",\"processed\":"<<(active[i]?"true":"false")<<",\"bounds\":["
             <<p.xmin<<','<<p.ymin<<','<<p.xmax<<','<<p.ymax<<"]}";
    }
    proof << "],\"areas\":[";
    bool first = true;
    productWriteRoomAreaReadback(job,saved,writtenAreas,report/L"room-area-readback.json");
    for(const auto& area:writtenAreas){
        if(!first)proof<<',';
        first=false;
        proof<<"{\"panel\":"<<area.panel<<",\"room\":\""<<area.name<<"\",\"body\":\""<<area.body
             <<"\",\"superscript\":\""<<area.exponent<<"\",\"m2\":"<<area.area<<'}';
    }
    proof << "],\"layouts\":[";
    const auto dict=open<AcDbDictionary>(saved.layoutDictionaryId());
    std::unique_ptr<AcDbDictionaryIterator> iter(dict->newIterator());
    std::map<int,std::wstring> paperLayouts;
    for(;!iter->done();iter->next()){
        const auto sheet=open<AcDbLayout>(iter->objectId());
        if(sheet->modelType())continue;
        const auto members=productFurnitureMembers(sheet->getBlockTableRecordId());
        if(std::any_of(members.begin(),members.end(),[](auto id){
            const auto entity=open<AcDbEntity>(id);const auto* vp=AcDbViewport::cast(entity.get());
            return vp&&vp->number()!=1&&vp->width()>0&&vp->height()>0;
        }))paperLayouts.emplace(sheet->getTabOrder(),iter->name());
    }
    first=true;
    for(const auto& entry:paperLayouts){if(!first)proof<<',';first=false;proof<<jsonUtf8(entry.second);}
    proof << "],\"pending\":[";
    first=true;
    for (const auto& item : job.detailPending) {
        if(!first)proof<<',';
        first=false;
        proof<<'['<<jsonUtf8(bcpproduct::decodeMessage(item[0]))<<','
             <<jsonUtf8(bcpproduct::decodeMessage(item[1]))<<','<<jsonUtf8(bcpproduct::decodeMessage(item[2]))<<']';
    }
    require(sha(source)==originalHash&&!fs::exists(output),"source or output changed during execution");
    productWriteRuleLedger(job,sha(candidate),job.rulePanels);
    bcpproduct::publishNewFile(candidate,output);
    proof << "],\"outputSHA256\":\"" << sha(output) << "\",\"completed\":true}";
    writeNew(report/L"plan-native-result.json",proof.str());
    writeNew(report/L"product-result.json",proof.str());
}

void dispatchPlan(const fs::path& config) noexcept {
    try {
        runNativePlan(config);
    } catch (const std::exception& error) {
        acutPrintf(L"\nPLAN_DRAW: %ls\n", bcpproduct::decodeMessage(error.what()).c_str());
        try {
            wchar_t path[32768]{};
            GetPrivateProfileStringW(L"plan",L"report",L"",path,32768,config.c_str());
            writeNew(fs::path(path)/L"plan-native-error.json",
                "{\"code\":\""+productDiagnosticCode(error.what())+"\",\"drawingWritten\":false,\"error\":"+
                jsonUtf8(bcpproduct::decodeMessage(error.what()))+"}");
            if(!fs::exists(fs::path(path)/L"product-result.json"))
                writeNew(fs::path(path)/L"product-result.json","{\"completed\":false,\"error\":"+jsonUtf8(bcpproduct::decodeMessage(error.what()))+"}");
        } catch (...) {}
    }
}

void planNativeCommand() {
    resbuf selected{};
    if (acedGetFileD(L"选择原生平面作业 INI",L"",L"ini",0,&selected)!=RTNORM) return;
    const fs::path config(selected.resval.rstring);
    acutDelString(selected.resval.rstring);
    dispatchPlan(config);
}

void runOwnedHarness() {
    isolated(ini(iniPath(),L"nonce"));
    require(curDoc(), "owned control document absent");
    const auto control=fs::path(curDoc()->fileName());
    require(control.filename()==L"control.dwg", "owned control drawing expected");
    const auto job=control.parent_path();
    dispatchPlan(job/L"plan.ini");
    writeNew(job/L"stage2-analysis.done","{}");
}
}

extern "C" AcRx::AppRetCode acrxEntryPoint(AcRx::AppMsgCode msg, void* packet) {
    if (msg==AcRx::kInitAppMsg) {
        productApplication=packet;
        acrxUnlockApplication(packet);
        acrxRegisterAppMDIAware(packet);
        for (const auto* name : {L"PLAN_DRAW",L"PLAN_NATIVE"})
            if (acedRegCmds->addCommand(L"PLAN_NATIVE_TOOLS",name,name,ACRX_CMD_MODAL,planNativeCommand)!=Acad::eOk)
                return AcRx::kRetError;
        if (acedRegCmds->addCommand(L"PLAN_NATIVE_TOOLS",L"PLAN_DRAW_INTERNAL_FINISH",L"PLAN_DRAW_INTERNAL_FINISH",
            ACRX_CMD_MODAL,productFinish)!=Acad::eOk) return AcRx::kRetError;
        if (acedRegCmds->addCommand(L"PLAN_NATIVE_TOOLS",L"PLAN_WALL_WRITE_INTERNAL",L"PLAN_WALL_WRITE_INTERNAL",
            ACRX_CMD_MODAL,writeNativeWallRevision)!=Acad::eOk) return AcRx::kRetError;
        for (const auto& command : std::vector<std::pair<const wchar_t*,void(*)()>>{
            {L"PLAN_DRAW_INTERNAL_OPEN_WORK",productOpenWorkSession},
            {L"PLAN_DRAW_INTERNAL_OPEN_PREPARED",productOpenPreparedSession},
            {L"PLAN_DRAW_INTERNAL_OPEN_FINAL",productOpenFinalSession}})
            if (acedRegCmds->addCommand(L"PLAN_NATIVE_TOOLS",command.first,command.first,ACRX_CMD_SESSION,command.second)!=Acad::eOk)
                return AcRx::kRetError;
        if (!ini(iniPath(),L"nonce").empty() &&
            acedRegCmds->addCommand(L"PLAN_NATIVE_TOOLS",L"BCP_STAGE2_ANALYZE",L"BCP_STAGE2_ANALYZE",
              ACRX_CMD_MODAL,runOwnedHarness)!=Acad::eOk) return AcRx::kRetError;
    } else if (msg==AcRx::kUnloadAppMsg) {
        if(productJob)return AcRx::kRetError;
        acedRegCmds->removeGroup(L"PLAN_NATIVE_TOOLS");
    }
    return AcRx::kRetOK;
}
