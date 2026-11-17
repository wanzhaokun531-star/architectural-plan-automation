// Preserve source instances and world transforms. Only the selected instances
// receive derived definitions when a drawing uses the same symbol elsewhere.
struct ProductFurnitureLeaf {
 std::string path;
 std::wstring type,text;
 std::vector<AcGePoint3d> points;
};

struct ProductFurnitureItem {
 std::vector<std::string> sources;
 std::string output;
 AcDbExtents bounds;
 std::vector<ProductFurnitureLeaf> leaves;
 bool sourceRegistered{};
 std::vector<std::string> mislayeredMembers;
 std::size_t flatPrimitiveCount{};
 bool zeroLayerBeforeGrouping{};
};

struct ProductFurnitureExclusion {
 std::string role;
 std::vector<AcGePoint3d> points;
};

bool productFurnitureLayerName(const std::wstring& name) {
 return name.find(L"家具")!=std::wstring::npos||name.find(L"洁具")!=std::wstring::npos||
        name.find(L"厨具")!=std::wstring::npos||_wcsicmp(name.c_str(),L"fur")==0;
}

bool productFurnitureDefinitionName(const std::wstring& name) {
 static const std::array<const wchar_t*,12> names{
  L"家具",L"洁具",L"厨具",L"坐便器",L"座便器",L"洗手盆",L"洗脸盆",L"水槽",L"沙发",L"餐桌",L"床头柜",L"灶具"
 };
 return std::any_of(names.begin(),names.end(),[&](const auto* item){return name.find(item)!=std::wstring::npos;});
}

std::vector<AcDbObjectId> productFurnitureMembers(AcDbObjectId id) {
 const auto block=open<AcDbBlockTableRecord>(id);
 AcDbBlockTableRecordIterator* raw{};
 productStatus(block->newIterator(raw),"read furniture component members");
 std::unique_ptr<AcDbBlockTableRecordIterator> iterator(raw);
 std::vector<AcDbObjectId> members;
 for(;!iterator->done();iterator->step()){
  AcDbObjectId member;
  productStatus(iterator->getEntityId(member),"read furniture component ID");
  members.push_back(member);
 }
 return members;
}

struct ProductFurniturePrimitive {std::string path;std::unique_ptr<AcDbEntity> entity;};

void productFurnitureBasicPrimitives(const AcDbEntity& entity,const std::string& path,
 std::vector<ProductFurniturePrimitive>& result,int depth=0){
 require(depth<32&&result.size()<100000,"furniture decomposition did not terminate");
 const std::wstring type=entity.isA()->name();
 if(const auto* poly=AcDbPolyline::cast(&entity))for(unsigned i=0;i<poly->numVerts();++i){
  double start{},end{};productStatus(poly->getWidthsAt(i,start,end),"read furniture polyline width");
  require(std::abs(start)<1e-9&&std::abs(end)<1e-9,"wide furniture polyline cannot be flattened without changing its outline");
 }
 if(type==L"AcDbPolyline"||type==L"AcDb2dPolyline"||type==L"AcDb3dPolyline"||type==L"AcDbRegion"){
  TemplateParts parts(&entity);
  productStatus(entity.explode(parts.values),"explode furniture compound curve");
  require(!parts.values.isEmpty(),"furniture compound produced no primitives");
  for(int i=0;i<parts.values.length();++i){
   const auto* child=AcDbEntity::cast(static_cast<AcRxObject*>(parts.values[i]));
   require(child&&child!=&entity&&child->database()==nullptr&&child->objectId().isNull(),"furniture explode transient ownership");
   productFurnitureBasicPrimitives(*child,path+"#"+std::to_string(i),result,depth+1);
  }
  return;
 }
 require(!entity.isKindOf(AcDbBlockReference::desc())&&!entity.isAProxy(),"furniture still contains a block or proxy");
 auto* raw=AcDbEntity::cast(entity.clone());
 require(raw&&raw->database()==nullptr&&raw->objectId().isNull(),"clone furniture primitive");
 result.push_back({path,std::unique_ptr<AcDbEntity>(raw)});
}

