struct PlanControlFrame {
 int panel,row;
 std::string handle;
 ProductPlanBounds bounds;
 double unitsPerPaperMM;
};

struct PlanTemplateRequest {
 fs::path source,output,report,resources,sheets;
 ProductTemplateRun run;
 std::vector<ProductTypographyIssue> issues;
 std::set<std::string> annotationScope;
 std::string protectedAfter;
 fs::path frameDatums;
 bool framesOnly{};
 std::vector<PlanControlFrame> frames;
 std::wstring previousPlotters,previousStyles;
 bool plotPathsChanged{};
 ~PlanTemplateRequest(){if(plotPathsChanged){acedSetEnv(L"PrinterConfigDir",previousPlotters.c_str());acedSetEnv(L"PrinterStyleSheetDir",previousStyles.c_str());}}
};

struct PlanTemplateTextSpec {ProductDimensionFont font;double modelHeight{};};
std::optional<PlanTemplateTextSpec> planTemplateTextSpec(const ProductSourceAnnotation& row,const std::string& role){
 if(role=="dimension"||role=="special-direction")return {};
 PlanTemplateTextSpec spec{{L"FY_HZ",L"tssdeng.shx",L"hztxt.shx",.7,0.,0},400.};
 if(role=="opening-label")spec={{L"_TCH_WINDOW",L"simplex.shx",L"gbcbig.shx",.65,0.,0},400.};
 else if(role=="special-title-block")spec={{L"FYTK-HZ",L"tssdeng.shx",L"hztxt.shx",.8,0.,0},350.};
 else if(role=="special-drawing-title")spec={{L"FY_FS",L"simsun.ttc",L"",.7,0.,0},700.};
 else if(role=="special-survey-elevation")spec={{L"FY_DIM",L"simplex.shx",L"hztxt.shx",.7,0.,0},350.};
 else if(role=="special-scale-or-slope")spec.modelHeight=400.;
 else if(role=="special-area-superscript")spec.modelHeight=0.;
 const auto body=productTypographyText(row);

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
   const auto font=productRestoreTypographyFont(db,spec->font,role=="opening-label"?4.:0);
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
   const auto actual=productReadDimensionFont(leaf.style);const bool font=productTypographyFontMatches(leaf.style,spec.font,(role=="opening-label"||role=="native-opening-label")?4.:0);
   const auto height=leaf.height*scale;
   const double expected=(role=="special-area"&&(leaf.text==L"2"||leaf.text==L"²")&&leaves.size()>1)?spec.modelHeight*.5:
    (role=="special-drawing-title"&&std::regex_match(leaf.text,std::wregex(L"1[:：][0-9]+")))?spec.modelHeight*5./7.:spec.modelHeight;
   // Native opening glyphs include Tianzheng's own aspect conversion.
   // The requested font width is the referenced style's xScale.
   const auto width=role=="native-opening-label"?actual.width:leaf.width;
   good=good&&font&&std::abs(width-spec.font.width)<.01&&(expected==0||std::abs(height-expected)<.01);
   AcString family;bool bold{},italic{};Charset charset{};Autodesk::AutoCAD::PAL::FontUtils::FontPitch pitch{};Autodesk::AutoCAD::PAL::FontUtils::FontFamily kind{};
   open<AcDbTextStyleTableRecord>(leaf.style)->font(family,bold,italic,charset,pitch,kind);
   out<<"{\"text\":"<<jsonUtf8(leaf.text)<<",\"style\":"<<jsonUtf8(actual.name)<<",\"font\":"<<jsonUtf8(actual.primary)
    <<",\"fontFamily\":"<<jsonUtf8(family.kACharPtr())
    <<",\"bigFont\":"<<jsonUtf8(actual.big)<<",\"styleWidth\":"<<actual.width<<",\"renderedWidth\":"<<leaf.width<<",\"width\":"<<width<<",\"height\":"<<height<<",\"expectedHeight\":"<<expected<<'}';
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
  record(ascii(h),"native-opening-label",{{L"_TCH_WINDOW",L"simplex.shx",L"gbcbig.shx",.65,0.,0},400.},leaves,1.);
 }
 out<<"]}";writeNew(report/L"template-typography-readback.json",out.str());
}

