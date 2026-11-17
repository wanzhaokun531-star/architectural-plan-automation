struct ProductPlanCleanupReceipt {
 std::string source,keeper,role;
 std::size_t panel{};
 std::vector<AcGePoint3d> keeperGrips;
 bool erased{};
};

struct ProductSourceMemberCheck {
 std::string path,root;
 std::size_t panel{};
};

std::vector<ProductSourceMemberCheck> productSourceWindowStrokeCandidates(AcDbDatabase& db,
 const std::vector<bcpproduct::GenericView>& views,double unit){
 struct WindowStroke {std::string handle;AcGePoint3d first,last;};
 std::vector<WindowStroke> strokes;
 for(const auto& [handle,type]:modelTypes(db)){
  if(type!=L"TDbOpening"&&type!=L"TDbGroupOpening")continue;
  const auto entity=open<AcDbEntity>(find(db,ascii(handle)));
  if(!productPlanDimensionVisible(*entity))continue;
  GenericOpeningInk ink;
  productGenericOpeningInk(*entity,ink);
  for(const auto& [first,last]:ink.straight)if(first.distanceTo(last)*unit>100.)
   strokes.push_back({ascii(handle),first,last});
 }
 std::vector<ProductSourceMemberCheck> result;
 std::set<std::string> recorded;
 std::set<AcDbObjectId> active;
 const auto inspect=[&](auto&& visit,AcDbObjectId id,const AcGeMatrix3d& transform,const std::string& parent)->void{
  const auto entity=open<AcDbEntity>(id);
  if(!productPlanDimensionVisible(*entity))return;
  const auto path=parent+axisHandle(id);
  if(const auto* ref=AcDbBlockReference::cast(entity.get())){
   const auto definition=ref->blockTableRecord();
   const auto record=open<AcDbBlockTableRecord>(definition);
   if(record->isFromExternalReference()||record->isFromOverlayReference()||active.size()>=32||
      !active.insert(definition).second)return;
   for(auto member:productFurnitureMembers(definition))visit(visit,member,transform*ref->blockTransform(),path+"/");
   active.erase(definition);
   return;
  }
  AcGePoint3d first,last;
  if(const auto* line=AcDbLine::cast(entity.get())){first=line->startPoint();last=line->endPoint();}
  else if(const auto* poly=AcDbPolyline::cast(entity.get());poly&&!poly->isClosed()&&poly->numVerts()==2&&
          poly->segType(0)==AcDbPolyline::kLine&&poly->getPointAt(0,first)==Acad::eOk&&poly->getPointAt(1,last)==Acad::eOk){}
  else return;
  first.transformBy(transform);last.transformBy(transform);
  if(first.distanceTo(last)*unit<=100.)return;
  const auto sameEndpoint=[&](const auto& a,const auto& b){return a.distanceTo(b)*unit<.01;};
  for(const auto& stroke:strokes){
   if(!((sameEndpoint(first,stroke.first)&&sameEndpoint(last,stroke.last))||
        (sameEndpoint(first,stroke.last)&&sameEndpoint(last,stroke.first))))continue;
   if(!recorded.insert(path+"/"+stroke.handle).second)continue;
   const auto center=first+(last-first)*.5;
   result.push_back({path,stroke.handle,bcpproduct::nearestGenericView(views,{center.x,center.y})+1});
  }
 };
 for(const auto& [handle,type]:modelTypes(db))if(type==L"AcDbLine"||type==L"AcDbPolyline"||type==L"AcDbBlockReference")
  inspect(inspect,find(db,ascii(handle)),AcGeMatrix3d::kIdentity,{});
 return result;
}

void productAuditSourceWindowStrokes(ProductJob& job,AcDbDatabase& db,
 const std::vector<bcpproduct::GenericView>& views,double unit,const std::vector<ProductSourceMemberCheck>& sourceMatches){
 if(!job.unifiedPlan)return;
 const auto matches=productSourceWindowStrokeCandidates(db,views,unit);
 const auto record=[&](const ProductSourceMemberCheck& row,const char* stage){
  productPlanAuditRow(job,{"NC19"},row.panel,row.path,"native-opening-overlap-candidate",
   "pending-source-window-stroke-purpose",u8"实体线段与原生洞口可见笔画端点重合；仍需确认构造、开启或投影用途后再决定清理",
   "\"nativeOpening\":\""+row.root+"\",\"inspectionStage\":\""+stage+"\"");
 };
 for(const auto& row:sourceMatches)record(row,"source");
 for(const auto& row:matches)record(row,"saved");
 if(matches.empty()&&sourceMatches.empty())productPlanAuditRow(job,{"NC19"},0,"","exact-source-opening-stroke-reconciliation",
  "verified-no-exact-line-overlap-candidates",u8"已遍历模型直接线、普通块及嵌套块的公开直线笔画；短段、部分重叠及复杂曲线用途按实际复核");
}

