// B01 preserves a source note's applicable style and design content. L06
// permits translating a standalone note after reading the ink it must clear.
struct ProductSourceNoteStyle {
 std::wstring name,primary,big,face;
 double width{},height{},oblique{};
 bool vertical{},bold{},italic{};

 bool operator==(const ProductSourceNoteStyle& other)const{
  return std::tie(name,primary,big,face,width,height,oblique,vertical,bold,italic)==
   std::tie(other.name,other.primary,other.big,other.face,other.width,other.height,other.oblique,
    other.vertical,other.bold,other.italic);
 }
};

ProductSourceNoteStyle productReadSourceNoteStyle(AcDbObjectId id){
 if(id.isNull())return {};
 const auto style=open<AcDbTextStyleTableRecord>(id);
 AcString primary,big,face;
 bool bold{},italic{};
 Charset charset{};
 Autodesk::AutoCAD::PAL::FontUtils::FontPitch pitch{};
 Autodesk::AutoCAD::PAL::FontUtils::FontFamily family{};
 style->fileName(primary);
 style->bigFontFileName(big);
 style->font(face,bold,italic,charset,pitch,family);
 return {symbolName(id),primary.kACharPtr(),big.kACharPtr(),face.kACharPtr(),style->xScale(),
  style->textSize(),style->obliquingAngle(),style->isVertical()!=0,bold,italic};
}

struct ProductSourceTextState {
 std::wstring type,contents,layer;
 AcCmColor color;
 AcDbExtents box;
 AcGePoint3d anchor;
 std::vector<ProductRoomLeaf> leaves;
 std::vector<ProductSourceNoteStyle> styles;
 std::vector<double> format;
};

std::optional<ProductSourceTextState> productSourceTextState(const AcDbEntity& entity){
 ProductSourceTextState state;
 state.type=entity.isA()->name();
 state.layer=symbolName(entity.layerId());
 state.color=entity.color();
 const auto anchor=productPlanNameAnchor(entity);
 if(!anchor||entity.getGeomExtents(state.box)!=Acad::eOk)return {};
 state.anchor=*anchor;
 productRoomLeaves(entity,state.leaves);
 for(const auto& leaf:state.leaves)state.styles.push_back(productReadSourceNoteStyle(leaf.style));
 if(const auto* text=AcDbText::cast(&entity)){
  state.contents=text->textString();
  state.format={text->widthFactor(),text->oblique(),text->thickness(),
   static_cast<double>(text->horizontalMode()),static_cast<double>(text->verticalMode())};
 }else if(const auto* mtext=AcDbMText::cast(&entity)){
  AcString raw;
  if(mtext->contents(raw)!=Acad::eOk)return {};
  state.contents=raw.kACharPtr();
  state.format={mtext->width(),mtext->textHeight(),mtext->rotation(),static_cast<double>(mtext->attachment())};
  state.styles.push_back(productReadSourceNoteStyle(mtext->textStyle()));
 }else state.contents=productPlanNameContents(entity);
 if(state.contents.empty()||state.styles.empty())return {};
 return state;
}

bool productSourceTextSame(const ProductSourceTextState& before,const ProductSourceTextState& after,
 const AcGeVector3d& shift,double tolerance){
 const auto close=[=](const auto& a,const auto& b){return (a+shift).distanceTo(b)<=tolerance;};
 return before.type==after.type&&before.contents==after.contents&&before.layer==after.layer&&
  before.color==after.color&&before.format==after.format&&before.styles==after.styles&&close(before.anchor,after.anchor)&&
  close(before.box.minPoint(),after.box.minPoint())&&close(before.box.maxPoint(),after.box.maxPoint())&&
  before.leaves.size()==after.leaves.size()&&std::equal(before.leaves.begin(),before.leaves.end(),after.leaves.begin(),
   [](const auto& a,const auto& b){
    return a.text==b.text&&std::abs(a.height-b.height)<1e-8&&
     std::abs(a.rotation-b.rotation)<1e-8;
   });
}

