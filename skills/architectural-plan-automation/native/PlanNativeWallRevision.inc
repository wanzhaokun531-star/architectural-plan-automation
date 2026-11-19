namespace {
struct PlanWallRevision {
 JointPaperJob sheets;
 fs::path resources, scope, floorLevels, working, targets;
 bool skipPrint{};
};

std::unique_ptr<PlanWallRevision> nativeWallRevision;

struct NativeWallScope {
 GenericNativeInput input;
 std::vector<bcpproduct::GenericView> views;
 std::vector<std::optional<int>> floors;
 std::vector<bool> selected;
 std::vector<bcpproduct::PlanLevelResult> levels;
 std::map<std::string,std::pair<std::string,std::string>> excluded;
 std::set<std::string> heightOnlyPointWalls;
};

NativeWallScope readNativeWallScope(ProductJob& job,AcDbDatabase& db,const PlanWallRevision& revision){
 NativeWallScope out;
 out.input=productReadGenericInput(job,db);
 // Height editing does not require a measurable plan baseline. Preserve the
 // real entity and use its finite XY extent solely for floor ownership.
 std::set<std::string> geometricWalls;for(const auto& wall:out.input.walls)geometricWalls.insert(wall.wall.handle);
 for(const auto& [raw,type]:modelTypes(db))if(type==L"TDbWall"&&!geometricWalls.count(ascii(raw))){
  const auto handle=ascii(raw);const auto fields=out.input.properties.find(handle);if(fields==out.input.properties.end())continue;
  const auto& values=fields->second;
  if(!values.count(L"高度")||!values.count(L"底标高")||!values.count(L"左厚度")||!values.count(L"右厚度"))continue;
  const auto entity=open<AcDbEntity>(find(db,handle));const auto* curve=AcDbCurve::cast(entity.get());AcGePoint3d a,b;AcDbExtents box;
  if(!curve||curve->getStartPoint(a)!=Acad::eOk||curve->getEndPoint(b)!=Acad::eOk||a.distanceTo(b)>1e-6||entity->getGeomExtents(box)!=Acad::eOk)continue;
  const auto at=box.minPoint()+(box.maxPoint()-box.minPoint())*.5;
  if(!std::isfinite(at.x)||!std::isfinite(at.y))continue;
  GenericNativeWall wall;wall.wall.handle=handle;wall.wall.start={at.x,at.y};wall.wall.end=wall.wall.start;
  wall.wall.left=bcpproduct::propertyNumber(values,L"左厚度");wall.wall.right=bcpproduct::propertyNumber(values,L"右厚度");wall.total=wall.wall.left+wall.wall.right;
  wall.low=values.count(L"对象类型")&&values.at(L"对象类型")==L"矮墙";
  out.input.walls.push_back(wall);out.heightOnlyPointWalls.insert(handle);
 }

 if(!revision.targets.empty()){
  std::ifstream frozen(revision.targets);std::string tag,digest;
  require(bool(frozen>>tag>>digest)&&tag=="PLANWALLTARGETS1"&&digest==revision.sheets.hash,"frozen walls must match exact input");
  std::set<std::size_t> panels;std::set<std::string> actual,expected;
  std::map<std::string,std::size_t> indices;
  for(std::size_t i=0;i<out.input.walls.size();++i)indices.emplace(out.input.walls[i].wall.handle,i);
  for(const auto& [handle,type]:modelTypes(db))if(type==L"TDbWall")actual.insert(ascii(handle));
  while(frozen>>tag&&tag!="END"){
   if(tag=="FLOOR"){
    std::size_t panel{};int floor{};std::string height,finished,basis,datum;
    require(bool(frozen>>panel>>floor>>height>>basis>>finished>>datum)&&panel>0&&panel<=actual.size()+1&&panels.insert(panel).second,"frozen floor association");
    if(panel>out.views.size()){out.views.resize(panel);out.floors.resize(panel);out.selected.resize(panel);out.levels.resize(panel);}
    out.selected[panel-1]=true;out.floors[panel-1]=floor;out.views[panel-1].bounds={1e100,1e100,-1e100,-1e100};
    auto& level=out.levels[panel-1];level.heightMM=height=="null"?std::optional<double>{}:std::optional<double>{std::stod(height)};
    level.finishedMetres=finished=="null"?std::optional<double>{}:std::optional<double>{std::stod(finished)};
    require(!level.heightMM||(std::isfinite(*level.heightMM)&&*level.heightMM>0),"invalid frozen height");
    level.heightBasis=basis;level.datumBasis=datum=="none"?"":datum;
   }else if(tag=="WALL"){
    std::size_t panel{};std::string handle;
    require(bool(frozen>>panel>>handle)&&panels.count(panel)&&expected.insert(handle).second&&indices.count(handle),"frozen wall missing, duplicated or unreadable");
    const auto index=indices.at(handle);auto& view=out.views[panel-1];view.walls.push_back(index);const auto& wall=out.input.walls[index].wall;
    view.bounds.xmin=std::min({view.bounds.xmin,wall.start.x,wall.end.x});view.bounds.xmax=std::max({view.bounds.xmax,wall.start.x,wall.end.x});
    view.bounds.ymin=std::min({view.bounds.ymin,wall.start.y,wall.end.y});view.bounds.ymax=std::max({view.bounds.ymax,wall.start.y,wall.end.y});
   }else{
    std::string handle,role,reason;
    require(tag=="OMIT"&&bool(frozen>>handle>>role>>std::quoted(reason))&&expected.insert(handle).second,"frozen omitted wall record");
    out.excluded[handle]={role,reason};
   }
  }
  require(tag=="END"&&!(frozen>>tag)&&actual==expected,"frozen wall inventory differs from final native DWG");
  for(const auto panel:panels)require(!out.views[panel-1].walls.empty(),"empty frozen floor");
  return out;
 }
 std::vector<bcp::Wall> walls;
 for(const auto& wall:out.input.walls)walls.push_back(wall.wall);
 out.views=bcpproduct::discoverGenericViews(walls);
 auto owners=productIdentifyPlanPanels(out.input,out.views,job.unifiedUnitMM);
 bool layoutOwnership=false;
 if(revision.scope.empty()){
  std::size_t assigned{};for(std::size_t i=0;i<out.views.size();++i)if(owners[i].role==ProductPanelRole::floor)assigned+=out.views[i].walls.size();
  if(assigned<out.input.walls.size()){
   struct LayoutFloor {bcpproduct::GenericBounds box;int floor;};std::vector<LayoutFloor> layoutFloors;
   const auto layouts=open<AcDbDictionary>(db.layoutDictionaryId());AcDbDictionaryIterator* raw=layouts->newIterator();std::unique_ptr<AcDbDictionaryIterator> it(raw);
   for(;!it->done();it->next()){
    const auto layout=open<AcDbLayout>(it->objectId());if(layout->modelType())continue;
    const auto record=open<AcDbBlockTableRecord>(layout->getBlockTableRecordId());AcDbBlockTableRecordIterator* item{};status(record->newIterator(item),"read wall floor viewports");std::unique_ptr<AcDbBlockTableRecordIterator> e(item);
    for(;!e->done();e->step()){
     AcDbObjectId id;status(e->getEntityId(id),"wall viewport id");const auto entity=open<AcDbEntity>(id);const auto* vp=AcDbViewport::cast(entity.get());
     if(!vp||!vp->isLocked()||vp->height()<=0||vp->width()<=0||vp->isPerspectiveOn()||std::abs(vp->twistAngle())>1e-8||!vp->viewDirection().isEqualTo(AcGeVector3d::kZAxis))continue;
     const double h=vp->viewHeight(),w=h*vp->width()/vp->height();const auto c=vp->viewTarget()+AcGeVector3d(vp->viewCenter().x,vp->viewCenter().y,0.);
     bcpproduct::GenericBounds box{c.x-w/2,c.y-h/2,c.x+w/2,c.y+h/2};std::set<int> floors;
     for(const auto& title:out.input.titles)if(title.anchor.x>=box.xmin&&title.anchor.x<=box.xmax&&title.anchor.y>=box.ymin&&title.anchor.y<=box.ymax)
      if(const auto f=bcpproduct::explicitPlanFloor(title.text))floors.insert(*f);
     if(floors.size()==1)layoutFloors.push_back({box,*floors.begin()});
    }
   }
   if(!layoutFloors.empty()){
    std::sort(layoutFloors.begin(),layoutFloors.end(),[](const auto& a,const auto& b){return std::tie(a.floor,a.box.xmin,a.box.ymin)<std::tie(b.floor,b.box.xmin,b.box.ymin);});
    std::vector<bcpproduct::GenericView> grouped(layoutFloors.size());std::vector<ProductPanelOwnership> ownership(layoutFloors.size());std::set<std::size_t> used;
    for(std::size_t i=0;i<layoutFloors.size();++i){grouped[i].bounds={1e100,1e100,-1e100,-1e100};ownership[i].role=ProductPanelRole::floor;ownership[i].floor=layoutFloors[i].floor;ownership[i].reason=u8"按已保存平面视口及视口内唯一楼层图名确定，合并本层墙片";}
    for(std::size_t k=0;k<out.input.walls.size();++k){const auto& wall=out.input.walls[k].wall;std::vector<std::size_t> matches;
     for(std::size_t i=0;i<layoutFloors.size();++i){const auto& b=layoutFloors[i].box;
      if(wall.start.x>=b.xmin&&wall.start.x<=b.xmax&&wall.end.x>=b.xmin&&wall.end.x<=b.xmax&&wall.start.y>=b.ymin&&wall.start.y<=b.ymax&&wall.end.y>=b.ymin&&wall.end.y<=b.ymax)matches.push_back(i);}
     if(matches.size()!=1)continue;const auto i=matches.front();used.insert(k);auto& v=grouped[i];v.walls.push_back(k);
     v.bounds.xmin=std::min({v.bounds.xmin,wall.start.x,wall.end.x});v.bounds.xmax=std::max({v.bounds.xmax,wall.start.x,wall.end.x});v.bounds.ymin=std::min({v.bounds.ymin,wall.start.y,wall.end.y});v.bounds.ymax=std::max({v.bounds.ymax,wall.start.y,wall.end.y});
    }
    for(std::size_t i=0;i<out.views.size();++i){auto remaining=out.views[i];remaining.walls.erase(std::remove_if(remaining.walls.begin(),remaining.walls.end(),[&](auto k){return used.count(k)>0;}),remaining.walls.end());if(!remaining.walls.empty()){grouped.push_back(std::move(remaining));ownership.push_back({});}}
    if(std::all_of(grouped.begin(),grouped.begin()+layoutFloors.size(),[](const auto& v){return !v.walls.empty();})){out.views=std::move(grouped);owners=std::move(ownership);layoutOwnership=true;}
   }
  }
 }
 std::set<std::string> recognized;
 for(const auto& wall:out.input.walls)recognized.insert(wall.wall.handle);
 for(const auto& [handle,type]:modelTypes(db))if(type==L"TDbWall"&&!recognized.count(ascii(handle)))
  out.excluded[ascii(handle)]={"unreadable",u8"原生墙未取得有效可读基线及属性，未作普通墙改高"};
 out.floors.resize(owners.size());
 out.selected.resize(owners.size());
 if(revision.scope.empty()){
  for(std::size_t i=0;i<owners.size();++i)if(owners[i].role==ProductPanelRole::floor&&owners[i].floor){
   out.selected[i]=true;out.floors[i]=owners[i].floor;
  }
 }else{
 std::ifstream request(revision.scope);
 std::string tag,digest,anchor;
 require(bool(request>>tag>>digest)&&tag=="PLANWALL1"&&digest==revision.sheets.hash,
         "wall scope must match the exact saved DWG");
 std::set<int> floors;
 while(request>>tag&&tag=="FLOOR"){
  int floor{};
  require(bool(request>>floor>>anchor)&&floors.insert(floor).second,"unique wall floor in verified revision");
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
 }
 for(std::size_t i=0;i<out.views.size();++i)if(!out.selected[i])for(const auto index:out.views[i].walls)
  out.excluded[out.input.walls[index].wall.handle]=owners[i].role==ProductPanelRole::roof?
   std::make_pair(std::string("roof"),std::string(u8"屋面构造墙按自身用途保留，不套主体楼层高度")):
   std::make_pair(std::string("unassigned"),std::string(u8"墙体未取得可靠主体楼层归属，保留原墙并列清单"));
 const std::vector<std::optional<ProductRoomGrid>> rooms(out.views.size());
 productGeneratePlanDatums(job,db,out.input,out.views,out.selected,out.floors,rooms,out.levels,true);
 // The source-bound explicit project profile has priority. A stored height is
 // reused only on the same floor component and never overrides a conflict.
 if(!layoutOwnership&&revision.floorLevels.empty()&&revision.targets.empty()){
  const auto stored=productReadPlanLevels(db);
  for(std::size_t i=0;i<out.views.size();++i)if(out.selected[i]){
   const auto it=stored.find(i+1);
   if(it!=stored.end()&&std::isfinite(it->second.first)&&it->second.first>0&&
      out.levels[i].heightBasis=="rule-default"&&it->second.second!="rule-default"){
    out.levels[i].heightMM=it->second.first;out.levels[i].heightBasis="persisted-"+it->second.second;
   }
  }
 }

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
        <<",\"basis\":\""<<level.heightBasis<<"\",\"datumBasis\":\""<<level.datumBasis<<"\",\"walls\":[";
  bool firstWall=true;
  for(const auto index:data.views[i].walls){
   const auto& wall=data.input.walls[index];
   const auto& fields=data.input.properties.at(wall.wall.handle);
   if(!firstWall)result<<',';
   firstWall=false;
   result<<"{\"handle\":\""<<wall.wall.handle<<"\",\"nativeLowWall\":"<<(wall.low?"true":"false")
         <<",\"heightOnlyDegenerate\":"<<(data.heightOnlyPointWalls.count(wall.wall.handle)?"true":"false")
         <<",\"height\":"<<(fields.count(L"高度")?std::to_string(bcpproduct::propertyNumber(fields,L"高度")):"null")
         <<",\"base\":"<<(fields.count(L"底标高")?std::to_string(bcpproduct::propertyNumber(fields,L"底标高")):"null");
   if(fields.count(L"左厚度")&&fields.count(L"右厚度"))result<<",\"left\":"<<bcpproduct::propertyNumber(fields,L"左厚度")<<",\"right\":"<<bcpproduct::propertyNumber(fields,L"右厚度");
   result<<",\"start\":["<<std::setprecision(17)<<wall.wall.start.x<<','<<wall.wall.start.y<<"],\"end\":["<<wall.wall.end.x<<','<<wall.wall.end.y<<"]}";
  }
  result<<"]}";
 }
 result<<"],\"excludedWalls\":[";first=true;
 for(const auto& [handle,reason]:data.excluded){if(!first)result<<',';first=false;
  result<<"{\"handle\":\""<<handle<<"\",\"role\":\""<<reason.first<<"\",\"reason\":"<<jsonUtf8(bcpproduct::decodeMessage(reason.second))<<'}';
 }
 result<<"]}";
 writeNew(report/L"wall-height-preflight.json",result.str());
}

void completeNativeWallRevision(const PlanWallRevision& request){
 writeNew(request.sheets.report/L"product-result.json","{\"completed\":true,\"dwgReopened\":true,\"sourceSHA256\":\""+
   request.sheets.hash+"\",\"outputSHA256\":\""+sha(request.sheets.output)+"\"}");
}

void printNativeWallRevision(void*){
 auto request=std::move(nativeWallRevision);
 AcApDocument* opened{};
 try{
  auto& sheets=request->sheets;
  require(sha(sheets.input)==sheets.hash,"wall revision modified its source");
  if(request->skipPrint){
   auto* active=acDocManager->mdiActiveDocument();
   if(active&&fs::equivalent(active->fileName(),request->working))
    productStatus(acDocManager->appContextCloseDocument(active),"close wall-only revision scratch");
   completeNativeWallRevision(*request);return;
  }
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