std::vector<ProductPlanCleanupReceipt> productCleanConfirmedSourceDuplicates(ProductJob& job,AcDbDatabase& db,
 const std::vector<bcpproduct::GenericView>& views,double unit){
 std::vector<ProductPlanCleanupReceipt> result;
 if(!job.unifiedPlan)return result;
 Working work(&db);
 const auto confirmations=productPlanDetailConfirmations(job);
 for(const auto& [source,association]:confirmations.redundantSourceLines){
  const auto& [keeper,role]=association;
  ProductPlanCleanupReceipt receipt{source,keeper,role};
  try{
   const auto discarded=productOutdoorEdge(db,source,.01/unit);
   const auto retained=productOutdoorEdge(db,keeper,.01/unit);
   const auto same=[&](const auto& a,const auto& b){return a.distanceTo(b)*unit<.01;};
   if(discarded&&retained){
    const auto center=retained->first+(retained->second-retained->first)*.5;
    receipt.panel=bcpproduct::nearestGenericView(views,{center.x,center.y})+1;
    const bool sameSegment=(same(discarded->first,retained->first)&&same(discarded->second,retained->second))||
     (same(discarded->first,retained->second)&&same(discarded->second,retained->first));
    const auto discardId=find(db,source);
    const auto keepId=find(db,keeper.substr(0,keeper.find(':')));
    auto object=open<AcDbEntity>(discardId);
    const auto keep=open<AcDbEntity>(keepId);
    const auto layer=open<AcDbLayerTableRecord>(object->layerId());
    const auto keepLayer=open<AcDbLayerTableRecord>(keep->layerId());
    const auto displayLinetype=[&](const auto& entity){
     const auto id=entity.linetypeId();
     return symbolName(id)==L"ByLayer"?open<AcDbLayerTableRecord>(entity.layerId())->linetypeObjectId():id;
    };
    const auto* reactors=object->reactors();
    const auto xdata=std::unique_ptr<resbuf,decltype(&acutRelRb)>(object->xData(),&acutRelRb);
    const bool safe=AcDbLine::cast(object.get())&&object->ownerId()==modelId(db)&&keep->ownerId()==modelId(db)&&
     object->extensionDictionary().isNull()&&!xdata&&(!reactors||reactors->isEmpty())&&!layer->isLocked()&&
     (!layer->isPlottable()||keepLayer->isPlottable())&&
     confirmations.redundantSourceLines.count(axisHandle(keepId))==0&&discardId!=keepId&&
     displayLinetype(*object)==displayLinetype(*keep)&&std::abs(object->linetypeScale()-keep->linetypeScale())<1e-8;
    if(sameSegment&&safe){
     receipt.keeperGrips=chainGrips(keepId);
     object.reset();
     productStatus(editObject<AcDbEntity>(discardId)->erase(),"retire confirmed redundant source line");
     receipt.erased=true;
    }
   }
  }catch(const std::exception&){}
  if(!receipt.erased)job.detailPending.push_back({u8"源重叠线",source,
   u8"已登记的重复线与保留边未通过端点、层线型、关联或可安全清理核验，保留原样"});
  result.push_back(std::move(receipt));
 }
 return result;
}

void productVerifyCleanSourceDuplicates(ProductJob& job,AcDbDatabase& db,
 const std::vector<ProductPlanCleanupReceipt>& receipts,double unit){
 if(!job.unifiedPlan)return;
 const auto model=modelTypes(db);
 if(receipts.empty())productPlanAuditRow(job,{"NC06","NC07"},0,"","confirmed-source-line-cleanup",
  "not-applicable-no-confirmed-source-duplicate-pair",u8"本输入未登记已确定同用途的可删除整段线；现有投影和构造线保留");
 for(const auto& receipt:receipts){
  const auto keeper=receipt.keeper.substr(0,receipt.keeper.find(':'));
  bool retained=false;
  try{
   const auto points=chainGrips(find(db,keeper));
   retained=points.size()==receipt.keeperGrips.size()&&std::equal(points.begin(),points.end(),receipt.keeperGrips.begin(),
    [&](const auto& a,const auto& b){return a.distanceTo(b)*unit<.01;});
  }catch(const std::exception&){}
  const bool absent=!model.count(bcpproduct::decodeMessage(receipt.source));
  const auto verified=receipt.erased&&retained&&absent;
  productPlanAuditRow(job,{"NC06","NC07","QA05"},receipt.panel,receipt.source,"source-line-replacement",
   verified?"verified-redundant-source-line-retired":"pending-duplicate-source-line",
   verified?u8"同用途整段端点及有效线型相同，保留边重开实际夹点一致":u8"未证实原线清理和保留边几何两项，需逐处复核",
   "\"keeper\":\""+receipt.keeper+"\",\"role\":\""+receipt.role+"\",\"retainedPoints\":"+wholePoints(receipt.keeperGrips));
 }
}

