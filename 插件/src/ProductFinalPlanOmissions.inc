// The final pass scans the current database, including objects created after
// the source cleanup. It never treats a list of erased handles as full coverage.
struct ProductFinalOmissionIssue {std::string object,role,reason;};
struct ProductFinalOmissionContext {
 GenericNativeInput input;
 std::vector<bcpproduct::GenericView> views;
 std::vector<bool> active;
 std::vector<std::optional<ProductRoomGrid>> grids;
 std::vector<std::set<int>> exterior;
 std::vector<ProductPlanBounds> windows;
 std::vector<ProductFinalOmissionIssue> sourceCandidates;
 std::set<std::string> cleanupScope;
 std::string error;
};
bool productFinalOmissionInScope(const ProductFinalOmissionContext& scope,const AcGePoint3d& point){
 return std::any_of(scope.windows.begin(),scope.windows.end(),[&](const auto& window){
  return point.x>=window.xmin&&point.x<=window.xmax&&point.y>=window.ymin&&point.y<=window.ymax;
 });
}
struct ProductFinalOmissionInventory {
 std::vector<ProductSourceAnnotation> rows;
 std::vector<ProductLooseDimensionStroke> strokes;
 std::set<std::string> paperPaths;
 std::size_t layouts{};
};

struct ProductFinalOmissionExclusion {
 std::string root,category,reason;
 std::wstring type;
 std::set<std::string> readPaths,unreadPaths;
 bool coverageKnown{};
};

bool productFinalOmissionBoundsInScope(const ProductFinalOmissionContext& scope,const AcDbExtents& box){
 return std::any_of(scope.windows.begin(),scope.windows.end(),[&](const auto& window){
  return box.maxPoint().x>=window.xmin&&box.minPoint().x<=window.xmax&&
   box.maxPoint().y>=window.ymin&&box.minPoint().y<=window.ymax;
 });
}

