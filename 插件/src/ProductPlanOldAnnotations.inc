#include "dbproxy.h"

using ProductSourceAnnotation=ProductJob::SourceAnnotation;
#include "ProductPlanLooseDimensions.inc"

std::string productOldAnnotationKind(const AcDbEntity& entity,const std::wstring& type){
 if(AcDbDimension::cast(&entity)||type.find(L"Dimension")!=std::wstring::npos)return "dimension";
 if(AcDbText::cast(&entity)||AcDbMText::cast(&entity)||type==L"TDbText"||type==L"TDbMText"||type==L"TDbDrawingName")
  return "text";
 if(type==L"AcDbMLeader"||type==L"TDbSymbMultiLeader"||type==L"AcDbTable")return "linked-text";
 if(const auto* proxy=AcDbProxyEntity::cast(&entity)){
  const auto* name=proxy->originalClassName();
  const std::wstring original=name?name:L"";
  if(original.find(L"Dimension")!=std::wstring::npos)return "unreadable-dimension";
  if(original.find(L"Text")!=std::wstring::npos||original.find(L"DrawingName")!=std::wstring::npos||
     original.find(L"Leader")!=std::wstring::npos)return "unreadable-text";
 }
 return {};
}

std::vector<ProductSourceAnnotation> productReadOldAnnotations(AcDbDatabase& db,
 std::vector<ProductLooseDimensionStroke>* ink=nullptr,AcDbObjectId paperSpace={}){
 std::vector<ProductSourceAnnotation> output;
 std::set<AcDbObjectId> definitions;
 const auto font=[](AcDbObjectId id,std::wstring text,double height){
  ProductJob::SourceAnnotationGlyph row;
  row.text=std::move(text);
  row.height=std::isfinite(height)?height:0;
  if(!id.isNull()){
   const auto style=productReadDimensionFont(id);
   row.style=style.name;row.primary=style.primary;row.big=style.big;
   row.width=std::isfinite(style.width)?style.width:0;
  }
  return row;
 };
 const auto walk=[&](auto&& self,AcDbObjectId id,const std::string& parent,const std::string& parentRoot,
  const AcGeMatrix3d& transform,bool shown,bool external,std::size_t depth)->void{
  const auto entity=open<AcDbEntity>(id);
  const auto handle=axisHandle(id),path=parent.empty()?handle:parent+"/"+handle;
  const auto& root=parentRoot.empty()?handle:parentRoot;
  const std::wstring type=entity->isA()->name();
  const auto layer=open<AcDbLayerTableRecord>(entity->layerId());
  const auto layerName=symbolName(entity->layerId());
  shown=shown&&productPlanDimensionVisible(*entity)&&layer->isPlottable();
  if(const auto* attribute=AcDbAttribute::cast(entity.get());attribute&&attribute->isInvisible())shown=false;
  if(const auto* definition=AcDbAttributeDefinition::cast(entity.get())){
   if(!definition->isConstant())return; // Each insert owns its editable attribute value.
   shown=shown&&!definition->isInvisible();
  }
  if(ink&&shown&&!entity->isAProxy()){
   const auto stroke=[&](AcGePoint3d a,AcGePoint3d b,bool standalone){
    a.transformBy(transform);b.transformBy(transform);
    if(a.distanceTo(b)>1e-6)ink->push_back({path,a,b,standalone});
   };
   if(const auto* line=AcDbLine::cast(entity.get()))
    stroke(line->startPoint(),line->endPoint(),!external&&parent.empty());
   else if(const auto* poly=AcDbPolyline::cast(entity.get())){
    const auto count=poly->numVerts();
    if(count>1)for(unsigned i=0;i<count-(poly->isClosed()?0:1);++i){
     AcGePoint3d a,b;
     if(poly->segType(i)==AcDbPolyline::kLine&&poly->getPointAt(i,a)==Acad::eOk&&
        poly->getPointAt((i+1)%count,b)==Acad::eOk)stroke(a,b,false);
    }
   }
  }
  const auto snapshot=[&](std::string kind){
   ProductSourceAnnotation row;
   row.path=path;row.root=root;row.handle=handle;row.kind=std::move(kind);row.type=type;
   row.layer=layerName;row.visible=shown;row.external=external;
   AcDbExtents box;
   if(entity->getGeomExtents(box)==Acad::eOk){
    auto position=box.minPoint()+(box.maxPoint()-box.minPoint())*.5;
    position.transformBy(transform);
    if(std::isfinite(position.x)&&std::isfinite(position.y)&&std::isfinite(position.z))row.position=position;
    box.transformBy(transform);
    row.bounds=box;
   }
   return row;
  };
  if(const auto* block=AcDbBlockReference::cast(entity.get())){
   std::unique_ptr<AcDbObjectIterator> attributes(block->attributeIterator());
   if(attributes)for(;!attributes->done();attributes->step())
    self(self,attributes->objectId(),path,root,transform,shown,external,depth+1);
   const auto definition=block->blockTableRecord();
   const auto record=open<AcDbBlockTableRecord>(definition);
   AcDbBlockTableRecordIterator* raw{};
   if(depth>=20||definitions.count(definition)||record->isUnloaded()||record->newIterator(raw)!=Acad::eOk){
    auto row=snapshot("unreadable-container");
    row.external=external||record->isFromExternalReference();
    output.push_back(std::move(row));
    return;
   }
   definitions.insert(definition);
   std::unique_ptr<AcDbBlockTableRecordIterator> iterator(raw);
   for(;!iterator->done();iterator->step()){
    AcDbObjectId child;
    if(iterator->getEntityId(child)==Acad::eOk)
     self(self,child,path,root,transform*block->blockTransform(),shown,external||record->isFromExternalReference(),depth+1);
   }
   definitions.erase(definition);
   return;
  }
  const auto kind=productOldAnnotationKind(*entity,type);
  if(kind.empty())return;
  auto row=snapshot(kind);
  try{
  if(kind=="dimension"){
   for(const auto& part:productDimensionTexts(*entity))
    row.glyphs.push_back(font(part.font,part.style.isNull()?L"":symbolName(part.style),part.height*part.scale));
  }else if(kind=="text"||kind=="linked-text"){
   std::vector<ProductRoomLeaf> leaves;
   productRoomLeaves(*entity,leaves);
   const auto setDirection=[&](double angle){
    AcGeVector3d direction(std::cos(angle),std::sin(angle),0.);
    direction.transformBy(transform);
    if(direction.length()>1e-8)row.textDirection=direction.normal();
   };
   if(!leaves.empty())setDirection(leaves.front().rotation);
   for(const auto& leaf:leaves)row.glyphs.push_back(font(leaf.style,leaf.text,leaf.height));
   if(row.glyphs.empty())if(const auto* text=AcDbMText::cast(entity.get())){
    AcString plain;
    if(text->text(plain)==Acad::eOk){
     row.glyphs.push_back(font(text->textStyle(),plain.kACharPtr(),text->textHeight()));
     setDirection(text->rotation());
    }
   }
  }
  }catch(const std::exception&){
   row.glyphs.clear();
   if(kind!="dimension")row.kind="unreadable-text";
  }
  output.push_back(std::move(row));
 };
 if(paperSpace.isNull()){
  for(const auto& [handle,_]:modelTypes(db))walk(walk,find(db,ascii(handle)),{},{},AcGeMatrix3d::kIdentity,true,false,0);
 }else{
  const auto record=open<AcDbBlockTableRecord>(paperSpace);
  AcDbBlockTableRecordIterator* raw{};
  status(record->newIterator(raw),"enumerate formal paper-space dimensions");
  std::unique_ptr<AcDbBlockTableRecordIterator> iterator(raw);
  for(;!iterator->done();iterator->step()){
   AcDbObjectId member;
   if(iterator->getEntityId(member)==Acad::eOk)
    walk(walk,member,{},{},AcGeMatrix3d::kIdentity,true,false,0);
  }
 }
 return output;
}

