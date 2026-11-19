using ProductPrintedInkKey=std::tuple<std::size_t,std::string,std::size_t>;

struct ProductPrintedInkTarget {
 AcGePoint3d before;
 double value{};
 bool movable{},reviewedInterior{};
};

struct ProductPrintedInkFrame {
 AcGePoint2d center, offset;
 double width{}, height{}, ratio{};
 AcDbObjectId viewport;
 fs::path pdf;
};

struct ProductPrintedInkProof {
 fs::path directory;
 std::string digest;
 std::map<ProductPrintedInkKey,ProductPrintedInkTarget> targets;
 std::set<ProductPrintedInkKey> missing, colliding;
 std::map<ProductPrintedInkKey,AcGeVector3d> moves;
 std::map<ProductPrintedInkKey,double> baselineMoves;
 std::set<std::pair<std::size_t,std::string>> jointSearchChains, jointMovableBaselines;
 std::map<std::size_t,fs::path> pages;
 std::set<std::size_t> extendedPanels;
 std::size_t checked{}, modelPlots{};
 bcpproduct::PlanDrawingChecks drawingChecks;
 std::vector<std::array<std::string,3>> detailPending;
};

ProductPrintedInkFrame productPrintedInkFrame(ProductJob& job,AcDbDatabase& db,
    const fs::path& directory,std::size_t panel){
 const auto& w=job.planPrintWindows.at(panel-1);
 ProductPrintedInkFrame frame{{(w[0]+w[2])/2,(w[1]+w[3])/2},{0,0},w[2]-w[0],w[3]-w[1],
    job.unifiedUnitMM/job.unifiedScale,{},directory/(L"打印校核_图幅"+std::to_wstring(panel)+L".pdf")};
 const auto layout=job.planSheetLayouts.find(static_cast<int>(panel));
 if(layout==job.planSheetLayouts.end())return frame;
 frame.pdf=directory/(L"正式布局校核_图幅"+std::to_wstring(panel)+L".pdf");
 frame.viewport=find(db,layout->second.viewport);
 const auto viewport=open<AcDbViewport>(frame.viewport);
 const auto source=viewport->viewTarget();
 const auto viewCenter=viewport->viewCenter();
 const auto paper=viewport->centerPoint();
 frame.ratio=viewport->customScale();
 // Activity is checked by the plotter with this layout current. An inactive
 // layout can still supply its saved viewport bounds and scale.
 require(frame.ratio>0&&viewport->isLocked()&&!viewport->isPerspectiveOn()&&
    (viewport->viewDirection()-AcGeVector3d::kZAxis).length()<1e-8&&
    std::abs(viewport->twistAngle())<1e-8,"paper ink review requires a locked orthogonal plan viewport");
 frame.width=viewport->width()/frame.ratio;
 frame.height=viewport->height()/frame.ratio;
 frame.center=AcGePoint2d(source.x+viewCenter.x,source.y+viewCenter.y);
 frame.offset=AcGePoint2d(paper.x-layout->second.paperWidth/2,paper.y-layout->second.paperHeight/2);
 return frame;
}

bool productPrintedInkInside(const ProductPrintedInkFrame& frame,const AcGePoint3d& point){
 return std::abs(point.x-frame.center.x)<=frame.width/2&&std::abs(point.y-frame.center.y)<=frame.height/2;
}

std::set<std::size_t> productPrintedInkPanels(const ProductPrintedInkProof& proof){
 std::set<std::size_t> panels;
 for(const auto& [panel,page]:proof.pages)panels.insert(panel);
 return panels;
}

void productMergePrintedInk(const ProductPrintedInkProof& prior,ProductPrintedInkProof& current){
 const auto measured=productPrintedInkPanels(current);
 const auto keep=[&](const auto& key){return !measured.count(std::get<0>(key));};
 for(const auto& key:prior.missing)if(keep(key))current.missing.insert(key);
 for(const auto& key:prior.colliding)if(keep(key))current.colliding.insert(key);
 for(const auto& item:prior.targets)if(keep(item.first))current.targets.insert(item);
 for(const auto& item:prior.moves)if(keep(item.first))current.moves.insert(item);
 for(const auto& item:prior.baselineMoves)if(keep(item.first))current.baselineMoves.insert(item);
 for(const auto& item:prior.pages)current.pages.insert(item);
 for(const auto& [key,check]:prior.drawingChecks.items){
  const auto& scope=std::get<1>(key);
  if((scope=="formal-layout-print"&&keep(key))||
     (scope=="source-text-clearance"&&check.state==bcpproduct::PlanCheck::verified))
   current.drawingChecks.items.insert_or_assign(key,check);
 }
 current.checked=current.targets.size()-current.missing.size();
}

std::set<std::pair<std::size_t,std::string>> productPrintedInkPriorCollisions(const ProductJob& job){
 std::set<std::pair<std::size_t,std::string>> handles;
 for(const auto& issue:job.detailPending){
  if(issue[0]!=u8"纸面图元净空"||issue[2]!=u8"实际文字边界相交，联合排版后复验")continue;
  const auto first=issue[1].find('/');
  if(first==std::string::npos||first==0||issue[1].find_first_not_of("0123456789")!=first)continue;
  const auto next=issue[1].find('/',first+1);
  if(next==std::string::npos)continue;
  const auto panel=static_cast<std::size_t>(std::stoul(issue[1].substr(0,first)));
  handles.emplace(panel,issue[1].substr(first+1,next-first-1));
  handles.emplace(panel,issue[1].substr(next+1));
 }
 return handles;
}

