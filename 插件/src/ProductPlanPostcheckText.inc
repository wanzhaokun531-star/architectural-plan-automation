// Local L06 repair on an isolated candidate. The caller owns SaveAs, the
// independent reopen, final PDF proof and adoption of the candidate.
struct ProductPlanPostcheckTextEdit {
 std::size_t panel{};
 std::string handle;
 AcGePoint3d before,after;
 std::vector<std::string> cleared;
 std::function<bool(AcDbDatabase&,const ProductJob&)> verifyReopened;
};

using ProductPlanPostcheckTextRepair=std::function<std::shared_ptr<const ProductPlanPostcheckTextEdit>(
 AcDbDatabase&,const ProductJob&,std::size_t,const std::string&)>;

struct ProductPostcheckTextContext {
 std::vector<bcpproduct::GenericView> views;
 std::vector<bool> active;
 std::vector<std::optional<ProductRoomGrid>> grids;
 std::map<std::string,std::wstring> captions;
 std::map<std::string,ProductSourceTextNote> notes;
 double paper{},tolerance{};
};

bool productPostcheckTextType(const std::wstring& type){
 return type==L"AcDbText"||type==L"AcDbMText"||type==L"TDbText"||type==L"TDbMText";
}

bool productPostcheckTextWritable(AcDbDatabase& db,const AcDbEntity& entity){
 if(!productPostcheckTextType(entity.isA()->name())||entity.isAProxy()||entity.ownerId()!=modelId(db)||
    !productPlanDimensionVisible(entity)||entity.hasFields()||!entity.extensionDictionary().isNull())return false;
 const auto layer=open<AcDbLayerTableRecord>(entity.layerId());
 if(layer->isLocked()||!layer->isPlottable())return false;
 if(const auto* reactors=entity.reactors())for(int i=0;i<reactors->length();++i)
  if(acdbIsPersistentReactor(reactors->at(i)))return false;
 return true;
}

bool productPostcheckTextSame(const ProductSourceTextState& before,const ProductSourceTextState& after,
 const AcGeVector3d& shift,double tolerance){
 return productSourceTextSame(before,after,shift,tolerance)&&
  std::equal(before.leaves.begin(),before.leaves.end(),after.leaves.begin(),[&](const auto& lhs,const auto& rhs){
   return (lhs.anchor+shift).distanceTo(rhs.anchor)<=tolerance;
  });
}

std::string productPostcheckTextPresentation(const AcDbEntity& entity){
 std::ostringstream out;
 out<<std::setprecision(17)<<axisHandle(entity.layerId())<<':'<<axisHandle(entity.linetypeId())<<':'
    <<entity.linetypeScale()<<':'<<static_cast<int>(entity.lineWeight())<<':'<<static_cast<int>(entity.visibility());
 if(const auto* text=AcDbText::cast(&entity)){
  const auto normal=text->normal();
  out<<':'<<axisHandle(text->textStyle())<<':'<<text->isMirroredInX()<<':'<<text->isMirroredInY()
     <<':'<<normal.x<<':'<<normal.y<<':'<<normal.z;
 }else if(const auto* text=AcDbMText::cast(&entity)){
  const auto normal=text->normal();
  out<<':'<<axisHandle(text->textStyle())<<':'<<normal.x<<':'<<normal.y<<':'<<normal.z;
 }
 return out.str();
}