std::optional<ProductPlanBounds> planControlRectangle(const AcDbPolyline& e){
 if(!e.isClosed()||e.numVerts()!=4||std::abs(e.elevation())>.01||
    !e.normal().isEqualTo(AcGeVector3d::kZAxis))return {};
 AcDbExtents box;if(e.getGeomExtents(box)!=Acad::eOk)return {};
 ProductPlanBounds b{box.minPoint().x,box.minPoint().y,box.maxPoint().x,box.maxPoint().y};
 std::set<int> corners;
 for(unsigned k=0;k<4;++k){
  AcGePoint2d a,z;double bulge{};e.getPointAt(k,a);e.getPointAt((k+1)%4,z);e.getBulgeAt(k,bulge);
  if(std::abs(bulge)>1e-9||a.distanceTo(z)<.01||(std::abs(a.x-z.x)>.01&&std::abs(a.y-z.y)>.01))return {};
  corners.insert((std::abs(a.x-b.xmax)<.01?2:0)+(std::abs(a.y-b.ymax)<.01?1:0));
 }
 if(corners.size()!=4)return {};
 return b;
}
ProductPlanBounds planFrameOffset(const ProductPlanBounds& b,double n){return {b.xmin-n,b.ymin-n,b.xmax+n,b.ymax+n};}
bool planFrameCoincides(const ProductPlanBounds& a,const ProductPlanBounds& b){
 return std::abs(a.xmin-b.xmin)<.01&&std::abs(a.ymin-b.ymin)<.01&&std::abs(a.xmax-b.xmax)<.01&&std::abs(a.ymax-b.ymax)<.01;
}
std::vector<PlanControlFrame> planTemplateFrames(ProductJob& job,AcDbDatabase& db,const fs::path& file,const fs::path& source){
 std::map<int,ProductPlanBounds> confirmed;
 if(!file.empty()){
  std::ifstream in(file);std::string tag,hash;require(bool(in>>tag>>hash)&&tag=="PLANFRAMES1"&&hash==sha(source),"frame datum source identity");
  while(in>>tag&&tag=="PANEL"){
   ProductPlanBounds b;int panel;require(bool(in>>panel>>b.xmin>>b.ymin>>b.xmax>>b.ymax)&&job.planSheetLayouts.count(panel)&&
    b.xmin<b.xmax&&b.ymin<b.ymax&&confirmed.emplace(panel,b).second,"invalid or duplicate frame datum");
  }
  require(tag=="END"&&!(in>>hash)&&confirmed.size()==job.planSheetLayouts.size(),"incomplete frame datum file");
 }
 std::map<std::string,ProductPlanBounds> boxes;
 for(const auto& [h,type]:modelTypes(db))if(type==L"AcDbPolyline"){
  const auto e=open<AcDbPolyline>(find(db,ascii(h)));if(_wcsicmp(symbolName(e->layerId()).c_str(),L"Defpoints"))continue;
  if(const auto b=planControlRectangle(*e))boxes.emplace(ascii(h),*b);
 }
 std::vector<PlanControlFrame> result;std::set<std::string> assigned;
 for(const auto& [panel,sheet]:job.planSheetLayouts){
  const auto vp=open<AcDbViewport>(find(db,sheet.viewport));const double scale=vp->customScale();
  require(scale>0&&!vp->isPerspectiveOn()&&std::abs(vp->twistAngle())<1e-8&&vp->viewDirection().isEqualTo(AcGeVector3d::kZAxis),"plan frame viewport transform");
  const auto centre=vp->viewTarget()+AcGeVector3d(vp->viewCenter().x,vp->viewCenter().y,0);
  const auto inView=[&](const ProductPlanBounds& b){return std::abs((b.xmin+b.xmax)*.5-centre.x)<vp->width()/(2*scale)&&std::abs((b.ymin+b.ymax)*.5-centre.y)<vp->height()/(2*scale);};
  auto handles=[&](const ProductPlanBounds& b){std::vector<std::string> ids;for(const auto& [h,box]:boxes)if(planFrameCoincides(b,box))ids.push_back(h);return ids;};
  ProductPlanBounds datum;
  if(confirmed.count(panel))datum=confirmed.at(panel);
  else{
   std::vector<ProductPlanBounds> groups;
   for(const auto& [h,inner]:boxes)if(inView(inner)){
    bool complete=true;for(int row=1;row<4;++row)if(handles(planFrameOffset(inner,row*8./scale)).size()!=1)complete=false;
    if(complete)groups.push_back(planFrameOffset(inner,-18./scale));
   }
   require(groups.size()==1,"four unique plan frames or confirmed per-side datums required");datum=groups.front();
  }
  require(inView(datum),"frame datum outside assigned plan viewport");
  for(int row=0;row<4;++row){
   auto b=planFrameOffset(datum,(18.+8.*row)/scale);
   const auto existing=handles(b);require(existing.size()<=1,"duplicate Defpoints rectangle at confirmed station");
   std::string handle;
   if(existing.empty()){
    auto poly=std::make_unique<AcDbPolyline>(4);poly->setDatabaseDefaults(&db);
    const auto corners=productPlanFrameCorners(b,0.);
    for(unsigned k=0;k<4;++k)status(poly->addVertexAt(k,corners[k]),"add control-frame corner");
    poly->setClosed(true);
    handle=axisHandle(productAppendPlanDetail(db,std::move(poly)));boxes.emplace(handle,b);
   }else handle=existing.front();
   require(assigned.insert(handle).second,"one frame belongs to two plan sheets");
   result.push_back({panel,row,handle,b,1./scale});
  }
 }
 // Reuse the production dashed pattern and LTSCALE compensation, per sheet scale.
 const auto layer=productPlanLayer(db,L"Defpoints",8,false);
 {auto l=editObject<AcDbLayerTableRecord>(layer);l->setIsOff(false);status(l->setIsFrozen(false),"thaw frame layer");}
 for(const auto& [panel,sheet]:job.planSheetLayouts){
  ProductPlanLayoutConfig config;
  for(const auto& f:result)if(f.panel==panel){
   auto e=editObject<AcDbPolyline>(find(db,f.handle));status(e->setLayer(layer),"frame layer");status(e->setColorIndex(256),"frame ByLayer gray");
   status(e->setVisibility(AcDb::kVisible),"frame visible");e->setPlinegen(Adesk::kTrue);
   config.scale=f.unitsPerPaperMM;config.frames.push_back({f.handle,panel,0,f.row});
  }
  productPlanFrameAppearance(db,config);
 }
 return result;
}