void productWritePrintedInkRequest(ProductJob& job,AcDbDatabase& db,
    ProductPrintedInkProof& proof,const fs::path& input,bool extended,
    const std::set<std::size_t>* panels=nullptr,bool proposeRepairs=true){
 std::ostringstream out;
 out<<std::setprecision(17)<<"{\"contract\":\"plan.printed-ink-input.v1\",\"drawingSHA256\":\""<<proof.digest
    <<"\",\"extendedInteriorSearch\":"<<(extended?"true":"false")
    <<",\"proposeRepairs\":"<<(proposeRepairs?"true":"false")<<",\"frames\":[";
 const auto roots=modelTypes(db);
 const auto priorCollisions=productPrintedInkPriorCollisions(job);
 const auto point=[&](const AcGePoint3d& p){out<<'['<<p.x<<','<<p.y<<']';};
 bool firstFrame=true;
 for(std::size_t panel=1;panel<=job.planPrintWindows.size();++panel){
  if(panels&&!panels->count(panel))continue;
  const auto frame=productPrintedInkFrame(job,db,proof.directory,panel);
  if(!input.empty()){
   require(fs::is_regular_file(frame.pdf)&&fs::file_size(frame.pdf)>0,"the reviewed plan sheet PDF is missing");
   proof.pages.emplace(panel,frame.pdf);
  }
  if(!firstFrame)out<<',';firstFrame=false;
  out<<"{\"panel\":"<<panel<<",\"file\":"<<jsonUtf8(frame.pdf.filename().wstring())<<",\"sha256\":\""<<(input.empty()?"":sha(frame.pdf))
     <<"\",\"ratio\":"<<frame.ratio<<",\"modelCenter\":["<<frame.center.x<<','<<frame.center.y
     <<"],\"modelSize\":["<<frame.width<<','<<frame.height<<"],\"paperOffset\":["<<frame.offset.x<<','<<frame.offset.y
     <<"],\"extendedInteriorSearch\":"<<((extended||proof.extendedPanels.count(panel))?"true":"false")<<",\"targets\":[";
  std::unique_ptr<AcDbViewport,Close<AcDbViewport>> view;
  if(!frame.viewport.isNull())view=open<AcDbViewport>(frame.viewport);
  bool first=true;
  for(const auto& [wideHandle,type]:roots){
   if(type!=L"TDbDimension2")continue;
   const auto handle=ascii(wideHandle);
   const auto id=find(db,handle);
   const auto entity=open<AcDbEntity>(id);
   const auto layer=open<AcDbLayerTableRecord>(entity->layerId());
   if(!productPlanDimensionVisible(*entity)||!layer->isPlottable()||
      (view&&view->isLayerFrozenInViewport(entity->layerId())))continue;
   const auto grips=chainGrips(id);
   if(grips.size()<5)continue;
   const bool vertical=std::abs(grips[0].x-grips[1].x)<1e-5;
   if(!vertical&&std::abs(grips[0].y-grips[1].y)>=1e-5)continue;
   const auto values=chainValues(id);
   if(values.empty()||(grips.size()!=2*values.size()+6&&grips.size()!=2*values.size()+3))continue;
   const auto sourcePanel=panel<=job.layoutSourcePanels.size()?job.layoutSourcePanels[panel-1]:panel;
   const auto baseline=job.drawingChecks.items.find({sourcePanel,"interior-baseline",handle});
   const bool interior=baseline!=job.drawingChecks.items.end();
   bool modelReview=interior&&baseline->second.state!=bcpproduct::PlanCheck::verified;
   for(std::size_t i=0;i<values.size()&&interior&&!modelReview;++i){
    const auto check=job.drawingChecks.items.find({sourcePanel,"interior-clearance",handle+"/"+std::to_string(i+1)});
    modelReview=check!=job.drawingChecks.items.end()&&check->second.state!=bcpproduct::PlanCheck::verified;
   }
   const auto normal=[&](const AcGePoint3d& p){return vertical?p.x:p.y;};
   const auto along=[&](const AcGePoint3d& p){return vertical?p.y:p.x;};
   const auto form=productDimensionTexts(*entity);
   if(form.size()!=values.size())continue;
   std::vector<bool> shortSegments(values.size(),false);
   for(std::size_t i=0;i<values.size();++i){
    const auto stationLength=std::abs(along(grips[i+3])-along(grips[i+2]))*frame.ratio;
    // Native transformed stations can put an exact 450 mm just above 4.5 mm
    // on paper. Match the final reader's tolerance so the boundary is covered.
    shortSegments[i]=stationLength<=4.5+1e-6;
   }
   if(!modelReview&&!priorCollisions.count({panel,handle})&&
      std::find(shortSegments.begin(),shortSegments.end(),true)==shortSegments.end())continue;
   std::vector<int> modes;
   productGenericChainText(db,id,vertical,&modes);
   for(std::size_t i=0;i<values.size();++i){
    const auto alongMiddle=(along(grips[i+2])+along(grips[i+3]))/2;
    const auto baseline=normal(grips[0]);
    const auto projectedStation=vertical?AcGePoint3d(baseline,alongMiddle,0):AcGePoint3d(alongMiddle,baseline,0);
    if(!productPrintedInkInside(frame,projectedStation))continue;
    const auto height=form[i].height*form[i].scale;
    if(!std::isfinite(height)||height<=0||!std::isfinite(values[i]))continue;
    const auto glyph=grips[values.size()+3+i];
    const auto number=std::to_string(std::llround(std::abs(values[i])));
    const auto movable=i<modes.size()&&modes[i]==2;
    proof.targets.emplace(ProductPrintedInkKey{panel,handle,i},ProductPrintedInkTarget{glyph,values[i],movable,modelReview});
    if(!first)out<<',';first=false;
    out<<"{\"handle\":\""<<handle<<"\",\"segment\":"<<i<<",\"layer\":"<<wholeLayer(entity->layerId())
       <<",\"vertical\":"<<(vertical?"true":"false")<<",\"short\":"<<(shortSegments[i]?"true":"false")
       <<",\"interior\":"<<(modelReview?"true":"false")
       <<",\"movable\":"<<(movable?"true":"false")<<",\"height\":"<<height<<",\"digits\":"<<number.size()
       <<",\"number\":\""<<number<<'"'
       <<",\"anchor\":";point(glyph);
    out<<",\"start\":";point(grips[i+2]);out<<",\"end\":";point(grips[i+3]);out<<",\"baseline\":";point(grips[0]);
    out<<'}';
   }
  }
  out<<"],\"jointChains\":[";
  first=true;
  for(const auto& [sheet,handle]:proof.jointSearchChains){
   if(sheet!=panel||!proof.targets.count({panel,handle,0}))continue;
   const auto id=find(db,handle);
   const auto entity=open<AcDbEntity>(id);
   const auto grips=chainGrips(id);
   const auto values=chainValues(id);
   bool complete=!values.empty();
   for(std::size_t i=0;i<values.size()&&complete;++i)
    complete=proof.targets.count({panel,handle,i})&&proof.targets.at({panel,handle,i}).movable;
   if(!complete)continue;
   const auto sourcePanel=panel<=job.layoutSourcePanels.size()?job.layoutSourcePanels[panel-1]:panel;
   const bool movable=job.drawingChecks.items.count({sourcePanel,"interior-baseline",handle});
   if(movable)proof.jointMovableBaselines.emplace(panel,handle);
   std::vector<std::pair<AcGePoint3d,AcGePoint3d>> strokes;
   productPlanDimensionStrokes(*entity,strokes);
   if(!first)out<<',';first=false;
   out<<"{\"handle\":\""<<handle<<"\",\"layer\":"<<wholeLayer(entity->layerId())
      <<",\"movableBaseline\":"<<(movable?"true":"false")<<",\"grips\":[";
   for(std::size_t i=0;i<grips.size();++i){if(i)out<<',';point(grips[i]);}
   out<<"],\"values\":[";
   for(std::size_t i=0;i<values.size();++i){if(i)out<<',';out<<values[i];}
   out<<"],\"ink\":[";
   for(std::size_t i=0;i<strokes.size();++i){
    if(i)out<<',';
    out<<'[';point(strokes[i].first);out<<',';point(strokes[i].second);out<<']';
   }
   out<<"]}";
  }
  out<<"]}";
 }
 out<<"]}";
 if(!input.empty())writeNew(input,out.str());
}

void productInvokePrintedInkAnalyzer(const ProductJob& job,const fs::path& request,const fs::path& reply){
 require(fs::is_regular_file(job.planInkAnalyzer)&&fs::is_regular_file(job.planInkPython),
    "plot ink analyzer and its Python executable must exist");
 const auto quote=[](const fs::path& path){
  const auto value=path.wstring();
  require(!value.empty()&&value.find(L'"')==std::wstring::npos,"invalid plot analyzer path");
  return L"\""+value+L"\"";
 };
 auto command=quote(job.planInkPython)+L" -I -B "+quote(job.planInkAnalyzer)+L" "+quote(request)+L" "+quote(reply);
 STARTUPINFOW startup{};startup.cb=sizeof(startup);
 PROCESS_INFORMATION child{};
 require(CreateProcessW(job.planInkPython.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,
    nullptr,request.parent_path().c_str(),&startup,&child)!=FALSE,"start plot ink analyzer");
 File process;process.value=child.hProcess;
 File thread;thread.value=child.hThread;
 if(WaitForSingleObject(process.value,90000)!=WAIT_OBJECT_0){
  TerminateProcess(process.value,1);
  WaitForSingleObject(process.value,5000);
  throw std::runtime_error("plot ink analyzer timed out");
 }
 DWORD exitCode{};
 require(GetExitCodeProcess(process.value,&exitCode)&&exitCode==0&&fs::is_regular_file(reply),"analyse actual plan PDF ink");
}

