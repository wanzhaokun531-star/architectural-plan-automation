// Owned regression fixtures only; this is never dispatched by a drawing job.
// Include dbtable.h at global scope before including the product sources.
void productWriteFinalRegressionFixture(const fs::path& source,const fs::path& output,
 const fs::path& report,const fs::path& resources,const fs::path& sheetFile){
 isolated(ini(iniPath(),L"nonce"));
 require(curDoc()&&fs::equivalent(source,curDoc()->fileName())&&!fs::exists(output)&&!bcpLoaded(),
  "final regression fixture requires the owned input and a fresh destination");
 const auto sourceHash=sha(source);AcDbDatabase db(false,true);readDrawing(db,source);Working working(&db);
 ProductJob job;job.root=report;job.profile=resources;job.sourceHash=sourceHash;
 job.currentPlanRules=true;job.architecturalPlanOnly=true;job.unifiedPlan=true;
 auto sheets=readJointPaperJob(source,sheetFile,output,report);jointPaperSheets(job,db,sheets);
 const auto scope=productPrepareFinalPlanOmissions(job,db);
 require(scope.error.empty()&&!scope.windows.empty(),"fixture needs reliable adopted plan windows");
 productCleanFinalPlanOmissions(job,db,scope);
 std::optional<AcGePoint3d> inside;
 for(std::size_t v=0;v<scope.views.size()&&!inside;++v)if(scope.active[v]&&scope.grids[v]){
  const auto& grid=*scope.grids[v];
  for(std::size_t y=0;y<grid.height()&&!inside;++y)for(std::size_t x=0;x<grid.width();++x){
   const auto point=grid.centre(x,y);const auto region=grid.at(point);
   if(region<0||scope.exterior[v].count(region)||grid.xs[x+1]-grid.xs[x]<1800||grid.ys[y+1]-grid.ys[y]<1800)continue;
   if(productFinalOmissionInScope(scope,point)){inside=point;break;}
  }
 }
 require(inside.has_value(),"fixture requires an actual closed interior cell of at least 1800 by 1800");
 const auto policy=productCurrentTypographyPolicy(job);
 const auto textStyle=productRestoreTypographyFont(db,productCurrentFont(policy.at("FY_HZ")),0);
 const auto makeText=[&](const std::wstring& value,const AcGePoint3d& point,double height=400.){
  auto text=std::make_unique<AcDbText>();text->setDatabaseDefaults(&db);
  status(text->setTextStyle(textStyle),"fixture text style");status(text->setHeight(height),"fixture text height");
  status(text->setWidthFactor(.7),"fixture text width");status(text->setPosition(point),"fixture text position");
  status(text->setTextString(value.c_str()),"fixture exact text content");return productAppendPlanDetail(db,std::move(text));
 };
 // Inject after the early cleanup: final enumeration must discover these new IDs.
 const auto roomArea=makeText(L"12.34㎡",*inside+AcGeVector3d(-500,-600,0));
 const auto ordinary=productAppendPlanDetail(db,std::make_unique<AcDbRotatedDimension>(0.,
  *inside+AcGeVector3d(-600,-300,0),*inside+AcGeVector3d(600,-300,0),*inside+AcGeVector3d(0,-100,0),nullptr,db.dimstyle()));
 AcDbDatabase donor(false,true);readDrawing(donor,resources/L"annotation-template/平面标注.dwg");AcDbObjectId nativeDonor;
 for(const auto& [h,type]:modelTypes(donor))if(type==L"TDbDimension2"){
  const auto id=find(donor,ascii(h));if(chainValues(id).size()==1){nativeDonor=id;break;}
 }
 require(!nativeDonor.isNull(),"fixture needs a signed native one-segment donor");
 const auto material=productTemplateClone(donor,nativeDonor,db);
 const auto native=productDetailChain(db,material,{*inside+AcGeVector3d(-600,100,0),*inside+AcGeVector3d(600,100,0)},200.);
 std::vector<std::string> exploded;
 // Already-exploded source expression, with suppressed arrows: three actual
 // line entities and numeric text bound to two real source witness points.
 // Never recursively explode a transient Tianzheng dimension view.
 std::vector<AcGePoint3d> looseStations;const auto witnesses=productPerimeterSourceWitnesses(db,job);
 for(const auto& a:witnesses){
  for(const auto& b:witnesses){
   const auto width=b.point.x-a.point.x;
   if(width<1000||width>8000||std::abs(a.point.y-b.point.y)>.01||!productFinalOmissionInScope(scope,a.point)||!productFinalOmissionInScope(scope,b.point))continue;
   const auto v=bcpproduct::nearestGenericView(scope.views,{a.point.x,a.point.y});
   if(scope.active[v]&&productInteriorStations({a.point,b.point},scope.input,scope.views[v],scope.grids[v],job.unifiedUnitMM,scope.exterior[v])){
    looseStations={a.point,b.point};break;
   }
  }
  if(!looseStations.empty())break;
 }
 require(looseStations.size()==2,"fixture needs two real interior dimension witness points");
 const auto a=looseStations[0],b=looseStations[1];const double baseline=a.y+700.;
 const auto addLine=[&](const AcGePoint3d& p,const AcGePoint3d& q){
  exploded.push_back(axisHandle(productAppendPlanDetail(db,std::make_unique<AcDbLine>(p,q))));
 };
 addLine({a.x,baseline,0},{b.x,baseline,0});addLine(a,{a.x,baseline+100,0});addLine(b,{b.x,baseline+100,0});
 std::wostringstream number;number<<std::fixed<<std::setprecision(2)<<(b.x-a.x);
 const auto looseText=makeText(number.str(),{(a.x+b.x)*.5,baseline+80,0},280.);
 {auto label=editObject<AcDbText>(looseText);label->setHorizontalMode(AcDb::kTextCenter);
  status(label->setAlignmentPoint({(a.x+b.x)*.5,baseline+80,0}),"centre actual loose dimension text");}
 exploded.push_back(axisHandle(looseText));
 status(editObject<AcDbEntity>(material)->erase(),"retire fixture donor material");
 require(!exploded.empty(),"fixture contains actual loose dimension strokes and text");
 const auto& window=scope.windows.front();const double x=window.xmin+1000,y=window.ymin+300;
 std::vector<std::string> glyphs;
 for(const auto& row:std::vector<std::pair<std::wstring,double>>{{L"㎡",0},{L"m²",1000},{L"m",2000}})
  glyphs.push_back(axisHandle(makeText(row.first,{x+row.second,window.ymin+1600,0})));
 glyphs.push_back(axisHandle(makeText(L"2",{x+2250,window.ymin+1800,0},200.)));
 // A true 2x2 table is a table even without three-by-three visible grid lines.
 auto table=std::make_unique<AcDbTable>();table->setDatabaseDefaults(&db);
 status(table->setSize(2,2),"fixture native table size");status(table->setColumnWidth(2200),"fixture native column width");
 status(table->setRowHeight(900),"fixture native row height");status(table->setPosition({x,y,0}),"fixture partly cropped native table");
 for(int r=0;r<2;++r)for(int c=0;c<2;++c){status(table->setTextStyle(r,c,textStyle),"fixture table font");
  status(table->setTextHeight(r,c,400),"fixture table character height");}
 status(table->setTextString(0,0,L"建筑面积（㎡）"),"fixture native table header");
 status(table->setTextString(1,0,L"123.45"),"fixture native area value");
 status(table->setTextString(1,1,L"尾行"),"fixture native tail marker");status(table->generateLayout(),"fixture table layout");
 const auto nativeTable=productAppendPlanDetail(db,std::move(table));
 // A separate existing AcDbGroup of lines/text tests grouped exploded tables.
 std::vector<AcDbObjectId> groupMembers;const double gx=x+5200;
 for(int r=0;r<3;++r)groupMembers.push_back(productAppendPlanDetail(db,std::make_unique<AcDbLine>(AcGePoint3d(gx,y-900*r,0),AcGePoint3d(gx+4400,y-900*r,0))));
 for(int c=0;c<3;++c)groupMembers.push_back(productAppendPlanDetail(db,std::make_unique<AcDbLine>(AcGePoint3d(gx+2200*c,y,0),AcGePoint3d(gx+2200*c,y-1800,0))));
 groupMembers.push_back(makeText(L"技术指标",{gx+100,y-500,0}));groupMembers.push_back(makeText(L"123.45",{gx+100,y-1500,0}));
 auto group=std::make_unique<AcDbGroup>(L"FINAL_TABLE_FIXTURE",true);AcDbObjectId groupId;
 {auto dictionary=editObject<AcDbDictionary>(db.groupDictionaryId());status(dictionary->setAt(L"FINAL_TABLE_FIXTURE",group.get(),groupId),"fixture table group");}
 std::unique_ptr<AcDbGroup,Close<AcDbGroup>> managed(group.release());
 for(const auto id:groupMembers)status(managed->append(id),"fixture exact existing table members");managed.reset();
 status(db.saveAs(output.c_str(),false,AcDb::kDHL_CURRENT),"save final regression fixture");
 AcDbDatabase saved(false,true);readDrawing(saved,output);
 const auto residuals=productAuditFinalPlanOmissions(job,saved,scope,output,report);
 productCapturePlanTables(job,saved,scope.views);productAuditRiskGlyphs(job,saved,output,report);
 require(sha(source)==sourceHash,"fixture modified saved source");
 std::ostringstream out;out<<"{\"completed\":true,\"reopened\":true,\"sourceUnchanged\":true,\"outputSHA256\":\""<<sha(output)
  <<"\",\"injectedAfterEarlyCleanup\":true,\"roomArea\":\""<<axisHandle(roomArea)<<"\",\"ordinaryDimension\":\""<<axisHandle(ordinary)
  <<"\",\"nativeDimension\":\""<<axisHandle(native)<<"\",\"nativeTable\":\""<<axisHandle(nativeTable)<<"\",\"groupTable\":\""<<axisHandle(groupId)
  <<"\",\"detectedTables\":"<<job.planTables.size()<<",\"omissionIssues\":"<<residuals.size()<<",\"exploded\":[";
 bool first=true;for(const auto& h:exploded){if(!first)out<<',';first=false;out<<'"'<<h<<'"';}out<<"],\"glyphObjects\":[";first=true;
 for(const auto& h:glyphs){if(!first)out<<',';first=false;out<<'"'<<h<<'"';}out<<"]}";
 writeNew(report/L"plan-native-result.json",out.str());
 writeNew(report/L"product-result.json",out.str());
}

