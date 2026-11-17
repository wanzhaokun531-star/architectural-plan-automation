#include "ProductPlanSheetFallback.inc"

void productInspectPaperTitleBlock(AcDbDatabase& db,const std::string& handle,const fs::path& output){
 const auto root=open<AcDbBlockReference>(find(db,handle));
 std::ostringstream report;
 report<<std::setprecision(17)<<"{\"objects\":[";
 bool first=true;
 const auto record=[&](const AcDbEntity& entity,const AcGeMatrix3d& transform){
  AcDbExtents box;
  if(entity.getGeomExtents(box)!=Acad::eOk)return;
  box.transformBy(transform);
  if(!first)report<<',';
  first=false;
  report<<"{\"handle\":\""<<axisHandle(entity.objectId())<<"\",\"type\":"<<jsonUtf8(entity.isA()->name())
        <<",\"bounds\":"<<wholePoints({box.minPoint(),box.maxPoint()});
  if(const auto* text=AcDbText::cast(&entity))report<<",\"text\":"<<jsonUtf8(text->textString());
  if(const auto* attr=AcDbAttribute::cast(&entity))report<<",\"tag\":"<<jsonUtf8(attr->tag());
  report<<'}';
 };
 const auto walk=[&](auto&& walk,const AcDbBlockReference& block,const AcGeMatrix3d& parent,int depth)->void{
  if(depth>4)return;
  std::unique_ptr<AcDbObjectIterator> attributes(block.attributeIterator());
  for(;attributes&&!attributes->done();attributes->step())
   record(*open<AcDbAttribute>(attributes->objectId()),parent);
  const auto definition=open<AcDbBlockTableRecord>(block.blockTableRecord());
  AcDbBlockTableRecordIterator* current=nullptr;
  productStatus(definition->newIterator(current),"inspect cloned paper frame elements");
  std::unique_ptr<AcDbBlockTableRecordIterator> objects(current);
  const auto transform=parent*block.blockTransform();
  for(;!objects->done();objects->step()){
   AcDbObjectId id;
   productStatus(objects->getEntityId(id),"paper frame child id");
   const auto entity=open<AcDbEntity>(id);
   record(*entity,transform);
   if(const auto* nested=AcDbBlockReference::cast(entity.get()))walk(walk,*nested,transform,depth+1);
  }
 };
 walk(walk,*root,AcGeMatrix3d::kIdentity,0);
 report<<"]}";writeNew(output,report.str());
}

void productWritePlanSheetLayout(ProductJob& job,AcDbDatabase& db,int panel,const fs::path& file,
 const std::string& digest,const std::string& frame,const std::string& block,const std::string& title,
 const AcGePoint3d& centre,double paper){
 require(panel>0&&!job.planSheetLayouts.count(panel)&&fs::is_regular_file(file)&&sha(file)==digest,"archive frame input and panel identity");
 AcDbDatabase archive(false,true);
 productStatus(archive.readDwgFile(file.c_str(),AcDbDatabase::kForReadAndAllShare,false),"open archive frame drawing");
 productStatus(archive.closeInput(true),"close archive frame input");
 AcDbObjectIdArray roots;
 AcGePoint3d base;
 {
  Working context(&archive);
  for(const auto* handle:{&frame,&block,&title})roots.append(find(archive,*handle));
  const auto outer=open<AcDbEntity>(roots[0]);
  const auto contents=open<AcDbEntity>(roots[2]);
  AcDbExtents ext,titleExt;
  const auto titleBlock=open<AcDbEntity>(roots[1]);
  require(AcDbBlockReference::cast(titleBlock.get())&&AcDbText::cast(contents.get())&&
   outer->getGeomExtents(ext)==Acad::eOk&&contents->getGeomExtents(titleExt)==Acad::eOk,
   "archive frame and visible title geometry");
  base=ext.minPoint();
  require(std::abs((ext.maxPoint().x-base.x)/100.-594.)<.01&&std::abs((ext.maxPoint().y-base.y)/100.-420.)<.01&&
   titleExt.minPoint().x>base.x&&titleExt.maxPoint().x<ext.maxPoint().x&&
   titleExt.minPoint().y>base.y&&titleExt.maxPoint().y<ext.maxPoint().y,"archive A2 frame and title bounds");
 }
 auto* manager=acdbHostApplicationServices()->layoutManager();
 require(manager,"native layout manager unavailable");
 const auto name=L"平面图幅-"+std::to_wstring(panel);
 AcString previousLayout;
 productStatus(manager->getActiveLayoutName(previousLayout,true,&db),"record current layout before placing paper viewport");
 AcDbObjectId layout,space;
 productStatus(manager->createLayout(name.c_str(),layout,space,&db),"create native paper space layout");
 try{
  AcDbIdMapping mapping;
  productStatus(archive.wblockCloneObjects(roots,space,mapping,AcDb::kDrcMangleName,false),"import archive title block into paper space");
  std::array<std::string,3> handles;
  for(int i=0;i<roots.length();++i){
   AcDbIdPair pair;
   pair.setKey(roots[i]);
   require(mapping.compute(pair)&&!pair.value().isNull(),"mapped archive title object");
   auto entity=editObject<AcDbEntity>(pair.value());
   productStatus(entity->transformBy(AcGeMatrix3d::translation(AcGePoint3d::kOrigin-base)),"move title block to paper origin");
   productStatus(entity->transformBy(AcGeMatrix3d::scaling(.01,AcGePoint3d::kOrigin)),"scale archive frame to paper millimetres");
   handles[i]=axisHandle(pair.value());
  }
  const auto viewportLayer=productPlanLayer(db,L"PLAN-SHEET-VIEWPORT",8);
  productStatus(editObject<AcDbLayerTableRecord>(viewportLayer)->setIsPlottable(false),"hide viewport frame in plot");
  // Activating the new layout initializes its paper-space viewport (number 1).
  productStatus(manager->setCurrentLayout(name.c_str(),&db),"initialize paper layout before appending model viewport");
  auto viewport=std::make_unique<AcDbViewport>();
  viewport->setDatabaseDefaults(&db);
  productStatus(viewport->setLayer(viewportLayer),"paper viewport layer");
  AcDbObjectId viewportId;
  {
   auto record=editObject<AcDbBlockTableRecord>(space);
   productStatus(record->appendAcDbEntity(viewportId,viewport.get()),"append native plan viewport");
   viewport.release()->close();
  }
  {
   auto view=editObject<AcDbViewport>(viewportId);
   productStatus(view->setCenterPoint({270.,210.,0}),"paper viewport centre");
   productStatus(view->setWidth(470.),"paper viewport width");
   productStatus(view->setHeight(365.),"paper viewport height");
   productStatus(view->setViewDirection(AcGeVector3d::kZAxis),"paper viewport plan direction");
   productStatus(view->setViewTarget(centre),"paper viewport source panel");
   productStatus(view->setViewCenter({0,0}),"paper viewport source centre");
   productStatus(view->setOn(),"show paper viewport");
   productStatus(view->setViewHeight(365.*paper),"paper viewport model height");
   productStatus(view->setCustomScale(1./paper),"paper viewport drawing scale");
   productStatus(view->setLocked(),"lock active paper viewport");
  }
  productStatus(manager->setCurrentLayout(previousLayout.kACharPtr(),&db),"restore prior layout");
  {
   auto settings=editObject<AcDbLayout>(layout);
   auto* validator=acdbHostApplicationServices()->plotSettingsValidator();
   require(validator,"paper settings validator unavailable");
   validator->refreshLists(settings.get());
   productStatus(validator->setPlotCfgName(settings.get(),L"DWG To PDF.pc3"),"paper layout plotter");
   validator->refreshLists(settings.get());
   productSelectPlanPaper(*settings,*validator,594,420,1.);
   productStatus(validator->setPlotPaperUnits(settings.get(),AcDbPlotSettings::kMillimeters),"layout paper units");
   productStatus(validator->setPlotType(settings.get(),AcDbPlotSettings::kLayout),"layout paper plot area");
   productStatus(validator->setUseStandardScale(settings.get(),false),"layout paper scale mode");
   productStatus(validator->setCustomPrintScale(settings.get(),1.,1.),"layout paper scale");
   if(!job.planPlotStyle.empty()){
    productStatus(validator->setCurrentStyleSheet(settings.get(),job.planPlotStyle.c_str()),"layout project plot style");
    settings->setPlotPlotStyles(true);
   }
  }
  job.planSheetLayouts.emplace(panel,ProductJob::PlanSheetLayout{name,handles[2],handles[1],axisHandle(viewportId),1./paper});
  if(job.validation)productInspectPaperTitleBlock(db,handles[1],job.root/(L"sheet-template-"+std::to_wstring(panel)+L".json"));
 }catch(...){
  manager->setCurrentLayout(previousLayout.kACharPtr(),&db);
  productStatus(manager->deleteLayout(name.c_str(),&db),"discard incomplete layout");
  throw;
 }
}

