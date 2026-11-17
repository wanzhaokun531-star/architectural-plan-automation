// Geometry-driven fallback. No project handle/count/coordinate is an admission
// condition. Resource drawings provide annotation objects, never architecture.
struct GenericNativeWall {bcp::Wall wall;bool low{},orthogonal{};double total{};};
struct GenericNativeColumn {std::string handle;AcGePoint3d centre;double halfX{},halfY{};};
struct GenericNativeTitle {std::string handle;AcGePoint3d anchor;std::wstring text;};
struct GenericNativeOpening {
 std::string handle;
 AcGePoint3d a,b;
 double width{},height{},sill{};
 bool window{},grouped{};
 std::wstring existing,kind,style;
 std::vector<bcp::Point> groupSnapEndpoints;
};
struct GenericNativeInput {
 std::vector<GenericNativeWall> walls;
 std::vector<GenericNativeColumn> columns;
 std::vector<GenericNativeTitle> titles,notes;
 std::vector<GenericNativeOpening> openings;
 std::map<std::string,bcpproduct::PublicProperties> properties;
};
GenericNativeInput productReadGenericInput(ProductJob& job,AcDbDatabase& db){
 GenericNativeInput data;
 data.properties=productReadProperties(db,job.root/L"通用对象属性");
 const auto& properties=data.properties;
 for(const auto& item:modelTypes(db)){
  const auto h=ascii(item.first);const auto id=find(db,h);auto e=open<AcDbEntity>(id);if(e->isAProxy()){job.detailPending.push_back({u8"对象读取",h,u8"代理对象保留，未推定内部构件"});continue;}
  if(item.second==L"TDbWall"){
   auto f=properties.find(h);if(f==properties.end()){job.detailPending.push_back({u8"墙体参数",h,u8"无可读公开属性，保留原构件"});continue;}const auto& p=f->second;auto* curve=AcDbCurve::cast(e.get());AcGePoint3d a,b;if(!curve||curve->getStartPoint(a)!=Acad::eOk||curve->getEndPoint(b)!=Acad::eOk){job.detailPending.push_back({u8"墙体几何",h,u8"公开曲线端点不可读，保留原构件"});continue;}
   const auto length=std::hypot(b.x-a.x,b.y-a.y);
   if(!std::isfinite(length)||length<=1e-6){
    job.detailPending.push_back({u8"墙体几何",h,u8"源墙基线为零长或无效，保留原对象，该墙不参与平面取点"});
    continue;
   }
   GenericNativeWall w;w.wall.handle=h;w.wall.start={a.x,a.y};w.wall.end={b.x,b.y};w.low=p.count(L"对象类型")&&p.at(L"对象类型")==L"矮墙";
   if(p.count(L"左厚度")&&p.count(L"右厚度")){w.wall.left=bcpproduct::propertyNumber(p,L"左厚度");w.wall.right=bcpproduct::propertyNumber(p,L"右厚度");w.total=w.wall.left+w.wall.right;}
   else if(w.low&&p.count(L"厚度")){w.total=bcpproduct::propertyNumber(p,L"厚度");w.wall.role=bcp::WallRole::localShort;/* width does not imply an invented left/right split */}
   else{job.detailPending.push_back({u8"墙体参数",h,u8"公开厚度表达未识别，保留该墙，未按案例猜左右厚度"});continue;}
   require(std::isfinite(w.total)&&w.total>0,"positive actual native wall thickness");w.orthogonal=std::abs(a.x-b.x)<1e-5||std::abs(a.y-b.y)<1e-5;data.walls.push_back(w);
  }else if(item.second==L"TDbColumn"){
   AcGePoint3dArray g;AcDbIntArray a,b;if(e->getGripPoints(g,a,b)==Acad::eOk&&g.length()>0){GenericNativeColumn c{h,g[0]};for(int k=1;k<g.length();++k){c.halfX=std::max(c.halfX,std::abs(g[k].x-g[0].x));c.halfY=std::max(c.halfY,std::abs(g[k].y-g[0].y));}data.columns.push_back(c);}else job.detailPending.push_back({u8"柱轴",h,u8"实际柱中心不可读，保留柱并待核对"});
  }else if(item.second==L"TDbOpening"||item.second==L"TDbGroupOpening"){
   const auto f=properties.find(h);if(f==properties.end())continue;const auto& p=f->second;AcGePoint3dArray g;AcDbIntArray a,b;if(e->getGripPoints(g,a,b)!=Acad::eOk||g.length()<3||!p.count(L"宽度")||!p.count(L"高度")){job.detailPending.push_back({u8"门窗参数",h,u8"公开洞口端点或宽高缺失，保留原构件"});continue;}
   GenericNativeOpening o;o.handle=h;o.a=g[1];o.b=g[2];o.a.z=o.b.z=0;
   o.width=bcpproduct::propertyNumber(p,L"宽度");o.height=bcpproduct::propertyNumber(p,L"高度");
   if(p.count(L"离地高度"))o.sill=bcpproduct::propertyNumber(p,L"离地高度");
   if(p.count(L"门窗类型"))o.kind=p.at(L"门窗类型");
   if(p.count(L"门窗编号"))o.existing=p.at(L"门窗编号");
   for(const auto* key:{L"门窗样式",L"门窗式样",L"样式",L"门扇样式",L"门类型"})
    if(p.count(key))o.style+=p.at(key)+L" ";
   o.window=o.kind.find(L"窗")!=std::wstring::npos&&o.kind.find(L"门")==std::wstring::npos;
   o.grouped=item.second==L"TDbGroupOpening";
   if(o.grouped){
    AcGePoint3dArray ends;
    AcDbIntArray ids;
    if(e->getOsnapPoints(AcDb::kOsModeEnd,0,g[0],g[0],AcGeMatrix3d::kIdentity,ends,ids)==Acad::eOk)
     for(int i=0;i<ends.length();++i)o.groupSnapEndpoints.push_back({ends[i].x,ends[i].y});
   }
   data.openings.push_back(std::move(o));
  }else if(item.second==L"TDbDrawingName"||item.second==L"TDbText"){
   const auto f=properties.find(h);
   if(f==properties.end())continue;
   const auto g=chainGrips(id);
   const bool title=item.second==L"TDbDrawingName";
   const auto text=f->second.find(title?L"图名内容":L"文字内容");
   if(!g.empty()&&text!=f->second.end())(title?data.titles:data.notes).push_back({h,g.front(),text->second});
  }
 }
 require(!data.walls.empty(),u8"E_NATIVE_WALLS：没有可读取的天正墙体；未使用旧案例数量作判断。");return data;
}
AcDbObjectId productGenericChain(ProductJob& job,AcDbDatabase& db,AcDbObjectId prototype,const std::vector<AcGePoint3d>& stations,double offset){
 require(stations.size()>=2,"actual generic dimension stations");std::vector<AcDbObjectId> pieces;
 for(std::size_t k=1;k<stations.size();++k){if(stations[k].distanceTo(stations[k-1])<1e-5)continue;pieces.push_back(productDetailChain(db,prototype,{stations[k-1],stations[k]},offset));
  if(job.planMode=="template-finalize")status(editObject<AcDbEntity>(pieces.back())->setVisibility(AcDb::kVisible),"selectable template-native merge member");}
 require(!pieces.empty(),"nonzero generic dimension chain");auto result=pieces.front();
 for(std::size_t k=1;k<pieces.size();++k){const auto before=wholeModelIds(db);WholeSelection first(result),second(pieces[k]);require(acedCommandS(RTSTR,L"TMergeDim",RTPICKS,first.value,RTPICKS,second.value,RTSTR,L"",RTNONE)==RTNORM,"connect actual generic dimension chain");const auto after=wholeModelIds(db);std::vector<AcDbObjectId> candidates;for(auto id:after)if(!before.count(id)||id==result||id==pieces[k])candidates.push_back(id);require(after.size()+1==before.size()&&candidates.size()==1,"generic connection must produce one native overall chain");result=candidates.front();if(job.unifiedPlan)++job.planPublicCommands[L"TMergeDim"];}
 require(chainValues(result).size()==pieces.size(),"actual generic chain membership");job.dimensionMergeCommandUsed=job.dimensionMergeCommandUsed||pieces.size()>1;return result;
}
void productGenericAxisNumber(AcDbObjectId id,const std::string& label,bool vertical){
 auto g=chainGrips(id);require(g.size()==8,"native double-ended axis prototype");const auto value=bcpproduct::decodeMessage(label);approvedCommandPoint(L"TReAxisNo",g[1]+(vertical?AcGeVector3d(400,0,0):AcGeVector3d(0,400,0)),value.c_str());
 int texts=0,circles=0;{auto e=open<AcDbEntity>(id);readAxisLeaves(*e,label,0,texts,circles);}require(texts==2&&circles==2,"native generic axis number readback");
 if(productJob&&productJob->unifiedPlan)++productJob->planPublicCommands[L"TReAxisNo"];
}
bool productOutsideSelectedPlanWindows(const ProductJob& job,AcDbDatabase& saved,const std::string& handle);
std::optional<AcDbObjectId> productTryPlanChain(ProductJob& job,AcDbDatabase& db,AcDbObjectId prototype,
 const std::vector<AcGePoint3d>& stations,double offset,std::size_t component,
 const std::string& source,const std::string& scope,const char* rule){
 if(!job.unifiedPlan)return productGenericChain(job,db,prototype,stations,offset);
 const auto before=wholeModelIds(db);
 try{
  return productGenericChain(job,db,prototype,stations,offset);
 }catch(const std::runtime_error& error){
  for(const auto& id:wholeModelIds(db))if(!before.count(id))
   productStatus(editObject<AcDbEntity>(id)->erase(),"roll back failed local dimension");
  require(wholeModelIds(db)==before,"local dimension command affected an existing model object");
  const auto detail=std::string(u8"局部尺寸生成失败，保留原对象。")+error.what();
  job.detailPending.push_back({scope,source,detail});
  std::ostringstream record;
  record<<"{\"panel\":"<<component<<",\"object\":"<<jsonUtf8(bcpproduct::decodeMessage(source))
        <<",\"scope\":"<<jsonUtf8(bcpproduct::decodeMessage(scope))<<",\"stations\":"<<wholePoints(stations)
        <<",\"detail\":"<<jsonUtf8(bcpproduct::decodeMessage(detail))<<",\"result\":\"failed-local-dimension-write\"}";
  productRecordRuleEvidence(job,rule,record.str());
  return {};
 }
}
#include "ProductNativeLabelHeight.inc"
#include "ProductGenericOpenings.inc"
#include "ProductGenericProjection.inc"
#include "ProductGenericRooms.inc"
#include "ProductPlanGrossAreas.inc"
AcDbObjectId productAppendPlanDetail(AcDbDatabase&,std::unique_ptr<AcDbEntity>);
bool productPlanDetailHits(AcDbDatabase&,const std::vector<AcGePoint3d>&,const AcDbExtents&,const std::set<std::string>&);
#include "ProductEntranceSteps.inc"
#include "ProductBalconyEdges.inc"
#include "ProductPlanAcceptance.inc"
#include "ProductPlanWallHeights.inc"
#include "ProductPlanSourceCleanup.inc"
#include "ProductPlanDetailNotes.inc"
#include "ProductIndoorStairs.inc"
#include "ProductPlanVerticalCore.inc"
struct ProductPlanFloorAlignment {
 std::optional<int> floor;
 bcpproduct::AxisRegistrationPoint offset;
 std::optional<std::size_t> lowerView;
 std::optional<bcpproduct::AxisFloorRegistration> tie;
 bool registered{};
};
#include "ProductPlanRoof.inc"
#include "ProductPerimeterReconciliation.inc"
#include "ProductProjectAxes.inc"
#include "ProductPlanPanelOwnership.inc"
#include "ProductPlanSourceDimensions.inc"
#include "ProductPlanSourceInventory.inc"
#include "ProductPlanSourceText.inc"
#ifdef PLAN_STANDALONE_ENTRY
#include "ProductPlanPostcheckText.inc"
#endif
#include "ProductPlanDatums.inc"
#include "ProductPlanOldAnnotations.inc"
#include "ProductPlanDimensionReplacement.inc"
#include "ProductPlanTypography.inc"
#include "ProductPlanTemplate.inc"
#ifdef PLAN_STANDALONE_ENTRY
#include "ProductPlanDeliveryReview.inc"
#endif

struct ProductPlanAreaNoteMove {
 std::string handle;
 std::wstring contents;
 AcGePoint3d anchor;
};

struct ProductPlanTextRepair {
 std::string handle;
 std::vector<AcGePoint3d> before,after;
 std::vector<double> values;
};

using ProductPaperDimensionChains=std::map<std::string,std::pair<AcDbObjectId,GenericDimensionBoxes>>;

struct ProductPaperDimensionInk {
 std::vector<AcDbExtents> fixed;
 ProductPaperDimensionChains chains;
 std::set<std::string> roofDetails;
 std::vector<std::pair<AcGePoint3d,AcGePoint3d>> roofStrokes;
 std::vector<std::pair<AcGePoint3d,AcGePoint3d>> logicalAxes;
 std::vector<std::pair<AcGePoint3d,AcGePoint3d>> dimensionStrokes;
};

void productCollectPlanSolidInk(const AcDbEntity& entity,std::vector<AcDbExtents>& boxes,int depth=0){
 if(depth>6||entity.visibility()!=AcDb::kVisible)return;
 const auto* hatch=AcDbHatch::cast(&entity);
 if(AcDbSolid::cast(&entity)||AcDbTrace::cast(&entity)||(hatch&&hatch->isSolidFill()&&hatch->numLoops()==1)){
  AcDbExtents box;
  if(entity.getGeomExtents(box)==Acad::eOk)boxes.push_back(box);
  return;
 }
 if(hatch||AcDbLine::cast(&entity)||AcDbArc::cast(&entity)||AcDbPolyline::cast(&entity))return;
 TemplateParts parts(&entity);
 if(entity.explode(parts.values)!=Acad::eOk)return;
 for(int i=0;i<parts.values.length();++i)
  if(const auto* child=AcDbEntity::cast(static_cast<AcRxObject*>(parts.values[i])))
   productCollectPlanSolidInk(*child,boxes,depth+1);
}

ProductPaperDimensionInk productReadPaperDimensionInk(AcDbDatabase& db,const ProductPlanRoof& roof){
 ProductPaperDimensionInk ink;
 std::set<std::string> roofHandles;
 for(const auto& item:roof.imported)roofHandles.insert(item.handle);
 std::optional<AcDbExtents> roofBounds;
 for(const auto& row:roof.retained){
  if(row.role!="BODY"||row.points.empty())continue;
  if(!roofBounds)roofBounds.emplace(row.points.front(),row.points.front());
  for(const auto& point:row.points)roofBounds->addPoint(point);
 }
 for(const auto& [handle,type]:modelTypes(db)){
  const auto key=ascii(handle);
  const auto id=find(db,key);
  const auto entity=open<AcDbEntity>(id);
  if(!productPlanDimensionVisible(*entity)||!open<AcDbLayerTableRecord>(entity->layerId())->isPlottable())continue;
  const auto onRoof=[&]{
   AcDbExtents bounds;
   return roofBounds&&entity->getGeomExtents(bounds)==Acad::eOk&&productGenericBoxesOverlap(*roofBounds,bounds,0);
  };
  if(type==L"AcDbLine"){
   const auto* line=AcDbLine::cast(entity.get());
   if(onRoof())ink.roofStrokes.emplace_back(line->startPoint(),line->endPoint());
   if(symbolName(line->layerId())==L"DOTE")ink.logicalAxes.emplace_back(line->startPoint(),line->endPoint());
  }
  if((roofHandles.count(key)&&type==L"AcDbBlockReference")||
     ((type==L"TDbSlopeRoof"||type==L"AcDbPolyline")&&onRoof())){
   GenericOpeningInk graphics;
   productGenericOpeningInk(*entity,graphics);
   ink.roofStrokes.insert(ink.roofStrokes.end(),graphics.straight.begin(),graphics.straight.end());
   ink.roofStrokes.insert(ink.roofStrokes.end(),graphics.curved.begin(),graphics.curved.end());
  }
  if(type==L"TDbWall"||type==L"TDbColumn")productCollectPlanSolidInk(*entity,ink.fixed);
  if(type==L"TDbDimension2"){
   const auto grips=chainGrips(id);
   if(grips.size()<8)continue;
   const auto delta=grips[1]-grips[0];
   if(std::abs(delta.x)>1e-5&&std::abs(delta.y)>1e-5)continue;
   const bool vertical=std::abs(delta.y)>std::abs(delta.x);
   auto boxes=productGenericChainText(db,id,vertical);
   const bool roofDetail=roofHandles.count(key)>0;
   if(roofDetail)ink.roofDetails.insert(key);
   productPlanDimensionStrokes(*entity,ink.dimensionStrokes);
   ink.chains.emplace(key,std::make_pair(id,std::move(boxes)));
  }else if(type==L"TDbDrawingName"||type==L"TDbAxisLabelSet"||type==L"TDbText"||type==L"TDbMText"||
    type==L"TDbSymbElevation"||type==L"TDbOpening"||type==L"TDbGroupOpening"||
    (roofHandles.count(key)&&type==L"TDbSymbMultiLeader")||
    AcDbText::cast(entity.get())||AcDbMText::cast(entity.get())||AcDbDimension::cast(entity.get())){
   std::vector<AcDbExtents> boxes;
   productGenericTextLeaves(*entity,boxes);
   ink.fixed.insert(ink.fixed.end(),boxes.begin(),boxes.end());
  }
 }
 return ink;
}

bool productPaperDimensionConflict(const ProductPaperDimensionInk& ink,const std::string& handle,
 const AcDbExtents& box,std::size_t index,double paper){
 const auto hits=[&](const auto& other){return productGenericBoxesOverlap(box,other,.25*paper);};
 if(std::any_of(ink.fixed.begin(),ink.fixed.end(),hits))return true;
 for(const auto& [otherHandle,other]:ink.chains)
  for(std::size_t i=0;i<other.second.size();++i)
   if((otherHandle!=handle||i!=index)&&other.second[i]&&hits(*other.second[i]))return true;
 const auto hitsLine=[&](const auto& line){
  return productGenericSegmentHitsBox(line.first,line.second,box,.18*paper);
 };
 return std::any_of(ink.logicalAxes.begin(),ink.logicalAxes.end(),hitsLine)||
  std::any_of(ink.dimensionStrokes.begin(),ink.dimensionStrokes.end(),hitsLine)||
  (ink.roofDetails.count(handle)&&std::any_of(ink.roofStrokes.begin(),ink.roofStrokes.end(),hitsLine));
}

