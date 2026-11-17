struct PlanFurnitureJob {fs::path source,output,report,sheets;bool fixture{};};

AcDbObjectId furnitureFixtureEntity(AcDbDatabase& db,AcDbObjectId owner,std::unique_ptr<AcDbEntity> entity){
 entity->setDatabaseDefaults(&db);
 AcDbObjectId id;
 auto record=editObject<AcDbBlockTableRecord>(owner);
 status(record->appendAcDbEntity(id,entity.get()),"append furniture regression entity");
 entity.release()->close();return id;
}

std::vector<std::string> makeFurnitureFixture(AcDbDatabase& db){
 const auto leaf=productCreateFurnitureDefinition(db,AcGePoint3d::kOrigin);
 {auto record=editObject<AcDbBlockTableRecord>(leaf);status(record->setName(L"家具_沙发_回归"),"fixture semantic definition");}
 furnitureFixtureEntity(db,leaf,std::make_unique<AcDbLine>(AcGePoint3d(0,0,0),AcGePoint3d(1200,0,0)));
 furnitureFixtureEntity(db,leaf,std::make_unique<AcDbArc>(AcGePoint3d(600,300,0),200.,0.,3.141592653589793));
 auto poly=std::make_unique<AcDbPolyline>();
 poly->addVertexAt(0,AcGePoint2d(0,0));poly->addVertexAt(1,AcGePoint2d(0,600));poly->addVertexAt(2,AcGePoint2d(1200,600));
 furnitureFixtureEntity(db,leaf,std::move(poly));
 const auto middle=productCreateFurnitureDefinition(db,AcGePoint3d::kOrigin);
 furnitureFixtureEntity(db,middle,std::make_unique<AcDbBlockReference>(AcGePoint3d(150,70,0),leaf));
 const auto outer=productCreateFurnitureDefinition(db,AcGePoint3d::kOrigin);
 furnitureFixtureEntity(db,outer,std::make_unique<AcDbBlockReference>(AcGePoint3d(-80,200,0),middle));
 std::vector<std::string> selected;
 for(int i=0;i<3;++i){
  auto ref=std::make_unique<AcDbBlockReference>(AcGePoint3d(i*3000.,i*1700.,0),outer);
  status(ref->setRotation(i*.4),"fixture rotation");
  status(ref->setScaleFactors(i==2?AcGeScale3d(-1.5,1.5,1.5):AcGeScale3d(1.+i*.5)),"fixture scale and mirror");
  const auto id=furnitureFixtureEntity(db,modelId(db),std::move(ref));
  selected.push_back(axisHandle(id));
 }
 const auto shared=furnitureFixtureEntity(db,modelId(db),std::make_unique<AcDbBlockReference>(AcGePoint3d(10000,10000,0),outer));
 // A hidden sibling still shares the original definitions and must stay intact.
 status(editObject<AcDbEntity>(shared)->setVisibility(AcDb::kInvisible),"fixture protected shared instance");
 furnitureFixtureEntity(db,modelId(db),std::make_unique<AcDbLine>(AcGePoint3d(5000,0,0),AcGePoint3d(5000,9000,0)));
 return selected;
}

void executePlanFurnitureJob(void* raw){
 std::unique_ptr<PlanFurnitureJob> task(static_cast<PlanFurnitureJob*>(raw));
 const auto& p=*task;AcApDocument* opened{};
 try{
  AcDbDatabase db(p.fixture,true);
  if(!p.fixture)readDrawing(db,p.source);
  Working working(&db);
  std::vector<std::string> fixtureExpected;
  if(p.fixture)fixtureExpected=makeFurnitureFixture(db);
  const auto control=p.report/L"furniture-before.dwg";
  status(db.saveAs(control.c_str(),false,AcDb::kDHL_CURRENT),"save furniture input control");
  ProductJob job;job.unifiedPlan=true;job.root=p.report;job.sourceHash=sha(control);
  const auto items=productApplyPlanFurniture(job,db,{});
  std::set<std::string> beforeScope,afterScope;
  for(const auto& item:items){beforeScope.insert(item.sources.begin(),item.sources.end());afterScope.insert(item.output);}
  status(db.saveAs(p.output.c_str(),false,AcDb::kDHL_CURRENT),"save furniture candidate");
  AcDbDatabase saved(false,true),before(false,true);
  readDrawing(saved,p.output);readDrawing(before,control);
  std::string protectedBefore;
  {Working context(&before);protectedBefore=wholeProtected(before,beforeScope,true);}
  bool flat=true,zero=true,matched=true;
  {Working context(&saved);
   require(bcpproduct::samePublicGeometry(protectedBefore,wholeProtected(saved,afterScope,true)),"non-furniture or shared instance changed");
   productVerifyPlanFurniture(job,saved,items,{});
   for(const auto& item:items){
    const auto ref=open<AcDbBlockReference>(find(saved,item.output));
    for(auto member:productFurnitureMembers(ref->blockTableRecord())){
     const auto entity=open<AcDbEntity>(member);
     flat=flat&&!entity->isKindOf(AcDbBlockReference::desc());
     zero=zero&&symbolName(entity->layerId())==L"0";
    }
   }
   if(p.fixture)for(const auto& h:fixtureExpected)matched=matched&&beforeScope.count(h)!=0;
  }
  if(!p.sheets.empty()){
   auto sheetJob=readJointPaperJob(p.source,p.sheets,p.output,p.report);
   opened=productOpenPrintedInkCandidate(p.output);
   ProductJob print;
   {Working plotted(opened->database());jointPaperSheets(print,*opened->database(),sheetJob);}
   productPrintPlanLayouts(print,*opened,p.output,p.report);
   productStatus(acDocManager->appContextCloseDocument(opened),"close furniture plot");opened=nullptr;
  }
  const auto passed=flat&&zero&&matched&&job.detailPending.empty();
  std::ostringstream pending;pending<<'[';
  for(std::size_t i=0;i<job.detailPending.size();++i){if(i)pending<<',';
   const auto& item=job.detailPending[i];pending<<"{\"category\":"<<jsonUtf8(bcpproduct::decodeMessage(item[0]))
    <<",\"object\":"<<jsonUtf8(bcpproduct::decodeMessage(item[1]))<<",\"reason\":"<<jsonUtf8(bcpproduct::decodeMessage(item[2]))<<'}';}
  pending<<']';
  writeNew(p.report/L"product-result.json","{\"completed\":true,\"sourceSHA256\":\""+sha(p.source)+
   "\",\"outputSHA256\":\""+sha(p.output)+"\",\"dwgReopened\":true,\"furnitureCount\":"+std::to_string(items.size())+
   ",\"noNestedBlocks\":"+(flat?"true":"false")+",\"membersOnZero\":"+(zero?"true":"false")+
   ",\"expectedFurnitureCovered\":"+(matched?"true":"false")+",\"nonFurniturePreserved\":true,\"unresolved\":"+pending.str()+
   ",\"testPassed\":"+(passed?"true":"false")+"}");
 }catch(const std::exception& error){
  if(opened)acDocManager->appContextCloseDocument(opened);
  writeNew(p.report/L"product-result.json","{\"completed\":false,\"error\":"+jsonUtf8(bcpproduct::decodeMessage(error.what()))+"}");
 }
}
