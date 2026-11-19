namespace {
struct JointPaperSheet {
 int panel{};
 std::wstring name;
 std::string viewport;
};

struct JointPaperChain {
 std::string handle;
 int panel{};
 double normal{};
 bool reverseBaseline{};
 std::map<std::size_t,AcGePoint3d> textPositions;
};

struct JointPaperNote {
 std::string handle;
 int panel{};
 AcGeVector3d shift;
 std::array<double,4> region{};
 std::optional<std::pair<AcGePoint3d,AcGePoint3d>> leader;
};

struct JointPaperJob {
 fs::path input,output,report;
 std::string hash;
 std::vector<JointPaperSheet> sheets;
 std::map<std::string,JointPaperChain> chains;
 std::map<std::string,JointPaperNote> notes;
 std::map<std::string,int> observed;
};

JointPaperJob readJointPaperJob(const fs::path& source,const fs::path& request,const fs::path& target,const fs::path& report,bool allowNoSheets=false){
 std::ifstream in(request);
 std::string tag,digest;
 require(bool(in>>tag>>digest)&&tag=="PLANJOINT1"&&digest==sha(source),"joint annotation plan must match the exact DWG");
 JointPaperJob job{source,target,report,digest};
 while(in>>tag&&tag!="END"){
  if(tag=="SHEET"){
   int panel{};std::string name;
   require(bool(in>>panel>>std::quoted(name))&&panel==job.sheets.size()+1&&!name.empty(),"joint annotation sheet");
   job.sheets.push_back({panel,bcpproduct::decodeMessage(name)});
   continue;
  }
  if(tag=="VIEWPORT"){
   std::size_t panel{};std::string handle;
   require(bool(in>>panel>>handle)&&panel>0&&panel<=job.sheets.size()&&job.sheets[panel-1].viewport.empty(),
      "bound saved plot viewport");
   job.sheets[panel-1].viewport=handle;
   continue;
  }
  std::string handle;int panel{};
  require(bool(in>>panel>>handle)&&panel>0&&handle.find_first_not_of("0123456789ABCDEF")==std::string::npos,"joint annotation object");
  if(tag=="CHAIN"||tag=="REVERSE"){
   double delta{};
   require(bool(in>>delta)&&std::isfinite(delta)&&std::abs(delta)<=1800&&
     job.chains.emplace(handle,JointPaperChain{handle,panel,delta,tag=="REVERSE"}).second,"joint chain baseline change");
  }else if(tag=="TEXT"){
   std::size_t part{};AcGePoint3d point;
   const auto found=job.chains.find(handle);
   require(bool(in>>part>>point.x>>point.y)&&std::isfinite(point.x)&&std::isfinite(point.y)&&
     found!=job.chains.end()&&found->second.panel==panel&&found->second.textPositions.emplace(part,point).second,
     "joint chain text anchor");
  }else if(tag=="NOTE"){
   JointPaperNote note{handle,panel};
   require(bool(in>>note.shift.x>>note.shift.y>>note.region[0]>>note.region[1]>>note.region[2]>>note.region[3])&&
     std::isfinite(note.shift.length())&&note.shift.length()<=2500&&
     std::all_of(note.region.begin(),note.region.end(),[](double a){return std::isfinite(a);})&&
     note.region[0]<note.region[2]&&note.region[1]<note.region[3]&&
     !job.chains.count(handle)&&job.notes.emplace(handle,note).second,"joint source note within a bounded original area");
  }else if(tag=="LEADER"){
   const auto note=job.notes.find(handle);
   AcGePoint3d tip,landing;
   require(bool(in>>tip.x>>tip.y>>landing.x>>landing.y)&&note!=job.notes.end()&&note->second.panel==panel&&
      !note->second.leader&&std::isfinite(tip.distanceTo(landing))&&tip.distanceTo(landing)<2000.,
      "bounded source note leader");
   note->second.leader=std::make_pair(tip,landing);
  }else if(tag=="READ"){
   require(job.observed.emplace(handle,panel).second,"unique collision object");
  }else throw std::runtime_error("unknown joint annotation action");
 }
 require(tag=="END"&&!(in>>tag)&&(allowNoSheets||!job.sheets.empty())&&job.sheets.size()<=12&&
   job.chains.size()+job.notes.size()<=32,"complete bounded joint annotation plan");
 for(const auto& [handle,note]:job.notes)
  require(note.shift.length()<=1200||note.leader.has_value(),"relocated source note requires a leader back to its source area");
 return job;
}

void jointPaperSheets(ProductJob& plot,AcDbDatabase& db,const JointPaperJob& job){
 auto* manager=acdbHostApplicationServices()->layoutManager();
 require(manager,"read saved joint annotation layouts");
 for(const auto& sheet:job.sheets){
  const auto layoutId=manager->findLayoutNamed(sheet.name.c_str(),&db);
  require(!layoutId.isNull(),"existing plot layout");
  const auto layout=open<AcDbLayout>(layoutId);
  double paperWidth{},paperHeight{};
  status(layout->getPlotPaperSize(paperWidth,paperHeight),"read actual saved plot paper size");
  if(layout->plotRotation()==AcDbPlotSettings::k90degrees||layout->plotRotation()==AcDbPlotSettings::k270degrees)
   std::swap(paperWidth,paperHeight);
  require(std::isfinite(paperWidth)&&std::isfinite(paperHeight)&&paperWidth>0&&paperHeight>0,
    "saved layout paper size is unavailable");
  if(!sheet.viewport.empty()){
   const auto view=open<AcDbViewport>(find(db,sheet.viewport));
   require(view->ownerId()==layout->getBlockTableRecordId(),"viewport must belong to the confirmed saved layout");
   plot.planSheetLayouts[sheet.panel]={sheet.name,"","",sheet.viewport,.01,"",paperWidth,paperHeight};
   plot.planPrintWindows.push_back({0,0,0,0});
   continue;
  }
  const auto record=open<AcDbBlockTableRecord>(layout->getBlockTableRecordId());
  AcDbBlockTableRecordIterator* raw{};
  status(record->newIterator(raw),"read existing paper objects");
  std::unique_ptr<AcDbBlockTableRecordIterator> entities(raw);
  AcDbObjectId viewport;
  for(;!entities->done();entities->step()){
   AcDbObjectId id;status(entities->getEntityId(id),"read existing paper viewport");
   const auto object=open<AcDbEntity>(id);
   const auto* view=AcDbViewport::cast(object.get());
   if(!view||!view->isLocked()||std::abs(view->customScale()-.01)>1e-8)continue;
   require(viewport.isNull(),"one locked 1:100 plan viewport per sheet");
   viewport=id;
  }
  require(!viewport.isNull(),"active locked plan viewport");
  plot.planSheetLayouts[sheet.panel]={sheet.name,"","",axisHandle(viewport),.01,"",paperWidth,paperHeight};
  plot.planPrintWindows.push_back({0,0,0,0});
 }
 plot.planPrintReview=true;
}

std::string jointChainStyle(AcDbDatabase& db,const std::string& handle){
 const auto entity=open<AcDbEntity>(find(db,handle));
 require(std::wstring_view(entity->isA()->name())==L"TDbDimension2"&&productPlanDimensionVisible(*entity)&&
   !productSourceNativeDimensionProtected(*entity),"read editable native plotted chain");
 std::ostringstream out;
 out<<std::setprecision(17)<<axisHandle(entity->layerId())<<':'<<entity->colorIndex()<<':';
 for(const auto& style:productDimensionTexts(*entity)){
  require(!style.style.isNull()&&!style.font.isNull(),"read the actual dimension style and font");
  const auto font=productReadDimensionFont(style.font);
  out<<jsonUtf8(symbolName(style.style))<<':'<<jsonUtf8(font.name)<<':'<<jsonUtf8(font.primary)<<':'
     <<jsonUtf8(font.big)<<':'<<font.width<<':'<<style.height<<':'<<style.scale<<':'<<style.fixed<<':'
     <<style.gap<<':'<<style.lineExtension<<':'<<style.measurementFactor<<';';
 }
 return out.str();
}

void writeJointPaperGeometry(AcDbDatabase& db,const JointPaperJob& job,const fs::path& result,const std::string& digest){
 std::ostringstream out;
 out<<std::setprecision(17)<<"{\"drawingSHA256\":\""<<digest<<"\",\"objects\":[";
 bool first=true;
 for(const auto& [handle,chain]:job.chains){
  const auto id=find(db,handle);
  const auto grips=chainGrips(id);
  const auto values=chainValues(id);
  require(grips.size()==values.size()*2+6&&!values.empty(),"complete editable chain geometry");
  const auto object=open<AcDbEntity>(id);
  std::vector<std::pair<AcGePoint3d,AcGePoint3d>> lines;
  productPlanDimensionStrokes(*object,lines);
  if(!first)out<<',';
  first=false;
  out<<"{\"handle\":\""<<handle<<"\",\"panel\":"<<chain.panel<<",\"style\":"
     <<jsonUtf8(bcpproduct::decodeMessage(jointChainStyle(db,handle)))<<",\"layer\":"<<wholeLayer(object->layerId())
     <<",\"grips\":"<<wholePoints(grips)<<",\"values\":";
  productWriteStationArray(out,values);
  out<<",\"ink\":[";
  for(std::size_t i=0;i<lines.size();++i){if(i)out<<',';out<<wholePoints({lines[i].first,lines[i].second});}
  out<<"]}";
 }
 for(const auto& [handle,note]:job.notes){
  const auto entity=open<AcDbEntity>(find(db,handle));
  require(productPostcheckTextWritable(db,*entity),"read editable independent source text");
  const auto s=productSourceTextState(*entity);
  require(s.has_value(),"read original note content and style");
  if(!first)out<<',';
  first=false;
  out<<"{\"handle\":\""<<handle<<"\",\"panel\":"<<note.panel<<",\"content\":"<<jsonUtf8(s->contents)
     <<",\"layer\":"<<wholeLayer(entity->layerId())<<",\"anchor\":"<<wholePoints({s->anchor})
     <<",\"bounds\":"<<wholePoints({s->box.minPoint(),s->box.maxPoint()})<<"}";
 }
 for(const auto& [handle,panel]:job.observed){
  const auto entity=open<AcDbEntity>(find(db,handle));
  AcDbExtents bounds;
  const bool hasBounds=entity->getGeomExtents(bounds)==Acad::eOk;
  if(!first)out<<',';
  first=false;
  out<<"{\"handle\":\""<<handle<<"\",\"panel\":"<<panel<<",\"type\":"<<jsonUtf8(entity->isA()->name())
     <<",\"layer\":"<<wholeLayer(entity->layerId())<<",\"bounds\":"
     <<(hasBounds?wholePoints({bounds.minPoint(),bounds.maxPoint()}):"[]");
  const auto state=productSourceTextState(*entity);
  if(state)out<<",\"content\":"<<jsonUtf8(state->contents)<<",\"anchor\":"<<wholePoints({state->anchor});
  out<<'}';
 }
 out<<"],\"openingGlyphs\":[";
 first=true;
 for(const auto& [raw,type]:modelTypes(db)){
  if(type!=L"TDbOpening"&&type!=L"TDbGroupOpening")continue;
  const auto opening=open<AcDbEntity>(find(db,ascii(raw)));
  if(!productPlanDimensionVisible(*opening))continue;
  std::vector<AcDbExtents> glyphs;
  productGenericTextLeaves(*opening,glyphs);
  for(const auto& glyph:glyphs){
   if(!first)out<<',';
   first=false;
   out<<"{\"handle\":\""<<ascii(raw)<<"\",\"bounds\":"<<wholePoints({glyph.minPoint(),glyph.maxPoint()})<<'}';
  }
 }
 out<<"]}";
 writeNew(result,out.str());
}

void executeJointPaperJob(void* data){
 std::unique_ptr<JointPaperJob> parameters(static_cast<JointPaperJob*>(data));
 auto& job=*parameters;
 AcApDocument* opened{};
 try{
  AcDbDatabase db(false,true);
  readDrawing(db,job.input);
  Working working(&db);
  writeJointPaperGeometry(db,job,job.report/L"joint-before.json",job.hash);
  if(!job.output.empty()){
   const auto roots=modelTypes(db);
   const auto paperAndBlocks=productPostcheckProtectedTextContainers(db);
   std::set<std::string> selected;
   for(const auto& [handle,row]:job.chains)selected.insert(handle);
   for(const auto& [handle,row]:job.notes)selected.insert(handle);
   std::map<std::string,std::pair<std::vector<AcGePoint3d>,std::vector<double>>> chains;
   std::map<std::string,std::string> styles;
   std::map<std::string,ProductSourceTextState> notes;
   std::map<std::string,std::pair<AcGePoint3d,AcGePoint3d>> leaders;
   for(const auto& [handle,note]:job.notes){
    const auto id=find(db,handle);
    auto object=open<AcDbEntity>(id);
    notes.emplace(handle,*productSourceTextState(*object));
    object.reset();
    if(note.shift.length()>0)status(editObject<AcDbEntity>(id)->transformBy(AcGeMatrix3d::translation(note.shift)),"translate original source text");
    if(note.leader){
     const auto& [tip,landing]=*note.leader;
     const auto old=notes.at(handle);
     const auto following=productSourceTextState(*open<AcDbEntity>(id));
     require(following&&tip.distanceTo(old.anchor)<1600.&&
      std::abs(landing.y-(following->box.minPoint().y+following->box.maxPoint().y)*.5)<100.&&
      (std::abs(landing.x-following->box.minPoint().x)<180.||
       std::abs(landing.x-following->box.maxPoint().x)<180.),"leader does not bind the original area to the new label");
     auto leader=std::make_unique<AcDbLeader>();
     leader->setDatabaseDefaults(&db);
     status(leader->setLayer(open<AcDbEntity>(id)->layerId()),"use source note layer for its leader");
     leader->setHasArrowHead(true);
     require(leader->appendVertex(tip)&&leader->appendVertex(landing),"native note leader vertices");
     const auto leaderId=productAppendPlanDetail(db,std::move(leader));
     const auto key=axisHandle(leaderId);
     selected.insert(key);
     leaders.emplace(key,*note.leader);
    }
   }
   for(const auto& [handle,chain]:job.chains){
    const auto id=find(db,handle);
    const auto values=chainValues(id);
    const auto original=chainGrips(id);
    styles[handle]=jointChainStyle(db,handle);
    const auto n=values.size();
    const bool vertical=std::abs(original[0].x-original[1].x)<1e-5;
    require(vertical||std::abs(original[0].y-original[1].y)<1e-5,"normal shift of a real orthogonal chain");
    const auto normal=[vertical](const auto& p){return vertical?p.x:p.y;};
    const auto before=normal(original[0]);
    const auto delta=vertical?AcGeVector3d(chain.normal,0,0):AcGeVector3d(0,chain.normal,0);
    for(std::size_t i=2;i<n+3;++i){
     const auto station=normal(original[i]);
     if(chain.reverseBaseline)
      require(std::abs(station-normal(original[2]))<.01&&
       (station-before)*(station-before-chain.normal)<0,"complete baseline reversal requires one row of measured stations");
     else require((station-before)*(station-before-chain.normal)>0,"chain baseline crosses a measured station");
    }
    const auto captionHits=productPrintedDimensionCaptionHits(db,id);
    if(chain.normal)moveDimensionGrip(id,0,original[0]+delta);
    for(std::size_t i=0;i<n;++i){
     const auto next=chain.textPositions.find(i);
     const auto goal=next==chain.textPositions.end()?original[n+3+i]+delta:next->second;
     require(goal.distanceTo(original[n+3+i])<=3500.,"same local dimension label");
     if(chain.normal||next!=chain.textPositions.end())moveDimensionGrip(id,static_cast<int>(n+3+i),goal);
    }
    const auto hits=productPrintedDimensionCaptionHits(db,id);
    for(const auto& other:hits)if(!captionHits.count(other))
     throw std::runtime_error("joint baseline "+handle+" crosses additional fixed opening number "+other);
    const auto updated=chainGrips(id);
    require(updated.size()==original.size()&&updated[0].distanceTo(original[0]+delta)<.01&&chainValues(id)==values&&
     std::equal(updated.begin()+2,updated.begin()+n+3,original.begin()+2,[](const auto& a,const auto& b){return a.distanceTo(b)<.01;}),
     "joint chain changed stations, values or baseline");
    require(planWitnessesVisible(readPlanVisibleWitnesses(id,vertical),values.size(),1.),"joint chain must retain both visible witnesses");
    const auto glyphs=productGenericChainText(db,id,vertical);
    require(glyphs.size()==n&&std::all_of(glyphs.begin(),glyphs.end(),[&](const auto& glyph){
      return glyph&&productPlanTextOnRequiredSide(*glyph,vertical,normal(updated[0]));
    }),"joint chain text must stay on the rule side");
    chains.emplace(handle,std::make_pair(std::move(updated),values));
   }
   // Compare the candidate with an unedited SaveAs at the same serialization boundary.
   const auto control=job.report/L"joint-unchanged.dwg";
   AcDbDatabase original(false,true);
   readDrawing(original,job.input);
   {Working w(&original);status(original.saveAs(control.c_str(),false,AcDb::kDHL_CURRENT),"save unedited joint control");}
   require(!fs::exists(job.output),"new joint annotation candidate path");
   status(db.saveAs(job.output.c_str(),false,AcDb::kDHL_CURRENT),"save joint annotation candidate");
   AcDbDatabase saved(false,true),reference(false,true);
   readDrawing(saved,job.output);readDrawing(reference,control);
   std::string unchanged;
   {Working w(&reference);unchanged=wholeProtected(reference,selected,true);}
   {
    Working w(&saved);
    auto afterRoots=modelTypes(saved);
    for(const auto& [handle,points]:leaders){
     require(afterRoots.erase(bcpproduct::decodeMessage(handle))==1,"new source note leader is absent on reopen");
     const auto entity=open<AcDbLeader>(find(saved,handle));
     require(entity->numVertices()==2&&entity->vertexAt(0).distanceTo(points.first)<.01&&
       entity->vertexAt(1).distanceTo(points.second)<.01,"source note leader did not keep its exact vertices");
    }
    require(afterRoots==roots&&bcpproduct::samePublicGeometry(unchanged,wholeProtected(saved,selected,true))&&
     bcpproduct::samePublicGeometry(paperAndBlocks,productPostcheckProtectedTextContainers(saved)),"unrelated geometry, layouts or block objects changed");
    for(const auto& [handle,shape]:chains){
     const auto id=find(saved,handle);
     require(productSamePrintedGrips(chainGrips(id),shape.first)&&chainValues(id)==shape.second&&
       jointChainStyle(saved,handle)==styles.at(handle),"joint chain appearance changed on reopen");
    }
    for(const auto& [handle,note]:job.notes){
     const auto e=open<AcDbEntity>(find(saved,handle));
     const auto state=productSourceTextState(*e);
     require(state&&productPostcheckTextSame(notes.at(handle),*state,note.shift,.01),"source text content, style or anchor changed");
     const auto lo=state->box.minPoint(),hi=state->box.maxPoint();
     require(lo.x>=note.region[0]&&lo.y>=note.region[1]&&hi.x<=note.region[2]&&hi.y<=note.region[3],
      "source note does not fit its verified original area");
    }
    writeJointPaperGeometry(saved,job,job.report/L"joint-after.json",sha(job.output));
   }
   opened=productOpenPrintedInkCandidate(job.output);
   ProductJob plot;
   {Working plotted(opened->database());jointPaperSheets(plot,*opened->database(),job);}
   productPrintPlanLayouts(plot,*opened,job.output,job.report);
   productStatus(acDocManager->appContextCloseDocument(opened),"close joint verification drawing");
   opened=nullptr;
  }
  require(sha(job.input)==job.hash,"joint annotation input changed");
  writeNew(job.report/L"product-result.json","{\"completed\":true,\"sourceSHA256\":\""+job.hash+
   "\",\"dwgReopened\":"+(job.output.empty()?std::string("false"):"true")+
   ",\"outputSHA256\":\""+(job.output.empty()?std::string():sha(job.output))+"\"}");
 }catch(const std::exception& error){
  if(opened)acDocManager->appContextCloseDocument(opened);
  writeNew(job.report/L"product-result.json","{\"completed\":false,\"error\":"+
   jsonUtf8(bcpproduct::decodeMessage(error.what()))+"}");
 }
}
}
