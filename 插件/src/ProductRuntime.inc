#include "PluginPolicy.h"

// Product layer over the tested native generators. Developer probes remain separate.
void* productApplication{};
fs::path productLastOutput,productLastIssues;
enum class ProductPhase { OpenWork, Generate, OpenPrepared, Join, OpenFinal };
struct ConfirmedPlanRoom {
 int panel;
 std::string source;
 std::wstring prefix;
 std::vector<AcGePoint2d> vertices;
 double area;
};
struct ProductPlanPostcheckTextEdit;
struct ProductJob {
 ULONGLONG startedAtTick{GetTickCount64()};
 ProductPhase phase=ProductPhase::OpenWork;
 fs::path source,output,root,work,profile,sourceCopy,generationConfig,joinConfig,captionConfig,testReceipt;
 std::string sourceHash,preparedHash,sourceMemory,sourceDictionary;AcApDocument* ownedDocument{};AcApDocument* sourceDocument{};bool preparationDocumentClosed{};bool locked{},validation{};
 std::optional<bcpproduct::ApprovedLayout> approvedLayout;std::map<std::string,std::string> approvedMap;std::string approvedAreas;
 std::optional<bcpproduct::ThreeLayout> threeLayout;bool latestThreeModel{};std::set<std::string> retainedExtraHandles;std::map<std::string,double> currentAreas;bool threeInput=false;bool dynamicInput=false;bool genericInput=false;int panelIndex=0;int doorCount=0;std::optional<bcp::Plan> currentPlan;
 int preparedRoots{},finalRoots{},chains{},dimensions{},captions{},negativeChecks{};bool extendedInput=false;bool axisSideCommandUsed=false;bool dimensionMergeCommandUsed=false;
 std::vector<AcDbObjectId> genericCaptionIds;
 bcpproduct::PlanDimensionStylePolicy dimensionStylePolicy;
 double planDimensionScale{100},planDimensionUnitMM{1};
 double unifiedScale{100},unifiedUnitMM{1};
 bool unifiedPlan{};
 bool allowRoomTextConversion{};
 std::map<std::wstring,unsigned> planPublicCommands;
 fs::path unifiedEntranceSteps,unifiedIndoorStairs,unifiedDetailConfirmations,unifiedFurnitureComponents,unifiedRoofPlan;
 fs::path unifiedFloorLevels;
 std::vector<ConfirmedPlanRoom> confirmedRooms;
 fs::path unifiedNewDimensions;
 fs::path unifiedPanelScope;
 std::wstring planPlotStyle;
 bool planPrintReview{};
 bool templatePreparation{};
 bool formalPlan{},formalReady{},formalDelivery{},architecturalPlanOnly{};
 fs::path planInkAnalyzer,planInkPython;
#ifdef PLAN_STANDALONE_ENTRY
 std::wstring planPostcheckMode{L"audit"};
 std::function<std::shared_ptr<const ProductPlanPostcheckTextEdit>(
   AcDbDatabase&,const ProductJob&,std::size_t,const std::string&)> planPostcheckTextRepair;
 std::function<void(AcDbDatabase&,const fs::path&)> planFinalDimensionCheck;
 AcDbObjectId approvedOpeningTextStyle;
#endif
 std::set<bcpproduct::PlanSourceInventory::Key> planPreservedDesign;
 std::set<std::string> planPostcheckKnownDimensions;
 std::string planBlockingChecklist;
 fs::path planDeliveredChecklist;
 std::vector<std::array<double,4>> planPrintWindows;
 std::size_t planPrintedPages{};
 bool planPrintGeometryChecked{};
 std::size_t planPrintDefects{};
 std::map<std::size_t,std::size_t> planUnassignedNetDomains;
 std::size_t planUnverifiedNetAreaLabels{};
 std::map<bcpproduct::PlanSourceInventory::Key,std::pair<std::size_t,std::string>> planReleaseLocations;
 std::map<std::size_t,std::string> planSourceFloorNames;
 fs::path unifiedProjectParameters;
 fs::path unifiedSheetChecks;
 struct PlanSheetLink {
  std::string anchor,note,target,archiveTarget;
  std::wstring text,targetTitle;
 };
 std::vector<PlanSheetLink> planSheetLinks;
 std::set<std::string> planSupplementalDetails;
 struct PlanSheetLayout {
  std::wstring name;
  std::string title,block,viewport;
  double scale{};
  std::string sourceTitle;
  double paperWidth{594.},paperHeight{420.};
 };
 std::map<int,PlanSheetLayout> planSheetLayouts;
 std::map<int,std::pair<std::string,std::wstring>> planSheetNumbers;
 struct RegisteredPlanDatum { std::string output; std::vector<AcGePoint3d> points; std::wstring value; };
 std::map<std::string,RegisteredPlanDatum> planRegisteredDatums;
 fs::path unifiedRoomNameReference;
 std::string unifiedProjectParametersHash;
 std::map<std::string,std::vector<double>> planDimensionMeasurements;
 std::map<std::string,std::string> planDimensionRatioReplacements;
 std::map<std::string,std::string> planRequiredDimensionReplacements;
 std::map<std::string,std::string> planRequiredDimensionIssues;
 std::map<std::string,std::string> planDimensionNormalizationPending;
 std::set<std::string> planDimensionExclusions;
 struct InteriorDoorMeasurement {
  std::string door,hostWall,adjacentWall;
  std::size_t panel{};
  AcGePoint3d first,second,jamb;
  bool vertical{},omit{},bathroom{},hasJamb{};
  double standardMM{};
  std::wstring ownNumber;
  bool omitJamb{};
 };
 std::vector<InteriorDoorMeasurement> interiorDoorMeasurements;
 struct DoorJambChange {
  std::string host,adjacent,reason;
  std::size_t panel{};
  AcGeVector3d offset;
  double priorMM{},standardMM{};
 };
 std::map<std::string,DoorJambChange> doorJambChanges;
 std::vector<std::pair<std::string,std::string>> pending;
 std::vector<std::array<std::string,3>> detailPending;
 std::optional<bcpproduct::PlanRuleCatalog> ruleCatalog;
 std::string ruleCatalogHash, planMode{"generate"};
 std::map<std::string,std::string> ruleEvidence;
 fs::path priorPlanLedger;
 std::string priorPlanLedgerHash;
 fs::path planResumeOutput;
 std::string planResumeOutputHash;
 int planSourceDbmod{-1};
 std::vector<std::array<double,4>> rulePanels;
 bcpproduct::PlanSourceInventory sourceInventory;
 bcpproduct::PlanDrawingChecks drawingChecks;
 std::string planInventoryHash, planAnnotationInventoryHash, planVerifiedCandidateHash;
 std::vector<std::size_t> layoutSourcePanels;
 struct SourceDimension {
  std::vector<AcGePoint3d> points;
  std::vector<double> values;
  AcGePoint3d baseline;
  std::wstring displayOverride;
  bool readable{}, automaticText{}, pointsFromSegments{};
 };
 std::map<std::string,SourceDimension> sourceDimensions;
 std::map<std::string,std::size_t> planVerifiedInteriorMeasurementPanels;
 struct SourceAnnotationGlyph {
  std::wstring text,style,primary,big;
  double height{},width{};
 };
 struct SourceAnnotation {
  std::string path,root,handle,kind;
  std::wstring type,layer;
  std::optional<AcGePoint3d> position;
  std::optional<AcDbExtents> bounds;
  std::optional<AcGeVector3d> textDirection;
  std::vector<SourceAnnotationGlyph> glyphs;
  std::size_t panel{};
  bool visible{},external{};
 };
 std::vector<SourceAnnotation> planSourceAnnotations;
 struct LooseDimension {
  std::vector<AcGePoint3d> stations;
  std::vector<std::string> strokes, anchors;
  std::vector<std::pair<AcGePoint3d,AcGePoint3d>> strokeGeometry;
  std::string reason, output;
  double baseline{}, value{}, valueToleranceMM{};
  bool vertical{}, dimensionLayerHint{}, standalone{}, inSelectedPanel{};
 };
 std::map<std::string,LooseDimension> planLooseDimensions;
 std::map<std::string,std::string> planAnnotationTextTargets;
 std::map<std::string,std::string> planAnnotationFurnitureTargets;
 struct OldPerimeterDimension {
  int panel{},side{};
  std::vector<std::string> replacement;
  bool verified{};
 };
 std::map<std::string,OldPerimeterDimension> oldPerimeterDimensions;
};
#include "ProductRuleCatalog.inc"
#ifdef PLAN_STANDALONE_ENTRY
#include "ProductPlanPrintReview.inc"
void productWritePlanDeliveryReview(ProductJob&,AcDbDatabase&,const std::string&);
#endif
void productRecordPlanPublished(ProductJob& job){
 if(!job.unifiedPlan)return;
 const auto sourceHash=sha(job.source);
 const auto outputHash=sha(job.output);
 const auto copiedHash=sha(job.sourceCopy);
 const bool native=acrxClassDictionary->at(L"TDbWall")&&
                   acrxClassDictionary->at(L"TDbDimension2");
 const auto route=std::string("{\"savedReopened\":true,\"sourceSHA256\":\"")+sourceHash+
  "\",\"sourceCopySHA256\":\""+copiedHash+"\",\"outputSHA256\":\""+outputHash+
  "\",\"scope\":\"saved-input-and-published-dwg\",\"result\":\""+
  (sourceHash==job.sourceHash&&copiedHash==sourceHash?"verified-independent-dwg-publication":"pending-source-fingerprint")+"\"}";
 for(const auto* rule:{"G01","G02","P06","P07","Q01"})productRecordRuleEvidence(job,rule,route);
 productRecordRuleEvidence(job,"G08","{\"savedReopened\":true,\"scope\":\"source-document-save-state\",\"dbmod\":"+
  std::to_string(job.planSourceDbmod)+",\"result\":\""+
  (job.planSourceDbmod>=0&&bcpproduct::canGenerateSavedDrawing(job.planSourceDbmod)?"verified-saved-source-state":"pending-source-save-state")+"\"}");
 const auto mode=std::string("{\"savedReopened\":true,\"scope\":\"native-plan-engine\",\"nativeWallsAndChainsRegistered\":")+
  (native?"true":"false")+",\"result\":\""+(native?"verified-native-plan-output":"pending-native-runtime")+"\"}";
 for(const auto* rule:{"T01","T05","G09"})productRecordRuleEvidence(job,rule,mode);
 const auto scope=std::string("{\"savedReopened\":true,\"scope\":\"independent-dwg-readback\",\"result\":\"verified-reopened-output-publication\",\"outputSHA256\":\"")+
  outputHash+"\",\"renderAndOpenItemsAreSeparate\":true}";
 for(const auto* rule:{"Q06","QA06"})productRecordRuleEvidence(job,rule,scope);
 const auto status=std::string("{\"savedReopened\":true,\"scope\":\"plan-delivery-and-rule-status\",\"result\":\"verified-separate-drawing-and-rule-status\",\"formalReady\":")+
  (job.formalReady?"true":"false")+",\"nativePendingCount\":"+
  std::to_string(job.detailPending.size())+"}";
 for(const auto* rule:{"Q10","QA02"})productRecordRuleEvidence(job,rule,status);
 if(!job.priorPlanLedger.empty()){
  require(sha(job.priorPlanLedger)==job.priorPlanLedgerHash,"previous plan ledger changed during generation");
  const auto copy=job.root/L"previous-plan-rule-ledger.json";
  fs::copy_file(job.priorPlanLedger,copy);
  require(sha(copy)==job.priorPlanLedgerHash,"previous rule ledger copy differs");
  const auto previous="{\"savedReopened\":true,\"scope\":\"previous-open-items-preserved\",\"result\":\"verified-prior-ledger-retained\",\"previousSHA256\":\""+
   job.priorPlanLedgerHash+"\",\"report\":\"previous-plan-rule-ledger.json\"}";
  for(const auto* rule:{"QA01","QA03"})productRecordRuleEvidence(job,rule,previous);
 }
 if(!job.planPublicCommands.empty()){
  std::ostringstream commands;
  commands<<"{\"savedReopened\":true,\"scope\":\"instrumented-public-command-paths\",\"commands\":[";
  bool first=true;
  for(const auto& [command,count]:job.planPublicCommands){
   if(!first)commands<<',';
   first=false;
   commands<<"{\"name\":"<<jsonUtf8(command)<<",\"successfulCalls\":"<<count<<'}';
  }
  commands<<"],\"result\":\"verified-public-cad-command-paths\"}";
  for(const auto* rule:{"T02","T04"})productRecordRuleEvidence(job,rule,commands.str());
 }
}
std::unique_ptr<ProductJob> productJob;
void productApplyCurrentPlanRules(ProductJob&,AcDbDatabase&);
void productApplyPlanDimensionStyle(ProductJob&,AcDbDatabase&,double,double);
void productVerifyPlanDimensionStyle(ProductJob&,AcDbDatabase&,double,double);
bcpproduct::PlanPaperPolicy productLoadPlanPaperPolicy(ProductJob& job,double scale,double unitMM){
 bcpproduct::PlanPaperPolicy policy;
 std::ifstream execution(job.profile/L"plan-policy/execution.tsv");
 if(execution)policy=bcpproduct::readPlanPaperPolicy(execution);
 else job.detailPending.push_back({u8"规则资源",u8"execution.tsv",u8"缺少可执行平面规则表，使用内置间距及尺寸样式规则并待核对"});
 job.dimensionStylePolicy=policy.dimensionStyle;
 job.planDimensionScale=scale;
 job.planDimensionUnitMM=unitMM;
 return policy;
}
std::wstring productSetting(const wchar_t* key,const std::wstring& fallback=L""){
 wchar_t value[32768]{};const auto n=GetPrivateProfileStringW(L"product",key,fallback.c_str(),value,32768,iniPath().c_str());require(n<32767,"product setting truncated");return {value,n};
}
bool productValidation(){return GetPrivateProfileIntW(L"product",L"validationMode",0,iniPath().c_str())==1;}
fs::path productResources(){auto path=productSetting(L"resourceRoot");return fs::weakly_canonical(path.empty()?fs::path(iniPath()).parent_path()/L"resources":fs::path(path));}
void productIni(const fs::path& file,const std::map<std::wstring,std::wstring>& values){
 require(!fs::exists(file),"runtime configuration already exists");std::wstring text=L"[stage2]\r\n";for(const auto& pair:values)text+=pair.first+L"="+pair.second+L"\r\n";
 File f;f.value=CreateFileW(file.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);require(f.value!=INVALID_HANDLE_VALUE,"cannot create runtime configuration");DWORD n{};const wchar_t bom=0xFEFF;require(WriteFile(f.value,&bom,2,&n,nullptr)&&n==2,"runtime BOM write");const auto bytes=static_cast<DWORD>(text.size()*sizeof(wchar_t));require(WriteFile(f.value,text.data(),bytes,&n,nullptr)&&n==bytes,"runtime configuration write");
}
std::map<std::wstring,std::wstring> productAssetSettings(const fs::path& root){
 const std::set<std::string> wanted{"confirmed-input.tsv","reference.dwg","axes.dwg","axes.tsv","layers.dwg","scheme.tsv","axis-exclusions.tsv","interior.tsv","prefixes.tsv"};
 std::ifstream input(root/L"resources.tsv");std::string tag,name,digest;int count{};require(static_cast<bool>(input>>tag>>count)&&tag=="BCPRESOURCES1"&&count==9,"resource manifest absent or invalid");std::set<std::string> seen;
 while(input>>name>>digest){require(wanted.count(name)&&seen.insert(name).second,"unexpected or duplicate product resource");const auto path=fs::weakly_canonical(root/fs::path(name));require(path.parent_path()==root&&sha(path)==digest,"product resource missing or fingerprint differs");}require(input.eof()&&seen==wanted,"incomplete product resources");
 const auto profile=root/L"profile.ini";const std::vector<std::wstring> flags{L"perimeterSpacingTrial",L"confirmedDimensionGap",L"exteriorDimensionEndpoints",L"fullInteriorAxes",L"axisPresentation",L"wallFollowingAxes",L"breakUnsupportedSpans",L"unifiedOpeningAnnotations",L"simplifyOpeningAnnotations",L"anchorCaptionsAndDirectThird",L"interiorDimensionReview"};std::map<std::wstring,std::wstring> settings;
 for(const auto& key:flags){require(GetPrivateProfileIntW(L"stage2",key.c_str(),0,profile.c_str())==1,"project generation flag is not supported by this release");settings.emplace(key,L"1");}require(GetPrivateProfileIntW(L"stage2",L"drawApron",-1,profile.c_str())==0,"apron generation is paused");settings.emplace(L"drawApron",L"0");
 for(const auto& pair:std::vector<std::pair<std::wstring,std::wstring>>{{L"cache",L"scheme.tsv"},{L"reference",L"reference.dwg"},{L"axisBank",L"axes.dwg"},{L"axisIndex",L"axes.tsv"},{L"layerStandard",L"layers.dwg"},{L"axisDecisions",L"axis-exclusions.tsv"},{L"interiorDimensionDecisions",L"interior.tsv"},{L"openingPrefixes",L"prefixes.tsv"}}){const auto path=root/pair.second;settings[pair.first]=path.wstring();const auto h=sha(path);settings[pair.first+L"Hash"]={h.begin(),h.end()};}return settings;
}
void productDependencies(){
 resbuf version{},secure{};require(acedGetVar(L"ACADVER",&version)==RTNORM&&version.restype==RTSTR,"AutoCAD version unavailable");const std::wstring ver=version.resval.rstring?version.resval.rstring:L"";if(version.resval.rstring)acutDelString(version.resval.rstring);require(ver.rfind(L"23.1",0)==0,"this release requires AutoCAD2020 R23.1");require(acedGetVar(L"SECURELOAD",&secure)==RTNORM&&secure.restype==RTSHORT&&secure.resval.rint==1,"keep SECURELOAD=1; use trusted installation paths");
 resbuf sdi{};require(acedGetVar(L"SDI",&sdi)==RTNORM&&sdi.restype==RTSHORT&&sdi.resval.rint==0,"this release requires SDI=0 (multiple documents); no setting is changed automatically");
 const fs::path tangent=productSetting(L"tangentDirectory",L"C:\\Tangent\\TArchT20V8\\SYS23x64");
 const auto load=[&](const wchar_t* name){const auto path=tangent/name;wchar_t trusted[32768]{};require(acedFindTrustedFile(path.c_str(),trusted,32768)==RTNORM&&fs::weakly_canonical(trusted)==fs::weakly_canonical(path),"Tianzheng component is not trusted; configure AutoCAD trusted paths manually");require(acrxDynamicLinker->loadModule(path.c_str(),false,false),"installed Tianzheng component cannot be loaded");};
 if(!acrxClassDictionary->at(L"TDbWall")||!acrxClassDictionary->at(L"TDbDimension2"))load(L"tch_kernal.arx");
 if(!acedRegCmds->lookupGlobalCmd(L"TMergeDim"))load(L"tch_annotate.arx");
 require(acrxClassDictionary->at(L"TDbWall")&&acrxClassDictionary->at(L"TDbDimension2")&&acedRegCmds->lookupGlobalCmd(L"TMergeDim"),"native Tianzheng objects/authorized dimension-join command unavailable");
}
bcp::Plan productPlan(const std::map<std::wstring,std::wstring>& settings){std::ifstream input(settings.at(L"cache"));require(static_cast<bool>(input),"project data cannot be opened");auto plan=bcp::readCache(input);std::ifstream exclusions(settings.at(L"axisDecisions"));bcp::readAxisExclusions(exclusions,plan);std::ifstream prefixes(settings.at(L"openingPrefixes"));bcp::readOpeningPrefixes(prefixes,plan);return plan;}
#include "ProductInput.inc"
#include "ProductDynamicInput.inc"
bcpproduct::FailureRecovery productFailure(const std::string& message)noexcept{
 return bcpproduct::recoverFailure(
  [&]{productWriteDiagnostic(message);if(!productJob){if(productValidation()){const fs::path receipt=ini(iniPath(),L"receiptDirectory");if(!fs::exists(receipt/L"product-result.json"))writeNew(receipt/L"product-result.json",std::string("{\"completed\":false,\"error\":")+jsonUtf8(bcpproduct::decodeMessage(message))+"}");}return;}const auto error=std::string("{\"contract\":\"bcp.product-result.v1\",\"version\":\"0.2.2\",\"completed\":false,\"error\":")+jsonUtf8(bcpproduct::decodeMessage(message))+"}";const auto path=productJob->root/L"生成失败.json";if(!fs::exists(path))writeNew(path,error);if(!productJob->testReceipt.empty()&&!fs::exists(productJob->testReceipt/L"product-result.json"))writeNew(productJob->testReceipt/L"product-result.json",error);if(productBatch&&!productBatch->receipt.empty()&&!fs::exists(productBatch->receipt/L"product-result.json"))writeNew(productBatch->receipt/L"product-result.json",error);},
  []{if(productJob&&productJob->locked){require(acrxDynamicLinker->unlockApplication(productApplication),"cannot unlock failed product task");productJob->locked=false;}productJob.reset();productBatch.reset();productRequestedPanel=0;},
  [&]{const auto text=bcpproduct::decodeMessage(message);acutPrintf(L"\n施工图生成未完成：%ls\n插件未保存或覆盖原方案。\n",text.c_str());if(!productDiagnostic.log.empty())acutPrintf(L"\n诊断报告：%ls\n",productDiagnostic.log.c_str());if(productJob)acutPrintf(L"\n任务收尾未能完成，模块仍保留加载状态。\n");});
}