std::string productPostcheckProtectedTextContainers(AcDbDatabase& db){
 std::ostringstream out;
 out<<std::setprecision(17)<<'[';
 const auto table=open<AcDbBlockTable>(db.blockTableId());
 AcDbBlockTableIterator* raw{};
 productStatus(table->newIterator(raw),"inspect protected paper and block records");
 std::unique_ptr<AcDbBlockTableIterator> blocks(raw);
 bool first=true;
 for(;!blocks->done();blocks->step()){
  AcDbObjectId id;
  productStatus(blocks->getRecordId(id),"read protected record identity");
  if(id==modelId(db))continue;
  const auto record=open<AcDbBlockTableRecord>(id);
  if(!first)out<<',';
  first=false;
  out<<"[\""<<axisHandle(id)<<"\","<<jsonUtf8(symbolName(id))<<",[";
  if(!record->isFromExternalReference()&&!record->isFromOverlayReference()){
   AcDbBlockTableRecordIterator* rawEntities{};
   productStatus(record->newIterator(rawEntities),"inspect protected block members");
   std::unique_ptr<AcDbBlockTableRecordIterator> members(rawEntities);
   bool firstMember=true;
   for(;!members->done();members->step()){
    AcDbObjectId objectId;
    productStatus(members->getEntityId(objectId),"read protected member identity");
    const auto entity=open<AcDbEntity>(objectId);
    if(!firstMember)out<<',';
    firstMember=false;
    out<<"[\""<<axisHandle(objectId)<<"\","<<jsonUtf8(entity->isA()->name())<<','<<wholeLayer(entity->layerId())
       <<','<<entity->colorIndex()<<','<<static_cast<int>(entity->visibility())<<','<<static_cast<int>(entity->lineWeight());
    AcDbExtents extents;
    AcGePoint3dArray grips;
    AcDbIntArray modes,indices;
    const auto readGrips=entity->getGripPoints(grips,modes,indices);
    std::vector<AcGePoint3d> points;
    for(int i=0;i<grips.length();++i)points.push_back(grips[i]);
    out<<','<<readGrips<<','<<wholePoints(points);
    if(entity->getGeomExtents(extents)==Acad::eOk)
     out<<','<<wholePoints({extents.minPoint(),extents.maxPoint()});
    if(const auto* viewport=AcDbViewport::cast(entity.get())){
     const auto center=viewport->viewCenter();
     const auto direction=viewport->viewDirection();
     out<<','<<wholePoints({viewport->centerPoint(),viewport->viewTarget(),{center.x,center.y,0},
       {direction.x,direction.y,direction.z}})<<",["<<viewport->width()<<','<<viewport->height()<<','
       <<viewport->customScale()<<','<<viewport->twistAngle()<<','<<viewport->isLocked()<<','<<viewport->isOn()<<']';
     AcDbObjectIdArray frozen;
     productStatus(viewport->getFrozenLayerList(frozen),"read protected viewport layers");
     out<<",[";
     for(int i=0;i<frozen.length();++i){if(i)out<<',';out<<'"'<<axisHandle(frozen[i])<<'"';}
     out<<']';
    }else if(const auto* text=AcDbText::cast(entity.get()))out<<','<<jsonUtf8(text->textString());
    else if(const auto* text=AcDbMText::cast(entity.get())){
     AcString contents;
     productStatus(text->contents(contents),"read protected paper or block text");
     out<<','<<jsonUtf8(contents.kACharPtr());
    }
    out<<']';
   }
  }
  out<<"]]";
 }
 return out.str()+']';
}

struct ProductPostcheckTextProtection {
 std::map<std::wstring,std::wstring> roots;
 std::vector<std::string> order;
 std::string model,paperAndBlocks,dictionary;

 bool matches(AcDbDatabase& db,const std::string& handle)const{
  return modelTypes(db)==roots&&wholeDrawOrder(db,{})==order&&
   bcpproduct::samePublicGeometry(model,wholeProtected(db,{handle},true))&&
   bcpproduct::samePublicGeometry(paperAndBlocks,productPostcheckProtectedTextContainers(db))&&
   productDictionaryState(db)==dictionary;
 }
};

bool productPostcheckTextSheet(const ProductJob& job,AcDbDatabase& db,std::size_t panel,
 AcDbObjectId layer,const AcDbExtents& box,double gap){
 const auto inside=[&](const auto& bounds){
  return box.minPoint().x>=bounds[0]+gap&&box.minPoint().y>=bounds[1]+gap&&
   box.maxPoint().x<=bounds[2]-gap&&box.maxPoint().y<=bounds[3]-gap;
 };
 const auto found=std::find(job.layoutSourcePanels.begin(),job.layoutSourcePanels.end(),panel);
 if(found==job.layoutSourcePanels.end())return false;
 const auto sheet=static_cast<std::size_t>(found-job.layoutSourcePanels.begin());
 if(sheet>=job.planPrintWindows.size())return false;
 const auto layout=job.planSheetLayouts.find(static_cast<int>(sheet+1));
 if(layout==job.planSheetLayouts.end())return inside(job.planPrintWindows[sheet]);
 const auto viewport=open<AcDbViewport>(find(db,layout->second.viewport));
 const double scale=viewport->customScale();
 if(!viewport->isLocked()||!viewport->isOn()||viewport->isPerspectiveOn()||
    viewport->isLayerFrozenInViewport(layer)||!(scale>0)||std::abs(viewport->twistAngle())>1e-8||
    (viewport->viewDirection()-AcGeVector3d::kZAxis).length()>1e-8)return false;
 const auto source=viewport->viewTarget();
 const auto center=viewport->viewCenter();
 const double x=source.x+center.x,y=source.y+center.y;
 const double halfWidth=viewport->width()/(2*scale),halfHeight=viewport->height()/(2*scale);
 return inside(std::array<double,4>{x-halfWidth,y-halfHeight,x+halfWidth,y+halfHeight});
}

