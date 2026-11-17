// Write a native paper frame with the drawing's model title and sheet number.
bool productSheetRect(const AcDbPolyline& frame){
 if(!frame.isClosed()||frame.numVerts()!=4)return false;
 for(unsigned i=0;i<4;++i){
  AcGePoint2d a,b;
  if(frame.segType(i)!=AcDbPolyline::kLine||frame.getPointAt(i,a)!=Acad::eOk||
     frame.getPointAt((i+1)%4,b)!=Acad::eOk||
     ((std::abs(a.x-b.x)<1e-5)==(std::abs(a.y-b.y)<1e-5)))return false;
 }
 return true;
}

bool productSheetContains(const AcDbExtents& outer,const AcDbExtents& inner,double tolerance=1e-5){
 return outer.minPoint().x<=inner.minPoint().x+tolerance&&outer.maxPoint().x+tolerance>=inner.maxPoint().x&&
        outer.minPoint().y<=inner.minPoint().y+tolerance&&outer.maxPoint().y+tolerance>=inner.maxPoint().y;
}

std::wstring productSheetSourceTitle(const AcDbEntity& entity){
 if(const auto* text=AcDbText::cast(&entity))return text->textString();
 if(const auto* text=AcDbMText::cast(&entity)){
  AcString actual;
  if(text->text(actual)==Acad::eOk)return actual.kACharPtr();
 }
 std::vector<std::wstring> parts;
 productOpeningTexts(entity,parts);
 std::wstring title;
 for(const auto& part:parts)if(part.rfind(L"1:",0)!=0&&part.rfind(L"1：",0)!=0)title+=part;
 return title;
}

AcDbObjectId productFallbackSheetText(AcDbDatabase& db,AcDbObjectId space,AcDbObjectId layer,
 AcDbObjectId style,const AcGePoint3d& position,double height,const std::wstring& value,
 double right){
 auto text=std::make_unique<AcDbText>();
 text->setDatabaseDefaults(&db);
 if(!style.isNull())productStatus(text->setTextStyle(style),"set source-bound sheet font");
 productStatus(text->setLayer(layer),"set paper title layer");
 productStatus(text->setPosition(position),"place paper title");
 productStatus(text->setHeight(height),"set plotted title height");
 productStatus(text->setTextString(value.c_str()),"write actual sheet title");
 AcDbObjectId id;
 {
  auto owner=editObject<AcDbBlockTableRecord>(space);
  productStatus(owner->appendAcDbEntity(id,text.get()),"append sheet title");
  text.release()->close();
 }
 auto saved=editObject<AcDbText>(id);
 AcDbExtents ext;
 productStatus(saved->getGeomExtents(ext),"measure written sheet title");
 if(ext.maxPoint().x>right){
  const double width=ext.maxPoint().x-ext.minPoint().x;
  const double factor=(right-position.x)/width;
  require(factor>=.6&&factor<1.,"source drawing title cannot fit its paper cell");
  productStatus(saved->setWidthFactor(factor),"fit actual sheet title to cell");
 }
 return id;
}

AcDbObjectId productFallbackSheetFrame(AcDbDatabase& db,AcDbObjectId space,AcDbObjectId layer,
 double width,double height,double footer){
 AcDbObjectId def;
 {
  auto table=editObject<AcDbBlockTable>(db.blockTableId());
  auto record=std::make_unique<AcDbBlockTableRecord>();
  productStatus(record->setName(L"*U"),"name anonymous generic paper frame");
  productStatus(table->add(def,record.get()),"create paper frame definition");
  record.release()->close();
 }
 {
  auto record=editObject<AcDbBlockTableRecord>(def);
  auto border=std::make_unique<AcDbPolyline>(4);
  border->setDatabaseDefaults(&db);
  for(const auto& point:std::array<AcGePoint2d,4>{{{8,8},{width-8,8},{width-8,height-8},{8,height-8}}})
   productStatus(border->addVertexAt(border->numVerts(),point),"draw paper border corner");
  border->setClosed(true);
  productStatus(border->setLayer(layer),"set paper border layer");
  AcDbObjectId id;
  productStatus(record->appendAcDbEntity(id,border.get()),"write paper border");
  border.release()->close();
  const auto line=[&](const AcGePoint3d& a,const AcGePoint3d& b){
   auto value=std::make_unique<AcDbLine>(a,b);
   value->setDatabaseDefaults(&db);
   productStatus(value->setLayer(layer),"set paper footer grid layer");
   AcDbObjectId row;
   productStatus(record->appendAcDbEntity(row,value.get()),"write paper footer grid");
   value.release()->close();
  };
  line({8,footer,0},{width-8,footer,0});
  line({width-155,8,0},{width-155,footer,0});
  line({width-76,8,0},{width-76,footer,0});
 }
 auto frame=std::make_unique<AcDbBlockReference>(AcGePoint3d::kOrigin,def);
 frame->setDatabaseDefaults(&db);
 productStatus(frame->setLayer(layer),"set paper frame layer");
 AcDbObjectId id;
 auto paper=editObject<AcDbBlockTableRecord>(space);
 productStatus(paper->appendAcDbEntity(id,frame.get()),"append actual paper frame block");
 frame.release()->close();
 return id;
}