std::vector<ProductPlanTextRepair> productRepairPaperDimensionText(ProductJob& job,AcDbDatabase& db,
 const ProductPlanRoof& roof,double paper){
 auto ink=productReadPaperDimensionInk(db,roof);
 std::vector<ProductPlanTextRepair> result;
 for(auto& [handle,chain]:ink.chains){
  if(!ink.roofDetails.count(handle)||job.planDimensionExclusions.count(handle))continue;
  auto& [id,boxes]=chain;
  const auto original=chainGrips(id);
  const auto values=chainValues(id);
  if(boxes.size()!=values.size()||original.size()!=2*values.size()+6)continue;
  const auto count=values.size();
  const auto delta=original[1]-original[0];
  const bool vertical=std::abs(delta.y)>std::abs(delta.x);
  const auto tangent=vertical?AcGeVector3d::kYAxis:AcGeVector3d::kXAxis;
  const auto cross=vertical?AcGeVector3d::kXAxis:AcGeVector3d::kYAxis;
  const auto baseline=vertical?original[0].x:original[0].y;
  const auto associated=[&](const AcDbExtents& box,std::size_t i){
   const auto station=[&](const auto& point){return vertical?point.y:point.x;};
   const auto low=std::min(station(original[i+2]),station(original[i+3]));
   const auto high=std::max(station(original[i+2]),station(original[i+3]));
   const auto width=station(box.maxPoint())-station(box.minPoint());
   const double endGap=high-low<width?width*.5:0.;
   if(station(box.maxPoint())<low-endGap||station(box.minPoint())>high+endGap)return false;
   const auto a=box.minPoint();
   const auto b=box.maxPoint();
   const double distance=vertical?baseline-b.x:a.y-baseline;
   const double height=vertical?b.x-a.x:b.y-a.y;
   return distance<=2*height+.35*paper;
  };
  const auto conflicts=[&](const AcDbExtents& box,std::size_t index){
   return productPaperDimensionConflict(ink,handle,box,index,paper);
  };
  std::vector<int> modes;
  productGenericChainText(db,id,vertical,&modes);
  bool moved=false;
  std::vector<std::size_t> order(count);
  std::iota(order.begin(),order.end(),0);
  std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return values[a]<values[b];});
  for(const auto i:order){
   if(i>=modes.size())continue;
   if(modes[i]!=2||!boxes[i]||!conflicts(*boxes[i],i))continue;
   const auto grip=count+3+i;
   const auto anchor=chainGrips(id)[grip];
   const auto attempt=[&](const AcGeVector3d& offset){
    moveDimensionGrip(id,static_cast<int>(grip),anchor+offset*paper);
    const auto proposed=chainGrips(id);
    auto glyphs=productGenericChainText(db,id,vertical);
    const bool sameGeometry=proposed.size()==original.size()&&std::equal(proposed.begin(),proposed.begin()+count+3,original.begin(),
      [](const auto& a,const auto& b){return a.distanceTo(b)<=1e-5;})&&chainValues(id)==values;
    if(sameGeometry&&glyphs.size()==count&&glyphs[i]&&associated(*glyphs[i],i)&&
      productPlanTextOnRequiredSide(*glyphs[i],vertical,baseline)&&
      !conflicts(*glyphs[i],i)){
     boxes=std::move(glyphs);
     return true;
    }
    moveDimensionGrip(id,static_cast<int>(grip),anchor);
    return false;
   };
   bool placed=false;
   std::vector<double> steps{0.},offsets{0.};
   for(int turn=1;turn<=50;++turn){
    const double shift=.25*turn;
    offsets.push_back(shift);
    offsets.push_back(-shift);
    steps.push_back(shift);
    steps.push_back(-shift);
   }
   for(const double normal:steps){
    for(const double parallel:offsets){
     if(normal==0.&&parallel==0.)continue;
     const auto shift=(cross*(vertical?-normal:normal)+tangent*parallel)*paper;
     const AcDbExtents estimate(boxes[i]->minPoint()+shift,boxes[i]->maxPoint()+shift);
     if(!associated(estimate,i)||!productPlanTextOnRequiredSide(estimate,vertical,baseline)||conflicts(estimate,i))continue;
     if(attempt(cross*(vertical?-normal:normal)+tangent*parallel)){placed=true;break;}
    }
    if(placed)break;
   }
   moved|=placed;
  }
  if(moved)result.push_back({handle,original,chainGrips(id),values});
 }
 return result;
}

void productVerifyPaperDimensionRepairs(ProductJob& job,AcDbDatabase& db,const std::vector<ProductPlanTextRepair>& repairs,
 const ProductPlanRoof& roof,double paper){
 std::ostringstream out;
 out<<"{\"savedReopened\":true,\"repairs\":[";
 bool first=true;
 for(const auto& repair:repairs){
  const auto id=find(db,repair.handle);
  const auto actual=chainGrips(id);
  const bool same=actual.size()==repair.after.size()&&std::equal(actual.begin(),actual.end(),repair.after.begin(),
   [](const auto& a,const auto& b){return a.distanceTo(b)<=1e-5;})&&chainValues(id)==repair.values;
  require(same,"paper dimension repair was not retained on reopening");
  if(!first)out<<',';
  first=false;
  out<<"{\"object\":\""<<repair.handle<<"\",\"before\":"<<wholePoints(repair.before)<<",\"after\":"<<wholePoints(actual)<<'}';
 }
 out<<"],\"roofDetailDimensions\":[";
 if(!roof.imported.empty()){
  const auto ink=productReadPaperDimensionInk(db,roof);
  first=true;
  for(const auto& item:roof.imported){
   if(item.type!=L"TDbDimension2")continue;
   const auto chain=ink.chains.find(item.handle);
   const auto values=chainValues(find(db,item.handle));
   bool clearAll=!values.empty();
   for(std::size_t i=0;i<values.size();++i){
    const auto location=std::to_string(roof.panel)+"/"+item.handle;
    bool clear=chain!=ink.chains.end()&&i<chain->second.second.size()&&chain->second.second[i];
    if(clear)clear=!productPaperDimensionConflict(ink,item.handle,*chain->second.second[i],i,paper);
    clearAll&=clear;
    if(!clear)job.detailPending.push_back({u8"纸面图元净空",location,
      u8"屋面细部尺寸第"+std::to_string(i+1)+u8"段在重开图中仍缺文字净空"});
    if(!first)out<<',';
    first=false;
    out<<"{\"object\":\""<<item.handle<<"\",\"segment\":"<<i+1<<",\"measurement\":"<<values[i]
       <<",\"clear\":"<<(clear?"true":"false")<<'}';
   }
   if(clearAll){
    auto& pending=job.detailPending;
    pending.erase(std::remove_if(pending.begin(),pending.end(),[&](const auto& issue){
     return issue[0]==u8"室内尺寸避让"&&issue[1]==item.handle;
    }),pending.end());
   }
  }
 }
 out<<"]}";
 writeNew(job.root/L"plan-print-text-repair.json",out.str());
}

std::vector<ProductPlanAreaNoteMove> productRepairAreaNoteClearance(
 AcDbDatabase& db,const ProductPlanLayoutConfig& layout){
 Working working(&db);
 const double paper=layout.scale/layout.unitMM;
 const double gap=.4*paper;
 std::vector<std::pair<AcDbObjectId,AcDbExtents>> obstacles;
 std::vector<AcDbObjectId> notes;
 for(const auto& [handle,type]:modelTypes(db)){
  const auto id=find(db,ascii(handle));
  const auto object=open<AcDbEntity>(id);
  if(!productPlanDimensionVisible(*object))continue;
  if(const auto* text=AcDbText::cast(object.get())){
   const std::wstring contents=text->textString();
   if(contents.find(L"建筑面积")!=std::wstring::npos)notes.push_back(id);
  }
  if(type==L"TDbDimension2"){
   TemplateParts parts(object.get());
   if(object->explode(parts.values)!=Acad::eOk)continue;
   const auto grips=chainGrips(id);
   const int count=parts.values.length();
   for(int i=0;i<count;++i){
    auto* dim=AcDbAlignedDimension::cast(static_cast<AcRxObject*>(parts.values[i]));
    if(!dim)continue;
    const auto vector=dim->xLine2Point()-dim->xLine1Point();
    const auto point=grips.size()==static_cast<std::size_t>(2*count+6)?grips[count+3+i]:dim->textPosition();
    if(const auto box=productGenericDimensionTextBox(db,*dim,point,std::abs(vector.y)>std::abs(vector.x)))
     obstacles.emplace_back(id,*box);
   }
   continue;
  }
  if(type==L"TDbAxisLabelSet"||type==L"TDbDrawingName"||type==L"TDbSymbElevation"||
     type==L"TDbText"||type==L"AcDbMText"){
   std::vector<AcDbExtents> leaves;
   productGenericTextLeaves(*object,leaves);
   for(const auto& box:leaves)obstacles.emplace_back(id,box);
   continue;
  }
  if(type!=L"AcDbText"&&type!=L"TDbWall"&&type!=L"TDbColumn"&&
     type!=L"TDbOpening"&&!AcDbDimension::cast(object.get()))continue;
  AcDbExtents box;
  if(object->getGeomExtents(box)==Acad::eOk)obstacles.emplace_back(id,box);
 }
 std::vector<ProductPlanAreaNoteMove> moves;
 for(const auto id:notes){
  auto note=open<AcDbText>(id);
  const auto contents=std::wstring(note->textString());
  AcDbExtents original;
  if(note->getGeomExtents(original)!=Acad::eOk)continue;
  const auto anchor=productPlanNameAnchor(*note);
  if(!anchor)continue;
  const auto withinSheet=std::any_of(layout.panels.begin(),layout.panels.end(),[&](const auto& panel){
   return original.maxPoint().x>=panel.xmin-45*paper&&original.minPoint().x<=panel.xmax+45*paper&&
          anchor->y<panel.ymin&&panel.ymin-anchor->y<=90*paper;
  });
  if(!withinSheet)continue;
  const auto clear=[&](const AcDbExtents& box){
   return std::none_of(obstacles.begin(),obstacles.end(),[&](const auto& item){
    return item.first!=id&&productGenericBoxesOverlap(box,item.second,gap);
   });
  };
  if(clear(original))continue;
  note.reset();
  for(int step=1;step<=60;++step){
   const AcGeVector3d delta(0,-step*paper/2,0);
   const AcDbExtents candidate(original.minPoint()+delta,original.maxPoint()+delta);
   if(!clear(candidate))continue;
   const auto moved=editObject<AcDbText>(id);
   if(moved->transformBy(AcGeMatrix3d::translation(delta))!=Acad::eOk)break;
   for(auto& obstacle:obstacles)if(obstacle.first==id)obstacle.second=candidate;
   moves.push_back({axisHandle(id),contents,*anchor+delta});
   break;
  }
 }
 return moves;
}

struct ProductLooseNotePlacement {
 std::string handle;
 std::vector<AcGePoint3d> before,after;
 std::wstring text;
};

std::wstring productConstructionNoteContents(AcDbObjectId id){
 const auto entity=open<AcDbEntity>(id);
 if(const auto* text=AcDbMText::cast(entity.get())){
  AcString value;productStatus(text->contents(value),"construction note contents");return value.kACharPtr();
 }
 return productPlanDetailContents(*entity);
}

std::vector<ProductLooseNotePlacement> productArrangeConstructionNotes(ProductJob& job,AcDbDatabase& db,
 std::vector<ProductEntranceReceipt>& entries,ProductPlanRoof& roof,double paper){
 const auto frames=productPlanDrawingFrames(db,paper);
 std::set<std::string> handles;
 for(auto& entry:entries){
  if(entry.verticalNote.empty()&&!entry.verticalText.empty()){
   auto note=std::make_unique<AcDbMText>();
   note->setDatabaseDefaults(&db);
   productStatus(note->setLayer(productPlanLayer(db,L"A-TEXT-DESC",7)),"deferred entrance note layer");
   productStatus(note->setTextHeight(2.5*paper),"deferred entrance note height");
   productStatus(note->setWidth(0.),"explicit paragraph entrance note");
   productStatus(note->setAttachment(AcDbMText::kMiddleCenter),"deferred entrance note alignment");
   const auto index=std::string("WESN").find(entry.side);
   const AcGeVector3d outward=index==0?AcGeVector3d(-1,0,0):index==1?AcGeVector3d(1,0,0):
    index==2?AcGeVector3d(0,-1,0):AcGeVector3d(0,1,0);
   productStatus(note->setLocation(entry.anchor+outward*(20*paper)),"deferred entrance note anchor");
   note->setContents(entry.verticalText.c_str());
   entry.verticalNote=axisHandle(productAppendPlanDetail(db,std::move(note)));
  }
  if(!entry.verticalNote.empty())handles.insert(entry.verticalNote);
 }
 for(const auto& link:job.planSheetLinks)if(!link.note.empty())handles.insert(link.note);
 for(const auto& [source,datum]:job.planRegisteredDatums)
  if(std::wstring(open<AcDbEntity>(find(db,datum.output))->isA()->name())==L"TDbSymbMultiLeader")handles.insert(datum.output);
 for(const auto& item:roof.imported)if(item.type==L"TDbSymbMultiLeader")handles.insert(item.handle);
 std::vector<ProductLooseNotePlacement> placements;
 for(const auto& handle:handles){
  const auto id=find(db,handle);
  const auto before=chainGrips(id);
  const auto value=productConstructionNoteContents(id);
  const auto entry=std::find_if(entries.begin(),entries.end(),[&](const auto& e){return e.verticalNote==handle;});
  const bool leader=std::wstring(open<AcDbEntity>(id)->isA()->name())==L"TDbSymbMultiLeader";
  if(leader&&before.size()!=4)continue;
  if(!leader){auto text=editObject<AcDbMText>(id);text->setWidth(0.);}
  ProductPlanDetailObstacles fixedInk(db,{handle});
  const auto boxBlocked=[&](const AcDbExtents& box){
    if(entry!=entries.end()){
     const auto a=box.minPoint(),b=box.maxPoint(),p=entry->anchor;
     if((entry->side=="W"&&b.x>p.x-5*paper)||(entry->side=="E"&&a.x<p.x+5*paper)||
        (entry->side=="S"&&b.y>p.y-5*paper)||(entry->side=="N"&&a.y<p.y+5*paper))return true;
    }
    return !productPlanInsideFrame(frames,before.front(),box,5*paper)||fixedInk.hits({},box);
  };
  const auto glyphs=[&](){
   const auto entity=open<AcDbEntity>(id);
   std::vector<AcDbExtents> boxes;
   productGenericTextLeaves(*entity,boxes);
   return boxes;
  };
  const auto clear=[&](){
   const auto boxes=glyphs();
   return !boxes.empty()&&std::none_of(boxes.begin(),boxes.end(),boxBlocked);
  };
  if(!clear()){
   std::vector<AcGeVector3d> offsets;
   for(int x=-32;x<=32;++x)for(int y=-32;y<=32;++y)
    if(x||y)offsets.emplace_back(x*2.5*paper,y*2.5*paper,0);
   std::stable_sort(offsets.begin(),offsets.end(),[](const auto& a,const auto& b){return a.lengthSqrd()<b.lengthSqrd();});
   AcGeVector3d current;
   const auto originalBoxes=glyphs();
   for(const auto& offset:offsets){
    if(originalBoxes.empty()||std::any_of(originalBoxes.begin(),originalBoxes.end(),[&](const auto& box){
     return boxBlocked(AcDbExtents(box.minPoint()+offset,box.maxPoint()+offset));
    }))continue;
    if(leader&&(before[0].x+offset.x-before[2].x)*(before[0].x-before[2].x)<=0)continue;
    {
     auto entity=editObject<AcDbEntity>(id);
     if(leader){
      // The landing grip moves its text and endpoint as one native unit.
      // Selecting dependent grips together reapplies Tianzheng's width logic.
      AcDbIntArray selected;selected.append(0);
      productStatus(entity->moveGripPointsAt(selected,offset-current),"place leader text with fixed arrow");
     }else productStatus(entity->transformBy(AcGeMatrix3d::translation(offset-current)),"place construction note");
    }
    current=offset;
    if(clear())break;
   }
   if(!clear()){
    auto entity=editObject<AcDbEntity>(id);
    if(leader){AcDbIntArray selected;selected.append(0);productStatus(entity->moveGripPointsAt(selected,-current),"restore unplaced native leader");}
    else productStatus(entity->transformBy(AcGeMatrix3d::translation(-current)),"restore unplaced construction note");
   }
  }
  const auto after=chainGrips(id);
  if(productConstructionNoteContents(id)!=value){
   std::ostringstream diagnostic;
   diagnostic<<"{\"object\":\""<<handle<<"\",\"leader\":"<<(leader?"true":"false")
    <<",\"beforeText\":"<<jsonUtf8(value)<<",\"afterText\":"<<jsonUtf8(productConstructionNoteContents(id))
    <<",\"before\":"<<wholePoints(before)<<",\"after\":"<<wholePoints(after)<<'}';
   writeNew(job.root/L"construction-note-rejected.json",diagnostic.str());
  }
  require(productConstructionNoteContents(id)==value,"construction note text preserved");
  if(leader)require(after.size()==4&&after[2].distanceTo(before[2])<1e-5,"leader arrow remains at source construction point");
  if(!clear())job.detailPending.push_back({u8"纸面图元净空",handle,u8"施工说明仍无可用文字净空"});
  else for(auto& entry:entries)if(entry.verticalNote==handle){
   const std::string pending=u8"；台阶高差说明尚未找到与实际构件及标注不相撞的位置";
   const auto at=entry.missing.find(pending);
   if(at!=std::string::npos)entry.missing.erase(at,pending.size());
  }
  for(auto& [source,datum]:job.planRegisteredDatums)if(datum.output==handle)datum.points=after;
  for(auto& item:roof.imported)if(item.handle==handle)item.points=after;
  placements.push_back({handle,before,after,value});
 }
 return placements;
}

struct ProductPlanTitlePlacement {
 std::string title;
 std::size_t panel{};
 std::vector<std::string> group;
 std::vector<AcGePoint3d> anchors;
 std::set<std::string> excluded;
 AcGeVector3d shift;
};