bool productPostcheckTextDomain(const ProductPostcheckTextContext& context,std::size_t panel,
 const AcGePoint3d& origin,const AcDbExtents& box){
 const auto& grid=context.grids[panel-1];
 const auto at=box.minPoint()+(box.maxPoint()-box.minPoint())*.5;
 if(productSourceTextOwner(context.views,at,55*context.paper,context.tolerance)!=panel-1)return false;
 const int room=grid?grid->at(origin):ProductRoomGrid::outside;
 if(room>=0){
  AcDbExtents route(origin,origin);
  route.addPoint(at);
  return productSourceTextInsideRoom(*grid,room,box)&&productSourceTextInsideRoom(*grid,room,route);
 }
 const auto& view=context.views[panel-1];
 const auto side=productSourceTextExteriorSide(origin,view);
 return side>=0&&productSourceTextExteriorSide(box.minPoint(),view)==side&&
  productSourceTextExteriorSide(box.maxPoint(),view)==side;
}

std::vector<std::string> productPostcheckTextHits(const std::vector<ProductSourceTextPriorityInk>& ink,
 const AcDbExtents& box,double gap){
 std::vector<std::string> hits;
 for(const auto& row:ink)if(productSourceTextInkHit(row,box,gap))hits.push_back(row.handle);
 return hits;
}