struct ProductPlanTitleTextStyle {
 std::string handle;
 std::size_t panel{};
 std::wstring body,type,layer;
 std::vector<std::wstring> visibleContents;
 std::optional<std::wstring> rawContents;
 std::string pending;
};

std::wstring productPlanTitleBody(std::wstring text){
 text.erase(std::remove_if(text.begin(),text.end(),[](wchar_t c){return iswspace(c)!=0||c==L'　';}),text.end());
 return text;
}

std::vector<ProductPlanTitleTextStyle> productNormalizePlanTitleText(ProductJob& job,AcDbDatabase& db,
 AcDbDatabase& reference,const GenericNativeInput& input,const std::map<std::wstring,std::wstring>& original,
 const std::vector<bcpproduct::GenericView>& views,const std::vector<bool>& generated){
 std::map<std::string,GenericNativeTitle> titles;
 const auto knownTitle=[](const std::wstring& text){return bcpproduct::explicitPlanFloor(text)||productRoofTitle(text);};
 for(const auto& title:input.titles)if(knownTitle(title.text))titles.emplace(title.handle,title);
 for(const auto& note:input.notes)if(knownTitle(note.text))titles.emplace(note.handle,note);
 for(const auto& [wide,type]:original){
  if(type!=L"AcDbText"&&type!=L"AcDbMText"&&type!=L"TDbMText")continue;
  const auto handle=ascii(wide);
  const auto id=find(db,handle);
  if(id.isErased())continue;
  const auto entity=open<AcDbEntity>(id);
  if(!productPlanDimensionVisible(*entity))continue;
  const auto body=productPlanNameContents(*entity);
  if(!knownTitle(body))continue;
  const auto anchor=productPlanNameAnchor(*entity);
  if(anchor)titles.emplace(handle,GenericNativeTitle{handle,*anchor,body});
 }
 if(titles.empty())return {};
 const auto style=productPlanTextStyle(db,reference,L"-宋体",productPlanTitleTextStyle);
 std::vector<ProductPlanTitleTextStyle> records;
 for(const auto& [handle,title]:titles){
  const bcp::Point anchor{title.anchor.x,title.anchor.y};
  const auto view=bcpproduct::nearestGenericView(views,anchor);
  if(!generated[view])continue;
  const auto id=find(db,handle);
  if(id.isErased())continue;
  auto entity=open<AcDbEntity>(id);
  if(!productPlanDimensionVisible(*entity))continue;
  ProductPlanTitleTextStyle row{handle,view+1,productPlanTitleBody(title.text),entity->isA()->name(),symbolName(entity->layerId())};
  if(const auto* mtext=AcDbMText::cast(entity.get()))row.rawContents=productPlanMTextOriginal(*mtext);
  else{
   std::vector<ProductRoomLeaf> leaves;
   productRoomLeaves(*entity,leaves);
   for(const auto& leaf:leaves)row.visibleContents.push_back(leaf.text);
  }
  entity.reset();
  const auto nearest=bcpproduct::genericDistanceSquared(views[view].bounds,anchor);
  const auto owners=std::count_if(views.begin(),views.end(),[&](const auto& candidate){
   return std::abs(bcpproduct::genericDistanceSquared(candidate.bounds,anchor)-nearest)<1e-5;
  });
  if(owners!=1)row.pending=u8"该文字无法唯一关联楼层图幅，保留原图名及其样式";
  else try{
   row.pending=productRestyleStandalonePlanText(db,id,style,[&](const auto& text){
    return productPlanTitleBody(text)==row.body;
   });
  }catch(const std::exception& error){row.pending=error.what();}
  records.push_back(std::move(row));
 }
 return records;
}