std::vector<ProductFinalOmissionExclusion> productFinalOmissionExclusions(ProductJob& job,AcDbDatabase& db,
 const ProductFinalOmissionContext& scope,const ProductFinalOmissionInventory& inventory){
 std::map<std::pair<std::string,std::string>,ProductFinalOmissionExclusion> records;
 const auto record=[&](const std::string& root,const std::string& category,const std::wstring& type,
  const std::string& reason)->ProductFinalOmissionExclusion&{
  auto& row=records[{root,category}];row.root=root;row.category=category;row.type=type;row.reason=reason;return row;
 };
 const auto protectedPath=[&](const std::string& path){
  std::size_t start=0;while(start<path.size()){const auto end=path.find('/',start);
   if(job.protectedFurnitureMembers.count(path.substr(start,end==std::string::npos?end:end-start)))return true;
   if(end==std::string::npos)break;start=end+1;}return false;
 };
 std::map<std::string,std::vector<const ProductSourceAnnotation*>> rowsByRoot;
 std::set<std::string> protectedRoots,strokeRoots;
 for(const auto& row:inventory.rows){rowsByRoot[row.root].push_back(&row);if(protectedPath(row.path))protectedRoots.insert(row.root);}
 for(const auto& stroke:inventory.strokes){const auto root=stroke.path.substr(0,stroke.path.find('/'));
  strokeRoots.insert(root);if(protectedPath(stroke.path))protectedRoots.insert(root);}
 // Furniture preservation is a write constraint, not evidence that every
 // contour in the preserved instance has been decoded as annotation content.
 for(const auto& [wide,type]:modelTypes(db)){
  const auto root=ascii(wide);const auto entity=open<AcDbEntity>(find(db,root));
  if(!productPlanDimensionVisible(*entity)||!open<AcDbLayerTableRecord>(entity->layerId())->isPlottable())continue;
  AcDbExtents box;const bool bounded=entity->getGeomExtents(box)==Acad::eOk;
  if(bounded&&!productFinalOmissionBoundsInScope(scope,box))continue;
  const bool protectedDomain=job.protectedFurnitureMembers.count(root)||protectedRoots.count(root);
  if(protectedDomain){
   auto& excluded=record(root,"protected-furniture",type,u8"家具原实例及共享定义禁止改写；已读标注继续只读复核，未识别线轮廓不冒称没有室内尺寸或面积");
   excluded.coverageKnown=true;
   for(const auto* row:rowsByRoot[root])if(row->visible){
    excluded.readPaths.insert(row->path);
    if(row->kind.rfind("unreadable-",0)==0){excluded.unreadPaths.insert(row->path);excluded.coverageKnown=false;}
   }
   if(strokeRoots.count(root)){
    excluded.unreadPaths.insert(root+"/contour-only-geometry");excluded.coverageKnown=false;
   }
   if(!bounded||excluded.readPaths.empty()){
    excluded.unreadPaths.insert(root+"/annotation-content-unestablished");excluded.coverageKnown=false;
   }
  }
 }
 for(const auto& row:inventory.rows)if(row.visible&&row.kind.rfind("unreadable-",0)==0){
  if(row.bounds&&!inventory.paperPaths.count(row.path)&&!productFinalOmissionBoundsInScope(scope,*row.bounds))continue;
  auto& excluded=record(row.root,"unreadable-container-or-annotation",row.type,u8"容器、标注或代理不能完整读取；没有删除或修改原对象");
  excluded.unreadPaths.insert(row.path);
 }
 // Inventory does not classify arbitrary native/proxy expressions. Walk their
 // identities only; no explode, OCR or guessed reconstruction is performed.
 const std::set<std::wstring> knownBuilding{L"TDbWall",L"TDbColumn",L"TDbOpening",L"TDbGroupOpening",L"TDbStair",
  L"TDbMultiStair",L"TDbRectStair",L"TDbArcStair",L"TDbLineStair",L"TDbAxisLabelSet",L"TDbSymbElevation",L"TDbSlopeRoof"};
 std::set<AcDbObjectId> active;
 const auto walk=[&](auto&& self,AcDbObjectId id,const std::string& root,const std::string& path,
  const AcGeMatrix3d& world,std::size_t depth)->void{
  const auto entity=open<AcDbEntity>(id);
  if(!productPlanDimensionVisible(*entity)||!open<AcDbLayerTableRecord>(entity->layerId())->isPlottable())return;
  AcDbExtents box;if(entity->getGeomExtents(box)==Acad::eOk){box.transformBy(world);if(!productFinalOmissionBoundsInScope(scope,box))return;}
  const std::wstring type=entity->isA()->name();
  if(const auto* block=AcDbBlockReference::cast(entity.get())){
   const auto definition=block->blockTableRecord();const auto table=open<AcDbBlockTableRecord>(definition);
   AcDbBlockTableRecordIterator* raw{};
   if(depth>=20||table->isUnloaded()||active.count(definition)||table->newIterator(raw)!=Acad::eOk){
    record(root,"unreadable-container-or-annotation",type,u8"可见嵌套容器没有完整遍历，保留原实例").unreadPaths.insert(path);return;
   }
   active.insert(definition);std::unique_ptr<AcDbBlockTableRecordIterator> iterator(raw);
   for(;!iterator->done();iterator->step()){AcDbObjectId child;if(iterator->getEntityId(child)==Acad::eOk)
    self(self,child,root,path+"/"+axisHandle(child),world*block->blockTransform(),depth+1);}
   active.erase(definition);return;
  }
  if((entity->isAProxy()||type.rfind(L"AcDb",0)!=0)&&productOldAnnotationKind(*entity,type).empty()&&!knownBuilding.count(type))
   record(root,"unknown-native-expression",type,u8"可见原生或代理表达没有标注内容解码依据，未将未读域计为全覆盖").unreadPaths.insert(path);
 };
 for(const auto& [wide,type]:modelTypes(db)){const auto root=ascii(wide);try{
  walk(walk,find(db,root),root,root,AcGeMatrix3d::kIdentity,0);
 }catch(const std::exception& e){record(root,"unreadable-container-or-annotation",type,e.what()).unreadPaths.insert(root);active.clear();}}
 std::vector<ProductFinalOmissionExclusion> result;for(auto& pair:records)result.push_back(std::move(pair.second));return result;
}