void productReadPrintedInkReply(ProductPrintedInkProof& proof,const fs::path& path){
 std::ifstream read(path);
 read.imbue(std::locale::classic());
 std::string tag,digest;
 std::size_t pages{},expected{},missing{},colliding{},moves{};
 require(bool(read>>tag>>digest>>pages>>expected>>proof.checked>>missing>>colliding>>moves)&&
     tag=="PLANINKRESULT1"&&digest==proof.digest&&pages==proof.pages.size()&&expected==proof.targets.size()&&
     proof.checked<=expected&&missing==expected-proof.checked&&colliding<=proof.checked&&moves<=proof.checked,
     "plot ink receipt identity or coverage");
 while(read>>tag&&tag!="END"){
  std::size_t panel{},segment{};
  std::string handle;
  require(bool(read>>panel>>handle>>segment),"plot ink reply row");
  const ProductPrintedInkKey key{panel,handle,segment};
  const auto target=proof.targets.find(key);
  require(target!=proof.targets.end(),"plot ink replies must identify a plotted dimension");
  if(tag=="MISSING")require(proof.missing.insert(key).second,"duplicate missing paper text");
  else if(tag=="COLLISION")require(proof.colliding.insert(key).second,"duplicate paper text collision");
  else if(tag=="MOVE"){
   double x{},y{};
   require(bool(read>>x>>y)&&std::isfinite(x)&&std::isfinite(y)&&target->second.movable&&
      proof.moves.emplace(key,AcGeVector3d(x,y,0)).second,"invalid printed dimension movement");
  }else if(tag=="BASELINE"){
   double normal{};
   require(bool(read>>normal)&&std::isfinite(normal)&&target->second.movable&&
      proof.jointMovableBaselines.count({panel,handle})&&
      proof.baselineMoves.emplace(key,normal).second,"invalid jointly proposed interior baseline");
  }else throw std::runtime_error("unknown plot ink receipt row");
 }
 std::string extra;
 require(tag=="END"&&!(read>>extra)&&proof.missing.size()==missing&&
     proof.colliding.size()==colliding&&proof.moves.size()==moves,"incomplete plot ink receipt");
 for(const auto& key:proof.missing)require(!proof.colliding.count(key),"unread glyph cannot be a verified collision");
 for(const auto& [key,normal]:proof.baselineMoves)
  require(proof.colliding.count(key),"joint baseline must belong to a verified collision");
 for(const auto& [key,move]:proof.moves){
  const auto& [panel,handle,segment]=key;
  const auto sameChain=proof.colliding.lower_bound(ProductPrintedInkKey{panel,handle,0});
  require(!proof.missing.count(key)&&sameChain!=proof.colliding.end()&&
      std::get<0>(*sameChain)==panel&&std::get<1>(*sameChain)==handle,
      "only observed labels belonging to a colliding native chain may be edited");
 }
}

ProductPrintedInkProof productMeasurePrintedInk(ProductJob& job,AcApDocument& doc,const fs::path& candidate,
    const fs::path& directory,bool extended=false,const std::set<std::size_t>* panels=nullptr,
    const std::set<std::size_t>& extendedPanels={},bool proposeRepairs=true){
 require(fs::create_directory(directory),"new isolated plan print attempt directory");
 ProductPrintedInkProof proof;
 proof.directory=directory;
 proof.digest=sha(candidate);
 proof.extendedPanels=extendedPanels;
 const auto pending=job.detailPending;
 const auto checks=job.drawingChecks;
 const auto originalCount=job.planPrintedPages;
 bool allLayouts=job.planSheetLayouts.size()==job.planPrintWindows.size();
 for(std::size_t panel=1;panel<=job.planPrintWindows.size();++panel)
  allLayouts=allLayouts&&job.planSheetLayouts.count(static_cast<int>(panel));
 const auto* selected=allLayouts?panels:nullptr;
 if(extended)for(std::size_t panel=1;panel<=job.planPrintWindows.size();++panel)
  if(!selected||selected->count(panel))proof.extendedPanels.insert(panel);
 job.planPrintedPages=0;
 try{
  if(!allLayouts)productPrintPlanReview(job,doc,candidate,directory);
  productPrintPlanLayouts(job,doc,candidate,directory,selected);
  proof.detailPending=job.detailPending;
  proof.drawingChecks=job.drawingChecks;
  proof.modelPlots=job.planPrintedPages;
  job.detailPending=pending;job.drawingChecks=checks;job.planPrintedPages=originalCount;
 }catch(...){
  job.detailPending=pending;job.drawingChecks=checks;job.planPrintedPages=originalCount;
  throw;
 }
 const auto input=directory/L"paper-ink-input.json",reply=directory/L"paper-ink-result.tsv";
 auto& db=*doc.database();Working current(&db);
 productWritePrintedInkRequest(job,db,proof,input,extended,selected,proposeRepairs);
 if(allLayouts){
  proof.modelPlots=proof.pages.size();
  std::ostringstream report;
  report<<"{\"outputSHA256\":\""<<proof.digest<<"\",\"scale\":"<<job.unifiedScale
        <<",\"visualReview\":\"short-dimension-ink-receipt\",\"plotStyle\":"<<jsonUtf8(job.planPlotStyle)<<",\"pages\":[";
  bool first=true;
  for(const auto& [panel,page]:proof.pages){
   if(!first)report<<',';first=false;
   report<<"{\"panel\":"<<panel<<",\"status\":\"plotted\",\"layout\":"<<jsonUtf8(job.planSheetLayouts.at(static_cast<int>(panel)).name)
         <<",\"file\":"<<jsonUtf8(page.filename().wstring())<<",\"sha256\":\""<<sha(page)<<"\"}";
  }
  report<<"]}";
  writeNew(directory/L"plan-print-review.json",report.str());
 }
 productInvokePrintedInkAnalyzer(job,input,reply);
 productReadPrintedInkReply(proof,reply);
 require(sha(candidate)==proof.digest,"the saved drawing changed during printed ink review");
 return proof;
}

bool productSamePrintedGrips(const std::vector<AcGePoint3d>& a,const std::vector<AcGePoint3d>& b){
 return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),
    [](const auto& x,const auto& y){return x.distanceTo(y)<1e-5;});
}

std::set<std::string> productPrintedDimensionCaptionHits(AcDbDatabase& db,AcDbObjectId id){
 const auto chain=open<AcDbEntity>(id);
 std::vector<std::pair<AcGePoint3d,AcGePoint3d>> strokes;
 productPlanDimensionStrokes(*chain,strokes);
 std::set<std::string> hits;
 if(strokes.empty())return hits;
 AcDbExtents reach(strokes.front().first,strokes.front().first);
 for(const auto& [a,b]:strokes){reach.addPoint(a);reach.addPoint(b);}
 for(const auto& [wide,type]:modelTypes(db)){
  if(type!=L"TDbOpening"&&type!=L"TDbGroupOpening")continue;
  const auto handle=ascii(wide);
  const auto opening=open<AcDbEntity>(find(db,handle));
  AcDbExtents bounds;
  if(!productPlanDimensionVisible(*opening)||opening->getGeomExtents(bounds)!=Acad::eOk||
     !productGenericBoxesOverlap(bounds,reach,0.))continue;
  std::vector<AcDbExtents> glyphs;
  productGenericTextLeaves(*opening,glyphs);
  if(std::any_of(glyphs.begin(),glyphs.end(),[&](const auto& glyph){
   return std::any_of(strokes.begin(),strokes.end(),[&](const auto& line){
    return productGenericSegmentHitsBox(line.first,line.second,glyph,-1e-5);
   });
  }))hits.insert(handle);
 }
 return hits;
}