void productWriteOldAnnotation(std::ostream& out,const ProductSourceAnnotation& row){
 const auto quoted=[](const std::string& value){return jsonUtf8(bcpproduct::decodeMessage(value));};
 out<<"{\"path\":"<<quoted(row.path)<<",\"root\":"<<quoted(row.root)<<",\"panel\":"<<row.panel
    <<",\"kind\":"<<quoted(row.kind)<<",\"type\":"<<jsonUtf8(row.type)<<",\"layer\":"<<jsonUtf8(row.layer)
    <<",\"visible\":"<<(row.visible?"true":"false")<<",\"external\":"<<(row.external?"true":"false")
    <<",\"position\":"<<wholePoints(row.position?std::vector<AcGePoint3d>{*row.position}:std::vector<AcGePoint3d>{})
    <<",\"glyphs\":[";
 bool first=true;
 for(const auto& glyph:row.glyphs){
  if(!first)out<<',';
  first=false;
  out<<"{\"text\":"<<jsonUtf8(glyph.text)<<",\"style\":"<<jsonUtf8(glyph.style)<<",\"font\":"<<jsonUtf8(glyph.primary)
     <<",\"bigFont\":"<<jsonUtf8(glyph.big)<<",\"height\":"<<glyph.height<<",\"widthFactor\":"<<glyph.width<<'}';
 }
 out<<"]}";
}