std::vector<ProductFurniturePrimitive> productFurnitureFlatPrimitives(const std::vector<AcDbObjectId>& roots){
 std::vector<ProductFurniturePrimitive> result;
 std::set<AcDbObjectId> active;
 const auto collect=[&](auto&& visit,AcDbObjectId id,const AcGeMatrix3d& world,const std::string& prefix)->void{
  const auto object=open<AcDbEntity>(id);
  const auto path=prefix+axisHandle(id);
  if(const auto* ref=AcDbBlockReference::cast(object.get())){
   const auto definition=ref->blockTableRecord();
   const auto record=open<AcDbBlockTableRecord>(definition);
   require(!record->isFromExternalReference()&&!record->isFromOverlayReference(),"external furniture definition is not a local editable component");
   require(active.size()<32&&active.insert(definition).second,"furniture block recursion in leaf audit");
   for(const auto member:productFurnitureMembers(definition))visit(visit,member,world*ref->blockTransform(),path+"/");
   active.erase(definition);
   return;
  }
  std::vector<ProductFurniturePrimitive> local;
  productFurnitureBasicPrimitives(*object,path,local);
  for(auto& primitive:local){
   if(world.isEqualTo(AcGeMatrix3d::kIdentity)){result.push_back(std::move(primitive));continue;}
   AcDbEntity* transformed{};
   productStatus(primitive.entity->getTransformedCopy(world,transformed),"preserve furniture world transform during decomposition");
   require(transformed&&transformed->database()==nullptr&&transformed->objectId().isNull(),"furniture transformed transient ownership");
   result.push_back({primitive.path,std::unique_ptr<AcDbEntity>(transformed)});
  }
 };
 for(const auto id:roots)collect(collect,id,AcGeMatrix3d::kIdentity,{});
 return result;
}

std::vector<ProductFurnitureLeaf> productFurnitureLeafInventory(const std::vector<AcDbObjectId>& roots) {
 std::vector<ProductFurnitureLeaf> leaves;
 for(const auto& primitive:productFurnitureFlatPrimitives(roots)){
  const auto& object=primitive.entity;
  ProductFurnitureLeaf leaf{primitive.path,object->isA()->name()};
  AcGePoint3dArray grips;AcDbIntArray modes,ids;
  if(object->getGripPoints(grips,modes,ids)==Acad::eOk)
   for(int i=0;i<grips.length();++i)leaf.points.push_back(grips[i]);
  if(const auto* curve=AcDbCurve::cast(object.get())){
   double first{},last{};
   if(curve->getStartParam(first)==Acad::eOk&&curve->getEndParam(last)==Acad::eOk&&std::isfinite(first)&&std::isfinite(last))
    for(const double fraction:{0.,.25,.5,.75,1.}){
     AcGePoint3d point;
     if(curve->getPointAtParam(first+(last-first)*fraction,point)==Acad::eOk)leaf.points.push_back(point);
    }
  }
  if(const auto* text=AcDbText::cast(object.get()))leaf.text=text->textString();
  if(const auto* mtext=AcDbMText::cast(object.get())){AcString contents;mtext->contents(contents);leaf.text=contents.kACharPtr();}
  if(leaf.points.empty()){
   AcDbExtents bounds;
   if(object->getGeomExtents(bounds)==Acad::eOk)leaf.points={bounds.minPoint(),bounds.maxPoint()};
  }
  leaves.push_back(std::move(leaf));
 }
 return leaves;
}

bool productSameFurnitureLeaf(const ProductFurnitureLeaf& source,const ProductFurnitureLeaf& saved) {
 return source.type==saved.type&&source.text==saved.text&&!source.points.empty()&&
  source.points.size()==saved.points.size()&&std::equal(source.points.begin(),source.points.end(),saved.points.begin(),
   [](const auto& a,const auto& b){return a.distanceTo(b)<.01;});
}