ProductFinalOmissionInventory productFinalOmissionInventory(AcDbDatabase& db){
 ProductFinalOmissionInventory result;
 result.rows=productReadOldAnnotations(db,&result.strokes);
 const auto dictionary=open<AcDbDictionary>(db.layoutDictionaryId());
 std::unique_ptr<AcDbDictionaryIterator> it(dictionary->newIterator());
 for(;!it->done();it->next()){
  const auto layout=open<AcDbLayout>(it->objectId());if(layout->modelType())continue;
  ++result.layouts;
  auto rows=productReadOldAnnotations(db,nullptr,layout->getBlockTableRecordId());
  for(const auto& row:rows)result.paperPaths.insert(row.path);
  result.rows.insert(result.rows.end(),rows.begin(),rows.end());
 }
 return result;
}

bool productFinalOmissionWorld(AcDbDatabase& db,const std::string& path,AcGeMatrix3d& world){
 std::size_t start=0;
 for(auto slash=path.find('/');slash!=std::string::npos;slash=path.find('/',start)){
  const auto handle=path.substr(start,slash-start);start=slash+1;
  const auto entity=open<AcDbEntity>(find(db,handle));
  const auto* ref=AcDbBlockReference::cast(entity.get());if(!ref)return false;
  world=world*ref->blockTransform();
 }
 return true;
}