void productAuditPlanColumnAndFacilities(ProductJob& job,AcDbDatabase& db,const GenericNativeInput& input,
 const std::vector<bcpproduct::GenericView>& views){
 if(!job.unifiedPlan)return;
 Working work(&db);
 for(const auto& [handle,type]:modelTypes(db)){
  const auto key=ascii(handle);
  const auto entity=open<AcDbEntity>(find(db,key));
  if(!productPlanDimensionVisible(*entity))continue;
  const auto layer=symbolName(entity->layerId());
  const bool column=layer==L"COLUMN"||layer.find(L"柱")!=std::wstring::npos;
  const bool service=layer.find(L"空调")!=std::wstring::npos||layer.find(L"管井")!=std::wstring::npos||
   layer.find(L"立管")!=std::wstring::npos||layer.find(L"套管")!=std::wstring::npos;
  if(!column&&!service)continue;
  AcDbExtents box;
  if(entity->getGeomExtents(box)!=Acad::eOk)continue;
  const auto center=box.minPoint()+(box.maxPoint()-box.minPoint())*.5;
  const auto panel=bcpproduct::nearestGenericView(views,{center.x,center.y})+1;
  if(column){
   const auto source=std::find_if(input.columns.begin(),input.columns.end(),[&](const auto& item){return item.handle==key;});
   const auto points=chainGrips(entity->objectId());
   const bool retained=type==L"TDbColumn"&&!entity->isAProxy()&&source!=input.columns.end()&&!points.empty()&&
    points.front().distanceTo(source->centre)*job.unifiedUnitMM<.01;
   productPlanAuditRow(job,{"NC09"},panel,key,"column-source-object-and-center",
    retained?"verified-existing-native-column-preserved":"pending-column-component-and-native-replacement",
    retained?u8"实际原生柱类型和中心已重开保留；柱截面及轴定位见专项":
             u8"柱类图元需要分组确认实际截面和方向，并验证天正替换件",
    "\"type\":"+jsonUtf8(type)+",\"bounds\":"+wholePoints({box.minPoint(),box.maxPoint()}));
  }
  if(service){
   productPlanAuditRow(job,{"B11"},panel,key,"existing-service-geometry",
    "pending-service-dimensions-and-coordination",u8"原位设备或管井表达已重开；规格、管径、标高及专业协调缺项目资料",
    "\"layer\":"+jsonUtf8(layer)+",\"type\":"+jsonUtf8(type)+",\"bounds\":"+wholePoints({box.minPoint(),box.maxPoint()}));
   job.detailPending.push_back({u8"设备与管井专业资料",key,u8"已保留设备或井道图元；缺项目规格、管径、标高及专业协调资料"});
  }
 }
}

void productAuditMainOpeningSourceNotes(ProductJob& job,AcDbDatabase& db,const GenericNativeInput& input,
 const std::vector<bcpproduct::GenericView>& views){
 if(!job.unifiedPlan)return;
 Working work(&db);
 const auto main=productConfirmedMainEntranceSides(job);
 for(const auto& opening:input.openings){
  if(!main.count(opening.handle))continue;
  const auto center=opening.a+(opening.b-opening.a)*.5;
  const auto view=bcpproduct::nearestGenericView(views,{center.x,center.y});
  std::size_t candidates=0;
  for(const auto& note:input.notes){
   const auto marker=note.text.find(L"大门高度");
   if(marker==std::wstring::npos||bcpproduct::nearestGenericView(views,{note.anchor.x,note.anchor.y})!=view)continue;
   ++candidates;
   const auto digit=note.text.find_first_of(L"0123456789",marker);
   const double height=digit==std::wstring::npos?0.:std::wcstod(note.text.c_str()+digit,nullptr);
   const bool mismatch=height>0.&&std::abs(height-opening.height*job.unifiedUnitMM)>.01;
   const auto object=open<AcDbEntity>(find(db,note.handle));
   const bool preserved=productPlanDimensionVisible(*object)&&productPlanNameContents(*object)==note.text;
   productPlanAuditRow(job,{"NC05"},view+1,note.handle,"main-door-source-note-review",
    !preserved||mismatch?"pending-main-door-source-note-intent":"verified-construction-note-retained",
    mismatch?u8"源说明与实际大门洞口高度不一致，需确认原说明用途，保留两项":
             u8"已读回原构造文字，现有资料未确认其失效或与原生对象重复",
    "\"opening\":\""+opening.handle+"\",\"nativeHeightMM\":"+std::to_string(opening.height*job.unifiedUnitMM)+
    ",\"sourceText\":"+jsonUtf8(note.text));
  }
  if(!candidates)productPlanAuditRow(job,{"NC05"},view+1,opening.handle,"main-door-source-note-review",
   "not-applicable-no-native-main-door-height-note",u8"本幅可读原生说明中无大门高度文字，原生洞口已单独验收；旧描线若另有项目指定须核真实对应关系");
 }
}
