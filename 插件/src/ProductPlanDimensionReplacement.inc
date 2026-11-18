struct ProductDimensionReplacement {
 std::string source, target, issue;
 std::size_t panel{};
 ProductJob::SourceDimension geometry;
 bool rebuiltFromStations{};
};

struct ProductPlanDimensionRun {
 std::vector<ProductDimensionReplacement> objects;
 ProductDimensionFont font;
 bool fontReady{};
};

bool productPlanDimensionFontConforms(AcDbObjectId id,const ProductDimensionFont& expected){
 const auto record=open<AcDbTextStyleTableRecord>(id);
 const auto actual=productReadDimensionFont(id);
 auto* annotation=AcDbAnnotativeObjectPE::cast(record->queryX(AcDbAnnotativeObjectPE::desc()));
 return actual.name==expected.name&&productSameDimensionFont(actual,expected)&&
        productDimensionEqual(record->textSize(),0)&&!record->isVertical()&&
        (!annotation||!annotation->annotative(record.get()));
}

AcDbObjectId productExactPlanDimensionFont(AcDbDatabase& db,const ProductDimensionFont& expected){
 AcDbObjectId found;
 {
  const auto table=open<AcDbTextStyleTable>(db.textStyleTableId());
  table->getAt(expected.name.c_str(),found);
 }
 if(!found.isNull()){
  if(productPlanDimensionFontConforms(found,expected))return found;
  const auto sourceName=symbolName(found);
  auto record=editObject<AcDbTextStyleTableRecord>(found);
  if(sourceName!=expected.name)status(record->setName(expected.name.c_str()),"restore exact dimension text style name");
  status(record->setFileName(expected.primary.c_str()),"restore dimension primary font");
  status(record->setBigFontFileName(expected.big.c_str()),"restore dimension big font");
  status(record->setTextSize(0),"restore variable dimension text height");
  status(record->setXScale(expected.width),"restore dimension text width factor");
  status(record->setObliquingAngle(expected.oblique),"restore dimension text obliquing");
  record->setFlagBits(static_cast<Adesk::UInt8>(expected.flags));
  record->setIsVertical(false);
  auto* annotation=AcDbAnnotativeObjectPE::cast(record->queryX(AcDbAnnotativeObjectPE::desc()));
  if(annotation&&annotation->annotative(record.get()))
   status(annotation->setAnnotative(record.get(),false),"restore non-annotative dimension text style");
  record.reset();
  require(productPlanDimensionFontConforms(found,expected),"restored dimension text style does not match Y10");
  return found;
 }
 const auto id=productPlanDimensionFont(db,expected);
 require(productPlanDimensionFontConforms(id,expected),"created dimension text style does not match Y10");
 return id;
}

std::map<std::wstring,AcDbObjectId> productPlanDimensionTableStyles(AcDbDatabase& db,const std::wstring& base){
 const auto table=open<AcDbDimStyleTable>(db.dimStyleTableId());
 AcDbObjectId baseId;
 table->getAt(base.c_str(),baseId);
 std::map<std::wstring,AcDbObjectId> styles{{base,baseId}};
 AcDbDimStyleTableIterator* raw{};
 status(table->newIterator(raw),"list plan dimension style table records");
 std::unique_ptr<AcDbDimStyleTableIterator> records(raw);
 for(;!records->done();records->step()){
  AcDbObjectId id;
  status(records->getRecordId(id),"read plan dimension style table record");
  const auto name=symbolName(id);
  if(id!=baseId&&productDimensionFamily(name,base))styles.emplace(name,id);
 }
 return styles;
}

void productSynchronizePlanDimensionTable(const ProductJob& job,AcDbDatabase& db,AcDbDatabase& reference,
 AcDbObjectId font){
 const auto& base=job.dimensionStylePolicy.name;
 // All plan paths restore the current mandatory text reference after imports.
 productPlanDimensionBase(db,reference,base);
 for(const auto& [name,id]:productPlanDimensionTableStyles(db,base)){
  if(id.isNull()||open<AcDbDimStyleTableRecord>(id)->dimtxsty()==font)continue;
  auto style=editObject<AcDbDimStyleTableRecord>(id);
  if(style->setDimtxsty(font)!=Acad::eOk)
   throw std::runtime_error("bind _TCH_DIM to plan dimension style "+ascii(name));
 }
}