std::vector<AcGePoint3d> productTryPrintedInteriorBaseline(AcDbDatabase& db,AcDbObjectId id,std::size_t segment,double paper,
 double shiftMM,const std::vector<AcGePoint3d>& grips,const std::vector<double>& values){
 const auto n=values.size();
 require(segment<n&&grips.size()==2*n+6,"interior chain has public baseline and stations");
 const bool vertical=std::abs(grips[0].x-grips[1].x)<1e-5;
 require(vertical||std::abs(grips[0].y-grips[1].y)<1e-5,"orthogonal printed interior chain");
 const auto normal=[vertical](const AcGePoint3d& p){return vertical?p.x:p.y;};
 const double station=normal(grips[segment+2]);
 const double offset=station-normal(grips[0]);
 require(std::abs(offset)>1e-5,"baseline trial requires an existing side of the measured station");
 const double delta=std::copysign(1.,offset)*paper*shiftMM;
 const AcGeVector3d movement(vertical?delta:0.,vertical?0.:delta,0.);
 for(std::size_t i=2;i<n+3;++i)
  require((normal(grips[i])-normal(grips[0]))*(normal(grips[i])-normal(grips[0])-delta)>0,
    "move without crossing any measured station");
 const auto originalCaptionHits=productPrintedDimensionCaptionHits(db,id);
 moveDimensionGrip(id,0,grips[0]+movement);
 auto updated=chainGrips(id);
 require(updated.size()==grips.size()&&updated.front().distanceTo(grips.front()+movement)<1e-5,
     "read the trial interior dimension baseline");
 for(std::size_t i=0;i<n;++i){
  auto anchor=grips[n+3+i]+movement;
  if(i==segment){
   const auto midpoint=grips[i+2]+(grips[i+3]-grips[i+2])*.5;
   if(vertical)anchor.y=midpoint.y;else anchor.x=midpoint.x;
  }
  moveDimensionGrip(id,static_cast<int>(n+3+i),anchor);
 }
 updated=chainGrips(id);
 require(updated.size()==grips.size()&&chainValues(id)==values&&
   std::equal(updated.begin()+2,updated.begin()+n+3,grips.begin()+2,[](const auto& a,const auto& b){
    return a.distanceTo(b)<1e-5;
   }),"printed interior baseline repair preserves the measured stations");
 {
  auto chain=open<AcDbEntity>(id);
  TemplateParts parts(chain.get());
  require(chain->explode(parts.values)==Acad::eOk&&parts.values.length()==n,
    "read every repaired interior dimension witness");
  for(int i=0;i<parts.values.length();++i){
   const auto* dim=AcDbAlignedDimension::cast(static_cast<AcRxObject*>(parts.values[i]));
   require(dim&&!dim->dimse1()&&!dim->dimse2(),"keep both repaired interior witness lines visible");
   const double defined=normal(dim->dimLinePoint()),gap=dim->dimexo()*dim->dimscale();
   require(std::min(std::abs(defined-normal(dim->xLine1Point())),
                    std::abs(defined-normal(dim->xLine2Point())))-gap>.01*paper,
      "retain both visible witnesses after moving the baseline");
  }
 }
 const auto newCaptionHits=productPrintedDimensionCaptionHits(db,id);
 require(std::includes(originalCaptionHits.begin(),originalCaptionHits.end(),newCaptionHits.begin(),newCaptionHits.end()),
     "the printed baseline trial crosses another fixed opening number");
 return updated;
}

