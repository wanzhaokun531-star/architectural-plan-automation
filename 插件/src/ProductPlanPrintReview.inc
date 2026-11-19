class ProductPlanPdf {
public:
 ~ProductPlanPdf(){close();}
 void write(AcPlPlotInfo& info,const fs::path& dwg,const fs::path& pdf){
  status(AcPlPlotFactory::createPublishEngine(engine_),"create PDF engine");
  require(engine_,"PDF engine unavailable");
  status(engine_->beginPlot(nullptr),"begin PDF plot");
  plot_=true;
  status(engine_->beginDocument(info,dwg.c_str(),nullptr,1,true,pdf.c_str()),"begin PDF document");
  document_=true;
  status(engine_->beginPage(pageInfo_,info,true),"begin PDF page");
  page_=true;
  status(engine_->beginGenerateGraphics(),"generate PDF graphics");
  status(engine_->endGenerateGraphics(),"end PDF graphics");
  require(close(),"finish PDF page and document");
 }
private:
 bool close(){
  if(!engine_)return true;
  bool ok=true;
  if(page_)ok&=engine_->endPage()==Acad::eOk;
  if(document_)ok&=engine_->endDocument()==Acad::eOk;
  if(plot_)ok&=engine_->endPlot()==Acad::eOk;
  engine_->destroy();
  engine_=nullptr;
  return ok;
 }
 AcPlPlotEngine* engine_{};
 AcPlPlotPageInfo pageInfo_;
 bool plot_{},document_{},page_{};
};

std::wstring productSelectPlanPaper(AcDbPlotSettings& settings,AcDbPlotSettingsValidator& validator,
 double modelWidth,double modelHeight,double scaleFactor){
 AcArray<const ACHAR*> media;
 status(validator.canonicalMediaNameList(&settings,media),"PDF paper list");
 std::vector<std::wstring> names;
 for(int i=0;i<media.length();++i)if(media[i])names.emplace_back(media[i]);
 for(const auto& paper:std::array<std::array<double,2>,3>{{{420,297},{594,420},{841,594}}}){
  for(const auto& name:names){
   if(name.find(L"ISO")==std::wstring::npos)continue;
   if(validator.setCanonicalMediaName(&settings,name.c_str())!=Acad::eOk)continue;
   double width{},height{},left{},bottom{},right{},top{};
   if(settings.getPlotPaperSize(width,height)!=Acad::eOk||
      settings.getPlotPaperMargins(left,bottom,right,top)!=Acad::eOk)continue;
   const bool landscape=width>=height;
   if(std::abs(std::max(width,height)-paper[0])>.5||std::abs(std::min(width,height)-paper[1])>.5)continue;
   const double availableX=width-left-right,availableY=height-bottom-top;
   if(modelWidth*scaleFactor>(landscape?availableX:availableY)||
      modelHeight*scaleFactor>(landscape?availableY:availableX))continue;
   status(validator.setPlotRotation(&settings,landscape?AcDbPlotSettings::k0degrees:AcDbPlotSettings::k90degrees),
     "plan paper landscape");
   return name;
  }
 }
 throw std::runtime_error("plot window exceeds A1 at the drawing scale");
}

// Saved layouts and the actual PDF job use the same paper-space contract.
std::wstring productApplyPlanLayoutPaper(AcDbPlotSettings& settings,AcDbPlotSettingsValidator& validator,
 double width,double height){
 const auto paper=productSelectPlanPaper(settings,validator,width,height,1.);
 status(validator.setPlotPaperUnits(&settings,AcDbPlotSettings::kMillimeters),"formal paper millimetres");
 status(validator.setPlotWindowArea(&settings,0,0,width,height),"formal paper frame window");
 status(validator.setPlotType(&settings,AcDbPlotSettings::kWindow),"formal paper window");
 status(validator.setUseStandardScale(&settings,false),"formal explicit paper scale");
 status(validator.setCustomPrintScale(&settings,1,1),"formal one-to-one paper scale");
 status(validator.setPlotCentered(&settings,true),"center complete formal frame");
 return paper;
}

