struct ProductVerticalCoreSource {
 struct Marker { std::string path; std::wstring text; AcGePoint3d location; std::size_t panel; };
 std::vector<Marker> lifts, landings, dimensionInk;
 std::vector<std::string> drawOrder;
 std::vector<Marker> dimensionRootsInBlocks;
 std::map<std::string,std::vector<std::pair<AcGePoint3d,AcGePoint3d>>> wallStrokes;
 std::size_t visibleRoots{},blockMembers{},evtr{},unsupported{};
};

std::vector<std::string> productVerticalDrawOrder(AcDbDatabase& db){
 const auto model=open<AcDbBlockTableRecord>(modelId(db));
 AcDbObjectIdArray ids;
 AcDbSortentsTable* raw{};
 if(model->getSortentsTable(raw,AcDb::kForRead,false)==Acad::eOk){
  std::unique_ptr<AcDbSortentsTable,Close<AcDbSortentsTable>> table(raw);
  productStatus(table->getFullDrawOrder(ids),"actual model-space drawing order");
 }else{
  AcDbBlockTableRecordIterator* iterator{};
  productStatus(model->newIterator(iterator),"default model-space drawing order");
  std::unique_ptr<AcDbBlockTableRecordIterator> owned(iterator);
  for(;!owned->done();owned->step()){
   AcDbObjectId id;
   if(owned->getEntityId(id)==Acad::eOk)ids.append(id);
  }
 }
 std::vector<std::string> order;
 for(int i=0;i<ids.length();++i)if(!ids[i].isErased())order.push_back(axisHandle(ids[i]));
 return order;
}