std::vector<ProductFinalOmissionIssue> productScanFinalPlanOmissions(ProductJob& job,AcDbDatabase& db,
 const ProductFinalOmissionContext& scope,const ProductFinalOmissionInventory& inventory,std::size_t* evaluated=nullptr){
 std::vector<ProductFinalOmissionIssue> result;
 const auto add=[&](const std::string& object,const std::string& role,const std::string& reason){
  if(std::none_of(result.begin(),result.end(),[&](const auto& r){return r.object==object&&r.role==role;}))
   result.push_back({object,role,reason});
 };
 if(!scope.error.empty()){add("whole-drawing","omission-coverage",scope.error);return result;}
 if(scope.views.empty()){add("whole-drawing","omission-coverage",u8"没有可核房间净域，不能判定室内标注零残留");return result;}
 if(std::none_of(scope.active.begin(),scope.active.end(),[](bool selected){return selected;}))
  add("whole-drawing","omission-coverage",u8"实际出图窗口未覆盖任何已识别平面，不能按空扫描认定禁用标注为零");
 for(std::size_t v=0;v<scope.views.size();++v)if(scope.active[v]&&!scope.grids[v])
  add("panel/"+std::to_string(v+1),"omission-coverage",u8"本幅缺少可靠闭合净域，室内标注覆盖未知，保留无法确定用途的对象并列清单");
 std::set<std::string> pairedAreas;
 std::set<std::string> pairedPaths;
 for(const auto& body:inventory.rows){
  if(body.root==body.handle||!body.bounds||body.glyphs.size()!=1||
     !productRoomAreaText(body.glyphs.front().text)||body.glyphs.front().text.back()!=L'm')continue;
  std::vector<std::string> matches;
  for(const auto& exponent:inventory.rows){
   if(!exponent.bounds||exponent.glyphs.size()!=1||exponent.glyphs.front().text!=L"2"||
      exponent.layer!=body.layer||exponent.glyphs.front().style!=body.glyphs.front().style||
      productAnnotationParent(exponent.path)!=productAnnotationParent(body.path))continue;
   const auto height=body.glyphs.front().height;
   const auto gap=exponent.bounds->minPoint().x-body.bounds->maxPoint().x;
   const auto rise=exponent.bounds->minPoint().y-body.bounds->minPoint().y;
   if(height>0&&std::abs(height-exponent.glyphs.front().height*2)<.01&&std::abs(gap)<height/2&&rise>0&&rise<height)
    matches.push_back(exponent.path);
  }
  if(matches.size()==1){pairedPaths.insert(body.path);pairedPaths.insert(matches.front());}
 }
 for(std::size_t v=0;v<scope.views.size();++v){
  if(!scope.active[v])continue;
  ProductRoomGrid empty;
  for(const auto& area:productOldNetAreas(db,scope.grids[v]?*scope.grids[v]:empty,scope.views,v,true)){
   if(!area.explicitUnit)continue;
   const auto entity=open<AcDbEntity>(find(db,area.body));
   if(area.region<0&&symbolName(entity->layerId()).find(L"房间")==std::wstring::npos)continue;
   pairedAreas.insert(area.body);if(!area.exponent.empty())pairedAreas.insert(area.exponent);
  }
 }
 static const std::wregex areaToken(LR"((?:(?:房间(?:净)?面积|净面积|面积)\s*[：:]?\s*)?[0-9]+(?:\.[0-9]+)?\s*(?:㎡|平方米|[mM](?:²|2|\^U2\^U|\\U\+00[Bb]2)))");
 for(const auto& row:inventory.rows){
  if(!row.visible)continue;
  const bool paper=inventory.paperPaths.count(row.path)!=0;
  if(!paper&&row.bounds&&!productFinalOmissionBoundsInScope(scope,*row.bounds))continue;
  if(evaluated)++*evaluated;
  if(row.kind.rfind("unreadable-",0)==0){
   add(row.path,"omission-coverage",u8"标注或嵌套容器不可完整读取，不能判定其中禁用标注为零");continue;
  }
  if(row.kind=="dimension"){
   try{
    auto actual=productReadSourceDimension(db,row.handle,.02/job.unifiedUnitMM);
    AcGeMatrix3d world;
    if(!actual.readable||actual.points.size()<2||!productFinalOmissionWorld(db,row.path,world)){
     add(row.path,"omission-coverage",u8"最终尺寸测点或实例变换不可读，未判断室内或外围用途");continue;
    }
    if(paper){add(row.path,"omission-coverage",u8"图纸空间尺寸尚未映射实际视口测点，不能推定为合规外围尺寸");continue;}
    for(auto& point:actual.points)point.transformBy(world);
    const auto& at=actual.points.front();const auto v=bcpproduct::nearestGenericView(scope.views,{at.x,at.y});
    if(!scope.active[v])continue;
    if(!scope.grids[v]){add(row.path,"omission-coverage",u8"本幅没有可靠闭合净域，保留尺寸并列用途待核，未以四向墙射线推定室内");continue;}
    if(productInteriorStations(actual.points,scope.input,scope.views[v],scope.grids[v],job.unifiedUnitMM,scope.exterior[v]))
     add(row.path,"D23",u8"独立重开全图扫描仍发现室内尺寸，包含后处理新增对象");
   }catch(const std::exception& e){add(row.path,"omission-coverage",e.what());}
  }else if(row.kind=="text"||row.kind=="linked-text"){
   std::wstring body;for(const auto& glyph:row.glyphs)body+=glyph.text;
   if(productGrossAreaProtected(body))continue;
   if(pairedAreas.count(row.handle)){add(row.path,"S01",u8"独立重开仍有房间面积正文或配对上标");continue;}
   if(!pairedPaths.count(row.path)&&!std::regex_search(body,areaToken))continue;
   const bool roomLayer=row.layer.find(L"房间")!=std::wstring::npos;
   bool room=false;
   if(row.position&&!paper){const auto v=bcpproduct::nearestGenericView(scope.views,{row.position->x,row.position->y});
    room=scope.grids[v]&&scope.grids[v]->at(*row.position)>=0;}
   if(roomLayer||room||productRoomRole(body)!=ProductRoomRole::other)
    add(row.path,"S01",u8"独立重开仍有房间面积标注或与房名混排的面积段");
  }
 }
 // Exploded dimensions are recognized across layers from line/text geometry.
 const auto loose=productRecognizeLooseDimensions(inventory.rows,inventory.strokes,
   productPerimeterSourceWitnesses(db,job),job.unifiedScale/job.unifiedUnitMM,job.unifiedUnitMM);
 for(const auto& [path,row]:loose)if(row.stations.size()>=2){
  const auto& at=row.stations.front();const auto v=bcpproduct::nearestGenericView(scope.views,{at.x,at.y});
  if(!scope.active[v]||!productFinalOmissionInScope(scope,at))continue;
  if(productInteriorStations(row.stations,scope.input,scope.views[v],scope.grids[v],job.unifiedUnitMM,scope.exterior[v]))
   add(path,"D23",u8"独立重开仍有散线文字构成的室内尺寸；"+row.reason);
 }
 return result;
}