struct ProductPlanDimensionTableRow {
 std::wstring name,textStyle;
 std::string reason;
 bool definitionVerified{};
};

std::vector<ProductPlanDimensionTableRow> productReadPlanDimensionTable(AcDbDatabase& db,
 const std::wstring& base,const ProductDimensionFont& approved){
 std::vector<ProductPlanDimensionTableRow> rows;
 for(const auto& [name,id]:productPlanDimensionTableStyles(db,base)){
  ProductPlanDimensionTableRow row;
  row.name=name;
  if(id.isNull())row.reason=u8"缺少基础平面尺寸样式";
  else{
   const auto font=open<AcDbDimStyleTableRecord>(id)->dimtxsty();
   if(!font.isNull()){
    row.textStyle=symbolName(font);
    row.definitionVerified=productPlanDimensionFontConforms(font,approved);
   }
   if(row.textStyle!=approved.name)row.reason=u8"尺寸样式表的文字样式引用须为 _TCH_DIM";
   else if(!row.definitionVerified)row.reason=u8"尺寸样式表引用的 _TCH_DIM 实际定义不符合 Y10";
  }
  rows.push_back(std::move(row));
 }
 return rows;
}

void productWritePlanDimensionTable(std::ostream& out,const std::vector<ProductPlanDimensionTableRow>& rows){
 const auto unresolved=std::count_if(rows.begin(),rows.end(),[](const auto& row){return !row.reason.empty();});
 out<<"{\"checked\":"<<rows.size()<<",\"unresolved\":"<<unresolved
    <<",\"allApplicableVerified\":"<<(unresolved?"false":"true")<<",\"styles\":[";
 bool first=true;
 for(const auto& row:rows){
  if(!first)out<<',';
  first=false;
  out<<"{\"name\":"<<jsonUtf8(row.name)<<",\"textStyle\":"<<jsonUtf8(row.textStyle)
     <<",\"definitionVerified\":"<<(row.definitionVerified?"true":"false")
     <<",\"verified\":"<<(row.reason.empty()?"true":"false")
     <<",\"reason\":"<<jsonUtf8(bcpproduct::decodeMessage(row.reason))<<'}';
 }
 out<<"]}";
}

std::size_t productPrintedDimensionPanel(const ProductJob& job,const ProductSourceAnnotation& row){
 if(!row.visible)return {};
 if(row.panel)return row.panel;
 if(!row.bounds)return {};
 const auto lo=row.bounds->minPoint(),hi=row.bounds->maxPoint();
 for(std::size_t i=0;i<job.planPrintWindows.size();++i){
  const auto& box=job.planPrintWindows[i];
  if(hi.x>=box[0]&&lo.x<=box[2]&&hi.y>=box[1]&&lo.y<=box[3])
   return i<job.layoutSourcePanels.size()?job.layoutSourcePanels[i]:i+1;
 }
 return {};
}

AcDbObjectId productCloneDimensionToOwner(AcDbDatabase& db,AcDbObjectId source,AcDbObjectId owner){
 AcDbObjectIdArray objects;
 objects.append(source);
 AcDbIdMapping mapping;
 status(db.deepCloneObjects(objects,owner,mapping,false),"create a new native dimension object");
 AcDbIdPair copied;
 copied.setKey(source);
 require(mapping.compute(copied)&&copied.isCloned()&&!copied.value().isNull()&&copied.value()!=source,
         "dimension replacement has a distinct native object identity");
 auto record=open<AcDbBlockTableRecord>(owner);
 AcDbSortentsTable* raw{};
 if(record->getSortentsTable(raw,AcDb::kForWrite,true)==Acad::eOk){
  std::unique_ptr<AcDbSortentsTable,Close<AcDbSortentsTable>> order(raw);
  AcDbObjectIdArray replacements;
  replacements.append(copied.value());
  order->moveBelow(replacements,source);
 }
 return copied.value();
}