ProductVerticalCoreSource productCaptureVerticalCore(AcDbDatabase& db,const GenericNativeInput& input,
 const std::vector<bcpproduct::GenericView>& views,bool legacyInkHeuristic){
 ProductVerticalCoreSource result;
 result.drawOrder=productVerticalDrawOrder(db);
 const auto record=[&](std::vector<ProductVerticalCoreSource::Marker>& target,const std::string& path,
                       const std::wstring& text,const AcGePoint3d& position){
  target.push_back({path,text,position,bcpproduct::nearestGenericView(views,{position.x,position.y})+1});
 };
 const auto classify=[&](const std::string& path,const std::wstring& text,const AcGePoint3d& position){
  if(text.find(L"电梯")!=std::wstring::npos||text.find(L"Elevator")!=std::wstring::npos)
   record(result.lifts,path,text,position);
  if(text.find(L"地下")!=std::wstring::npos||text.find(L"错层")!=std::wstring::npos||
     text.find(L"上屋面")!=std::wstring::npos||text.find(L"屋顶楼梯")!=std::wstring::npos||
     text.find(L"屋面楼梯")!=std::wstring::npos)record(result.landings,path,text,position);
 };
 const auto walk=[&](const auto& self,AcDbObjectId id,const AcGeMatrix3d& transform,const std::string& prefix,
                     const std::wstring& inheritedLayer,unsigned depth)->void{
  if(depth>12){++result.unsupported;return;}
  const auto entity=open<AcDbEntity>(id);
  if(!productPlanDimensionVisible(*entity))return;
  if(entity->isAProxy()){++result.unsupported;return;}
  const auto handle=axisHandle(id);
  const auto path=prefix.empty()?handle:prefix+"/"+handle;
  auto layer=symbolName(entity->layerId());
  if(layer==L"0"&&!inheritedLayer.empty())layer=inheritedLayer;
  if(depth)++result.blockMembers;else ++result.visibleRoots;
  AcDbExtents bounds;
  const bool bounded=entity->getGeomExtents(bounds)==Acad::eOk;
  auto location=bounded?bounds.minPoint()+(bounds.maxPoint()-bounds.minPoint())*.5:AcGePoint3d();
  if(bounded)location.transformBy(transform);
  if(layer==L"EVTR"){
   ++result.evtr;
   if(bounded)record(result.lifts,path,L"EVTR",location);
  }
  if(depth&&bounded&&(AcDbAlignedDimension::cast(entity.get())||AcDbRotatedDimension::cast(entity.get())))
   record(result.dimensionRootsInBlocks,path,entity->isA()->name(),location);
  if(const auto* block=AcDbBlockReference::cast(entity.get())){
   const auto definition=open<AcDbBlockTableRecord>(block->blockTableRecord());
   if(bounded)classify(path,symbolName(block->blockTableRecord()),location);
   if(definition->isFromExternalReference()){++result.unsupported;return;}
   for(const auto member:productFurnitureMembers(block->blockTableRecord()))
    self(self,member,transform*block->blockTransform(),path,layer,depth+1);
   const auto raw=block->attributeIterator();
   if(raw){
    std::unique_ptr<AcDbObjectIterator> attributes(raw);
    for(;!attributes->done();attributes->step())
     self(self,attributes->objectId(),transform,path,layer,depth+1);
   }
   return;
  }
  std::wstring text;
  if(const auto* plain=AcDbText::cast(entity.get()))text=plain->textString();
  else if(const auto* mtext=AcDbMText::cast(entity.get())){AcString value;mtext->contents(value);text=value.kACharPtr();}
  else if(const auto found=input.properties.find(handle);found!=input.properties.end()){
   for(const auto* key:{L"文字内容",L"图名内容",L"门窗类型",L"门窗样式"})
    if(const auto value=found->second.find(key);value!=found->second.end())text+=value->second+L" ";
  }else if(std::wstring(entity->isA()->name())==L"TDbText"){
   std::vector<ProductRoomLeaf> leaves;
   productRoomLeaves(*entity,leaves);
   for(const auto& leaf:leaves)text+=leaf.text+L" ";
  }
  if(bounded&&!text.empty())classify(path,text,location);
  const bool dimLayer=layer==L"PUB_DIM"||layer==L"DIM"||layer.find(L"尺寸")!=std::wstring::npos;
  const bool looseLine=AcDbLine::cast(entity.get())||AcDbPolyline::cast(entity.get());
  const bool dateNotation=std::count(text.begin(),text.end(),L'.')>1;
  const bool numeric=std::any_of(text.begin(),text.end(),[](wchar_t c){return c>=L'0'&&c<=L'9';})&&
   !dateNotation&&text.find_first_not_of(L"0123456789.<> +-　\t")==std::wstring::npos&&
   (dimLayer||layer==L"STAIR"||layer==L"EVTR"||
    std::count_if(text.begin(),text.end(),[](wchar_t c){return c>=L'0'&&c<=L'9';})>=3);
  if(legacyInkHeuristic&&bounded&&((dimLayer&&looseLine)||numeric))
   record(result.dimensionInk,path,text,location);
 };
 for(const auto& [handle,type]:modelTypes(db))walk(walk,find(db,ascii(handle)),AcGeMatrix3d::kIdentity,"",L"",0);
 for(const auto& wall:input.walls){
  const auto entity=open<AcDbEntity>(find(db,wall.wall.handle));
  GenericOpeningInk ink;
  productGenericOpeningInk(*entity,ink);
  result.wallStrokes.emplace(wall.wall.handle,std::move(ink.straight));
 }
 return result;
}

void productRestoreVerticalDrawOrder(AcDbDatabase& db,const ProductVerticalCoreSource& source,
 const std::vector<ProductStairRow>& stairs){
 std::map<std::string,std::string> replacements;
 for(const auto& stair:stairs)if(stair.state=="plan-written"&&!stair.treads.empty()){
  for(const auto& handle:source.drawOrder)if(std::find(stair.treads.begin(),stair.treads.end(),handle)!=stair.treads.end()){
   replacements.emplace(handle,stair.object);break;
  }
 }
 if(replacements.empty())return;
 std::vector<AcDbObjectId> target;
 std::set<AcDbObjectId> newStairs;
 for(const auto& handle:source.drawOrder){
  const auto found=replacements.find(handle);
  const auto name=bcpproduct::decodeMessage(found==replacements.end()?handle:found->second);
  AcDbObjectId id;
  if(db.getAcDbObjectId(id,false,AcDbHandle(name.c_str()))==Acad::eOk&&!id.isErased()){
   target.push_back(id);
   if(found!=replacements.end())newStairs.insert(id);
  }
 }
 const auto model=editObject<AcDbBlockTableRecord>(modelId(db));
 AcDbSortentsTable* raw{};
 productStatus(model->getSortentsTable(raw,AcDb::kForWrite,true),"stair replacement relative draw order");
 std::unique_ptr<AcDbSortentsTable,Close<AcDbSortentsTable>> table(raw);
 for(std::size_t i=0;i<target.size();++i)if(newStairs.count(target[i])){
  AcDbObjectIdArray item;
  item.append(target[i]);
  if(i)productStatus(table->moveAbove(item,target[i-1]),"restore rebuilt stair after previous source object");
  else if(target.size()>1)productStatus(table->moveBelow(item,target[1]),"restore rebuilt stair before next source object");
 }
}