ProductFinalOmissionContext productPrepareFinalPlanOmissions(ProductJob& job,AcDbDatabase& db){
 ProductFinalOmissionContext scope;
 const bool ownContext=!productJob;
 if(ownContext){productJob=std::make_unique<ProductJob>();productJob->ownedDocument=curDoc();}
 try{
  ProductJob reading;reading.root=job.root/L"final-omission-scope";
  scope.input=productReadGenericInput(reading,db);
  for(const auto& [panel,sheet]:job.planSheetLayouts){
   const auto viewport=open<AcDbViewport>(find(db,sheet.viewport));const auto scale=viewport->customScale();
   require(scale>0&&!viewport->isPerspectiveOn()&&std::abs(viewport->twistAngle())<1e-8&&
    viewport->viewDirection().isEqualTo(AcGeVector3d::kZAxis),"final omission viewport transform");
   const auto centre=viewport->viewTarget()+AcGeVector3d(viewport->viewCenter().x,viewport->viewCenter().y,0);
   scope.windows.push_back({centre.x-viewport->width()/(2*scale),centre.y-viewport->height()/(2*scale),
    centre.x+viewport->width()/(2*scale),centre.y+viewport->height()/(2*scale)});
  }
  require(!scope.windows.empty(),"final omission scope requires adopted plan viewports");
  std::vector<bcp::Wall> walls;for(const auto& w:scope.input.walls)walls.push_back(w.wall);
  scope.views=bcpproduct::discoverGenericViews(walls);
  scope.active.assign(scope.views.size(),false);scope.grids.resize(scope.views.size());scope.exterior.resize(scope.views.size());
  for(std::size_t v=0;v<scope.views.size();++v){
   const auto& bounds=scope.views[v].bounds;
   scope.active[v]=productFinalOmissionInScope(scope,{(bounds.xmin+bounds.xmax)*.5,(bounds.ymin+bounds.ymax)*.5,0});
   if(!scope.active[v])continue;
   std::vector<std::array<double,4>> rectangles;bool exact=true;
   for(const auto index:scope.views[v].walls){const auto& wall=scope.input.walls[index];
    if(wall.low)continue;if(!wall.orthogonal){exact=false;continue;}rectangles.push_back(approvedRect(wall.wall));}
   if(exact)scope.grids[v]=productRoomGrid(rectangles);
   if(scope.grids[v])for(const auto& name:productRoomNames(db,scope.input,scope.views,v,nullptr)){
    if(name.role!=ProductRoomRole::excluded)continue;
    const auto at=name.bounds.minPoint()+(name.bounds.maxPoint()-name.bounds.minPoint())*.5;
    const auto region=scope.grids[v]->at(at);if(region>=0)scope.exterior[v].insert(region);
   }
  }
  productProtectFurnitureMembers(job,db);
  const auto inventory=productFinalOmissionInventory(db);
  scope.sourceCandidates=productScanFinalPlanOmissions(job,db,scope,inventory);
  const auto loose=productRecognizeLooseDimensions(inventory.rows,inventory.strokes,
    productPerimeterSourceWitnesses(db,job),job.unifiedScale/job.unifiedUnitMM,job.unifiedUnitMM);
  for(const auto& issue:scope.sourceCandidates){
   if(issue.role!="D23")continue;
   const auto item=loose.find(issue.object);
   if(item!=loose.end()&&item->second.standalone){
    scope.cleanupScope.insert(item->first);
    scope.cleanupScope.insert(item->second.strokes.begin(),item->second.strokes.end());
   }
  }
 }catch(const std::exception& e){scope.error=e.what();}
 if(ownContext)productJob.reset();
 return scope;
}