AcDbObjectId productPlanModelFrameLayer(AcDbDatabase& db,const AcDbViewport& viewport,
 const std::array<std::string,3>& handles){
 const std::set<std::string> registered(handles.begin(),handles.end());
 require(registered.size()==3,"source model frame handles must be distinct");
 const auto centre=viewport.viewTarget();
 const auto scale=viewport.customScale();
 require(scale>0&&viewport.viewCenter().distanceTo(AcGePoint2d::kOrigin)<1e-5&&
  viewport.viewDirection().isParallelTo(AcGeVector3d::kZAxis)&&std::abs(viewport.twistAngle())<1e-8,
  "source model frame verification requires an orthogonal plan viewport");
 const AcGeVector3d half(viewport.width()/(2*scale),viewport.height()/(2*scale),0);
 const AcDbExtents visible(centre-half,centre+half);
 std::array<AcDbExtents,3> bounds;
 AcDbObjectId layerId;
 for(std::size_t i=0;i<handles.size();++i){
  const auto entity=open<AcDbEntity>(find(db,handles[i]));
  const auto* frame=AcDbPolyline::cast(entity.get());
  require(entity->ownerId()==modelId(db)&&(i<2?(frame&&frame->isClosed()&&frame->numVerts()==4):
   AcDbBlockReference::cast(entity.get())!=nullptr),"registered source model frame shape and owner");
  require(entity->getGeomExtents(bounds[i])==Acad::eOk&&productGenericBoxesOverlap(visible,bounds[i],0),
   "registered model frame lies in the intended viewport");
  if(i==0)layerId=entity->layerId();
  require(entity->layerId()==layerId,"registered model frame uses a single layer");
 }
 const auto outer=bounds[0];
 require(bounds[1].minPoint().x>outer.minPoint().x&&bounds[1].maxPoint().x<outer.maxPoint().x&&
  bounds[1].minPoint().y>outer.minPoint().y&&bounds[1].maxPoint().y<outer.maxPoint().y&&
  productGenericBoxesOverlap(bounds[1],bounds[2],0),"inner frame and source title belong to registered outer frame");
 for(const auto& [handle,type]:modelTypes(db)){
  if(registered.count(ascii(handle)))continue;
  const auto entity=open<AcDbEntity>(find(db,ascii(handle)));
  if(entity->layerId()!=layerId||!productPlanDimensionVisible(*entity))continue;
  AcDbExtents box;
  require(entity->getGeomExtents(box)==Acad::eOk&&!productGenericBoxesOverlap(box,visible,0),
   "other visible model content shares the source frame layer in this viewport");
 }
 return layerId;
}

std::vector<AcDbObjectId> productPaperApprovalMarks(AcDbDatabase& db,const std::string& titleBlock,
 const std::array<double,4>& cell){
 require(std::all_of(cell.begin(),cell.end(),[](double value){return std::isfinite(value);})&&
  cell[0]>=0&&cell[1]>=0&&cell[2]<=594&&cell[3]<=420&&cell[0]<cell[2]&&cell[1]<cell[3],
  "registered paper signature cell");
 const auto parent=open<AcDbBlockReference>(find(db,titleBlock));
 const auto definition=open<AcDbBlockTableRecord>(parent->blockTableRecord());
 AcDbBlockTableRecordIterator* current=nullptr;
 productStatus(definition->newIterator(current),"inspect paper approval cell");
 std::unique_ptr<AcDbBlockTableRecordIterator> children(current);
 std::vector<AcDbObjectId> marks;
 for(;!children->done();children->step()){
  AcDbObjectId id;
  productStatus(children->getEntityId(id),"paper approval child id");
  const auto entity=open<AcDbEntity>(id);
  if(!AcDbBlockReference::cast(entity.get())||!productPlanDimensionVisible(*entity))continue;
  AcDbExtents bounds;
  if(entity->getGeomExtents(bounds)!=Acad::eOk)continue;
  bounds.transformBy(parent->blockTransform());
  if(bounds.minPoint().x>=cell[0]&&bounds.minPoint().y>=cell[1]&&
     bounds.maxPoint().x<=cell[2]&&bounds.maxPoint().y<=cell[3])marks.push_back(id);
 }
 return marks;
}