std::set<std::string> productSavePrintedInkRepair(const ProductJob& job,const ProductPrintedInkProof& proof,
    const fs::path& candidate,const fs::path& edited,
    const std::optional<std::pair<ProductPrintedInkKey,double>>& baselineTrial={}){
 using Repair=std::tuple<std::vector<AcGePoint3d>,std::vector<double>,std::map<std::size_t,AcGeVector3d>>;
 std::map<std::string,Repair> repairs;
 std::map<std::string,std::vector<ProductDimensionText>> originalAppearance;
 std::set<std::string> changed;
 AcDbDatabase db(false,true);
 productStatus(db.readDwgFile(candidate.c_str(),AcDbDatabase::kForReadAndAllShare,false),"open isolated paper repair drawing");
 productStatus(db.closeInput(true),"close paper repair input");
 Working working(&db);
 for(const auto& [key,movement]:proof.moves){
  if(baselineTrial)break;
  const auto& [panel,handle,segment]=key;
  auto found=repairs.find(handle);
  if(found==repairs.end()){
   const auto id=find(db,handle);
   require(std::wstring(open<AcDbEntity>(id)->isA()->name())==L"TDbDimension2","keep native dimension chain");
   found=repairs.emplace(handle,Repair{chainGrips(id),chainValues(id),{}}).first;
   changed.insert(handle);
  }
  auto& [grips,values,edits]=found->second;
  const auto& target=proof.targets.at(key);
  const auto paper=job.unifiedScale/job.unifiedUnitMM;
  require(movement.length()<paper*35&&segment<values.size()&&grips.size()==values.size()*2+6&&
      grips[values.size()+3+segment].distanceTo(target.before)<1e-5&&values[segment]==target.value,
      "printed dimension edit differs from its saved anchor");
  const auto [same,inserted]=edits.emplace(segment,movement);
  require(inserted||(same->second-movement).length()<1e-5,"the same text has dissimilar edits on different sheets");
 }
 if(baselineTrial){
  const auto& [key,_]=*baselineTrial;
  const auto& [panel,handle,segment]=key;
  const auto id=find(db,handle);
  repairs.emplace(handle,Repair{chainGrips(id),chainValues(id),{}});
  changed.insert(handle);
 }
 const auto roots=modelTypes(db);
 const auto protectedBefore=wholeProtected(db,changed,true);
 for(auto& [handle,item]:repairs){
  auto& [grips,values,edits]=item;
  const auto id=find(db,handle);
  originalAppearance.emplace(handle,productDimensionTexts(*open<AcDbEntity>(id)));
  if(baselineTrial&&handle==std::get<1>(baselineTrial->first))
   grips=productTryPrintedInteriorBaseline(db,id,std::get<2>(baselineTrial->first),job.unifiedScale/job.unifiedUnitMM,
       baselineTrial->second,grips,values);
  const auto originalGrips=grips;
  std::optional<double> movedNormal;
  for(const auto& [key,normal]:proof.baselineMoves){
   const auto& [panel,chain,segment]=key;
   if(chain!=handle)continue;
   if(movedNormal){
    require(std::abs(*movedNormal-normal)<1e-5,"the same baseline has different proposals on different sheets");
    continue;
   }
   const bool vertical=std::abs(grips[0].x-grips[1].x)<1e-5;
   const double stationNormal=vertical?grips[segment+2].x:grips[segment+2].y;
   const double baseNormal=vertical?grips[0].x:grips[0].y;
   const double paper=job.unifiedScale/job.unifiedUnitMM;
   require(std::abs(normal)<=18*paper,"joint baseline shift exceeds allowed interior search");
   grips=productTryPrintedInteriorBaseline(db,id,segment,paper,
      normal/(paper*std::copysign(1.,stationNormal-baseNormal)),grips,values);
   movedNormal=normal;
  }
  for(const auto& [segment,movement]:edits){
   const auto at=values.size()+3+segment;
   grips[at]=originalGrips[at]+movement;
   moveDimensionGrip(id,static_cast<int>(at),grips[at]);
  }
  require(productSamePrintedGrips(chainGrips(id),grips)&&chainValues(id)==values,
      "printed dimension edit changed a station, baseline or measured value");
 }
 require(modelTypes(db)==roots&&bcpproduct::samePublicGeometry(protectedBefore,wholeProtected(db,changed,true)),
    "printed dimension edit changed protected drawing objects");
 require(!fs::exists(edited),"new private plot repair candidate");
 productStatus(db.saveAs(edited.c_str(),false,AcDb::kDHL_CURRENT),"save printed dimension repair");
 AcDbDatabase saved(false,true);
 productStatus(saved.readDwgFile(edited.c_str(),AcDbDatabase::kForReadAndAllShare,false),"reopen printed dimension repair independently");
 productStatus(saved.closeInput(true),"close reopened plot repair input");
 Working reopened(&saved);
 for(const auto& [handle,item]:repairs){
  const auto id=find(saved,handle);
  const auto& [grips,values,edits]=item;
  require(productSamePrintedGrips(chainGrips(id),grips)&&chainValues(id)==values,
    "the reopened dimension no longer matches its accepted grips and measurements");
  const auto style=productDimensionTexts(*open<AcDbEntity>(id));
  const auto& before=originalAppearance.at(handle);
  require(style.size()==values.size()&&style.size()==before.size()&&
    std::equal(before.begin(),before.end(),style.begin(),[](auto a,const auto& b){
     const auto sameRecord=[](AcDbObjectId x,AcDbObjectId y){
      return x.isNull()?y.isNull():!y.isNull()&&axisHandle(x)==axisHandle(y);
     };
     if(!sameRecord(a.style,b.style)||!sameRecord(a.font,b.font))return false;
     a.style=b.style;a.font=b.font;
     return productEqualDimensionAppearance(a,b);
    }),
    "the reopened dimension no longer references its original per-segment appearance");
 }
 const auto protectedAfter=wholeProtected(saved,changed,true);
 require(modelTypes(saved)==roots,"printed dimension edits changed the model object set upon reopen");
 if(!bcpproduct::samePublicGeometry(protectedBefore,protectedAfter)){
  // Some Tianzheng classes materialize their derived grips differently after SaveAs.
  // Compare against the same serialization of the untouched candidate before accepting any such difference.
  const auto control=edited.parent_path()/(edited.stem().wstring()+L"-untouched-roundtrip.dwg");
  AcDbDatabase unedited(false,true);
  productStatus(unedited.readDwgFile(candidate.c_str(),AcDbDatabase::kForReadAndAllShare,false),"open untouched paper repair control");
  productStatus(unedited.closeInput(true),"close untouched paper repair control input");
  {
   Working unchanged(&unedited);
   require(modelTypes(unedited)==roots&&bcpproduct::samePublicGeometry(protectedBefore,wholeProtected(unedited,changed,true)),
       "untouched control does not match the original paper repair drawing");
   productStatus(unedited.saveAs(control.c_str(),false,AcDb::kDHL_CURRENT),"save untouched paper repair control");
  }
  AcDbDatabase controlReadback(false,true);
  productStatus(controlReadback.readDwgFile(control.c_str(),AcDbDatabase::kForReadAndAllShare,false),"independently reopen untouched control");
  productStatus(controlReadback.closeInput(true),"close untouched control readback");
  Working compare(&controlReadback);
  const auto protectedControl=wholeProtected(controlReadback,changed,true);
  const bool same=modelTypes(controlReadback)==roots&&bcpproduct::samePublicGeometry(protectedControl,protectedAfter);
  if(!same){
   writeNew(edited.wstring()+L".protected-before.json",protectedBefore);
   writeNew(edited.wstring()+L".protected-after.json",protectedAfter);
   writeNew(edited.wstring()+L".protected-control.json",protectedControl);
  }
  require(same,"printed dimension edits changed protected objects beyond an untouched save/reopen");
  writeNew(edited.wstring()+L".roundtrip.json","{\"controlSourceSHA256\":\""+sha(candidate)+
      "\",\"uneditedRoundtripSHA256\":\""+sha(control)+"\",\"repairedSHA256\":\""+sha(edited)+
      "\",\"protectedObjectsMatch\":true}");
 }
 for(const auto& [h,item]:repairs){
  const auto& [grips,values,edits]=item;
  const auto id=find(saved,h);
  require(productSamePrintedGrips(chainGrips(id),grips)&&chainValues(id)==values,"printed dimension edit changed when reopened");
 }
 return changed;
}

AcApDocument* productOpenPrintedInkCandidate(const fs::path& path){
 AcApDocManager::DocOpenParams options{};
 options.mpwszFileName=path.c_str();
 options.mnInitialViewType=AcApDocManager::DocOpenParams::kDefaultView;
 options.mnFlags=AcApDocManager::DocOpenParams::kFileNameArgIsUnicode;
 productStatus(acDocManager->appContextOpenDocument(&options),"open saved printed ink candidate");
 auto* document=acDocManager->mdiActiveDocument();
 require(document&&fs::weakly_canonical(document->fileName())==fs::weakly_canonical(path),"printed ink candidate document identity");
 return document;
}

void productReverifyPrintedInteriorInk(ProductJob& job,AcDbDatabase& db,const ProductPrintedInkProof& proof,
    const std::set<std::pair<std::size_t,std::string>>& verified){
 for(const auto& [sheet,handle]:verified){
  const auto panel=sheet<=job.layoutSourcePanels.size()?job.layoutSourcePanels[sheet-1]:sheet;
  auto baseline=job.drawingChecks.items.find({panel,"interior-baseline",handle});
  if(baseline==job.drawingChecks.items.end())continue;
  const auto id=find(db,handle);
  const auto grips=chainGrips(id);
  const auto count=chainValues(id).size();
  if(!count||grips.size()!=2*count+6)continue;
  const bool vertical=std::abs(grips[0].x-grips[1].x)<1e-5;
  if(!vertical&&std::abs(grips[0].y-grips[1].y)>=1e-5)continue;
  std::vector<int> movement;
  productGenericChainText(db,id,vertical,&movement);
  if(movement.size()!=count||std::any_of(movement.begin(),movement.end(),[](int mode){return mode!=2;}))continue;
  bool allClear=true;
  for(std::size_t segment=0;segment<count;++segment){
   const auto number=std::to_string(segment+1);
   const auto found=job.drawingChecks.items.find({panel,"interior-clearance",handle+"/"+number});
   const auto target=proof.targets.find({sheet,handle,segment});
   if(found==job.drawingChecks.items.end()||target==proof.targets.end()){
    allClear=false;
    continue;
   }
   auto& check=found->second;
   if(check.state==bcpproduct::PlanCheck::verified)continue;
   if(!target->second.reviewedInterior){allClear=false;continue;}
   check.state=bcpproduct::PlanCheck::verified;
   check.basis=u8"同一重开DWG的无引线实际 PDF 字框、尺寸测段与周边线迹";
   check.reason.clear();
   const auto prefix=u8"第"+number+u8"段 ";
   auto& issues=job.detailPending;
   issues.erase(std::remove_if(issues.begin(),issues.end(),[&](const auto& issue){
    return (issue[0]==u8"室内尺寸避让"||issue[0]==u8"室内尺寸测段对应"||issue[0]==u8"室内尺寸字框未验证")&&
        issue[1]==handle&&issue[2].rfind(prefix,0)==0;
   }),issues.end());
   productRecordRuleEvidence(job,"D18","{\"panel\":"+std::to_string(panel)+",\"object\":\""+handle+
      "\",\"segment\":"+number+",\"savedReopened\":true,\"sameSide\":true,\"nearMeasuredSegment\":true,"
      "\"drawingSHA256\":\""+proof.digest+"\",\"result\":\"verified-plotted-interior-text-clearance\"}");
  }
  auto& witnessCheck=baseline->second;
  if(!allClear||witnessCheck.state==bcpproduct::PlanCheck::verified||
     witnessCheck.basis!="saved-reopened-visible-adjustment-awaiting-text")continue;
  const auto chain=open<AcDbEntity>(id);
  TemplateParts parts(chain.get());
  if(chain->explode(parts.values)!=Acad::eOk||parts.values.length()!=static_cast<int>(count))continue;
  const auto normal=[vertical](const auto& point){return vertical?point.x:point.y;};
  bool visible=true;
  for(int i=0;i<parts.values.length()&&visible;++i){
   const auto* part=AcDbAlignedDimension::cast(static_cast<AcRxObject*>(parts.values[i]));
   if(!part||part->dimse1()||part->dimse2()){visible=false;continue;}
   const auto line=normal(part->dimLinePoint());
   const auto start=part->dimexo()*part->dimscale();
   visible=std::min(std::abs(line-normal(part->xLine1Point())),std::abs(line-normal(part->xLine2Point())))>
       start+.01/job.unifiedUnitMM;
  }
  if(!visible)continue;
  witnessCheck.state=bcpproduct::PlanCheck::verified;
  witnessCheck.basis=u8"重开实测可见界线及同一版 PDF 净空";
  witnessCheck.reason.clear();
  auto& issues=job.detailPending;
  issues.erase(std::remove_if(issues.begin(),issues.end(),[&](const auto& issue){
   return issue[0]==u8"室内尺寸界线"&&issue[1]==handle;
  }),issues.end());
  productRecordRuleEvidence(job,"D23","{\"panel\":"+std::to_string(panel)+",\"object\":\""+handle+
     "\",\"savedReopened\":true,\"drawingSHA256\":\""+proof.digest+
     "\",\"result\":\"verified-visible-adjustment-and-plotted-text-clearance\"}");
 }
}

