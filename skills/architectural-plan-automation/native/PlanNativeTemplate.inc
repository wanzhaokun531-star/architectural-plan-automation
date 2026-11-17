struct PlanTemplateRequest {
 fs::path source,output,report,resources,sheets;
 ProductTemplateRun run;
 std::vector<ProductTypographyIssue> issues;
 std::set<std::string> annotationScope;
 std::string protectedAfter;
 std::vector<std::string> retiredFrames;
 std::wstring previousPlotters,previousStyles;
 bool plotPathsChanged{};
 ~PlanTemplateRequest(){if(plotPathsChanged){acedSetEnv(L"PrinterConfigDir",previousPlotters.c_str());acedSetEnv(L"PrinterStyleSheetDir",previousStyles.c_str());}}
};

struct PlanTemplateTextSpec {ProductDimensionFont font;double modelHeight{};};
std::optional<PlanTemplateTextSpec> planTemplateTextSpec(const ProductSourceAnnotation& row,const std::string& role){
 if(role=="dimension"||role=="special-direction")return {};
 PlanTemplateTextSpec spec{{L"FY_HZ",L"tssdeng.shx",L"hztxt.shx",.7,0.,0},400.};
 if(role=="opening-label")spec={{L"_TCH_WINDOW",L"tssdeng.shx",L"hztxt.shx",.65,0.,0},row.type==L"TDbText"?238.:280.};
 else if(role=="special-title-block")spec={{L"FYTK-HZ",L"tssdeng.shx",L"hztxt.shx",.8,0.,0},350.};
 else if(role=="special-drawing-title")spec={{L"FY_FS",L"simsun.ttc",L"",.7,0.,0},700.};
 else if(role=="special-survey-elevation")spec={{L"FY_DIM",L"simplex.shx",L"hztxt.shx",.7,0.,0},350.};
 else if(role=="special-scale-or-slope")spec.modelHeight=350.;
 else if(role=="special-area-superscript")spec.modelHeight=0.;
 const auto body=productTypographyText(row);
 if(role=="general"&&(body==L"上"||body==L"下"||body.find(L"坡度")!=std::wstring::npos))spec.modelHeight=350.;
 return spec;
}

AcDbObjectId planTemplateCopyFrameDefinition(AcDbDatabase& db,AcDbObjectId original,std::map<AcDbObjectId,AcDbObjectId>& copied,int depth=0){
 require(depth<32,"title block nesting limit");if(copied.count(original))return copied.at(original);
 const auto source=open<AcDbBlockTableRecord>(original);require(!source->isFromExternalReference(),"external title block is protected");
 auto block=std::make_unique<AcDbBlockTableRecord>();static unsigned long sequence{};const auto name=L"PLAN_TEMPLATE_TITLE_"+std::to_wstring(GetTickCount64())+L"_"+std::to_wstring(++sequence);status(block->setName(name.c_str()),"unique title block name");
 block->setOrigin(source->origin());AcDbObjectId result;{auto table=editObject<AcDbBlockTable>(db.blockTableId());status(table->add(result,block.get()),"isolate scaled title block");}block.release()->close();copied[original]=result;
 AcDbObjectIdArray members;AcDbBlockTableRecordIterator* raw{};status(source->newIterator(raw),"read title members");std::unique_ptr<AcDbBlockTableRecordIterator> it(raw);
 for(;!it->done();it->step()){AcDbObjectId id;status(it->getEntityId(id),"title member id");members.append(id);}
 AcDbIdMapping mapping;status(db.deepCloneObjects(members,result,mapping,false),"copy title block members");
 for(int k=0;k<members.length();++k){AcDbIdPair pair(members[k],{},false);require(mapping.compute(pair)&&pair.isCloned(),"title member clone missing");
  auto entity=open<AcDbEntity>(pair.value());if(const auto* child=AcDbBlockReference::cast(entity.get())){const auto old=child->blockTableRecord();entity.reset();const auto next=planTemplateCopyFrameDefinition(db,old,copied,depth+1);status(editObject<AcDbBlockReference>(pair.value())->setBlockTableRecord(next),"isolate nested title block");}}
 return result;
}
void planTemplateIsolateFrames(AcDbDatabase& db){
 std::set<std::string> roots;
 for(const auto& row:productTypographyInventory(db))if(row.visible&&productTypographyRole(db,row)=="special-title-block")roots.insert(row.root);
 for(const auto& root:roots){const auto id=find(db,root);auto block=open<AcDbBlockReference>(id);const auto original=block->blockTableRecord();
  std::vector<ProductRoomLeaf> before;productRoomLeaves(*block,before);block.reset();std::map<AcDbObjectId,AcDbObjectId> copied;
  const auto replacement=planTemplateCopyFrameDefinition(db,original,copied);status(editObject<AcDbBlockReference>(id)->setBlockTableRecord(replacement),"retain title instance placement");
  std::vector<ProductRoomLeaf> after;productRoomLeaves(*open<AcDbEntity>(id),after);require(productPlanTextSameLeaves(before,after),"frame isolation altered original glyphs");
 }
}