AcDbObjectId productFallbackModelFrameLayer(AcDbDatabase& db,const AcDbExtents& window,
 const AcDbExtents& sourceTitle){
 // Accept only a dedicated model-frame layer. Mixed or unreadable drawing
 // content remains visible and is audited by the ordinary clipping checks.
 AcDbObjectId layer;
 std::vector<AcDbExtents> frames,blocks;
 for(const auto& [handle,type]:modelTypes(db)){
  const auto entity=open<AcDbEntity>(find(db,ascii(handle)));
  if(symbolName(entity->layerId())!=L"PUB_TITLE"||!productPlanDimensionVisible(*entity))continue;
  AcDbExtents box;
  if(entity->getGeomExtents(box)!=Acad::eOk)return {};
  if(!productGenericBoxesOverlap(window,box,0.))continue;
  if(const auto* frame=AcDbPolyline::cast(entity.get());frame&&productSheetRect(*frame))frames.push_back(box);
  else if(AcDbBlockReference::cast(entity.get()))blocks.push_back(box);
  else return {};
  layer=entity->layerId();
 }
 if(layer.isNull()||std::none_of(frames.begin(),frames.end(),[&](const auto& frame){
  return productSheetContains(frame,sourceTitle);
 }))return {};
 for(const auto& block:blocks)if(std::none_of(frames.begin(),frames.end(),[&](const auto& frame){
  return productSheetContains(frame,block,30.)&&block.maxPoint().y-block.minPoint().y<
   (frame.maxPoint().y-frame.minPoint().y)*.45;
 }))return {};
 return layer;
}