void productDismissSupersededPaperInk(ProductJob& job,AcDbDatabase& db,const ProductPrintedInkProof& proof){
 Working working(&db);
 std::map<std::pair<std::size_t,std::string>,std::size_t> coverage;
 std::set<std::pair<std::size_t,std::string>> hasIssue;
 for(const auto& [key,target]:proof.targets){
  const auto& [panel,handle,segment]=key;
  ++coverage[{panel,handle}];
  if(proof.missing.count(key)||proof.colliding.count(key))hasIssue.emplace(panel,handle);
 }
 std::set<std::pair<std::size_t,std::string>> verified;
 for(const auto& [key,checked]:coverage){
  if(!hasIssue.count(key)&&chainValues(find(db,key.second)).size()==checked)verified.insert(key);
 }
 productReverifyPrintedInteriorInk(job,db,proof,verified);
 auto& issues=job.detailPending;
 issues.erase(std::remove_if(issues.begin(),issues.end(),[&](const auto& issue){
  if(issue[0]!=u8"纸面图元净空")return false;
  const auto slash=issue[1].find('/');
  if(slash==std::string::npos||slash==0||issue[1].find_first_not_of("0123456789")!=slash)return false;
  const auto panel=static_cast<std::size_t>(std::stoul(issue[1].substr(0,slash)));
  const auto h=issue[1].substr(slash+1);
  if(issue[2].rfind(u8"屋面细部尺寸第",0)==0)return verified.count({panel,h})>0;
  if(issue[2]!=u8"实际文字边界相交，联合排版后复验")return false;
  const auto pair=h.find('/');
  if(pair==std::string::npos)return false;
  const bool covered=verified.count({panel,h.substr(0,pair)})||verified.count({panel,h.substr(pair+1)});
  if(covered&&job.planPrintDefects>0)--job.planPrintDefects;
  return covered;
 }),issues.end());
}

std::set<std::size_t> productAffectedPrintPanels(ProductJob& job,AcDbDatabase& before,AcDbDatabase& after,
 const std::set<std::string>& handles){
 std::set<std::size_t> affected;
 const auto owner=[&](AcDbDatabase& db){
  Working working(&db);
  for(const auto& handle:handles){
   const auto id=find(db,handle);
   const auto entity=open<AcDbEntity>(id);
   AcDbExtents bounds;
   if(entity->getGeomExtents(bounds)!=Acad::eOk)return false;
   if(std::wstring_view(entity->isA()->name())==L"TDbDimension2")
    for(const auto& point:chainGrips(id))bounds.addPoint(point);
   const auto a=bounds.minPoint(),b=bounds.maxPoint();
   if(!std::isfinite(a.x)||!std::isfinite(a.y)||!std::isfinite(b.x)||!std::isfinite(b.y)||a.x>b.x||a.y>b.y)return false;
   for(std::size_t panel=1;panel<=job.planPrintWindows.size();++panel){
    if(!job.planSheetLayouts.count(static_cast<int>(panel)))return false;
    const auto frame=productPrintedInkFrame(job,db,{},panel);
    // Include the paper clearance band so strokes on a clipped edge are rechecked.
    const double margin=1./frame.ratio;
    if(a.x<=frame.center.x+frame.width/2+margin&&b.x>=frame.center.x-frame.width/2-margin&&
       a.y<=frame.center.y+frame.height/2+margin&&b.y>=frame.center.y-frame.height/2-margin)affected.insert(panel);
   }
  }
  return true;
 };
 // An empty result makes the caller verify all views in the trial.
 try{if(!owner(before)||!owner(after))return {};}
 catch(const std::exception&){return {};}
 return affected;
}

#include "ProductPlanPostcheckRepair.inc"