void planTemplateOtherText(ProductJob& job,AcDbDatabase& db,std::vector<ProductTypographyIssue>& issues){
 std::set<std::string> done;
 for(const auto& row:productTypographyInventory(db))if(row.visible&&done.insert(row.handle).second){
  const auto role=productTypographyRole(db,row);const auto spec=planTemplateTextSpec(row,role);
  if(!spec||role=="general"||role=="opening-label"||role=="linked-note")continue;
  try{
   const auto font=productRestoreTypographyFont(db,spec->font,0);
   auto height=spec->modelHeight*productTypographySpaceFactor(db,row)/productTypographyInstanceScale(db,row);
   if(height==0){std::vector<ProductRoomLeaf> leaves;productRoomLeaves(*open<AcDbEntity>(find(db,row.handle)),leaves);if(!leaves.empty())height=leaves.front().height;}
   auto editable=row;if(role=="special-area"||row.type==L"TDbDrawingName")editable.kind="template-mixed-text";
   if(height>0){const auto issue=productTypographyRestyleText(db,editable,font,height);if(!issue.empty())issues.push_back({row.path,role,issue});}
  }catch(const std::exception& e){issues.push_back({row.path,role,e.what()});}
 }
}

void planTemplateAuditText(AcDbDatabase& db,const fs::path& drawing,const fs::path& report,std::vector<ProductTypographyIssue>& issues){
 std::ostringstream out;out<<std::setprecision(12)<<"{\"outputSHA256\":\""<<sha(drawing)<<"\",\"savedReopened\":true,\"objects\":[";bool first=true;
 const auto record=[&](const std::string& object,const std::string& role,const PlanTemplateTextSpec& spec,std::vector<ProductRoomLeaf> leaves,double scale){
  bool good=!leaves.empty();std::string reason;
  if(!first)out<<',';first=false;out<<"{\"object\":\""<<object<<"\",\"role\":\""<<role<<"\",\"glyphs\":[";
  for(std::size_t k=0;k<leaves.size();++k){const auto& leaf=leaves[k];if(k)out<<',';
   const auto actual=productReadDimensionFont(leaf.style);const bool font=productTypographyFontMatches(leaf.style,spec.font,0);
   const auto height=leaf.height*scale;
   const double expected=(role=="special-area"&&(leaf.text==L"2"||leaf.text==L"²")&&leaves.size()>1)?spec.modelHeight*.5:
    (role=="special-drawing-title"&&std::regex_match(leaf.text,std::wregex(L"1[:：][0-9]+")))?spec.modelHeight*5./7.:spec.modelHeight;
   good=good&&font&&(expected==0||std::abs(height-expected)<.01);
   out<<"{\"text\":"<<jsonUtf8(leaf.text)<<",\"style\":"<<jsonUtf8(actual.name)<<",\"font\":"<<jsonUtf8(actual.primary)
    <<",\"bigFont\":"<<jsonUtf8(actual.big)<<",\"width\":"<<actual.width<<",\"height\":"<<height<<",\"expectedHeight\":"<<spec.modelHeight<<'}';
  }
  if(!good){reason=u8"模板用途字体定义或有效字高不符；保留正文与构件关联，未计为通过";issues.push_back({object,role,reason});}
  out<<"],\"verified\":"<<(good?"true":"false")<<",\"reason\":"<<jsonUtf8(bcpproduct::decodeMessage(reason))<<'}';
 };
 for(const auto& row:productTypographyInventory(db))if(row.visible){
  const auto role=productTypographyRole(db,row);auto spec=planTemplateTextSpec(row,role);if(!spec)continue;
  try{std::vector<ProductRoomLeaf> leaves;productRoomLeaves(*open<AcDbEntity>(find(db,row.handle)),leaves);
   spec->modelHeight*=productTypographySpaceFactor(db,row);record(row.path,role,*spec,leaves,productTypographyInstanceScale(db,row));
  }catch(const std::exception& e){issues.push_back({row.path,role,e.what()});}
 }
 for(const auto& [h,type]:modelTypes(db))if(type==L"TDbOpening"||type==L"TDbGroupOpening"){
  const auto entity=open<AcDbEntity>(find(db,ascii(h)));if(!productPlanDimensionVisible(*entity))continue;
  std::vector<ProductRoomLeaf> leaves;productRoomLeaves(*entity,leaves);if(leaves.empty())continue;
  record(ascii(h),"native-opening-label",{{L"_TCH_WINDOW",L"tssdeng.shx",L"hztxt.shx",.65,0.,0},238.},leaves,1.);
 }
 out<<"]}";writeNew(report/L"template-typography-readback.json",out.str());
}