void productProbeTableVisibility(const fs::path& source,const fs::path& report,
 const fs::path& resources,const fs::path& sheetFile){
 isolated(ini(iniPath(),L"nonce"));
 require(curDoc()&&fs::equivalent(source,curDoc()->fileName())&&!bcpLoaded(),"table probe requires owned source");
 const auto original=sha(source);AcDbDatabase db(false,true);readDrawing(db,source);Working working(&db);
 ProductJob job;job.root=report;job.profile=resources;job.sourceHash=original;job.currentPlanRules=true;job.unifiedPlan=true;
 const auto sheets=readJointPaperJob(source,sheetFile,report/L"unused.dwg",report);jointPaperSheets(job,db,sheets);
 const auto scope=productPrepareFinalPlanOmissions(job,db);require(scope.error.empty(),"table probe real plan scope");
 productCapturePlanTables(job,db,scope.views);
 const auto found=std::find_if(job.planTables.begin(),job.planTables.end(),[](const auto& table){return table.kind=="confirmed-existing-table-group";});
 require(found!=job.planTables.end(),"table probe requires a confirmed existing grouped table");
 const auto table=*found;const auto id=find(db,table.members.front());const auto originalLayer=open<AcDbEntity>(id)->layerId();
 const auto layer=productPlanLayer(db,L"FINAL_TABLE_VISIBILITY_TEST",7);
 const auto vp=find(db,job.planSheetLayouts.begin()->second.viewport);
 status(editObject<AcDbEntity>(id)->setLayer(layer),"isolate table probe member layer");
 require(productTableMembersPrintable(db,table,vp),"baseline table members must be printable");
 const auto stillCaptured=[&](){productCapturePlanTables(job,db,scope.views);return std::any_of(job.planTables.begin(),job.planTables.end(),[&](const auto& row){return row.kind==table.kind&&row.members==table.members;});};
 editObject<AcDbLayerTableRecord>(layer)->setIsOff(true);
 const bool offRejected=!productTableMembersPrintable(db,table,vp)&&stillCaptured();
 editObject<AcDbLayerTableRecord>(layer)->setIsOff(false);
 status(editObject<AcDbLayerTableRecord>(layer)->setIsPlottable(false),"table nonplot negative case");
 const bool nonplotRejected=!productTableMembersPrintable(db,table,vp)&&stillCaptured();
 status(editObject<AcDbLayerTableRecord>(layer)->setIsPlottable(true),"restore plottability probe");
 AcDbObjectIdArray layers;layers.append(layer);
 status(editObject<AcDbViewport>(vp)->freezeLayersInViewport(layers),"table viewport-freeze negative case");
 const bool frozenRejected=!productTableMembersPrintable(db,table,vp)&&stillCaptured();
 status(editObject<AcDbViewport>(vp)->thawLayersInViewport(layers),"restore viewport-freeze probe");
 status(editObject<AcDbEntity>(id)->setLayer(originalLayer),"restore actual table member layer");
 require(offRejected&&nonplotRejected&&frozenRejected&&productTableMembersPrintable(db,table,vp),"hidden table member escaped actual readback");
 require(sha(source)==original&&!fs::exists(report/L"unused.dwg"),"read-only table probe changed saved drawing");
 const auto result="{\"completed\":true,\"readOnly\":true,\"sourceSHA256\":\""+original+
  "\",\"existingGroupStillCaptured\":true,\"offLayerRejected\":true,\"nonplotLayerRejected\":true,\"viewportFrozenLayerRejected\":true,\"sourceUnchanged\":true,\"pdfPrinted\":false}";
 writeNew(report/L"plan-table-visibility-probe.json",result);writeNew(report/L"product-result.json",result);
}