bool productCanNormalizeFurniture(const AcDbEntity& entity,std::set<AcDbObjectId>& path,bool allowText=false) {
 if(entity.isAProxy()||std::wstring_view(entity.isA()->name()).rfind(L"AcDb",0)!=0)return false;
 if(const auto* block=AcDbBlockReference::cast(&entity)){
  if(entity.isKindOf(AcDbMInsertBlock::desc()))return false;
  std::unique_ptr<AcDbObjectIterator> attrs(block->attributeIterator());
  if(attrs&&!attrs->done())return false;
  const auto id=block->blockTableRecord();
  const auto definition=open<AcDbBlockTableRecord>(id);
  if(definition->isLayout()||definition->isFromExternalReference()||definition->isFromOverlayReference()||
     !path.insert(id).second||path.size()>32)return false;
  const auto members=productFurnitureMembers(id);
  const bool safe=!members.empty()&&std::all_of(members.begin(),members.end(),[&](auto member){
   return productCanNormalizeFurniture(*open<AcDbEntity>(member),path,allowText);
  });
  path.erase(id);
  return safe;
 }
 if(entity.isKindOf(AcDbAttributeDefinition::desc())||entity.isKindOf(AcDbAttribute::desc()))return false;
 return entity.isKindOf(AcDbCurve::desc())||entity.isKindOf(AcDbHatch::desc())||
        entity.isKindOf(AcDbRegion::desc())||entity.isKindOf(AcDbPoint::desc())||
        (allowText&&(entity.isKindOf(AcDbText::desc())||entity.isKindOf(AcDbMText::desc())));
}

bool productFurnitureNestedEvidence(const AcDbEntity& entity,std::set<AcDbObjectId>& active){
 if(productFurnitureLayerName(symbolName(entity.layerId())))return true;
 const auto* ref=AcDbBlockReference::cast(&entity);
 if(!ref)return false;
 const auto id=ref->blockTableRecord();
 const auto record=open<AcDbBlockTableRecord>(id);
 if(record->isFromExternalReference()||record->isFromOverlayReference()||active.size()>=32||!active.insert(id).second)return false;
 const bool named=productFurnitureDefinitionName(symbolName(id));
 const auto members=productFurnitureMembers(id);
 const bool children=!members.empty()&&std::all_of(members.begin(),members.end(),[&](auto member){
  return productFurnitureNestedEvidence(*open<AcDbEntity>(member),active);
 });
 active.erase(id);
 return named||children;
}

AcDbObjectId productCreateFurnitureDefinition(AcDbDatabase& db,const AcGePoint3d& origin) {
 auto block=std::make_unique<AcDbBlockTableRecord>();
 productStatus(block->setName(L"*U"),"name editable furniture definition");
 block->setOrigin(origin);
 AcDbObjectId id;
 auto table=editObject<AcDbBlockTable>(db.blockTableId());
 productStatus(table->add(id,block.get()),"add editable furniture definition");
 block.release()->close();
 return id;
}

void productSetFurnitureLayer(AcDbEntity& entity,AcDbObjectId layer) {
 productStatus(entity.setLayer(layer),"set furniture member layer");
 productStatus(entity.setColorIndex(256),"set furniture member ByLayer color");
}

std::vector<std::vector<std::string>> productFurnitureConfirmed(const ProductJob& job) {
 const auto path=job.unifiedFurnitureComponents.empty()?job.profile/L"plan-policy/furniture-components.tsv":job.unifiedFurnitureComponents;
 std::ifstream file(path);
 if(!file)return {};
 std::string tag, digest;
 require(bool(file>>tag>>digest)&&tag=="PLANFURNITURE1","invalid furniture component header");
 if(digest!=job.sourceHash)return {};
 std::string line;
 std::getline(file,line);
 std::vector<std::vector<std::string>> result;
 std::set<std::string> used;
 while(std::getline(file,line)){
  if(line=="END")return result;
  std::istringstream row(line);
  require(bool(row>>tag)&&tag=="ITEM","invalid furniture component row");
  std::vector<std::string> handles;
  for(std::string handle;row>>handle;){
   require(!handle.empty()&&handle.find_first_not_of("0123456789ABCDEF")==std::string::npos&&
           used.insert(handle).second,"invalid or duplicate furniture member");
   handles.push_back(handle);
  }
  require(!handles.empty(),"empty furniture component");
  result.push_back(std::move(handles));
 }
 throw std::runtime_error("missing furniture component END");
}