void productWritePlanSheetLinks(ProductJob& job,AcDbDatabase& db,double paper){
 if(!job.unifiedPlan||job.unifiedSheetChecks.empty())return;
 Working work(&db);
 std::ifstream in(job.unifiedSheetChecks);
 std::string line,digest,kind;
 require(bool(in>>kind>>digest)&&kind=="PLANSHEETS1"&&digest==job.sourceHash,"sheet writing requires original DWG digest");
 std::map<std::string,std::string> targets;
 double right=-1e100;
 for(const auto& [handle,type]:modelTypes(db)){
  AcDbExtents box;
  if(open<AcDbEntity>(find(db,ascii(handle)))->getGeomExtents(box)==Acad::eOk)right=std::max(right,box.maxPoint().x);
 }
 const auto layer=productPlanLayer(db,L"A-TEXT-DESC",7);
 while(std::getline(in,line)){
  std::istringstream row(line);
  if(!(row>>kind))continue;
  if(kind=="END")break;
  if(kind=="FRAME"){
   int panel{};
   std::string file,digest,frame,block,title,extra;
   AcGePoint3d centre;
   require(bool(row>>panel>>frame>>block>>title>>centre.x>>centre.y>>std::quoted(file)>>digest)&&!(row>>extra),"archive paper layout definition");
   try{
    productWritePlanSheetLayout(job,db,panel,job.unifiedSheetChecks.parent_path()/fs::u8path(file),digest,frame,block,title,centre,paper);
   }catch(const std::exception& error){
    job.detailPending.push_back({u8"正式图签及详图索引",std::to_string(panel),error.what()});
   }
  }else if(kind=="MODELFRAME"){
   int panel{};std::array<std::string,3> handles;std::string extra;
   require(bool(row>>panel>>handles[0]>>handles[1]>>handles[2])&&!(row>>extra),"registered original model frame");
   const auto sheet=job.planSheetLayouts.find(panel);
   if(sheet==job.planSheetLayouts.end())continue;
   const auto viewportId=find(db,sheet->second.viewport);
   auto view=open<AcDbViewport>(viewportId);
   AcDbObjectIdArray layers;layers.append(productPlanModelFrameLayer(db,*view,handles));
   view.reset();
   productStatus(editObject<AcDbViewport>(viewportId)->freezeLayersInViewport(layers),"suppress old model frame in actual paper viewport");
  }else if(kind=="CLEARAPPROVAL"){
   int panel{};std::size_t expected{};std::array<double,4> cell;std::string extra;
   require(bool(row>>panel>>expected>>cell[0]>>cell[1]>>cell[2]>>cell[3])&&
    expected>0&&expected<=30&&!(row>>extra),"registered archived sign-off marks");
   const auto sheet=job.planSheetLayouts.find(panel);
   if(sheet==job.planSheetLayouts.end())continue;
   const auto marks=productPaperApprovalMarks(db,sheet->second.block,cell);
   require(marks.size()==expected,"exact archived signature marker count");
   for(const auto id:marks)productStatus(editObject<AcDbEntity>(id)->erase(),"remove archive-only signature mark");
  }else if(kind=="NUMBER"){
   int panel{};
   std::string file,digest,local,catalog,entry,extra;
   require(bool(row>>panel>>local>>catalog>>entry>>std::quoted(file)>>digest),"archive drawing number references");
   AcGePoint3d paperNumber(567.,28.,0);
   if(row>>extra){
    require(extra=="AT"&&bool(row>>paperNumber.x>>paperNumber.y)&&!(row>>extra)&&
     std::isfinite(paperNumber.x)&&std::isfinite(paperNumber.y)&&paperNumber.x>0&&paperNumber.x<594&&
     paperNumber.y>0&&paperNumber.y<420,"registered drawing number paper cell");
   }
   const auto sheet=job.planSheetLayouts.find(panel);
   if(sheet==job.planSheetLayouts.end())continue;
   const auto path=job.unifiedSheetChecks.parent_path()/fs::u8path(file);
   require(fs::is_regular_file(path)&&sha(path)==digest,"archive drawing number input digest");
   AcDbDatabase archive(false,true);
   productStatus(archive.readDwgFile(path.c_str(),AcDbDatabase::kForReadAndAllShare,false),"read archive drawing numbers");
   productStatus(archive.closeInput(true),"close archive drawing numbers input");
   std::wstring number,title;
   {
    Working context(&archive);
    const auto read=[&](const std::string& handle){
     std::vector<std::wstring> leaves;
     productOpeningTexts(*open<AcDbEntity>(find(archive,handle)),leaves);
     return std::accumulate(leaves.begin(),leaves.end(),std::wstring{});
    };
    number=read(local);
    title=read(entry);
    require(!title.empty()&&!number.empty()&&number==read(catalog),"archive drawing number must agree with the drawing list");
   }
   const auto paperTitle=open<AcDbText>(find(db,sheet->second.title));
   require(title==paperTitle->textString(),"archive drawing list title must match the paper title");
   const auto space=paperTitle->ownerId();
   auto text=std::make_unique<AcDbText>();
   text->setDatabaseDefaults(&db);
   productStatus(text->setTextStyle(paperTitle->textStyle()),"paper drawing number style");
   productStatus(text->setHeight(2.5),"paper drawing number height");
   productStatus(text->setTextString(number.c_str()),"write archived drawing number");
   productStatus(text->setLayer(layer),"visible paper drawing number layer");
   productStatus(text->setPosition(paperNumber),"place archived drawing number within its registered paper cell");
   AcDbObjectId id;
   {
    auto record=editObject<AcDbBlockTableRecord>(space);
    productStatus(record->appendAcDbEntity(id,text.get()),"append paper drawing number");
    text.release()->close();
   }
   require(job.planSheetNumbers.emplace(panel,std::make_pair(axisHandle(id),number)).second,"one drawing number per paper layout");
  }else if(kind=="REGISTERDATUM"||kind=="REGISTERNOTE"){
   std::string file,digest,label,sourceA,sourceB,archiveA,archiveB,extra;
   require(bool(row>>label>>sourceA>>archiveA>>sourceB>>archiveB>>std::quoted(file)>>digest)&&!(row>>extra),
    "registered datum needs a source, two original geometry anchors and two archive anchors");
   const auto path=job.unifiedSheetChecks.parent_path()/fs::u8path(file);
   require(fs::is_regular_file(path)&&sha(path)==digest,"registered datum archive digest");
   AcDbDatabase archive(false,true);
   productStatus(archive.readDwgFile(path.c_str(),AcDbDatabase::kForReadAndAllShare,false),"read registered plan datum archive");
   productStatus(archive.closeInput(true),"close registered plan datum archive");
   const auto first=chainGrips(find(db,sourceA)),second=chainGrips(find(db,sourceB));
   const auto a=chainGrips(find(archive,archiveA)),b=chainGrips(find(archive,archiveB));
   require(first.size()>=2&&second.size()>=2&&a.size()==first.size()&&b.size()==second.size(),
    "registered datum anchor grip count");
   // Registration places plan annotations in XY. A native wall's repaired
   // base elevation is not a translation of the floor datum's numeric value.
   const AcGeVector3d shift(first.front().x-a.front().x,first.front().y-a.front().y,0);
   const auto equal=[&](const auto& x,const auto& y){
    return std::hypot(x.x-y.x-shift.x,x.y-y.y-shift.y)<=.01/job.unifiedUnitMM;
   };
   require(sourceA!=sourceB&&archiveA!=archiveB&&
    std::equal(first.begin(),first.end(),a.begin(),equal)&&std::equal(second.begin(),second.end(),b.begin(),equal),
    "registered datum anchors disagree on translation");
   const auto original=find(archive,label);
   std::vector<std::wstring> leaves;
   {
    Working reference(&archive);
    const auto object=open<AcDbEntity>(original);
    const auto expected=kind=="REGISTERDATUM"?L"TDbSymbElevation":L"TDbSymbMultiLeader";
    require(std::wstring(object->isA()->name())==expected&&!object->isAProxy(),"registered plan annotation native type");
    productOpeningTexts(*object,leaves);
   }
   require(!leaves.empty(),"registered datum has visible elevation text");
   const auto id=cloneOne(archive,original,db);
   wholeTranslate(id,shift);
   const auto actual=chainGrips(id);
   require(!actual.empty(),"registered datum has a visible anchor");
   const auto value=std::accumulate(leaves.begin(),leaves.end(),std::wstring{});
   require(job.planRegisteredDatums.emplace(label,ProductJob::RegisteredPlanDatum{axisHandle(id),actual,value}).second,
    "unique registered plan datum");
  }else if(kind=="DETAILS"){
   try{
   std::string file,expected,extra;
   std::array<double,4> bounds{};
   require(bool(row>>std::quoted(file)>>expected>>bounds[0]>>bounds[1]>>bounds[2]>>bounds[3])&&!(row>>extra)&&
     bounds[0]<bounds[2]&&bounds[1]<bounds[3],"archive detail bounds");
   const auto path=job.unifiedSheetChecks.parent_path()/fs::u8path(file);
   require(fs::is_regular_file(path)&&sha(path)==expected,"archive detail input fingerprint");
   AcDbDatabase archive(false,true);
   productStatus(archive.readDwgFile(path.c_str(),AcDbDatabase::kForReadAndAllShare,false),"open registered detail sheet");
   productStatus(archive.closeInput(true),"close registered detail source input");
   std::map<std::string,AcDbObjectId> selected;
   {
    Working source(&archive);
    for(const auto& [handle,type]:modelTypes(archive)){
     if(type==L"TDbPipe"||type==L"TDbPipeFitting")continue;
     const auto id=find(archive,ascii(handle));
     const auto entity=open<AcDbEntity>(id);
     AcDbExtents box;
     if(!productPlanDimensionVisible(*entity)||entity->getGeomExtents(box)!=Acad::eOk)continue;
     if(box.minPoint().x>=bounds[0]&&box.maxPoint().x<=bounds[2]&&box.minPoint().y>=bounds[1]&&box.maxPoint().y<=bounds[3])
      selected.emplace(ascii(handle),id);
    }
   }
   require(!selected.empty(),"registered archive detail window is empty");
   AcDbObjectIdArray sourceIds;
   for(const auto& [handle,id]:selected)sourceIds.append(id);
   auto definition=std::make_unique<AcDbBlockTableRecord>();
   const auto name=L"PLAN_DETAILS_"+std::to_wstring(job.planSupplementalDetails.size()+1);
   productStatus(definition->setName(name.c_str()),"detail drawing block name");
   AcDbObjectId definitionId;
   {
    auto table=editObject<AcDbBlockTable>(db.blockTableId());
    productStatus(table->add(definitionId,definition.get()),"create editable detail drawing block");
    definition.release()->close();
   }
   AcDbIdMapping mapping;
   productStatus(archive.wblockCloneObjects(sourceIds,definitionId,mapping,AcDb::kDrcMangleName,false),"import registered archive details together");
   const AcGeVector3d shift(right+150*paper-bounds[0],-bounds[1],0);
   const auto instance=productAppendPlanDetail(db,std::make_unique<AcDbBlockReference>(AcGePoint3d::kOrigin+shift,definitionId));
   job.planSupplementalDetails.insert(axisHandle(instance));
   for(const auto& [handle,source]:selected){
    AcDbIdPair pair;
    pair.setKey(source);
    require(mapping.compute(pair)&&!pair.value().isNull(),"mapped archive detail root");
    const auto output=axisHandle(pair.value());
    targets.emplace(handle,output);
   }
   right+=150*paper+bounds[2]-bounds[0];
   }catch(const std::exception& error){
    job.detailPending.push_back({u8"可选详图", "",error.what()});
   }
  }else if(kind=="LINK"){
   try{
   ProductJob::PlanSheetLink link;
   std::string title,extra;
   require(bool(row>>link.anchor>>link.archiveTarget>>std::quoted(title))&&!(row>>extra)&&!title.empty(),"plan detail index requires anchor and archive title");
   const auto target=targets.find(link.archiveTarget);
   if(target==targets.end()){
    job.detailPending.push_back({u8"正式图签及详图索引",link.anchor,u8"所指归档详图未写入当前 DWG"});
    continue;
   }
   link.target=target->second;
   link.targetTitle=bcpproduct::decodeMessage(title);
   const auto detail=open<AcDbEntity>(find(db,link.target));
   std::vector<std::wstring> labels;
   productOpeningTexts(*detail,labels);
   std::wstring joined;
   for(const auto& text:labels)if(text.rfind(L"1:",0)!=0)joined+=text;
   require(joined==link.targetTitle,"registered detail title does not match the imported detail");
   const auto anchor=find(db,link.anchor);
   AcDbExtents box;
   productStatus(open<AcDbEntity>(anchor)->getGeomExtents(box),"detail index source geometry");
   const auto centre=box.minPoint()+(box.maxPoint()-box.minPoint())*.5;
   link.text=L"详见本图："+link.targetTitle;
   ProductPlanDetailObstacles obstacles(db,{});
   for(const double dy:{7.,-7.,11.,-11.,15.,-15.,20.,-20.,25.,-25.}){
    for(const double dx:{0.,8.,-8.,16.,-16.,24.,-24.}){
     auto note=std::make_unique<AcDbMText>();
     note->setDatabaseDefaults(&db);
     productStatus(note->setLayer(layer),"index text layer");
     productStatus(note->setTextStyle(db.textstyle()),"index text style");
     productStatus(note->setTextHeight(2.5*paper),"index text height");
     productStatus(note->setAttachment(AcDbMText::kMiddleCenter),"index text alignment");
     productStatus(note->setLocation(centre+AcGeVector3d(dx*paper,dy*paper,0)),"index text position");
     note->setContents(link.text.c_str());
     AcDbExtents ink;
     if(note->getGeomExtents(ink)!=Acad::eOk||obstacles.hits({},ink))continue;
     link.note=axisHandle(productAppendPlanDetail(db,std::move(note)));
     break;
    }
    if(!link.note.empty())break;
   }
   job.planSheetLinks.push_back(std::move(link));
   }catch(const std::exception& error){
    job.detailPending.push_back({u8"可选详图索引","",error.what()});
   }
  }
 }
}