void productCaptureOldAnnotations(ProductJob& job,AcDbDatabase& db,const std::vector<bcpproduct::GenericView>& views,
 const std::vector<ProductPanelOwnership>& ownership,const std::vector<ProductPerimeterSourceWitness>& witnesses){
 std::vector<ProductLooseDimensionStroke> strokes;
 job.planSourceAnnotations=productReadOldAnnotations(db,&strokes);
 const auto types=modelTypes(db);
 std::vector<ProductPerimeterSourceWitness> anchors;
 for(const auto& witness:witnesses){
  const auto found=types.find(bcpproduct::decodeMessage(witness.handle));
  if(found!=types.end()&&(found->second==L"TDbWall"||found->second==L"TDbColumn"||
     found->second==L"TDbOpening"||found->second==L"TDbGroupOpening"))anchors.push_back(witness);
 }
 job.planLooseDimensions=productRecognizeLooseDimensions(job.planSourceAnnotations,strokes,anchors,
   job.unifiedScale/job.unifiedUnitMM,job.unifiedUnitMM);
 std::ostringstream out;
 out<<std::setprecision(17)<<"{\"sourceSHA256\":\""<<job.sourceHash<<"\",\"capturedBeforeDeepening\":true,\"objects\":[";
 bool first=true;
 for(auto& row:job.planSourceAnnotations){
  if(job.planLooseDimensions.count(row.path))row.kind="dimension-text";
  if(row.position&&!views.empty()){
   const auto view=bcpproduct::nearestGenericView(views,{row.position->x,row.position->y});
   row.panel=ownership[view].parentPanel.value_or(view)+1;
  }
  if(!first)out<<',';
  first=false;
  productWriteOldAnnotation(out,row);
 }
 out<<"]}";
 const auto path=job.root/L"plan-source-annotations.json";
 writeNew(path,out.str());
 job.planAnnotationInventoryHash=sha(path);
}

bool productSameOldTextAppearance(const ProductSourceAnnotation& source,const ProductSourceAnnotation& saved,
 bool verifiedFurniture=false){
 return source.type==saved.type&&(source.layer==saved.layer||(verifiedFurniture&&saved.layer==L"家具"))&&
  source.glyphs.size()==saved.glyphs.size()&&
  std::equal(source.glyphs.begin(),source.glyphs.end(),saved.glyphs.begin(),[](const auto& a,const auto& b){
   return a.text==b.text&&a.style==b.style&&a.primary==b.primary&&a.big==b.big&&
     productDimensionEqual(a.height,b.height)&&productDimensionEqual(a.width,b.width);
  });
}

