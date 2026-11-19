struct ProductFurnitureItem {
 std::vector<std::string> sources;
 std::string output;
 std::wstring layer;
 std::string signature;
 std::set<std::string> members;
 bool changed{};
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

bool productKitchenFixtureName(std::wstring name);

bool productFurnitureNestedEvidence(const AcDbEntity& entity,std::set<AcDbObjectId>& active){
 if(productFurnitureLayerName(symbolName(entity.layerId()))||productKitchenFixtureName(symbolName(entity.layerId())))return true;
 const auto* ref=AcDbBlockReference::cast(&entity);
 if(!ref)return false;
 const auto id=ref->blockTableRecord();
 const auto record=open<AcDbBlockTableRecord>(id);
 if(record->isFromExternalReference()||record->isFromOverlayReference()||active.size()>=32||!active.insert(id).second)return false;
 const bool named=productFurnitureDefinitionName(symbolName(id))||productKitchenFixtureName(symbolName(id));
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

// Preserve the INSERT, its instance attributes, transform and shared definition.
bool productKitchenFixtureName(std::wstring name){
 std::transform(name.begin(),name.end(),name.begin(),::towlower);
 for(const auto* clue:{L"洁具",L"厨具",L"卫浴",L"马桶",L"坐便",L"座便",L"蹲便",L"洗手盆",L"洗脸盆",L"面盆",L"洗手台",L"花洒",L"洗菜盆",L"燃气灶",L"油烟机",L"小便斗",L"蹲厕",L"浴缸",L"淋浴器",L"水槽",L"灶具",L"toilet",L"washbasin",L"bathtub",L"kitchen sink"})
  if(name.find(clue)!=std::wstring::npos)return true;
 return false;
}

bool productFurnitureStructure(AcDbObjectId id,bool root,std::set<AcDbObjectId>& active,
 std::ostringstream& signature,std::set<std::string>& protectedMembers,bool& kitchen){
 const auto entity=open<AcDbEntity>(id);
 const auto handle=axisHandle(id);
 const auto layer=symbolName(entity->layerId());
 kitchen=kitchen||productKitchenFixtureName(layer);
 if(!root)protectedMembers.insert(handle);
 signature<<'('<<handle<<' '<<jsonUtf8(entity->isA()->name())<<' '<<entity->color().color()<<' '
          <<int(entity->visibility())<<' '<<int(entity->lineWeight())<<' '<<axisHandle(entity->linetypeId());
 if(!root)signature<<' '<<jsonUtf8(layer);
 if(const auto* ref=AcDbBlockReference::cast(entity.get())){
  const auto definition=ref->blockTableRecord();
  const auto record=open<AcDbBlockTableRecord>(definition);
  if(record->isLayout()||record->isFromExternalReference()||record->isFromOverlayReference()||
     active.size()>=64||!active.insert(definition).second)return false;
  kitchen=kitchen||productKitchenFixtureName(symbolName(definition));
  // AutoCAD renumbers the display aliases of anonymous blocks on reopen.
  // Their persistent block-table record handle and every member stay stable.
  signature<<' '<<axisHandle(definition)<<' '<<(record->isAnonymous()?"anonymous":jsonUtf8(symbolName(definition)));
  const auto matrix=ref->blockTransform();
  for(int r=0;r<4;++r)for(int c=0;c<4;++c)signature<<' '<<matrix.entry[r][c];
  std::unique_ptr<AcDbObjectIterator> attrs(ref->attributeIterator());
  for(;attrs&&!attrs->done();attrs->step())
   if(!productFurnitureStructure(attrs->objectId(),false,active,signature,protectedMembers,kitchen))return false;
  for(auto child:productFurnitureMembers(definition))
   if(!productFurnitureStructure(child,false,active,signature,protectedMembers,kitchen))return false;
  active.erase(definition);
 }else{
  AcGePoint3dArray grips;AcDbIntArray modes,ids;
  if(entity->getGripPoints(grips,modes,ids)==Acad::eOk)
   for(int i=0;i<grips.length();++i)signature<<' '<<grips[i].x<<' '<<grips[i].y<<' '<<grips[i].z;
  if(const auto* curve=AcDbCurve::cast(entity.get())){
   double a{},b{};if(curve->getStartParam(a)==Acad::eOk&&curve->getEndParam(b)==Acad::eOk)
    for(double fraction:{0.,.25,.5,.75,1.}){AcGePoint3d point;
     if(curve->getPointAtParam(a+(b-a)*fraction,point)==Acad::eOk)signature<<' '<<point.x<<' '<<point.y<<' '<<point.z;}
  }
  if(const auto* label=AcDbText::cast(entity.get()))signature<<' '<<jsonUtf8(label->textString())<<' '<<label->height()<<' '<<label->rotation()<<' '<<axisHandle(label->textStyle());
  if(const auto* label=AcDbAttribute::cast(entity.get()))signature<<' '<<jsonUtf8(label->tag());
  if(const auto* label=AcDbAttributeDefinition::cast(entity.get()))signature<<' '<<jsonUtf8(label->tag());
  if(const auto* label=AcDbMText::cast(entity.get())){AcString s;label->contents(s);signature<<' '<<jsonUtf8(s.kACharPtr());}
 }
 signature<<')';return true;
}

void productProtectFurnitureMembers(ProductJob& job,AcDbDatabase& db){
 for(const auto& [handle,type]:modelTypes(db)){
  const auto id=find(db,ascii(handle));const auto entity=open<AcDbEntity>(id);
  if(!AcDbBlockReference::cast(entity.get())){
   const auto layer=symbolName(entity->layerId());
   if(layer==L"A-家具"||layer==L"A-洁具、厨具")job.protectedFurnitureMembers.insert(ascii(handle));
   continue;
  }
  std::set<AcDbObjectId> path;
  if(!productFurnitureNestedEvidence(*entity,path))continue;
  bool kitchen=false;std::ostringstream ignored;
  productFurnitureStructure(id,true,path,ignored,job.protectedFurnitureMembers,kitchen);
  job.protectedFurnitureMembers.insert(ascii(handle));
 }
}

std::vector<ProductFurnitureItem> productApplyPlanFurniture(ProductJob& job,AcDbDatabase& db,
 const std::map<std::string,ProductFurnitureExclusion>& exclusions){
 if(!job.unifiedPlan&&!job.currentPlanRules)return {};
 Working working(&db);
 std::set<std::string> confirmed;
 for(const auto& group:productFurnitureConfirmed(job))confirmed.insert(group.begin(),group.end());
 const auto roots=modelTypes(db);
 for(const auto& key:confirmed)if(!roots.count(bcpproduct::decodeMessage(key)))
  job.detailPending.push_back({u8"家具原件归层",key,u8"登记句柄未读到模型空间原件，须核归属。"});
 std::vector<ProductFurnitureItem> result;
 for(const auto& [handle,type]:roots){
  const auto key=ascii(handle);
  if(exclusions.count(key))continue;
  const auto id=find(db,key);
  const auto entity=open<AcDbEntity>(id);
  if(!productPlanDimensionVisible(*entity))continue;
  const auto* ref=AcDbBlockReference::cast(entity.get());
  std::set<AcDbObjectId> active;
  if(!confirmed.count(key)&&(!ref||!productFurnitureNestedEvidence(*entity,active)))continue;
  if(!ref&&!entity->isKindOf(AcDbCurve::desc())&&!entity->isKindOf(AcDbHatch::desc())&&!entity->isKindOf(AcDbRegion::desc())){
   job.detailPending.push_back({u8"家具原件归层",key,u8"登记对象用途需核实，已保留。"});continue;
  }
  try{
   require(!open<AcDbLayerTableRecord>(entity->layerId())->isLocked(),"source furniture layer is locked");
   bool kitchen=false;
   std::ostringstream before;before<<std::fixed<<std::setprecision(6);
   std::set<std::string> members;
   require(productFurnitureStructure(id,true,active,before,members,kitchen),"furniture contains an external or recursive reference");
   const std::wstring target=kitchen?L"A-洁具、厨具":L"A-家具";
   const auto layer=productPlanLayer(db,target.c_str(),kitchen?165:254);
   const bool changed=entity->layerId()!=layer;
   if(changed){
    productStatus(entity->upgradeOpen(),"open original furniture for layer change");
    productStatus(entity->setLayer(layer,false),"set original furniture layer without changing attributes");
   }
   job.protectedFurnitureMembers.insert(members.begin(),members.end());
   job.protectedFurnitureMembers.insert(key);
   result.push_back({{key},key,target,before.str(),std::move(members),changed});
  }catch(const std::exception& error){job.detailPending.push_back({u8"家具原件归层",key,error.what()});}
 }
 return result;
}

void productVerifyPlanFurniture(ProductJob& job,AcDbDatabase& db,const std::vector<ProductFurnitureItem>& items,
 const std::map<std::string,ProductFurnitureExclusion>& exclusions){
 if(!job.unifiedPlan&&!job.currentPlanRules)return;
 Working context(&db);
 std::ostringstream out;out<<"{\"policy\":\"source-layer-only\",\"savedReopened\":true,\"items\":[";
 bool first=true,allVerified=true;
 for(const auto& item:items){
  const auto id=find(db,item.output);
  std::set<AcDbObjectId> active;std::set<std::string> members;bool kitchen=false;
  std::ostringstream actual;actual<<std::fixed<<std::setprecision(6);
  const bool intact=productFurnitureStructure(id,true,active,actual,members,kitchen)&&actual.str()==item.signature&&members==item.members;
  const auto entity=open<AcDbEntity>(id);
  const auto layer=open<AcDbLayerTableRecord>(entity->layerId());
  const bool layerOK=symbolName(entity->layerId())==item.layer&&layer->color().colorIndex()==(kitchen?165:254);
  if(!first)out<<',';first=false;
  const std::string row="{\"object\":\""+item.output+"\",\"sources\":[\""+item.output+"\"],\"targetLayer\":"+jsonUtf8(item.layer)+
   ",\"memberCount\":"+std::to_string(members.size())+",\"layerChanged\":"+(item.changed?"true":"false")+
   ",\"geometryVerified\":"+(intact?"true":"false")+",\"layerVerified\":"+(layerOK?"true":"false")+
   ",\"result\":\""+(layerOK&&intact?"verified-original-instance":"pending")+"\"}";
  out<<row;
  for(auto rule:{"NC08","NC10","NC11","NC14","NC15","NC16","NC17","Y09","QA04","QA05"})productRecordRuleEvidence(job,rule,row);
  if(!intact||!layerOK){
   allVerified=false;
   job.detailPending.push_back({u8"家具保存重开",item.output,u8"原件结构、属性、几何或目标图层未能对应。"});
  }
 }
 out<<"],\"excluded\":[";first=true;
 for(const auto& [h,expected]:exclusions){
  bool retained=false;
  try{const auto actual=chainGrips(find(db,h));
   retained=!expected.points.empty()&&actual.size()==expected.points.size()&&std::equal(actual.begin(),actual.end(),expected.points.begin(),
    [](const auto& a,const auto& b){return a.distanceTo(b)<.01;});}catch(const std::exception&){}
  if(!first)out<<',';first=false;
  const auto row="{\"object\":\""+h+"\",\"role\":\""+expected.role+"\",\"retained\":"+(retained?"true":"false")+"}";
  out<<row;for(auto rule:{"NC12","NC13","NC18"})productRecordRuleEvidence(job,rule,row);
 }
 out<<"]}";writeNew(job.root/L"plan-furniture-readback.json",out.str());
 require(allVerified,"reopened furniture identity, structure, attributes, geometry or layer changed; see plan-furniture-readback.json");
}