void productCleanFinalPlanOmissions(ProductJob& job,AcDbDatabase& db,const ProductFinalOmissionContext& scope){
 if(!scope.error.empty()||scope.views.empty())return;
 const bool previous=job.currentPlanRules;job.currentPlanRules=true;
 auto source=std::move(job.planSourceAnnotations);
 try{
  job.planSourceAnnotations=productReadOldAnnotations(db);
  for(auto& row:job.planSourceAnnotations)if(row.position)
   row.panel=1+bcpproduct::nearestGenericView(scope.views,{row.position->x,row.position->y});
  const auto roots=modelTypes(db);
  productApplyPlanOmissions(job,db,scope.input,scope.views,scope.active,scope.grids,roots,
   [&](const auto& point){return productFinalOmissionInScope(scope,point);});
  const auto inventory=productFinalOmissionInventory(db);
  const auto loose=productRecognizeLooseDimensions(inventory.rows,inventory.strokes,
    productPerimeterSourceWitnesses(db,job),job.unifiedScale/job.unifiedUnitMM,job.unifiedUnitMM);
  std::map<std::string,std::size_t> uses;
  for(const auto& [path,row]:loose)for(const auto& stroke:row.strokes)++uses[stroke];
  for(const auto& [path,row]:loose){
   if(!row.standalone||row.stations.size()<2)continue;
   const auto& at=row.stations.front();const auto v=bcpproduct::nearestGenericView(scope.views,{at.x,at.y});
   if(!scope.active[v]||!std::all_of(row.stations.begin(),row.stations.end(),
    [&](const auto& point){return productFinalOmissionInScope(scope,point);}))continue;
   if(!productInteriorStations(row.stations,scope.input,scope.views[v],scope.grids[v],job.unifiedUnitMM,scope.exterior[v]))continue;
   const auto label=productLooseDimensionEntity(db,path);if(!label)continue;
   std::vector<AcDbObjectId> targets{*label};
   for(std::size_t k=0;k<row.strokes.size();++k){
    const auto id=productLooseDimensionEntity(db,row.strokes[k]);if(!id||uses[row.strokes[k]]!=1)break;
    const auto entity=open<AcDbEntity>(*id);const auto* line=AcDbLine::cast(entity.get());
    const auto& [a,b]=row.strokeGeometry[k];
    if(!line||line->startPoint().distanceTo(a)*job.unifiedUnitMM>.01||line->endPoint().distanceTo(b)*job.unifiedUnitMM>.01)break;
    targets.push_back(*id);
   }
   if(targets.size()!=row.strokes.size()+1||!std::all_of(targets.begin(),targets.end(),[&](const auto id){
    const auto entity=open<AcDbEntity>(id);return entity->ownerId()==modelId(db)&&entity->extensionDictionary().isNull()&&
     !job.protectedFurnitureMembers.count(axisHandle(id))&&productOmissionEditable(db,axisHandle(id))&&!productSourceNativeDimensionProtected(*entity);
   }))continue;
   for(const auto id:targets)productRetirePlanAnnotation(job,db,axisHandle(id),"D23");
  }
 }catch(...){job.planSourceAnnotations=std::move(source);job.currentPlanRules=previous;throw;}
 job.planSourceAnnotations=std::move(source);job.currentPlanRules=previous;
}

