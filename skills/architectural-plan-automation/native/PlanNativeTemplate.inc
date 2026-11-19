#include "../../../插件/src/ProductFinalPlanOmissions.inc"
#include "../../../插件/src/ProductPlanFontDependencies.inc"
#include "../../../插件/src/ProductPlanRiskGlyphs.inc"

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
 ProductFinalOmissionContext omissionContext;
 ProductFontDependencyCleanup fontCleanup;
 std::vector<ProductPlanUnitGlyphRepair> unitGlyphRepairs;
 std::vector<ProductPlanTableGroup> tables;
 std::vector<std::size_t> tableSourcePanels;
 std::set<std::string> annotationScope;
 std::string protectedAfter;
 std::vector<ProductFurnitureItem> layeredFurniture;
 fs::path frameDatums;
 bool framesOnly{};
 std::vector<PlanControlFrame> frames;
 std::wstring previousPlotters,previousStyles;
 bool plotPathsChanged{};
 ~PlanTemplateRequest(){if(plotPathsChanged){acedSetEnv(L"PrinterConfigDir",previousPlotters.c_str());acedSetEnv(L"PrinterStyleSheetDir",previousStyles.c_str());}}
};

struct PlanTemplateTextSpec {ProductDimensionFont font;double modelHeight{};};
std::optional<PlanTemplateTextSpec> planTemplateTextSpec(const ProductJob& job,const ProductSourceAnnotation& row,const std::string& role){
 if(role=="dimension"||role=="special-direction")return {};
 const auto policy=productCurrentTypographyPolicy(job);
 PlanTemplateTextSpec spec{productCurrentFont(policy.at("FY_HZ")),productTypographyModelHeight(job,"FY_HZ")};
 if(role=="opening-label")spec={productCurrentFont(policy.at("_TCH_WINDOW")),productTypographyModelHeight(job,"_TCH_WINDOW")};
 else if(role=="special-title-block")spec={{L"FYTK-HZ",L"tssdeng.shx",L"hztxt.shx",.8,0.,0},350.};
 else if(role=="special-drawing-title")spec={{L"FY_FS",L"simsun.ttc",L"",.7,0.,0},700.};
 else if(role=="special-survey-elevation")spec={{L"FY_DIM",L"simplex.shx",L"hztxt.shx",.7,0.,0},350.};
 else if(role=="special-scale-or-slope")spec.modelHeight=400.;
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
void planTemplateIsolateFrames(AcDbDatabase& db,const std::set<std::string>& furniture){
 std::set<std::string> roots;
 for(const auto& row:productTypographyInventory(db))if(row.visible&&!furniture.count(row.handle)&&!furniture.count(row.root)&&productTypographyRole(db,row)=="special-title-block")roots.insert(row.root);
 for(const auto& root:roots){const auto id=find(db,root);auto block=open<AcDbBlockReference>(id);const auto original=block->blockTableRecord();
  std::vector<ProductRoomLeaf> before;productRoomLeaves(*block,before);block.reset();std::map<AcDbObjectId,AcDbObjectId> copied;
  const auto replacement=planTemplateCopyFrameDefinition(db,original,copied);status(editObject<AcDbBlockReference>(id)->setBlockTableRecord(replacement),"retain title instance placement");
  std::vector<ProductRoomLeaf> after;productRoomLeaves(*open<AcDbEntity>(id),after);require(productPlanTextSameLeaves(before,after),"frame isolation altered original glyphs");
 }
}

void planTemplateOtherText(ProductJob& job,AcDbDatabase& db,std::vector<ProductTypographyIssue>& issues){
 std::set<std::string> done;
 for(const auto& row:productTypographyInventory(db))if(row.visible&&!job.protectedFurnitureMembers.count(row.handle)&&done.insert(row.handle).second){
  const auto role=productTypographyRole(db,row);const auto spec=planTemplateTextSpec(job,row,role);
  if(!spec||role=="general"||role=="opening-label"||role=="linked-note")continue;
  try{
   const auto font=productRestoreTypographyFont(db,spec->font,role=="opening-label"?productCurrentTypographyPolicy(job).at("_TCH_WINDOW").fixedHeight:0);
   auto height=spec->modelHeight*productTypographySpaceFactor(db,row)/productTypographyInstanceScale(db,row);
   if(height==0){std::vector<ProductRoomLeaf> leaves;productRoomLeaves(*open<AcDbEntity>(find(db,row.handle)),leaves);if(!leaves.empty())height=leaves.front().height;}
   auto editable=row;if(role=="special-area"||row.type==L"TDbDrawingName")editable.kind="template-mixed-text";
   if(height>0){const auto issue=productTypographyRestyleText(db,editable,font,height);if(!issue.empty())issues.push_back({row.path,role,issue});}
  }catch(const std::exception& e){issues.push_back({row.path,role,e.what()});}
 }
}

// The body is still FY_HZ/4.0. A true squared-metre exponent is recognized
// through the independent glyph geometry, not merely a special-area role or 2.
bool planTemplatePairedAreaSuperscript(const std::vector<ProductRoomLeaf>& leaves,std::size_t index){
 if(index==0||index>=leaves.size()||leaves[index].text!=L"2")return false;
 std::wstring contents;for(const auto& leaf:leaves)contents+=leaf.text;
 if(contents.find(L"面积")==std::wstring::npos)return false;
 const auto& body=leaves[index-1];const auto& exponent=leaves[index];
 if(!std::regex_match(body.text,std::wregex(L"[0-9]+(?:\\.[0-9]+)?[mM]"))||!body.bounds||!exponent.bounds||
    body.height<=0||std::abs(exponent.height-body.height*.5)>.001||body.style!=exponent.style||
    std::abs(std::remainder(exponent.rotation-body.rotation,2*3.141592653589793))>1e-8)return false;
 const AcGeVector3d along(std::cos(body.rotation),std::sin(body.rotation),0),up(-along.y,along.x,0);
 const auto offset=exponent.anchor-body.anchor;const double rise=offset.dotProduct(up);
 if(offset.dotProduct(along)<=0||rise<body.height*.1||rise>body.height*1.1)return false;
 const auto project=[&](const AcDbExtents& box){
  std::pair<double,double> range{std::numeric_limits<double>::max(),-std::numeric_limits<double>::max()};
  for(double x:{box.minPoint().x,box.maxPoint().x})for(double y:{box.minPoint().y,box.maxPoint().y}){
   const double value=AcGeVector3d(x-body.anchor.x,y-body.anchor.y,0).dotProduct(along);
   range.first=std::min(range.first,value);range.second=std::max(range.second,value);
  }return range;
 };
 const auto bodyRange=project(*body.bounds),exponentRange=project(*exponent.bounds);
 const double gap=exponentRange.first-bodyRange.second;
 return gap>=-body.height*.1&&gap<=body.height*.6;
}

void planTemplateAuditTitlePaperBounds(ProductJob& job,AcDbDatabase& db,const fs::path& drawing,
 const fs::path& report,const std::vector<ProductSourceAnnotation>& rows,std::vector<ProductTypographyIssue>& issues){
 struct Paper {int panel{};std::wstring name;AcDbObjectId owner;double x{},y{},right{},top{};std::size_t count{},empty{},unread{};std::string reason,coordinates{"null"};};
 std::vector<Paper> papers;bool coverage=true;
 for(const auto& [panel,sheet]:job.planSheetLayouts){Paper paper;paper.panel=panel;paper.name=sheet.name;
  try{const auto id=acdbHostApplicationServices()->layoutManager()->findLayoutNamed(sheet.name.c_str(),&db);
   const auto layout=open<AcDbLayout>(id);paper.owner=layout->getBlockTableRecordId();paper.coordinates=productPlanPaperSettingsJson(*layout);double width{},height{},n{},d{},activeScale{};
   status(layout->getPlotPaperSize(width,height),"read saved title-paper size");status(layout->getCustomPrintScale(n,d),"read saved title-paper scale");
   if(layout->useStandardScale())status(layout->getStdScale(activeScale),"read active standard paper scale");else if(d>0)activeScale=n/d;
   require(layout->plotPaperUnits()==AcDbPlotSettings::kMillimeters&&std::abs(activeScale-1.)<1e-8,"title-paper coordinates need actual millimetre 1:1 layout");
   double marginLeft{},marginBottom{},marginRight{},marginTop{},originX{},originY{};
   status(layout->getPlotPaperMargins(marginLeft,marginBottom,marginRight,marginTop),"read saved paper margins");
   status(layout->getPlotOrigin(originX,originY),"read saved paper origin");
   require(std::max({std::abs(marginLeft),std::abs(marginBottom),std::abs(marginRight),std::abs(marginTop)})<.001,
    "nonzero printable margins need their own physical-paper transform");
   if(layout->plotRotation()==AcDbPlotSettings::k90degrees||layout->plotRotation()==AcDbPlotSettings::k270degrees)std::swap(width,height);
   require(width>0&&height>0,"invalid actual paper bounds");paper.right=width;paper.top=height;
   if(layout->plotType()==AcDbPlotSettings::kWindow){
    status(layout->getPlotWindowArea(paper.x,paper.y,paper.right,paper.top),"read saved paper plot window");
    require(std::abs(paper.right-paper.x-width)<.01&&std::abs(paper.top-paper.y-height)<.01,"paper plot window is not the full physical sheet");
    require(layout->plotCentered()||(std::abs(originX)<.001&&std::abs(originY)<.001),"unresolved noncentered paper-window origin");
   }else require(layout->plotType()==AcDbPlotSettings::kLayout&&std::abs(originX)<.001&&std::abs(originY)<.001,"unresolved paper-origin transform");
  }catch(const std::exception& e){paper.reason=e.what();coverage=false;}papers.push_back(std::move(paper));
 }
 std::ostringstream out;out<<std::setprecision(12)<<"{\"contract\":\"plan.title-paper-bounds.v1\",\"outputSHA256\":\""<<sha(drawing)
  <<"\",\"savedReopened\":true,\"scope\":\"confirmed paper-space title-block text-leaf entities; physical paper edge only; model tables and cell fitting are separate\",\"objects\":[";bool first=true;
 for(const auto& row:rows)if(row.visible&&productTypographyRole(db,row)=="special-title-block"){
  const auto owner=open<AcDbEntity>(find(db,row.root))->ownerId();
  const auto selected=std::find_if(papers.begin(),papers.end(),[&](const auto& paper){return paper.owner==owner;});
  if(selected==papers.end())continue;auto& paper=*selected;++paper.count;std::string reason=paper.reason;
  const auto text=productTypographyText(row);const bool noInk=text.empty();if(noInk)++paper.empty;
  std::array<double,4> excess{};
  if(noInk)reason.clear();
  else if(!row.bounds){reason="nonempty title-block text leaf has no reliable actual extent";++paper.unread;coverage=false;}
  else if(reason.empty()){const auto& box=*row.bounds;excess={std::max(0.,paper.x-box.minPoint().x),std::max(0.,box.maxPoint().x-paper.right),
     std::max(0.,paper.y-box.minPoint().y),std::max(0.,box.maxPoint().y-paper.top)};
   if(*std::max_element(excess.begin(),excess.end())>.01){std::ostringstream why;why<<u8"图签文字超出实际纸面边界，布局="<<productUtf8(paper.name)
     <<", left/right/bottom/top(mm)="<<excess[0]<<'/'<<excess[1]<<'/'<<excess[2]<<'/'<<excess[3];reason=why.str();}}
  // Shared unknown coordinates/extent coverage belong to one layout item.
  // Keep per-object items only for a measured, real paper-edge overrun.
  if(!noInk&&paper.reason.empty()&&row.bounds&&!reason.empty())issues.push_back({row.path,"title-paper-bounds",reason});
  if(!first)out<<',';first=false;out<<"{\"object\":\""<<row.path<<"\",\"panel\":"<<paper.panel<<",\"layout\":"<<jsonUtf8(paper.name)
   <<",\"text\":"<<jsonUtf8(text)<<",\"nativeType\":"<<jsonUtf8(row.type)<<",\"noInk\":"<<(noInk?"true":"false")
   <<",\"applicable\":"<<(noInk?"false":"true")
   <<",\"paperBounds\":["<<paper.x<<','<<paper.y<<','<<paper.right<<','<<paper.top<<"],\"bounds\":";
  if(row.bounds)out<<'['<<row.bounds->minPoint().x<<','<<row.bounds->minPoint().y<<','<<row.bounds->maxPoint().x<<','<<row.bounds->maxPoint().y<<']';else out<<"null";
  out<<",\"overrunMM\":["<<excess[0]<<','<<excess[1]<<','<<excess[2]<<','<<excess[3]<<"],\"verified\":"<<(noInk?"null":reason.empty()?"true":"false")
   <<",\"reason\":"<<jsonUtf8(bcpproduct::decodeMessage(reason))<<'}';
 }
 out<<"],\"layouts\":[";first=true;
 for(const auto& paper:papers){if(!first)out<<',';first=false;if(!paper.count)coverage=false;
  std::string reason=paper.reason;if(reason.empty()&&paper.unread)reason="nonempty title-block leaves have unread extents: "+std::to_string(paper.unread);
  out<<"{\"panel\":"<<paper.panel<<",\"layout\":"<<jsonUtf8(paper.name)<<",\"textLeafCount\":"<<paper.count
   <<",\"inkLeafCount\":"<<paper.count-paper.empty<<",\"emptyLeafCount\":"<<paper.empty<<",\"unreadExtentCount\":"<<paper.unread
   <<",\"paperCoordinates\":"<<paper.coordinates<<",\"reason\":"<<jsonUtf8(bcpproduct::decodeMessage(reason))<<'}';
  if(!paper.count||!reason.empty())issues.push_back({"layout:"+std::to_string(paper.panel),"title-paper-coverage",
   reason.empty()?u8"纸空间未识别到可逐叶复核的图签文字；模型表格验收不能代替此覆盖":reason});
 }
 out<<"],\"expectedCoverageKnown\":"<<(!papers.empty()&&coverage?"true":"false")<<'}';writeNew(report/L"plan-title-paper-readback.json",out.str());
}

void planTemplateAuditText(ProductJob& job,AcDbDatabase& db,const fs::path& drawing,const fs::path& report,std::vector<ProductTypographyIssue>& issues){
 std::ostringstream out;out<<std::setprecision(12)<<"{\"outputSHA256\":\""<<sha(drawing)<<"\",\"savedReopened\":true,\"policySHA256\":\""<<productCurrentTypographyPolicy(job).sourceHash<<"\",\"objects\":[";bool first=true;
 const auto record=[&](const std::string& object,const std::string& role,const PlanTemplateTextSpec& spec,std::vector<ProductRoomLeaf> leaves,double scale,double paperUnits,const std::wstring& nativeType){
  bool good=!leaves.empty();std::string reason;
  if(!first)out<<',';first=false;out<<"{\"object\":\""<<object<<"\",\"role\":\""<<role<<"\",\"nativeType\":"<<jsonUtf8(nativeType)<<",\"unitsPerPaperMM\":"<<paperUnits<<",\"glyphs\":[";
  for(std::size_t k=0;k<leaves.size();++k){const auto& leaf=leaves[k];if(k)out<<',';
   const auto actual=productReadDimensionFont(leaf.style);const bool font=productTypographyFontMatches(leaf.style,spec.font,(role=="opening-label"||role=="native-opening-label")?productCurrentTypographyPolicy(job).at("_TCH_WINDOW").fixedHeight:0);
   const auto height=leaf.height*scale;
   const bool squared=role=="special-area"&&nativeType==L"TDbText"&&object.find('/')==std::string::npos&&std::abs(scale-1)<1e-9&&planTemplatePairedAreaSuperscript(leaves,k);
   const double expected=squared?spec.modelHeight*.5:
    (role=="special-drawing-title"&&std::regex_match(leaf.text,std::wregex(L"1[:：][0-9]+")))?spec.modelHeight*5./7.:spec.modelHeight;
   // Native opening glyphs include Tianzheng's own aspect conversion.
   // The requested font width is the referenced style's xScale.
   const auto width=role=="native-opening-label"?actual.width:leaf.width;
   good=good&&font&&std::abs(width-spec.font.width)<.01&&(expected==0||std::abs(height-expected)<.01);
   AcString family;bool bold{},italic{};Charset charset{};Autodesk::AutoCAD::PAL::FontUtils::FontPitch pitch{};Autodesk::AutoCAD::PAL::FontUtils::FontFamily kind{};
   open<AcDbTextStyleTableRecord>(leaf.style)->font(family,bold,italic,charset,pitch,kind);
   out<<"{\"text\":"<<jsonUtf8(leaf.text)<<",\"style\":"<<jsonUtf8(actual.name)<<",\"font\":"<<jsonUtf8(actual.primary)
    <<",\"fontFamily\":"<<jsonUtf8(family.kACharPtr())
    <<",\"bigFont\":"<<jsonUtf8(actual.big)<<",\"styleWidth\":"<<actual.width<<",\"renderedWidth\":"<<leaf.width<<",\"width\":"<<width<<",\"height\":"<<height<<",\"expectedHeight\":"<<expected
    <<",\"anchor\":["<<leaf.anchor.x<<','<<leaf.anchor.y<<','<<leaf.anchor.z<<"],\"rotation\":"<<leaf.rotation<<",\"bounds\":";
   if(leaf.bounds)out<<'['<<leaf.bounds->minPoint().x<<','<<leaf.bounds->minPoint().y<<','<<leaf.bounds->minPoint().z<<','
     <<leaf.bounds->maxPoint().x<<','<<leaf.bounds->maxPoint().y<<','<<leaf.bounds->maxPoint().z<<']';else out<<"null";
   const auto definition=open<AcDbTextStyleTableRecord>(leaf.style);auto* annotation=AcDbAnnotativeObjectPE::cast(definition->queryX(AcDbAnnotativeObjectPE::desc()));
   out<<",\"fixedHeight\":"<<definition->textSize()<<",\"oblique\":"<<definition->obliquingAngle()<<",\"flags\":"<<static_cast<unsigned>(definition->flagBits())
      <<",\"annotative\":"<<(annotation&&annotation->annotative(definition.get())?"true":"false")<<",\"vertical\":"<<(definition->isVertical()?"true":"false")<<'}';
  }
  if(!good){reason=role=="linked-note"?
   u8"关联引注内部字体或字高不符；当前没有已验证的独立公开改字接口，保留原文、引线、锚点及关联，未计为通过":
   u8"模板用途字体定义、实际字宽或有效字高不符；保留正文与构件关联，未计为通过";issues.push_back({object,role,reason});}
  out<<"],\"verified\":"<<(good?"true":"false")<<",\"reason\":"<<jsonUtf8(bcpproduct::decodeMessage(reason))<<'}';
 };
 ProductJob protectedItems;productProtectFurnitureMembers(protectedItems,db);
 const auto textRows=productTypographyInventory(db);
 for(const auto& row:textRows)if(row.visible&&!protectedItems.protectedFurnitureMembers.count(row.handle)){
  const auto role=productTypographyRole(db,row);auto spec=planTemplateTextSpec(job,row,role);if(!spec)continue;
  try{std::vector<ProductRoomLeaf> leaves;productRoomLeaves(*open<AcDbEntity>(find(db,row.handle)),leaves,0,true);
   spec->modelHeight*=productTypographySpaceFactor(db,row);record(row.path,role,*spec,leaves,productTypographyInstanceScale(db,row),job.unifiedScale/job.unifiedUnitMM*productTypographySpaceFactor(db,row),row.type);
  }catch(const std::exception& e){issues.push_back({row.path,role,e.what()});}
 }
 for(const auto& [h,type]:modelTypes(db))if(type==L"TDbOpening"||type==L"TDbGroupOpening"){
  const auto entity=open<AcDbEntity>(find(db,ascii(h)));if(!productPlanDimensionVisible(*entity))continue;
  std::vector<ProductRoomLeaf> leaves;productRoomLeaves(*entity,leaves,0,true);if(leaves.empty())continue;
  record(ascii(h),"native-opening-label",{productTypographyOpeningFont(job),productTypographyModelHeight(job,"_TCH_WINDOW")},leaves,1.,job.unifiedScale/job.unifiedUnitMM,type);
 }
 out<<"]}";writeNew(report/L"template-typography-readback.json",out.str());
 planTemplateAuditTitlePaperBounds(job,db,drawing,report,textRows,issues);
}

void planRequireProtectedGeometry(const fs::path& report,const std::string& before,const std::string& after,const char* reason){
 if(bcpproduct::samePublicGeometry(before,after))return;
 writeNew(report/L"protected-before.json",before);
 writeNew(report/L"protected-after.json",after);
 throw std::runtime_error(reason);
}

std::vector<ProductFurnitureItem> planLayeredFurniture(AcDbDatabase& db){
 std::vector<ProductFurnitureItem> items;
 for(const auto& [h,type]:modelTypes(db)){
  const auto handle=ascii(h);const auto id=find(db,handle);
  const auto entity=open<AcDbEntity>(id);const auto layer=symbolName(entity->layerId());
  if(layer!=L"A-家具"&&layer!=L"A-洁具、厨具")continue;
  ProductFurnitureItem item;item.output=handle;item.layer=layer;
  std::set<AcDbObjectId> stack;std::ostringstream signature;signature<<std::fixed<<std::setprecision(6);bool kitchen=false;
  require(productFurnitureStructure(id,true,stack,signature,item.members,kitchen),"cannot record original layered furniture");
  item.signature=signature.str();items.push_back(std::move(item));
 }
 return items;
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
// A roof with no control frames can supply its own datum when its actual
// closed eave and the two measured TCH axis rows agree on every side.
std::vector<ProductPlanBounds> planRoofFrameDatums(ProductJob& job,AcDbDatabase& db,
 const ProductPlanBounds& window,double scale,int panel){
 const auto policy=productLoadPlanPaperPolicy(job,1./scale,1.);
 const auto spacing=bcpproduct::planAnnotationLayout(1./scale,1.,policy);
 std::vector<ProductPlanBounds> outlines;
 std::vector<std::pair<ProductJob::SourceDimension,double>> chains;
 for(const auto& [h,type]:modelTypes(db)){
  if(type!=L"AcDbPolyline"&&type!=L"TDbDimension2")continue;
  const auto key=ascii(h);const auto entity=open<AcDbEntity>(find(db,key));
  if(!productPlanDimensionVisible(*entity))continue;
  AcDbExtents b;if(entity->getGeomExtents(b)!=Acad::eOk)continue;
  const auto centre=b.minPoint()+(b.maxPoint()-b.minPoint())*.5;
  if(centre.x<window.xmin||centre.x>window.xmax||centre.y<window.ymin||centre.y>window.ymax)continue;
  if(type==L"TDbDimension2"){
   auto chain=productReadSourceDimension(db,key,.02);
   if(!chain.readable||chain.points.size()<2)continue;
   TemplateParts parts(entity.get());double gap=0;
   if(entity->explode(parts.values)==Acad::eOk&&!parts.values.isEmpty()){
    const auto* part=AcDbAlignedDimension::cast(static_cast<AcRxObject*>(parts.values[0]));
    if(part)gap=part->dimexo()*part->dimscale();
   }
   chains.emplace_back(std::move(chain),gap);
  }else{
   const auto layer=symbolName(entity->layerId());
   if(!AcDbPolyline::cast(entity.get())->isClosed()||
      (_wcsicmp(layer.c_str(),L"ROOF")&&layer.find(L"檐口")==std::wstring::npos))continue;
   outlines.push_back({b.minPoint().x,b.minPoint().y,b.maxPoint().x,b.maxPoint().y});
  }
 }
 std::vector<ProductPlanBounds> matches;
 std::ostringstream audit;audit<<std::setprecision(17)<<"{\"panel\":"<<panel<<",\"chains\":"<<chains.size()<<",\"outlines\":[";
 bool firstOutline=true;
 for(const auto& box:outlines){
  if(!firstOutline)audit<<',';firstOutline=false;audit<<"{\"bounds\":["<<box.xmin<<','<<box.ymin<<','<<box.xmax<<','<<box.ymax<<"],\"pairs\":[";
  bool allSides=true;
  for(int side=0;side<4;++side){
   const int sign=side%2?1:-1;const double datum=productPlanDatum(box,side);
   std::array<std::vector<std::pair<double,double>>,2> endpoints;
   const auto along=[&](const auto& p){return side<2?p.y:p.x;};
   const auto normal=[&](const auto& p){return side<2?p.x:p.y;};
   for(const auto& [chain,gap]:chains){
    const double offset=sign*(normal(chain.baseline)-datum);
    const auto& a=chain.points.front();const auto& b=chain.points.back();
    if(std::abs(normal(a)-normal(b))>.02||std::abs(along(a)-along(b))<1.)continue;
    if(std::abs(std::accumulate(chain.values.begin(),chain.values.end(),0.)-std::abs(along(a)-along(b)))>.02)continue;
    const bool eaveWitness=gap>=0&&std::abs(sign*(normal(a)-datum)+gap-spacing.start)<.02&&offset>0;
    if(chain.values.size()==1&&(std::abs(offset-spacing.total)<.02||eaveWitness))endpoints[0].emplace_back(std::min(along(a),along(b)),std::max(along(a),along(b)));
    if(chain.values.size()>1&&(std::abs(offset-spacing.second)<.02||eaveWitness))endpoints[1].emplace_back(std::min(along(a),along(b)),std::max(along(a),along(b)));
   }
   if(side)audit<<',';audit<<'['<<endpoints[0].size()<<','<<endpoints[1].size()<<']';
   allSides=allSides&&endpoints[0].size()==1&&endpoints[1].size()==1&&
    std::abs(endpoints[0][0].first-endpoints[1][0].first)<.02&&std::abs(endpoints[0][0].second-endpoints[1][0].second)<.02;
  }
  audit<<"]}";if(allSides)matches.push_back(box);
 }
 audit<<"]}";writeNew(job.root/(L"roof-frame-datums-"+std::to_wstring(panel)+L".json"),audit.str());
 return matches;
}


std::vector<PlanControlFrame> planTemplateFrames(ProductJob& job,AcDbDatabase& db,const fs::path& file,const fs::path& source){
 std::map<int,ProductPlanBounds> confirmed;
 if(!file.empty()){
  std::ifstream in(file);std::string tag,hash;require(bool(in>>tag>>hash)&&tag=="PLANFRAMES1"&&hash==sha(source),"frame datum source identity");
  while(in>>tag&&tag=="PANEL"){
   ProductPlanBounds b;int panel;require(bool(in>>panel>>b.xmin>>b.ymin>>b.xmax>>b.ymax)&&job.planSheetLayouts.count(panel)&&
    b.xmin<b.xmax&&b.ymin<b.ymax&&confirmed.emplace(panel,b).second,"invalid or duplicate frame datum");
  }
  require(tag=="END"&&!(in>>hash)&&!confirmed.empty(),"incomplete frame datum file");
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
   if(groups.empty()){
    const ProductPlanBounds window{centre.x-vp->width()/(2*scale),centre.y-vp->height()/(2*scale),
                                   centre.x+vp->width()/(2*scale),centre.y+vp->height()/(2*scale)};
    groups=planRoofFrameDatums(job,db,window,scale,panel);
   }
   require(groups.size()==1,"four unique plan frames or measured per-side datums required");datum=groups.front();
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
 {
  auto l=editObject<AcDbLayerTableRecord>(layer);
  if(l->isOff())l->setIsOff(false);
  if(l->isFrozen())status(l->setIsFrozen(false),"thaw frozen frame layer");
 }
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
  if(old==L"A-家具"||old==L"A-洁具、厨具")continue;
  if(type==L"TDbWall")target=L"A-WALL";
  else if(type==L"TDbColumn")target=L"A-COLUMN";
  else if(type==L"TDbOpening"||type==L"TDbGroupOpening")target=L"A-WINDOW";
  else if(type==L"AcDbLine"&&symbolName(entity->linetypeId())==L"DOTE")target=L"A-DOTE";
  else if(old==L"A-房间名称")target=old;

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
  validator->refreshLists(layout.get());productApplyPlanLayoutPaper(*layout,*validator,sheet.paperWidth,sheet.paperHeight);
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
  planRequireProtectedGeometry(p.report,p.protectedAfter,planTypographyProtected(saved,p.annotationScope),"template non-annotation objects changed on independent reopen");
  if(!p.framesOnly){ProductJob check;check.root=p.report;check.currentPlanRules=true;productVerifyPlanFurniture(check,saved,p.layeredFurniture,{});}
  planVerifyControlFrames(saved,p);
  if(!p.framesOnly){
  ProductJob typographyJob;typographyJob.profile=p.resources;typographyJob.root=p.report;
  productProtectFurnitureMembers(typographyJob,saved);
  const auto auditSheets=readJointPaperJob(p.source,p.sheets,p.output,p.report);jointPaperSheets(typographyJob,saved,auditSheets);
  typographyJob.planTables=p.tables;typographyJob.layoutSourcePanels=p.tableSourcePanels;
  productAuditPlanTables(typographyJob,saved);
  for(const auto& issue:typographyJob.detailPending)p.issues.push_back({issue[1],"table-coverage",issue[2]});
  for(const auto& issue:productAuditStoredAxisEnds(saved,p.report/L"axis-expected-readback.json"))p.issues.push_back({issue.first,"axis-expected",issue.second});
  const auto dependencyIssues=productAuditPlanFontDependencies(saved,p.fontCleanup,p.output,p.report);
  p.issues.insert(p.issues.end(),dependencyIssues.begin(),dependencyIssues.end());
  planTemplateAuditText(typographyJob,saved,p.output,p.report,p.issues);
  const auto unitGlyphIssues=productAuditPlanUnitGlyphRepairs(saved,p.unitGlyphRepairs,p.output,p.report);
  p.issues.insert(p.issues.end(),unitGlyphIssues.begin(),unitGlyphIssues.end());
  const auto riskGlyphIssues=productAuditRiskGlyphs(typographyJob,saved,p.output,p.report);
  p.issues.insert(p.issues.end(),riskGlyphIssues.begin(),riskGlyphIssues.end());
  for(const auto& issue:productAuditFinalPlanOmissions(typographyJob,saved,p.omissionContext,p.output,p.report))
   p.issues.push_back({issue.object,issue.role,issue.reason});
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
      const auto endIndex=static_cast<std::size_t>(&handle-item.targets.data());
      const auto& expectedNumber=endIndex<item.axisNumbers.size()?item.axisNumbers[endIndex]:item.axisNumber;
      require(productAxisCaption(id)==expectedNumber,"reopened complete native axis number differs");
      for(const auto& leaf:leaves)require(productTypographyFontMatches(leaf.style,{L"_TCH_AXIS",L"COMPLEX",L"GBCBIG",1,0,0},0),"reopened native axis font differs");
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
 for(const auto& fold:productFindAxisFolds(db).axisFolds)p->annotationScope.insert(fold.second);
 p->frames=planTemplateFrames(job,db,p->frameDatums,p->source);
 for(const auto& f:p->frames)p->annotationScope.insert(f.handle);
 p->layeredFurniture=planLayeredFurniture(db);
 productProtectFurnitureMembers(job,db);
 p->omissionContext=productPrepareFinalPlanOmissions(job,db);
 if(!p->omissionContext.views.empty()){
  productCapturePlanTables(job,db,p->omissionContext.views);p->tables=job.planTables;
  for(const auto& sheet:job.planSheetLayouts){const auto vp=open<AcDbViewport>(find(db,sheet.second.viewport));const auto c=vp->viewTarget();const auto offset=vp->viewCenter();
   p->tableSourcePanels.push_back(1+bcpproduct::nearestGenericView(p->omissionContext.views,{c.x+offset.x,c.y+offset.y}));}
 }
 p->annotationScope.insert(p->omissionContext.cleanupScope.begin(),p->omissionContext.cleanupScope.end());
 const auto protectedBefore=planTypographyProtected(db,p->annotationScope);
 productCleanFinalPlanOmissions(job,db,p->omissionContext);
 planTemplateIsolateFrames(db,job.protectedFurnitureMembers);
 p->issues=productNormalizeWholePlanTypography(job,db,reference);
 std::map<std::wstring,std::vector<AcDbObjectId>> openingGroups;
 for(const auto& [h,type]:modelTypes(db))if(type==L"TDbOpening"||type==L"TDbGroupOpening"){
  const auto id=find(db,ascii(h));const auto e=open<AcDbEntity>(id);if(!productPlanDimensionVisible(*e))continue;
  std::vector<ProductRoomLeaf> leaves;productRoomLeaves(*e,leaves);if(!leaves.empty())openingGroups[type].push_back(id);
 }
 for(const auto& [type,ids]:openingGroups){
  const auto problems=productEnsureEffectiveOpeningHeight(db,ids,productTypographyModelHeight(job,"_TCH_WINDOW"));
  p->issues.insert(p->issues.end(),problems.begin(),problems.end());
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
 const auto finalFonts=productRestorePlanTypography(job,db,reference,true);
 productSynchronizePlanDimensionTable(job,db,reference,finalFonts.dim);
 productCleanFinalPlanOmissions(job,db,p->omissionContext);
 {std::ostringstream state;state<<"{\"objects\":[";bool first=true;for(const auto& item:p->run.objects){if(!first)state<<',';first=false;
  state<<"{\"source\":\""<<item.source<<"\",\"kind\":\""<<item.kind<<"\",\"targets\":"<<item.targets.size()<<",\"issue\":"<<jsonUtf8(bcpproduct::decodeMessage(item.issue))<<'}';}
  state<<"]}";writeNew(p->report/L"template-adaptation-progress.json",state.str());}
 for(const auto& item:p->run.objects)for(const auto& target:item.targets)p->annotationScope.insert(target);
 for(const auto& fold:productFindAxisFolds(db).axisFolds)p->annotationScope.insert(fold.second);
 planRequireProtectedGeometry(p->report,protectedBefore,planTypographyProtected(db,p->annotationScope),"template annotations changed unrelated source geometry");
 planTemplateLayers(job,db);
 planTemplateTitleClearance(job,db,p->issues);
 p->unitGlyphRepairs=productRepairPlanUnitGlyphs(job,db,p->issues);
 planTemplatePlotSettings(job,db);
 p->fontCleanup=productCleanUnusedMissingPlanFonts(db);
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