void productWriteFallbackPlanSheet(ProductJob& job,AcDbDatabase& db,int panel,
 const std::array<double,4>& window,double paper,const std::string& sourceHandle){
 auto source=open<AcDbEntity>(find(db,sourceHandle));
 AcDbExtents sourceBounds;
 productStatus(source->getGeomExtents(sourceBounds),"source title bounds for paper layout");
 const auto title=productSheetSourceTitle(*source);
 const AcDbExtents modelWindow({window[0],window[1],0},{window[2],window[3],0});
 require(!title.empty()&&productPlanDimensionVisible(*source)&&productSheetContains(modelWindow,sourceBounds),
  "registered drawing title must be visible within the actual panel");
 std::vector<ProductRoomLeaf> leaves;
 productRoomLeaves(*source,leaves);
 const auto style=leaves.empty()?db.textstyle():leaves.front().style;
 source.reset();
 const double viewWidth=(window[2]-window[0])/paper,viewHeight=(window[3]-window[1])/paper;
 const double footer=31.;
 const auto media=std::array<std::array<double,2>,2>{{{594.,420.},{841.,594.}}};
 const auto fit=std::find_if(media.begin(),media.end(),[&](const auto& size){
  return viewWidth>0&&viewHeight>0&&viewWidth<=size[0]-24&&viewHeight<=size[1]-footer-18;
 });
 require(fit!=media.end(),"plan cannot fit an A1 paper viewport at the requested scale");
 const double width=(*fit)[0],height=(*fit)[1];
 const auto layer=productPlanLayer(db,L"PLAN-SHEET-FRAME",7);
 const auto viewportLayer=productPlanLayer(db,L"PLAN-SHEET-VIEWPORT",8,false);
 const auto hidden=productFallbackModelFrameLayer(db,modelWindow,sourceBounds);
 auto* manager=acdbHostApplicationServices()->layoutManager();
 require(manager,"native layout manager unavailable");
 const auto base=L"平面出图-"+std::to_wstring(panel);
 auto name=base;
 for(int serial=2;!manager->findLayoutNamed(name.c_str(),&db).isNull();++serial)
  name=base+L"-"+std::to_wstring(serial);
 AcString previous;
 productStatus(manager->getActiveLayoutName(previous,true,&db),"record layout before generating plan sheet");
 AcDbObjectId layout,space;
 productStatus(manager->createLayout(name.c_str(),layout,space,&db),"create fallback paper space");
 try{
  const auto frame=productFallbackSheetFrame(db,space,layer,width,height,footer);
  const auto titleId=productFallbackSheetText(db,space,layer,style,{14,15,0},5.,title,width-160);
  const auto number=L"建施-"+std::wstring(panel<10?L"0":L"")+std::to_wstring(panel);
  const auto numberId=productFallbackSheetText(db,space,layer,style,{width-70,15,0},3.5,number,width-13);
  std::wostringstream scale;
  scale.imbue(std::locale::classic());
  scale<<L"1:"<<job.unifiedScale;
  productFallbackSheetText(db,space,layer,style,{width-148,15,0},3.5,scale.str(),width-81);
  productStatus(manager->setCurrentLayout(name.c_str(),&db),"initialize fallback paper layout");
  const auto defaultViewports=open<AcDbLayout>(layout)->getViewportArray();
  for(const auto& id:defaultViewports){
   auto defaults=editObject<AcDbViewport>(id);
   if(defaults->number()!=1)productStatus(defaults->erase(),"remove automatic viewport from new paper layout");
  }
  AcDbObjectId viewportId;
  {
   auto viewport=std::make_unique<AcDbViewport>();
   viewport->setDatabaseDefaults(&db);
   productStatus(viewport->setLayer(viewportLayer),"set nonprinting viewport layer");
   auto owner=editObject<AcDbBlockTableRecord>(space);
   productStatus(owner->appendAcDbEntity(viewportId,viewport.get()),"write actual model viewport");
   viewport.release()->close();
  }
  {
   auto view=editObject<AcDbViewport>(viewportId);
   const AcGePoint3d centre((window[0]+window[2])*.5,(window[1]+window[3])*.5,0);
   productStatus(view->setCenterPoint({width*.5,(height+footer)*.5,0}),"center fallback paper viewport");
   productStatus(view->setWidth(viewWidth),"fit source view width at drawing scale");
   productStatus(view->setHeight(viewHeight),"fit source view height at drawing scale");
   productStatus(view->setViewDirection(AcGeVector3d::kZAxis),"use top plan view");
   productStatus(view->setPerspectiveOff(),"use orthographic plan view");
   productStatus(view->setViewTarget(centre),"register paper viewport to original drawing");
   productStatus(view->setViewCenter({0,0}),"center model view on the target");
   productStatus(view->setOn(),"enable actual paper viewport");
   productStatus(view->setViewHeight(viewHeight*paper),"set exact model window height");
   productStatus(view->setCustomScale(1./paper),"set exact drawing scale");
   if(!hidden.isNull()){
    AcDbObjectIdArray layers;
    layers.append(hidden);
    productStatus(view->freezeLayersInViewport(layers),"suppress verified old frame only in the fallback paper viewport");
   }
   productStatus(view->setLocked(),"lock fallback plan viewport");
  }
  productStatus(manager->setCurrentLayout(previous.kACharPtr(),&db),"restore model layout after frame generation");
  {
   auto settings=editObject<AcDbLayout>(layout);
   auto* validator=acdbHostApplicationServices()->plotSettingsValidator();
   require(validator,"paper settings validator unavailable");
   validator->refreshLists(settings.get());
   productStatus(validator->setPlotCfgName(settings.get(),L"DWG To PDF.pc3"),"fallback layout plotter");
   validator->refreshLists(settings.get());
   productSelectPlanPaper(*settings,*validator,width,height,1.);
   productStatus(validator->setPlotPaperUnits(settings.get(),AcDbPlotSettings::kMillimeters),"fallback layout millimetres");
   productStatus(validator->setPlotWindowArea(settings.get(),0,0,width,height),"fallback whole-sheet window");
   productStatus(validator->setPlotType(settings.get(),AcDbPlotSettings::kWindow),"fallback sheet print area");
   productStatus(validator->setUseStandardScale(settings.get(),false),"fallback explicit paper scale");
   productStatus(validator->setCustomPrintScale(settings.get(),1.,1.),"fallback one-to-one paper scale");
   productStatus(validator->setPlotCentered(settings.get(),true),"fallback centered paper plot");
   if(!job.planPlotStyle.empty()){
    productStatus(validator->setCurrentStyleSheet(settings.get(),job.planPlotStyle.c_str()),"fallback plot style");
    settings->setPlotPlotStyles(true);
   }
  }
  job.planSheetLayouts.emplace(panel,ProductJob::PlanSheetLayout{name,axisHandle(titleId),axisHandle(frame),
   axisHandle(viewportId),1./paper,sourceHandle,width,height});
  job.planSheetNumbers.emplace(panel,std::make_pair(axisHandle(numberId),number));
 }catch(...){
  manager->setCurrentLayout(previous.kACharPtr(),&db);
  productStatus(manager->deleteLayout(name.c_str(),&db),"discard incomplete fallback sheet");
  throw;
 }
}