std::vector<std::string> planTemplateFrameCandidates(AcDbDatabase& db){
 struct Frame{std::string handle;AcGePoint3d centre;double width,height;};std::vector<Frame> frames;std::set<std::string> selected;
 for(const auto& [h,type]:modelTypes(db))if(type==L"AcDbPolyline"){
  const auto e=open<AcDbPolyline>(find(db,ascii(h)));if(symbolName(e->layerId())!=L"Defpoints"||!e->isClosed()||e->numVerts()!=4)continue;
  AcDbExtents box;if(e->getGeomExtents(box)!=Acad::eOk)continue;bool rectangle=true;
  for(unsigned k=0;k<4;++k){double bulge{};AcGePoint2d a,b;e->getBulgeAt(k,bulge);e->getPointAt(k,a);e->getPointAt((k+1)%4,b);
   rectangle=rectangle&&std::abs(bulge)<1e-9&&(std::abs(a.x-b.x)<.01||std::abs(a.y-b.y)<.01);}
  if(rectangle)frames.push_back({ascii(h),box.minPoint()+(box.maxPoint()-box.minPoint())*.5,box.maxPoint().x-box.minPoint().x,box.maxPoint().y-box.minPoint().y});
 }
 for(const auto& a:frames){std::vector<std::string> group{a.handle};
  for(int step=1;step<4;++step){auto it=std::find_if(frames.begin(),frames.end(),[&](const auto& b){return a.centre.distanceTo(b.centre)<.01&&std::abs(b.width-a.width-step*1600.)<.01&&std::abs(b.height-a.height-step*1600.)<.01;});if(it!=frames.end())group.push_back(it->handle);}
  if(group.size()==4)selected.insert(group.begin(),group.end());
 }
 return {selected.begin(),selected.end()};
}