std::vector<ProductFurnitureItem> productApplyPlanFurniture(ProductJob& job,AcDbDatabase& db,
 const std::map<std::string,ProductFurnitureExclusion>& exclusions) {
 if(!job.unifiedPlan)return {};
 Working working(&db);
 auto components=productFurnitureConfirmed(job);
 std::set<std::string> scoped;
 for(const auto& component:components)scoped.insert(component.begin(),component.end());
 for(const auto& [handle,type]:modelTypes(db)){
  const auto key=ascii(handle);
  if(scoped.count(key)||exclusions.count(key))continue;
  const auto entity=open<AcDbEntity>(find(db,key));
  if(!productPlanDimensionVisible(*entity))continue;
  const auto* ref=AcDbBlockReference::cast(entity.get());
  std::set<AcDbObjectId> evidence;
  if(!productFurnitureNestedEvidence(*entity,evidence))continue;
  std::set<AcDbObjectId> path;
  if(ref&&productCanNormalizeFurniture(*entity,path,true)){
   components.push_back({key});
  }else if(type!=L"TDbText"&&type!=L"AcDbText"&&type!=L"AcDbMText"){
   job.detailPending.push_back({u8"家具整件识别",key,
     ref?u8"家具块含属性、阵列、外参、代理或其他无法保形拆解的成员；保留该件并继续其他物件。":
         u8"仅图层提示为家具的非块对象，缺少完整单件成员依据；保留原对象，不把散线或建筑对象直接打包成家具。"});
  }
 }
 if(components.empty())return {};
 const auto layer=productPlanLayer(db,L"家具",252);
 AcDbObjectId zero;{const auto table=open<AcDbLayerTable>(db.layerTableId());productStatus(table->getAt(L"0",zero),"read layer zero");}
 std::vector<ProductFurnitureItem> result;
 for(const auto& component:components){
  AcDbObjectId newDefinition,newReference;
  std::vector<AcDbObjectId> retired;
  try{
  std::vector<AcDbObjectId> members;
  std::vector<std::string> mislayered;
  AcDbExtents bounds;
  for(const auto& handle:component){
   const auto id=find(db,handle);
   const auto entity=open<AcDbEntity>(id);
   std::set<AcDbObjectId> path;
   if(exclusions.count(handle)||entity->ownerId()!=modelId(db)||!productPlanDimensionVisible(*entity)||
      !productCanNormalizeFurniture(*entity,path,true)){
    job.detailPending.push_back({u8"家具整件识别",handle,
      u8"已登记物件含隐藏、属性、外参或无法保形转换的复杂成员，整件保留，继续其他物件。"});
    members.clear();
    break;
   }
   AcDbExtents box;
   productStatus(entity->getGeomExtents(box),"read source furniture extent");
   if(members.empty())bounds=box;else bounds.addExt(box);
   const auto sourceLayer=symbolName(entity->layerId());
   if(sourceLayer==L"STAIR"||sourceLayer==L"A-STAIR"||sourceLayer==L"DIM_ELEV")mislayered.push_back(handle);
   members.push_back(id);
  }
  if(members.empty())continue;
  const auto leaves=productFurnitureLeafInventory(members);
  const bool sourceRegistered=scoped.count(component.front())!=0;
  if(members.size()==1&&!sourceRegistered&&!open<AcDbEntity>(members.front())->isKindOf(AcDbBlockReference::desc())&&
     !open<AcDbEntity>(members.front())->isKindOf(AcDbPolyline::desc())){
   job.detailPending.push_back({u8"家具整件识别",component.front(),u8"单一散线缺完整物件边界依据，保留原样。"});
   continue;
  }
  auto primitives=productFurnitureFlatPrimitives(members);
  require(!primitives.empty(),"no furniture primitives after decomposition");
  AcGePoint3d base=bounds.minPoint();
  {const auto source=open<AcDbEntity>(members.front());if(const auto* ref=AcDbBlockReference::cast(source.get()))base=ref->position();}
  // Keep world coordinates in the definition and use matching definition origin
  // and insertion point. Its transform is identity: repeated SDK transforms of
  // near-circular ellipses can reparameterize their axes and arc endpoints.
  // Normalize detached primitives first. Never edit a shared source definition.
  bool firstPrimitive=true;
  for(auto& primitive:primitives){
   AcDbExtents box;productStatus(primitive.entity->getGeomExtents(box),"read furniture primitive world bounds");
   if(firstPrimitive){bounds=box;firstPrimitive=false;}else bounds.addExt(box);
   productStatus(primitive.entity->setLayer(zero),"normalize furniture primitive to zero before grouping");
   productStatus(primitive.entity->setColorIndex(0),"inherit furniture color through new block");
  }
  newDefinition=productCreateFurnitureDefinition(db,base);
  {auto definition=editObject<AcDbBlockTableRecord>(newDefinition);
   for(auto& primitive:primitives){
    AcDbObjectId child;
    productStatus(definition->appendAcDbEntity(child,primitive.entity.get()),"append flattened furniture primitive");
    primitive.entity.release()->close();
   }
  }
  auto reference=std::make_unique<AcDbBlockReference>(base,newDefinition);
  reference->setDatabaseDefaults(&db);
  require(reference->blockTransform().isEqualTo(AcGeMatrix3d::kIdentity),"furniture regrouping must not transform world geometry twice");
  productSetFurnitureLayer(*reference,layer);
  {auto model=editObject<AcDbBlockTableRecord>(modelId(db));
   productStatus(model->appendAcDbEntity(newReference,reference.get()),"place complete editable furniture block");}
  reference.release()->close();
  AcDbExtents actual;
  productStatus(open<AcDbEntity>(newReference)->getGeomExtents(actual),"read new furniture extent");
  const auto candidate=productFurnitureLeafInventory({newReference});
  std::ostringstream diagnostic;diagnostic<<"{\"bounds\":"<<wholePoints({bounds.minPoint(),bounds.maxPoint()})
   <<",\"actualBounds\":"<<wholePoints({actual.minPoint(),actual.maxPoint()})<<",\"leaves\":[";
  for(std::size_t i=0;i<leaves.size();++i){if(i)diagnostic<<',';
   diagnostic<<"{\"type\":"<<jsonUtf8(leaves[i].type)<<",\"source\":"<<wholePoints(leaves[i].points)
    <<",\"target\":"<<(i<candidate.size()?wholePoints(candidate[i].points):"null")<<'}';}
  diagnostic<<"]}";
  writeNew(job.root/(L"furniture-candidate-"+bcpproduct::decodeMessage(component.front())+L".json"),diagnostic.str());
  require(actual.minPoint().distanceTo(bounds.minPoint())<.01&&actual.maxPoint().distanceTo(bounds.maxPoint())<.01,
          "furniture grouping changed world geometry");
  require(candidate.size()==leaves.size(),"furniture primitive coverage changed");
  for(std::size_t i=0;i<leaves.size();++i)require(productSameFurnitureLeaf(leaves[i],candidate[i]),"furniture primitive world geometry changed");
  for(auto member:members){productStatus(editObject<AcDbEntity>(member)->erase(),"retire verified grouped furniture member");retired.push_back(member);}
  result.push_back({component,axisHandle(newReference),bounds,leaves,sourceRegistered,mislayered,primitives.size(),true});
  }catch(const std::exception& error){
   // Roll back only this item; an unconverted component must retain its source.
   for(auto id:retired){AcDbEntity* raw{};productStatus(acdbOpenObject(raw,id,AcDb::kForWrite,true),"restore original furniture");
    const auto restored=raw->erase(false);raw->close();productStatus(restored,"unretire original furniture");}
   if(!newReference.isNull())productStatus(editObject<AcDbEntity>(newReference)->erase(),"discard failed furniture candidate");
   if(!newDefinition.isNull())productStatus(editObject<AcDbBlockTableRecord>(newDefinition)->erase(),"discard failed furniture definition");
   job.detailPending.push_back({u8"家具整件识别",component.front(),std::string(u8"保留原件并跳过：")+error.what()});
  }
 }
 return result;
}