void productWriteFallbackPlanSheets(ProductJob& job,AcDbDatabase& db,const GenericNativeInput& input,
 const std::vector<bcpproduct::GenericView>& views,const ProductPlanRoof& roof,double paper){
 if(!job.architecturalPlanOnly||!job.formalPlan||!job.planPrintReview)return;
 std::set<int> explicitSheets;
 if(!job.unifiedSheetChecks.empty()){
  std::ifstream in(job.unifiedSheetChecks);
  std::string kind,digest;
  require(bool(in>>kind>>digest)&&kind=="PLANSHEETS1"&&digest==job.sourceHash,"read explicit sheet file for paper fallback");
  std::string record;
  std::getline(in,record);
  while(std::getline(in,record)){
   std::istringstream row(record);
   if(!(row>>kind))continue;
   if(kind=="END")break;
   if(kind=="FRAME"||kind=="SHEET"||kind=="ARCHIVESHEET"){
    int panel{};
    require(bool(row>>panel)&&panel>0,"explicit paper sheet panel");
    explicitSheets.insert(panel);
   }
  }
 }
 for(std::size_t i=0;i<job.planPrintWindows.size();++i){
  const auto panel=static_cast<int>(i+1);
  if(job.planSheetLayouts.count(panel)||explicitSheets.count(panel))continue;
  try{
   std::string source;
   const auto& window=job.planPrintWindows[i];
   if(i<job.layoutSourcePanels.size()){
    const auto index=job.layoutSourcePanels[i]-1;
    const AcDbExtents bounds({window[0],window[1],0},{window[2],window[3],0});
    std::set<std::string> titles;
    for(const auto* group:{&input.titles,&input.notes})for(const auto& name:*group){
     if(!bcpproduct::explicitPlanFloor(name.text)||productRoofTitle(name.text)||
        bcpproduct::nearestGenericView(views,{name.anchor.x,name.anchor.y})!=index)continue;
     AcDbObjectId id;
     if(db.getAcDbObjectId(id,false,AcDbHandle(bcpproduct::decodeMessage(name.handle).c_str()))!=Acad::eOk||id.isErased())continue;
     const auto entity=open<AcDbEntity>(id);
     AcDbExtents titleBox;
     if(productPlanDimensionVisible(*entity)&&entity->getGeomExtents(titleBox)==Acad::eOk&&
        productSheetContains(bounds,titleBox))titles.insert(name.handle);
    }
    if(titles.size()==1)source=*titles.begin();
   }else if(i==job.layoutSourcePanels.size()){
    source=roof.existingTitle;
    if(source.empty())for(const auto& item:roof.items)if(item.kind=="TITLE")source=item.handle;
   }
   require(!source.empty(),"the model panel has no uniquely readable drawing title");
   productWriteFallbackPlanSheet(job,db,panel,window,paper,source);
  }catch(const std::exception& error){
   job.detailPending.push_back({u8"打印图幅",std::to_string(panel),error.what()});
  }
 }
}