bool productRequiredDimensionTypography(const ProductDimensionText& part,const ProductJob& job,
 const ProductDimensionFont& approved){
 const auto expected=job.dimensionStylePolicy.nominalPaperHeightMM*job.unifiedScale/job.unifiedUnitMM;
 return productDimensionMatches(part,job.dimensionStylePolicy.name,job.unifiedScale,expected)&&
   !part.font.isNull()&&productPlanDimensionFontConforms(part.font,approved)&&
   productDimensionEqual(part.measurementFactor,job.unifiedUnitMM);
}

bool productDimensionCloneIsEqual(AcDbDatabase& db,const ProductDimensionReplacement& item,bool checkBaseline=true){
 const auto actual=productReadSourceDimension(db,item.target,.01);
 if(!productSameDimensionGeometry(item.geometry,actual,.01,false)||
    item.geometry.automaticText!=actual.automaticText||item.geometry.displayOverride!=actual.displayOverride)return false;
 if(!checkBaseline)return true;
 if(item.rebuiltFromStations){
  const auto along=(item.geometry.points.back()-item.geometry.points.front()).normal();
  const AcGeVector3d normal(-along.y,along.x,0.);
  return std::abs((actual.baseline-item.geometry.baseline).dotProduct(normal))<=.01;
 }
 return item.geometry.baseline.distanceTo(actual.baseline)<=.01;
}