void planTemplateLayers(ProductJob& job,AcDbDatabase& db){
 std::ifstream input(job.profile/L"plotters/layers.tsv");std::string header;input>>header;
 require(header=="PLANTEMPLATELAYERS1","template layer policy absent");
 std::map<std::wstring,AcDbObjectId> layers;std::string role,name;int aci{},thinWeight{},largeWeight{};
 // A2 uses the A3/A4 profile; A1 is applied through per-viewport overrides separately.
 while(input>>role&&role!="END"){
  require(bool(input>>std::quoted(name)>>aci>>thinWeight>>largeWeight),"template layer row");
  const auto wide=bcpproduct::decodeMessage(name);const auto id=productPlanLayer(db,wide.c_str(),aci);layers[wide]=id;
  auto layer=editObject<AcDbLayerTableRecord>(id);AcCmColor color;color.setColorIndex(static_cast<Adesk::UInt16>(aci));
  // AutoCAD's fixed enum has 0.09 but no 0.10; the CTB carries exact 0.10.
  layer->setColor(color);if(thinWeight>=0)status(layer->setLineWeight(static_cast<AcDb::LineWeight>(thinWeight==10?9:thinWeight)),"template layer paper weight");
  if(largeWeight>=0)for(const auto& [panel,sheet]:job.planSheetLayouts)if(std::max(sheet.paperWidth,sheet.paperHeight)>594.1)
   status(layer->setLineWeight(static_cast<AcDb::LineWeight>(largeWeight==10?9:largeWeight),find(db,sheet.viewport)),"template A1 layer viewport weight");
 }
 require(role=="END","complete template layer policy");
 for(const auto& [h,type]:modelTypes(db)){
  auto entity=open<AcDbEntity>(find(db,ascii(h)));const auto old=symbolName(entity->layerId());std::wstring target;
  if(type==L"TDbWall")target=L"A-WALL";
  else if(type==L"TDbColumn")target=L"A-COLUMN";
  else if(type==L"TDbOpening"||type==L"TDbGroupOpening")target=L"A-WINDOW";
  else if(type==L"AcDbLine"&&symbolName(entity->linetypeId())==L"DOTE")target=L"A-DOTE";
  else if(old==L"A-房间名称")target=old;
  else if(old==L"A-家具"||old==L"A-洁具、厨具")target=old;
  if(!target.empty()){
   const auto id=entity->objectId();entity.reset();auto edit=editObject<AcDbEntity>(id);
   status(edit->setLayer(layers.at(target)),"template semantic layer");status(edit->setColorIndex(256),"template semantic ByLayer color");
   status(edit->setLineWeight(AcDb::kLnWtByLayer),"template semantic layer lineweight");
  }
 }
 // ACI254 is reserved for gray furniture at print time; preserve black tables.
 const auto table=open<AcDbBlockTable>(db.blockTableId());AcDbBlockTableIterator* raw{};status(table->newIterator(raw),"template print block inventory");
 std::unique_ptr<AcDbBlockTableIterator> it(raw);
 for(;!it->done();it->step()){
  AcDbObjectId block;status(it->getRecordId(block),"template print block id");const auto b=open<AcDbBlockTableRecord>(block);
  if(b->isFromExternalReference())continue;AcDbBlockTableRecordIterator* rawEntities{};status(b->newIterator(rawEntities),"template print entities");
  std::unique_ptr<AcDbBlockTableRecordIterator> entities(rawEntities);
  for(;!entities->done();entities->step()){
   AcDbObjectId id;status(entities->getEntityId(id),"template plot entity");auto e=open<AcDbEntity>(id);const auto layer=open<AcDbLayerTableRecord>(e->layerId());
   const auto color=e->colorIndex()==256?layer->color().colorIndex():e->colorIndex();
   if(color==254&&symbolName(e->layerId())!=L"A-家具"){e.reset();status(editObject<AcDbEntity>(id)->setColorIndex(7),"keep non-furniture 254 black under template CTB");}
  }
 }
}