void productWritePlanDeliveryReview(ProductJob& job,AcDbDatabase& saved,const std::string& hash){
 if(!job.unifiedPlan)return;
 Working work(&saved);
 using bcpproduct::PlanReleaseGate;
 bcpproduct::PlanRelease release;
 std::size_t unresolved{},failed{},namedAreaFailures{};
 const auto gateName=[](PlanReleaseGate gate){
  return gate==PlanReleaseGate::mainPlan?"mainPlan":gate==PlanReleaseGate::reopened?"reopened":"files";
 };
 const auto csvCell=[](const std::string& value){
  std::string field="\"";
  for(const char c:value){if(c=='"')field+='"';field+=c;}
  return field+'"';
 };
 std::ostringstream missing,errors,checklist;
 for(auto* file:{&missing,&errors})
  *file<<"\xEF\xBB\xBF"<<u8"源图幅,项目,源对象,源依据,重开结果,处理\r\n";
 checklist<<"\xEF\xBB\xBF"<<u8"源图幅,实际部位,源对象,问题,已有依据,所缺依据,影响,处理,DWG SHA-256,适用规则,实际楼层,图面保留状态\r\n";
 const auto log=[&](std::ostringstream& file,std::size_t panel,const std::string& scope,
  const std::string& object,const std::string& basis,const std::string& observed,const std::string& action){
  file<<panel<<','<<csvCell(scope)<<','<<csvCell(object)<<','<<csvCell(basis)<<','
      <<csvCell(observed)<<','<<csvCell(action)<<"\r\n";
 };
 for(const auto& [key,row]:job.sourceInventory.items){
  if(row.check!=bcpproduct::PlanCheck::pending&&row.check!=bcpproduct::PlanCheck::failed)continue;
  if(job.architecturalPlanOnly&&row.check==bcpproduct::PlanCheck::pending&&job.planPreservedDesign.count(key))continue;
  if(row.check==bcpproduct::PlanCheck::pending)++unresolved;
  else ++failed;
  const auto& scope=std::get<1>(key);
  if(scope=="room-area"){++job.planUnverifiedNetAreaLabels;++namedAreaFailures;}
 }
 const auto addSourceIssue=[&](const auto& key,PlanReleaseGate gate,const std::string& basis,
  const std::string& reason,const std::string& input,const std::string& label){
  const auto& [panel,scope,source]=key;
  const auto location=job.planReleaseLocations.find(key);
  const auto obligation=job.sourceInventory.items.find(key);
  const auto rules=obligation==job.sourceInventory.items.end()?std::vector<std::string>{}:obligation->second.rules;
  if(location==job.planReleaseLocations.end())
   release.add(gate,panel,source,scope,basis,reason,input,label,rules);
  else release.add(gate,location->second.first,location->second.second,scope,basis,reason,input,label,rules);
 };
 const auto readEntity=[&](const std::string& handle){
  AcDbObjectId id;
  if(saved.getAcDbObjectId(id,false,AcDbHandle(bcpproduct::decodeMessage(handle).c_str()))!=Acad::eOk||id.isErased())
   return AcDbObjectId{};
  return id;
 };
 std::ostringstream checks;
 bool first=true;
 std::size_t sheetCount{},references{},expectedReferences{},optionalInputs{};
 std::set<int> sheets,verifiedFrames;
 std::set<std::string> numbers,blocks;
 if(!job.unifiedSheetChecks.empty()){
  std::ifstream in(job.unifiedSheetChecks);
  std::string kind,digest;
  require(bool(in>>kind>>digest)&&kind=="PLANSHEETS1"&&digest==job.sourceHash,
    "sheet checks must match the source DWG");
  while(in>>kind&&kind!="END"){
   bool verified=false;
   std::string handle,number,title,target,frameDetail;
   int checkedPanel{};
   if(kind=="SHEET"){
    int panel{};
    require(bool(in>>panel>>handle>>std::quoted(number)>>std::quoted(title))&&!number.empty()&&!title.empty()&&
      panel>0&&static_cast<std::size_t>(panel)<=job.planPrintWindows.size()&&sheets.insert(panel).second&&
      numbers.insert(number).second&&blocks.insert(handle).second,"sheet check needs unique panel, block and drawing number");
    const auto id=readEntity(handle);
    if(!id.isNull()){
     const auto entity=open<AcDbEntity>(id);
     if(const auto* block=AcDbBlockReference::cast(entity.get())){
      bool numberFound=false,titleFound=false;
      const std::unique_ptr<AcDbObjectIterator> attrs(block->attributeIterator());
      if(attrs)for(;!attrs->done();attrs->step()){
       const auto attr=open<AcDbAttribute>(attrs->objectId());
       const std::wstring text=attr->textString();
       if(attr->isInvisible())continue;
       numberFound|=text==bcpproduct::decodeMessage(number);
       titleFound|=text==bcpproduct::decodeMessage(title);
      }
      AcDbExtents extents;
      const auto& window=job.planPrintWindows.at(static_cast<std::size_t>(panel-1));
      const bool inside=block->getGeomExtents(extents)==Acad::eOk&&extents.minPoint().x>=window[0]&&
       extents.minPoint().y>=window[1]&&extents.maxPoint().x<=window[2]&&extents.maxPoint().y<=window[3];
      verified=numberFound&&titleFound&&inside&&productPlanDimensionVisible(*block);
     }
    }
    if(verified)++sheetCount;
   }else if(kind=="ARCHIVESHEET"){
    int panel{};
    std::string fileName,expectedHash,titleHandle,archiveTitleHandle;
    require(bool(in>>panel>>handle>>titleHandle>>archiveTitleHandle>>std::quoted(fileName)>>expectedHash)&&panel>0&&
      static_cast<std::size_t>(panel)<=job.planPrintWindows.size(),"archive sheet source and current plan title");
    checkedPanel=panel;
    const auto file=job.unifiedSheetChecks.parent_path()/fs::u8path(fileName);
    const auto currentTitle=readEntity(titleHandle);
    if(!currentTitle.isNull()&&fs::is_regular_file(file)&&sha(file)==expectedHash){
     const auto titleEntity=open<AcDbEntity>(currentTitle);
     std::vector<std::wstring> titleText;
     productOpeningTexts(*titleEntity,titleText);
     AcDbDatabase archive(false,true);
     if(archive.readDwgFile(file.c_str(),AcDbDatabase::kForReadAndAllShare,false)==Acad::eOk&&
        archive.closeInput(true)==Acad::eOk){
      Working reference(&archive);
      AcDbObjectId id;
      if(archive.getAcDbObjectId(id,false,AcDbHandle(bcpproduct::decodeMessage(handle).c_str()))==Acad::eOk&&!id.isErased()){
       const auto entity=open<AcDbEntity>(id);
       if(const auto* block=AcDbBlockReference::cast(entity.get())){
        std::vector<std::string> values;
        const std::unique_ptr<AcDbObjectIterator> attrs(block->attributeIterator());
        if(attrs)for(;!attrs->done();attrs->step()){
         const auto attribute=open<AcDbAttribute>(attrs->objectId());
         if(!attribute->isInvisible()&&*attribute->textString())values.push_back(productUtf8(attribute->textString()));
        }
        const auto archiveTitle=open<AcDbEntity>(find(archive,archiveTitleHandle));
        std::vector<std::wstring> archiveText;
        productOpeningTexts(*archiveTitle,archiveText);
        const auto* rawTitle=AcDbText::cast(archiveTitle.get());
        if(rawTitle)archiveText={rawTitle->textString()};
        AcDbExtents blockBox,titleBox;
        const bool titleInside=block->getGeomExtents(blockBox)==Acad::eOk&&archiveTitle->getGeomExtents(titleBox)==Acad::eOk&&
         titleBox.minPoint().x>=blockBox.minPoint().x&&titleBox.maxPoint().x<=blockBox.maxPoint().x&&
         titleBox.minPoint().y>=blockBox.minPoint().y&&titleBox.maxPoint().y<=blockBox.maxPoint().y;
        verified=titleInside&&productPlanDimensionVisible(*block)&&productPlanDimensionVisible(*archiveTitle)&&
         !archiveText.empty()&&std::find(titleText.begin(),titleText.end(),archiveText.front())!=titleText.end();
        AcString plotStyle;
        archive.getStyleSheet(plotStyle);
        std::ostringstream evidence;
        evidence<<"{\"panel\":"<<panel<<",\"archiveBlock\":\""<<handle<<"\",\"currentTitle\":\""<<titleHandle
          <<"\",\"archiveAttributes\":";
        productWriteHandleArray(evidence,values);
        evidence<<",\"archiveTitleHandle\":\""<<archiveTitleHandle<<"\",\"archiveTitleMatches\":"<<(verified?"true":"false")
          <<",\"archiveModelPlotStyle\":"<<jsonUtf8(plotStyle.kACharPtr())
          <<",\"currentTitleText\":";
        std::vector<std::string> titleValues;
        for(const auto& text:titleText)titleValues.push_back(productUtf8(text));
        productWriteHandleArray(evidence,titleValues);
        evidence<<",\"savedReopened\":true,\"result\":\"pending-sheet-placement-and-index\"}";
        productRecordRuleEvidence(job,"P03",evidence.str());
        number=values.empty()?"":values.front();
       }
      }
     }
    }
    if(!verified)log(errors,panel,u8"归档图签",handle,titleHandle,u8"归档可见属性或当前标题未读到",
      u8"核归档块、图号和当前原方案图幅");
   }else if(kind=="FRAME"){
    int panel{};
    require(bool(in>>panel),"read registered paper frame panel");
    checkedPanel=panel;
    std::string skip;
    std::getline(in,skip);
    const auto written=job.planSheetLayouts.find(panel);
    if(written!=job.planSheetLayouts.end()){
     const auto& sheet=written->second;
     handle=sheet.block;
     target=productUtf8(sheet.name);
     const auto blockId=readEntity(handle),titleId=readEntity(sheet.title),viewportId=readEntity(sheet.viewport);
     const auto layout=acdbHostApplicationServices()->layoutManager()->findLayoutNamed(sheet.name.c_str(),&saved);
     if(!layout.isNull()&&!blockId.isNull()&&!titleId.isNull()&&!viewportId.isNull()){
      const auto space=open<AcDbLayout>(layout)->getBlockTableRecordId();
      const auto block=open<AcDbBlockReference>(blockId);
      const auto text=open<AcDbText>(titleId);
      const auto view=open<AcDbViewport>(viewportId);
      const bool owned=block->ownerId()==space&&text->ownerId()==space&&view->ownerId()==space;
      const auto scale=view->customScale();
      const bool rightScale=std::abs(scale-sheet.scale)<=1e-9;
      verified=owned&&view->number()!=1&&view->isLocked()&&rightScale&&*text->textString();
      if(verified)verifiedFrames.insert(panel);
      frameDetail=std::string(",\"ownersMatch\":")+(owned?"true":"false")+",\"viewportOn\":"+
       (view->isOn()?"true":"false")+",\"viewportLocked\":"+(view->isLocked()?"true":"false")+
       ",\"viewportNumber\":"+std::to_string(view->number())+",\"viewportScale\":"+std::to_string(scale)+
       ",\"visibleTitle\":"+jsonUtf8(text->textString());
     }
    }
   }else if(kind=="MODELFRAME"){
    int panel{};std::array<std::string,3> handles;
    require(bool(in>>panel>>handles[0]>>handles[1]>>handles[2]),"read registered original model frame");
    checkedPanel=panel;handle=handles.front();
    const auto sheet=job.planSheetLayouts.find(panel);
    if(sheet!=job.planSheetLayouts.end()&&verifiedFrames.count(panel)){
     const auto view=open<AcDbViewport>(find(saved,sheet->second.viewport));
     const auto layer=productPlanModelFrameLayer(saved,*view,handles);
     verified=view->isLayerFrozenInViewport(layer);
     frameDetail=",\"sourceFrameLayerFrozenInPaperViewport\":"+std::string(verified?"true":"false");
    }
   }else if(kind=="CLEARAPPROVAL"){
    int panel{};std::size_t expected{};std::array<double,4> cell;
    require(bool(in>>panel>>expected>>cell[0]>>cell[1]>>cell[2]>>cell[3]),"read registered archived signatures");
    checkedPanel=panel;
    const auto sheet=job.planSheetLayouts.find(panel);
    if(sheet!=job.planSheetLayouts.end()&&verifiedFrames.count(panel)){
     handle=sheet->second.block;
     const auto remaining=productPaperApprovalMarks(saved,handle,cell);
     verified=remaining.empty();
     frameDetail=",\"remainingArchiveSignatureBlocks\":"+std::to_string(remaining.size());
    }
   }else if(kind=="NUMBER"){
    int panel{};
    require(bool(in>>panel),"read verified sheet number panel");
    checkedPanel=panel;
    std::string skip;
    std::getline(in,skip);
    const auto sheet=job.planSheetLayouts.find(panel);
    const auto marked=job.planSheetNumbers.find(panel);
    if(sheet!=job.planSheetLayouts.end()&&marked!=job.planSheetNumbers.end()&&verifiedFrames.count(panel)){
     handle=marked->second.first;
     number=productUtf8(marked->second.second);
     const auto numberId=readEntity(handle),titleId=readEntity(sheet->second.title);
     if(!numberId.isNull()&&!titleId.isNull()){
      const auto text=open<AcDbText>(numberId),titleText=open<AcDbText>(titleId);
      AcDbExtents ext;
      verified=text->ownerId()==titleText->ownerId()&&marked->second.second==text->textString()&&
       productPlanDimensionVisible(*text)&&text->getGeomExtents(ext)==Acad::eOk&&
       ext.minPoint().x>=0&&ext.maxPoint().x<=594&&ext.minPoint().y>=0&&ext.maxPoint().y<=420&&
       sheets.insert(panel).second&&numbers.insert(number).second;
     }
    }
    if(verified)++sheetCount;
   }else if(kind=="INPUT"||kind=="PLANINPUT"||kind=="DETAILINPUT"){
    int panel{};
    std::string source,basis,needed,scope,record;
    std::getline(in,record);
    std::istringstream input(record);
    if(kind=="PLANINPUT")require(bool(input>>scope)&&bcpproduct::planReleaseGateForSource(scope)==PlanReleaseGate::mainPlan,
     "plan input must name the affected main plan check");
    require(bool(input>>panel>>handle>>std::quoted(source)>>std::quoted(basis)>>std::quoted(needed))&&
     panel>=0&&!source.empty()&&!basis.empty()&&!needed.empty(),"explicit unresolved project input and evidence");
    const bool optional=kind=="DETAILINPUT";
    if(optional){
     ++optionalInputs;
     productRecordRuleEvidence(job,"B08","{\"panel\":"+std::to_string(panel)+",\"source\":"+
      jsonUtf8(bcpproduct::decodeMessage(handle))+",\"requiredForPlanRelease\":false,\"savedReopened\":true,"
      "\"result\":\"pending-optional-detail-input\"}");
     job.detailPending.push_back({u8"可选详图",handle,source+u8"。"+needed});
    }else if(kind=="PLANINPUT"){
     const auto key=bcpproduct::PlanSourceInventory::Key{static_cast<std::size_t>(panel),scope,handle=="-"?"":handle};
     const auto item=job.sourceInventory.items.find(key);
     require(item!=job.sourceInventory.items.end(),"plan input must reference an actual source obligation");
     std::string tag,anchor,extra;
     if(input>>tag){
      require(tag=="AT"&&bool(input>>anchor)&&!(input>>extra)&&
       std::any_of(job.sourceInventory.items.begin(),job.sourceInventory.items.end(),[&](const auto& known){
        return std::get<0>(known.first)==static_cast<std::size_t>(panel)&&std::get<2>(known.first)==anchor;
       }),"plan input location must reference an actual source object on the same panel");
      job.planReleaseLocations[key]={static_cast<std::size_t>(panel),anchor};
     }
     if(item->second.check==bcpproduct::PlanCheck::failed||
        (item->second.check==bcpproduct::PlanCheck::pending&&
         !(job.architecturalPlanOnly&&job.planPreservedDesign.count(key)))){
      addSourceIssue(key,PlanReleaseGate::mainPlan,basis,u8"缺主平面依据",needed,source);
     }
    }else if(!job.architecturalPlanOnly){
     const auto item=std::find_if(job.sourceInventory.items.begin(),job.sourceInventory.items.end(),[&](const auto& row){
      return std::get<2>(row.first)==handle&&(row.second.check==bcpproduct::PlanCheck::pending||row.second.check==bcpproduct::PlanCheck::failed);
     });
     release.add(PlanReleaseGate::mainPlan,item==job.sourceInventory.items.end()?panel:std::get<0>(item->first),
      handle,"project-input",basis,u8"缺主平面依据",needed,source);
    }
    continue;
   }else if(kind=="REGISTERDATUM"||kind=="REGISTERNOTE"){
    std::string key,skip;
    require(bool(in>>key),"read registered plan datum key");
    std::getline(in,skip);
    const auto record=job.planRegisteredDatums.find(key);
    if(record!=job.planRegisteredDatums.end()){
     handle=record->second.output;
     target=key;
     const auto id=readEntity(handle);
     if(!id.isNull()){
      const auto entity=open<AcDbEntity>(id);
      const auto points=chainGrips(id);
      std::vector<std::wstring> text;
      productOpeningTexts(*entity,text);
      const auto expected=kind=="REGISTERDATUM"?L"TDbSymbElevation":L"TDbSymbMultiLeader";
      verified=std::wstring(entity->isA()->name())==expected&&productPlanDimensionVisible(*entity)&&
       !points.empty()&&points.size()==record->second.points.size()&&
       std::equal(points.begin(),points.end(),record->second.points.begin(),[&](const auto& a,const auto& b){
        return a.distanceTo(b)*job.unifiedUnitMM<=.01;
       })&&
       std::accumulate(text.begin(),text.end(),std::wstring{})==record->second.value;
      number=productUtf8(record->second.value);
     }
    }
   }else if(kind=="DETAILS"){
    std::string skip;
    std::getline(in,skip);
    continue;
   }else if(kind=="LINK"){
    std::string line;
    std::getline(in,line);
    std::istringstream row(line);
    std::string archiveTarget;
    if(!(row>>handle>>archiveTarget>>std::quoted(title))){
     job.detailPending.push_back({u8"可选详图索引","",u8"索引登记不完整"});
     continue;
    }
    ++expectedReferences;
    try{
    const auto link=std::find_if(job.planSheetLinks.begin(),job.planSheetLinks.end(),[&](const auto& candidate){
     return candidate.anchor==handle&&candidate.archiveTarget==archiveTarget;
    });
    if(link!=job.planSheetLinks.end()&&!link->note.empty()){
     number=link->note;
     target=link->target;
     const auto id=readEntity(link->note),destination=readEntity(link->target);
     if(!id.isNull()&&!destination.isNull()&&!readEntity(link->anchor).isNull()){
      const auto entity=open<AcDbEntity>(id),detail=open<AcDbEntity>(destination);
      const auto* note=AcDbMText::cast(entity.get());
      AcString contents;
      std::vector<std::wstring> labels;
      productOpeningTexts(*detail,labels);
      std::wstring joined;
      for(const auto& label:labels)if(label.rfind(L"1:",0)!=0)joined+=label;
      verified=note&&note->contents(contents)==Acad::eOk&&contents==link->text.c_str()&&
       joined==link->targetTitle&&productPlanDimensionVisible(*note)&&productPlanDimensionVisible(*detail);
     }
    }
    }catch(const std::exception& error){job.detailPending.push_back({u8"可选详图索引",handle,error.what()});}
    if(verified)++references;
   }else if(kind=="INDEX"){
    std::string line;
    std::getline(in,line);
    std::istringstream row(line);
    std::string targetHandle,targetNumber;
    if(!(row>>handle>>std::quoted(number)>>std::quoted(target)>>targetHandle>>std::quoted(targetNumber))||
      number.empty()||target.empty()||targetHandle.empty()||targetNumber.empty()){
     job.detailPending.push_back({u8"可选详图索引",handle,u8"索引登记不完整"});
     continue;
    }
    ++expectedReferences;
    try{
    const auto id=readEntity(handle);
    const auto file=job.unifiedSheetChecks.parent_path()/fs::u8path(target);
    if(!fs::is_regular_file(file)){
     job.detailPending.push_back({u8"可选详图索引",handle,u8"缺对应详图文件 "+target});
    }else if(!id.isNull()){
     const auto entity=open<AcDbEntity>(id);
     std::vector<std::wstring> text;
     productOpeningTexts(*entity,text);
     verified=productPlanDimensionVisible(*entity)&&
       std::find(text.begin(),text.end(),bcpproduct::decodeMessage(number))!=text.end();
     if(verified){
      AcDbDatabase details(false,true);
      AcDbObjectId targetId;
      verified=details.readDwgFile(file.c_str(),AcDbDatabase::kForReadAndAllShare,false)==Acad::eOk&&
       details.closeInput(true)==Acad::eOk&&
       details.getAcDbObjectId(targetId,false,AcDbHandle(bcpproduct::decodeMessage(targetHandle).c_str()))==Acad::eOk&&
       !targetId.isErased();
      if(verified){
       Working detailContext(&details);
       const auto detail=open<AcDbEntity>(targetId);
       text.clear();
       productOpeningTexts(*detail,text);
       verified=productPlanDimensionVisible(*detail)&&
        std::find(text.begin(),text.end(),bcpproduct::decodeMessage(targetNumber))!=text.end();
      }
     }
    }
    }catch(const std::exception& error){
     verified=false;
     job.detailPending.push_back({u8"可选详图索引",handle,error.what()});
    }
    if(verified)++references;
   }else require(false,"unknown sheet check row");
   if(!first)checks<<',';
   first=false;
   const bool optional=kind=="LINK"||kind=="INDEX";
   if(optional)productRecordRuleEvidence(job,"B08","{\"source\":"+jsonUtf8(bcpproduct::decodeMessage(handle))+
    ",\"requiredForPlanRelease\":false,\"savedReopened\":true,\"result\":\""+
    (verified?"verified-optional-detail-link":"pending-optional-detail-link")+"\"}");
   checks<<"{\"kind\":\""<<kind<<"\",\"requiredForPlanRelease\":"<<(optional?"false":"true")
         <<",\"handle\":"<<jsonUtf8(bcpproduct::decodeMessage(handle))
         <<",\"number\":"<<jsonUtf8(bcpproduct::decodeMessage(number))<<",\"target\":"<<jsonUtf8(bcpproduct::decodeMessage(target))
         <<",\"panel\":"<<checkedPanel<<",\"verified\":"<<(verified?"true":"false")<<frameDetail<<'}';
   if(!verified&&optional)job.detailPending.push_back({u8"可选详图索引",handle,u8"未从重开图核实索引或目标详图"});
   if(!verified&&!optional){
    const bool annotation=kind=="REGISTERDATUM"||kind=="REGISTERNOTE";
    release.add(annotation?PlanReleaseGate::mainPlan:PlanReleaseGate::files,checkedPanel,
     annotation?(handle.empty()?target:handle):"sheet",kind,number,
     annotation?u8"已登记的平面标高或说明重开未通过":u8"图签字段重开未通过","");
   }
  }
  require(kind=="END"&&!(in>>kind),"complete sheet check file");
 }
 for(const auto& [panel,sheet]:job.planSheetLayouts){
  if(sheet.sourceTitle.empty())continue;
  bool verified=false;
  std::string number;
  try{
   const auto mark=job.planSheetNumbers.find(panel);
   if(mark!=job.planSheetNumbers.end())number=productUtf8(mark->second.second);
   if(productVerifyFallbackPlanSheet(job,saved,panel,sheet)&&!numbers.count(number)&&!sheets.count(panel)){
    numbers.insert(number);
    sheets.insert(panel);
    ++sheetCount;
    verified=true;
   }
  }catch(const std::exception& error){
   job.detailPending.push_back({u8"打印图幅",std::to_string(panel),error.what()});
  }
  if(!first)checks<<',';
  first=false;
  checks<<"{\"kind\":\"GENERATEDSHEET\",\"requiredForPlanRelease\":true"
        <<",\"handle\":"<<jsonUtf8(bcpproduct::decodeMessage(sheet.title))
        <<",\"sourceTitle\":"<<jsonUtf8(bcpproduct::decodeMessage(sheet.sourceTitle))
        <<",\"number\":"<<jsonUtf8(bcpproduct::decodeMessage(number))
        <<",\"panel\":"<<panel<<",\"verified\":"<<(verified?"true":"false")<<'}';
 }
 for(const auto& [key,row]:job.sourceInventory.items){
  if(row.check!=bcpproduct::PlanCheck::pending&&row.check!=bcpproduct::PlanCheck::failed)continue;
  if(job.architecturalPlanOnly&&row.check==bcpproduct::PlanCheck::pending&&job.planPreservedDesign.count(key))continue;
  const auto gate=bcpproduct::planReleaseGateForSource(std::get<1>(key));
  if(gate)addSourceIssue(key,*gate,row.basis,row.reason,row.requiredProjectInput,"");
 }
 if(!job.sourceInventory.sealed||job.planVerifiedCandidateHash!=hash)
  release.add(PlanReleaseGate::reopened,0,"dwg","saved-reopened","",u8"缺独立保存重开对账","");
 for(std::size_t panel=1;panel<=job.planPrintWindows.size();++panel)
  if(!sheets.count(static_cast<int>(panel)))release.add(PlanReleaseGate::files,panel,"sheet","sheet-title","",
   u8"本幅缺可核图名及图号",job.unifiedSheetChecks.empty()?u8"提供图签块、图号和图名":"");
 if(!sheetCount&&job.planPrintWindows.empty())
  release.add(PlanReleaseGate::files,0,"sheet","sheet-title","",u8"没有可核验的交付图幅","");
 if(job.planUnverifiedNetAreaLabels>namedAreaFailures)
  release.add(PlanReleaseGate::mainPlan,0,"area-label","room-area","",u8"存在已计算但未落字或重开未通过的净域，见房间核验","");
 if(job.planPlotStyle.empty())release.add(PlanReleaseGate::files,0,"plot-style","plot-style","",
   u8"缺确认的正式打印样式",u8"提供项目 CTB／STB 或经实际出图验证的等效样式",u8"打印样式");
 if(!job.planPrintReview||job.planPrintedPages!=job.planPrintWindows.size()||!job.planPrintedPages)
  release.add(PlanReleaseGate::files,0,"pdf","pdf-pages","",u8"打印校核稿未全幅生成","");
 // Reuse explicit drawing findings from the native readers. The general
 // issue list also contains informational items and paused project scope.
 const std::set<std::string> releaseDefects{u8"平面名称终核失败",u8"房名与门窗编号净空",u8"图名图面净空",
  u8"打印校核",u8"打印图幅",u8"轴圈局部失败",u8"外围尺寸文字净空",u8"纸面图元净空"};
 std::set<std::array<std::string,3>> seen;
 for(const auto& issue:job.detailPending){
  if(!seen.insert(issue).second)continue;
  if(releaseDefects.count(issue[0])){
   const bool print=issue[0]==u8"打印校核"||issue[0]==u8"打印图幅"||issue[0]==u8"纸面图元净空";
   std::size_t panel{};
   auto source=issue[1];
   if(issue[0]==u8"纸面图元净空"){
    const auto slash=source.find('/');
    if(slash!=std::string::npos&&slash&&source.find_first_not_of("0123456789") == slash){
     panel=static_cast<std::size_t>(std::stoul(source.substr(0,slash)));
     source.erase(0,slash+1);
    }
   }
   release.add(print?PlanReleaseGate::files:PlanReleaseGate::mainPlan,panel,source,issue[0],"",issue[2],"");
  }
 }
 if(!job.planPrintGeometryChecked)
  release.add(PlanReleaseGate::files,0,"pdf","paper-geometry","",u8"纸面几何核验未完成","");
 else if(job.planPrintDefects&&release.passes(PlanReleaseGate::files))
  release.add(PlanReleaseGate::files,0,"pdf","paper-geometry","",u8"纸面几何核验仍有缺陷","");
 const std::array<std::pair<PlanReleaseGate,const char*>,3> gates{{
  {PlanReleaseGate::mainPlan,u8"主平面完整"},{PlanReleaseGate::reopened,u8"重开结果正确"},
  {PlanReleaseGate::files,u8"交付文件可用"}}};
 job.drawingChecks.appendTo(release);
 job.formalReady=job.formalPlan&&release.issues.empty();
 const bool dwgReopened=job.sourceInventory.sealed&&job.planVerifiedCandidateHash==hash;
 job.formalDelivery=job.formalReady||(job.formalPlan&&job.architecturalPlanOnly&&dwgReopened&&!release.issues.empty());
 std::ostringstream out;
 out<<"{\"sourceSHA256\":\""<<job.sourceHash<<"\",\"outputSHA256\":\""<<hash
    <<"\",\"projectParametersSHA256\":\""<<job.unifiedProjectParametersHash
    <<"\",\"formalRequested\":"<<(job.formalPlan?"true":"false")
    <<",\"formalReady\":"<<(job.formalReady?"true":"false")<<",\"formalDelivery\":"<<(job.formalDelivery?"true":"false")
    <<",\"dwgReopened\":"<<(dwgReopened?"true":"false")<<",\"sourcePending\":"<<unresolved<<",\"sourceFailed\":"<<failed
    <<",\"acceptanceProfile\":\"architectural-main-plan-B01-B05\",\"acceptanceMode\":\""
    <<(job.architecturalPlanOnly?"architectural-plan":"construction-document")<<"\""
    <<",\"preservedDesignChecks\":"<<job.planPreservedDesign.size()
    <<",\"verifiedTitleBlocks\":"<<sheetCount<<",\"verifiedIndexLinks\":"<<references
    <<",\"optionalIndexChecks\":"<<expectedReferences<<",\"optionalDetailInputs\":"<<optionalInputs
    <<",\"unverifiedNetAreaLabels\":"<<job.planUnverifiedNetAreaLabels
    <<",\"plottedPages\":"<<job.planPrintedPages<<",\"sheetChecks\":["<<checks.str()<<"],\"gates\":{";
 for(std::size_t i=0;i<gates.size();++i){
  if(i)out<<',';
  out<<'"'<<gateName(gates[i].first)<<"\":"<<(release.passes(gates[i].first)?"true":"false");
 }
 out<<"},\"blockingLocations\":[";
 bool firstLocation=true;
 const auto quoted=[](const std::string& value){return jsonUtf8(bcpproduct::decodeMessage(value));};
 const auto append=[](std::string& combined,const std::string& value,std::set<std::string>& seen){
  if(value.empty()||!seen.insert(value).second)return;
  if(!combined.empty())combined+=u8"；";
  combined+=value;
 };
 for(const auto& [key,issue]:release.issues){
  const auto& [panel,source]=key;
  if(!firstLocation)out<<',';
  firstLocation=false;
  const auto& title=issue.location.empty()?issue.findings.front().scope:issue.location;
  const auto floorIt=job.planSourceFloorNames.find(panel);
  const auto floor=panel==0?std::string(u8"全套"):floorIt==job.planSourceFloorNames.end()?
   u8"源图幅 "+std::to_string(panel):floorIt->second;
  auto drawingState=std::string(u8"成图保留本项所在图幅的当前表达，具体差异见问题");
  const auto handle=source.substr(0,source.find('/'));
  if(handle.size()>=2&&handle.find_first_not_of("0123456789ABCDEFabcdef")==std::string::npos){
   const auto id=readEntity(handle);
   if(!id.isNull())try{
    drawingState=productPlanDimensionVisible(*open<AcDbEntity>(id))?
     u8"重开后该对象可见，保留当前表达":u8"重开后该源对象不显示，见本项对应关系";
   }catch(const std::exception&){drawingState=u8"成图已重开，该对象显示状态未能读取";}
  }
  out<<"{\"panel\":"<<panel<<",\"source\":"<<quoted(source)<<",\"location\":"<<quoted(title)
     <<",\"floor\":"<<quoted(floor)<<",\"drawingState\":"<<quoted(drawingState)<<",\"findings\":[";
  std::string basis,observed,input,impact,rules;
  std::set<std::string> bases,reasons,inputs,impacts,ruleIds;
  bool firstFinding=true;
  for(const auto& finding:issue.findings){
   if(!firstFinding)out<<',';
   firstFinding=false;
   out<<"{\"gate\":\""<<gateName(finding.gate)<<"\",\"check\":"<<quoted(finding.scope)
      <<",\"basis\":"<<quoted(finding.basis)<<",\"reason\":"<<quoted(finding.reason)<<",\"requiredInput\":"<<quoted(finding.input)
      <<",\"rules\":[";
   for(std::size_t i=0;i<finding.rules.size();++i){
    if(i)out<<',';
    out<<quoted(finding.rules[i]);
    append(rules,finding.rules[i],ruleIds);
   }
   out<<"]}";
   append(basis,finding.basis,bases);
   append(observed,finding.reason,reasons);
   append(input,finding.input,inputs);
   const bool area=finding.scope=="room-area"||finding.scope=="net-domain";
   append(impact,area?u8"房间净面积":finding.gate==PlanReleaseGate::files?u8"交付文件":
    finding.gate==PlanReleaseGate::reopened?u8"源图和成图核对":u8"施工平面",impacts);
  }
  out<<"]}";
  if(rules.empty())rules="P04;P06";
  if(observed.empty())observed=u8"重开核验尚未证实本项已落实";
  if(input.empty())log(errors,panel,title,source,basis,observed,u8"按源图修正并重新验收");
  else log(missing,panel,title,source,basis,observed,input);
  checklist<<panel<<','<<csvCell(title)<<','<<csvCell(source)<<','<<csvCell(observed)<<','<<csvCell(basis)<<','
   <<csvCell(input)<<','<<csvCell(impact)<<','<<csvCell(job.formalDelivery?u8"本项留在清单，其余图幅继续完成":
    input.empty()?u8"按源图修正并重新验收":u8"补齐所缺依据后修正并复验")<<','<<hash<<','
   <<csvCell(rules)<<','<<csvCell(floor)<<','<<csvCell(drawingState)<<"\r\n";
 }
 out<<"],\"remaining\":[";
 bool firstGap=true;
 for(const auto& [gate,name]:gates)if(!release.passes(gate)){
  if(!firstGap)out<<',';
  firstGap=false;
  out<<quoted(name);
  job.detailPending.push_back({u8"施工图交付","",std::string(name)+u8"未通过，见放行报告"});
 }
 out<<"]}";
 writeNew(job.root/L"plan-delivery-review.json",out.str());
 writeNew(job.root/L"缺失资料清单.csv",missing.str());
 writeNew(job.root/L"生成错误清单.csv",errors.str());
 if(!job.formalReady){
  if(release.issues.empty())checklist<<u8"0,全套,,尚未请求正式验收,,,正式交付,需要正式版时运行正式验收,"<<hash<<"\r\n";
  job.planBlockingChecklist=checklist.str();
 }
 const auto result=std::string("{\"savedReopened\":true,\"report\":\"plan-delivery-review.json\",\"result\":\"")+
  (job.formalReady?"verified-formal-delivery":job.formalDelivery?"formal-delivery-with-documented-exceptions":"pending-formal-delivery-review")+"\"}";
 for(const auto* rule:{"P03","P04","QA02","Q10"})productRecordRuleEvidence(job,rule,result);
}