ProductPlanDimensionRun productEnforcePlanDimensions(ProductJob& job,AcDbDatabase& db,AcDbDatabase& reference,
 const std::set<std::string>& inputHandles,bool replaceAll,bool wholeDrawing=false){
 ProductPlanDimensionRun run;
 const auto& policy=job.dimensionStylePolicy;
 const auto ratio=policy.name+L"&&"+std::to_wstring(static_cast<long long>(std::llround(job.unifiedScale)));
 auto standard=productDimensionReference(reference,ratio);
 if(!standard)standard=productDimensionReference(reference,policy.name);
 AcDbObjectId font;
 run.font={L"_TCH_DIM",L"simplex.shx",L"gbcbig.shx",1.,0.,0};
 if(standard&&productDimensionFontAvailable(db,run.font.primary)&&productDimensionFontAvailable(db,run.font.big)){
  font=productExactPlanDimensionFont(db,run.font);
  run.fontReady=true;
  productSynchronizePlanDimensionTable(job,db,reference,font);
 }
 std::set<std::string> visited, outside;
 AcDbObjectId prototype;
 auto inventory=productReadOldAnnotations(db);
 if(auto* layouts=acdbHostApplicationServices()->layoutManager()){
  for(const auto& [panel,sheet]:job.planSheetLayouts){
   const auto layout=layouts->findLayoutNamed(sheet.name.c_str(),&db);
   if(layout.isNull())continue;
   const auto record=open<AcDbLayout>(layout);
   auto paper=productReadOldAnnotations(db,nullptr,record->getBlockTableRecordId());
   for(auto& member:paper){
    member.panel=panel;
    inventory.push_back(std::move(member));
   }
  }
 }
 for(const auto& row:inventory)if(!wholeDrawing&&row.kind=="dimension"&&row.visible&&!productPrintedDimensionPanel(job,row))
  outside.insert(row.handle);
 for(const auto& row:inventory){
  const auto panel=wholeDrawing&&row.visible?std::max(std::size_t(1),productPrintedDimensionPanel(job,row)):productPrintedDimensionPanel(job,row);
  if(!panel||(row.kind!="dimension"&&row.kind!="unreadable-dimension")||!visited.insert(row.handle).second)continue;
  ProductDimensionReplacement item;
  item.source=row.handle;
  item.panel=panel;
  const bool replace=replaceAll||inputHandles.count(row.handle);
  const auto id=find(db,row.handle);
  auto entity=open<AcDbEntity>(id);
  const auto owner=entity->ownerId();
  const auto layer=open<AcDbLayerTableRecord>(entity->layerId());
  const bool editable=!row.external&&!layer->isLocked()&&(row.path==row.handle||!outside.count(row.handle));
  if(!editable){
   item.issue=u8"旧尺寸位于外部参照、锁定层或图窗内外共用块，无法隔离该实例";
   run.objects.push_back(std::move(item));
   continue;
  }
  const bool native=row.type==L"TDbDimension2";
  item.geometry=productReadSourceDimension(db,row.handle,.01);
  AcDbObjectId finalId=id;
  std::vector<ProductDimensionText> parts;
  try{
   if(native)parts=productDimensionTexts(*entity);
   require(item.geometry.readable&&item.geometry.automaticText,
           "source dimension lacks a verifiable native chain or contains a numerical override");
   require(run.fontReady,"approved dimension text font is unavailable");
   const auto usableStyle=[&](const ProductDimensionText& piece){
    return !piece.style.isNull()&&productDimensionFamily(symbolName(piece.style),policy.name)&&
           productDimensionRatioMatches(symbolName(piece.style),policy.name,job.unifiedScale)&&
           piece.scale>0&&productDimensionEqual(piece.measurementFactor,job.unifiedUnitMM);
   };
   const auto count=item.geometry.values.size();
   if(native&&parts.size()==count&&!std::all_of(parts.begin(),parts.end(),usableStyle)&&owner==modelId(db)){
    // Native chains can retain a private style name despite resource mapping.
    // Recreate from a genuine approved chain with the same segment count using
    // public grips, and verify every original station and measured value.
    AcDbObjectId donor;
    for(const auto& [h,type]:modelTypes(db))if(type==L"TDbDimension2"){
     const auto probe=find(db,ascii(h));if(probe==id)continue;
     auto candidate=open<AcDbEntity>(probe);if(!productPlanDimensionVisible(*candidate))continue;
     const auto forms=productDimensionTexts(*candidate);
     if(forms.size()==count&&std::all_of(forms.begin(),forms.end(),usableStyle)){donor=probe;break;}
    }
    require(!donor.isNull(),"no verified native prototype with matching segment count");
    const auto& points=item.geometry.points;require(points.size()==count+1&&count>0,"complete source stations");
    const auto direction=(points.back()-points.front()).normal();
    for(std::size_t k=0;k<count;++k)require((points[k+1]-points[k]).crossProduct(direction).length()<.01&&
     std::abs(points[k+1].distanceTo(points[k])-item.geometry.values[k])<.01,"non-collinear source measurements");
    const auto originalGrips=chainGrips(id);const AcGeVector3d normal(-direction.y,direction.x,0);
    const auto offset=(item.geometry.baseline-points.front()).dotProduct(normal);entity.reset();
    const auto before=wholeModelIds(db);
    try{finalId=productDetailChain(db,donor,points,offset);}
    catch(...){for(const auto& added:wholeModelIds(db))if(!before.count(added))editObject<AcDbEntity>(added)->erase();throw;}
    item.rebuiltFromStations=true;
    if(originalGrips.size()==2*count+6)for(std::size_t k=0;k<count;++k)
     moveDimensionGrip(finalId,static_cast<int>(count+3+k),originalGrips[count+3+k]);
    parts=productDimensionTexts(*open<AcDbEntity>(finalId));entity=open<AcDbEntity>(id);
   }
   if((!native||parts.size()!=count||
       !std::all_of(parts.begin(),parts.end(),usableStyle))&&owner==modelId(db)&&
       (count==1||(curDoc()&&curDoc()->database()==&db))){
    const auto& points=item.geometry.points;
    require(count&&points.size()==count+1&&points.front().distanceTo(points.back())>.01&&
            !productSourceNativeDimensionProtected(*entity),
            "source measurement has a live association or no complete set of distinct stations");
    const auto direction=(points.back()-points.front()).normal();
    require(std::abs(direction.z)<1e-9,"source dimension does not have a horizontal measurement direction");
    for(std::size_t index=0;index<count;++index){
     const auto step=points[index+1]-points[index];
     require(step.dotProduct(direction)>.01&&step.crossProduct(direction).length()<=.01&&
             std::abs(step.length()-item.geometry.values[index])<=.01,
             "source measurement needs a non-collinear projection or a different measurement factor");
    }
    const AcGeVector3d normal(-direction.y,direction.x,0.);
    const auto offset=(item.geometry.baseline-points[0]).dotProduct(normal);
    if(prototype.isNull()){
     prototype=cloneOne(reference,find(reference,"19A0"),db);
     status(editObject<AcDbEntity>(prototype)->setVisibility(AcDb::kInvisible),"hide approved construction prototype");
    }
    entity.reset();
    if(count==1)finalId=productDetailChain(db,prototype,points,offset);
    else{
     const auto before=wholeModelIds(db);
     try{finalId=productGenericChain(job,db,prototype,points,offset);}
     catch(...){
      for(const auto& added:wholeModelIds(db))if(!before.count(added))
       status(editObject<AcDbEntity>(added)->erase(),"remove incomplete replacement chain");
      require(wholeModelIds(db)==before,"failed replacement changed an existing dimension");
      throw;
     }
    }
    item.rebuiltFromStations=true;
    status(editObject<AcDbEntity>(finalId)->setVisibility(AcDb::kVisible),"display new measurement chain");
    parts=productDimensionTexts(*open<AcDbEntity>(finalId));
    require(parts.size()==count,"new native dimension must reproduce all source segments");
   }
   require(parts.size()==item.geometry.values.size(),"all source dimension segments must be readable");
   for(const auto& part:parts)
    require(usableStyle(part),
            "native dimension style family, scale or measurement factor cannot be converted equivalently");
   entity.reset();
   if(replace&&finalId==id)finalId=productCloneDimensionToOwner(db,id,owner);
   for(const auto& part:parts){
    auto style=editObject<AcDbDimStyleTableRecord>(part.style);
    status(style->setDimtxsty(font),"bind exact _TCH_DIM to native dimension segments");
    status(style->setDimtxt(policy.nominalPaperHeightMM*job.unifiedScale/job.unifiedUnitMM/part.scale),
           "set native dimension text paper height");
    status(style->setDimtmove(2),"maintain independent native dimension lettering");
   }
   item.target=axisHandle(finalId);
   const auto finalParts=productDimensionTexts(*open<AcDbEntity>(finalId));
   require(productDimensionCloneIsEqual(db,item)&&finalParts.size()==parts.size()&&
      std::all_of(finalParts.begin(),finalParts.end(),[&](const auto& part){return productRequiredDimensionTypography(part,job,run.font);}),
      "new native dimension did not reproduce measurements and exact text style");
   job.planDimensionMeasurements[item.target]=item.geometry.values;
   if(finalId!=id){
    status(editObject<AcDbEntity>(id)->setVisibility(AcDb::kInvisible),"remove the original dimension from its printed sheet");
    job.planDimensionExclusions.insert(item.source);
    job.planRequiredDimensionReplacements[item.source]=item.target;
   }
  }catch(const std::exception& error){
   entity.reset();
   if(finalId!=id)status(editObject<AcDbEntity>(finalId)->erase(),"discard an unverified dimension replacement");
   item.target.clear();
   item.issue=error.what();
   // An unsuccessful replacement must not silently remove the measured span.
   // Preserve its source expression and list it explicitly for this DWG.
   job.planRequiredDimensionIssues[item.source]=item.issue;
  }
  run.objects.push_back(std::move(item));
 }
 if(!prototype.isNull())status(editObject<AcDbEntity>(prototype)->erase(),"remove approved construction prototype");
 if(run.fontReady)productSynchronizePlanDimensionTable(job,db,reference,font);
 return run;
}