std::vector<ProductPlanTitlePlacement> productArrangePlanTitles(ProductJob& job,AcDbDatabase& db,
 const GenericNativeInput& input,const std::vector<bcpproduct::GenericView>& views,double paper,
 std::vector<ProductPlanAreaSummary>& summaries,std::vector<ProductPlanAreaNoteMove>& areaMoves){
 if(!job.unifiedPlan)return {};
 std::vector<ProductPlanTitlePlacement> result;
 std::vector<const GenericNativeTitle*> titles;
 for(const auto& title:input.titles)
  if(bcpproduct::explicitPlanFloor(title.text)||productRoofTitle(title.text))titles.push_back(&title);
 for(const auto& title:input.notes)
  if(bcpproduct::explicitPlanFloor(title.text)&&!productRoofTitle(title.text))titles.push_back(&title);
 const auto typeMap=modelTypes(db);
 for(const auto* title:titles){
  const auto view=bcpproduct::nearestGenericView(views,{title->anchor.x,title->anchor.y});
  const auto selected=std::find(job.layoutSourcePanels.begin(),job.layoutSourcePanels.end(),view+1);
  const bool floorTitle=bcpproduct::explicitPlanFloor(title->text).has_value()&&!productRoofTitle(title->text);
  if(floorTitle&&selected==job.layoutSourcePanels.end())continue;
  const auto titleId=find(db,title->handle);
  auto root=open<AcDbEntity>(titleId);
  AcDbExtents titleBounds;
  if(!productPlanDimensionVisible(*root)||root->getGeomExtents(titleBounds)!=Acad::eOk)continue;
  const bool separateTitle=std::wstring(root->isA()->name())==L"TDbText"||AcDbText::cast(root.get());
  const auto titleLayer=root->layerId();
  const auto anchor=productPlanNameAnchor(*root).value_or(title->anchor);
  root.reset();
  const auto centre=titleBounds.minPoint()+(titleBounds.maxPoint()-titleBounds.minPoint())*.5;
  std::optional<AcDbExtents> clip;
  AcDbObjectIdArray originalFrameLayers;
  for(const auto& [number,sheet]:job.planSheetLayouts){
   const auto viewport=open<AcDbViewport>(find(db,sheet.viewport));
   const auto scale=viewport->customScale();
   if(scale<=0)continue;
   const auto target=viewport->viewTarget();
   const AcGeVector3d half(viewport->width()/(2*scale),viewport->height()/(2*scale),0);
   const AcDbExtents window(target-half,target+half);
   if(!productGenericBoxesOverlap(titleBounds,window,0))continue;
   if(clip){clip.reset();break;}
   clip=window;
   productStatus(viewport->getFrozenLayerList(originalFrameLayers),"read verified title viewport layers");
  }
  std::vector<std::string> group{title->handle};
  if(separateTitle){
   std::vector<std::pair<std::string,AcDbExtents>> ratios;
   for(const auto& [wide,type]:typeMap){
    if(type!=L"AcDbText"&&type!=L"TDbText")continue;
    const auto handle=ascii(wide);
    if(handle==title->handle)continue;
    const auto note=open<AcDbEntity>(find(db,handle));
    AcDbExtents box;
    if(note->layerId()!=titleLayer||!productPlanDimensionVisible(*note)||note->getGeomExtents(box)!=Acad::eOk||
       box.minPoint().x<titleBounds.maxPoint().x-.5*paper||box.minPoint().x>titleBounds.maxPoint().x+8*paper)continue;
    const auto at=productPlanNameAnchor(*note);
    if(!at||std::abs(at->y-anchor.y)>2.5*paper)continue;
    const auto value=productPlanNameContents(*note);
    if(value.size()>=3&&value.front()==L'1'&&(value[1]==L':'||value[1]==L'：')&&
       std::all_of(value.begin()+2,value.end(),[](auto c){return c>=L'0'&&c<=L'9';}))
     ratios.emplace_back(handle,box);
   }
   if(ratios.size()>1)continue;
   double right=titleBounds.maxPoint().x;
   if(ratios.size()==1){
    group.push_back(ratios.front().first);
    right=ratios.front().second.maxPoint().x;
   }
   for(const auto& [wide,type]:typeMap){
    if(type!=L"AcDbPolyline"&&type!=L"AcDbLine")continue;
    const auto handle=ascii(wide);
    const auto entity=open<AcDbEntity>(find(db,handle));
    if(!productPlanDimensionVisible(*entity)||symbolName(entity->layerId())!=L"A-TEXT-DESC")continue;
    AcGePoint3d a,b;
    if(const auto* polyline=AcDbPolyline::cast(entity.get())){
     if(polyline->numVerts()!=2||polyline->isClosed()||polyline->segType(0)!=AcDbPolyline::kLine||
        polyline->getPointAt(0,a)!=Acad::eOk||polyline->getPointAt(1,b)!=Acad::eOk)continue;
    }else{
     const auto* line=AcDbLine::cast(entity.get());
     a=line->startPoint();b=line->endPoint();
    }
    if(std::abs(a.y-b.y)>1e-5||a.y>anchor.y||anchor.y-a.y>7*paper)continue;
    const auto [left,end]=std::minmax(a.x,b.x);
    if(std::abs(left-titleBounds.minPoint().x)<=2*paper&&std::abs(end-right)<=2*paper)
     group.push_back(handle);
   }
  }
  for(const auto& [wide,type]:typeMap){
   if(type!=L"AcDbText"&&type!=L"TDbText")continue;
   const auto handle=ascii(wide);
   const auto note=open<AcDbEntity>(find(db,handle));
   if(!productPlanDimensionVisible(*note))continue;
   const auto value=productPlanNameContents(*note);
   if(value.find(L"建筑面积")==std::wstring::npos&&value.rfind(L"门廊面积",0)!=0&&
      value.rfind(L"主楼滴水面积",0)!=0)continue;
   const auto anchor=productPlanNameAnchor(*note);
   if(!anchor||std::abs(anchor->x-centre.x)>100*paper||anchor->y>centre.y||
      centre.y-anchor->y>65*paper)continue;
   const auto distance=[&](const GenericNativeTitle* candidate){
    return std::hypot(candidate->anchor.x-anchor->x,candidate->anchor.y-anchor->y);
   };
   if(std::any_of(titles.begin(),titles.end(),[&](const auto* other){
    return other!=title&&distance(other)<=distance(title);
   }))continue;
   group.push_back(handle);
  }
  if(!clip){
   if(!floorTitle||!job.architecturalPlanOnly||!job.formalPlan||!job.planPrintReview||
      job.planSheetLayouts.count(static_cast<int>(selected-job.layoutSourcePanels.begin()+1)))continue;
   // The final fallback viewport is built from the measured member boxes after arrangement.
   const auto& bounds=views[view].bounds;
   const double margin=75*paper;
   clip=AcDbExtents({std::min(bounds.xmin,titleBounds.minPoint().x)-margin,
                    std::min(bounds.ymin,titleBounds.minPoint().y)-margin,0},
                   {std::max(bounds.xmax,titleBounds.maxPoint().x)+margin,
                    std::max(bounds.ymax,titleBounds.maxPoint().y)+margin,0});
  }
  std::set<std::string> excluded(group.begin(),group.end());
  for(const auto& [wide,type]:typeMap){
   if(originalFrameLayers.isEmpty())break;
   const auto entity=open<AcDbEntity>(find(db,ascii(wide)));
   if(originalFrameLayers.contains(entity->layerId()))excluded.insert(ascii(wide));
  }
  ProductPlanDetailObstacles fixed(db,excluded);
  std::vector<AcDbExtents> boxes;
  for(const auto& handle:group){
   const auto entity=open<AcDbEntity>(find(db,handle));
   AcDbExtents box;
   productStatus(entity->getGeomExtents(box),"title group bounds");
   boxes.push_back(box);
  }
  std::optional<AcGeVector3d> shift;
  const std::vector<AcDbExtents> frame{*clip};
  for(double y=0.;y<=65.&&!shift;y+=.5)for(const double x:{0.,-5.,5.,-10.,10.,-20.,20.,-30.,30.}){
   const AcGeVector3d delta(x*paper,-y*paper,0);
   const bool clear=std::all_of(boxes.begin(),boxes.end(),[&](const auto& box){
    const AcDbExtents moved(box.minPoint()+delta,box.maxPoint()+delta);
    return productPlanInsideFrame(frame,centre,moved,5*paper)&&!fixed.hits({},moved);
   });
   if(clear){shift=delta;break;}
  }
  const auto delta=shift.value_or(AcGeVector3d::kIdentity);
  ProductPlanTitlePlacement placement{title->handle,view+1,group,{},excluded,delta};
  for(const auto& handle:group){
   const auto id=find(db,handle);
   if(delta.lengthSqrd()>1e-8)wholeTranslate(id,delta);
   const auto entity=open<AcDbEntity>(id);
   const auto anchor=productPlanNameAnchor(*entity);
   if(anchor)placement.anchors.push_back(*anchor);
   for(auto& summary:summaries)if(summary.output==handle)summary.anchor=summary.anchor+delta;
   for(auto& move:areaMoves)if(move.handle==handle)move.anchor=move.anchor+delta;
  }
  if(!shift)job.detailPending.push_back({u8"图名图面净空",title->handle,u8"图名及建筑面积说明与实际图元相交，未找到图幅内可用位置"});
  result.push_back(std::move(placement));
 }
 return result;
}

void productVerifyPlanTitlePlacement(ProductJob& job,AcDbDatabase& db,const std::vector<ProductPlanTitlePlacement>& groups){
 for(const auto& group:groups){
  ProductPlanDetailObstacles obstacles(db,group.excluded);
  bool clear=group.anchors.size()==group.group.size();
  for(std::size_t i=0;i<group.group.size()&&clear;++i){
   const auto entity=open<AcDbEntity>(find(db,group.group[i]));
   AcDbExtents box;
   const auto anchor=productPlanNameAnchor(*entity);
   clear=anchor&&anchor->distanceTo(group.anchors[i])*job.unifiedUnitMM<=.01&&
    entity->getGeomExtents(box)==Acad::eOk&&!obstacles.hits({},box);
  }
  job.drawingChecks.record(group.panel,"title-and-area-ink-clearance",group.title,{"L06","P05","QA02"},
   bcpproduct::PlanReleaseGate::mainPlan,clear?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::failed,
   u8"图名与面积说明按原组重开核验，实际界线和尺寸线避让",
   clear?"":u8"图名或面积说明与实际线迹相交，或重开锚点改变");
 }
}

void productCollectPlanPrintWindows(ProductJob& job,AcDbDatabase& db,const GenericNativeInput& input,
 const std::vector<bcpproduct::GenericView>& views,const ProductPlanLayoutConfig& layout,
 const ProductPlanRoof& roof,const std::vector<ProductPlanAreaSummary>& areaSummaries,
 const std::vector<ProductPlanTitlePlacement>& titlePositions,const std::vector<ProductPlanAreaNoteMove>& movedAreaNotes){
 if(!job.unifiedPlan)return;
 const double paper=layout.scale/layout.unitMM;
 const double margin=6*paper;
 const auto expand=[&](std::array<double,4>& window,const std::string& handle){
  const auto entity=open<AcDbEntity>(find(db,handle));
  AcDbExtents box;
  if(!productPlanDimensionVisible(*entity)||entity->getGeomExtents(box)!=Acad::eOk)return;
  window[0]=std::min(window[0],box.minPoint().x-margin);
  window[1]=std::min(window[1],box.minPoint().y-margin);
  window[2]=std::max(window[2],box.maxPoint().x+margin);
  window[3]=std::max(window[3],box.maxPoint().y+margin);
 };
 const auto expandFrame=[&](std::array<double,4>& window,const AcGePoint3d& anchor){
  std::vector<std::pair<double,std::string>> frames;
  for(const auto& [handle,type]:modelTypes(db)){
   if(type!=L"AcDbPolyline")continue;
   const auto entity=open<AcDbEntity>(find(db,ascii(handle)));
   const auto* poly=AcDbPolyline::cast(entity.get());
   AcDbExtents box;
   if(!poly||!poly->isClosed()||poly->numVerts()!=4||symbolName(poly->layerId())!=L"PUB_TITLE"||
      !productPlanDimensionVisible(*poly)||poly->getGeomExtents(box)!=Acad::eOk)continue;
   const auto a=box.minPoint(),b=box.maxPoint();
   if(anchor.x<a.x||anchor.x>b.x||anchor.y<a.y||anchor.y>b.y)continue;
   const double w=(b.x-a.x)/paper,h=(b.y-a.y)/paper;
   if(w<200||w>850||h<140||h>610)continue;
   frames.emplace_back(w*h,ascii(handle));
  }
  if(frames.empty())return;
  const auto outer=std::max_element(frames.begin(),frames.end());
  expand(window,outer->second);
 };
 for(std::size_t panel=0;panel<layout.panels.size();++panel){
  const auto& bounds=layout.panels[panel];
  const auto placed=productPlacementBounds(layout,static_cast<int>(panel));
  const double band=60*paper;
  std::array<double,4> window{std::min(bounds.xmin,placed.xmin)-band,std::min(bounds.ymin,placed.ymin)-band,
                            std::max(bounds.xmax,placed.xmax)+band,std::max(bounds.ymax,placed.ymax)+band};
  const auto source=job.layoutSourcePanels.at(panel)-1;
  const auto inPanel=[&](const auto& point){
   const auto distance=bcpproduct::genericDistanceSquared(views[source].bounds,{point.x,point.y});
   return distance<=band*band&&std::all_of(job.layoutSourcePanels.begin(),job.layoutSourcePanels.end(),[&](const auto other){
    return distance<=bcpproduct::genericDistanceSquared(views[other-1].bounds,{point.x,point.y});
   });
  };
  for(const auto& title:input.titles)
   if(bcpproduct::explicitPlanFloor(title.text)&&inPanel(title.anchor)){
    expand(window,title.handle);
    expandFrame(window,title.anchor);
   }
  if(job.architecturalPlanOnly&&!job.planSheetLayouts.count(static_cast<int>(panel+1))){
   std::set<std::string> visibleTitles;
   for(const auto* names:{&input.titles,&input.notes})for(const auto& title:*names){
    if(!bcpproduct::explicitPlanFloor(title.text)||
       bcpproduct::nearestGenericView(views,{title.anchor.x,title.anchor.y})!=source)continue;
    AcDbObjectId id;
    if(db.getAcDbObjectId(id,false,AcDbHandle(bcpproduct::decodeMessage(title.handle).c_str()))==Acad::eOk&&!id.isErased()&&
       productPlanDimensionVisible(*open<AcDbEntity>(id)))visibleTitles.insert(title.handle);
   }
   if(visibleTitles.size()==1)expand(window,*visibleTitles.begin());
  }
  for(const auto& summary:areaSummaries)
   if(summary.view==source)expand(window,summary.output);
  for(const auto& title:titlePositions)if(title.panel==source+1)
   for(const auto& member:title.group)expand(window,member);
  for(const auto& note:movedAreaNotes)
   if(inPanel(note.anchor))expand(window,note.handle);
  for(const auto& note:input.notes)
   if(note.text.find(L"面积")!=std::wstring::npos&&inPanel(note.anchor)){
    AcDbObjectId id;
    if(db.getAcDbObjectId(id,false,AcDbHandle(bcpproduct::decodeMessage(note.handle).c_str()))==Acad::eOk&&!id.isErased())
     expand(window,note.handle);
   }
  job.planPrintWindows.push_back(window);
 }
 if(!roof.items.empty()){
  const auto outline=std::find_if(roof.items.begin(),roof.items.end(),[](const auto& item){return item.kind=="OUTLINE";});
  if(outline!=roof.items.end()&&!outline->points.empty()){
   AcDbExtents box(outline->points.front(),outline->points.front());
   for(const auto& point:outline->points)box.addPoint(point);
   std::array<double,4> window{box.minPoint().x-margin,box.minPoint().y-margin,
     box.maxPoint().x+margin,box.maxPoint().y+margin};
   for(const auto& item:roof.items)expand(window,item.handle);
   for(const auto& copy:roof.copies)expand(window,copy.handle);
   job.planPrintWindows.push_back(window);
  }
 }else if(!roof.existingTitle.empty()){
  const GenericNativeTitle* title=nullptr;
  for(const auto* names:{&input.titles,&input.notes})for(const auto& item:*names)
   if(item.handle==roof.existingTitle)title=&item;
  if(title){
   std::vector<std::pair<double,std::string>> bodies;
   for(const auto& [handle,type]:modelTypes(db))if(type==L"TDbSlopeRoof"){
    const auto entity=open<AcDbEntity>(find(db,ascii(handle)));
    AcDbExtents box;
    if(entity->getGeomExtents(box)!=Acad::eOk)continue;
    const auto centre=box.minPoint()+(box.maxPoint()-box.minPoint())*.5;
    const double distance=std::hypot(centre.x-title->anchor.x,centre.y-title->anchor.y);
    if(distance<200*paper)bodies.emplace_back(distance,ascii(handle));
   }
   if(bodies.empty()&&job.architecturalPlanOnly){
    // The largest source-registered closed roof boundary encloses the skylights and internal edges.
    double greatest=0.;
    std::string exterior;
    for(const auto& item:roof.retained)if(item.role=="BOUNDARY"){
     const auto outline=open<AcDbPolyline>(find(db,item.handle));
     double area{};
     if(outline->getArea(area)==Acad::eOk&&area>greatest){greatest=area;exterior=item.handle;}
    }
    if(!exterior.empty())bodies.emplace_back(0.,exterior);
   }
   std::sort(bodies.begin(),bodies.end());
   if(!bodies.empty()&&(bodies.size()==1||bodies[1].first-bodies[0].first>.01/layout.unitMM)){
    const auto body=open<AcDbEntity>(find(db,bodies.front().second));
    AcDbExtents bounds;
    if(body->getGeomExtents(bounds)==Acad::eOk){
     const double band=60*paper;
     std::array<double,4> window{bounds.minPoint().x-band,bounds.minPoint().y-band,
       bounds.maxPoint().x+band,bounds.maxPoint().y+band};
     expand(window,roof.existingTitle);
     for(const auto& item:roof.retained)expand(window,item.handle);
     for(const auto& title:titlePositions)if(title.title==roof.existingTitle)
      for(const auto& member:title.group)expand(window,member);
     expandFrame(window,title->anchor);
     if(!roof.drainSchedule.empty())expand(window,roof.drainSchedule);
     for(const auto& mark:roof.existingAxisMarks){
      expand(window,mark.line);
      if(mark.label!="-")expand(window,mark.label);
     }
     for(const auto& side:roof.existingAxisSides)
      for(const auto& handle:side.dimensions)if(!handle.empty())expand(window,handle);
     job.planPrintWindows.push_back(window);
    }
   }
  }
  if(job.planPrintWindows.size()==layout.panels.size())
   job.detailPending.push_back({u8"打印图幅",roof.existingTitle,u8"既有屋顶图幅未取得唯一可核的屋面实体范围"});
 }
 if(roof.items.empty()&&roof.existingTitle.empty()&&!job.architecturalPlanOnly)
  job.detailPending.push_back({u8"打印图幅",u8"屋顶",u8"未取得可供正式纸面校核的独立屋顶图"});
}

void productCheckPlanPrintGeometry(ProductJob& job,AcDbDatabase& db,
 const GenericNativeInput& input,const std::vector<bcpproduct::GenericView>& views){
 struct Ink {
  std::string handle;
  std::size_t view{};
  AcDbExtents bounds;
  std::vector<AcDbExtents> text;
  bool dimension{};
  AcDbObjectId layer;
  std::optional<std::array<AcGePoint3d,4>> outline;
 };
 Working work(&db);
 std::vector<Ink> ink;
 const auto ownerOf=[&](const AcGePoint3d& point){
  if(job.layoutSourcePanels.empty())return bcpproduct::nearestGenericView(views,{point.x,point.y});
  auto best=job.layoutSourcePanels.front()-1;
  for(const auto panel:job.layoutSourcePanels){
   if(bcpproduct::genericDistanceSquared(views[panel-1].bounds,{point.x,point.y})<
      bcpproduct::genericDistanceSquared(views[best].bounds,{point.x,point.y}))best=panel-1;
  }
  return best;
 };
 for(const auto& [handle,type]:modelTypes(db)){
  const auto id=find(db,ascii(handle));
  const auto entity=open<AcDbEntity>(id);
  const auto layer=open<AcDbLayerTableRecord>(entity->layerId());
  if(!layer->isPlottable()||!productPlanDimensionVisible(*entity))continue;
  AcDbExtents bounds;
  if(entity->getGeomExtents(bounds)!=Acad::eOk)continue;
  const auto centre=bounds.minPoint()+(bounds.maxPoint()-bounds.minPoint())*.5;
  const auto owner=std::find_if(input.titles.begin(),input.titles.end(),[&](const auto& title){return title.handle==ascii(handle);});
  const auto point=owner!=input.titles.end()?owner->anchor:centre;
  Ink row{ascii(handle),ownerOf(point),bounds,{},
   type==L"TDbDimension2"||AcDbDimension::cast(entity.get())!=nullptr,entity->layerId()};
  if(const auto* polyline=AcDbPolyline::cast(entity.get());polyline&&polyline->isClosed()&&
     polyline->numVerts()==4&&std::abs(polyline->thickness())<1e-8){
   std::array<AcGePoint3d,4> vertices;
   bool traced=true;
   for(unsigned k=0;k<vertices.size();++k){
    double startWidth{},endWidth{};
    if(polyline->segType(k)!=AcDbPolyline::kLine||polyline->getPointAt(k,vertices[k])!=Acad::eOk||
       polyline->getWidthsAt(k,startWidth,endWidth)!=Acad::eOk||
       std::abs(startWidth)>1e-8||std::abs(endWidth)>1e-8){
     traced=false;
     break;
    }
   }
   if(traced)row.outline=vertices;
  }
  if(type==L"TDbDimension2")row.text=productPlanChainGlyphs(db,*entity);
  else if(AcDbDimension::cast(entity.get())||type==L"TDbDrawingName"||
     type==L"TDbAxisLabelSet"||type==L"TDbText"||type==L"TDbMText"||type==L"TDbSymbElevation"||type==L"TDbSymbMultiLeader"||
     AcDbText::cast(entity.get())||AcDbMText::cast(entity.get()))productGenericTextLeaves(*entity,row.text);
  ink.push_back(std::move(row));
 }
 std::ostringstream report;
 report<<"{\"savedReopened\":true,\"pages\":[";
 const auto issue=[&](std::size_t page,const std::string& handle,const char* reason){
  ++job.planPrintDefects;
  job.detailPending.push_back({u8"纸面图元净空",std::to_string(page+1)+"/"+handle,reason});
 };
 for(std::size_t i=0;i<job.planPrintWindows.size();++i){
  if(i)report<<',';
  auto w=job.planPrintWindows[i];
  const auto before=job.planPrintDefects;
  struct Glyph { std::string handle; AcDbExtents box; bool dimension; };
  std::vector<Glyph> glyphs;
  AcDbObjectIdArray frozenLayers;
#ifdef PLAN_STANDALONE_ENTRY
  const auto sheet=job.planSheetLayouts.find(static_cast<int>(i+1));
  if(sheet!=job.planSheetLayouts.end()&&!sheet->second.sourceTitle.empty()){
   const auto viewport=open<AcDbViewport>(find(db,sheet->second.viewport));
   productStatus(viewport->getFrozenLayerList(frozenLayers),"read saved paper viewport's frozen layers");
   if(viewport->customScale()>0.&&std::abs(viewport->twistAngle())<1e-8){
    const auto target=viewport->viewTarget();
    const auto origin=viewport->viewCenter();
    const auto x=target.x+origin.x,y=target.y+origin.y;
    const auto halfWidth=viewport->width()/(2*viewport->customScale());
    const auto halfHeight=viewport->height()/(2*viewport->customScale());
    w={x-halfWidth,y-halfHeight,x+halfWidth,y+halfHeight};
   }
  }
#endif
  const AcDbExtents box({w[0],w[1],0},{w[2],w[3],0});
  for(const auto& row:ink){
   if(frozenLayers.contains(row.layer))continue;
   const bool inside=row.bounds.minPoint().x>=w[0]&&row.bounds.maxPoint().x<=w[2]&&
                     row.bounds.minPoint().y>=w[1]&&row.bounds.maxPoint().y<=w[3];
   const bool visible=inside||productGenericBoxesOverlap(box,row.bounds,0.);
   if(!visible)continue;
   if(!inside&&row.outline){
    const auto& points=*row.outline;
    bool strokesVisible=false;
    for(std::size_t k=0;k<points.size();++k)
     if(productGenericSegmentHitsBox(points[k],points[(k+1)%points.size()],box,0.)){
      strokesVisible=true;
      break;
     }
    if(!strokesVisible)continue;
   }
   if(i<job.layoutSourcePanels.size()&&row.view+1!=job.layoutSourcePanels[i]){
    issue(i,row.handle,u8"打印窗口混入邻幅对象，按原图归属核边界");
    continue;
   }
   if(!inside){
    issue(i,row.handle,u8"对象跨越打印窗口，按原图归属核裁切");
    continue;
   }
   for(const auto& text:row.text){
    const auto meets=[&](const auto& other){
     return (other.handle!=row.handle||(row.dimension&&other.dimension))&&
      productGenericBoxesOverlap(text,other.box,0.);
    };
    const auto overlap=std::find_if(glyphs.begin(),glyphs.end(),meets);
    if(overlap!=glyphs.end())issue(i,row.handle+"/"+overlap->handle,u8"实际文字边界相交，联合排版后复验");
    glyphs.push_back({row.handle,text,row.dimension});
   }
  }
  report<<"{\"page\":"<<i+1<<",\"visibleTextBoxes\":"<<glyphs.size()
        <<",\"defects\":"<<job.planPrintDefects-before<<'}';
 }
 job.planPrintGeometryChecked=!job.planPrintWindows.empty();
 report<<"],\"defects\":"<<job.planPrintDefects<<'}';
 writeNew(job.root/L"plan-print-geometry.json",report.str());
}