std::string productPlanPaperSettingsJson(const AcDbPlotSettings& settings){
 double width{},height{},n{},d{},standard{},x{},y{},left{},bottom{},right{},top{},wx{},wy{},wr{},wt{};
 status(settings.getPlotPaperSize(width,height),"paper size evidence");
 status(settings.getCustomPrintScale(n,d),"custom paper scale evidence");
 status(settings.getStdScale(standard),"standard paper scale evidence");
 status(settings.getPlotOrigin(x,y),"paper origin evidence");
 status(settings.getPlotPaperMargins(left,bottom,right,top),"paper margins evidence");
 status(settings.getPlotWindowArea(wx,wy,wr,wt),"paper window evidence");
 std::ostringstream out;out<<std::setprecision(12)<<"{\"units\":"<<static_cast<int>(settings.plotPaperUnits())
  <<",\"useStandardScale\":"<<(settings.useStandardScale()?"true":"false")<<",\"standardScaleType\":"<<static_cast<int>(settings.stdScaleType())
  <<",\"standardScale\":"<<standard<<",\"customScale\":["<<n<<','<<d<<"],\"rotation\":"<<static_cast<int>(settings.plotRotation())
  <<",\"paperSize\":["<<width<<','<<height<<"],\"origin\":["<<x<<','<<y<<"],\"margins\":["<<left<<','<<bottom<<','<<right<<','<<top
  <<"],\"window\":["<<wx<<','<<wy<<','<<wr<<','<<wt<<"],\"plotType\":"<<static_cast<int>(settings.plotType())
  <<",\"centered\":"<<(settings.plotCentered()?"true":"false")<<'}';return out.str();
}

void productPrintPlanReview(ProductJob& job,AcApDocument& doc,const fs::path& candidate,
                            const fs::path& reportDirectory={}){
 if(!job.planPrintReview)return;
 const auto& directory=reportDirectory.empty()?job.root:reportDirectory;
 auto& db=*doc.database();
 Working work(&db);
 resbuf original{};
 const bool restore=acedGetVar(L"BACKGROUNDPLOT",&original)==RTNORM&&original.restype==RTSHORT;
 std::ostringstream report;
 report<<"{\"outputSHA256\":\""<<sha(candidate)<<"\",\"scale\":"<<job.unifiedScale<<",\"pages\":[";
 const auto issue=[&](std::size_t panel,const char* reason){
  job.detailPending.push_back({u8"打印校核",std::to_string(panel),reason});
 };
 for(std::size_t i=0;i<job.planPrintWindows.size();++i){
  if(i)report<<',';
  report<<"{\"panel\":"<<i+1;
  const auto& window=job.planPrintWindows[i];
  const auto pdf=directory/(L"打印校核_图幅"+std::to_wstring(i+1)+L".pdf");
  try{
   require(restore&&acplProcessPlotState()==kNotPlotting,"native PDF plotting unavailable");
   resbuf foreground{};
   foreground.restype=RTSHORT;
   require(acedSetVar(L"BACKGROUNDPLOT",&foreground)==RTNORM,"set foreground PDF plotting");
   AcDbPlotSettings settings(true);
   const auto model=open<AcDbBlockTableRecord>(modelId(db));
   auto layoutId=model->getLayoutId();
   const auto layout=open<AcDbLayout>(layoutId);
   status(settings.copyFrom(layout.get()),"copy model plot settings");
   auto* validator=acdbHostApplicationServices()->plotSettingsValidator();
   require(validator,"native plot settings validator");
   validator->refreshLists(&settings);
   status(validator->setPlotCfgName(&settings,L"DWG To PDF.pc3"),"select DWG To PDF");
   validator->refreshLists(&settings);
   const double ratio=job.unifiedUnitMM/job.unifiedScale;
   const auto paper=productSelectPlanPaper(settings,*validator,window[2]-window[0],window[3]-window[1],ratio);
   status(validator->setPlotPaperUnits(&settings,AcDbPlotSettings::kMillimeters),"PDF millimetres");
   if(!job.planPlotStyle.empty()){
    AcArray<const ACHAR*> styles;
    status(validator->plotStyleSheetList(styles),"list installed plot styles");
    bool installed=false;
    for(int k=0;k<styles.length();++k)if(styles[k]&&_wcsicmp(styles[k],job.planPlotStyle.c_str())==0)installed=true;
    require(installed,"requested CTB/STB is not installed");
    status(validator->setCurrentStyleSheet(&settings,job.planPlotStyle.c_str()),"apply project plot style");
    settings.setPlotPlotStyles(true);
   }
   status(validator->setPlotWindowArea(&settings,window[0],window[1],window[2],window[3]),"plan PDF window");
   status(validator->setPlotType(&settings,AcDbPlotSettings::kWindow),"plan window plot");
   status(validator->setUseStandardScale(&settings,false),"explicit plan scale");
   status(validator->setCustomPrintScale(&settings,job.unifiedUnitMM,job.unifiedScale),"set plan plot scale");
   status(validator->setPlotCentered(&settings,true),"center plan plot");
   AcPlPlotInfo info;
   info.setLayout(layoutId);
   info.setOverrideSettings(&settings);
   AcPlPlotInfoValidator validation;
   validation.setMediaMatchingPolicy(AcPlPlotInfoValidator::kMatchEnabled);
   status(validation.validate(info),"validate plan PDF settings");
   double numerator{},denominator{};
   require(info.validatedSettings(),"validated PDF settings unavailable");
   status(info.validatedSettings()->getCustomPrintScale(numerator,denominator),"read validated PDF scale");
   if(denominator<=0||std::abs(numerator/denominator-ratio)>=1e-9||
      info.validatedSettings()->plotPaperUnits()!=AcDbPlotSettings::kMillimeters)
    throw std::runtime_error("validated PDF scale differs: "+std::to_string(numerator)+"/"+std::to_string(denominator)+
     " paper unit "+std::to_string(info.validatedSettings()->plotPaperUnits()));
   ProductPlanPdf engine;
   engine.write(info,candidate,pdf);
   require(fs::is_regular_file(pdf)&&fs::file_size(pdf)>0,"plan PDF missing");
   ++job.planPrintedPages;
   report<<",\"status\":\"plotted\",\"paper\":"<<jsonUtf8(paper)<<",\"file\":"<<jsonUtf8(pdf.filename().wstring())
         <<",\"sha256\":\""<<sha(pdf)<<"\"";
  }catch(const std::exception& error){
   issue(i+1,error.what());
   report<<",\"status\":\"failed\",\"reason\":"<<jsonUtf8(bcpproduct::decodeMessage(error.what()));
  }
  report<<'}';
 }
 if(restore&&acedSetVar(L"BACKGROUNDPLOT",&original)!=RTNORM)issue(0,"restore background plot setting");
 report<<"],\"visualReview\":\"pending\",\"plotStyle\":"<<jsonUtf8(job.planPlotStyle)<<'}';
 writeNew(directory/L"plan-print-review.json",report.str());
}