bool productVerifyFallbackPlanSheet(const ProductJob& job,AcDbDatabase& db,int panel,
 const ProductJob::PlanSheetLayout& sheet){
 const auto marked=job.planSheetNumbers.find(panel);
 if(sheet.sourceTitle.empty()||marked==job.planSheetNumbers.end()||panel<=0||
    static_cast<std::size_t>(panel)>job.planPrintWindows.size())return false;
 const auto layout=acdbHostApplicationServices()->layoutManager()->findLayoutNamed(sheet.name.c_str(),&db);
 if(layout.isNull())return false;
 const auto layoutRecord=open<AcDbLayout>(layout);
 const auto space=layoutRecord->getBlockTableRecordId();
 const auto frame=open<AcDbBlockReference>(find(db,sheet.block));
 const auto title=open<AcDbText>(find(db,sheet.title));
 const auto number=open<AcDbText>(find(db,marked->second.first));
 const auto view=open<AcDbViewport>(find(db,sheet.viewport));
 const auto source=open<AcDbEntity>(find(db,sheet.sourceTitle));
 const auto& w=job.planPrintWindows[static_cast<std::size_t>(panel-1)];
 const AcDbExtents paper({0,0,0},{sheet.paperWidth,sheet.paperHeight,0});
 AcDbExtents frameInk,titleInk,numberInk;
 const double scale=view->customScale();
 const auto centre=view->centerPoint();
 const AcGeVector3d half(view->width()*.5,view->height()*.5,0);
 const AcDbExtents viewportPaper(centre-half,centre+half);
 const AcDbExtents actualViewportArea({8,31,0},{sheet.paperWidth-8,sheet.paperHeight-8,0});
 const auto viewports=layoutRecord->getViewportArray();
 double width{},height{},numerator{},denominator{};
 if(layoutRecord->getPlotPaperSize(width,height)!=Acad::eOk||
    layoutRecord->getCustomPrintScale(numerator,denominator)!=Acad::eOk)return false;
 return frame->ownerId()==space&&title->ownerId()==space&&number->ownerId()==space&&view->ownerId()==space&&
  viewports.length()==2&&viewports.contains(view->objectId())&&layoutRecord->plotPaperUnits()==AcDbPlotSettings::kMillimeters&&
  layoutRecord->plotType()==AcDbPlotSettings::kWindow&&!layoutRecord->useStandardScale()&&
  denominator>0&&std::abs(numerator/denominator-1.)<1e-9&&
  std::abs(std::max(width,height)-sheet.paperWidth)<.5&&std::abs(std::min(width,height)-sheet.paperHeight)<.5&&
  *title->textString()&&productSheetSourceTitle(*source)==title->textString()&&marked->second.second==number->textString()&&
  productPlanDimensionVisible(*frame)&&productPlanDimensionVisible(*title)&&productPlanDimensionVisible(*number)&&
  frame->getGeomExtents(frameInk)==Acad::eOk&&title->getGeomExtents(titleInk)==Acad::eOk&&
  number->getGeomExtents(numberInk)==Acad::eOk&&productSheetContains(paper,frameInk)&&
  productSheetContains(frameInk,titleInk)&&productSheetContains(frameInk,numberInk)&&
  !productGenericBoxesOverlap(titleInk,numberInk,0)&&view->isOn()&&view->number()!=1&&view->isLocked()&&
  productSheetContains(actualViewportArea,viewportPaper)&&!view->isPerspectiveOn()&&!view->isNonRectClipOn()&&
  scale>0&&std::abs(scale-sheet.scale)<1e-9&&std::abs(view->twistAngle())<1e-8&&
  view->viewDirection().isEqualTo(AcGeVector3d::kZAxis)&&view->viewCenter().distanceTo(AcGePoint2d::kOrigin)<1e-5&&
  view->viewTarget().distanceTo(AcGePoint3d((w[0]+w[2])*.5,(w[1]+w[3])*.5,0))<1e-5&&
  std::abs(view->width()/scale-(w[2]-w[0]))<1e-5&&std::abs(view->height()/scale-(w[3]-w[1]))<1e-5;
}