// Export the facts used by this build with source-scoped handles and WCS.
void productWritePlanFacts(ProductJob& job,const GenericNativeInput& input,
                           const std::vector<bcpproduct::GenericView>& views,
                           const std::vector<bool>& generated,const std::vector<std::vector<GenericPlanAxis>>& axes,
                           const ProductPlanLayoutConfig& layout,
                           const std::vector<ProductPlanFloorAlignment>& alignment) {
 std::ostringstream out;
 out.imbue(std::locale::classic());
 out<<std::setprecision(17)<<"{\"schema\":\"bcp.plan-facts.v1\",\"sourceSHA256\":\""<<job.sourceHash
    <<"\",\"coordinates\":\"source WCS\",\"unitMM\":"<<layout.unitMM<<",\"scale\":"<<layout.scale
    <<",\"floorRegistrationConfirmed\":"<<
      (std::any_of(alignment.begin(),alignment.end(),[](const auto& floor){return floor.tie.has_value();})?"true":"false")
    <<",\"views\":[";
 for (std::size_t view=0;view<views.size();++view) {
  if (view) out<<',';
  const auto& group=views[view];
  out<<"{\"component\":"<<view+1<<",\"generated\":"<<(generated[view]?"true":"false")
     <<",\"bounds\":["<<group.bounds.xmin<<','<<group.bounds.ymin<<','<<group.bounds.xmax<<','<<group.bounds.ymax
     <<"],\"floor\":";
  const auto& current=alignment[view];
  if(current.floor)out<<*current.floor;else out<<"null";
  out<<",\"finishedLevelMM\":null,\"floorRegistration\":";
  if(current.tie){
   out<<"{\"lowerView\":"<<*current.lowerView+1<<",\"offsetFromLower\":["<<current.tie->shift.x<<','<<current.tie->shift.y
      <<"],\"matchingIntersections\":"<<current.tie->matches<<",\"maxResidualMM\":"<<current.tie->maxResidual*layout.unitMM<<'}';
  }else out<<"null";
  out<<",\"titles\":[";
  bool first=true;
  const auto writeTitle=[&](const auto& title){
   if(bcpproduct::nearestGenericView(views,{title.anchor.x,title.anchor.y})!=view)return;
   if(!first)out<<',';
   first=false;
   out<<"{\"handle\":\""<<title.handle<<"\",\"text\":"<<jsonUtf8(title.text)<<'}';
  };
  for(const auto& title:input.titles)writeTitle(title);
  if(job.unifiedPlan)for(const auto& note:input.notes)
   if(bcpproduct::explicitPlanFloor(note.text))writeTitle(note);
  out<<"],\"axisCandidates\":[";
  first=true;
  for (const auto& axis:axes[view]) {
   if (!first) out<<',';
   first=false;
   out<<"{\"label\":\""<<axis.label<<"\",\"vertical\":"<<(axis.vertical?"true":"false")
      <<",\"coordinate\":"<<axis.coordinate<<",\"showNegative\":"<<(axis.showNegative?"true":"false")
      <<",\"showPositive\":"<<(axis.showPositive?"true":"false")<<'}';
  }
  out<<"],\"walls\":[";
  first=true;
  for (auto index:group.walls) {
   const auto& wall=input.walls[index];
   if (!first) out<<',';
   first=false;
   out<<"{\"handle\":\""<<wall.wall.handle<<"\",\"endpointXY\":[["<<wall.wall.start.x<<','<<wall.wall.start.y
      <<"],["<<wall.wall.end.x<<','<<wall.wall.end.y<<"]],\"thickness\":"<<wall.total
      <<",\"lowWall\":"<<(wall.low?"true":"false")<<'}';
  }
  out<<"],\"columns\":[";
  first=true;
  for (const auto& column:input.columns) {
   if (bcpproduct::nearestGenericView(views,{column.centre.x,column.centre.y})!=view) continue;
   if (!first) out<<',';
   first=false;
   out<<"{\"handle\":\""<<column.handle<<"\",\"centreXY\":["<<column.centre.x<<','<<column.centre.y<<"]}";
  }
  out<<"],\"openings\":[";
  first=true;
  for (const auto& opening:input.openings) {
   const auto mid=opening.a+(opening.b-opening.a)*.5;
   if (bcpproduct::nearestGenericView(views,{mid.x,mid.y})!=view) continue;
   if (!first) out<<',';
   first=false;
   out<<"{\"handle\":\""<<opening.handle<<"\",\"endpointXY\":[["<<opening.a.x<<','<<opening.a.y
      <<"],["<<opening.b.x<<','<<opening.b.y<<"]],\"width\":"<<opening.width<<",\"height\":"<<opening.height
      <<",\"sourceSill\":";
   const auto& properties=input.properties.at(opening.handle);
   if (properties.count(L"离地高度")) out<<opening.sill; else out<<"null";
   out<<",\"sourceKind\":"<<jsonUtf8(opening.kind)<<",\"sourceNumber\":"<<jsonUtf8(opening.existing)<<'}';
  }
  out<<"]}";
 }
 out<<"]}";
 writeNew(job.root/L"平面构件数据.json",out.str());
}

void productAuditGenericAxes(ProductJob& job,AcDbDatabase& db,const ProductPlanLayoutConfig& layout,
 const std::vector<std::vector<GenericPlanAxis>>& views,const std::vector<bool>& active,
 const std::vector<ProductPlanFloorAlignment>& alignment,
 const std::map<std::wstring,std::wstring>& original,const std::array<std::vector<double>,2>& buildingStations) {
 Working working(&db);
 const double tolerance=.01/layout.unitMM;
 int panel=0;
 for(std::size_t view=0;view<views.size();++view){
  if(!active[view])continue;
  if(alignment[view].tie){
   const auto& tie=*alignment[view].tie;
   std::ostringstream record;
   record<<std::setprecision(17)<<"{\"panel\":"<<view+1<<",\"lowerPanel\":"<<*alignment[view].lowerView+1
     <<",\"scope\":\"source-wall-intersection-translation\",\"matchingIntersections\":"<<tie.matches
     <<",\"shiftXY\":["<<tie.shift.x<<','<<tie.shift.y<<"],\"maxResidualMM\":"<<tie.maxResidual*layout.unitMM
     <<",\"report\":\"平面构件数据.json\",\"result\":\"verified-source-floor-translation\"}";
   productRecordRuleEvidence(job,"A32",record.str());
  }
  for(const auto& axis:views[view]){
   const auto prefix=[&](const std::string& handle){
    std::ostringstream row;
    row<<std::setprecision(17)<<"{\"panel\":"<<view+1<<",\"object\":\""<<handle
       <<"\",\"label\":\""<<axis.label<<"\",\"vertical\":"<<(axis.vertical?"true":"false")
       <<",\"station\":"<<axis.coordinate<<",\"savedReopened\":true,";
    return row.str();
   };
   bool localGeometry=!axis.writtenLines.empty();
   std::vector<std::pair<double,double>> actualRanges;
   std::vector<std::string> lines;
   for(const auto& [handle,expected]:axis.writtenLines){
    const auto line=open<AcDbLine>(find(db,handle));
    const auto a=line->startPoint(),b=line->endPoint();
    const bool sameEndpoints=a.distanceTo(expected.first)<tolerance&&b.distanceTo(expected.second)<tolerance;
    const bool onStation=std::abs(productAxisStation(a,axis.vertical)-axis.coordinate)<=tolerance&&
       std::abs(productAxisStation(b,axis.vertical)-axis.coordinate)<=tolerance;
    const bool independent=line->ownerId()==modelId(db)&&symbolName(line->layerId())==layout.logicalAxisLayer&&
       productPlanDimensionVisible(*line);
    localGeometry&=sameEndpoints&&independent&&onStation;
    const auto type=open<AcDbLinetypeTableRecord>(line->linetypeId());
    const bool dashed=type->numDashes()>1;
    auto editable=std::unique_ptr<AcDbEntity>(AcDbEntity::cast(line->clone()));
    AcDbIntArray index;
    index.append(1);
    const auto step=(b-a).normal()*std::max(1.,layout.scale/layout.unitMM);
    const bool stretched=editable&&editable->moveGripPointsAt(index,step)==Acad::eOk&&
       AcDbLine::cast(editable.get())->startPoint().distanceTo(a)<=tolerance&&
       AcDbLine::cast(editable.get())->endPoint().distanceTo(b+step)<=tolerance;
    const auto first=productAxisNormal(a,axis.vertical),last=productAxisNormal(b,axis.vertical);
    actualRanges.emplace_back(std::min(first,last),std::max(first,last));
    lines.push_back(handle);
    const auto points="\"endpoints\":"+wholePoints({a,b})+",\"independent\":"+(independent?"true":"false")+
        ",\"dashed\":"+(dashed?"true":"false")+",\"result\":\"";
    productRecordRuleEvidence(job,"A09",prefix(handle)+points+(sameEndpoints?"verified-generated-axis-range":"pending")+"\"}");
    productRecordRuleEvidence(job,"A10",prefix(handle)+points+
        (sameEndpoints&&dashed?"verified-continuous-line-geometry":"pending")+"\"}");
    productRecordRuleEvidence(job,"A11",prefix(handle)+points+
        (independent&&stretched?"verified-public-endpoint-grip-on-unattached-clone":"pending-public-grip")+"\"}");
   }
   const auto axisObject=axis.labelHandle.empty()?(lines.empty()?std::string{}:lines.front()):axis.labelHandle;
   const auto sourceRecord=[&]{
    std::ostringstream row;
    row<<std::setprecision(17)<<prefix(axisObject)<<"\"sourceWalls\":";
    productWriteHandleArray(row,axis.sourceWalls);
    row<<",\"sourceColumns\":";
    productWriteHandleArray(row,axis.sourceColumns);
    row<<",\"axisLines\":";
    productWriteHandleArray(row,lines);
    row<<",\"sourceServiceRanges\":[";
    for(std::size_t i=0;i<axis.sourceRanges.size();++i){
     if(i)row<<',';
     row<<'['<<axis.sourceRanges[i].first<<','<<axis.sourceRanges[i].second<<']';
    }
    row<<"],\"projectableNegative\":"<<(axis.negative?"true":"false")
       <<",\"projectablePositive\":"<<(axis.positive?"true":"false")
       <<",\"report\":\"平面构件数据.json\",\"result\":\"";
    return row.str();
   }();
   const bool allSourceCovered=localGeometry&&!axis.sourceRanges.empty()&&std::all_of(
      axis.sourceRanges.begin(),axis.sourceRanges.end(),[&](const auto& source){
       return std::any_of(actualRanges.begin(),actualRanges.end(),[&](const auto& actual){
        return actual.first<=source.first+tolerance&&actual.second>=source.second-tolerance;
       });
      });
   const auto check=allSourceCovered?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::pending;
   const auto reason=allSourceCovered?"":u8"对应独立轴线的原生端点、图层或服务范围未通过重开核验";
   for(const auto& handle:axis.sourceWalls)
    productCheckPlanSource(job,view+1,"wall-axis",handle,check,lines,reason);
   for(const auto& handle:axis.sourceColumns)
    productCheckPlanSource(job,view+1,axis.vertical?"column-axis-X":"column-axis-Y",handle,check,lines,reason);
   if(!axis.sourceWalls.empty()||!axis.sourceColumns.empty())
    productRecordRuleEvidence(job,"A01",sourceRecord+(allSourceCovered?"verified-major-wall-and-native-column-stations":"pending-source-axis-coverage")+"\"}");
   productRecordRuleEvidence(job,"A04",sourceRecord+(allSourceCovered?"verified-source-service-side-candidate":"pending-source-service")+"\"}");
   productRecordRuleEvidence(job,"A16",sourceRecord+(allSourceCovered?"verified-native-source-range-and-readback":"pending-service-range")+"\"}");
   productRecordRuleEvidence(job,"A17",sourceRecord+(allSourceCovered?"verified-native-source-range-and-readback":"pending-service-range")+"\"}");
   const auto sources=axis.sourceWalls.size()+axis.sourceColumns.size();
   if(sources>=2)productRecordRuleEvidence(job,"A24",sourceRecord+
      (allSourceCovered&&lines.size()==1?"verified-exact-source-common-station":"pending-common-station")+"\"}");
   if(sources)productRecordRuleEvidence(job,"A25",sourceRecord+
      (allSourceCovered?"verified-root-native-geometry-only":"pending-native-geometry")+"\"}");
   if(sources)productRecordRuleEvidence(job,"A31",sourceRecord+
      (allSourceCovered?"verified-generated-native-source-position":"pending-source-position")+"\"}");
   if(!axis.negative&&!axis.positive&&sources)
    productRecordRuleEvidence(job,"A23",sourceRecord+(allSourceCovered?"verified-distinct-local-axis-geometry":"pending-local-axis")+"\"}");
   if(axis.labelHandle.empty()){
    if(axis.showNegative||axis.showPositive){
     const auto row=prefix(axisObject)+"\"scope\":\"expected-axis-label\",\"result\":\"pending-missing-axis-label\"}";
     for(const auto* rule:{"A02","A03","A07","A28"})productRecordRuleEvidence(job,rule,row);
    }
    if(alignment[view].registered&&!axis.writtenLines.empty())
     productRecordRuleEvidence(job,"A32",prefix(axis.writtenLines.front().first)+
         "\"scope\":\"registered-local-axis\",\"visibleCircle\":false,\"result\":\""+
         (localGeometry?"verified-local-axis-position":"pending")+"\"}");
    continue;
   }
   int texts=0,circles=0;
   bool native=true;
   bool joinedToAxis=true;
   const auto bounds=productPlacementBounds(layout,panel);
   bool negative=false,positive=false;
   std::vector<std::string> actualHandles;
   for(const auto& role:layout.axes){
    if(role.panel!=panel)continue;
    const auto original=layout.axisOriginalHandles.find(role.handle);
    if((original==layout.axisOriginalHandles.end()?role.handle:original->second)!=axis.labelHandle)continue;
    actualHandles.push_back(role.handle);
    const auto id=find(db,role.handle);
    const auto object=open<AcDbEntity>(id);
    native&=!object->isAProxy()&&std::wstring(object->isA()->name())==L"TDbAxisLabelSet";
    readAxisLeaves(*object,axis.label,0,texts,circles);
    const auto grips=chainGrips(id);
    for(std::size_t i=0;i+1<grips.size();i+=4){
     const auto side=productAxisEnd(bounds,axis.vertical,grips[i],grips[i+1]).side;
     (side%2?positive:negative)=true;
     const auto fold=productFoldPoints(db,layout,role.handle,side);
     const auto anchor=fold.empty()?grips[i]:fold.front();
     const bool onStation=std::abs(productAxisStation(anchor,axis.vertical)-axis.coordinate)<tolerance;
     const bool hasService=side%2?axis.positive:axis.negative;
     const auto& lines=axis.writtenLines;
     const bool joined=std::any_of(lines.begin(),lines.end(),[&](const auto& segment){
      const auto line=open<AcDbLine>(find(db,segment.first));
      const AcGeLineSeg3d edge(line->startPoint(),line->endPoint());
      return edge.closestPointTo(anchor).distanceTo(anchor)<tolerance;
     });
     joinedToAxis&=onStation&&(joined||!hasService);
     const auto row=prefix(role.handle)+"\"side\":"+std::to_string(side)+",\"anchor\":"+wholePoints({anchor})+
        ",\"nativeLead\":"+wholePoints({grips[i]})+",\"fold\":"+wholePoints(fold)+
        ",\"report\":\"外围间距核验.json\",\"result\":\"";
     productRecordRuleEvidence(job,"A12",row+(onStation?"verified-axis-lead-anchor":"pending")+"\"}");
     productRecordRuleEvidence(job,"A28",row+
        (onStation&&joined?"verified-axis-line-connection":onStation&&!hasService?"verified-reference-circle-no-line-extension":"pending-line-connection")+"\"}");
    }
   }
   const int expectedCount=axis.showNegative+axis.showPositive;
   const bool correctText=!actualHandles.empty()&&texts==expectedCount&&circles==expectedCount;
   const bool sidesMatch=negative==axis.showNegative&&positive==axis.showPositive&&correctText;
   const auto& outputHandle=actualHandles.empty()?axis.labelHandle:actualHandles.front();
   const auto record=prefix(outputHandle)+"\"negative\":"+(negative?"true":"false")+
      ",\"positive\":"+(positive?"true":"false")+",\"circleCount\":"+std::to_string(circles)+",\"result\":\"";
   productRecordRuleEvidence(job,"A02",record+(correctText?"verified-generated-axis-number":"pending")+"\"}");
   productRecordRuleEvidence(job,"A03",record+(sidesMatch?"verified-generated-axis-sides":"pending")+"\"}");
   productRecordRuleEvidence(job,"A07",record+
      (correctText&&native?
         "verified-native-axis-label":"pending")+"\"}");
   if(axis.showNegative!=axis.showPositive){
    productRecordRuleEvidence(job,"A05",record+(sidesMatch&&localGeometry?"verified-label-visibility-independent-of-logical-axis":"pending-side-visibility")+"\"}");
    productRecordRuleEvidence(job,"A20",record+(sidesMatch&&localGeometry?"pending-functional-space-classification":"pending-side-visibility")+"\"}");
   }
   if(alignment[view].registered){
    const auto& offset=alignment[view].offset;
    const double station=axis.coordinate-(axis.vertical?offset.x:offset.y);
    const auto& whole=buildingStations[axis.vertical?0:1];
    const auto match=std::lower_bound(whole.begin(),whole.end(),station-tolerance);
    const auto rank=static_cast<std::size_t>(match-whole.begin());
    const auto number=axis.vertical?std::to_string(rank+1):bcp::alphabetLabel(rank).value_or("");
    const bool registeredNumber=match!=whole.end()&&std::abs(*match-station)<=tolerance&&axis.label==number;
    productRecordRuleEvidence(job,"A27",record+
       (correctText&&localGeometry&&registeredNumber?"verified-registered-station-and-glyph":"pending-registered-number")+"\"}");
    std::ostringstream normalized;
    normalized<<std::setprecision(17)<<axis.coordinate-(axis.vertical?offset.x:offset.y);
    productRecordRuleEvidence(job,"A32",prefix(outputHandle)+"\"scope\":\"registered-axis-label-and-position\","
      "\"registeredStation\":"+normalized.str()+",\"result\":\""+
      (sidesMatch&&native&&joinedToAxis&&localGeometry?"verified-registered-axis-label":"pending")+"\"}");
    const bool verified=correctText&&registeredNumber&&sidesMatch&&native&&joinedToAxis&&localGeometry;
    job.drawingChecks.record(view+1,"registered-axis",outputHandle,{"A27","A32"},
     bcpproduct::PlanReleaseGate::mainPlan,verified?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::failed,
     "saved-reopened",verified?u8"全楼轴位、编号及引线接合通过":u8"全楼轴位、编号或引线接合复验未通过");
   }
  }
  ++panel;
 }
 for(const auto& [raw,type]:original){
  if(type!=L"AcDbLine"&&type!=L"TDbAxisLabelSet")continue;
  const auto handle=ascii(raw);
  AcDbObjectId id;
  if(db.getAcDbObjectId(id,false,AcDbHandle(raw.c_str()))!=Acad::eOk||id.isErased())continue;
  const auto object=open<AcDbEntity>(id);
  const auto layer=symbolName(object->layerId());
  if(type==L"AcDbLine"&&layer!=L"DOTE"&&layer!=L"AXIS")continue;
  const auto grips=chainGrips(object->objectId());
  std::ostringstream row;
  row<<"{\"object\":\""<<handle<<"\",\"kind\":"<<jsonUtf8(type)<<",\"layer\":"<<jsonUtf8(layer)
     <<",\"sourceAndReopenedHandle\":true,\"points\":"<<wholePoints(grips)<<",\"savedReopened\":true,"
     <<"\"result\":\"pending-retained-source-axis-role-and-number-occupancy\"}";
  for(const auto* rule:{"A25","A26","A31"})productRecordRuleEvidence(job,rule,row.str());
 }
}