void productVerifyRequiredPlanDimensions(ProductJob& job,AcDbDatabase& saved,const ProductPlanDimensionRun& run,
 const fs::path& drawing){
 std::ostringstream report;
 report<<std::setprecision(17)<<"{\"savedReopened\":true,\"outputSHA256\":\""<<sha(drawing)
       <<"\",\"requiredTextStyle\":\"_TCH_DIM\",\"dimensionStyleTable\":";
 const auto styleTable=productReadPlanDimensionTable(saved,job.dimensionStylePolicy.name,run.font);
 productWritePlanDimensionTable(report,styleTable);
 for(const auto& row:styleTable)if(!row.reason.empty())
  job.drawingChecks.record(0,"dimension-style-table",ascii(row.name),{"Y10"},
   bcpproduct::PlanReleaseGate::reopened,bcpproduct::PlanCheck::failed,u8"独立重开读取尺寸样式表",row.reason);
 report<<",\"dimensions\":[";
 bool first=true;
 for(const auto& item:run.objects){
  std::vector<ProductDimensionText> parts;
  auto issue=item.issue;
  const auto original=open<AcDbEntity>(find(saved,item.source));
  const bool sourceVisible=productPlanDimensionVisible(*original)&&open<AcDbLayerTableRecord>(original->layerId())->isPlottable();
  if(issue.empty()){
   const auto fresh=open<AcDbEntity>(find(saved,item.target));
   parts=productDimensionTexts(*fresh);
   if((item.source!=item.target&&sourceVisible)||!productPlanDimensionVisible(*fresh)||
       !productDimensionCloneIsEqual(saved,item,false)||parts.size()!=item.geometry.values.size()||
       !std::all_of(parts.begin(),parts.end(),[&](const auto& part){return productRequiredDimensionTypography(part,job,run.font);}))
    issue=u8"独立重开时尺寸退旧、真实测站或分段文字样式核验失败";
  }
  const auto proof="{\"panel\":"+std::to_string(item.panel)+",\"source\":\""+item.source+
      "\",\"target\":\""+item.target+"\",\"sourceVisible\":"+(sourceVisible?"true":"false")+
      ",\"issue\":"+jsonUtf8(bcpproduct::decodeMessage(issue))+"}";
  productRecordRuleEvidence(job,"D30",proof);
  productRecordRuleEvidence(job,"Y10",proof);
  if(!issue.empty())job.drawingChecks.record(item.panel,"dimension-replacement",item.source,{"D30","Y10","Q03"},
       bcpproduct::PlanReleaseGate::reopened,bcpproduct::PlanCheck::failed,u8"独立重开逐段核查",issue);
  if(!first)report<<',';
  first=false;
  report<<proof.substr(0,proof.size()-1)<<",\"parts\":[";
  for(std::size_t i=0;i<parts.size();++i){
   if(i)report<<',';
   const auto& part=parts[i];
   const auto style=part.font.isNull()?ProductDimensionFont{}:productReadDimensionFont(part.font);
   report<<"{\"segment\":"<<i+1<<",\"value\":"
         <<(i<item.geometry.values.size()?std::to_string(item.geometry.values[i]):"null")
         <<",\"dimensionStyle\":"<<jsonUtf8(part.style.isNull()?L"":symbolName(part.style))
         <<",\"textStyle\":"<<jsonUtf8(style.name)
         <<",\"fontFile\":"<<jsonUtf8(style.primary)<<",\"bigFontFile\":"<<jsonUtf8(style.big)
         <<",\"textStyleDefinitionVerified\":"
         <<(!part.font.isNull()&&productPlanDimensionFontConforms(part.font,run.font)?"true":"false")
         <<",\"effectiveHeight\":"<<part.height*part.scale<<'}';
  }
  report<<"]}";
 }
 report<<"]}";
 writeNew(job.root/L"required-dimension-readback.json",report.str());
}