void planTemplateTitleClearance(ProductJob& job,AcDbDatabase& db,std::vector<ProductTypographyIssue>& issues){
 std::vector<AcDbExtents> circles;
 for(const auto& [h,type]:modelTypes(db))if(type==L"TDbAxisLabelSet"){
  const auto e=open<AcDbEntity>(find(db,ascii(h)));if(!productPlanDimensionVisible(*e))continue;const auto g=chainGrips(e->objectId());
  if(g.size()!=4&&g.size()!=8)continue;for(std::size_t k=0;k<g.size();k+=4){const auto radius=g[k+1].distanceTo(g[k+2]);circles.emplace_back(g[k+1]-AcGeVector3d(radius,radius,0),g[k+1]+AcGeVector3d(radius,radius,0));}
 }
 const auto rows=productTypographyInventory(db);
 for(const auto& row:rows)if(row.visible&&row.root==row.handle&&productTypographyRole(db,row)=="special-drawing-title"){
  try{const auto id=find(db,row.handle);auto title=open<AcDbEntity>(id);AcDbExtents box;status(title->getGeomExtents(box),"read template title extent");
   if(title->ownerId()!=modelId(db)){
    for(const auto& [panel,sheet]:job.planSheetLayouts){const auto layout=acdbHostApplicationServices()->layoutManager()->findLayoutNamed(sheet.name.c_str(),&db);
     if(open<AcDbLayout>(layout)->getBlockTableRecordId()!=title->ownerId())continue;
     const double shift=std::min(0.,sheet.paperWidth-10.-box.maxPoint().x);require(box.minPoint().x+shift>=10.,"paper title cannot fit its current frame at the rule height");
     if(shift<0){title.reset();status(editObject<AcDbEntity>(id)->transformBy(AcGeMatrix3d::translation({shift,0,0})),"keep full template drawing title within paper");}break;
    }continue;
   }
   double shift=0.;for(const auto& circle:circles)if(box.maxPoint().x>=circle.minPoint().x-200&&box.minPoint().x<=circle.maxPoint().x+200&&box.maxPoint().y>=circle.minPoint().y-200&&box.minPoint().y<=circle.maxPoint().y+200)
    shift=std::min(shift,circle.minPoint().y-200.-box.maxPoint().y);
   if(shift>=-.01)continue;require(shift>=-1800.,"title-axis clearance exceeds local shift limit");title.reset();
   std::set<std::string> group{row.handle};
   for(const auto& other:rows)if(other.visible&&other.root==other.handle&&other.handle!=row.handle){
    const auto role=productTypographyRole(db,other);if(role!="special-area"&&role!="special-area-superscript"&&role!="general")continue;
    const auto e=open<AcDbEntity>(find(db,other.handle));if(e->ownerId()!=modelId(db))continue;AcDbExtents nearby;if(e->getGeomExtents(nearby)!=Acad::eOk)continue;
    if(nearby.maxPoint().y<=box.maxPoint().y&&nearby.maxPoint().y>=box.minPoint().y-6000.&&nearby.maxPoint().x>=box.minPoint().x-500.&&nearby.minPoint().x<=box.maxPoint().x+500.)group.insert(other.handle);
   }
   for(const auto& h:group)status(editObject<AcDbEntity>(find(db,h))->transformBy(AcGeMatrix3d::translation({0,shift,0})),"move title and related descriptions below axis circles");
  }catch(const std::exception& e){issues.push_back({row.path,"title-clearance",e.what()});}
 }
}

void planTemplatePlotSettings(ProductJob& job,AcDbDatabase& db){
 auto* manager=acdbHostApplicationServices()->layoutManager();
 auto* validator=acdbHostApplicationServices()->plotSettingsValidator();
 const auto pc3=job.profile/L"plotters/FY-XS-2026.9-PDF.pc3";
 const auto ctb=job.profile/L"plotters/FY-XS-2026.9-A3 A4.ctb";
 require(fs::is_regular_file(pc3)&&fs::is_regular_file(ctb),"packaged template plot configuration absent");
 for(const auto& [panel,sheet]:job.planSheetLayouts){
  const auto id=manager->findLayoutNamed(sheet.name.c_str(),&db);auto layout=editObject<AcDbLayout>(id);
  validator->refreshLists(layout.get());status(validator->setPlotCfgName(layout.get(),pc3.filename().c_str()),"bind template PDF device");
  validator->refreshLists(layout.get());productSelectPlanPaper(*layout,*validator,sheet.paperWidth,sheet.paperHeight,1.);
  const auto selectedStyle=std::max(sheet.paperWidth,sheet.paperHeight)>594.1?L"FY-XS-2026.9-A1.ctb":L"FY-XS-2026.9-A3 A4.ctb";
  const auto styleStatus=validator->setCurrentStyleSheet(layout.get(),selectedStyle);
  require(styleStatus==Acad::eOk,("bind template CTB status="+std::to_string(styleStatus)+" CTBmode="+std::to_string(db.plotStyleMode())).c_str());
  layout->setPrintLineweights(true);layout->setPlotPlotStyles(true);
 }
 job.planPlotStyle=ctb.wstring();
}

