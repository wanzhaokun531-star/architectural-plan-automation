struct PlanTypographyRequest {fs::path source,output,report,resources,sheets;bool inspect{};};

std::string planTypographyProtected(AcDbDatabase& db,const std::set<std::string>& scope){
 std::map<std::string,std::string> nativeLabels;
 for(const auto& [h,type]:modelTypes(db))if(type==L"TDbSymbElevation"||type==L"TDbSymbArrow"){
  const auto key=ascii(h);if(scope.count(key))continue;
  const auto label=open<AcDbEntity>(find(db,key));
  nativeLabels[key]=jsonUtf8(productPlanDetailContents(*label));
 }
 auto value=wholeProtected(db,scope,true,&nativeLabels);
 for(const auto& [handle,type]:modelTypes(db))if(type==L"AcDbBlockReference"&&!scope.count(ascii(handle))){
  const auto ref=open<AcDbBlockReference>(find(db,ascii(handle)));const auto definition=ref->blockTableRecord();
  const auto name=symbolName(definition);if(name.rfind(L"*U",0)!=0)continue;
  const auto from="\"blockDefinition\":"+jsonUtf8(name),to="\"blockDefinition\":\"handle:"+axisHandle(definition)+"\"";
  for(std::size_t at=0;(at=value.find(from,at))!=std::string::npos;at+=to.size())value.replace(at,from.size(),to);
 }
 return value;
}

std::string planTypographyStyleTable(AcDbDatabase& db){
 std::ostringstream out;out<<'[';bool first=true;
 const auto table=open<AcDbTextStyleTable>(db.textStyleTableId());
 AcDbTextStyleTableIterator* raw{};status(table->newIterator(raw),"read typography styles");
 std::unique_ptr<AcDbTextStyleTableIterator> it(raw);
 for(;!it->done();it->step()){
  AcDbObjectId id;status(it->getRecordId(id),"read typography style id");
  const auto font=productReadDimensionFont(id);const auto record=open<AcDbTextStyleTableRecord>(id);
  if(!first)out<<',';first=false;
  out<<"{\"name\":"<<jsonUtf8(font.name)<<",\"primary\":"<<jsonUtf8(font.primary)<<",\"big\":"<<jsonUtf8(font.big)
   <<",\"width\":"<<font.width<<",\"height\":"<<record->textSize()<<",\"oblique\":"<<font.oblique<<",\"flags\":"<<font.flags<<'}';
 }
 out<<']';return out.str();
}