void productVerifyPlanTitleText(ProductJob& job,AcDbDatabase& db,const std::vector<ProductPlanTitleTextStyle>& records){
 Working working(&db);
 for(const auto& row:records){
  bool preserved=false,font=false;
  AcDbObjectId id;
  if(db.getAcDbObjectId(id,false,AcDbHandle(bcpproduct::decodeMessage(row.handle).c_str()))==Acad::eOk&&!id.isErased()){
   const auto entity=open<AcDbEntity>(id);
   preserved=row.type==entity->isA()->name()&&row.layer==symbolName(entity->layerId())&&productPlanDimensionVisible(*entity);
   if(const auto* mtext=AcDbMText::cast(entity.get())){
    preserved=preserved&&row.rawContents&&row.rawContents==productPlanMTextOriginal(*mtext);
    font=productPlanTitleBody(productPlanNameContents(*mtext))==row.body&&productPlanMTextUsesStyle(*mtext)&&
     productPlanTitleTextStyle(db,mtext->textStyle());
   }else{
    std::vector<ProductRoomLeaf> leaves;
    std::vector<std::wstring> contents;
    productRoomLeaves(*entity,leaves);
    unsigned matched=0;
    font=true;
    for(const auto& leaf:leaves){
     contents.push_back(leaf.text);
     if(productPlanTitleBody(leaf.text)!=row.body)continue;
     ++matched;
     font=font&&productPlanTitleTextStyle(db,leaf.style);
    }
    font=font&&matched>0;
    preserved=preserved&&!contents.empty()&&contents==row.visibleContents;
   }
  }
  const bool verified=row.pending.empty()&&font&&preserved;
  const auto reason=verified?std::string(u8"独立重开已核图名正文实际为 -宋体，原正文、比例文字及图层保持"):
   row.pending.empty()?std::string(u8"重开未同时证实图名原文、图层及有效 -宋体"):row.pending;
  const auto status=verified?"verified-source-title-style":preserved?"pending-source-title-style":"failed-source-title-readback";
  productPlanAuditRow(job,{"P01"},row.panel,row.handle,"source-title-style",status,reason);
  job.drawingChecks.record(row.panel,"source-title-style",row.handle,{"P01"},bcpproduct::PlanReleaseGate::mainPlan,
   verified?bcpproduct::PlanCheck::verified:preserved?bcpproduct::PlanCheck::pending:bcpproduct::PlanCheck::failed,
   u8"原图名正文、比例文字与实际 -宋体字体",verified?std::string{}:reason);
 }
}

struct ProductSourceTextNote {
 std::string handle;
 ProductSourceTextState source;
 AcGeVector3d shift{0,0,0};
 std::size_t panel{};
 std::set<std::string> conflicts;
 std::string reason;
 bool editable{};
};