void productAuditVerticalCore(ProductJob& job,AcDbDatabase& saved,const GenericNativeInput& input,
 const ProductVerticalCoreSource& source,const std::vector<ProductStairRow>& stairs,
 const std::vector<bcpproduct::GenericView>& views){
 Working working(&saved);
 const auto summary="\"visibleSourceRoots\":"+std::to_string(source.visibleRoots)+",\"visibleBlockMembers\":"+
  std::to_string(source.blockMembers)+",\"evtrMembers\":"+std::to_string(source.evtr)+
  ",\"unreadableProxyOrXref\":"+std::to_string(source.unsupported);
 const auto evidence=[&](const auto& row){
  return "\"sourcePath\":"+jsonUtf8(bcpproduct::decodeMessage(row.path))+",\"sourceText\":"+jsonUtf8(row.text)+
         ",\"position\":"+wholePoints({row.location});
 };
 if(source.lifts.empty())productPlanAuditRow(job,{"SE04","SE05","SE09"},0,"","source-elevator-and-shaft-inventory",
  source.unsupported?"pending-unreadable-source-elevator-scope":"not-applicable-no-elevator-source-trigger",
  u8"逐根及块成员核源图原生门窗公开类型、文字、块名、EVTR 层，未检出电梯或井道证据",summary);
 for(const auto& lift:source.lifts){
  const auto& view=views[lift.panel-1];
  const auto tolerance=.01/job.unifiedUnitMM;
  std::array<std::optional<std::pair<double,std::size_t>>,4> faces;
  for(const auto index:view.walls){
   const auto& wall=input.walls[index];
   if(wall.low||!wall.orthogonal)continue;
   const auto box=approvedRect(wall.wall);
   const bool vertical=std::abs(wall.wall.start.x-wall.wall.end.x)<=tolerance;
   const double along=vertical?lift.location.y:lift.location.x;
   const double normal=vertical?lift.location.x:lift.location.y;
   const int n=vertical?0:1;
   const int a=1-n;
   if(along<=box[a]+tolerance||along>=box[a+2]-tolerance)continue;
   const int side=normal>box[n+2]+tolerance?2*n:normal<box[n]-tolerance?2*n+1:-1;
   if(side<0)continue;
   const auto station=box[n+(side%2?0:2)];
   if(!faces[side]||std::abs(normal-station)<std::abs(normal-faces[side]->first))faces[side]=std::make_pair(station,index);
  }
  const bool allFaces=std::all_of(faces.begin(),faces.end(),[](const auto& face){return face.has_value();});
  std::ostringstream measured;
  measured<<std::setprecision(17)<<evidence(lift)<<",\"candidateWallFaces\":[";
  for(std::size_t i=0;i<faces.size();++i){
   if(i)measured<<',';
   if(!faces[i]){measured<<"null";continue;}
   const auto& [coordinate,index]=*faces[i];
   measured<<"{\"source\":\""<<input.walls[index].wall.handle<<"\",\"coordinate\":"<<coordinate
      <<",\"wallThicknessMM\":"<<input.walls[index].total*job.unifiedUnitMM<<'}';
  }
  measured<<']';
  if(allFaces)measured<<",\"candidateNetXMM\":"<<(faces[1]->first-faces[0]->first)*job.unifiedUnitMM
                       <<",\"candidateNetYMM\":"<<(faces[3]->first-faces[2]->first)*job.unifiedUnitMM;
  measured<<",\"sourceDoors\":[";
  bool first=true;
  for(const auto& opening:input.openings){
   const auto host=productGenericHost(opening,input,view);
   if(!host||opening.window||std::none_of(faces.begin(),faces.end(),[&](const auto& face){return face&&face->second==host->wall;}))continue;
   if(!first)measured<<',';
   first=false;
   measured<<"{\"source\":\""<<opening.handle<<"\",\"wallOpeningMM\":"<<opening.width*job.unifiedUnitMM
       <<",\"stations\":"<<wholePoints({opening.a,opening.b})<<'}';
  }
  measured<<']';
  productPlanAuditRow(job,{"SE04","SE05","SE09"},lift.panel,lift.path,"source-elevator-and-shaft-candidate",
   "pending-confirmed-shaft-equipment-and-wall-connections",
   u8"源电梯候选及沿两方向可证墙面已定位；邻接关系、井道转角、门净宽、开门侧/形式和轿厢须另核，洞口宽单独记录",
   measured.str());
 }
 if(source.landings.empty())productPlanAuditRow(job,{"SE12"},0,"","source-special-stair-level-inventory",
  source.unsupported?"pending-unreadable-special-level-scope":"not-applicable-no-explicit-special-level-trigger",
  u8"已检查源文字和块成员，未见地下室、上屋面或错层的明确专项表达，未删除源楼梯或文字",summary);
 for(const auto& landing:source.landings)productPlanAuditRow(job,{"SE12"},landing.panel,landing.path,"source-special-stair-level",
  "pending-confirmed-special-level-connectivity",u8"实际源图专项标记已定位，需核本层通行和项目确认；保留源表达",evidence(landing));
 if(!job.architecturalPlanOnly&&source.dimensionInk.empty())productPlanAuditRow(job,{"TL09"},0,"","source-loose-dimension-ink-inventory",
  source.unsupported?"pending-unreadable-source-dimension-scope":"not-applicable-no-loose-dimension-ink-candidate",
  u8"已扫尺寸层散线和所有可读模型文字的纯数字字形，未识别到可替换线文尺寸候选",summary);
 for(const auto& ink:source.dimensionInk)productPlanAuditRow(job,{"TL09"},ink.panel,ink.path,"source-loose-dimension-ink-candidate",
  "pending-loose-dimension-semantic-association",u8"源线文候选需关联实际两端构件与测量用途，先保留，未将踏步序号当尺寸",evidence(ink));
 for(const auto& nested:source.dimensionRootsInBlocks)productPlanAuditRow(job,{"D26"},nested.panel,nested.path,"nested-source-ordinary-dimension",
  "pending-nested-source-projection",u8"已定位块内原生普通尺寸；保持其定义和块变换，须按实例核实际投影后再转换",evidence(nested));

 std::map<std::string,std::size_t> savedOrder;
 const auto reopened=productVerticalDrawOrder(saved);
 for(std::size_t i=0;i<reopened.size();++i)savedOrder.emplace(reopened[i],i);
 std::optional<std::size_t> last;
 bool preserved=true;
 std::size_t compared{};
 for(const auto& name:source.drawOrder)if(const auto found=savedOrder.find(name);found!=savedOrder.end()){
  preserved=preserved&&(!last||found->second>*last);last=found->second;++compared;
 }
 productPlanAuditRow(job,{"SE20"},0,"","retained-model-relative-drawing-order",
  compared&&preserved?"verified-retained-relative-drawing-order":"pending-retained-relative-drawing-order",
  u8"通过 AutoCAD 公开排序表比较全部同句柄模型对象的实际相对绘制顺序",
  "\"comparedRetainedRoots\":"+std::to_string(compared));
 for(const auto& stair:stairs){
  if(stair.state!="plan-written"||stair.treads.empty())continue;
  const auto at=savedOrder.find(stair.object);
  const auto sourceAt=std::find_if(source.drawOrder.begin(),source.drawOrder.end(),[&](const auto& handle){
   return std::find(stair.treads.begin(),stair.treads.end(),handle)!=stair.treads.end();
  });
  bool orderMatches=at!=savedOrder.end()&&sourceAt!=source.drawOrder.end();
  if(orderMatches)for(auto it=source.drawOrder.begin();it!=source.drawOrder.end();++it){
   const auto existing=savedOrder.find(*it);
   if(existing!=savedOrder.end()&&((it<sourceAt)!=(existing->second<at->second)))orderMatches=false;
  }
  productPlanAuditRow(job,{"SE20"},stair.panel,stair.object,"rebuilt-stair-source-draw-order",
   orderMatches?"verified-rebuilt-stair-relative-order":"pending-rebuilt-stair-relative-order",
   u8"原生梯段与其最先绘制的被替代踏步相对保留构件顺序逐项重开比较");
 }

 const auto close=[&](const auto& a,const auto& b){return a.distanceTo(b)*job.unifiedUnitMM<.01;};
 for(const auto& [wall,prior]:source.wallStrokes){
  AcDbObjectId id;
  const auto wide=bcpproduct::decodeMessage(wall);
  GenericOpeningInk actual;
  const bool present=saved.getAcDbObjectId(id,false,AcDbHandle(wide.c_str()))==Acad::eOk&&!id.isErased();
  if(present)productGenericOpeningInk(*open<AcDbEntity>(id),actual);
  const bool unchanged=present&&!prior.empty()&&prior.size()==actual.straight.size()&&
   std::all_of(prior.begin(),prior.end(),[&](const auto& a){return std::any_of(actual.straight.begin(),actual.straight.end(),[&](const auto& b){
    return (close(a.first,b.first)&&close(a.second,b.second))||(close(a.first,b.second)&&close(a.second,b.first));
   });});
  const auto found=std::find_if(input.walls.begin(),input.walls.end(),[&](const auto& row){return row.wall.handle==wall;});
  const auto panel=found==input.walls.end()?0:bcpproduct::nearestGenericView(views,found->wall.start)+1;
  productPlanAuditRow(job,{"SE17"},panel,wall,"saved-native-wall-rendered-strokes",
   unchanged?"verified-wall-rendered-strokes-preserved":"pending-wall-rendered-stroke-reconciliation",
   u8"重开核对公开可见墙线端点、数量及裁剪结果；不从包围框推定转角连接",
   "\"sourceStrokeCount\":"+std::to_string(prior.size())+",\"savedStrokeCount\":"+std::to_string(actual.straight.size()));
  productPlanAuditRow(job,{"R05"},panel,wall,"wall-boundary-area-trigger",
   unchanged?"not-applicable-wall-boundary-preserved":"pending-wall-area-recompute-audit",
   unchanged?u8"所核原墙实际可见边线一致；此墙未触发归整后重算，项目净面积独立核验":u8"原墙图面边线变化或不可读；需核实际墙域及受影响净面积",
   "\"sourceStrokeCount\":"+std::to_string(prior.size())+",\"savedStrokeCount\":"+std::to_string(actual.straight.size()));
 }
 for(const auto& stair:stairs){
  const auto handle=stair.source.empty()?stair.object:stair.source;
  if(handle.empty())continue;
  const auto id=find(saved,stair.object.empty()?handle:stair.object);
  const auto entity=open<AcDbEntity>(id);
  const auto strokes=productStairVisibleStrokes(*entity,.01/job.unifiedUnitMM);
  std::set<std::string> crossings;
  std::size_t checked{};
  for(const auto& tread:strokes){
   const AcGePoint3d a(tread.first.x,tread.first.y,0),b(tread.second.x,tread.second.y,0);
   if(a.distanceTo(b)*job.unifiedUnitMM<300.)continue;
   ++checked;
   for(const auto& wall:input.walls){
    if(wall.low||!wall.orthogonal)continue;
    const auto rect=approvedRect(wall.wall);
    const double margin=.01/job.unifiedUnitMM;
    if(rect[0]+margin>=rect[2]-margin||rect[1]+margin>=rect[3]-margin)continue;
    AcDbExtents body({rect[0]+margin,rect[1]+margin,0},{rect[2]-margin,rect[3]-margin,0});
    if(productGenericSegmentHitsBox(a+(b-a)*.05,a+(b-a)*.95,body,0))crossings.insert(wall.wall.handle);
   }
  }
  if(crossings.empty())productPlanAuditRow(job,{"SE21"},stair.panel,handle,"stair-wall-overlap-screen",
   checked?"not-applicable-no-public-stair-stroke-wall-crossing":"pending-no-public-stair-strokes",
   u8"筛查实际楼梯公开线段与原墙体实体范围，缺平台或通行资料时仍按 SE23 另核",
   "\"checkedStairStrokes\":"+std::to_string(checked));
  for(const auto& wall:crossings)productPlanAuditRow(job,{"SE21"},stair.panel,wall,"stair-wall-overlap-screen",
   "pending-source-wall-use-and-platform-confirmation",
   u8"实际楼梯可见线段进入墙体轮廓；核是否为平台通行、剖切投影或保留围护，未经项目确认保持原墙",
   "\"stair\":\""+handle+"\"");
 }
}