void finishPlanTemplate(void* raw){
 std::unique_ptr<PlanTemplateRequest> task(static_cast<PlanTemplateRequest*>(raw));auto& p=*task;AcApDocument* opened{};
 try{
  // Keep the command document alive; close owned drawing databases before module unload.
  AcDbDatabase saved(false,true);readDrawing(saved,p.output);Working w(&saved);
  require(bcpproduct::samePublicGeometry(p.protectedAfter,planTypographyProtected(saved,p.annotationScope)),"template non-annotation objects changed on independent reopen");
  for(const auto& h:p.retiredFrames){AcDbObjectId id;require(saved.getAcDbObjectId(id,false,AcDbHandle(bcpproduct::decodeMessage(h).c_str()))!=Acad::eOk||id.isErased(),"old helper frame survived reopen");}
  planTemplateAuditText(saved,p.output,p.report,p.issues);
  std::ostringstream audit;audit<<"{\"savedReopened\":true,\"sourceSHA256\":\""<<sha(p.source)<<"\",\"outputSHA256\":\""<<sha(p.output)
   <<"\",\"templateSHA256\":\""<<p.run.templateHash<<"\",\"objects\":[";bool first=true;
  for(auto& item:p.run.objects){
   if(item.issue.empty())try{
    for(const auto& handle:item.targets){const auto id=find(saved,handle);auto e=open<AcDbEntity>(id);
     require(productPlanDimensionVisible(*e),"reopened template object invisible");
     if(item.kind=="dimension"){
      require(productTemplateDimensionDefinition(*e)==p.run.dimensionDefinition,"reopened template segment definition differs");
      const auto actual=productReadSourceDimension(saved,handle,.01);
      require(actual.points.size()==item.stations.size()&&actual.values.size()==item.values.size(),"reopened measurement count differs");
      for(std::size_t k=0;k<item.stations.size();++k)require(actual.points[k].distanceTo(item.stations[k])<.01,"reopened station differs");
      for(std::size_t k=0;k<item.values.size();++k)require(std::abs(actual.values[k]-item.values[k])<.01,"reopened measured value differs");
     }else{const auto g=chainGrips(id);const auto k=static_cast<std::size_t>(&handle-item.targets.data())*4;
      require(g.size()==4&&g[0].distanceTo(item.axisGrips[k])<.01&&g[1].distanceTo(item.axisGrips[k+1])<.01,"reopened axis position differs");
      std::vector<ProductRoomLeaf> leaves;productRoomLeaves(*e,leaves);require(!leaves.empty(),"reopened native axis glyph absent");
      for(const auto& leaf:leaves)require(leaf.text==item.axisNumber&&productTypographyFontMatches(leaf.style,{L"_TCH_AXIS",L"COMPLEX",L"GBCBIG",1,0,0},0),"reopened native axis font differs");
     }
    }
   }catch(const std::exception& error){item.issue=error.what();}
   if(!item.issue.empty())p.issues.push_back({item.source,item.kind,item.issue});
   if(!first)audit<<',';first=false;audit<<"{\"source\":\""<<item.source<<"\",\"kind\":\""<<item.kind<<"\",\"donor\":\""<<item.donor
    <<"\",\"targets\":[";for(std::size_t k=0;k<item.targets.size();++k){if(k)audit<<',';audit<<'"'<<item.targets[k]<<'"';}
   audit<<"],\"stations\":"<<wholePoints(item.stations)<<",\"axisGrips\":"<<wholePoints(item.axisGrips)
    <<",\"verified\":"<<(item.issue.empty()?"true":"false")<<",\"reason\":"<<jsonUtf8(bcpproduct::decodeMessage(item.issue))<<'}';
  }audit<<"],\"retiredHiddenDimensions\":[";for(std::size_t k=0;k<p.run.retiredHidden.size();++k){if(k)audit<<',';audit<<'"'<<p.run.retiredHidden[k]<<'"';}
  audit<<"]}";writeNew(p.report/L"template-native-readback.json",audit.str());
  auto sheetJob=readJointPaperJob(p.source,p.sheets,p.output,p.report);ProductJob job;job.root=p.report;job.profile=p.resources;job.planMode="template-finalize";
  opened=productOpenPrintedInkCandidate(p.output);{Working print(opened->database());jointPaperSheets(job,*opened->database(),sheetJob);}
  productPrintPlanLayouts(job,*opened,p.output,p.report);
  {Working print(opened->database());ProductPrintedInkProof proof;proof.directory=p.report;proof.digest=sha(p.output);
   productWritePrintedInkRequest(job,*opened->database(),proof,p.report/L"typography-paper-input.json",false,nullptr,false);}
  productStatus(acDocManager->appContextCloseDocument(opened),"close template final plot");opened=nullptr;
  std::ostringstream result;result<<"{\"completed\":true,\"dwgReopened\":true,\"nonAnnotationGeometryPreserved\":true,\"sourceSHA256\":\""<<sha(p.source)<<"\",\"outputSHA256\":\""<<sha(p.output)<<"\",\"unresolved\":[";
  first=true;for(const auto& issue:p.issues){if(!first)result<<',';first=false;result<<"{\"object\":\""<<issue.object<<"\",\"role\":\""<<issue.role<<"\",\"reason\":"<<jsonUtf8(bcpproduct::decodeMessage(issue.reason))<<'}';}
  result<<"]}";writeNew(p.report/L"product-result.json",result.str());
 }catch(const std::exception& error){if(opened)acDocManager->appContextCloseDocument(opened);writeNew(p.report/L"product-result.json","{\"completed\":false,\"error\":"+jsonUtf8(bcpproduct::decodeMessage(error.what()))+"}");}
}

