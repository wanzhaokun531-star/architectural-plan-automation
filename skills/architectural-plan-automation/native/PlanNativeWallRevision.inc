namespace {
struct PlanWallRevision {
 JointPaperJob sheets;
 fs::path resources, scope, floorLevels, working;
};

std::unique_ptr<PlanWallRevision> nativeWallRevision;

struct NativeWallScope {
 GenericNativeInput input;
 std::vector<bcpproduct::GenericView> views;
 std::vector<std::optional<int>> floors;
 std::vector<bool> selected;
 std::vector<bcpproduct::PlanLevelResult> levels;
};

NativeWallScope readNativeWallScope(ProductJob& job,AcDbDatabase& db,const PlanWallRevision& revision){
 NativeWallScope out;
 out.input=productReadGenericInput(job,db);
 std::vector<bcp::Wall> walls;
 for(const auto& wall:out.input.walls)walls.push_back(wall.wall);
 out.views=bcpproduct::discoverGenericViews(walls);
 const auto owners=productIdentifyPlanPanels(out.input,out.views,job.unifiedUnitMM);
 out.floors.resize(owners.size());
 out.selected.resize(owners.size());
 std::ifstream request(revision.scope);
 std::string tag,digest,anchor;
 require(bool(request>>tag>>digest)&&tag=="PLANWALL1"&&digest==revision.sheets.hash,
         "wall scope must match the exact saved DWG");
 std::set<int> floors;
 while(request>>tag&&tag=="FLOOR"){
  int floor{};
  require(bool(request>>floor>>anchor)&&floor>0&&floors.insert(floor).second,"unique wall floor in verified revision");
  const auto view=std::find_if(out.views.begin(),out.views.end(),[&](const auto& candidate){
   return std::any_of(candidate.walls.begin(),candidate.walls.end(),[&](auto i){return walls[i].handle==anchor;});
  });
  require(view!=out.views.end(),"wall floor reference is absent in the saved drawing");
  const auto i=std::size_t(view-out.views.begin());
  require(!out.selected[i]&&owners[i].role==ProductPanelRole::floor&&owners[i].floor==floor,
          "wall scope disagrees with the saved native floor title and walls");
  out.selected[i]=true;
  out.floors[i]=floor;
 }
 require(tag=="END"&&!(request>>tag)&&!floors.empty(),"complete wall floor scope");
 const std::vector<std::optional<ProductRoomGrid>> rooms(out.views.size());
 productGeneratePlanDatums(job,db,out.input,out.views,out.selected,out.floors,rooms,out.levels,true);
 return out;
}

void writeNativeWallPreflight(const NativeWallScope& data,const fs::path& report){
 std::ostringstream result;
 result<<"{\"floors\":[";
 bool first=true;
 for(std::size_t i=0;i<data.views.size();++i){
  if(!data.selected[i])continue;
  if(!first)result<<',';
  first=false;
  const auto& level=data.levels[i];
  const auto number=[](const std::optional<double>& value){return value?std::to_string(*value):"null";};
  result<<"{\"component\":"<<i+1<<",\"floor\":"<<*data.floors[i]
        <<",\"finishedMetres\":"<<number(level.finishedMetres)<<",\"targetMM\":"<<number(level.heightMM)
        <<",\"basis\":\""<<level.heightBasis<<"\",\"walls\":[";
  bool firstWall=true;
  for(const auto index:data.views[i].walls){
   const auto& wall=data.input.walls[index];
   const auto& fields=data.input.properties.at(wall.wall.handle);
   if(!firstWall)result<<',';
   firstWall=false;
   result<<"{\"handle\":\""<<wall.wall.handle<<"\",\"nativeLowWall\":"<<(wall.low?"true":"false")
         <<",\"height\":"<<bcpproduct::propertyNumber(fields,L"高度")<<'}';
  }
  result<<"]}";
 }
 result<<"]}";
 writeNew(report/L"wall-height-preflight.json",result.str());
}

void printNativeWallRevision(void*){
 auto request=std::move(nativeWallRevision);
 AcApDocument* opened{};
 try{
  auto& sheets=request->sheets;
  require(sha(sheets.input)==sheets.hash,"wall revision modified its source");
  opened=productOpenPrintedInkCandidate(sheets.output);
  ProductJob plot;
  {Working working(opened->database());jointPaperSheets(plot,*opened->database(),sheets);}
  productPrintPlanLayouts(plot,*opened,sheets.output,sheets.report);
  productStatus(acDocManager->appContextCloseDocument(opened),"close wall revision plot document");
  opened=nullptr;
  auto* active=acDocManager->mdiActiveDocument();
  if(active&&fs::equivalent(active->fileName(),request->working))
   productStatus(acDocManager->appContextCloseDocument(active),"close saved wall revision scratch");
  writeNew(sheets.report/L"product-result.json","{\"completed\":true,\"dwgReopened\":true,\"sourceSHA256\":\""+
    sheets.hash+"\",\"outputSHA256\":\""+sha(sheets.output)+"\"}");
 }catch(const std::exception& error){
  if(opened)acDocManager->appContextCloseDocument(opened);
  writeNew(request->sheets.report/L"product-result.json","{\"completed\":false,\"error\":"+
   jsonUtf8(bcpproduct::decodeMessage(error.what()))+"}");
 }
}

void writeNativeWallRevision(){
 if(!nativeWallRevision)return;
 auto& request=*nativeWallRevision;
 try{
  require(!productJob&&curDoc()&&fs::equivalent(curDoc()->fileName(),request.working),"wall changes require the owned scratch document");
  auto job=std::make_unique<ProductJob>();
  job->root=request.sheets.report;job->profile=request.resources;
  job->sourceHash=request.sheets.hash;job->ownedDocument=curDoc();job->unifiedPlan=true;
  job->unifiedFloorLevels=request.floorLevels;
  productJob=std::move(job);
  auto& operation=*productJob;
  auto& db=*operation.ownedDocument->database();
  Working working(&db);
  ApprovedCommandVars vars;
  productLoadRuleCatalog(operation,true);
  const auto data=readNativeWallScope(operation,db,request);
  writeNativeWallPreflight(data,operation.root);
  std::set<std::string> targets;
  std::map<std::string,ProductHeightGeometry> wallGeometry;
  for(std::size_t i=0;i<data.views.size();++i)if(data.selected[i]){
   for(const auto* scope:{"floor-datum","story-height"})
    operation.sourceInventory.expect(i+1,scope,"",{"B04","SE02"},"selected floor","");
   for(const auto index:data.views[i].walls){
    const auto& wall=data.input.walls[index];
    if(wall.low)continue;
    const auto& handle=wall.wall.handle;
    require(targets.insert(handle).second,"each wall must belong to exactly one floor");
    wallGeometry.emplace(handle,productHeightGeometry(find(db,handle),true));
    operation.sourceInventory.expect(i+1,"wall-height",handle,{"B04","SE02"},"selected native wall","");
   }
  }
  const auto original=modelTypes(db);
  const auto protectedPaper=productPostcheckProtectedTextContainers(db);
  const auto changed=productApplyWallHeights(operation,db,data.input,data.views,data.selected,data.levels);
  require(changed.size()==targets.size(),"write and verify every selected native wall");
  productStorePlanLevels(db,data.levels,data.selected);
  status(db.saveAs(request.working.c_str(),true,AcDb::kDHL_CURRENT),"save owned wall revision scratch");
  require(fs::copy_file(request.working,request.sheets.output),"save new wall revision DWG");
  AcDbDatabase saved(false,true),control(false,true);
  readDrawing(saved,request.sheets.output);
  readDrawing(control,request.sheets.input);
  const auto controlPath=operation.root/L"wall-serialization-control.dwg";
  {Working reference(&control);status(control.saveAs(controlPath.c_str(),false,AcDb::kDHL_CURRENT),"save wall serialization control");}
  AcDbDatabase compare(false,true);
  readDrawing(compare,controlPath);
  std::string expected;
  {Working reference(&compare);expected=wholeProtected(compare,targets,true);}
  {
   Working reopened(&saved);
   require(modelTypes(saved)==original&&bcpproduct::samePublicGeometry(expected,wholeProtected(saved,targets,true))&&
     bcpproduct::samePublicGeometry(protectedPaper,productPostcheckProtectedTextContainers(saved)),
     "wall revision affected unrelated entities or paper layouts");
   for(const auto& [handle,state]:wallGeometry)
    require(productSameHeightGeometry(state,productHeightGeometry(find(saved,handle),true),.01),
            "saved wall geometry changed apart from height");
   productVerifyWallHeights(operation,saved,changed,data.levels,data.selected);
  }
  productJob.reset();
  acDocManager->executeInApplicationContext(printNativeWallRevision,nullptr);
 }catch(const std::exception& error){
  productJob.reset();
  writeNew(request.sheets.report/L"product-result.json","{\"completed\":false,\"error\":"+
    jsonUtf8(bcpproduct::decodeMessage(error.what()))+"}");
  nativeWallRevision.reset();
 }
}

void openNativeWallRevision(void*){
 try{
  auto* document=productOpenPrintedInkCandidate(nativeWallRevision->working);
  status(acDocManager->sendStringToExecute(document,L"PLAN_WALL_WRITE_INTERNAL\n",true,false,false),"queue native wall revision");
 }catch(const std::exception& error){
  writeNew(nativeWallRevision->sheets.report/L"product-result.json","{\"completed\":false,\"error\":"+
    jsonUtf8(bcpproduct::decodeMessage(error.what()))+"}");
  nativeWallRevision.reset();
 }
}

void runNativeWallRevision(PlanWallRevision request){
 require(!productJob&&!nativeWallRevision&&curDoc()&&fs::equivalent(curDoc()->fileName(),request.sheets.input),
         "wall preflight requires the owned source drawing");
 if(!request.sheets.output.empty()){
  request.working=request.sheets.report/L"wall-working.dwg";
  require(fs::copy_file(request.sheets.input,request.working),"make the owned wall revision scratch");
  nativeWallRevision=std::make_unique<PlanWallRevision>(std::move(request));
  acDocManager->executeInApplicationContext(openNativeWallRevision,nullptr);
  return;
 }
 auto job=std::make_unique<ProductJob>();
 job->root=request.sheets.report;job->profile=request.resources;job->sourceHash=request.sheets.hash;
 job->ownedDocument=curDoc();job->unifiedPlan=true;job->unifiedFloorLevels=request.floorLevels;
 productJob=std::move(job);
 try{
  auto& db=*curDoc()->database();
  Working working(&db);
  productLoadRuleCatalog(*productJob,true);
  const auto data=readNativeWallScope(*productJob,db,request);
  writeNativeWallPreflight(data,request.sheets.report);
  require(sha(request.sheets.input)==request.sheets.hash,"wall preflight changed source");
  writeNew(request.sheets.report/L"product-result.json","{\"completed\":true,\"sourceSHA256\":\""+
   request.sheets.hash+"\",\"readOnly\":true}");
 }catch(...){productJob.reset();throw;}
 productJob.reset();
}
}