std::vector<ProductFinalOmissionIssue> productAuditFinalPlanOmissions(ProductJob& job,AcDbDatabase& saved,
 const ProductFinalOmissionContext& scope,const fs::path& drawing,const fs::path& report){
 Working context(&saved);
 auto inventory=productFinalOmissionInventory(saved);
 std::size_t evaluated=0;auto issues=productScanFinalPlanOmissions(job,saved,scope,inventory,&evaluated);
 const auto exclusions=productFinalOmissionExclusions(job,saved,scope,inventory);
 std::set<std::string> unknownRoots;for(const auto& row:exclusions)if(!row.coverageKnown)unknownRoots.insert(row.root);
 for(const auto* rule:{"D23","S01"})if(!unknownRoots.empty())issues.push_back({std::string("coverage/")+rule,
  std::string("omission-coverage-")+rule,u8"独立终核存在未完整读取的实例域（"+std::to_string(unknownRoots.size())+u8"个）；已读标注仍核对，家具保持原样，未计为全覆盖；详见 exclusions"});
 std::ostringstream out;out<<"{\"contract\":\"plan.final-omissions.v1\",\"savedReopened\":true,\"independentInventory\":true,"
  <<"\"outputSHA256\":\""<<sha(drawing)<<"\",\"modelEnumerated\":true,\"layoutsEnumerated\":"<<inventory.layouts
  <<",\"modelEnumerationScope\":\"model-space root enumeration only; not complete glyph decoding\",\"enumeratedModelRoots\":"<<modelTypes(saved).size()
  <<",\"enumeratedAnnotationRows\":"<<inventory.rows.size()<<",\"evaluatedAnnotationRows\":"<<evaluated
  <<",\"scannedObjects\":"<<evaluated<<",\"exclusions\":[";
 bool firstExclusion=true;for(const auto& row:exclusions){
  if(!firstExclusion)out<<',';firstExclusion=false;
  out<<"{\"object\":"<<jsonUtf8(bcpproduct::decodeMessage(row.root))<<",\"root\":"<<jsonUtf8(bcpproduct::decodeMessage(row.root))
     <<",\"type\":"<<jsonUtf8(row.type)<<",\"category\":"<<jsonUtf8(bcpproduct::decodeMessage(row.category))
     <<",\"reason\":"<<jsonUtf8(bcpproduct::decodeMessage(row.reason))<<",\"rules\":[\"D23\",\"S01\"],\"writeProtected\":true,\"coverageKnown\":"<<(row.coverageKnown?"true":"false")
     <<",\"readAnnotationPaths\":[";bool comma=false;for(const auto& path:row.readPaths){if(comma)out<<',';comma=true;out<<jsonUtf8(bcpproduct::decodeMessage(path));}
  out<<"],\"unreadPaths\":[";comma=false;for(const auto& path:row.unreadPaths){if(comma)out<<',';comma=true;out<<jsonUtf8(bcpproduct::decodeMessage(path));}out<<"]}";
 }
 out<<"],\"ruleCoverage\":{\"D23\":{\"coverageKnown\":"<<(unknownRoots.empty()?"true":"false")
    <<"},\"S01\":{\"coverageKnown\":"<<(unknownRoots.empty()?"true":"false")<<"}},\"sourceCandidates\":[";
 bool first=true;
 const auto write=[&](const auto& row,bool source){if(!first)out<<',';first=false;out<<"{\"object\":"<<jsonUtf8(bcpproduct::decodeMessage(row.object))
  <<",\"role\":"<<jsonUtf8(bcpproduct::decodeMessage(row.role))<<",\"reason\":"<<jsonUtf8(bcpproduct::decodeMessage(row.reason));
  const auto handle=row.object.substr(row.object.rfind('/')==std::string::npos?0:row.object.rfind('/')+1);
  if(source&&(row.role=="D23"||row.role=="S01")&&!handle.empty()&&
    std::all_of(handle.begin(),handle.end(),[](unsigned char c){return std::isxdigit(c)!=0;})){
   AcDbObjectId id;const bool present=saved.getAcDbObjectId(id,false,AcDbHandle(bcpproduct::decodeMessage(handle).c_str()))==Acad::eOk&&!id.isErased();
   out<<",\"presentAfterReopen\":"<<(present?"true":"false");
  }out<<'}';};
 for(const auto& row:scope.sourceCandidates)write(row,true);
 out<<"],\"residuals\":[";first=true;for(const auto& row:issues)write(row,false);
 out<<"]}";writeNew(report/L"plan-final-omissions-readback.json",out.str());
 return issues;
}