#ifdef PLAN_STANDALONE_ENTRY
void applyConfirmedPlanRooms(ProductJob&,AcDbDatabase&,const std::vector<ConfirmedPlanRoom>&,
 const std::vector<ProductPlanBounds>&,std::vector<ProductPlanNameCheck>&,double,double,
 std::vector<ProductRoomWriteReceipt>&);
std::function<void(AcDbDatabase&)> arrangeFullPlanRoomDimensions(
 ProductJob& job,AcDbDatabase& db,AcDbDatabase& reference,
 const std::map<std::wstring,std::wstring>& original,const GenericNativeInput& input,const std::vector<bcpproduct::GenericView>& views,
 const std::vector<bool>& active,const std::vector<ProductPlanBounds>& panels,
 std::vector<std::optional<ProductRoomGrid>> grids,
 const std::vector<std::pair<AcDbObjectId,bool>>& added,const std::vector<ProductStairRow>& stairs,
 double scale,double unit,const std::function<void()>& placeSourceText);
#endif

void productApplyConfirmedPlanBases(ProductJob& job,AcDbDatabase& db,GenericNativeInput& input){
 const auto specs=productPlanDetailConfirmations(job).elevationBases;
 if(specs.empty())return;
 const double tolerance=.01/job.unifiedUnitMM;
 std::map<std::string,std::vector<AcGePoint3d>> before;
 std::set<std::string> handles;
 for(const auto& [handle,values]:specs){
  const auto entity=open<AcDbEntity>(find(db,handle));
  const std::wstring type=entity->isA()->name();
  require(!entity->isAProxy()&&(type==L"TDbWall"||type==L"TDbOpening"),"plan base supports native wall or independent opening");
  const auto points=chainGrips(entity->objectId());
  require(!points.empty()&&std::abs(points.front().z-values[0])<tolerance,"source-bound plan base does not match original elevation");
  before.emplace(handle,points);handles.insert(handle);
 }
 const auto fields=productReadProperties(db,job.root/L"构件基标高修正前",&handles);
 auto* transaction=db.transactionManager();
 require(transaction->startTransaction()!=nullptr,"start confirmed plan base transaction");
 try{
  for(const auto& [handle,values]:specs){
   // Native wall reactors can already have moved an associated opening.
   const auto points=chainGrips(find(db,handle));
   require(!points.empty(),"read native base before translation");
   const double current=points.front().z;
   require(std::abs(current-values[0])<tolerance||std::abs(current-values[1])<tolerance,
     "unexpected associated object elevation change");
   if(std::abs(current-values[1])>=tolerance)
    status(editObject<AcDbEntity>(find(db,handle))->transformBy(
      AcGeMatrix3d::translation({0,0,values[1]-current})),"translate confirmed native plan base");
  }
  const auto actual=productReadProperties(db,job.root/L"构件基标高修正后",&handles);
  for(const auto& [handle,values]:specs){
   const auto points=chainGrips(find(db,handle));
   const auto& original=before.at(handle);
   require(points.size()==original.size()&&std::abs(points.front().z-values[1])<tolerance,
     "native plan base translation failed");
   for(std::size_t i=0;i<points.size();++i)
    require(std::hypot(points[i].x-original[i].x,points[i].y-original[i].y)<tolerance,
      "plan base correction changed plan geometry");
   const auto& old=fields.at(handle);const auto& now=actual.at(handle);
   for(const auto* field:{L"宽度",L"高度",L"左厚度",L"右厚度",L"门窗类型",L"门窗编号"})
    require(!old.count(field)||(now.count(field)&&old.at(field)==now.at(field)),
      "plan base correction changed protected public properties");
   const auto* field=old.count(L"离地高度")?L"离地高度":L"底标高";
   require(now.count(field)&&std::abs(bcpproduct::propertyNumber(now,field)-values[1])<=.51/job.unifiedUnitMM,
     "native public base property did not follow translation");
  }
  status(transaction->endTransaction(),"commit native base correction");
  for(const auto& handle:handles)input.properties[handle]=actual.at(handle);
  for(auto& opening:input.openings)if(specs.count(opening.handle)){
   opening.sill=specs.at(opening.handle)[1];
  }
 }catch(...){transaction->abortTransaction();throw;}
}

