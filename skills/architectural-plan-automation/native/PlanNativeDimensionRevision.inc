namespace {
void executePlanDimensionRevision(void* data){
 std::unique_ptr<JointPaperJob> request(static_cast<JointPaperJob*>(data));
 auto& sheets=*request;
 AcApDocument* opened{};
 try{
  AcDbDatabase db(false,true),reference(false,true);
  readDrawing(db,sheets.input);
  readDrawing(reference,sheets.report/L"dimension-reference.dwg");
  Working working(&db);
  ProductJob job;
  job.root=sheets.report;
  job.profile=sheets.report;
  job.unifiedPlan=true;
  job.architecturalPlanOnly=true;
  jointPaperSheets(job,db,sheets);
  for(std::size_t i=0;i<sheets.sheets.size();++i){
   const auto& sheet=sheets.sheets[i];
   const auto view=open<AcDbViewport>(find(db,sheet.viewport));
   const auto normal=view->viewDirection();
   require(normal.normal().dotProduct(AcGeVector3d::kZAxis)>1.-1e-9&&std::abs(view->twistAngle())<1e-9&&
     view->height()>0&&view->width()>0,"dimension revision requires an orthogonal plan viewport");
   const auto height=view->viewHeight();
   const auto width=height*view->width()/view->height();
   const auto centre=view->viewTarget()+AcGeVector3d(view->viewCenter().x,view->viewCenter().y,0.);
   job.planPrintWindows[i]={centre.x-width/2,centre.y-height/2,centre.x+width/2,centre.y+height/2};
  }
  const auto before=modelTypes(db);
  const auto changes=productEnforcePlanDimensions(job,db,reference,{},true);
  require(!changes.objects.empty(),"drawings have no dimension objects in the recorded print windows");
  require(!fs::exists(sheets.output),"dimension revision candidate path already exists");
  status(db.saveAs(sheets.output.c_str(),false,AcDb::kDHL_CURRENT),"save a new native dimension revision");
  AcDbDatabase saved(false,true);
  readDrawing(saved,sheets.output);
  {
   Working reopened(&saved);
   const auto after=modelTypes(saved);
   for(const auto& [handle,type]:before)if(type.find(L"Dimension")==std::wstring::npos)
    require(after.at(handle)==type,"non-dimension object membership changed on reopen");
   productVerifyRequiredPlanDimensions(job,saved,changes,sheets.output);
  }
  opened=productOpenPrintedInkCandidate(sheets.output);
  productPrintPlanLayouts(job,*opened,sheets.output,sheets.report);
  productStatus(acDocManager->appContextCloseDocument(opened),"close new dimension verification drawing");
  opened=nullptr;
  require(sha(sheets.input)==sheets.hash,"dimension revision source changed");
  writeNew(sheets.report/L"product-result.json","{\"completed\":true,\"dwgReopened\":true,\"sourceSHA256\":\""+
    sheets.hash+"\",\"outputSHA256\":\""+sha(sheets.output)+"\"}");
 }catch(const std::exception& error){
  if(opened)acDocManager->appContextCloseDocument(opened);
  writeNew(sheets.report/L"product-result.json","{\"completed\":false,\"error\":"+
   jsonUtf8(bcpproduct::decodeMessage(error.what()))+"}");
 }
}
}