void planVerifyControlFrames(AcDbDatabase& db,const PlanTemplateRequest& p){
 std::ostringstream out;out<<std::setprecision(17)<<"{\"outputSHA256\":\""<<sha(p.output)<<"\",\"frames\":[";bool first=true;
 for(const auto& f:p.frames){
  const auto e=open<AcDbPolyline>(find(db,f.handle));const auto l=open<AcDbLayerTableRecord>(e->layerId());
  const auto lt=open<AcDbLinetypeTableRecord>(e->linetypeId());const auto b=planControlRectangle(*e);
  require(b&&planFrameCoincides(*b,f.bounds)&&e->ownerId()==modelId(db)&&e->visibility()==AcDb::kVisible&&
   symbolName(e->layerId())==L"Defpoints"&&!l->isOff()&&!l->isFrozen()&&!l->isPlottable()&&
   l->color().colorIndex()==8&&e->color().isByLayer()&&lt->numDashes()==2&&lt->dashLengthAt(0)>0&&lt->dashLengthAt(1)<0&&
   std::abs(e->linetypeScale()*db.ltscale()-f.unitsPerPaperMM)<.001,"reopened gray visible nonplot control frame");
  if(!first)out<<',';first=false;out<<"{\"panel\":"<<f.panel<<",\"row\":"<<f.row+1<<",\"handle\":\""<<f.handle
    <<"\",\"bounds\":["<<b->xmin<<','<<b->ymin<<','<<b->xmax<<','<<b->ymax<<"]}";
 }
 out<<"]}";writeNew(p.report/L"defpoints-frame-readback.json",out.str());
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
  planVerifyControlFrames(saved,p);
  if(!p.framesOnly){
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
  }
  bool first=true;
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
 if(p->framesOnly){
  p->frames=planTemplateFrames(job,db,p->frameDatums,p->source);
  for(const auto& f:p->frames)p->annotationScope.insert(f.handle);
  AcDbDatabase original(false,true);readDrawing(original,p->source);
  const auto baseline=planTypographyProtected(original,p->annotationScope);
  p->protectedAfter=planTypographyProtected(db,p->annotationScope);
  require(bcpproduct::samePublicGeometry(baseline,p->protectedAfter),"frame repair changed other model objects");
  status(db.saveAs(p->output.c_str(),false,AcDb::kDHL_CURRENT),"save repaired plan frames");
  acDocManager->executeInApplicationContext(finishPlanTemplate,p.release());return;
 }
 AcDbDatabase reference(false,true);readDrawing(reference,p->resources/L"reference.dwg");
 for(const auto& row:productTypographyInventory(db))p->annotationScope.insert(row.root);
 for(const auto& [h,type]:modelTypes(db))if(type==L"TDbAxisLabelSet"||type==L"TDbOpening"||type==L"TDbGroupOpening")p->annotationScope.insert(ascii(h));
 p->frames=planTemplateFrames(job,db,p->frameDatums,p->source);
 for(const auto& f:p->frames)p->annotationScope.insert(f.handle);
 const auto protectedBefore=planTypographyProtected(db,p->annotationScope);
 planTemplateIsolateFrames(db);
 p->issues=productNormalizeWholePlanTypography(job,db,reference);
 std::map<std::wstring,std::vector<AcDbObjectId>> openingGroups;
 for(const auto& [h,type]:modelTypes(db))if(type==L"TDbOpening"||type==L"TDbGroupOpening"){
  const auto id=find(db,ascii(h));const auto e=open<AcDbEntity>(id);if(!productPlanDimensionVisible(*e))continue;
  std::vector<ProductRoomLeaf> leaves;productRoomLeaves(*e,leaves);if(!leaves.empty())openingGroups[type].push_back(id);
 }
 for(const auto& [type,ids]:openingGroups)if(!productNativeLabelHeight(db,ids,400./85.)){
  for(const auto id:ids){std::vector<double> heights;productOpeningTextHeights(*open<AcDbEntity>(id),heights);
   if(heights.size()==1&&std::abs(heights[0]-400.)<.01)continue;
   if(!productNativeLabelHeight(db,{id},400./85.))p->issues.push_back({axisHandle(id),"native-opening-label",u8"公开文字高度字段未能读回有效字高400（纸面4.0）；保留原生洞口并继续"});
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
 const auto finalFonts=productRestorePlanTypography(db,reference,true);
 productSynchronizePlanDimensionTable(job,db,reference,finalFonts.dim);
 {std::ostringstream state;state<<"{\"objects\":[";bool first=true;for(const auto& item:p->run.objects){if(!first)state<<',';first=false;
  state<<"{\"source\":\""<<item.source<<"\",\"kind\":\""<<item.kind<<"\",\"targets\":"<<item.targets.size()<<",\"issue\":"<<jsonUtf8(bcpproduct::decodeMessage(item.issue))<<'}';}
  state<<"]}";writeNew(p->report/L"template-adaptation-progress.json",state.str());}
 for(const auto& item:p->run.objects)for(const auto& target:item.targets)p->annotationScope.insert(target);
 require(bcpproduct::samePublicGeometry(protectedBefore,planTypographyProtected(db,p->annotationScope)),"template annotations changed unrelated source geometry");
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