void productFinishGeneric(){
 auto& job=*productJob;auto& db=*curDoc()->database();Working context(&db);ApprovedCommandVars variables;
 std::vector<std::pair<const char*,ULONGLONG>> timings;
 auto tick=GetTickCount64();
 const auto stamp=[&](const char* stage){
  const auto now=GetTickCount64();
  if(job.validation)timings.emplace_back(stage,now-tick);
  tick=now;
 };
 auto input=productReadGenericInput(job,db);const auto original=modelTypes(db);
 stamp("native-source-read");
 const auto sourceMasks=job.unifiedPlan?productMasks(db):std::vector<std::string>{};
 const auto sourcePerimeterWitnesses=job.unifiedPlan?productPerimeterSourceWitnesses(db,job):std::vector<ProductPerimeterSourceWitness>{};
 const auto sourceAxisOccupants=job.unifiedPlan?productAxisOccupancy(db):std::vector<ProductAxisOccupant>{};
 std::set<std::string> reservedAxisNumbers;
 for(const auto& occupant:sourceAxisOccupants)
  reservedAxisNumbers.insert(occupant.numbers.begin(),occupant.numbers.end());
 std::vector<bcp::Wall> walls;for(const auto& w:input.walls)walls.push_back(w.wall);const auto views=bcpproduct::discoverGenericViews(walls);require(!views.empty(),"current native plan components");
 productRecordRulePanels(job,views);
 const auto sourceGeometry=job.unifiedPlan?productCapturePlanSourceGeometry(db,input,views):std::vector<ProductPlanSourceGeometry>{};
 const auto sourceOpeningTraces=job.unifiedPlan?productSourceWindowStrokeCandidates(db,views,job.unifiedUnitMM):
  std::vector<ProductSourceMemberCheck>{};
 const auto verticalSource=job.unifiedPlan?productCaptureVerticalCore(db,input,views,!job.architecturalPlanOnly):ProductVerticalCoreSource{};
 auto nameChecks=productPlanNameInventory(db,input,views,original,job.unifiedPlan);
 auto sourceText=job.unifiedPlan?productCaptureSourceText(db,input,original,nameChecks):
  std::vector<ProductSourceTextNote>{};
 auto ownership=job.unifiedPlan?productIdentifyPlanPanels(input,views,job.unifiedUnitMM):
   std::vector<ProductPanelOwnership>{};
 productApplyPlanPanelScope(job,input,views,ownership);
 auto axisViews=views;
 // Keep source component identities for reconciliation. Attached components
 // nevertheless serve their owning floor's axes and perimeter stations.
 for(std::size_t i=0;i<ownership.size();++i)if(ownership[i].parentPanel){
  const auto parent=*ownership[i].parentPanel;
  require(parent<views.size()&&ownership[parent].eligible(),"attached axis service needs an active parent floor");
  auto& target=axisViews[parent];
  target.walls.insert(target.walls.end(),views[i].walls.begin(),views[i].walls.end());
  target.bounds.xmin=std::min(target.bounds.xmin,views[i].bounds.xmin);
  target.bounds.xmax=std::max(target.bounds.xmax,views[i].bounds.xmax);
  target.bounds.ymin=std::min(target.bounds.ymin,views[i].bounds.ymin);
  target.bounds.ymax=std::max(target.bounds.ymax,views[i].bounds.ymax);
 }
 productCapturePlanInventory(job,db,input,views,original,nameChecks,ownership);
 if(job.architecturalPlanOnly)productCaptureOldAnnotations(job,db,views,ownership,sourcePerimeterWitnesses);
 std::vector<bool> selected(views.size());
 if(job.unifiedPlan){
  for(std::size_t i=0;i<views.size();++i)selected[i]=ownership[i].eligible();
  productApplyConfirmedPlanBases(job,db,input);
  productNormalizeDoorJambs(job,db,input,views,original,selected);
 }
 std::vector<ProductRoomWriteReceipt> writtenAreas;
 std::vector<std::vector<GenericNativeTitle>> titles(views.size());for(const auto& t:input.titles)titles[bcpproduct::nearestGenericView(views,{t.anchor.x,t.anchor.y})].push_back(t);
 std::vector<std::vector<GenericNativeColumn>> columns(views.size());for(const auto& c:input.columns)columns[bcpproduct::nearestGenericView(views,{c.centre.x,c.centre.y})].push_back(c);
 const bool named=std::any_of(titles.begin(),titles.end(),[](const auto& t){return !t.empty();});ProductPlanLayoutConfig layout;layout.projectAssociations=false;
 layout.scale=job.unifiedScale;layout.unitMM=job.unifiedUnitMM;
 productPlanLayer(db,L"PUB_DIM",25);
 AcDbDatabase reference(false,true),bank(false,true);productStatus(reference.readDwgFile((job.profile/L"reference.dwg").c_str(),AcDbDatabase::kForReadAndAllShare,false),"read style-only dimension library");productStatus(reference.closeInput(true),"close dimension library input");productStatus(bank.readDwgFile((job.profile/L"axes.dwg").c_str(),AcDbDatabase::kForReadAndAllShare,false),"read style-only axis library");productStatus(bank.closeInput(true),"close axis library input");ensureAxisLayer(reference,db);
 if(job.architecturalPlanOnly){
  std::set<std::string> excludedDimensions;
  for(const auto& [key,item]:job.sourceInventory.items){
   const auto& [panel,scope,handle]=key;
   if(scope=="source-dimension"&&(!panel||!selected[panel-1]))excludedDimensions.insert(handle);
  }
  productSeparateSharedPlanDimensionStyles(job,db,reference,excludedDimensions,layout.scale,layout.unitMM);
 }
 const auto linePrototype=cloneOne(reference,find(reference,"17E4"),db);
 AcDbObjectId actualAxisLinetype;
 double actualAxisScale;
 {
  const auto e=open<AcDbEntity>(linePrototype);
  actualAxisLinetype=e->linetypeId();
  actualAxisScale=e->linetypeScale()*reference.ltscale()/db.ltscale();
 }
 auto logicalLayer=productPlanLayer(db,L"DOTE",24);
 if(job.unifiedPlan){
  const auto layer=open<AcDbLayerTableRecord>(logicalLayer);
  if(layer->isOff()||layer->isFrozen()||layer->isLocked()||!layer->isPlottable()){
   // Preserve visibility of existing axes when the source controls DOTE.
   auto layers=open<AcDbLayerTable>(db.layerTableId());
   std::wstring name=L"DOTE-PLAN";
   for(unsigned suffix=2;layers->has(name.c_str());++suffix)name=L"DOTE-PLAN-"+std::to_wstring(suffix);
   layers.reset();
   layout.logicalAxisLayer=name;
   logicalLayer=productPlanLayer(db,name.c_str(),24);
  }
 }
 {
  auto layer=editObject<AcDbLayerTableRecord>(logicalLayer);
  productStatus(layer->setLinetypeObjectId(actualAxisLinetype),"actual generic axis dash pattern");
 }
 const auto prototype=cloneOne(reference,find(reference,"19A0"),db);AcDbObjectId axisV,axisH;{std::ifstream in(job.profile/L"axes.tsv");std::string tag,hash,dir,label,h;double a,b,c,d;in>>tag>>hash>>tag>>a>>b>>c>>d;while(in>>dir>>label>>h){if(dir=="V"&&label=="1")axisV=find(bank,h);if(dir=="H"&&label=="A")axisH=find(bank,h);}}require(!axisV.isNull()&&!axisH.isNull(),"native generic axis style prototypes");
 const auto module=fs::path(productSetting(L"tangentDirectory",L"C:\\Tangent\\TArchT20V8\\SYS23x64"))/L"tch_kerncmd.arx";if(!acedRegCmds->lookupGlobalCmd(L"TChWinLab")||!acedRegCmds->lookupGlobalCmd(L"TReAxisNo"))require(acrxDynamicLinker->loadModule(module.c_str(),false,false),"load authorized native annotation commands");
 std::vector<GenericFloorGrid> previous;
 std::vector<GenericOpeningCaption> captions;
 std::vector<std::pair<AcDbObjectId,bool>> interior;
 std::vector<bool> generatedViews(views.size());
#ifdef PLAN_STANDALONE_ENTRY
 std::vector<ProductPlanBounds> roomPanels(views.size());
#endif
 std::vector<std::optional<ProductRoomGrid>> roomGrids(views.size());
 std::vector<std::vector<GenericPlanAxis>> exportedAxes(views.size());
 std::vector<ProductPlanFloorAlignment> alignment(views.size());
 std::vector<ProductPlanBounds> sourceBounds(views.size());
 std::vector<bool> eligible(views.size());
 std::vector<std::vector<std::vector<AcGePoint2d>>> sourceSurfaces(views.size());
 std::array<std::vector<double>,2> buildingStations;
 ProductProjectAxes projectAxes;
 if(job.unifiedPlan){
  const auto byFloor=productMainFloorPanels(ownership);
  std::vector<std::vector<bcpproduct::AxisRegistrationPoint>> ties(views.size());
  for(std::size_t view=0;view<views.size();++view){
   if(!ownership[view].floor)continue;
   const int floor=*ownership[view].floor;
   alignment[view].floor=floor;
   if(ownership[view].role!=ProductPanelRole::floor)continue;
   std::vector<bcpproduct::AxisServiceWall> tieWalls;
   for(const auto index:views[view].walls){
    const auto& source=input.walls[index];
    if(source.low||!source.orthogonal||
       (std::abs(source.total-200)>1e-5&&std::abs(source.total-240)>1e-5))continue;
    const auto a=bcp::bodyCentreStart(source.wall);
    const bcp::Point b{a.x+source.wall.end.x-source.wall.start.x,a.y+source.wall.end.y-source.wall.start.y};
    const bool vertical=std::abs(a.x-b.x)<1e-5;
    tieWalls.push_back({vertical,vertical?a.x:a.y,vertical?std::min(a.y,b.y):std::min(a.x,b.x),
                        vertical?std::max(a.y,b.y):std::max(a.x,b.x),source.total/2});
   }
   ties[view]=bcpproduct::axisFloorIntersections(tieWalls);
  }
  const auto ground=byFloor.find(1);
  if(ground!=byFloor.end()&&ground->second.size()==1){
   auto lower=ground->second.front();
   for(int floor=2;;++floor){
    const auto next=byFloor.find(floor);
    if(next==byFloor.end()||next->second.size()!=1)break;
    const auto upper=next->second.front();
    const auto tie=bcpproduct::registerAxisFloor(ties[lower],ties[upper],1.0/layout.unitMM);
    if(!tie)break;
    auto& current=alignment[upper];
    current.lowerView=lower;
    current.tie=tie;
    current.offset={alignment[lower].offset.x+tie->shift.x,alignment[lower].offset.y+tie->shift.y};
    current.registered=alignment[lower].registered=true;
    lower=upper;
   }
  }
  for(std::size_t view=0;view<views.size();++view){
   if(!ownership[view].eligible())continue;
   ProductPlanBounds bounds{1e100,1e100,-1e100,-1e100};
   const auto addPoint=[&](double x,double y){
    bounds.xmin=std::min(bounds.xmin,x);bounds.xmax=std::max(bounds.xmax,x);
    bounds.ymin=std::min(bounds.ymin,y);bounds.ymax=std::max(bounds.ymax,y);
   };
   for(const auto index:axisViews[view].walls){
    const auto& wall=input.walls[index];
    if(wall.low)continue;
    const auto start=bcp::bodyCentreStart(wall.wall);
    addPoint(start.x,start.y);
    addPoint(start.x+wall.wall.end.x-wall.wall.start.x,start.y+wall.wall.end.y-wall.wall.start.y);
   }
   for(const auto& column:columns[view])addPoint(column.centre.x,column.centre.y);
   sourceBounds[view]=bounds;
   eligible[view]=bounds.xmin<bounds.xmax&&bounds.ymin<bounds.ymax;
   if(eligible[view])sourceSurfaces[view]=productGenericProjectionSurfaces(db,input,views,view,bounds);
  }
 }
 const auto stairDirectionMarkers=job.unifiedPlan?productSourceStairDirectionMarkers(db,views,eligible):
  std::vector<ProductStairDirectionMarker>{};
 auto roofGeometry=productPlanRoofGeometry(job,db,input,views,eligible,alignment,sourceBounds,layout.scale,layout.unitMM);
 const auto stairProfile=job.unifiedIndoorStairs.empty()?job.profile/L"plan-policy/indoor-stairs.tsv":job.unifiedIndoorStairs;
 auto indoorStairs=job.unifiedPlan?productIndoorStairs(job,db,views,&reference,layout.unitMM,layout.scale,
  stairProfile,job.sourceHash):std::vector<ProductStairRow>{};
 if(job.unifiedPlan){
  projectAxes=productPrepareAxisProject(job,input,axisViews,columns,alignment,layout.unitMM);
  for(std::size_t view=0;view<views.size();++view){
   const auto& current=alignment[view];
   if(!current.registered){
    if(ownership[view].role==ProductPanelRole::floor&&productMainFloorPanels(ownership).size()>1&&current.floor)job.detailPending.push_back({u8"跨层轴网配准",std::to_string(view+1),
     u8"同一栋连续楼层或三个非共线墙体交点的唯一平移关系未确定，保留本幅独立编号。"});
    continue;
   }
   for(const auto index:axisViews[view].walls){
    const auto& source=input.walls[index];
    if(source.low||!source.orthogonal||
       (std::abs(source.total-200)>1e-5&&std::abs(source.total-240)>1e-5))continue;
    const auto a=bcp::bodyCentreStart(source.wall);
    const bool vertical=std::abs(source.wall.start.x-source.wall.end.x)<1e-5;
    const double station=productAxisProjectStation(projectAxes.walls,view,source.wall.handle,vertical,vertical?a.x:a.y);
    (vertical?buildingStations[0]:buildingStations[1]).push_back(station-(vertical?current.offset.x:current.offset.y));
   }
   for(const auto& column:columns[view]){
    buildingStations[0].push_back(productAxisProjectStation(projectAxes.columns,view,column.handle,true,column.centre.x)-current.offset.x);
    buildingStations[1].push_back(productAxisProjectStation(projectAxes.columns,view,column.handle,false,column.centre.y)-current.offset.y);
   }
  }
  for(auto& stations:buildingStations){
   std::sort(stations.begin(),stations.end());
   stations.erase(std::unique(stations.begin(),stations.end(),[&](double a,double b){
    return std::abs(a-b)<=.01/layout.unitMM;
   }),stations.end());
  }
 }
 std::ostringstream report;report<<std::setprecision(17)<<"{\"schema\":\"bcp.generic-native-plan.v1\",\"sampleCountAdmission\":false,\"architectureCopiedFromSamples\":false,\"views\":[";bool firstView=true;
 struct ObliqueWallAxis {std::size_t panel;std::string wall,line;AcGePoint3d start,end;};
 std::vector<ObliqueWallAxis> obliqueWallAxes;
 for(std::size_t view=0;view<views.size();++view){
  if(job.unifiedPlan&&!ownership[view].eligible()){
   if(ownership[view].role==ProductPanelRole::unassigned)
    job.detailPending.push_back({u8"图幅归属",std::to_string(view+1),ownership[view].reason});
   continue;
  }
  if(!job.unifiedPlan&&named&&titles[view].empty()){job.detailPending.push_back({u8"独立局部图",std::to_string(view+1),u8"未关联主体图名，保留原图；不推定所属楼层，不自动替换主体"});continue;}
  ProductPlanBounds bounds{1e100,1e100,-1e100,-1e100};std::vector<bcpproduct::AxisServiceWall> service;std::vector<bcpproduct::AxisServiceColumn> columnService;
  std::vector<GenericPlanAxis> axes;
  const auto addAxis=[&](bool vertical,double coordinate)->GenericPlanAxis&{
   const auto existing=std::find_if(axes.begin(),axes.end(),[&](const auto& a){return a.vertical==vertical&&std::abs(a.coordinate-coordinate)<1e-5;});
   if(existing!=axes.end())return *existing;
   axes.push_back({vertical,coordinate});
   return axes.back();
  };
  for(auto index:axisViews[view].walls){const auto& w=input.walls[index];if(w.low)continue;const auto a=bcp::bodyCentreStart(w.wall);const bcp::Point b{a.x+w.wall.end.x-w.wall.start.x,a.y+w.wall.end.y-w.wall.start.y};bounds.xmin=std::min({bounds.xmin,a.x,b.x});bounds.xmax=std::max({bounds.xmax,a.x,b.x});bounds.ymin=std::min({bounds.ymin,a.y,b.y});bounds.ymax=std::max({bounds.ymax,a.y,b.y});if(!w.orthogonal){
   job.detailPending.push_back({u8"斜墙轴号",w.wall.handle,u8"原斜墙保留，洞口可按实际方向测量；附加斜轴编号规则待确认"});
   continue;
  }const bool vertical=std::abs(a.x-b.x)<1e-5;const auto station=productAxisProjectStation(projectAxes.walls,view,w.wall.handle,vertical,vertical?a.x:a.y);service.push_back({vertical,station,vertical?std::min(a.y,b.y):std::min(a.x,b.x),vertical?std::max(a.y,b.y):std::max(a.x,b.x),w.total/2});if(std::abs(w.total-200)<1e-5||std::abs(w.total-240)<1e-5||std::abs(station-(vertical?a.x:a.y))>1e-5)addAxis(vertical,station).sourceWalls.push_back(w.wall.handle);}
  auto verticalColumns=columnService,horizontalColumns=columnService;
  for(const auto& c:columns[view]){
   const auto x=productAxisProjectStation(projectAxes.columns,view,c.handle,true,c.centre.x);
   const auto y=productAxisProjectStation(projectAxes.columns,view,c.handle,false,c.centre.y);
   bounds.xmin=std::min(bounds.xmin,x);bounds.xmax=std::max(bounds.xmax,x);bounds.ymin=std::min(bounds.ymin,y);bounds.ymax=std::max(bounds.ymax,y);
   addAxis(true,x).sourceColumns.push_back(c.handle);addAxis(false,y).sourceColumns.push_back(c.handle);
   columnService.push_back({c.centre.x,c.centre.y,c.halfX,c.halfY});
   verticalColumns.push_back({x,c.centre.y,c.halfX,c.halfY});horizontalColumns.push_back({c.centre.x,y,c.halfX,c.halfY});
  }
  if(bounds.xmin>=bounds.xmax||bounds.ymin>=bounds.ymax){job.detailPending.push_back({u8"平面范围",std::to_string(view+1),u8"缺少可确定的主体墙柱范围，保留该局部，其他平面继续"});continue;}
  for(auto& a:axes){
   if(job.unifiedPlan){
    const auto support=bcpproduct::resolveAxisSideService(a.vertical,a.coordinate,service,a.vertical?verticalColumns:horizontalColumns);
    if(support){
     a.negative=a.showNegative=support->negative;
     a.positive=a.showPositive=support->positive;
    }
    continue;
   }
   std::vector<std::pair<double,double>> ranges;
   for(const auto& w:service)if(w.vertical==a.vertical&&std::abs(w.coordinate-a.coordinate)<1e-5)ranges.emplace_back(w.lower,w.upper);
   for(const auto& c:columnService)if(std::abs((a.vertical?c.x:c.y)-a.coordinate)<1e-5)ranges.emplace_back(a.vertical?c.y:c.x,a.vertical?c.y:c.x);
   const auto sides=bcp::axisSides(ranges,a.vertical?(bounds.ymin+bounds.ymax)/2:(bounds.xmin+bounds.xmax)/2);
   a.negative=a.showNegative=sides.negative;a.positive=a.showPositive=sides.positive;
  }
  const auto surfaces=job.unifiedPlan?sourceSurfaces[view]:productGenericProjectionSurfaces(db,input,views,view,bounds);
  const auto projection=productMatchGenericProjection(bounds,axes,previous,surfaces);
  const auto& registered=alignment[view];
  const bool registeredProjection=projection&&registered.registered&&alignment[projection->reference].registered&&
   std::abs(projection->shift.x-(registered.offset.x-alignment[projection->reference].offset.x))<=1./layout.unitMM&&
   std::abs(projection->shift.y-(registered.offset.y-alignment[projection->reference].offset.y))<=1./layout.unitMM;
  if(job.unifiedPlan)productApplyRetreatAxes(projectAxes,view,bounds,input,views[view],axes,.01/layout.unitMM);
  if(!surfaces.empty()&&!projection)job.detailPending.push_back({u8"屋顶露台轴网",std::to_string(view+1),u8"已读到室外投影边界；下层共同轴位未唯一匹配，保留本层已知轴网"});
  const auto annotation=projection?projection->bounds:bounds;
  const int panel=static_cast<int>(layout.panels.size());
  generatedViews[view]=true;
  job.layoutSourcePanels.push_back(view+1);
#ifdef PLAN_STANDALONE_ENTRY
  roomPanels[view]=bounds;
#endif
  layout.panels.push_back(annotation);
  layout.placementAxes.push_back(bounds);
  if(job.unifiedPlan){
   const auto& original=views[view].bounds;
   const ProductPlanBounds originalFacade{original.xmin,original.ymin,original.xmax,original.ymax};
   productStageOldPerimeterDimensions(job,db,layout,panel,view+1,&originalFacade);
  }
  std::sort(axes.begin(),axes.end(),[](const auto& a,const auto& b){return a.vertical!=b.vertical?a.vertical>b.vertical:a.coordinate<b.coordinate;});
  if(alignment[view].registered){
   const auto& offset=alignment[view].offset;
   std::array<std::set<std::size_t>,2> matchedStations;
   for(auto& axis:axes){
    axis.label.clear();
    const auto direction=axis.vertical?0:1;
    const auto& stations=buildingStations[direction];
    const auto position=axis.coordinate-(axis.vertical?offset.x:offset.y);
    const auto match=std::lower_bound(stations.begin(),stations.end(),position-.01/layout.unitMM);
    if(match==stations.end()||std::abs(*match-position)>.01/layout.unitMM)continue;
    const auto index=static_cast<std::size_t>(match-stations.begin());
    if(!matchedStations[direction].insert(index).second)continue;
    const auto candidate=axis.vertical?std::to_string(index+1):bcp::alphabetLabel(index).value_or("");
    if(!reservedAxisNumbers.count(candidate))axis.label=candidate;
   }
  }
  std::set<std::string> occupied=reservedAxisNumbers;
  if(alignment[view].registered){
   for(std::size_t i=0;i<buildingStations[0].size();++i)occupied.insert(std::to_string(i+1));
   for(std::size_t i=0;i<buildingStations[1].size();++i){
    const auto label=bcp::alphabetLabel(i);
    if(label)occupied.insert(*label);
   }
  }
  for(const auto& a:axes)if(!a.label.empty())occupied.insert(a.label);
  std::size_t nx=0,ny=0;
  if(alignment[view].registered){nx=buildingStations[0].size();ny=buildingStations[1].size();}
  for(auto& a:axes)if(a.label.empty()){
   do {a.label=a.vertical?std::to_string(++nx):bcp::alphabetLabel(ny++).value_or("");}
   while(!a.label.empty()&&occupied.count(a.label));
   occupied.insert(a.label);
  }
  if(!job.unifiedPlan)for(bool vertical:{true,false})for(bool negative:{true,false}){std::vector<std::size_t> indices;std::vector<double> positions,reaches;for(std::size_t i=0;i<axes.size();++i){const auto& a=axes[i];if(a.vertical!=vertical||!(negative?a.negative:a.positive))continue;double reach=negative?1e100:-1e100;for(const auto& w:service)if(w.vertical==vertical&&std::abs(w.coordinate-a.coordinate)<1e-5)reach=negative?std::min(reach,w.lower):std::max(reach,w.upper);for(const auto& c:columnService)if(std::abs((vertical?c.x:c.y)-a.coordinate)<1e-5)reach=negative?std::min(reach,vertical?c.y:c.x):std::max(reach,vertical?c.y:c.x);indices.push_back(i);positions.push_back(a.coordinate);reaches.push_back(reach);}const auto keep=bcp::outerAxisLabels(positions,reaches,negative,800);for(std::size_t i=0;i<indices.size();++i)if(!keep[i]){if(negative)axes[indices[i]].showNegative=false;else axes[indices[i]].showPositive=false;}}
  for(auto& a:axes){
   if(a.label.empty()||(!a.showNegative&&!a.showPositive))continue;
   AcDbObjectId id;
   try{
   id=cloneOne(bank,a.vertical?axisV:axisH,db);
   const auto g=chainGrips(id);
   require(g.size()==8,"axis template has no two readable ends");
   {auto e=editObject<AcDbEntity>(id);productStatus(e->transformBy(AcGeMatrix3d::translation(a.vertical?AcGeVector3d(a.coordinate-g[0].x,0,0):AcGeVector3d(0,a.coordinate-g[0].y,0))),"place current logical axis");}
   trialPositionAxis(id,a.vertical,a.vertical?annotation.ymin:annotation.xmin,a.vertical?annotation.ymax:annotation.xmax,3600,true);productGenericAxisNumber(id,a.label,a.vertical);
   a.labelHandle=axisHandle(id);
   if(a.showNegative!=a.showPositive){
    layout.keepNegativeAxisEnd.emplace(a.labelHandle,a.showNegative);
    job.axisSideCommandUsed=true;
   }
   layout.axes.push_back({a.labelHandle,panel,a.vertical?1:0,0});
   }catch(const std::exception& error){
    if(!job.unifiedPlan)throw;
    if(!id.isNull()&&!id.isErased())productStatus(editObject<AcDbEntity>(id)->erase(),"remove failed new axis label");
    productRecordPlanAxisFailure(job,view+1,a.label,id.isNull()?std::string{}:axisHandle(id),error.what());
   }
  }
  // Logical lines exist independently of which sides display an axis circle.
  if(job.architecturalPlanOnly)for(const auto index:axisViews[view].walls){
   const auto& wall=input.walls[index];
   const auto thickness=wall.total*layout.unitMM;
   if(wall.low||wall.orthogonal||(std::abs(thickness-200)>=.01&&std::abs(thickness-240)>=.01))continue;
   if(!job.sourceInventory.items.count({view+1,"wall-axis",wall.wall.handle}))continue;
   const auto entity=open<AcDbEntity>(find(db,wall.wall.handle));
   const auto* curve=AcDbCurve::cast(entity.get());
   AcGePoint3d rawStart,rawEnd,mid;
   double distance{};
   const auto eps=.01/layout.unitMM;
   if(!curve||curve->getStartPoint(rawStart)!=Acad::eOk||curve->getEndPoint(rawEnd)!=Acad::eOk||
      curve->getDistAtPoint(rawEnd,distance)!=Acad::eOk||distance<=eps||
      curve->getPointAtDist(distance*.5,mid)!=Acad::eOk||
      std::abs(distance-rawStart.distanceTo(rawEnd))>eps||
      mid.distanceTo(rawStart+(rawEnd-rawStart)*.5)>eps)continue;
   const auto center=bcp::bodyCentreStart(wall.wall);
   const AcGePoint3d start(center.x,center.y,0.),end(center.x+rawEnd.x-rawStart.x,center.y+rawEnd.y-rawStart.y,0.);
   AcDbObjectId existing;
   bool partialOverlap=false;
   const auto axisDirection=(end-start).normal();
   for(const auto& [raw,type]:modelTypes(db)){
    if(type!=L"AcDbLine")continue;
    const auto id=find(db,ascii(raw));
    const auto line=open<AcDbLine>(id);
    if(line->layerId()!=logicalLayer||line->linetypeId()!=actualAxisLinetype||
       !productPlanDimensionVisible(*line))continue;
    const auto collinear=[&](const auto& point){return (point-start).crossProduct(axisDirection).length()<=eps;};
    if(!collinear(line->startPoint())||!collinear(line->endPoint()))continue;
    const auto a=(line->startPoint()-start).dotProduct(axisDirection);
    const auto b=(line->endPoint()-start).dotProduct(axisDirection);
    const auto length=start.distanceTo(end);
    if(std::abs(std::min(a,b))<=eps&&std::abs(std::max(a,b)-length)<=eps)existing=id;
    else if(std::min(std::max(a,b),length)-std::max(std::min(a,b),0.)>eps)partialOverlap=true;
   }
   if(existing.isNull()&&partialOverlap)continue;
   if(existing.isNull()){
    auto line=std::make_unique<AcDbLine>(start,end);
    line->setDatabaseDefaults(&db);
    productStatus(line->setLayer(logicalLayer),"use actual logical axis layer for oblique wall");
    productStatus(line->setColorIndex(256),"use logical axis layer colour");
    productStatus(line->setLinetype(actualAxisLinetype),"use actual DOTE linetype for oblique wall");
    productStatus(line->setLinetypeScale(actualAxisScale),"use logical axis scale");
    existing=productAppendPlanDetail(db,std::move(line));
   }
   obliqueWallAxes.push_back({view+1,wall.wall.handle,axisHandle(existing),start,end});
  }
  for(auto& a:axes){
   auto ranges=a.projectedRanges;
   if(job.unifiedPlan){
    const auto support=bcpproduct::resolveAxisSideService(a.vertical,a.coordinate,service,a.vertical?verticalColumns:horizontalColumns);
    if(support&&support->span.upper-support->span.lower>1e-5){
     ranges.emplace_back(support->span.lower,support->span.upper);
    }
    if(!a.negative&&!a.positive){
     std::ostringstream where;
     where<<std::setprecision(17)<<"view "<<view+1<<" / "<<(a.vertical?"X":"Y")<<'='<<a.coordinate;
     job.detailPending.push_back({u8"局部轴定位",where.str(),
      u8"轴按实际墙柱局部范围表达；未确认外围投影，需核实局部定位尺寸和轴号引用。"});
    }
   }else{
    const auto span=bcpproduct::inferAxisService(a.vertical,a.coordinate,a.vertical?bounds.ymin:bounds.xmin,a.vertical?bounds.ymax:bounds.xmax,1800,service,columnService);
    if(span)ranges.emplace_back(span->span.lower,span->span.upper);
   }
   if(ranges.empty()){job.detailPending.push_back({u8"逻辑轴线",a.label,u8"有效范围未确定；未默认贯穿整栋"});continue;}
   a.sourceRanges=ranges;
   std::sort(ranges.begin(),ranges.end());
   std::vector<std::pair<double,double>> joined;
   for(auto range:ranges){
    if(joined.empty()||range.first>joined.back().second+1e-5)joined.push_back(range);
    else joined.back().second=std::max(joined.back().second,range.second);
   }
   if(!job.unifiedPlan&&!a.projectedRanges.empty()){
    if(a.showNegative)joined.front().first=std::min(joined.front().first,(a.vertical?annotation.ymin:annotation.xmin)-1800);
    if(a.showPositive)joined.back().second=std::max(joined.back().second,(a.vertical?annotation.ymax:annotation.xmax)+1800);
   }
   for(auto range:joined){
    auto line=std::make_unique<AcDbLine>(a.vertical?AcGePoint3d(a.coordinate,range.first,0):AcGePoint3d(range.first,a.coordinate,0),
                                       a.vertical?AcGePoint3d(a.coordinate,range.second,0):AcGePoint3d(range.second,a.coordinate,0));
    line->setDatabaseDefaults(&db);
    productStatus(line->setLayer(logicalLayer),"set actual logical axis layer");
    line->setColorIndex(256);
    line->setLinetype(actualAxisLinetype);line->setLinetypeScale(actualAxisScale);
    auto model=editObject<AcDbBlockTableRecord>(modelId(db));AcDbObjectId id;
    productStatus(model->appendAcDbEntity(id,line.get()),"append independent actual logical axis");
    a.writtenLines.push_back({axisHandle(id),{line->startPoint(),line->endPoint()}});
    line.release()->close();
   }
  }
  for(int side=0;side<4;++side){
   const bool vertical=side<2;
   const bool negative=side==0||side==2;
   const double low=vertical?annotation.ymin:annotation.xmin;
   const double high=vertical?annotation.ymax:annotation.xmax;
   const double normal=productPlanDatum(annotation,side);
   std::vector<double> stations;
   if(!job.unifiedPlan)stations={low,high};
   for(const auto& a:axes){
    if(a.vertical==vertical||!(negative?a.negative:a.positive))continue;
    if(job.unifiedPlan||(a.coordinate>low+1e-5&&a.coordinate<high-1e-5))stations.push_back(a.coordinate);
   }
   std::sort(stations.begin(),stations.end());
   stations.erase(std::unique(stations.begin(),stations.end(),[](double a,double b){return std::abs(a-b)<1e-5;}),stations.end());
   if(job.unifiedPlan){
    auto& expected=layout.expectedPerimeter[{panel,side}];
    expected.stations=stations;
    expected.axisScopeKnown=surfaces.empty();
    if(!expected.axisScopeKnown)expected.axisScopeReason=!projection?
     u8"已识别室外轮廓，尚无唯一匹配的共同轴位":
     registeredProjection?u8"投影平移与跨层配准相符，仍须逐段证明屋面、露台或挑空的边界用途及本侧服务":
      u8"投影候选与共同墙柱配准未互证，须确认退台边界及实际服务轴";
    for(const auto& axis:axes){
     if(axis.vertical==vertical||!(negative?axis.negative:axis.positive))continue;
     auto& lines=expected.axisLines[axis.coordinate];
     for(const auto& line:axis.writtenLines)lines.push_back(line.first);
    }
    for(const auto index:views[view].walls){
     const auto& wall=input.walls[index];
     if(wall.low||!wall.orthogonal)continue;
     const auto a=bcp::bodyCentreStart(wall.wall);
     const bool wallVertical=std::abs(wall.wall.start.x-wall.wall.end.x)<1e-5;
     const double point=wallVertical?a.x:a.y;
     if(wallVertical!=vertical&&std::any_of(stations.begin(),stations.end(),[&](double station){
      return std::abs(station-point)<=.01/layout.unitMM;
     }))expected.sourceObjects.push_back(wall.wall.handle);
    }
    for(const auto& column:columns[view]){
     const double point=vertical?column.centre.y:column.centre.x;
     if(std::any_of(stations.begin(),stations.end(),[&](double station){
      return std::abs(station-point)<=.01/layout.unitMM;
     }))expected.sourceObjects.push_back(column.handle);
    }
   }
   if(stations.size()<2){
    job.detailPending.push_back({u8"外围轴间尺寸",std::to_string(view+1)+"/"+std::to_string(side),
      u8"本侧不足两个有依据的外围轴站，保留已有尺寸待核对。"});
    continue;
   }
   std::vector<AcGePoint3d> points;
   for(auto station:stations)points.emplace_back(vertical?normal:station,vertical?station:normal,0);
   for(int row=1;row<=2;++row){
    const auto samples=row==1?std::vector<AcGePoint3d>{points.front(),points.back()}:points;
    const auto before=job.unifiedPlan?wholeModelIds(db):std::set<AcDbObjectId>{};
    try{
     const auto id=productGenericChain(job,db,prototype,samples,(side==0||side==3)?1000:-1000);
     layout.dimensions.push_back({axisHandle(id),panel,side,row});
    }catch(const std::runtime_error& error){
     if(!job.unifiedPlan)throw;
     for(const auto& id:wholeModelIds(db))if(!before.count(id))
      productStatus(editObject<AcDbEntity>(id)->erase(),"roll back failed new perimeter chain");
     require(wholeModelIds(db)==before,"perimeter command affected an existing model object");
     const auto sideKey=std::string(1,"WESN"[side]);
     const auto reason=u8"第"+std::to_string(row)+u8"道新链生成失败，保留源链。"+error.what();
     productCheckPlanSource(job,view+1,"perimeter",sideKey,bcpproduct::PlanCheck::failed,{},reason);
     job.detailPending.push_back({u8"外围尺寸局部失败",std::to_string(view+1)+"/"+sideKey,reason});
     break;
    }
   }
  }
  std::vector<ProductOpeningAxisTerminal> openingAxisTerminals;
  if(job.unifiedPlan)for(const auto& axis:axes){
   if(axis.sourceWalls.empty()||axis.writtenLines.empty())continue;
   for(const auto& range:axis.sourceRanges)
    openingAxisTerminals.push_back({axis.vertical,axis.coordinate,range.first,range.second,axis.sourceWalls.front()});
  }
  productGenericOpenings(job,db,input,views,view,bounds,panel,prototype,layout,captions,interior,openingAxisTerminals);
  if(job.unifiedPlan)productApplyAxisDetails(job,db,projectAxes,view,panel,input,views[view],bounds,axes,prototype,layout);
  productGenericRooms(job,db,input,views,view,bounds,surfaces,panel,reference,layout.scale,layout.unitMM,nameChecks,
                      job.unifiedPlan,&writtenAreas
#ifdef PLAN_STANDALONE_ENTRY
                      ,job.confirmedRooms,
                      job.unifiedPlan?&roomGrids[view]:nullptr
#endif
                      );
  if(job.unifiedPlan)productFinishOldPerimeterDimensions(job,db,layout,panel);
  else for(const auto& item:original)if(item.second==L"TDbDimension2"){
   const auto id=find(db,ascii(item.first));
   const auto gp=chainGrips(id);
   if(gp.size()<4)continue;
   if(bcpproduct::nearestGenericView(views,{gp[0].x,gp[0].y})!=view)continue;
   const bool outside=gp[0].x<bounds.xmin||gp[0].x>bounds.xmax||gp[0].y<bounds.ymin||gp[0].y>bounds.ymax;
   if(!outside)continue;
   auto e=editObject<AcDbEntity>(id);
   productStatus(e->setVisibility(AcDb::kInvisible),"suppress replaced source perimeter dimension");
  }
  if(!firstView)report<<',';firstView=false;report<<"{\"component\":"<<view+1<<",\"panel\":"<<panel<<",\"walls\":"<<views[view].walls.size()<<",\"columns\":"<<columns[view].size()<<",\"axisCandidates\":"<<axes.size()<<",\"bounds\":["<<annotation.xmin<<','<<annotation.ymin<<','<<annotation.xmax<<','<<annotation.ymax<<"],\"projectedReference\":";
  if(projection)report<<projection->reference;else report<<"null";
  report<<",\"projectionSourceContours\":[";
  for(std::size_t i=0;i<surfaces.size();++i){
   if(i)report<<',';
   std::vector<AcGePoint3d> points;
   for(const auto& point:surfaces[i])points.emplace_back(point.x,point.y,0);
   report<<wholePoints(points);
  }
  report<<"],\"projectionRegistrationVerified\":"<<(registeredProjection?"true":"false");
  if(projection)report<<",\"projectionShiftXY\":["<<projection->shift.x<<','<<projection->shift.y<<']';
  report<<"}";
  exportedAxes[view]=axes;
  previous.push_back({bounds,axes,view});
 }
 // Four paper-controlled helper frames per generated independent plan.
 for(std::size_t p=0;p<layout.panels.size();++p)for(int row=0;row<4;++row){auto frame=std::make_unique<AcDbPolyline>(4);frame->setDatabaseDefaults(&db);for(int k=0;k<4;++k)frame->addVertexAt(k,AcGePoint2d(k%2,k/2));frame->setClosed(true);auto model=editObject<AcDbBlockTableRecord>(modelId(db));AcDbObjectId id;productStatus(model->appendAcDbEntity(id,frame.get()),"generic four control frames");frame.release()->close();layout.frames.push_back({axisHandle(id),static_cast<int>(p),0,row});}
 require(!layout.panels.empty(),u8"E_PLAN_COMPONENTS：未确定可生成的平面；已保留全部输入对象。");
 stamp("inventory-floor-geometry-and-initial-layout");
 if(job.validation)writeNew(job.root/L"native-00-axis-layout.step","entered");
 layout.paper=productLoadPlanPaperPolicy(job,layout.scale,layout.unitMM);
 productResolvePlanPlacement(job,db,layout);
 productPlanLayout(db,layout,[&](const auto& h){return find(db,h);},{},job.unifiedPlan?&job:nullptr);
 if(job.unifiedPlan){
  int panel=0;
  for(std::size_t view=0;view<exportedAxes.size();++view){
   if(!generatedViews[view])continue;
   for(auto& axis:exportedAxes[view]){
    if(axis.labelHandle.empty()||axis.writtenLines.empty())continue;
    for(const auto& role:layout.axes){
     if(role.panel!=panel)continue;
     const auto source=layout.axisOriginalHandles.find(role.handle);
     const auto& originalHandle=source==layout.axisOriginalHandles.end()?role.handle:source->second;
     if(originalHandle!=axis.labelHandle)continue;
     const auto grips=chainGrips(find(db,role.handle));
     for(std::size_t i=0;i+1<grips.size();i+=4){
      const auto side=productAxisEnd(productPlacementBounds(layout,panel),axis.vertical,grips[i],grips[i+1]).side;
      const bool negative=side%2==0;
      if(!(negative?axis.negative:axis.positive))continue;
      const auto fold=productFoldPoints(db,layout,role.handle,side);
      const auto anchor=fold.empty()?grips[i]:fold.front();
      if(std::abs(productAxisStation(anchor,axis.vertical)-axis.coordinate)>1e-5)continue;
      auto& line=negative?axis.writtenLines.front():axis.writtenLines.back();
      auto& point=negative?line.second.first:line.second.second;
      const double along=axis.vertical?anchor.y:anchor.x;
      const double old=axis.vertical?point.y:point.x;
      if(negative?along>=old-1e-5:along<=old+1e-5)continue;
      const auto target=axis.vertical?AcGePoint3d(axis.coordinate,along,point.z):AcGePoint3d(along,axis.coordinate,point.z);
      auto entity=editObject<AcDbLine>(find(db,line.first));
      productStatus(negative?entity->setStartPoint(target):entity->setEndPoint(target),"connect proved logical axis to actual label anchor");
      point=target;
     }
    }
   }
   ++panel;
  }
 }
 stamp("axis-and-perimeter-layout");
 if(job.validation)writeNew(job.root/L"native-00-roof.step","entered");
 auto roof=productWritePreparedRoof(job,db,std::move(roofGeometry),generatedViews,layout,exportedAxes,prototype);
 for(const auto& copy:roof.copies)if(copy.row)layout.projectedPerimeter.insert(copy.handle);
 for(const auto& side:roof.existingAxisSides)
  for(const auto& handle:side.dimensions)if(!handle.empty())layout.projectedPerimeter.insert(handle);
 for(const auto& c:input.columns){auto e=editObject<AcDbEntity>(find(db,c.handle));e->setLayer(productPlanLayer(db,L"COLUMN",140));e->setColorIndex(256);}for(const auto& w:input.walls)if(!w.low){auto e=editObject<AcDbEntity>(find(db,w.wall.handle));e->setLayer(productPlanLayer(db,L"WALL",140));e->setColorIndex(256);}
 if(job.validation)writeNew(job.root/L"native-00-caption-height.step","entered");
 const bool fontConfirmed=productNativeLabelHeight(db,job.genericCaptionIds);writeNew(job.root/L"通用编号字高核验.json",std::string("{\"publicPropertiesField\":\"文字高度\",\"nominalPaperHeight\":3.0,\"nativeOpeningCount\":")+std::to_string(job.genericCaptionIds.size())+",\"actualReadbackConfirmed\":"+(fontConfirmed?"true":"false")+"}");if(!fontConfirmed)job.detailPending.push_back({u8"门窗编号字高",u8"已生成编号",u8"公开特性字段未全部读回纸面3.0，保留真实编号并列待调整；不以文字样式高度冒充编号本体已修改"});
 if(job.validation)writeNew(job.root/L"native-01-caption-placement.step","entered");
 stamp("roof-and-opening-height");
 productGenericRecenterCaptions(job,db,captions);
 productPrepareCaptionLayer(db,captions,std::all_of(generatedViews.begin(),generatedViews.end(),[](bool value){return value;}));
 productPlanFrameAppearance(db,layout);
 stamp("opening-label-placement-and-frame");
 // Removing masks must not relocate witnesses against the old global envelope.
 productLatestStyle(db,false);
 const auto furnitureExclusions=productPlanConfirmedNonFurnitureOutlines(job,db,input,layout.unitMM);
 const auto furniture=productApplyPlanFurniture(job,db,furnitureExclusions);
 const auto sourceCleanup=productCleanConfirmedSourceDuplicates(job,db,views,layout.unitMM);
 stamp("furniture-and-source-cleanup");
 {auto e=editObject<AcDbEntity>(linePrototype);productStatus(e->erase(),"retire temporary axis style carrier");}
 {auto e=editObject<AcDbEntity>(prototype);productStatus(e->erase(),"retire temporary annotation-only prototype");}
 if(job.unifiedPlan){
  for(const auto& [key,item]:job.sourceInventory.items){
   const auto& [panel,scope,handle]=key;
   if(scope=="source-dimension"&&(!panel||!generatedViews[panel-1]))
    job.planDimensionExclusions.insert(handle);
  }
 }else if(std::find(generatedViews.begin(),generatedViews.end(),false)!=generatedViews.end()){
  const auto alive=modelTypes(db);
  for(const auto& [handle,type]:original){
   const bool native=type==L"TDbDimension2";
   if((!native&&type.find(L"Dimension")==std::wstring::npos)||!alive.count(handle))continue;
   const auto key=ascii(handle);
   try{
    const auto id=find(db,key);
    auto entity=open<AcDbEntity>(id);
    AcGePoint3d at;
    if(native){
     const auto grips=chainGrips(id);
     require(grips.size()>=3,"source dimension position");
     at=grips[2];
    }else{
     if(!AcDbDimension::cast(entity.get()))continue;
     AcDbExtents box;
     productStatus(entity->getGeomExtents(box),"source dimension position");
     at=box.minPoint()+(box.maxPoint()-box.minPoint())*.5;
    }
    if(generatedViews[bcpproduct::nearestGenericView(views,{at.x,at.y})])continue;
   }catch(const std::exception&){
    job.detailPending.push_back({u8"尺寸归属",key,u8"无法确认原尺寸所属平面，保留原表达"});
   }
   job.planDimensionExclusions.insert(key);
  }
 }
 const auto balconyEdges=productGenerateBalconyEdges(job,db,input,views,layout.unitMM);
 stamp("remaining-source-dimensions-and-balcony-edges");
#ifdef PLAN_STANDALONE_ENTRY
 if(!job.confirmedRooms.empty()){
  std::vector<ProductPlanBounds> roomPanels;
  for(const auto& view:views)roomPanels.push_back({view.bounds.xmin,view.bounds.ymin,view.bounds.xmax,view.bounds.ymax});
  for(const auto& room:job.confirmedRooms)
   require(room.panel>0&&static_cast<std::size_t>(room.panel)<=selected.size()&&selected[room.panel-1],
       "confirmed room must belong to a selected main floor");
  applyConfirmedPlanRooms(job,db,job.confirmedRooms,roomPanels,nameChecks,layout.scale,layout.unitMM,writtenAreas);
 }
#endif
 stamp("confirmed-room-area-placement");
 std::vector<std::optional<int>> floorNumbers;
 for(const auto& entry:alignment)floorNumbers.push_back(entry.floor);
 auto levelViews=generatedViews;
 if(job.unifiedPlan)for(std::size_t i=0;i<levelViews.size();++i)
  levelViews[i]=levelViews[i]&&ownership[i].eligible();
 std::vector<bcpproduct::PlanLevelResult> levels;
 const auto datums=productGeneratePlanDatums(job,db,input,views,levelViews,floorNumbers,roomGrids,levels);
 stamp("floor-datums");
 const auto wallHeights=productApplyWallHeights(job,db,input,axisViews,levelViews,levels);
 stamp("native-wall-height-writes");
 if(job.unifiedPlan)productStorePlanLevels(db,levels,levelViews);
 const auto detailNotes=productGenerateDetailNotes(job,db,views,generatedViews);
 auto entranceSteps=productEntranceSteps(job,db,input,views,layout.unitMM,
  job.unifiedEntranceSteps.empty()?job.profile/L"plan-policy/entrance-steps.tsv":job.unifiedEntranceSteps,
  job.sourceHash,&reference,layout.scale,job.unifiedPlan?&generatedViews:nullptr);
 for(auto& entrance:entranceSteps)for(const auto& caption:captions)
  if(axisHandle(caption.id)==entrance.door)entrance.number=caption.number;
 if(!job.unifiedPlan)indoorStairs=productIndoorStairs(job,db,views,&reference,layout.unitMM,layout.scale,
  stairProfile,job.sourceHash);
 if(job.unifiedPlan)productRestoreVerticalDrawOrder(db,verticalSource,indoorStairs);
 stamp("detail-notes-entrance-and-draw-order");
 productApplyPlanDimensionStyle(job,db,layout.scale,layout.unitMM);
 if(productNormalizeSourceNativeDimensionStyles(job,db,reference,views,generatedViews,layout.scale,layout.unitMM))
  productApplyPlanDimensionStyle(job,db,layout.scale,layout.unitMM);
 const auto previousInterior=interior.size();
 productResolveLooseDimensions(job,db,reference,views,generatedViews,interior);
 if(interior.size()!=previousInterior)productApplyPlanDimensionStyle(job,db,layout.scale,layout.unitMM);
 if(job.architecturalPlanOnly){
  for(const auto& issue:productNormalizeWholePlanTypography(job,db,reference))
   job.detailPending.push_back({u8"排版前字体恢复",issue.object,issue.reason});
 }
 const auto titleTextStyles=job.unifiedPlan?
  productNormalizePlanTitleText(job,db,reference,input,original,views,generatedViews):std::vector<ProductPlanTitleTextStyle>{};
 approvedTitleClearance(job,db);productGenericTitleClearance(job,db);
 if(job.validation)writeNew(job.root/L"native-03-area-summaries.step","entered");
 stamp("dimension-styles-loose-dimensions-and-titles");
 auto areaSummaries=productRestylePlanAreaSummaries(job,db,reference,input,views,generatedViews);
 const auto grossAreas=productCaptureGrossAreas(job,db,areaSummaries,views);
 const auto frameMoves=productClearPlanFrameGroups(job,db);
 productSeedPlanDimensionText(job,db,layout.scale/layout.unitMM);
 std::map<std::string,std::wstring> sourceTextCaptions;
 for(const auto& caption:captions)sourceTextCaptions.emplace(axisHandle(caption.id),caption.number);
 const auto placeSourceText=[&]{
  productPlaceSourceText(job,db,views,generatedViews,roomGrids,layout.scale,layout.unitMM,sourceTextCaptions,sourceText);
 };
 placeSourceText();
 if(job.validation)writeNew(job.root/L"native-04-dimension-layout.step","entered");
 std::function<void(AcDbDatabase&)> verifyRoomDimensions;
 bool jointRoomLayout=false;
#ifdef PLAN_STANDALONE_ENTRY
 jointRoomLayout=job.unifiedPlan;
#endif
 if(!jointRoomLayout)for(const auto& [id,vertical]:interior)
  productGenericInteriorClearance(job,db,id,vertical,layout.scale/layout.unitMM);
 if(job.validation)writeNew(job.root/L"native-05-detail-dimensions.step","entered");
 ProductPerimeterGrips perimeterDefinition;
 productGenericPerimeterTextClearance(job,db,layout,false,jointRoomLayout?&perimeterDefinition:nullptr);
 productArrangeEntranceDimensions(job,db,entranceSteps,layout.scale,layout.unitMM);
 productArrangeExistingRoofDimensions(job,db,roof,layout.scale,layout.unitMM);
 productPlaceRoofDrainSchedule(job,db,roof,layout.scale/layout.unitMM);
 productPlaceExistingRoofTitle(db,roof,layout.scale/layout.unitMM);
 if(!jointRoomLayout)productArrangeIndoorStairDimensions(job,db,indoorStairs,layout.scale,layout.unitMM);
#ifdef PLAN_STANDALONE_ENTRY
 if(job.validation)writeNew(job.root/L"native-06-sheet-layout.step","entered");
 stamp("area-summaries-source-notes-and-detail-dimensions");
 productWritePlanSheetLinks(job,db,layout.scale/layout.unitMM);
 stamp("archived-paper-layouts");
#endif

 auto movedAreaNotes=job.unifiedPlan?productRepairAreaNoteClearance(db,layout):
  std::vector<ProductPlanAreaNoteMove>{};
 for(const auto& move:movedAreaNotes)for(auto& summary:areaSummaries)
  if(summary.output==move.handle)summary.anchor=move.anchor;
 if(job.validation)writeNew(job.root/L"native-07-paper-dimension-repair.step","entered");
 stamp("area-summary-clearance");
 const auto paperDimensionRepairs=job.unifiedPlan?productRepairPaperDimensionText(job,db,roof,layout.scale/layout.unitMM):
  std::vector<ProductPlanTextRepair>{};
 for(const auto& repair:paperDimensionRepairs)for(auto& item:roof.imported){
  if(item.handle!=repair.handle)continue;
  require(item.points.size()==repair.before.size()&&std::equal(item.points.begin(),item.points.end(),repair.before.begin(),
   [](const auto& a,const auto& b){return a.distanceTo(b)<=1e-5;}),"roof text repair starts at registered geometry");
  const auto count=repair.values.size();
  for(std::size_t k=0;k<repair.before.size();++k)
   if(k<count+3||k>=2*count+3)
    require(repair.before[k].distanceTo(repair.after[k])<=1e-5,"roof text repair preserves measurement and baseline grips");
  item.points=repair.after;
 }
 if(job.unifiedPlan&&!jointRoomLayout)productCompactRemainingCaptionConflicts(job,db,captions);
 if(job.unifiedPlan)productArrangeWindowExteriorNoteClearance(job,db,captions,layout.scale/layout.unitMM);
 if(job.validation)writeNew(job.root/L"native-08-vertical-notes.step","entered");
 if(job.unifiedPlan)productWriteStairVerticalNotes(job,db,indoorStairs,views,layout.scale/layout.unitMM);
 if(job.validation)writeNew(job.root/L"native-09-note-layout.step","entered");
 const auto looseNotes=job.unifiedPlan?productArrangeConstructionNotes(job,db,entranceSteps,roof,layout.scale/layout.unitMM):
  std::vector<ProductLooseNotePlacement>{};
 const auto titlePositions=productArrangePlanTitles(job,db,input,views,layout.scale/layout.unitMM,areaSummaries,movedAreaNotes);
 for(const auto& group:titlePositions)for(auto& note:sourceText)
  if(std::find(group.group.begin(),group.group.end(),note.handle)!=group.group.end()){
   note.shift+=group.shift;
   note.editable=false;
  }
#ifdef PLAN_STANDALONE_ENTRY
 // Joint layout must see the visible captions, level markers and construction
 // notes in their final positions. Later room edits can invalidate clearances.
 if(jointRoomLayout){
  if(job.validation)writeNew(job.root/L"native-09-final-room-layout.step","entered");
  stamp("paper-geometry-stairs-and-construction-notes");
  verifyRoomDimensions=arrangeFullPlanRoomDimensions(job,db,reference,original,input,views,
    generatedViews,roomPanels,roomGrids,interior,indoorStairs,layout.scale,layout.unitMM,placeSourceText);
  stamp("joint-room-dimension-layout");
  productCompactRemainingCaptionConflicts(job,db,captions);
 }
#endif
 report<<"],\"sourceWalls\":"<<input.walls.size()<<",\"sourceOpenings\":"<<input.openings.size()<<",\"sourceColumns\":"<<input.columns.size()<<",\"lowWallsPreserved\":"<<std::count_if(input.walls.begin(),input.walls.end(),[](const auto& w){return w.low;})<<",\"allRulesComplete\":false}";writeNew(job.root/L"通用输入核验.json",report.str());
 job.detailPending.push_back({u8"通用后续模块",u8"总建筑面积",u8"当前保留源图楼层总建筑面积；本次按实际墙内边重算房间净面积，未据此替代总建筑面积"});
 job.chains=job.dimensions=0;for(const auto& item:modelTypes(db))if(item.second==L"TDbDimension2"){auto e=open<AcDbEntity>(find(db,ascii(item.first)));if(e->visibility()==AcDb::kVisible){++job.chains;job.dimensions+=static_cast<int>(chainValues(e->objectId()).size());}}
 placeSourceText();
#ifdef PLAN_STANDALONE_ENTRY
 if(job.planPostcheckMode==L"repair")
  job.planPostcheckTextRepair=productMakePostcheckTextRepair(views,generatedViews,roomGrids,sourceTextCaptions,
      sourceText,layout.scale,layout.unitMM);
#endif
 productCollectPlanPrintWindows(job,db,input,views,layout,roof,areaSummaries,titlePositions,movedAreaNotes);
#ifdef PLAN_STANDALONE_ENTRY
 productWriteFallbackPlanSheets(job,db,input,views,roof,layout.scale/layout.unitMM);
#endif
 if(job.architecturalPlanOnly){
  std::set<std::string> sourceDimensionHandles;
  for(const auto& source:job.planSourceAnnotations)
   if(source.kind=="dimension"||source.kind=="unreadable-dimension")sourceDimensionHandles.insert(source.handle);
  for(const auto& issue:productNormalizeWholePlanTypography(job,db,reference))
   job.detailPending.push_back({u8"统一字体",issue.object,issue.reason});
  auto dimensions=productEnforcePlanDimensions(job,db,reference,sourceDimensionHandles,false,true);
  job.planFinalDimensionCheck=[&job,dimensions=std::move(dimensions)](AcDbDatabase& saved,const fs::path& drawing){
   Working working(&saved);
   productVerifyRequiredPlanDimensions(job,saved,dimensions,drawing);
  };
 }
 approvedResultView(db);job.finalRoots=static_cast<int>(modelTypes(db).size());const auto fingerprint=productFingerprint(db);const auto control=job.work/L"join/control.dwg";productStatus(db.saveAs(control.c_str(),true,AcDb::kDHL_CURRENT),"save owned generic work");const auto candidate=job.work/L"ready-to-publish.dwg";productStatus(db.saveAs(candidate.c_str(),false,AcDb::kDHL_CURRENT),"save generic native construction candidate");
 stamp("remaining-placement-final-rules-and-save");
 if(job.validation){
  std::ostringstream durations;
  durations<<"step\tmilliseconds\n";
  for(const auto& [stage,ms]:timings)durations<<stage<<'\t'<<ms<<'\n';
  writeNew(job.root/L"plan-native-stage-timing.tsv",durations.str());
 }
 {
 if(job.validation)writeNew(job.root/L"native-10-saved-reopen.step","entered");
  AcDbDatabase saved(false,true);
  productStatus(saved.readDwgFile(candidate.c_str(),AcDbDatabase::kForReadAndAllShare,false),"reopen generic native result");
  productStatus(saved.closeInput(true),"close generic readback");
  const auto reopened=productFingerprint(saved);
  if(!bcpproduct::samePublicGeometry(fingerprint,reopened)){
   writeNew(job.root/L"save-before.json",fingerprint);
   writeNew(job.root/L"save-after.json",reopened);
   require(false,"generic result changed on save/reopen");
  }
  productVerifyPlanLayout(saved,layout,[&](const auto& h){return find(saved,h);},job.root/L"外围间距核验.json",true,
                          job.unifiedPlan?&job:nullptr);
  if(job.unifiedPlan)productAuditGenericAxes(job,saved,layout,exportedAxes,generatedViews,alignment,original,buildingStations);
 if(job.unifiedPlan){
  const auto floors=productMainFloorPanels(ownership);
  for(const auto& [floor,components]:floors)for(const auto view:components){
   const bool required=floors.size()>1||components.size()>1;
   const bool verified=alignment[view].registered;
   job.drawingChecks.record(view+1,"floor-registration","",{"A30","A32","QA10"},
    bcpproduct::PlanReleaseGate::mainPlan,!required?bcpproduct::PlanCheck::omitted:
      verified?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::pending,
    "main-floor-panels",!required?u8"仅一幅主体楼层，跨层配准不适用":
      verified?u8"主体楼层共同点配准已核":u8"主体楼层配准未通过，须核同楼归属、共同点及变换关系");
  }
 }
  productVerifyRoof(job,saved,roof,layout);
  if(job.unifiedPlan)productVerifyPaperDimensionRepairs(job,saved,paperDimensionRepairs,roof,layout.scale/layout.unitMM);
  {
   std::ostringstream notes;notes<<"{\"savedReopened\":true,\"notes\":[";
   for(std::size_t i=0;i<looseNotes.size();++i){
    const auto& note=looseNotes[i];const auto id=find(saved,note.handle);const auto points=chainGrips(id);
    require(points.size()==note.after.size()&&std::equal(points.begin(),points.end(),note.after.begin(),
     [](const auto& a,const auto& b){return a.distanceTo(b)<=1e-5;})&&
     productConstructionNoteContents(id)==note.text,"reopened construction note placement");
    if(i)notes<<',';
    notes<<"{\"object\":\""<<note.handle<<"\",\"before\":"<<wholePoints(note.before)<<",\"after\":"<<wholePoints(points)<<'}';
   }
   notes<<"]}";writeNew(job.root/L"construction-note-placement.json",notes.str());
  }
  productAuditAxisProject(job,saved,projectAxes,exportedAxes,generatedViews,layout);
  productInspectAxisCandidates(job,saved,input,axisViews,columns,exportedAxes,layout,projectAxes,original);
  productAuditOpeningAxisTerminals(job,saved,layout);
  if(job.unifiedPlan){
   for(const auto& axis:obliqueWallAxes){
    const auto line=open<AcDbLine>(find(saved,axis.line));
    const auto eps=.01/layout.unitMM;
    const auto matches=[&](const auto& start,const auto& end){
     return start.distanceTo(axis.start)<=eps&&end.distanceTo(axis.end)<=eps;
    };
    const auto layer=open<AcDbLayerTableRecord>(line->layerId());
    const bool verified=productPlanDimensionVisible(*line)&&layer->isPlottable()&&
     (matches(line->startPoint(),line->endPoint())||matches(line->endPoint(),line->startPoint()))&&
     symbolName(line->linetypeId())==symbolName(actualAxisLinetype);
    productCheckPlanSource(job,axis.panel,"wall-axis",axis.wall,
     verified?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::failed,{axis.line},
     verified?u8"斜墙实际中线 DOTE 已重开核验，斜轴编号另列":u8"斜墙定位轴线重开后未与实际中线一致");
    job.drawingChecks.record(axis.panel,"oblique-axis-number",axis.wall,{"A09","A16"},bcpproduct::PlanReleaseGate::mainPlan,
     bcpproduct::PlanCheck::pending,u8"已读回斜墙及现有轴网",u8"已核可定位的局部斜墙轴线，源图未明确新增斜轴的编号和轴圈显示侧");
   }
   for(std::size_t view=0;view<views.size();++view){
    std::vector<std::string> undecided,major;
    for(auto index:views[view].walls){
     const auto& source=input.walls[index];
     if(!source.orthogonal)undecided.push_back(source.wall.handle);
     else if(!source.low&&(std::abs(source.total*layout.unitMM-200)<.01||std::abs(source.total*layout.unitMM-240)<.01))
      major.push_back(source.wall.handle);
    }
    std::ostringstream row;
    row<<"{\"panel\":"<<view+1<<",\"scope\":\"original-readable-native-wall-geometry\",\"majorOrthogonalWalls\":";
    productWriteHandleArray(row,major);
    row<<",\"obliqueWallsNeedingProjectAxisRule\":";
    productWriteHandleArray(row,undecided);
    row<<",\"savedReopened\":true,\"result\":\""
       <<(undecided.empty()?"verified-no-oblique-wall-in-readable-source-scope":"pending-oblique-wall-axis")<<"\"}";
    productRecordRuleEvidence(job,"A08",row.str());
   }
   const auto finalOccupants=productAxisOccupancy(saved);
   for(const auto& occupant:sourceAxisOccupants){
    const auto final=std::find_if(finalOccupants.begin(),finalOccupants.end(),[&](const auto& row){return row.handle==occupant.handle;});
    bool reused=false;
    for(const auto& row:finalOccupants){
     if(std::any_of(sourceAxisOccupants.begin(),sourceAxisOccupants.end(),[&](const auto& source){return source.handle==row.handle;}))continue;
     for(const auto& number:row.numbers)reused|=occupant.numbers.count(number)!=0;
    }
    std::ostringstream row;
    row<<"{\"object\":\""<<occupant.handle<<"\",\"sourceNumbers\":";
    productWriteHandleArray(row,std::vector<std::string>(occupant.numbers.begin(),occupant.numbers.end()));
    row<<",\"sourceVisible\":"<<(occupant.visible?"true":"false")<<",\"savedReopened\":true,\"result\":\""
       <<(final!=finalOccupants.end()&&final->numbers==occupant.numbers&&!occupant.numbers.empty()&&!reused?
         "verified-retained-number-reservation":"pending-axis-occupancy")<<"\"}";
    productRecordRuleEvidence(job,"A26",row.str());
   }
   if(sourceAxisOccupants.empty()){
    std::ostringstream row;
    row<<"{\"scope\":\"original-model-space-native-axis-labels\",\"sourceLabels\":0,\"reopenedLabels\":"
       <<finalOccupants.size()<<",\"savedReopened\":true,\"result\":\"verified-no-original-native-label-reservations\"}";
    productRecordRuleEvidence(job,"A26",row.str());
   }
  }
  productAuditPlanPanels(job,saved,input,views,ownership,generatedViews);
  std::set<std::string> entranceDimensions;
  for(const auto& entrance:entranceSteps)
   for(const auto& dimension:entrance.dimensions)entranceDimensions.insert(dimension.first);
  if(verifyRoomDimensions){
   Working readback(&saved);
   productGenericPerimeterTextClearance(job,saved,layout,true,&perimeterDefinition);
   verifyRoomDimensions(saved);
  }
  productAuditPerimeterReconciliation(job,saved,layout,sourceMasks,entranceDimensions,sourcePerimeterWitnesses);
  if(!job.unifiedPanelScope.empty())for(const auto& [handle,originalDimension]:job.sourceDimensions)
   if(!job.planDimensionRatioReplacements.count(handle)&&productOutsideSelectedPlanWindows(job,saved,handle)){
    job.planDimensionExclusions.insert(handle);
    job.planDimensionMeasurements.erase(handle);
   }
  productVerifyPlanDimensionStyle(job,saved,layout.scale,layout.unitMM);
  productAuditSourceDimensionDisposition(job,saved,candidate,layout.unitMM);
  if(job.unifiedPlan)for(const auto& [handle,originalType]:original){
   if(originalType!=L"TDbDimension2"&&originalType!=L"TDbDrawingName"&&originalType!=L"TDbText")continue;
   AcDbObjectId id;
   const auto key=ascii(handle);
   if(job.sourceDimensions.count(key))continue;
   const bool exists=saved.getAcDbObjectId(id,false,AcDbHandle(handle.c_str()))==Acad::eOk&&!id.isErased();
   if(!exists){
    productPlanAuditRow(job,{"G06","Y06"},0,key,"source-annotation-retirement",
     "pending-source-annotation-replacement-association",u8"此源标注已替换或退出；须核原与新对象、测量用途和关联内容");
    continue;
   }
   const auto entity=open<AcDbEntity>(id);
   const bool native=std::wstring(entity->isA()->name())==originalType;
   const bool shown=productPlanDimensionVisible(*entity);
   productPlanAuditRow(job,{"G06","Y06"},0,key,"source-annotation-disposition",
    !native||!shown?"pending-source-annotation-visibility-review":"verified-source-annotation-visible",
    shown?u8"重开读回该源句柄仍可见，具体标注内容和测量覆盖见各专项":u8"已读回源标注被隐藏；须按源图核原意图及是否仍需要",
    "\"sourceType\":"+jsonUtf8(originalType)+",\"visible\":"+(shown?"true":"false"));
  }
  productAuditPlanDimensionText(job,saved,job.unifiedPlan?&views:nullptr);
  productAuditCaptionLayer(job,saved,captions,job.unifiedPlan?&input:nullptr,job.unifiedPlan?&views:nullptr,layout.unitMM,&original);
  productVerifyOpeningInkClearance(job,saved,captions);
  if(!job.unifiedPanelScope.empty())for(const auto& [key,item]:job.sourceInventory.items){
   const auto& [panel,scope,handle]=key;
   if(scope=="opening-number"&&item.check==bcpproduct::PlanCheck::pending&&
      productOutsideSelectedPlanWindows(job,saved,handle))
    productCheckPlanSource(job,panel,scope,handle,bcpproduct::PlanCheck::omitted,{handle},
     u8"原洞口已重开核实保留，实际范围完全在已选建筑平面打印图窗之外");
  }
  productVerifyPlanNames(job,saved,nameChecks,&captions);
  if(job.unifiedPlan)productVerifySourceText(job,saved,sourceTextCaptions,sourceText,layout.scale,layout.unitMM);
  productVerifyPlanTitleClearance(job,saved,input,views);
  productVerifyPlanTitlePlacement(job,saved,titlePositions);
  if(job.unifiedPlan)productVerifyPlanTitleText(job,saved,titleTextStyles);
  productVerifyPlanAreaSummaries(job,saved,areaSummaries,layout.unitMM);
  if(job.architecturalPlanOnly){
   for(const auto& name:nameChecks)if(!name.output.empty()&&name.source!=name.output)
    job.planAnnotationTextTargets[name.source]=name.output;
   for(const auto& area:areaSummaries)if(area.source!=area.output)
    job.planAnnotationTextTargets[area.source]=area.output;
   for(const auto& area:writtenAreas){
    if(!area.oldBody.empty()&&!area.body.empty()&&area.oldBody!=area.body)
     job.planAnnotationTextTargets[area.oldBody]=area.body;
    if(!area.oldExponent.empty()&&!area.exponent.empty()&&area.oldExponent!=area.exponent)
     job.planAnnotationTextTargets[area.oldExponent]=area.exponent;
   }
  }
  productVerifyGrossAreas(job,saved,grossAreas);
  for(const auto& move:movedAreaNotes){
   Working work(&saved);
   const auto note=open<AcDbText>(find(saved,move.handle));
   const auto anchor=productPlanNameAnchor(*note);
   const bool ok=anchor&&anchor->distanceTo(move.anchor)*layout.unitMM<=.01&&
    move.contents==note->textString();
   productPlanAuditRow(job,{"P05","L06"},0,move.handle,"area-note-clearance",
    ok?"verified-area-note-placement":"failed-area-note-placement",
    u8"建筑面积说明移位后重开核对原文与预期锚点");
   if(!ok)job.detailPending.push_back({u8"纸面图元净空",move.handle,u8"面积说明重开后的文字或位置不符"});
  }
  productVerifyPlanFrameGroups(job,saved,frameMoves);
  productVerifyPlanFurniture(job,saved,furniture,furnitureExclusions);
  productVerifyCleanSourceDuplicates(job,saved,sourceCleanup,layout.unitMM);
  productAuditSourceWindowStrokes(job,saved,views,layout.unitMM,sourceOpeningTraces);
  productAuditPlanColumnAndFacilities(job,saved,input,views);
  productAuditMainOpeningSourceNotes(job,saved,input,views);
  productWriteRoomAreaReadback(job,saved,writtenAreas,job.root/L"room-area-readback.json",nameChecks);
  for(const auto& [panel,unnamed]:job.planUnassignedNetDomains){
   std::vector<std::string> evidence;
   std::vector<std::string> incomplete;
   for(const auto& [key,row]:job.sourceInventory.items){
    if(std::get<0>(key)!=panel||std::get<1>(key)!="room-area")continue;
    if(row.check!=bcpproduct::PlanCheck::verified)incomplete.push_back(std::get<2>(key));
    else evidence.insert(evidence.end(),row.output.begin(),row.output.end());
   }
   if(incomplete.empty()&&!evidence.empty()&&!unnamed)productCheckPlanSource(job,panel,"net-domain","",
    bcpproduct::PlanCheck::verified,std::move(evidence),u8"净域均有用途，逐间落字并经独立重开读回");
   else if(unnamed)productCheckPlanSource(job,panel,"net-domain","",bcpproduct::PlanCheck::pending,
    std::move(evidence),u8"有"+std::to_string(unnamed)+u8"处无明确用途的闭合净域，位置和实测面积见本幅通用房间核验");
   else if(!incomplete.empty())productCheckPlanSource(job,panel,"net-domain","",bcpproduct::PlanCheck::pending,
    std::move(evidence),u8"本层有"+std::to_string(incomplete.size())+u8"个面积用途尚未通过，边界、楼梯及对应房名见本幅通用房间核验");
  }
  productVerifyEntranceSteps(job,saved,entranceSteps,job.root/L"exterior-entrance-steps.json");
  productVerifyBalconyEdges(job,saved,balconyEdges,layout.unitMM);
  productVerifyPlanDatums(job,saved,datums);
  productVerifyWallHeights(job,saved,wallHeights,levels,levelViews);
  if(job.unifiedPlan)for(std::size_t view=0;view<views.size();++view)if(generatedViews[view]&&!levelViews[view]){
   const auto reason=u8"明确屋面图幅，屋面及其构造标高按 A29 核验";
   for(const auto* scope:{"floor-datum","story-height"})
    productCheckPlanSource(job,view+1,scope,"",bcpproduct::PlanCheck::omitted,{},reason);
   for(const auto index:views[view].walls)
    productCheckPlanSource(job,view+1,"wall-height",input.walls[index].wall.handle,
     bcpproduct::PlanCheck::omitted,{},reason);
  }
  productVerifyDetailNotes(job,saved,detailNotes,input,views);
  if(job.unifiedPlan)productVerifyPlanSourceGeometry(job,saved,sourceGeometry);
  if(job.unifiedPlan)productAuditVerticalCore(job,saved,input,verticalSource,indoorStairs,views);
  productAuditPlanInputAndDependencies(job,saved,input,views,generatedViews);
  productAuditPlanEntranceDetails(job,entranceSteps);
  productAuditDeferredProjectItems(job,input,views);
  productVerifyIndoorStairs(job,saved,indoorStairs,layout.unitMM,job.root/L"indoor-stairs.json",stairDirectionMarkers);
  if(job.architecturalPlanOnly)for(const auto& [key,item]:job.sourceInventory.items){
   const auto& [panel,scope,handle]=key;
   if(!panel||generatedViews.at(panel-1)||(scope!="stair"&&scope!="unreadable-object"))continue;
   if(productOutsideSelectedPlanWindows(job,saved,handle))
    productCheckPlanSource(job,panel,scope,handle,bcpproduct::PlanCheck::omitted,{handle},
     u8"原对象已重开核实保留，实体范围完全位于本次已选建筑平面打印窗口之外");
  }
  if(job.unifiedPlan)for(const auto& stair:indoorStairs)
   if(stair.panel&&ownership.at(stair.panel-1).role==ProductPanelRole::detail&&stair.state=="native-preserved")
    productCheckPlanSource(job,stair.panel,"stair",stair.source,bcpproduct::PlanCheck::omitted,{stair.object},
       ownership.at(stair.panel-1).reason);
 }
 if(job.sourceInventory.sealed)job.planVerifiedCandidateHash=sha(candidate);
 if(job.unifiedPlan){
  AcDbDatabase paperReadback(false,true);
  productStatus(paperReadback.readDwgFile(candidate.c_str(),AcDbDatabase::kForReadAndAllShare,false),"read saved paper geometry");
  productStatus(paperReadback.closeInput(true),"close saved paper geometry input");
  productCheckPlanPrintGeometry(job,paperReadback,input,views);
 }
 productWritePlanFacts(job,input,views,generatedViews,exportedAxes,layout,alignment);
 variables.restore();
 require(sha(job.source)==job.sourceHash&&!fs::exists(job.output),"generic source changed or output already exists");
 if(!job.unifiedPlan)bcpproduct::publishNewFile(candidate,job.output);
 job.phase=ProductPhase::OpenFinal;
 productQueueSession(L"_BCP_INTERNAL_OPEN_FINAL\n");
}