void productRepairPlottedPlan(ProductJob& job,AcApDocument*& final,fs::path& candidate){
 require(acDocManager->isApplicationContext()&&final&&job.planPrintReview&&!job.planPrintWindows.empty(),
    "printed repair requires an owned reopened drawing and print windows");
 const auto directory=job.work/L"printed-ink";
 require(fs::create_directory(directory),"start a new private printed repair");
 const bool localPostcheck=job.planPostcheckMode==L"repair";
 std::size_t passes=0;
 std::size_t trialSheetPrints=0,scopeFallbacks=0;
 const auto trialStart=GetTickCount64();
 constexpr std::size_t legacyAttemptLimit=16;
 std::size_t passLimit=1+2*legacyAttemptLimit;
 const auto initial=[&]{
  ProductPrintedInkProof result;
  result.digest=sha(candidate);
  result.drawingChecks=job.drawingChecks;
  result.detailPending=job.detailPending;
  std::set<std::size_t> affected;
  bool known=true;
  try{
   auto& db=*final->database();Working working(&db);
   productWritePrintedInkRequest(job,db,result,{},false);
  }catch(const std::exception&){known=false;++scopeFallbacks;}
  for(const auto& item:result.targets)affected.insert(std::get<0>(item.first));
  if(known&&affected.empty())return result;
  ++passes;
  result=productMeasurePrintedInk(job,*final,candidate,directory/L"attempt-0",false,known?&affected:nullptr);
  trialSheetPrints+=result.pages.size();
  return result;
 };
 auto best=initial();
 const auto measure=[&](AcApDocument& document,const fs::path& saved,const fs::path& report,
                        const std::set<std::string>& handles,bool extended=false){
  require(passes<passLimit,"local plan print budget exhausted");
  ++passes;
  const auto affected=productAffectedPrintPanels(job,*final->database(),*document.database(),handles);
  if(affected.empty())++scopeFallbacks;
  auto result=productMeasurePrintedInk(job,document,saved,report,extended,
      affected.empty()?nullptr:&affected,best.extendedPanels);
  trialSheetPrints+=result.pages.size();
  productMergePrintedInk(best,result);
  return result;
 };
 const auto initialProblems=best.colliding.size()+best.missing.size();
 if(localPostcheck)passLimit=8+4*std::min<std::size_t>(initialProblems/12,2);
 const auto hasPendingText=localPostcheck&&job.planPostcheckTextRepair&&
  std::any_of(job.drawingChecks.items.begin(),job.drawingChecks.items.end(),[](const auto& entry){
   return std::get<1>(entry.first)=="source-text-clearance"&&
    (entry.second.state==bcpproduct::PlanCheck::pending||entry.second.state==bcpproduct::PlanCheck::failed);
  });
 const auto extendedPassReserve=localPostcheck?std::size_t{1}:std::size_t{0};
 const auto sourceTextPassLimit=passLimit-extendedPassReserve;
 const auto dimensionPassLimit=sourceTextPassLimit-(hasPendingText?2:0);
 std::size_t accepted=0;
 std::string trialError;
 std::set<ProductPrintedInkKey> acceptedBaselines;
 const std::vector<double> baselineOffsets{-6.,3.,-3.,-12.,4.5,-18.,1.5,-1.5,.75,-.75,2.25,-2.25};
 std::map<ProductPrintedInkKey,std::size_t> nextBaselineOffset;
 bool trialBaselineNext=false;
 for(std::size_t i=1;i<=legacyAttemptLimit&&passes<dimensionPassLimit;++i){
  std::optional<std::pair<ProductPrintedInkKey,double>> baselineTrial;
  if(best.moves.empty()||trialBaselineNext){
   if(!best.missing.empty()||best.colliding.empty())break;
   auto target=best.colliding.end();
   for(auto candidate=best.colliding.begin();candidate!=best.colliding.end();++candidate){
    const auto& key=*candidate;
    const auto& [panel,handle,segment]=key;
    if(!best.targets.at(key).movable||panel>job.layoutSourcePanels.size()||
       !job.drawingChecks.items.count({job.layoutSourcePanels[panel-1],"interior-baseline",handle})||
       nextBaselineOffset[key]>=baselineOffsets.size())continue;
    if(target==best.colliding.end()||nextBaselineOffset[key]<nextBaselineOffset[*target])target=candidate;
   }
   if(target==best.colliding.end())break;
   baselineTrial=std::make_pair(*target,baselineOffsets[nextBaselineOffset[*target]++]);
  }
  const auto token=std::to_wstring(i);
  auto edited=directory/(L"drawing-"+token+L".dwg");
  AcApDocument* trial{};
  try{
   auto handles=productSavePrintedInkRepair(job,best,candidate,edited,baselineTrial);
   trial=productOpenPrintedInkCandidate(edited);
   auto proof=measure(*trial,edited,directory/(L"attempt-"+token),handles);
   if(passes<dimensionPassLimit&&baselineTrial&&!proof.moves.empty()&&!proof.colliding.empty()&&
      proof.missing.size()<=best.missing.size()){
    // A moved baseline opens space but can leave the adjacent labels in the
    // wrong place. Review the baseline and its new text layout as one trial.
    const auto repacked=directory/(L"drawing-"+token+L"-repacked.dwg");
    const auto updated=productSavePrintedInkRepair(job,proof,edited,repacked);
    handles.insert(updated.begin(),updated.end());
    productStatus(acDocManager->appContextCloseDocument(trial),"close baseline-only review");
    trial=nullptr;
    trial=productOpenPrintedInkCandidate(repacked);
    proof=measure(*trial,repacked,directory/(L"attempt-"+token+L"-repacked"),handles);
    edited=repacked;
   }
   if(!productPrintedProofRetainsClearance(best,proof)||
      proof.missing.size()+proof.colliding.size()>=best.missing.size()+best.colliding.size()){
    productStatus(acDocManager->appContextCloseDocument(trial),"discard nonimproving printed repair");
    trial=nullptr;
    trialBaselineNext=true;
    continue;
   }
   productStatus(acDocManager->appContextCloseDocument(final),"close previous reviewed drawing");
   final=trial;
   trial=nullptr;
   best=std::move(proof);
   candidate=edited;
   job.planVerifiedCandidateHash=best.digest;
   if(baselineTrial)acceptedBaselines.insert(baselineTrial->first);
   trialBaselineNext=false;
   ++accepted;
  }catch(const std::exception& error){
   if(trial)acDocManager->appContextCloseDocument(trial);
   trialError=error.what();
   trialBaselineNext=true;
  }
 }
 std::size_t textExamined=0;
 auto textRepairs=localPostcheck?productRepairSourceTextInk(job,final,candidate,best,directory,measure,
   passes,sourceTextPassLimit,textExamined,trialError):std::vector<ProductAcceptedSourceText>{};
 if(localPostcheck&&passes<passLimit&&!best.colliding.empty()){
  AcApDocument* trial{};
  try{
   auto search=best;
   search.moves.clear();search.baselineMoves.clear();search.colliding.clear();search.missing.clear();
   for(const auto& [panel,handle,segment]:best.colliding)search.jointSearchChains.emplace(panel,handle);
   search.directory=directory/L"extended-search";
   require(fs::create_directory(search.directory),"new isolated extended search directory");
   const auto panels=productPrintedInkPanels(best);
   for(const auto& [panel,page]:best.pages)fs::copy_file(page,search.directory/page.filename());
   const auto input=search.directory/L"paper-ink-extended-input.json";
   const auto reply=search.directory/L"paper-ink-extended-result.tsv";
   search.pages.clear();search.targets.clear();
   {
    auto& db=*final->database();
    Working context(&db);
    productWritePrintedInkRequest(job,db,search,input,true,&panels);
   }
   productInvokePrintedInkAnalyzer(job,input,reply);
   productReadPrintedInkReply(search,reply);
   if(!search.moves.empty()){
    auto edited=directory/L"drawing-extended.dwg";
    auto handles=productSavePrintedInkRepair(job,search,candidate,edited);
    std::shared_ptr<const ProductPlanPostcheckTextEdit> joinedNote;
    if(job.planPostcheckTextRepair){
     for(const auto& [key,check]:best.drawingChecks.items){
      const auto& [sourcePanel,scope,handle]=key;
      if(scope!="source-text-clearance"||check.state!=bcpproduct::PlanCheck::pending)continue;
      const auto joined=directory/(L"drawing-extended-note-"+bcpproduct::decodeMessage(handle)+L".dwg");
      ++textExamined;
      std::shared_ptr<const ProductPlanPostcheckTextEdit> note;
      try{note=productSavePostcheckSourceText(job,edited,joined,sourcePanel,handle);}
      catch(const std::exception& error){trialError=error.what();break;}
      if(!note||std::none_of(search.colliding.begin(),search.colliding.end(),[&](const auto& collision){
       const auto& [panel,chain,segment]=collision;
       return panel<=job.layoutSourcePanels.size()&&job.layoutSourcePanels[panel-1]==sourcePanel&&
        std::find(note->cleared.begin(),note->cleared.end(),chain)!=note->cleared.end();
      }))break;
      edited=joined;
      joinedNote=note;
      handles.insert(note->handle);
      break;
     }
    }
    trial=productOpenPrintedInkCandidate(edited);
    auto proof=measure(*trial,edited,directory/L"attempt-extended",handles,true);
    if(productPrintedProofRetainsClearance(best,proof)&&proof.colliding.size()<best.colliding.size()){
     for(const auto& edit:textRepairs){
      const bcpproduct::PlanDrawingChecks::Key key{edit.panel,"source-text-clearance",edit.handle};
      proof.drawingChecks.items.insert_or_assign(key,best.drawingChecks.items.at(key));
     }
     if(joinedNote){
      const bcpproduct::PlanDrawingChecks::Key key{joinedNote->panel,"source-text-clearance",joinedNote->handle};
      auto check=best.drawingChecks.items.at(key);
      check.state=bcpproduct::PlanCheck::verified;
      check.basis=u8"原图文字随相撞尺寸联合试排，同版重开并实打";
      check.reason.clear();
      proof.drawingChecks.items.insert_or_assign(key,std::move(check));
      textRepairs.push_back({joinedNote->panel,joinedNote->handle,joinedNote->before,joinedNote->after,joinedNote->cleared});
     }
     productStatus(acDocManager->appContextCloseDocument(final),"close plan before accepted extended local repair");
     final=trial;trial=nullptr;
     candidate=edited;best=std::move(proof);job.planVerifiedCandidateHash=best.digest;
     ++accepted;
    }else{
     productStatus(acDocManager->appContextCloseDocument(trial),"discard unsuccessful extended local repair");
     trial=nullptr;
    }
   }
  }catch(const std::exception& error){
   if(trial)acDocManager->appContextCloseDocument(trial);
   trialError=error.what();
  }
 }
 const auto trialMilliseconds=GetTickCount64()-trialStart;
 const auto finalStart=GetTickCount64();
 auto finalProof=productMeasurePrintedInk(job,*final,candidate,directory/L"final",false,nullptr,best.extendedPanels,false);
 const auto finalMilliseconds=GetTickCount64()-finalStart;
 if(!productPrintedProofRetainsClearance(best,finalProof)){
  // Record discrepancies from the final full print in the delivery checks.
  trialError="final full-sheet review differs from trial coverage or clearance";
  finalProof.drawingChecks.record(0,"print-scope-reconciliation","pdf",{"P04","P06"},bcpproduct::PlanReleaseGate::files,
   bcpproduct::PlanCheck::failed,u8"最终整套实打与局部返修证据对账",u8"最终整套实打出现新增冲突或原有核验覆盖变化");
 }
 for(const auto& [key,check]:best.drawingChecks.items)
  if(std::get<1>(key)=="source-text-clearance"&&check.state==bcpproduct::PlanCheck::verified)
   finalProof.drawingChecks.items.insert_or_assign(key,check);
 best=std::move(finalProof);
 for(const auto& edit:textRepairs){
  const auto evidence=std::string("{\"sourcePanel\":")+std::to_string(edit.panel)+
    ",\"object\":\""+edit.handle+"\",\"drawingSHA256\":\""+best.digest+
    "\",\"savedReopened\":true,\"result\":\"verified-local-source-text-clearance\",\"before\":"+
    wholePoints({edit.before})+",\"after\":"+wholePoints({edit.after})+"}";
  productRecordRuleEvidence(job,"L06",evidence);
 }
 job.detailPending=best.detailPending;
 job.drawingChecks=best.drawingChecks;
 job.planPrintedPages=best.modelPlots;
 productDismissSupersededPaperInk(job,*final->database(),best);
 for(const auto& key:acceptedBaselines){
  const auto& [panel,handle,segment]=key;
  const auto stillOpen=[&](const auto& keys){
   return std::any_of(keys.begin(),keys.end(),[&](const auto& item){
    return std::get<0>(item)==panel&&std::get<1>(item)==handle;
   });
  };
  if(stillOpen(best.colliding)||stillOpen(best.missing))continue;
  const auto sourcePanel=job.layoutSourcePanels.at(panel-1);
  auto& check=job.drawingChecks.items.at({sourcePanel,"interior-baseline",handle});
  check.state=bcpproduct::PlanCheck::verified;
  check.basis=u8"独立重开真实原生基线、测点、测值与同版 PDF 净空";
  check.reason.clear();
  productRecordRuleEvidence(job,"D25","{\"object\":\""+handle+"\",\"sourcePanel\":"+std::to_string(sourcePanel)+
      ",\"drawingSHA256\":\""+best.digest+"\",\"savedReopened\":true,"
      "\"result\":\"verified-printed-interior-baseline-and-text\"}");
 }
 for(const auto& item:fs::directory_iterator(best.directory)){
  const auto file=item.path();
  if(file.extension()==L".pdf"||file.filename()==L"plan-print-review.json"||
     file.filename()==L"formal-layout-print-review.json")fs::copy_file(file,job.root/file.filename());
 }
 const auto openCount=best.missing.size()+best.colliding.size();
 const bool verified=openCount==0;
 const auto record=[&](const auto& issues, const std::string& reason){
  for(const auto& [panel,h,segment]:issues){
   const auto sourcePanel=panel<=job.layoutSourcePanels.size()?job.layoutSourcePanels[panel-1]:panel;
   job.drawingChecks.record(sourcePanel,"printed-dimension-ink",h+"/"+std::to_string(segment+1),{"D17","D23","QA10","P06"},
      bcpproduct::PlanReleaseGate::files,bcpproduct::PlanCheck::failed,u8"同一重开 DWG 的实际 PDF 线迹",reason);
  }
 };
 record(best.missing,u8"实打短尺寸或同链相邻数字未唯一对应原生文字夹点");
 record(best.colliding,u8"实打短尺寸或同链相邻数字与可见线迹或文字相交");
 job.drawingChecks.record(0,"printed-ink","pdf",{"D17","D23","P06"},bcpproduct::PlanReleaseGate::files,
    verified?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::failed,u8"实际 PDF 短尺寸及相邻数字逐段核验",
    verified?"":u8"实打尺寸净空尚有未闭合项");
 std::ostringstream out;
 out<<"{\"contract\":\"plan.printed-ink.v1\",\"drawingSHA256\":\""<<best.digest<<"\",\"verified\":"<<(verified?"true":"false")
    <<",\"checked\":"<<best.checked<<",\"expected\":"<<best.targets.size()<<",\"unresolved\":"<<openCount
    <<",\"acceptedRepairs\":"<<accepted<<",\"trialError\":"<<jsonUtf8(bcpproduct::decodeMessage(trialError));
 if(localPostcheck)out<<",\"printPasses\":"<<passes<<",\"printPassLimit\":"<<passLimit
    <<",\"initialUnresolved\":"<<initialProblems<<",\"sourceTextExamined\":"<<textExamined
    <<",\"acceptedSourceTextRepairs\":"<<textRepairs.size();
 out<<",\"pages\":[";
 bool first=true;
 for(const auto& [panel,page]:best.pages){
  if(!first)out<<',';first=false;
  out<<"{\"file\":"<<jsonUtf8(page.filename().wstring())<<",\"sha256\":\""<<sha(page)<<"\"}";
 }
 out<<"],\"trialSheetPrints\":"<<trialSheetPrints<<",\"scopeFallbacks\":"<<scopeFallbacks
    <<",\"finalSheetPrints\":"<<best.pages.size()<<",\"finalPrintPasses\":1,\"trialMilliseconds\":"
    <<trialMilliseconds<<",\"finalPrintMilliseconds\":"<<finalMilliseconds<<'}';
 require(sha(candidate)==best.digest,"formal DWG and plotted ink proof differ");
 writeNew(job.root/L"plan-printed-ink.json",out.str());
}