bool productFurnitureColors(AcDbObjectId id,AcDbObjectId layer,std::set<AcDbObjectId>& seen,std::string& offending) {
 const auto entity=open<AcDbEntity>(id);
 if(entity->layerId()!=layer||!entity->color().isByLayer()){
  offending=axisHandle(id);
  return false;
 }
 const auto* ref=AcDbBlockReference::cast(entity.get());
 if(!ref||!seen.insert(ref->blockTableRecord()).second)return false;
 const auto members=productFurnitureMembers(ref->blockTableRecord());
 return !members.empty()&&std::all_of(members.begin(),members.end(),[&](auto member){
  const auto child=open<AcDbEntity>(member);
  const bool okay=!child->isKindOf(AcDbBlockReference::desc())&&symbolName(child->layerId())==L"0"&&child->color().isByBlock();
  if(!okay)offending=axisHandle(member);
  return okay;
 });
}

void productVerifyPlanFurniture(ProductJob& job,AcDbDatabase& db,const std::vector<ProductFurnitureItem>& items,
 const std::map<std::string,ProductFurnitureExclusion>& exclusions) {
 if(!job.unifiedPlan)return;
 Working working(&db);
 AcDbObjectId layer;
 {const auto table=open<AcDbLayerTable>(db.layerTableId());table->getAt(L"家具",layer);}
 const bool color=!layer.isNull()&&open<AcDbLayerTableRecord>(layer)->color().colorIndex()==252;
 std::ostringstream out;
 out<<std::setprecision(17)<<"{\"savedReopened\":true,\"layerACI252\":"<<(color?"true":"false")<<",\"items\":[";
 bool first=true;
 std::set<std::string> ownedSources;
 const auto model=modelTypes(db);
 std::size_t crossCategory=0;
 for(const auto& item:items){
  const auto id=find(db,item.output);
  const auto entity=open<AcDbEntity>(id);
  AcDbExtents bounds;
  std::set<AcDbObjectId> seen;
  std::string offending;
  const auto* block=AcDbBlockReference::cast(entity.get());
  const bool extentsRead=block&&entity->getGeomExtents(bounds)==Acad::eOk;
  const bool envelope=extentsRead&&bounds.minPoint().distanceTo(item.bounds.minPoint())<.01&&
   bounds.maxPoint().distanceTo(item.bounds.maxPoint())<.01;
  const auto actual=block?productFurnitureLeafInventory({id}):std::vector<ProductFurnitureLeaf>{};
  std::vector<bool> matched(actual.size());
  std::vector<std::pair<std::string,std::string>> coverage;
  for(const auto& source:item.leaves){
   for(std::size_t i=0;i<actual.size();++i){
    if(matched[i]||!productSameFurnitureLeaf(source,actual[i]))continue;
    matched[i]=true;
    coverage.emplace_back(source.path,actual[i].path);
    break;
   }
  }
  const bool geometry=envelope&&!actual.empty()&&coverage.size()==item.leaves.size()&&coverage.size()==actual.size();
  bool unique=true;
  for(const auto& source:item.sources){
   const bool once=ownedSources.insert(source).second;
   const bool retired=source==item.output||!model.count(bcpproduct::decodeMessage(source));
   unique=unique&&once&&retired;
  }
  const bool layers=color&&block&&productFurnitureColors(id,layer,seen,offending);
  const bool flat=block&&productFurnitureMembers(block->blockTableRecord()).size()==item.flatPrimitiveCount;
  const bool verified=geometry&&layers&&unique&&flat&&item.zeroLayerBeforeGrouping;
  if(verified)for(const auto& [source,target]:coverage)job.planAnnotationFurnitureTargets.emplace(source,target);
  if(!verified)job.detailPending.push_back({u8"家具保存重开",item.output,u8"目标块、有效层色、原成员世界几何或成员唯一归属未通过。"});
  std::ostringstream row;
  row<<"{\"object\":\""<<item.output<<"\",\"sources\":[";
  for(std::size_t i=0;i<item.sources.size();++i){
   if(i)row<<',';
   row<<'"'<<item.sources[i]<<'"';
  }
  row<<"],\"bounds\":"<<wholePoints({item.bounds.minPoint(),item.bounds.maxPoint()})
     <<",\"actualBounds\":"<<(extentsRead?wholePoints({bounds.minPoint(),bounds.maxPoint()}):"null")
     <<",\"savedReopened\":true,\"geometryVerified\":"<<(geometry?"true":"false")
     <<",\"sourceAssociation\":\""<<(item.sourceRegistered?"source-bound-project-component":"classified-existing-block")
     <<"\",\"uniqueSourceOwnership\":"<<(unique?"true":"false")
     <<",\"sourceLeafCount\":"<<item.leaves.size()<<",\"outputLeafCount\":"<<actual.size()
     <<",\"membersLayerAndColorVerified\":"<<(layers?"true":"false")
     <<",\"zeroLayerBeforeGrouping\":"<<(item.zeroLayerBeforeGrouping?"true":"false")
     <<",\"flatPrimitiveCount\":"<<item.flatPrimitiveCount
     <<",\"noNestedBlocks\":"<<((layers&&flat)?"true":"false")
     <<",\"firstNonconformingMember\":\""<<offending<<'"'
     <<",\"matchedLeafCount\":"<<coverage.size()<<",\"report\":\"plan-furniture-readback.json\""
     <<",\"result\":\""<<(verified?"verified-furniture-instance":"pending")<<"\"";
  const auto summary=row.str();
  row<<",\"memberCorrespondence\":[";
  for(std::size_t i=0;i<coverage.size();++i){
   if(i)row<<',';
   row<<"[\""<<coverage[i].first<<"\",\""<<coverage[i].second<<"\"]";
  }
  row<<"]}";
  for(const auto* rule:{"NC08","NC10","NC11","NC14","NC15","NC16","NC17","Y09","QA04","QA05"})
   productRecordRuleEvidence(job,rule,summary+"}");
  if(!item.mislayeredMembers.empty()){
   ++crossCategory;
   std::string scopes=" ,\"sourceMembersOnStairOrElevationLayer\":[";
   for(std::size_t i=0;i<item.mislayeredMembers.size();++i){if(i)scopes+=',';scopes+='"'+item.mislayeredMembers[i]+'"';}
   productRecordRuleEvidence(job,"NC20",summary+scopes+
    "],\"dependentVerification\":[\"indoor-stairs.json\",\"exterior-entrance-steps.json\"]}");
  }
  if(!first)out<<',';
  first=false;
  out<<row.str();
 }
 for(const auto& pending:job.detailPending)if(pending[0]==u8"家具整件识别"){
  const auto evidence="{\"object\":"+jsonUtf8(bcpproduct::decodeMessage(pending[1]))+",\"detail\":"+
   jsonUtf8(bcpproduct::decodeMessage(pending[2]))+",\"result\":\"pending-source-component-classification\"}";
  for(const auto* rule:{"NC08","NC10","NC11","NC12","NC13","NC14","NC17","NC18","NC20","QA04","QA05"})
   productRecordRuleEvidence(job,rule,evidence);
 }
 if(!crossCategory)productRecordRuleEvidence(job,"NC20","{\"scope\":\"confirmed-furniture-source-layer-inventory\","
  "\"result\":\"not-applicable-no-confirmed-root-member-on-stair-or-elevation-layer\"}");
 out<<"],\"excluded\":[";
 first=true;
 for(const auto& [handle,expected]:exclusions){
  bool retained=false;
  try{
   const auto actual=chainGrips(find(db,handle));
   retained=model.count(bcpproduct::decodeMessage(handle))&&!expected.points.empty()&&actual.size()==expected.points.size()&&
    std::equal(actual.begin(),actual.end(),expected.points.begin(),[](const auto& a,const auto& b){return a.distanceTo(b)<.01;});
  }catch(const std::exception&){}
  const auto evidence="{\"object\":\""+handle+"\",\"role\":\""+expected.role+"\",\"savedReopened\":true,\"result\":\""+
   (retained?"verified-source-outline-excluded-and-preserved":"pending-source-outline-retention")+"\"}";
  for(const auto* rule:{"NC12","NC13","NC17","NC18","QA04","QA05"})productRecordRuleEvidence(job,rule,evidence);
  if(!first)out<<',';
  first=false;
  out<<evidence;
 }
 out<<"]}";
 writeNew(job.root/L"plan-furniture-readback.json",out.str());
}