void executePlanTemplate(std::unique_ptr<PlanTemplateRequest> p){
 require(curDoc()&&fs::equivalent(p->source,curDoc()->fileName()),"template finalization requires owned control copy");
 auto& db=*curDoc()->database();Working working(&db);ProductJob job;job.root=p->report;job.profile=p->resources;
 wchar_t plotters[32768]{},styles[32768]{};
 require(acedGetEnv(L"PrinterConfigDir",plotters)==RTNORM&&acedGetEnv(L"PrinterStyleSheetDir",styles)==RTNORM,"read owned plot resource paths");
 p->previousPlotters=plotters;p->previousStyles=styles;p->plotPathsChanged=true;
 require(acedSetEnv(L"PrinterConfigDir",(p->resources/L"plotters").c_str())==RTNORM&&
         acedSetEnv(L"PrinterStyleSheetDir",(p->resources/L"plotters").c_str())==RTNORM,"set owned template plot resource paths");
 acplPlotConfigManager->refreshList();
 job.architecturalPlanOnly=true;job.unifiedPlan=true;job.planMode="template-finalize";
 const auto sheetJob=readJointPaperJob(p->source,p->sheets,p->output,p->report);jointPaperSheets(job,db,sheetJob);
 AcDbDatabase reference(false,true);readDrawing(reference,p->resources/L"reference.dwg");
 for(const auto& row:productTypographyInventory(db))p->annotationScope.insert(row.root);
 for(const auto& [h,type]:modelTypes(db))if(type==L"TDbAxisLabelSet"||type==L"TDbOpening"||type==L"TDbGroupOpening")p->annotationScope.insert(ascii(h));
 p->retiredFrames=planTemplateFrameCandidates(db);for(const auto& h:p->retiredFrames)p->annotationScope.insert(h);
 const auto protectedBefore=planTypographyProtected(db,p->annotationScope);
 planTemplateIsolateFrames(db);
 p->issues=productNormalizeWholePlanTypography(job,db,reference);
 std::map<std::wstring,std::vector<AcDbObjectId>> openingGroups;
 for(const auto& [h,type]:modelTypes(db))if(type==L"TDbOpening"||type==L"TDbGroupOpening"){
  const auto id=find(db,ascii(h));const auto e=open<AcDbEntity>(id);if(!productPlanDimensionVisible(*e))continue;
  std::vector<ProductRoomLeaf> leaves;productRoomLeaves(*e,leaves);if(!leaves.empty())openingGroups[type].push_back(id);
 }
 for(const auto& [type,ids]:openingGroups)if(!productNativeLabelHeight(db,ids,2.8)){
  for(const auto id:ids){std::vector<double> heights;productOpeningTextHeights(*open<AcDbEntity>(id),heights);
   if(heights.size()==1&&std::abs(heights[0]-238.)<.01)continue;
   if(!productNativeLabelHeight(db,{id},2.8))p->issues.push_back({axisHandle(id),"native-opening-label",u8"公开文字高度字段未能改为住宅2.8；保留原生洞口并继续"});
  }
 }
 planTemplateOtherText(job,db,p->issues);
 for(const auto& [handle,change]:sheetJob.chains){
  const auto id=find(db,handle);const auto original=chainGrips(id);const auto values=chainValues(id);const auto n=values.size();
  try{
   require(change.normal==0&&!change.reverseBaseline&&sheetJob.notes.empty(),"template repair accepts bounded text-only proposals");
   require(n&&(original.size()==2*n+6||original.size()==2*n+3),"template repair native grip layout");
   const bool vertical=std::abs(original[0].x-original[1].x)<.01;
   for(const auto& [part,point]:change.textPositions){require(part<n&&point.distanceTo(original[n+3+part])<=1800,"bounded template text repair");moveDimensionGrip(id,static_cast<int>(n+3+part),point);}
   const auto actual=chainGrips(id);require(actual.size()==original.size()&&chainValues(id)==values&&std::equal(original.begin(),original.begin()+n+3,actual.begin(),[](const auto& a,const auto& b){return a.distanceTo(b)<.01;}),"template text repair changed baseline or stations");
   const auto glyphs=productGenericChainText(db,id,vertical);require(glyphs.size()==n,"template repair glyph readback");
   for(const auto& [part,point]:change.textPositions)require(glyphs[part]&&productPlanTextOnRequiredSide(*glyphs[part],vertical,vertical?original[0].x:original[0].y),"template repair crossed the required text side");
  }catch(const std::exception& e){for(const auto& [part,point]:change.textPositions)if(part<n)moveDimensionGrip(id,static_cast<int>(n+3+part),original[n+3+part]);p->issues.push_back({handle,"text-repair",e.what()});}
 }
 p->run=productApplyNativeTemplate(job,db);
 {std::ostringstream state;state<<"{\"objects\":[";bool first=true;for(const auto& item:p->run.objects){if(!first)state<<',';first=false;
  state<<"{\"source\":\""<<item.source<<"\",\"kind\":\""<<item.kind<<"\",\"targets\":"<<item.targets.size()<<",\"issue\":"<<jsonUtf8(bcpproduct::decodeMessage(item.issue))<<'}';}
  state<<"]}";writeNew(p->report/L"template-adaptation-progress.json",state.str());}
 for(const auto& item:p->run.objects)for(const auto& target:item.targets)p->annotationScope.insert(target);
 require(bcpproduct::samePublicGeometry(protectedBefore,planTypographyProtected(db,p->annotationScope)),"template annotations changed unrelated source geometry");
 for(const auto& h:p->retiredFrames)status(editObject<AcDbEntity>(find(db,h))->erase(),"retire verified four-ring Defpoints helper");
 planTemplateLayers(job,db);
 planTemplateTitleClearance(job,db,p->issues);
 planTemplatePlotSettings(job,db);
 p->protectedAfter=planTypographyProtected(db,p->annotationScope);
 status(db.saveAs(p->output.c_str(),false,AcDb::kDHL_CURRENT),"save template-native final drawing");
 acDocManager->executeInApplicationContext(finishPlanTemplate,p.release());
}

void finishPlanOwnedCleanup(void* raw){
 std::unique_ptr<std::pair<fs::path,fs::path>> paths(static_cast<std::pair<fs::path,fs::path>*>(raw));
 try{auto* keep=curDoc();require(keep&&fs::equivalent(paths->first,keep->fileName()),"owned cleanup control identity");
  std::vector<AcApDocument*> others;std::unique_ptr<AcApDocumentIterator> it(acDocManager->newAcApDocumentIterator());
  for(;!it->done();it->step())if(it->document()!=keep)others.push_back(it->document());
  for(auto* document:others)productStatus(acDocManager->appContextCloseDocument(document),"close owned task drawing before native module unload");
  writeNew(paths->second/L"owned-documents-closed.json","{\"completed\":true,\"closed\":"+std::to_string(others.size())+"}");
 }catch(const std::exception& e){writeNew(paths->second/L"owned-documents-closed.json","{\"completed\":false,\"error\":"+jsonUtf8(bcpproduct::decodeMessage(e.what()))+"}");}
}