void productWriteContracts(ProductJob& job,AcDbDatabase& db,const DrawnAnnotations& drawn,const bcp::Result& analysed){
 struct Segment {const DrawnDimension* d;AcGePoint3d a,b,text;};std::map<std::string,std::vector<Segment>> groups;
 for(const auto& d:drawn.dimensions){const auto gp=chainGrips(d.id);require(gp.size()==8,"product interval is not direct TCH");const auto key=bcpproduct::dimensionGroup(d.row,d.side,d.sourceWall);auto a=gp[2],b=gp[3];if((d.vertical?a.y:a.x)>(d.vertical?b.y:b.x))std::swap(a,b);groups[key].push_back({&d,a,b,gp[4]});}
 std::vector<std::pair<std::string,std::vector<Segment>>> runs;
 for(auto& group:groups){auto& list=group.second;std::sort(list.begin(),list.end(),[](const Segment& a,const Segment& b){return a.d->vertical?a.a.y<b.a.y:a.a.x<b.a.x;});int part=0;for(const auto& s:list){if(runs.empty()||runs.back().first!=group.first+":"+std::to_string(part)||(runs.back().second.back().b-s.a).length()>1e-6){if(!runs.empty()&&runs.back().first==group.first+":"+std::to_string(part))++part;runs.push_back({group.first+":"+std::to_string(part),{s}});}else{require(std::abs(runs.back().second.front().d->baseline-s.d->baseline)<1e-6,"continuous product dimension baseline differs");runs.back().second.push_back(s);}}}
 job.chains=static_cast<int>(runs.size());job.dimensions=static_cast<int>(drawn.dimensions.size());job.finalRoots=job.preparedRoots-job.dimensions+job.chains;require(job.dimensions>0&&job.chains>0&&job.chains<=job.dimensions&&job.finalRoots>0,"invalid dynamic generation contract");if(!job.dynamicInput&&!job.extendedInput)require(job.dimensions==106&&job.chains==24&&job.preparedRoots==390&&job.finalRoots==308,"project generation coverage differs");
 std::ostringstream contract;contract<<std::setprecision(17)<<"BCPCHAIN1 "<<job.preparedRoots<<' '<<job.dimensions<<' '<<job.chains<<'\n';for(const auto& run:runs){contract<<"CHAIN "<<run.first<<' '<<run.second.front().d->vertical<<' '<<run.second.size()<<'\n';for(const auto& s:run.second)contract<<"D "<<axisHandle(s.d->id)<<' '<<s.d->expected<<' '<<s.a.x<<' '<<s.a.y<<' '<<s.a.z<<' '<<s.b.x<<' '<<s.b.y<<' '<<s.b.z<<' '<<s.text.x<<' '<<s.text.y<<' '<<s.text.z<<' '<<s.d->baseline<<'\n';}writeNew(job.work/L"join/chain.tsv",contract.str());
 std::ostringstream captions;int doors=0;for(const auto& c:drawn.captions)if(!c.number.empty()&&c.number[0]==L'M')++doors;job.doorCount=doors;if(!job.dynamicInput)require(doors==14&&drawn.captions.size()==27,"product opening caption coverage differs");captions<<std::setprecision(17)<<"DOORCAP1 "<<job.finalRoots<<' '<<doors<<'\n';for(const auto& c:drawn.captions)if(!c.number.empty()&&c.number[0]==L'M')captions<<"CAP "<<axisHandle(c.id)<<' '<<c.opening<<' '<<c.side<<' '<<c.anchorA.x<<' '<<c.anchorA.y<<' '<<c.anchorB.x<<' '<<c.anchorB.y<<'\n';writeNew(job.work/L"caption/doors.tsv",captions.str());job.captions=static_cast<int>(drawn.captions.size());std::ostringstream lower;std::vector<std::string> rows;for(const auto& c:drawn.captions)if(!c.number.empty()&&c.number[0]==L'M'){for(const auto& chain:analysed.chains)if(std::find(chain.openings.begin(),chain.openings.end(),c.opening)!=chain.openings.end()){const auto wall=std::find_if(analysed.topology.walls.begin(),analysed.topology.walls.end(),[&](const bcp::WallRecognition& w){return w.handle==chain.wall;});require(wall!=analysed.topology.walls.end(),"door host recognition missing");if(wall->role!=bcp::WallRole::exterior)rows.push_back("CAP "+axisHandle(c.id)+" "+c.opening+" "+chain.wall);}}if(!job.dynamicInput)require(rows.size()==11,"fixed project interior M scope changed");lower<<"DOORLOWER1 "<<rows.size()<<'\n';for(const auto& row:rows)lower<<row<<'\n';writeNew(job.work/L"caption/interior-doors.tsv",lower.str());
}
std::string productDictionaryState(AcDbDatabase& db){AcDbDictionary* dict{};status(db.getNamedObjectsDictionary(dict,AcDb::kForRead),"open source named dictionary");std::unique_ptr<AcDbDictionaryIterator> it(dict->newIterator());std::map<std::wstring,std::string> entries;for(;!it->done();it->next())entries[it->name()]=axisHandle(it->objectId());dict->close();std::ostringstream out;for(const auto& entry:entries)out<<jsonUtf8(entry.first)<<":"<<entry.second<<"\n";if(entries.count(L"ACAD_WIPEOUT_VARS"))out<<"maskFrame70="<<witnessFrameSetting(db);return out.str();}
bool productDocumentAlive(AcApDocument* doc){std::unique_ptr<AcApDocumentIterator> it(acDocManager->newAcApDocumentIterator());for(;!it->done();it->step())if(it->document()==doc)return true;return false;}
std::string productFingerprint(AcDbDatabase& db){Working context(&db);return wholeProtected(db,{});}
const wchar_t* productSessionCommand(const wchar_t* command){
#ifdef PLAN_STANDALONE_ENTRY
 if(std::wstring_view(command)==L"_BCP_INTERNAL_OPEN_WORK\n")return L"_PLAN_DRAW_INTERNAL_OPEN_WORK\n";
 if(std::wstring_view(command)==L"_BCP_INTERNAL_OPEN_PREPARED\n")return L"_PLAN_DRAW_INTERNAL_OPEN_PREPARED\n";
 if(std::wstring_view(command)==L"_BCP_INTERNAL_OPEN_FINAL\n")return L"_PLAN_DRAW_INTERNAL_OPEN_FINAL\n";
 if(std::wstring_view(command)==L"_BCP_INTERNAL_FINISH\n")return L"_PLAN_DRAW_INTERNAL_FINISH\n";
#endif
 return command;
}
void productQueueSession(const wchar_t* command){require(productJob&&curDoc(),"product session queue has no active document");status(acDocManager->sendStringToExecute(curDoc(),productSessionCommand(command),true,false,false),"queue product session continuation");}
void productOpenWork(void*);void productOpenPrepared(void*);void productOpenFinal(void*);
void productStatus(Acad::ErrorStatus result,const char* action){if(result!=Acad::eOk)throw std::runtime_error(std::string(action)+" [Acad::ErrorStatus="+std::to_string(static_cast<int>(result))+"]");}
void productLogAxisFilters(AcDbDatabase& db,const DrawnAnnotations& drawn,const fs::path& file){Working w(&db);std::ostringstream out;out<<"[";bool first=true;for(const auto& a:drawn.axes){if(a.filter.isNull())continue;AcDbObjectId id;status(db.getAcDbObjectId(id,false,a.filter.handle()),"trace axis filter");auto f=open<AcDbSpatialFilter>(id);AcGePoint2dArray points;AcGeVector3d n;double z{},front{},back{};Adesk::Boolean on=false;status(f->getDefinition(points,n,z,front,back,on),"trace axis filter definition");if(!first)out<<",";first=false;out<<"{\"label\":"<<jsonUtf8(std::wstring(a.label.begin(),a.label.end()))<<",\"handle\":\""<<axisHandle(id)<<"\",\"enabled\":"<<(on?"true":"false")<<",\"inverted\":"<<(f->isInverted()?"true":"false")<<",\"expectedInverted\":"<<(a.clipInverted?"true":"false")<<",\"vertices\":"<<points.length()<<",\"normal\":["<<n.x<<","<<n.y<<","<<n.z<<"],\"elevation\":"<<z<<"}";}out<<"]";writeNew(file,out.str());}
#include "ProductApprovedLayout.inc"
#include "ProductPlanLayout.inc"
#include "ProductPlanDimensionStyle.inc"
#include "ProductPlanFurniture.inc"
#include "ProductDoorJambs.inc"
#include "ProductOutdoorDetails.inc"
#include "ProductCurrentPlanRules.inc"
#include "ProductLatestPrototype.inc"
#include "ProductThreeFloor.inc"
#include "ProductGenericPlan.inc"
#ifdef PLAN_STANDALONE_ENTRY
#include "ProductPlanPrintedInk.inc"
#include "ProductPlanPostcheck.inc"
#endif
void productGenerateFoundation(){
 require(curDoc()==productJob->ownedDocument,"foundation is not in owned preparation scratch");require(acdbHostApplicationServices()->workingDatabase()==curDoc()->database(),"scratch working database differs");ScopedNativeMaskFrame::set(L"WIPEOUTFRAME",0);require(ScopedNativeMaskFrame::get(L"WIPEOUTFRAME")==0,"private scratch mask frame did not apply");AcDbDatabase generated(false,true);status(generated.readDwgFile(productJob->sourceCopy.c_str(),AcDbDatabase::kForReadAndAllShare,false),"read owned generation source");status(generated.closeInput(true),"finish generation source read");
 if(productJob->genericInput){productJob->preparedRoots=productJob->finalRoots=static_cast<int>(modelTypes(generated).size());const auto control=productJob->work/L"join/control.dwg";productStatus(generated.saveAs(control.c_str(),false,AcDb::kDHL_CURRENT),"save actual generic input work copy");productJob->preparedHash=sha(control);return;}if(productJob->threeInput){productGenerateThree(*productJob,generated);return;}auto settings=productAssetSettings(productJob->profile);auto plan=productJob->dynamicInput?productPreparePanel(generated,*productJob,settings):productJob->currentPlan?*productJob->currentPlan:productPlan(settings);if(!productJob->dynamicInput)productVerifyLegacyParameters(generated,*productJob,plan);verify(generated,plan);if(productJob->dynamicInput){productGenerateApproved(*productJob,generated);return;}std::string supplement;const auto endpoints=captureGroupEndpoints(generated,plan,supplement);const auto result=bcp::analyse(plan);for(const auto& issue:result.issues)productJob->pending.push_back({issue.code,issue.handle});
 const auto drawn=drawAnnotations(generated,plan,result,productJob->generationConfig.wstring());require(drawn.apronStrokes.empty()&&drawn.apronTexts.empty(),"paused apron module generated objects");productJob->preparedRoots=static_cast<int>(modelTypes(generated).size());productWriteContracts(*productJob,generated,drawn,result);productLogAxisFilters(generated,drawn,productJob->work/L"foundation/spatial-live.json");const auto control=productJob->work/L"join/control.dwg";productStatus(generated.saveAs(control.c_str(),false,AcDb::kDHL_CURRENT),"save product generated working drawing");{AcDbDatabase saved(false,true);status(saved.readDwgFile(control.c_str(),AcDbDatabase::kForReadAndAllShare,false),"reopen generated working drawing");status(saved.closeInput(true),"finish generated readback");productLogAxisFilters(saved,drawn,productJob->work/L"foundation/spatial-saved.json");verify(saved,plan,productJob->preparedRoots-plan.sourceObjectCount);verifyDrawnAnnotations(saved,drawn);}
 writeNew(productJob->work/L"foundation/annotations.json",drawn.report);writeNew(productJob->work/L"foundation/analysis.json",bcp::toJson(plan,result));writeNew(productJob->work/L"foundation/endpoints.json",endpoints);productJob->preparedHash=sha(control);require(acdbHostApplicationServices()->workingDatabase()==curDoc()->database(),"factory left an unexpected working database");writeNew(productJob->root/L"foundation-native-verified.step","side database verified; own scratch environment is discarded with its document\n");
}
void productStartBatch(const fs::path& source,const fs::path& base,const fs::path& receipt);
void productStart(const fs::path& source,const fs::path& output,const fs::path& testReceipt={},const fs::path& jobResources={},
                  bool unified=false,double scale=100,double unit=1,const fs::path& entrance={},const fs::path& stairs={},
                  const fs::path& report={},const fs::path& detailConfirmations={},const fs::path& furnitureComponents={},
                  const fs::path& roofPlan={},const fs::path& previousPlanLedger={},const fs::path& floorLevels={},
                  const fs::path& panelScope={},bool printReview=false,const std::wstring& plotStyle={},
                  const fs::path& projectParameters={},const fs::path& sheetChecks={},bool formal=false,
                  const fs::path& roomNameReference={},bool architecturalPlanOnly=false){
 require(!productJob,"another construction drawing job is still active");bcpproduct::validateOutput(source,output);productDependencies();const auto profile=jobResources.empty()?productResources():jobResources;std::map<std::wstring,std::wstring> settings;bcp::Plan plan;bool dynamic=false,three=false,generic=true;
 // Validate saved input before creating output directories. Save-only changes are allowed;
 // this project profile is matched by actual public geometry, not by byte identity alone.
 {productDiagnostic.source=source;productDiagnostic.output=output;productDiagnostic.stage=L"输入建筑校核";AcDbDatabase input(false,true);status(input.readDwgFile(source.c_str(),AcDbDatabase::kForReadAndAllShare,false),"read input for project preflight");status(input.closeInput(true),"input preflight read complete");const auto actual=productInventory(input);productDiagnostic.input=productInventoryJson(actual);/* Legacy drafting presets are optional optimizations, never an input admission schema. Missing or nonmatching presets use the same geometry-driven entry. */if(!unified&&productSetting(L"useVerifiedProjectPreset",L"0")==L"1"&&fs::exists(profile/L"scheme.tsv")&&fs::exists(profile/L"resources.tsv")){try{settings=productAssetSettings(profile);plan=productPlan(settings);three=productMatchesThree(input,profile);dynamic=!three&&productMatchesTwoPlan(input,profile,actual.counts.objects);if(three||dynamic)generic=false;else{plan.sourceObjectCount=actual.counts.objects;verify(input,plan);generic=false;}}catch(const std::exception&){three=dynamic=false;generic=true;settings.clear();}}if(generic)require(actual.counts.walls>0,u8"E_NATIVE_CONTENT：未读到可处理的天正墙体；请检查所需构件组件及输入类型。新项目不按案例对象数量拒绝。");else if(three||dynamic)require(actual.proxies==0&&actual.generatedCarriers==0,"configured source contains proxies or generated axes");}
 if(dynamic&&!productBatch){productStartBatch(source,output,testReceipt);return;}
 auto job=std::make_unique<ProductJob>();job->threeInput=three;job->dynamicInput=dynamic;job->genericInput=generic;job->extendedInput=!dynamic&&plan.sourceObjectCount!=213;if(!dynamic)job->currentPlan=plan;job->panelIndex=productRequestedPanel;job->source=fs::weakly_canonical(source);job->output=fs::weakly_canonical(output);job->sourceHash=sha(source);job->profile=profile;job->unifiedPlan=unified;job->unifiedScale=scale;job->unifiedUnitMM=unit;job->unifiedEntranceSteps=entrance;job->unifiedIndoorStairs=stairs;productLoadRuleCatalog(*job,unified);job->testReceipt=testReceipt;job->validation=!testReceipt.empty();const auto token=std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64());job->root=report.empty()?output.parent_path()/(output.stem().wstring()+L"_生成记录_"+token):report;if(report.empty())require(fs::create_directory(job->root),"unique generation directory");job->work=job->root/L"过程";for(const auto& stage:{L"foundation",L"join",L"caption"})require(fs::create_directories(job->work/stage),"create native product stage");job->sourceCopy=job->work/L"foundation/input.dwg";fs::copy_file(source,job->sourceCopy);require(sha(job->sourceCopy)==job->sourceHash,"input copy changed");job->generationConfig=job->work/L"foundation/generation.ini";settings[L"receiptDirectory"]=(job->work/L"foundation").wstring();productIni(job->generationConfig,settings);
 job->planSourceDbmod=productDiagnostic.dbmod;
 job->unifiedDetailConfirmations=detailConfirmations;
 job->unifiedFurnitureComponents=furnitureComponents;
 job->unifiedRoofPlan=roofPlan;
 job->unifiedFloorLevels=floorLevels;
 job->unifiedPanelScope=panelScope;
 job->planPrintReview=printReview||formal;
 job->formalPlan=formal;
 job->architecturalPlanOnly=architecturalPlanOnly;
 job->planPlotStyle=plotStyle;
 job->unifiedProjectParameters=projectParameters;
 job->unifiedSheetChecks=sheetChecks;
 job->unifiedRoomNameReference=roomNameReference;
 if(!projectParameters.empty())job->unifiedProjectParametersHash=sha(projectParameters);
 if(!previousPlanLedger.empty()){
  require(fs::is_regular_file(previousPlanLedger),u8"E_PLAN_RESOURCE：找不到上一轮平面规则台账。");
  job->priorPlanLedger=previousPlanLedger;
  job->priorPlanLedgerHash=sha(previousPlanLedger);
 }
 if(curDoc()&&fs::weakly_canonical(curDoc()->fileName())==fs::weakly_canonical(source)){job->sourceDocument=curDoc();Working sourceScope(job->sourceDocument->database());job->sourceMemory=wholeProtected(*job->sourceDocument->database(),{},job->unifiedPlan);job->sourceDictionary=productDictionaryState(*job->sourceDocument->database());}productJob=std::move(job);acutPrintf(L"\n正在生成轴网、尺寸和门窗编号……\n");
 const auto control=productJob->work/L"foundation/control.dwg";fs::copy_file(productJob->sourceCopy,control);productJob->preparedHash=sha(control);productJob->locked=acrxDynamicLinker->lockApplication(productApplication);require(productJob->locked,"cannot lock plugin for document callback");productQueueSession(L"_BCP_INTERNAL_OPEN_WORK\n");
}
void productStartBatch(const fs::path& source,const fs::path& base,const fs::path& receipt={}){
 require(!productBatch&&!productJob,"another batch is active");bcpproduct::validateOutput(source,base);ProductBatch batch;batch.source=source;batch.base=base;batch.receipt=receipt;
 for(const auto name:{L"一层",L"二层"}){auto output=base.parent_path()/(base.stem().wstring()+L"_"+name+L".dwg");bcpproduct::validateOutput(source,output);batch.outputs.push_back(output);}productBatch=std::move(batch);productRequestedPanel=0;fs::path child;if(!receipt.empty()){child=receipt/L"floor-1";require(fs::create_directory(child),"batch receipt must be new");}productStart(source,productBatch->outputs[0],child);
}
void productOpenWork(void* data){if(!productJob||data!=productJob.get())return;try{require(acDocManager->isApplicationContext(),"product opener requires application context");AcApDocManager::DocOpenParams p{};const auto path=productJob->work/L"foundation/control.dwg";p.mpwszFileName=path.c_str();p.mnInitialViewType=AcApDocManager::DocOpenParams::kDefaultView;p.mnFlags=AcApDocManager::DocOpenParams::kFileNameArgIsUnicode;status(acDocManager->appContextOpenDocument(&p),"open owned generated document");auto* doc=acDocManager->mdiActiveDocument();require(doc&&fs::weakly_canonical(doc->fileName())==fs::weakly_canonical(path),"opened document is not owned work");productJob->ownedDocument=doc;productJob->phase=ProductPhase::Generate;status(acDocManager->sendStringToExecute(doc,productSessionCommand(L"_BCP_INTERNAL_FINISH\n"),true,false,false),"queue native product finalize");}catch(const std::exception& e){productFailure(e.what());}catch(...){productFailure("unknown product open failure");}}
void productOpenPrepared(void* data){if(!productJob||data!=productJob.get())return;try{auto& job=*productJob;require(acDocManager->isApplicationContext()&&job.preparedRoots>0,"prepared document opener context");auto* scratch=job.ownedDocument;require(productDocumentAlive(scratch)&&fs::weakly_canonical(scratch->fileName())==fs::weakly_canonical(job.work/L"foundation/control.dwg"),"preparation scratch identity differs");const auto path=job.work/L"join/control.dwg";AcApDocManager::DocOpenParams p{};p.mpwszFileName=path.c_str();p.mnInitialViewType=AcApDocManager::DocOpenParams::kDefaultView;p.mnFlags=AcApDocManager::DocOpenParams::kFileNameArgIsUnicode;writeNew(job.root/L"before-prepared-open.step","before open prepared\n");status(acDocManager->appContextOpenDocument(&p),"open verified generated work");auto* doc=acDocManager->mdiActiveDocument();require(doc&&doc!=scratch&&fs::weakly_canonical(doc->fileName())==fs::weakly_canonical(path),"prepared result is not owned work");job.ownedDocument=doc;job.phase=ProductPhase::Join;writeNew(job.root/L"prepared-opened.step","opened verified generated doc\n");status(acDocManager->appContextCloseDocument(scratch),"close own preparation scratch");job.preparationDocumentClosed=true;writeNew(job.root/L"preparation-closed.step","closed own preparation scratch\n");status(acDocManager->sendStringToExecute(doc,productSessionCommand(L"_BCP_INTERNAL_FINISH\n"),true,false,false),"queue product dimension finalization");}catch(const std::exception& e){productFailure(e.what());}catch(...){productFailure("unknown prepared document open failure");}}
void productFinish()noexcept{
 try{require(productJob&&curDoc()==productJob->ownedDocument&&acDocManager->mdiActiveDocument()==productJob->ownedDocument,"product finalize is not in the owned document");auto& job=*productJob;const auto join=job.work/L"join",caption=job.work/L"caption";const auto control=join/L"control.dwg";if(job.preparedRoots==0){require(sha(job.work/L"foundation/control.dwg")==job.preparedHash,"owned preparation drawing changed");productGenerateFoundation();writeNew(job.root/L"foundation-returned.step","side database released\n");job.phase=ProductPhase::OpenPrepared;productQueueSession(L"_BCP_INTERNAL_OPEN_PREPARED\n");return;}require(sha(control)==job.preparedHash,"prepared product drawing changed");if(job.genericInput){productFinishGeneric();return;}if(job.threeLayout){productFinishThree();return;}if(job.approvedLayout){productFinishApproved();return;}const auto component=fs::path(productSetting(L"tangentDirectory",L"C:\\Tangent\\TArchT20V8\\SYS23x64"))/L"tch_annotate.arx";const auto hashWide=[](const std::string& h){return std::wstring(h.begin(),h.end());};
  job.joinConfig=join/L"join.ini";productIni(job.joinConfig,{{L"receiptDirectory",join.wstring()},{L"allowTMergeDim",L"1"},{L"chainInput",control.wstring()},{L"chainInputHash",hashWide(job.preparedHash)},{L"chainContract",(join/L"chain.tsv").wstring()},{L"chainContractHash",hashWide(sha(join/L"chain.tsv"))},{L"chainComponent",component.wstring()},{L"chainComponentHash",hashWide(sha(component))},{L"joinExpectedRoots",std::to_wstring(job.preparedRoots)},{L"joinExpectedSegments",std::to_wstring(job.dimensions)},{L"joinExpectedChains",std::to_wstring(job.chains)}});acutPrintf(L"\n正在连接整体天正尺寸链……\n");wholeDimensionChains(job.joinConfig.wstring());
  const auto joined=join/L"方案施工图_整体连续天正尺寸.dwg";job.captionConfig=caption/L"caption.ini";productIni(job.captionConfig,{{L"receiptDirectory",caption.wstring()},{L"captionInput",joined.wstring()},{L"captionInputHash",hashWide(sha(joined))},{L"captionContract",(caption/L"doors.tsv").wstring()},{L"captionContractHash",hashWide(sha(caption/L"doors.tsv"))},{L"captionExpectedRoots",std::to_wstring(job.finalRoots)},{L"captionExpectedCount",std::to_wstring(job.doorCount)},{L"captionGraphicProbe",L"1"},{L"dynamicInput",job.dynamicInput?L"1":L"0"},{L"interiorDoorLower",L"1"},{L"interiorDoorContract",(caption/L"interior-doors.tsv").wstring()},{L"interiorDoorContractHash",hashWide(sha(caption/L"interior-doors.tsv"))}});acutPrintf(L"\n正在检查门编号避让……\n");doorCaptionClearance(job.captionConfig.wstring());
  const auto corrected=caption/L"方案施工图_门编号避让.dwg";AcDbDatabase ready(false,true);status(ready.readDwgFile(corrected.c_str(),AcDbDatabase::kForReadAndAllShare,false),"read corrected native result");status(ready.closeInput(true),"finish corrected result read");const auto targets=readDoorCaptionTargets(caption/L"doors.tsv",job.finalRoots,job.doorCount);auto* active=curDoc()->database();for(const auto& t:targets){auto text=open<AcDbText>(find(ready,t.text));setDoorCaptionPosition(*active,find(*active,t.text),text->alignmentPoint());}productLatestStyle(*active);productLatestStyle(ready);job.finalRoots=static_cast<int>(modelTypes(*active).size());require(bcpproduct::samePublicGeometry(productFingerprint(*active),productFingerprint(ready)),"active result differs from validated native result");
  productApplyCurrentPlanRules(job,*active);job.finalRoots=static_cast<int>(modelTypes(*active).size());const auto finalized=productFingerprint(*active);require(sha(job.source)==job.sourceHash&&!fs::exists(job.output),"input changed or output appeared during generation");productStatus(active->saveAs(control.c_str(),true,AcDb::kDHL_CURRENT),"save own work document before closing");const auto candidate=job.work/L"ready-to-publish.dwg";status(active->saveAs(candidate.c_str(),false,AcDb::kDHL_CURRENT),"save final construction candidate");
  {AcDbDatabase saved(false,true);status(saved.readDwgFile(candidate.c_str(),AcDbDatabase::kForReadAndAllShare,false),"reopen final construction drawing");status(saved.closeInput(true),"finish final construction read");require(modelTypes(saved).size()==job.finalRoots&&bcpproduct::samePublicGeometry(productFingerprint(saved),finalized),"final native result changed on save/reopen");
   productVerifyPlanDimensionStyle(job,saved,job.planDimensionScale,job.planDimensionUnitMM);
  }
  bcpproduct::publishNewFile(candidate,job.output);
  std::ostringstream csv;csv<<"\xEF\xBB\xBF"<<u8"类别,对象,状态,说明\r\n";for(const auto& issue:job.pending){const bool number=issue.first=="OPENING_NUMBER_RULE_REQUIRED";csv<<(number?u8"门窗编号,":u8"墙体用途,")<<issue.second<<u8",待确认,"<<(number?u8"洞口宽高不在已确认数字编码规则内":u8"墙体用途未明确 未擅自新增轴线")<<" ("<<issue.first<<")\r\n";}{std::ifstream sideIssues(job.work/L"caption/interior-door-side-confirmation.tsv");std::string tag,h;int unknown{};require(static_cast<bool>(sideIssues>>tag>>unknown)&&tag=="DOORSIDE1"&&unknown>=0&&unknown<=1000,"door side issue header");std::set<std::string> ids;for(int i=0;i<unknown;++i){require(static_cast<bool>(sideIssues>>tag>>h)&&tag=="OPEN"&&ids.insert(h).second,"door side issue row");auto opening=open<AcDbEntity>(find(*active,h));require(std::wstring(opening->isA()->name())==L"TDbOpening","door side issue is not an opening");csv<<u8"门编号侧向,"<<h<<u8",待确认,方案未提供开启弧 暂沿用原方案侧向\r\n";}sideIssues>>std::ws;require(sideIssues.eof(),"door side issues trailing data");}{std::ifstream reviews(job.work/L"caption/interior-door-placement-review.tsv");std::string tag,h;int n{};require(static_cast<bool>(reviews>>tag>>n)&&tag=="DOORREVIEW1"&&n>=0&&n<=1000,"door review header");std::set<std::string> ids;for(int i=0;i<n;++i){require(static_cast<bool>(reviews>>tag>>h)&&tag=="OPEN"&&ids.insert(h).second,"door review row");find(*active,h);csv<<u8"门编号布置,"<<h<<u8",待确认,开启侧下方无安全空位 暂保留原编号位置并复核墙线、家具及图面\r\n";}reviews>>std::ws;require(reviews.eof(),"door review trailing data");}for(const auto& item:std::vector<std::pair<std::string,std::string>>{{u8"散水与明沟",u8"暂停，未生成"},{u8"正式图框与CTB",u8"待补资料及正式出图验收"},{u8"构造节点与详图索引",u8"待固定详图库和参数"},{u8"楼梯厨卫专项定位",u8"待后续模块实现"}})csv<<item.first<<u8",项目,"<<item.second<<u8",首版未作为完成项\r\n";writeNew(job.root/L"待确认清单.csv",csv.str());job.phase=ProductPhase::OpenFinal;productQueueSession(L"_BCP_INTERNAL_OPEN_FINAL\n");
 }catch(const std::exception& e){productFailure(e.what());}catch(...){productFailure("unknown native product finalize failure");}
}
void productOpenFinal(void* data){if(!productJob||data!=productJob.get())return;try{auto& job=*productJob;require(acDocManager->isApplicationContext(),"result opener requires application context");auto candidate=job.unifiedPlan?job.work/L"ready-to-publish.dwg":job.output;AcApDocManager::DocOpenParams p{};p.mpwszFileName=candidate.c_str();p.mnInitialViewType=AcApDocManager::DocOpenParams::kDefaultView;p.mnFlags=AcApDocManager::DocOpenParams::kFileNameArgIsUnicode;status(acDocManager->appContextOpenDocument(&p),"open final product drawing");auto* final=acDocManager->mdiActiveDocument();require(final&&final!=job.ownedDocument&&fs::weakly_canonical(final->fileName())==fs::weakly_canonical(candidate),"final result document not activated");require(productDocumentAlive(job.ownedDocument),"owned intermediate was closed before final open");const auto closed=acDocManager->appContextCloseDocument(job.ownedDocument);require(closed==Acad::eOk,"owned intermediate document did not close normally");require(sha(job.source)==job.sourceHash,"source changed during final open");
  if(job.sourceDocument){
   require(productDocumentAlive(job.sourceDocument),"original input document was closed before final memory check");
   Working sourceScope(job.sourceDocument->database());
   const auto geometry=wholeProtected(*job.sourceDocument->database(),{},job.unifiedPlan);
   const auto dictionary=productDictionaryState(*job.sourceDocument->database());
   const bool same=job.unifiedPlan?bcpproduct::samePublicGeometry(geometry,job.sourceMemory):geometry==job.sourceMemory;
   if(job.validation&&(!same||dictionary!=job.sourceDictionary)){
    writeNew(job.root/L"source-memory-before.json",job.sourceMemory);
    writeNew(job.root/L"source-memory-after.json",geometry);
    writeNew(job.root/L"source-dictionary-before.txt",job.sourceDictionary);
    writeNew(job.root/L"source-dictionary-after.txt",dictionary);
   }
   require(same&&dictionary==job.sourceDictionary,"source document public geometry or named dictionary changed in memory");
  }
  auto finalHash=sha(candidate);
  if(!job.planResumeOutput.empty()){
   require(sha(job.planResumeOutput)==job.planResumeOutputHash,"prior result changed during formal revision");
  }
  if(job.sourceInventory.sealed){
   require(finalHash==job.planVerifiedCandidateHash,"published DWG differs from independently reopened candidate");
   require(sha(job.root/L"plan-source-inventory.json")==job.planInventoryHash,"source inventory changed during generation");
  }
#ifdef PLAN_STANDALONE_ENTRY
  const auto initialCandidateHash=finalHash;
  const auto generationMilliseconds=GetTickCount64()-job.startedAtTick;
  if(job.planPrintReview){
   if(job.architecturalPlanOnly&&job.planInkAnalyzer.empty())
    job.drawingChecks.record(0,"printed-ink","pdf",{"D17","D23","P06"},bcpproduct::PlanReleaseGate::files,
     bcpproduct::PlanCheck::failed,u8"实际打印尺寸净空",u8"正式作业须完成原生打印字形复验");
   try{
    if(!job.planInkAnalyzer.empty())productRepairPlottedPlan(job,final,candidate);
    else{productPrintPlanReview(job,*final,candidate);productPrintPlanLayouts(job,*final,candidate);}
   }
   catch(const std::exception& error){
    job.detailPending.push_back({u8"打印校核","",error.what()});
    job.drawingChecks.record(0,"formal-layout-print","project",{"P03","P04","QA02"},bcpproduct::PlanReleaseGate::files,
     bcpproduct::PlanCheck::failed,u8"正式布局打印未完整执行",error.what());
   }
  }
  finalHash=sha(candidate);
  if(job.sourceInventory.sealed)
   require(finalHash==job.planVerifiedCandidateHash,"paper repair output differs from its independent readback");
  if(job.planFinalDimensionCheck)job.planFinalDimensionCheck(*final->database(),candidate);
  if(job.architecturalPlanOnly){
   AcDbDatabase reference(false,true);productStatus(reference.readDwgFile((job.profile/L"reference.dwg").c_str(),AcDbDatabase::kForReadAndAllShare,false),"open final typography reference");
   productStatus(reference.closeInput(true),"close final typography resource");
   productAuditWholePlanTypography(job,*final->database(),reference,candidate,job.root/L"final-typography-readback.json");
  }
  if(job.architecturalPlanOnly)productReconcileOldAnnotations(job,*final->database(),finalHash);
  if(job.architecturalPlanOnly&&job.planPostcheckMode!=L"off")
   productAuditSavedPlan(job,candidate,initialCandidateHash,finalHash,generationMilliseconds);
  if(!job.planResumeOutput.empty())
   writeNew(job.root/L"plan-resume.json","{\"mode\":\"source-bound-full-revalidation\",\"sourceSHA256\":\""+
    job.sourceHash+"\",\"priorOutputSHA256\":\""+job.planResumeOutputHash+
    "\",\"priorLedgerSHA256\":\""+job.priorPlanLedgerHash+"\",\"candidateSHA256\":\""+finalHash+"\"}");
  try{productWritePlanDeliveryReview(job,*final->database(),finalHash);}
  catch(const std::exception& error){
   job.formalReady=false;
   job.formalDelivery=false;
   std::string reason="\"";
   for(const char c:std::string(error.what())){if(c=='"')reason+='"';reason+=c;}
   reason+='"';
   job.planBlockingChecklist=std::string("\xEF\xBB\xBF")+
    u8"源图幅,实际部位,源对象,问题,已有依据,所缺依据,影响,处理,DWG SHA-256\r\n0,全套,,"+
    reason+u8",,,正式验收,修正验收错误并复验,"+finalHash+"\r\n";
   job.detailPending.push_back({u8"施工图交付","",error.what()});
   const auto row=std::string("{\"savedReopened\":true,\"result\":\"failed-formal-delivery-review\",\"detail\":")+
    jsonUtf8(bcpproduct::decodeMessage(error.what()))+"}";
   for(const auto* rule:{"P03","P04","QA02","Q10"})productRecordRuleEvidence(job,rule,row);
  }
#endif
  if(!job.latestThreeModel&&!job.genericInput)productOutdoorDetails(job,*final->database(),ProductPlanLayoutConfig{});
  if(job.unifiedPlan){
   productStatus(acDocManager->appContextCloseDocument(final),"close independently reopened candidate");
   require(sha(candidate)==finalHash,"candidate changed during validation");
   auto delivered=job.output;
   fs::path checklist;
   if(!job.formalReady){
    auto stem=delivered.stem().wstring();
    if(job.formalDelivery)checklist=delivered.parent_path()/(stem+L"_遗留清单.csv");
    else{
     const std::wstring suffix=L"_待复核";
     if(stem.size()<suffix.size()||stem.compare(stem.size()-suffix.size(),suffix.size(),suffix)!=0)stem+=suffix;
     delivered=delivered.parent_path()/(stem+delivered.extension().wstring());
     checklist=delivered.parent_path()/(stem+L"_卡点清单.csv");
    }
    require(!job.planBlockingChecklist.empty(),"missing drawing exceptions checklist");
   }
   bcpproduct::validateOutput(job.source,delivered);
   const auto staging=delivered.parent_path()/(L".plan-draw-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
   require(fs::create_directory(staging),"create private delivery staging beside output");
   const auto stagedDrawing=staging/L"drawing.dwg",stagedChecklist=staging/L"blockers.csv";
   bool checklistWritten=false;
   try{
    fs::copy_file(candidate,stagedDrawing);
    require(sha(stagedDrawing)==finalHash,"staged DWG differs from independently reopened candidate");
    if(!checklist.empty()){
     writeNew(stagedChecklist,job.planBlockingChecklist);
     bcpproduct::publishNewFile(stagedChecklist,checklist);
     checklistWritten=true;
    }
    bcpproduct::publishNewFile(stagedDrawing,delivered);
   }catch(...){
    std::error_code error;
    if(checklistWritten)fs::remove(checklist,error);
    fs::remove(stagedDrawing,error);
    fs::remove(stagedChecklist,error);
    fs::remove(staging,error);
    throw;
   }
   std::error_code error;
   fs::remove(staging,error);
   job.planDeliveredChecklist=checklist;
   job.output=delivered;
   require(sha(job.output)==finalHash,"delivered drawing differs from independently reopened candidate");
  }
  if(job.sourceInventory.sealed)productWritePlanInventory(job,finalHash);
  productRecordPlanPublished(job);
  productWriteDetailIssues(job);productWriteRuleLedger(job,finalHash,job.rulePanels);
  if(job.locked){require(acrxDynamicLinker->unlockApplication(productApplication),"cannot unlock completed product module");job.locked=false;}
  const auto deliveryStatus=job.formalReady?"formal":job.formalDelivery?"formal-with-exceptions":job.unifiedPlan?"pending-review":"generated";
  std::ostringstream out;
  out<<"{\"contract\":\"bcp.product-result.v1\",\"version\":\"0.2.2\",\"source\":"<<jsonUtf8(job.source.wstring())<<",\"sourceSHA256\":\""<<job.sourceHash<<"\",\"output\":"<<jsonUtf8(job.output.wstring())<<",\"outputSHA256\":\""<<finalHash<<"\",\"records\":"<<jsonUtf8(job.root.wstring())<<",\"preparedRoots\":"<<job.preparedRoots<<",\"finalRoots\":"<<job.finalRoots<<",\"dimensionChains\":"<<job.chains<<",\"dimensionSegments\":"<<job.dimensions<<",\"openingCaptions\":"<<job.captions<<",\"pendingWallRoles\":"<<job.pending.size()<<",\"preparationDocumentClosed\":"<<(job.preparationDocumentClosed?"true":"false")<<",\"intermediateDocumentClosed\":true,\"finalDocumentOpened\":true,\"sourceUnchanged\":true,\"sourceMemoryObserved\":"<<(job.sourceDocument?"true":"false")<<",\"apronGenerated\":false,\"COMUsed\":false,\"ComputerUseUsed\":false,\"TianzhengCommand\":\""<<(job.genericInput?"TMergeDim,TReAxisNo,TObjEdit,TChWinLab":job.latestThreeModel?"SDK native project resources":job.threeLayout?"TChWinLab":job.approvedLayout?(job.dimensionMergeCommandUsed?"TChWinLab,TObjEdit,TMergeDim":"TChWinLab,TObjEdit"):job.axisSideCommandUsed?"TMergeDim,TObjEdit":"TMergeDim")<<"\",\"pythonRequiredAtRuntime\":false,\"completed\":true,\"published\":true,\"deliveryStatus\":\""<<deliveryStatus
     <<"\",\"blockingChecklist\":"<<jsonUtf8(job.planDeliveredChecklist.wstring())
     <<",\"checklistSHA256\":\""<<(job.planDeliveredChecklist.empty()?std::string{}:sha(job.planDeliveredChecklist))<<'"'
     <<",\"formalDelivery\":"<<(job.formalDelivery?"true":"false")
     <<",\"constructionDrawingComplete\":"<<(job.formalReady?"true":"false")<<",\"error\":null}";
  writeNew(job.root/L"生成校验.json",out.str());if(!job.testReceipt.empty()){writeNew(job.testReceipt/L"product-result.json",out.str());writeNew(job.testReceipt/L"stage2-analysis.done",out.str());}
  productLastOutput=job.output;
  productLastIssues=job.planDeliveredChecklist.empty()?job.root/(job.formalPlan?L"生成错误清单.csv":L"待确认清单.csv"):job.planDeliveredChecklist;
  const auto message=job.formalReady?L"\n正式施工图已生成：%ls\n验收记录：%ls\n":
   job.formalDelivery?L"\n正式版建筑平面已生成：%ls\n遗留清单：%ls\n":
   job.unifiedPlan?L"\n待复核 DWG 已生成：%ls\n卡点清单：%ls\n":L"\n基础施工图已生成：%ls\n待确认清单：%ls\n";
  acutPrintf(message,job.output.c_str(),productLastIssues.c_str());
  productJob.reset();
 if(productBatch){auto& batch=*productBatch;if(++batch.panel<2){productRequestedPanel=batch.panel;fs::path child;if(!batch.receipt.empty()){child=batch.receipt/L"floor-2";require(fs::create_directory(child),"second-floor receipt must be new");}productStart(batch.source,batch.outputs[batch.panel],child);}else{std::ostringstream summary;summary<<"{\"contract\":\"bcp.two-plan-result.v1\",\"completed\":true,\"sourceSHA256\":\""<<sha(batch.source)<<"\",\"outputs\":[";for(std::size_t i=0;i<batch.outputs.size();++i){if(i)summary<<',';summary<<"{\"path\":"<<jsonUtf8(batch.outputs[i].wstring())<<",\"SHA256\":\""<<sha(batch.outputs[i])<<"\"}";}summary<<"],\"error\":null}";if(!batch.receipt.empty())writeNew(batch.receipt/L"product-result.json",summary.str());productBatch.reset();productRequestedPanel=0;}}
 }catch(const std::exception& e){productFailure(e.what());}catch(...){productFailure("unknown final product document failure");}}
void productInfo()noexcept{acutPrintf(L"\n建筑施工图助手 0.2.2 / AutoCAD2020 x64\n按当前图纸几何分幅并生成轴网和整体天正尺寸，不按项目名、数量或坐标准入。已核定项目保留原专项能力；通用分支尚未覆盖全部面积、构造及门型识别，缺项列清单。未知对象保留，不能把生成结束等同全部施工图完成。\n输入 BCP 生成，选择输出文件即可；BCP_BUILD 仍可用。BCP_ISSUES 查看清单。散水暂停；其他构造与正式出图仍待后续模块和资料。\n");}
void productCheck()noexcept{try{productDiagnostic={};const auto source=productCheckDocument();productInspectFile(source);}catch(const std::exception& e){productFailure(e.what());}}
void productIssues()noexcept{if(productLastIssues.empty()){acutPrintf(L"\n请先生成一次施工图。\n");return;}acutPrintf(L"\n待确认清单：%ls\n",productLastIssues.c_str());}
#include "ProductDocumentProbe.inc"
void productBuild()noexcept{
 if(productJob){acutPrintf(L"\n已有生成任务正在运行，请等待完成。\n");return;}try{if(productValidation()){isolated(ini(iniPath(),L"nonce"));const fs::path source=productSetting(L"testInput"),output=productSetting(L"testOutput"),receipt=ini(iniPath(),L"receiptDirectory");productDiagnostic={};productDocumentProbe(source,receipt);productCheckDocument();if(productSetting(L"testBehavior")==L"fixture-extra"){productExtraFixture(source,receipt);return;}if(productSetting(L"testBehavior").rfind(L"inspect",0)==0){productInspectFile(source,receipt);return;}productStart(source,output,receipt);return;}
  productDiagnostic={};const fs::path source=productCheckDocument();const auto suggested=source.parent_path()/(source.stem().wstring()+L"_施工图.dwg");resbuf selected{};const auto picked=acedGetFileD(L"另存建筑施工图（保留原方案）",suggested.c_str(),L"dwg",1,&selected);if(picked!=RTNORM){if(selected.restype==RTSTR&&selected.resval.rstring)acutDelString(selected.resval.rstring);acutPrintf(L"\n已取消，原方案未改动。\n");return;}require(selected.restype==RTSTR&&selected.resval.rstring,"output selection missing");const fs::path output=selected.resval.rstring;acutDelString(selected.resval.rstring);productStart(source,output);
 }catch(const std::exception& e){productFailure(e.what());}catch(...){productFailure("unknown product command failure");}
}
void productOpenWorkSession(){if(productJob&&productJob->phase==ProductPhase::OpenWork)productOpenWork(productJob.get());}
void productOpenPreparedSession(){if(productJob&&productJob->phase==ProductPhase::OpenPrepared)productOpenPrepared(productJob.get());}
void productOpenFinalSession(){if(productJob&&productJob->phase==ProductPhase::OpenFinal)productOpenFinal(productJob.get());}
#include "ProductAudit.inc"
void productRecoveryTest()noexcept{try{require(productValidation()&&!productJob,"recovery test requires idle validation module");isolated(ini(iniPath(),L"nonce"));const fs::path receipt=ini(iniPath(),L"receiptDirectory");require(curDoc()&&fs::weakly_canonical(curDoc()->fileName())==fs::weakly_canonical(receipt/L"control.dwg"),"recovery test source is not owned control");const auto blocked=receipt/L"blocked-diagnostic-location";writeNew(blocked,"This regular file deliberately prevents child log creation.\n");productJob=std::make_unique<ProductJob>();productJob->root=blocked;productJob->locked=acrxDynamicLinker->lockApplication(productApplication);require(productJob->locked,"cannot acquire recovery test module lock");const auto outcome=productFailure("validation: diagnostic destination is not a directory");std::ostringstream out;out<<"{\"contract\":\"bcp.product-recovery-test.v1\",\"realDiagnosticWriteFailure\":"<<(outcome.diagnosticFailed?"true":"false")<<",\"jobCleared\":"<<(!productJob?"true":"false")<<",\"retainedJobLock\":"<<(productJob&&productJob->locked?"true":"false")<<",\"sourceUnmodifiedByTest\":true}";writeNew(receipt/L"product-recovery-test.json",out.str());}catch(const std::exception& e){acutPrintf(L"\nBCP_RECOVERY_TEST_FAILED %hs\n",e.what());}}
AcRx::AppRetCode productEntry(AcRx::AppMsgCode msg,void* pkt)noexcept{try{if(msg==AcRx::kInitAppMsg){productApplication=pkt;acrxUnlockApplication(pkt);acrxRegisterAppMDIAware(pkt);for(const auto& command:std::vector<std::pair<const wchar_t*,void(*)()>>{{L"BCP",productBuild},{L"BCP_BUILD",productBuild},{L"BCP_INFO",productInfo},{L"BCP_CHECK",productCheck},{L"BCP_ISSUES",productIssues},{L"BCP_INTERNAL_FINISH",productFinish}})if(acedRegCmds->addCommand(L"BCP_PRODUCT",command.first,command.first,ACRX_CMD_MODAL,command.second)!=Acad::eOk)return AcRx::kRetError;for(const auto& command:std::vector<std::pair<const wchar_t*,void(*)()>>{{L"BCP_INTERNAL_OPEN_WORK",productOpenWorkSession},{L"BCP_INTERNAL_OPEN_PREPARED",productOpenPreparedSession},{L"BCP_INTERNAL_OPEN_FINAL",productOpenFinalSession}})if(acedRegCmds->addCommand(L"BCP_PRODUCT",command.first,command.first,ACRX_CMD_SESSION,command.second)!=Acad::eOk)return AcRx::kRetError;if(productValidation()){if(acedRegCmds->addCommand(L"BCP_PRODUCT",L"BCP_STAGE2_RECOVERY_TEST",L"BCP_STAGE2_RECOVERY_TEST",ACRX_CMD_MODAL,productRecoveryTest)!=Acad::eOk)return AcRx::kRetError;if(acedRegCmds->addCommand(L"BCP_PRODUCT",L"BCP_STAGE2_ANALYZE",L"BCP_STAGE2_ANALYZE",ACRX_CMD_MODAL,productBuild)!=Acad::eOk||acedRegCmds->addCommand(L"BCP_PRODUCT",L"BCP_STAGE2_MASKAUDIT",L"BCP_STAGE2_MASKAUDIT",ACRX_CMD_MODAL,productAudit)!=Acad::eOk)return AcRx::kRetError;}productInfo();}else if(msg==AcRx::kUnloadAppMsg){if(productJob)return AcRx::kRetError;acedRegCmds->removeGroup(L"BCP_PRODUCT");}return AcRx::kRetOK;}catch(...){return AcRx::kRetError;}}