void productReconcileOldAnnotations(ProductJob& job,AcDbDatabase& db,const std::string& hash){
 require(!job.planAnnotationInventoryHash.empty()&&
  sha(job.root/L"plan-source-annotations.json")==job.planAnnotationInventoryHash,
  "old annotation inventory changed during generation");
 Working work(&db);
 const auto current=productReadOldAnnotations(db);
 std::map<std::string,const ProductSourceAnnotation*> byPath;
 for(const auto& row:current)byPath.emplace(row.path,&row);
 std::map<std::pair<std::string,std::string>,const bcpproduct::PlanObligation*> bySource;
 for(const auto& [key,item]:job.sourceInventory.items){
  const auto& [panel,scope,path]=key;
  const auto [match,unique]=bySource.emplace(std::make_pair(scope,path),&item);
  if(!unique)match->second=nullptr;
 }
 const auto sourceEvidence=[&](std::size_t panel,const char* scope,const std::string& path)->const bcpproduct::PlanObligation*{
  const auto evidence=job.sourceInventory.items.find({panel,scope,path});
  if(evidence!=job.sourceInventory.items.end())return &evidence->second;
  const auto alternate=bySource.find({scope,path});
  return alternate==bySource.end()?nullptr:alternate->second;
 };
 const auto scope=[&](const ProductSourceAnnotation& row){
  if(!row.visible)return false;
  if(row.position&&!job.planPrintWindows.empty())return std::any_of(job.planPrintWindows.begin(),job.planPrintWindows.end(),[&](const auto& box){
   return row.position->x>=box[0]&&row.position->x<=box[2]&&row.position->y>=box[1]&&row.position->y<=box[3];
  });
  return !row.panel||std::find(job.layoutSourcePanels.begin(),job.layoutSourcePanels.end(),row.panel)!=job.layoutSourcePanels.end();
 };
 const std::set<std::string> textChecks{"room-name-height","source-title-style","area-summary-font","source-text-style"};
 std::map<std::string,std::string> priorOpen;
 std::set<std::string> verifiedText;
 for(const auto& [key,item]:job.drawingChecks.items){
  const auto& [panel,check,handle]=key;
  const bool textStyle=textChecks.count(check)!=0;
  if(!textStyle&&check!="dimension-style"&&check!="source-text-clearance")continue;
  if(item.state==bcpproduct::PlanCheck::failed||item.state==bcpproduct::PlanCheck::pending)
   priorOpen.try_emplace(handle,item.reason);
  if(textStyle&&item.state==bcpproduct::PlanCheck::verified)verifiedText.insert(handle);
 }
 std::ostringstream out;
 out<<"{\"sourceSHA256\":\""<<job.sourceHash<<"\",\"sourceInventorySHA256\":\""<<job.planAnnotationInventoryHash
    <<"\",\"outputSHA256\":\""<<hash<<"\",\"savedReopened\":true,\"objects\":[";
 std::ostringstream loose;
 loose<<std::setprecision(17)<<"{\"sourceSHA256\":\""<<job.sourceHash<<"\",\"outputSHA256\":\""<<hash
  <<"\",\"savedReopened\":true,\"objects\":[";
 AcDbDatabase reference(false,true);
 const bool hasLooseReference=!job.planLooseDimensions.empty()&&
  reference.readDwgFile((job.profile/L"reference.dwg").c_str(),AcDbDatabase::kForReadAndAllShare,false)==Acad::eOk&&
  reference.closeInput(true)==Acad::eOk;
 std::map<std::string,std::size_t> counts;
 bool first=true,firstLoose=true;
 for(const auto& old:job.planSourceAnnotations){
  std::vector<std::string> targets{old.path};
  const auto mapped=job.planAnnotationTextTargets.find(old.path);
  const auto furniture=job.planAnnotationFurnitureTargets.find(old.path);
  const bool verifiedFurniture=furniture!=job.planAnnotationFurnitureTargets.end();
  const auto legacy=job.planLooseDimensions.find(old.path);
  const bool inScope=(legacy!=job.planLooseDimensions.end()&&legacy->second.inSelectedPanel)||scope(old);
  if(legacy!=job.planLooseDimensions.end()&&!legacy->second.output.empty())targets={legacy->second.output};
  const auto* disposition=sourceEvidence(old.panel,"source-dimension-disposition",old.path);
  const auto* name=sourceEvidence(old.panel,"name",old.path);
  if(verifiedFurniture)targets={furniture->second};
  if(mapped!=job.planAnnotationTextTargets.end())targets={mapped->second};
  if(disposition&&!disposition->output.empty())targets=disposition->output;
  const auto requiredDimension=job.planRequiredDimensionReplacements.find(old.handle);
  if(old.kind=="dimension"&&requiredDimension!=job.planRequiredDimensionReplacements.end())
   targets={old.path.substr(0,old.path.size()-old.handle.size())+requiredDimension->second};
  if(name&&!name->output.empty())targets=name->output;
  std::vector<const ProductSourceAnnotation*> actual;
  for(const auto& target:targets){
   const auto found=byPath.find(target);
   if(found!=byPath.end()&&found->second->visible)actual.push_back(found->second);
  }
  std::string status="outside-plot-scope",reason=u8"源标注位于本次平面出图范围之外，保留源图表达";
  const bool unresolvedStyle=priorOpen.count(old.path)||std::any_of(targets.begin(),targets.end(),[&](const auto& handle){
   return priorOpen.count(handle);
  });
  if(!old.visible){status="source-not-printed";reason=u8"源标注原为隐藏或不打印";}
  else if(inScope){
   status="unclosed";
   reason=u8"重开后尚未找到能对应该源标注的合规对象";
   if(old.kind=="dimension"){
    const auto& policy=job.dimensionStylePolicy;
    const auto expected=policy.nominalPaperHeightMM*job.unifiedScale/job.unifiedUnitMM;
    const auto matches=[&](const ProductSourceAnnotation* row){
     if(row->type!=L"TDbDimension2"||row->glyphs.empty())return false;
     return std::all_of(row->glyphs.begin(),row->glyphs.end(),[&](const auto& glyph){
      return glyph.style==L"_TCH_DIM"&&productDimensionFamily(glyph.text,policy.name)&&productDimensionRatioMatches(glyph.text,policy.name,job.unifiedScale)&&
       productDimensionEqual(glyph.height,expected);
     });
    };
    if(disposition&&disposition->check==bcpproduct::PlanCheck::omitted){
     status="omitted-by-rule";reason=disposition->reason;
    }else if(((disposition&&disposition->check==bcpproduct::PlanCheck::verified)||
       requiredDimension!=job.planRequiredDimensionReplacements.end())&&!unresolvedStyle&&
      (byPath.find(old.path)==byPath.end()||!byPath.at(old.path)->visible)&&
      !actual.empty()&&actual.size()==targets.size()&&std::all_of(actual.begin(),actual.end(),matches)){
     status="replaced-verified";
     reason=u8"原尺寸去向、现行原生尺寸样式及名义字高已重开核验";
    }else if(old.path!=old.handle)reason=u8"块内旧尺寸的实例测站、字体及天正链对应关系仍需逐段核实";
    else if(disposition&&!disposition->reason.empty())reason=disposition->reason;
   }else if(old.kind=="unreadable-container")reason=u8"块或外部参照不可读，里面可能包含旧文字和尺寸";
   else if(old.kind=="unreadable-dimension")reason=u8"代理尺寸无法读取真实测点、测值与样式";
   else if(old.kind=="unreadable-text")reason=u8"旧文字的字形样式无法完整读取";
   else if(legacy!=job.planLooseDimensions.end()){
    const auto& row=legacy->second;
    reason=row.reason;
    if(!row.output.empty()){
     const auto retired=[&](const std::string& path){
      const auto id=productLooseDimensionEntity(db,path);
      return !id||!productPlanDimensionVisible(*open<AcDbEntity>(*id));
     };
     if(!unresolvedStyle&&hasLooseReference&&retired(old.path)&&
        std::all_of(row.strokes.begin(),row.strokes.end(),retired)&&
        productVerifyLooseDimensionTarget(job,db,row,reference)){
      status="replaced-verified";
      reason=u8"重开已核原生尺寸的测点、测值、字体、字高和图层，旧文字、旧线均已退出";
     }else reason=u8"重开时对应原生尺寸或旧线退出未全部通过";
    }
   }
   else if(!unresolvedStyle&&name&&name->check==bcpproduct::PlanCheck::verified&&!actual.empty()){
    status=targets==std::vector<std::string>{old.path}?"retained-verified":"replaced-verified";reason=name->reason;
   }else if(!unresolvedStyle&&actual.size()==1&&!actual.front()->glyphs.empty()&&mapped!=job.planAnnotationTextTargets.end()&&
     (byPath.find(old.path)==byPath.end()||!byPath.at(old.path)->visible)){
    status="replaced-verified";reason=u8"已核名称或面积规则下的替换对象在最终图中可见";
   }else if(!unresolvedStyle&&actual.size()==1&&!actual.front()->glyphs.empty()&&verifiedText.count(old.path)){
    status="restyled-verified";reason=u8"该文字按用途对应的现行样式已在重开图中核验";
   }else if(!unresolvedStyle&&verifiedFurniture&&actual.size()==1&&!old.glyphs.empty()&&
     productSameOldTextAppearance(old,*actual.front(),true)){
    status="retained-verified";reason=u8"家具换块后，重开逐成员核对原文字、字体、字高和家具图层";
   }else if(!unresolvedStyle&&!name&&actual.size()==1&&!old.glyphs.empty()&&productSameOldTextAppearance(old,*actual.front())){
    status="retained-source-text";reason=u8"同句柄、原文、字体定义、字高和图层仍可读，按源文字用途保留";
   }
   if(status=="unclosed"){
    const auto specialist=priorOpen.find(old.path);
    if(specialist!=priorOpen.end()&&!specialist->second.empty())reason=specialist->second;
   }
   if(status=="unclosed")job.drawingChecks.record(old.panel,"source-annotation-reconciliation",old.path,
    old.kind=="dimension"||old.kind=="unreadable-dimension"||old.kind=="dimension-text"?std::vector<std::string>{"D22","Q03","G06","Y04"}:
     std::vector<std::string>{"L06","G06","P04"},bcpproduct::PlanReleaseGate::reopened,
    bcpproduct::PlanCheck::pending,u8"深化前逐对象登记并对照最终重开图",reason);
  }
  if(legacy!=job.planLooseDimensions.end()){
   const auto& row=legacy->second;
   std::ostringstream item;
   item<<"{\"panel\":"<<old.panel<<",\"source\":\""<<old.path<<"\",\"value\":"<<row.value
       <<",\"layer\":"<<jsonUtf8(old.layer)<<",\"layerHint\":"<<(row.dimensionLayerHint?"true":"false")
       <<",\"stations\":"<<wholePoints(row.stations)<<",\"sourceStrokes\":";
   productWriteHandleArray(item,row.strokes);
   item<<",\"sourceAnchors\":";
   productWriteHandleArray(item,row.anchors);
   item<<",\"output\":\""<<row.output<<"\",\"result\":\""<<(status=="unclosed"?"pending-loose-dimension":
      status=="replaced-verified"?"verified-loose-dimension-replacement":"not-applicable-outside-sheet")
       <<"\",\"reason\":"<<jsonUtf8(bcpproduct::decodeMessage(reason))<<'}';
   if(!firstLoose)loose<<',';
   firstLoose=false;
   loose<<item.str();
   productRecordRuleEvidence(job,"TL09",item.str());
  }
  ++counts[status];
  if(!first)out<<',';
  first=false;
  out<<"{\"source\":";productWriteOldAnnotation(out,old);
  out<<",\"result\":\""<<status<<"\",\"reason\":"<<jsonUtf8(bcpproduct::decodeMessage(reason))<<",\"targets\":[";
  for(std::size_t i=0;i<actual.size();++i){if(i)out<<',';productWriteOldAnnotation(out,*actual[i]);}
  out<<"]}";
 }
 out<<"],\"count\":"<<job.planSourceAnnotations.size()<<",\"counts\":{";
 first=true;
 for(const auto& [key,value]:counts){if(!first)out<<',';first=false;out<<'"'<<key<<"\":"<<value;}
 out<<"}}";
 writeNew(job.root/L"plan-annotation-reconciliation.json",out.str());
 loose<<"]}";
 writeNew(job.root/L"plan-loose-dimension-review.json",loose.str());
 if(firstLoose)productRecordRuleEvidence(job,"TL09","{\"scope\":\"source-drawing\","
   "\"result\":\"not-applicable-no-loose-dimension-pattern\",\"savedReopened\":true}");
}