std::vector<ProductSourceTextNote> productCaptureSourceText(AcDbDatabase& db,const GenericNativeInput& input,
 const std::map<std::wstring,std::wstring>& original,const std::vector<ProductPlanNameCheck>& names){
 std::set<std::string> controlled;
 std::set<std::wstring> numbers;
 std::set<AcDbObjectId> attached;
 for(const auto& name:names)controlled.insert(name.source);
 for(const auto& opening:input.openings)if(!opening.existing.empty())numbers.insert(opening.existing);
 for(const auto& [handle,type]:original)if(type==L"AcDbLeader"){
  const auto leader=open<AcDbLeader>(find(db,ascii(handle)));
  if(!leader->annotationObjId().isNull())attached.insert(leader->annotationObjId());
 }
 std::vector<ProductSourceTextNote> notes;
 for(const auto& [wide,type]:original){
  if(type!=L"AcDbText"&&type!=L"AcDbMText"&&type!=L"TDbText"&&type!=L"TDbMText")continue;
  const auto handle=ascii(wide);
  if(controlled.count(handle))continue;
  const auto entity=open<AcDbEntity>(find(db,handle));
  const auto layer=open<AcDbLayerTableRecord>(entity->layerId());
  if(entity->isAProxy()||!productPlanDimensionVisible(*entity)||!layer->isPlottable())continue;
  const auto state=productSourceTextState(*entity);
  if(!state)continue;
  auto plain=productPlanNameContents(*entity);
  if(plain.empty())plain=state->contents;
  const auto layerName=symbolName(entity->layerId());
  if(layerName==L"AXIS"||layerName==L"DOTE"||layerName==L"WINDOW_TEXT"||layerName==L"Defpoints"||
     layerName==L"A-房间名称"||layerName==L"房间名称"||layerName.find(L"DIM")!=std::wstring::npos||
     layerName.find(L"图名")!=std::wstring::npos||layerName.find(L"标高")!=std::wstring::npos||
     layerName.find(L"编号")!=std::wstring::npos||plain.find(L"平面图")!=std::wstring::npos||
     bcpproduct::explicitPlanFloor(plain)||productRoofTitle(plain)||
     productPlanGrossAreaLabel(plain)||productPlanGroupedAreaLabel(plain)||productRoomAreaText(plain)||
     productPlanKnownName(plain)||numbers.count(plain))continue;
  if(plain.find_first_of(L"0123456789")!=std::wstring::npos&&
     plain.find_first_not_of(L"0123456789+-.± 　") ==std::wstring::npos)continue;
  const auto content=plain.find_first_not_of(L" 　");
  if(content!=std::wstring::npos&&plain.size()<=12&&
     plain.find_first_not_of(L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_. 　")==std::wstring::npos)continue;
  bool editable=!layer->isLocked()&&!entity->hasFields()&&!attached.count(entity->objectId());
  if(const auto* reactors=entity->reactors())for(int i=0;i<reactors->length();++i)
   if(acdbIsPersistentReactor(reactors->at(i)))editable=false;
  notes.push_back({handle,*state,{0,0,0},0,{}, {},editable});
 }
 return notes;
}

struct ProductSourceTextPriorityInk {
 std::string handle;
 std::vector<AcDbExtents> glyphs;
 std::vector<std::pair<AcGePoint3d,AcGePoint3d>> strokes;
 std::vector<std::pair<AcGePoint3d,double>> circles;
 bool axis{};
};

void productSourceTextAxisInk(const AcDbEntity& item,ProductSourceTextPriorityInk& ink,int depth=0){
 if(depth>6||item.visibility()!=AcDb::kVisible)return;
 if(const auto* circle=AcDbCircle::cast(&item)){
  ink.circles.emplace_back(circle->center(),circle->radius());
  return;
 }
 if(const auto* line=AcDbLine::cast(&item)){
  ink.strokes.emplace_back(line->startPoint(),line->endPoint());
  return;
 }
 if(AcDbText::cast(&item)||AcDbMText::cast(&item)||AcDbArc::cast(&item))return;
 TemplateParts parts(&item);
 if(item.explode(parts.values)==Acad::eOk)for(int i=0;i<parts.values.length();++i)
  if(const auto* child=AcDbEntity::cast(static_cast<AcRxObject*>(parts.values[i])))
   productSourceTextAxisInk(*child,ink,depth+1);
}

std::vector<ProductSourceTextPriorityInk> productReadSourceTextPriorityInk(
 const ProductJob& job,AcDbDatabase& db,const std::map<std::string,std::wstring>& captions){
 std::vector<ProductSourceTextPriorityInk> rows;
 for(const auto& [wide,type]:modelTypes(db)){
  const auto handle=ascii(wide);
  const auto caption=captions.find(handle);
  const bool nativeDimension=type==L"TDbDimension2";
  const bool ordinaryDimension=type.find(L"Dimension")!=std::wstring::npos;
  const bool axis=type==L"TDbAxisLabelSet";
  if(!nativeDimension&&!ordinaryDimension&&!axis&&caption==captions.end())continue;
  if((nativeDimension||ordinaryDimension)&&job.planDimensionExclusions.count(handle))continue;
  const auto entity=open<AcDbEntity>(find(db,handle));
  if(!productPlanDimensionVisible(*entity)||!open<AcDbLayerTableRecord>(entity->layerId())->isPlottable())continue;
  ProductSourceTextPriorityInk ink{handle};
  if(nativeDimension){
   ink.glyphs=productPlanChainGlyphs(db,*entity);
   productPlanDimensionStrokes(*entity,ink.strokes);
  }else if(axis){
   ink.axis=true;
   productGenericTextLeaves(*entity,ink.glyphs);
   productSourceTextAxisInk(*entity,ink);
  }else if(caption!=captions.end()){
   if(const auto glyph=productGenericCaptionGlyph(*entity,caption->second))ink.glyphs.push_back(glyph->box);
  }else if(AcDbDimension::cast(entity.get())){
   productGenericTextLeaves(*entity,ink.glyphs);
   productPlanDimensionStrokes(*entity,ink.strokes);
  }
  if(!ink.glyphs.empty()||!ink.strokes.empty()||!ink.circles.empty())rows.push_back(std::move(ink));
 }
 return rows;
}

bool productSourceTextInkHit(const ProductSourceTextPriorityInk& ink,const AcDbExtents& box,double gap){
 const auto lo=box.minPoint(),hi=box.maxPoint();
 return std::any_of(ink.glyphs.begin(),ink.glyphs.end(),[&](const auto& glyph){
  return productGenericBoxesOverlap(box,glyph,gap);
 })||std::any_of(ink.strokes.begin(),ink.strokes.end(),[&](const auto& line){
  return productGenericSegmentHitsBox(line.first,line.second,box,gap);
 })||std::any_of(ink.circles.begin(),ink.circles.end(),[&](const auto& circle){
  const auto [centre,radius]=circle;
  return std::hypot(centre.x-std::clamp(centre.x,lo.x,hi.x),
                    centre.y-std::clamp(centre.y,lo.y,hi.y))<radius+gap;
 });
}

std::optional<std::size_t> productSourceTextOwner(const std::vector<bcpproduct::GenericView>& views,
 const AcGePoint3d& point,double maxDistance,double tolerance){
 const bcp::Point sample{point.x,point.y};
 const auto owner=bcpproduct::nearestGenericView(views,sample);
 const auto distance=std::sqrt(bcpproduct::genericDistanceSquared(views[owner].bounds,sample));
 if(distance>maxDistance)return {};
 for(std::size_t i=0;i<views.size();++i)if(i!=owner&&
  std::sqrt(bcpproduct::genericDistanceSquared(views[i].bounds,sample))<=distance+tolerance)return {};
 return owner;
}

bool productSourceTextInsideRoom(const ProductRoomGrid& grid,int room,const AcDbExtents& box){
 const auto lo=box.minPoint(),hi=box.maxPoint();
 if(room<0||grid.at(lo)!=room||grid.at(hi)!=room)return false;
 const auto firstX=std::upper_bound(grid.xs.begin(),grid.xs.end(),lo.x)-grid.xs.begin()-1;
 const auto firstY=std::upper_bound(grid.ys.begin(),grid.ys.end(),lo.y)-grid.ys.begin()-1;
 for(auto y=firstY;grid.ys[y]<hi.y;++y)for(auto x=firstX;grid.xs[x]<hi.x;++x)
  if(grid.cells[grid.index(x,y)]!=room)return false;
 return true;
}

int productSourceTextExteriorSide(const AcGePoint3d& point,const bcpproduct::GenericView& view){
 const auto& box=view.bounds;
 if(point.y>=box.ymin&&point.y<=box.ymax){
  if(point.x<box.xmin)return 0;
  if(point.x>box.xmax)return 1;
 }
 if(point.x>=box.xmin&&point.x<=box.xmax){
  if(point.y<box.ymin)return 2;
  if(point.y>box.ymax)return 3;
 }
 return -1;
}

void productPlaceSourceText(ProductJob& job,AcDbDatabase& db,const std::vector<bcpproduct::GenericView>& views,
 const std::vector<bool>& active,const std::vector<std::optional<ProductRoomGrid>>& grids,double scale,double unit,
 const std::map<std::string,std::wstring>& captions,std::vector<ProductSourceTextNote>& notes){
 if(notes.empty())return;
 const double paper=scale/unit;
 const double gap=.35*paper,tolerance=.01/unit;
 const auto priority=productReadSourceTextPriorityInk(job,db,captions);
 std::set<std::string> axisHandles;
 for(const auto& row:priority)if(row.axis)axisHandles.insert(row.handle);
 const auto frames=productPlanDrawingFrames(db,paper);
 std::vector<AcGeVector3d> offsets;
 constexpr int maxHalfPaperSteps=24;
 for(int x=-maxHalfPaperSteps;x<=maxHalfPaperSteps;++x)for(int y=-maxHalfPaperSteps;y<=maxHalfPaperSteps;++y)
  if((x||y)&&x*x+y*y<=maxHalfPaperSteps*maxHalfPaperSteps)offsets.emplace_back(x*.5*paper,y*.5*paper,0);
 std::stable_sort(offsets.begin(),offsets.end(),[](const auto& a,const auto& b){return a.lengthSqrd()<b.lengthSqrd();});
 const auto centre=[](const auto& box){return box.minPoint()+(box.maxPoint()-box.minPoint())*.5;};
 const auto intersections=[&](const auto& box){
  std::set<std::string> hits;
  for(const auto& ink:priority)if(productSourceTextInkHit(ink,box,gap))hits.insert(ink.handle);
  return hits;
 };
 for(auto& note:notes){
  AcDbObjectId id;
  if(db.getAcDbObjectId(id,false,AcDbHandle(bcpproduct::decodeMessage(note.handle).c_str()))!=Acad::eOk||id.isErased())continue;
  auto entity=open<AcDbEntity>(id);
  const auto current=productSourceTextState(*entity);
  if(!current||!productPlanDimensionVisible(*entity))continue;
  const auto origin=centre(note.source.box);
  const auto owner=productSourceTextOwner(views,origin,55*paper,tolerance);
  const auto nearest=bcpproduct::nearestGenericView(views,{origin.x,origin.y});
  if(!active[nearest]||bcpproduct::genericDistanceSquared(views[nearest].bounds,{origin.x,origin.y})>std::pow(55*paper,2))continue;
  note.panel=owner?*owner+1:0;
  const auto hits=intersections(current->box);
  note.reason.clear();
  if(hits.empty())continue;
  note.conflicts.insert(hits.begin(),hits.end());
  if(!owner){
   note.reason=u8"源文字所在位置不能唯一关联到一个图幅，待复核所属楼层和构件";
   continue;
  }
  if(!note.editable||!productSourceTextSame(note.source,*current,note.shift,tolerance)){
   note.reason=u8"源文字有附属关系、图层锁定或其文字状态已经变化，需核原附着关系后协调";
   continue;
  }
  const auto& grid=grids[*owner];
  const auto room=grid?grid->at(origin):ProductRoomGrid::outside;
  const auto exterior=productSourceTextExteriorSide(origin,views[*owner]);
  if(room<0&&exterior<0){
   note.reason=u8"源文字未能确认同一房间净域或本幅室外同侧，保留原附着位置待复核";
   continue;
  }
  const auto sameDomain=[&](const AcDbExtents& box){
   const auto at=centre(box);
   if(productSourceTextOwner(views,at,55*paper,tolerance)!=owner||
      !productPlanInsideFrame(frames,origin,box,gap))return false;
   if(room>=0){
    AcDbExtents path(origin,origin);
    path.addPoint(at);
    return productSourceTextInsideRoom(*grid,room,box)&&productSourceTextInsideRoom(*grid,room,path);
   }
   return productSourceTextExteriorSide(box.minPoint(),views[*owner])==exterior&&
    productSourceTextExteriorSide(box.maxPoint(),views[*owner])==exterior;
  };
  entity.reset();
  auto exclusions=axisHandles;
  exclusions.insert(note.handle);
  ProductPlanDetailObstacles obstacles(db,exclusions,true);
  bool moved=false;
  for(const auto& offset:offsets){
   const AcDbExtents candidate(note.source.box.minPoint()+offset,note.source.box.maxPoint()+offset);
   if(!sameDomain(candidate)||!intersections(candidate).empty()||obstacles.hits({},candidate))continue;
   const auto delta=offset-note.shift;
   auto writable=editObject<AcDbEntity>(id);
   if(writable->transformBy(AcGeMatrix3d::translation(delta))!=Acad::eOk)break;
   const auto actual=productSourceTextState(*writable);
   if(actual&&productSourceTextSame(note.source,*actual,offset,tolerance)){
    note.shift=offset;
    moved=true;
    break;
   }
   productStatus(writable->transformBy(AcGeMatrix3d::translation(-delta)),"restore source note after failed readback");
  }
  if(!moved)note.reason=u8"同一房间或同侧构造范围内未找到具备净空的附近位置";
 }
}

void productVerifySourceText(ProductJob& job,AcDbDatabase& saved,
 const std::map<std::string,std::wstring>& captions,const std::vector<ProductSourceTextNote>& notes,double scale,double unit){
 Working work(&saved);
 const auto ink=notes.empty()?std::vector<ProductSourceTextPriorityInk>{}:productReadSourceTextPriorityInk(job,saved,captions);
 std::set<std::string> axisHandles;
 for(const auto& row:ink)if(row.axis)axisHandles.insert(row.handle);
 std::ostringstream report;
 report<<"{\"savedReopened\":true,\"notes\":[";
 bool first=true;
 for(const auto& note:notes){
  if(!note.panel&&note.conflicts.empty())continue;
  AcDbObjectId id;
  std::optional<ProductSourceTextState> result;
  if(saved.getAcDbObjectId(id,false,AcDbHandle(bcpproduct::decodeMessage(note.handle).c_str()))==Acad::eOk&&!id.isErased()){
   const auto entity=open<AcDbEntity>(id);
   if(productPlanDimensionVisible(*entity))result=productSourceTextState(*entity);
  }
  const bool preserved=result&&productSourceTextSame(note.source,*result,note.shift,.01/unit);
  bool clear=result&&std::none_of(ink.begin(),ink.end(),[&](const auto& item){
   return productSourceTextInkHit(item,result->box,.35*scale/unit);
  });
  const bool moved=note.shift.length()>.01/unit;
  if(clear&&moved){
   auto exclusions=axisHandles;
   exclusions.insert(note.handle);
   clear=!ProductPlanDetailObstacles(saved,exclusions,true).hits({},result->box);
  }
  const auto state=!preserved?bcpproduct::PlanCheck::failed:clear?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::pending;
  const std::string reason=!preserved?u8"源文字保存重开后的内容、样式或锚点与平移记录不符":
   clear?u8"原句柄、内容、实际字体资源及净空已经重开核验":note.reason.empty()?u8"保存重开后与规则标注仍重叠":note.reason;
  const auto status=!preserved?"failed":clear?"verified":"pending";
  if(!first)report<<',';
  first=false;
  const auto after=result?std::vector<AcGePoint3d>{result->anchor}:std::vector<AcGePoint3d>{};
  report<<"{\"panel\":"<<note.panel<<",\"object\":\""<<note.handle<<"\",\"before\":"
   <<wholePoints({note.source.anchor})<<",\"after\":"<<wholePoints(after)
   <<",\"text\":"<<jsonUtf8(note.source.contents)<<",\"originalStyles\":[";
  for(std::size_t i=0;i<note.source.styles.size();++i){
   const auto& style=note.source.styles[i];
   if(i)report<<',';
   report<<"{\"style\":"<<jsonUtf8(style.name)<<",\"font\":"<<jsonUtf8(style.primary)
    <<",\"bigFont\":"<<jsonUtf8(style.big)<<",\"typeface\":"<<jsonUtf8(style.face)<<'}';
  }
  report<<"],\"moved\":"<<(moved?"true":"false")<<",\"conflictingAnnotations\":";
  productWriteHandleArray(report,std::vector<std::string>(note.conflicts.begin(),note.conflicts.end()));
  report<<",\"result\":\""<<status<<"\",\"detail\":"<<jsonUtf8(bcpproduct::decodeMessage(reason))<<'}';
  productPlanAuditRow(job,{"B01","L06","P06"},note.panel,note.handle,"source-text-clearance",status,reason);
  job.drawingChecks.record(note.panel,"source-text-clearance",note.handle,{"B01","L06","P06"},
   bcpproduct::PlanReleaseGate::mainPlan,state,"saved-reopened",reason);
 }
 report<<"]}";
 writeNew(job.root/L"source-text-clearance.json",report.str());
}