// Plot the actual paper-space layout. Model-window previews cannot prove that
// the saved title block, locked viewport and paper clipping are printable.
void productPrintPlanLayouts(ProductJob& job,AcApDocument& doc,const fs::path& candidate,
                             const fs::path& reportDirectory={},const std::set<std::size_t>* panels=nullptr){
 if(!job.planPrintReview)return;
 const auto& directory=reportDirectory.empty()?job.root:reportDirectory;
 auto& db=*doc.database();Working work(&db);
 struct LayoutDocumentLock {
  AcApDocument* doc;
  ~LayoutDocumentLock(){if(doc)acDocManager->unlockDocument(doc);}
 } lock{nullptr};
 const auto lockResult=acDocManager->lockDocument(&doc,AcAp::kWrite,nullptr,nullptr,false);
 if(lockResult!=Acad::eOk)throw std::runtime_error("cannot lock owned formal plot document: "+std::to_string(lockResult));
 lock.doc=&doc;
 auto* manager=acdbHostApplicationServices()->layoutManager();
 require(manager,"formal layout manager unavailable");
 AcString previous;status(manager->getActiveLayoutName(previous,true,&db),"record active layout for formal plot");
 resbuf original{};
 require(acedGetVar(L"BACKGROUNDPLOT",&original)==RTNORM&&original.restype==RTSHORT,"read plot execution mode");
 std::ostringstream report;report<<"{\"outputSHA256\":\""<<sha(candidate)<<"\",\"pages\":[";bool first=true;
 for(const auto& [panel,sheet]:job.planSheetLayouts){
  if(panels&&!panels->count(panel))continue;
  if(!first)report<<',';first=false;
  report<<"{\"panel\":"<<panel<<",\"layout\":"<<jsonUtf8(sheet.name);
  bool passed=false;std::string reason;
  try{
   require(acplProcessPlotState()==kNotPlotting,"formal PDF plot engine busy");
   resbuf foreground{};foreground.restype=RTSHORT;
   require(acedSetVar(L"BACKGROUNDPLOT",&foreground)==RTNORM,"set foreground formal plot");
   const auto activate=manager->setCurrentLayout(sheet.name.c_str(),&db);
   if(activate!=Acad::eOk)throw std::runtime_error("activate saved formal layout: "+std::to_string(activate));
   auto layoutId=manager->findLayoutNamed(sheet.name.c_str(),&db);
   require(!layoutId.isNull(),"formal layout missing");
   AcDbPlotSettings settings(false);
   {const auto layout=open<AcDbLayout>(layoutId);status(settings.copyFrom(layout.get()),"copy saved formal plot settings");}
   auto* validator=acdbHostApplicationServices()->plotSettingsValidator();require(validator,"formal plot validator");
   validator->refreshLists(&settings);
   if(job.planMode!="template-finalize")status(validator->setPlotCfgName(&settings,L"DWG To PDF.pc3"),"formal PDF device");
   validator->refreshLists(&settings);
   const auto paper=productApplyPlanLayoutPaper(settings,*validator,sheet.paperWidth,sheet.paperHeight);
   AcPlPlotInfo info;info.setLayout(layoutId);info.setOverrideSettings(&settings);
   AcPlPlotInfoValidator validation;validation.setMediaMatchingPolicy(AcPlPlotInfoValidator::kMatchEnabled);
   status(validation.validate(info),"validate actual formal PDF layout");
   double numerator{},denominator{};require(info.validatedSettings(),"formal validated settings missing");
   status(info.validatedSettings()->getCustomPrintScale(numerator,denominator),"read formal PDF scale");
   require(denominator>0&&std::abs(numerator/denominator-1.)<1e-9,"formal paper scale changed");
   const auto viewport=open<AcDbViewport>(find(db,sheet.viewport));
   require(viewport->isOn()&&viewport->isLocked()&&std::abs(viewport->customScale()-sheet.scale)<1e-9,"formal model viewport scale or visibility changed");
   const ACHAR* effectiveDevice{};const ACHAR* effectiveStyle{};
   status(info.validatedSettings()->getPlotCfgName(effectiveDevice),"read effective PDF device");
   status(info.validatedSettings()->getCurrentStyleSheet(effectiveStyle),"read effective plot style");
   if(job.planMode=="template-finalize"){
    require(effectiveDevice&&fs::path(effectiveDevice).filename()==L"FY-XS-2026.9-PDF.pc3","template PDF device was overridden");
    require(effectiveStyle&&fs::path(effectiveStyle).filename()==(std::max(sheet.paperWidth,sheet.paperHeight)>594.1?L"FY-XS-2026.9-A1.ctb":L"FY-XS-2026.9-A3 A4.ctb"),"template CTB was overridden");
    require(info.validatedSettings()->printLineweights()&&info.validatedSettings()->plotPlotStyles(),"template lineweights or plot styles are disabled");
   }
   const auto pdf=directory/(L"正式布局校核_图幅"+std::to_wstring(panel)+L".pdf");
   ProductPlanPdf engine;engine.write(info,candidate,pdf);
   require(fs::is_regular_file(pdf)&&fs::file_size(pdf)>0,"formal paper PDF missing");passed=true;
   report<<",\"status\":\"plotted\",\"paper\":"<<jsonUtf8(paper)<<",\"file\":"<<jsonUtf8(pdf.filename().wstring())
    <<",\"sha256\":\""<<sha(pdf)<<"\",\"paperScale\":1,\"viewportScale\":"<<sheet.scale
    <<",\"paperCoordinates\":"<<productPlanPaperSettingsJson(*info.validatedSettings())
    <<",\"device\":"<<jsonUtf8(effectiveDevice?effectiveDevice:L"")<<",\"plotStyle\":"<<jsonUtf8(effectiveStyle?effectiveStyle:L"");
  }catch(const std::exception& error){reason=error.what();report<<",\"status\":\"failed\",\"reason\":"<<jsonUtf8(bcpproduct::decodeMessage(reason));}
  report<<'}';
  job.drawingChecks.record(panel,"formal-layout-print",sheet.viewport,{"P03","P04","QA02"},bcpproduct::PlanReleaseGate::files,
   passed?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::failed,u8"实际纸空间图签、锁定视口及完整图框原生打印",reason);
 }
 const auto restoreLayout=manager->setCurrentLayout(previous.kACharPtr(),&db);
 const auto restorePlotMode=acedSetVar(L"BACKGROUNDPLOT",&original);
 report<<"],\"visualReview\":\"pending\"}";writeNew(directory/L"formal-layout-print-review.json",report.str());
 if(restoreLayout!=Acad::eOk)throw std::runtime_error("restore active layout after formal plot: "+std::to_string(restoreLayout));
 require(restorePlotMode==RTNORM,"restore formal plot execution mode");
}