void executePlanTypography(void* raw){
 std::unique_ptr<PlanTypographyRequest> task(static_cast<PlanTypographyRequest*>(raw));const auto& p=*task;
 AcApDocument* opened{};
 try{
  AcDbDatabase db(false,true),reference(false,true);readDrawing(db,p.source);readDrawing(reference,p.resources/L"reference.dwg");
  Working working(&db);
  writeNew(p.report/L"typography-styles-before.json","{\"source\":"+planTypographyStyleTable(db)+",\"reference\":"+planTypographyStyleTable(reference)+"}");
  const auto inventory=productTypographyInventory(db);std::ostringstream objects;objects<<"{\"objects\":[";bool first=true;
  for(const auto& row:inventory){if(!first)objects<<',';first=false;productWriteOldAnnotation(objects,row);}
  objects<<"]}";writeNew(p.report/L"typography-objects-before.json",objects.str());
  if(p.inspect){writeNew(p.report/L"product-result.json","{\"completed\":true,\"readOnly\":true,\"sourceSHA256\":\""+sha(p.source)+"\"}");return;}
  auto sheets=readJointPaperJob(p.source,p.sheets,p.output,p.report);
  ProductJob job;job.root=p.report;job.profile=p.resources;job.sourceHash=sha(p.source);job.architecturalPlanOnly=true;job.unifiedPlan=true;
  job.planMode="typography-finalize";
  jointPaperSheets(job,db,sheets);
  std::set<std::string> beforeScope;std::map<std::string,std::vector<AcGePoint3d>> openingGeometry;
  for(const auto& row:productTypographyInventory(db))if(row.visible){const auto role=productTypographyRole(db,row);
   if(role=="general"||role=="opening-label"||role=="dimension")beforeScope.insert(row.root);}
  for(const auto& [h,type]:modelTypes(db))if(type==L"TDbOpening"||type==L"TDbGroupOpening"){
   const auto key=ascii(h);beforeScope.insert(key);openingGeometry[key]=chainGrips(find(db,key));}
  const auto protectedBefore=planTypographyProtected(db,beforeScope);
  const auto changes=productNormalizeWholePlanTypography(job,db,reference);
  for(const auto& issue:changes)job.detailPending.push_back({u8"字体修正",issue.object,issue.reason});
  auto dimensions=productEnforcePlanDimensions(job,db,reference,{},true,true);
  status(db.saveAs(p.output.c_str(),false,AcDb::kDHL_CURRENT),"save typography-corrected drawing");
  AcDbDatabase saved(false,true);readDrawing(saved,p.output);
  std::vector<ProductTypographyIssue> issues;
  {Working reopened(&saved);
   std::set<std::string> afterScope=beforeScope;for(const auto& d:dimensions.objects)if(!d.target.empty())afterScope.insert(d.target);
   const auto protectedAfter=planTypographyProtected(saved,afterScope);
   writeNew(p.report/L"protected-before.json",protectedBefore);writeNew(p.report/L"protected-after.json",protectedAfter);
   require(bcpproduct::samePublicGeometry(protectedBefore,protectedAfter),"typography revision changed unrelated geometry");
   for(const auto& [handle,prior]:openingGeometry){const auto after=chainGrips(find(saved,handle));
    require(prior.size()==after.size()&&prior.size()>1&&std::equal(prior.begin(),prior.end()-1,after.begin(),[](const auto& a,const auto& b){return a.distanceTo(b)<.01;}),"opening geometry changed during caption typography correction");}
   productVerifyRequiredPlanDimensions(job,saved,dimensions,p.output);
   issues=productAuditWholePlanTypography(job,saved,reference,p.output,p.report/L"final-typography-readback.json");
   for(const auto& issue:changes)if(issue.role=="note-clearance")issues.push_back(issue);
   for(const auto& item:dimensions.objects)if(!item.issue.empty())issues.push_back({item.source,"dimension-replacement",item.issue});
   writeNew(p.report/L"typography-styles-after.json",planTypographyStyleTable(saved));
  }
  opened=productOpenPrintedInkCandidate(p.output);productPrintPlanLayouts(job,*opened,p.output,p.report);
  {Working plotted(opened->database());ProductPrintedInkProof proof;proof.directory=p.report;proof.digest=sha(p.output);
   productWritePrintedInkRequest(job,*opened->database(),proof,p.report/L"typography-paper-input.json",false,nullptr,false);}
  productStatus(acDocManager->appContextCloseDocument(opened),"close final typography printing");opened=nullptr;
  std::ostringstream pending;pending<<'[';for(std::size_t i=0;i<issues.size();++i){if(i)pending<<',';
   pending<<"{\"object\":\""<<issues[i].object<<"\",\"role\":\""<<issues[i].role<<"\",\"reason\":"<<jsonUtf8(bcpproduct::decodeMessage(issues[i].reason))<<'}';}pending<<']';
  writeNew(p.report/L"product-result.json","{\"completed\":true,\"dwgReopened\":true,\"nonAnnotationGeometryPreserved\":true,\"sourceSHA256\":\""+sha(p.source)+
   "\",\"outputSHA256\":\""+sha(p.output)+"\",\"unresolved\":"+pending.str()+"}");
 }catch(const std::exception& error){if(opened)acDocManager->appContextCloseDocument(opened);
  writeNew(p.report/L"product-result.json","{\"completed\":false,\"error\":"+jsonUtf8(bcpproduct::decodeMessage(error.what()))+"}");}
}