ProductPlanPostcheckTextRepair productMakePostcheckTextRepair(std::vector<bcpproduct::GenericView> views,
 std::vector<bool> active,std::vector<std::optional<ProductRoomGrid>> grids,
 std::map<std::string,std::wstring> captions,const std::vector<ProductSourceTextNote>& notes,double scale,double unit){
 if(views.empty()||active.size()!=views.size()||grids.size()!=views.size()||!(scale>0)||!(unit>0)||
    !std::isfinite(scale/unit))return {};
 auto context=std::make_shared<ProductPostcheckTextContext>(ProductPostcheckTextContext{
  std::move(views),std::move(active),std::move(grids),std::move(captions),{},scale/unit,.01/unit});
 for(const auto& note:notes)if(note.editable&&note.panel&&productPostcheckTextType(note.source.type))
  context->notes.emplace(note.handle,note);
 if(context->notes.empty())return {};
 return [context](AcDbDatabase& db,const ProductJob& job,std::size_t panel,const std::string& handle)
     ->std::shared_ptr<const ProductPlanPostcheckTextEdit>{
  Working working(&db);
  const auto found=context->notes.find(handle);
  if(found==context->notes.end()||!panel||panel>context->views.size()||!context->active[panel-1]||
     found->second.panel!=panel)return {};
  const auto& note=found->second;
  AcDbObjectId id;
  if(db.getAcDbObjectId(id,false,AcDbHandle(bcpproduct::decodeMessage(handle).c_str()))!=Acad::eOk||id.isErased())return {};
  auto entity=open<AcDbEntity>(id);
  if(!productPostcheckTextWritable(db,*entity))return {};
  const auto current=productSourceTextState(*entity);
  if(!current||!productPostcheckTextSame(note.source,*current,note.shift,context->tolerance))return {};
  const auto layer=entity->layerId();
  const auto presentation=productPostcheckTextPresentation(*entity);
  const auto origin=note.source.box.minPoint()+(note.source.box.maxPoint()-note.source.box.minPoint())*.5;
  if(productSourceTextOwner(context->views,origin,55*context->paper,context->tolerance)!=panel-1)return {};
  entity.reset();
  const auto priority=productReadSourceTextPriorityInk(job,db,context->captions);
  const double gap=.35*context->paper;
  const auto observed=productPostcheckTextHits(priority,current->box,gap);
  if(observed.empty())return {};
  const auto frames=productPlanDrawingFrames(db,context->paper);
  std::set<std::string> exclusions{handle};
  for(const auto& row:priority)if(row.axis)exclusions.insert(row.handle);
  ProductPlanDetailObstacles artwork(db,exclusions,true);
  std::vector<AcGeVector3d> displacements;
  constexpr int quarterSteps=48;
  for(int x=-quarterSteps;x<=quarterSteps;++x)for(int y=-quarterSteps;y<=quarterSteps;++y)
   if((x||y)&&x*x+y*y<=quarterSteps*quarterSteps)displacements.emplace_back(x*.25*context->paper,y*.25*context->paper,0);
  std::stable_sort(displacements.begin(),displacements.end(),[](const auto& a,const auto& b){
   return a.lengthSqrd()<b.lengthSqrd();
  });
  const auto clears=[&](const AcDbExtents& box){
   return productPostcheckTextDomain(*context,panel,origin,box)&&productPlanInsideFrame(frames,origin,box,gap)&&
    productPostcheckTextSheet(job,db,panel,layer,box,gap)&&productPostcheckTextHits(priority,box,gap).empty()&&
    !artwork.hits({},box);
  };
  const auto offset=std::find_if(displacements.begin(),displacements.end(),[&](const auto& delta){
   return clears(AcDbExtents(current->box.minPoint()+delta-note.shift,current->box.maxPoint()+delta-note.shift));
  });
  if(offset==displacements.end())return {};
  const auto protectedBefore=std::make_shared<ProductPostcheckTextProtection>(ProductPostcheckTextProtection{
   modelTypes(db),wholeDrawOrder(db,{}),wholeProtected(db,{handle},true),
   productPostcheckProtectedTextContainers(db),productDictionaryState(db)});
  const auto delta=*offset-note.shift;
  auto proposal=std::make_shared<ProductPlanPostcheckTextEdit>();
  proposal->panel=panel;
  proposal->handle=handle;
  proposal->before=current->anchor;
  proposal->after=current->anchor+delta;
  proposal->cleared=observed;
  proposal->verifyReopened=[context,protectedBefore,panel,handle,origin,delta,before=*current,observed,presentation]
     (AcDbDatabase& saved,const ProductJob& savedJob){
   Working reopened(&saved);
   AcDbObjectId noteId;
   if(saved.getAcDbObjectId(noteId,false,AcDbHandle(bcpproduct::decodeMessage(handle).c_str()))!=Acad::eOk||noteId.isErased())return false;
   const auto noteEntity=open<AcDbEntity>(noteId);
   if(!productPostcheckTextWritable(saved,*noteEntity)||productPostcheckTextPresentation(*noteEntity)!=presentation)return false;
   const auto after=productSourceTextState(*noteEntity);
   if(!after||!productPostcheckTextSame(before,*after,delta,context->tolerance)||!protectedBefore->matches(saved,handle))return false;
   const auto fixed=productReadSourceTextPriorityInk(savedJob,saved,context->captions);
   // An obstacle must still exist under the same key. A disappearance cannot
   // count as resolving a source-note collision.
   const double margin=.35*context->paper;
   const auto oldKeys=productPostcheckTextHits(fixed,before.box,margin);
   if(oldKeys!=observed)return false;
   std::set<std::string> excluded{handle};
   for(const auto& row:fixed)if(row.axis)excluded.insert(row.handle);
   return productPostcheckTextDomain(*context,panel,origin,after->box)&&
    productPlanInsideFrame(productPlanDrawingFrames(saved,context->paper),origin,after->box,margin)&&
    productPostcheckTextSheet(savedJob,saved,panel,noteEntity->layerId(),after->box,margin)&&
    productPostcheckTextHits(fixed,after->box,margin).empty()&&
    !ProductPlanDetailObstacles(saved,excluded,true).hits({},after->box);
  };
  approvedgrips::Transaction transaction(db);
  AcDbObject* object{};
  productStatus(transaction.transaction->getObject(object,id,AcDb::kForWrite),"open postcheck source note");
  auto* writable=AcDbEntity::cast(object);
  if(!writable||writable->transformBy(AcGeMatrix3d::translation(delta))!=Acad::eOk)return {};
  const auto actual=productSourceTextState(*writable);
  if(!actual||productPostcheckTextPresentation(*writable)!=presentation||
     !productPostcheckTextSame(*current,*actual,delta,context->tolerance)||
     !clears(actual->box)||!protectedBefore->matches(db,handle))return {};
  transaction.finish(true);
  return proposal;
 };
}
