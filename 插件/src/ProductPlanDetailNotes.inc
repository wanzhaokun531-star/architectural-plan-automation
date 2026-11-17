struct ProductPlanDetailNote {
 std::size_t panel{};
 std::string source,textObject,leaderObject;
 std::wstring text;
 std::vector<AcGePoint3d> path;
 AcGePoint3d position;
 bool pendingText{};
 bool index{};
 bool nativeIndex{};
 std::string issue;
};

AcDbObjectId productAppendPlanDetail(AcDbDatabase& db,std::unique_ptr<AcDbEntity> entity){
 AcDbObjectId id;
 auto model=editObject<AcDbBlockTableRecord>(modelId(db));
 productStatus(model->appendAcDbEntity(id,entity.get()),"append editable plan annotation");
 entity.release()->close();
 return id;
}

std::wstring productPlanDetailContents(const AcDbEntity& entity){
 if(const auto* text=AcDbMText::cast(&entity)){
  AcString contents;
  text->text(contents);
  return contents.kACharPtr();
 }
 std::vector<ProductRoomLeaf> leaves;
 productRoomLeaves(entity,leaves);
 std::wstring result;
 for(const auto& leaf:leaves){if(!result.empty())result+=L"\n";result+=leaf.text;}
 return result;
}

std::vector<AcDbExtents> productPlanDrawingFrames(AcDbDatabase& db,double paper){
 std::vector<AcDbExtents> frames;
 for(const auto& [handle,type]:modelTypes(db)){
  if(type!=L"AcDbPolyline")continue;
  const auto entity=open<AcDbEntity>(find(db,ascii(handle)));
  const auto* poly=AcDbPolyline::cast(entity.get());
  if(!open<AcDbLayerTableRecord>(entity->layerId())->isPlottable())continue;
  AcDbExtents box;
  if(!poly||!poly->isClosed()||poly->numVerts()!=4||!productPlanDimensionVisible(*poly)||poly->getGeomExtents(box)!=Acad::eOk)continue;
  const auto size=box.maxPoint()-box.minPoint();
  if(size.x/paper>=200&&size.x/paper<=850&&size.y/paper>=140&&size.y/paper<=610)frames.push_back(box);
 }
 return frames;
}

bool productPlanInsideFrame(const std::vector<AcDbExtents>& frames,const AcGePoint3d& anchor,const AcDbExtents& box,double margin){
 const AcDbExtents* frame=nullptr;
 for(const auto& f:frames){
  const auto a=f.minPoint(),b=f.maxPoint();
  if(anchor.x<a.x||anchor.x>b.x||anchor.y<a.y||anchor.y>b.y)continue;
  if(!frame||(b-a).length()<(frame->maxPoint()-frame->minPoint()).length())frame=&f;
 }
 return !frame||(box.minPoint().x>=frame->minPoint().x+margin&&box.minPoint().y>=frame->minPoint().y+margin&&
  box.maxPoint().x<=frame->maxPoint().x-margin&&box.maxPoint().y<=frame->maxPoint().y-margin);
}

void productPlanDimensionStrokes(const AcDbEntity& entity,std::vector<std::pair<AcGePoint3d,AcGePoint3d>>& lines){
 if(const auto* dim=AcDbAlignedDimension::cast(&entity)){
  const auto a=dim->xLine1Point(),b=dim->xLine2Point();
  if(a.distanceTo(b)<=1e-8)return;
  const auto tangent=(b-a).normal();
  const auto project=[&](const auto& p){return dim->dimLinePoint()+tangent*((p-dim->dimLinePoint()).dotProduct(tangent));};
  lines.emplace_back(project(a),project(b));
  for(int witness=0;witness<2;++witness){
   if(witness==0?dim->dimse1():dim->dimse2())continue;
   const auto& p=witness==0?a:b;
   const auto q=project(p);const auto d=q-p;
   const auto offset=dim->dimexo()*dim->dimscale();
   if(d.length()>std::max(offset,1e-8))lines.emplace_back(p+d.normal()*offset,q+d.normal()*(dim->dimexe()*dim->dimscale()));
  }
  return;
 }
 TemplateParts parts(&entity);
 if(entity.explode(parts.values)==Acad::eOk)for(int i=0;i<parts.values.length();++i)
  if(const auto* dim=AcDbAlignedDimension::cast(static_cast<AcRxObject*>(parts.values[i])))productPlanDimensionStrokes(*dim,lines);
}

class ProductPlanDetailObstacles {
public:
 ProductPlanDetailObstacles(AcDbDatabase& db,const std::set<std::string>& excluded,bool protectUnread=false)
  :db_(db),protectUnread_(protectUnread){
  for(const auto& [wide,type]:modelTypes(db)){
   const bool physical=type==L"TDbWall"||type==L"TDbColumn"||type==L"TDbOpening"||type==L"TDbGroupOpening"||
    type==L"TDbLineStair"||type==L"AcDbBlockReference"||type==L"AcDbLine"||type==L"AcDbPolyline"||type==L"AcDbArc"||
    (protectUnread&&(type.find(L"Stair")!=std::wstring::npos||type==L"AcDbCircle"||type==L"AcDbHatch"||
       type==L"AcDbLeader"||type==L"AcDbMLeader"||type==L"AcDbEllipse"||type==L"AcDbSpline"));
   const bool dimension=type==L"TDbDimension2";
   const bool annotation=type==L"AcDbText"||type==L"AcDbMText"||type==L"TDbText"||type==L"TDbMText"||
    type==L"TDbDrawingName"||type==L"TDbAxisLabelSet"||type==L"TDbSymbDrawingIndex"||
    type==L"TDbSymbMultiLeader"||type==L"TDbSymbElevation";
   if(!physical&&!dimension&&!annotation)continue;
   const auto handle=ascii(wide);
   if(excluded.count(handle))continue;
   const auto id=find(db,handle);
   const auto entity=open<AcDbEntity>(id);
   if(!productPlanDimensionVisible(*entity)||!open<AcDbLayerTableRecord>(entity->layerId())->isPlottable())continue;
   AcDbExtents bounds;
   const bool hasBounds=entity->getGeomExtents(bounds)==Acad::eOk;
   if(annotation&&!hasBounds)continue;
   rows_.push_back({id,physical,dimension,hasBounds?std::optional<AcDbExtents>(bounds):std::nullopt});
  }
 }

 bool hits(const std::vector<AcGePoint3d>& path,const AcDbExtents& box){
  const auto labelHits=[&](const AcDbExtents& glyph){
   if(productGenericBoxesOverlap(box,glyph,0))return true;
   for(std::size_t k=1;k<path.size();++k){
    AcDbExtents line(path[k-1],path[k-1]);line.addPoint(path[k]);
    if(productGenericBoxesOverlap(line,glyph,0))return true;
   }
   return false;
  };
  const auto strokeHits=[&](const auto& line){return productGenericSegmentHitsBox(line.first,line.second,box,0);};
  for(auto& row:rows_){
   if(!row.physical&&!row.dimension){
    if(labelHits(*row.bounds))return true;
    continue;
   }
   if(row.physical&&row.bounds&&!productGenericBoxesOverlap(*row.bounds,box,0))continue;
   if(!row.read){
    const auto entity=open<AcDbEntity>(row.id);
    if(row.physical){
     GenericOpeningInk ink;productGenericOpeningInk(*entity,ink);
     if(ink.curved.empty())row.glyphs=std::move(ink.arcs);
     row.strokes=std::move(ink.straight);
     row.strokes.insert(row.strokes.end(),ink.curved.begin(),ink.curved.end());
    }else{
     row.glyphs=productPlanChainGlyphs(db_,*entity);
     productPlanDimensionStrokes(*entity,row.strokes);
    }
    row.read=true;
   }
   if(std::any_of(row.strokes.begin(),row.strokes.end(),strokeHits))return true;
   if(row.physical){
    if(protectUnread_&&row.bounds&&row.strokes.empty()&&row.glyphs.empty())return true;
    if(std::any_of(row.glyphs.begin(),row.glyphs.end(),[&](const auto& glyph){
     return productGenericBoxesOverlap(box,glyph,0);
    }))return true;
   }else if(std::any_of(row.glyphs.begin(),row.glyphs.end(),labelHits))return true;
  }
  return false;
 }

private:
 struct Row {
  AcDbObjectId id;
  bool physical{},dimension{};
  std::optional<AcDbExtents> bounds;
  std::vector<AcDbExtents> glyphs;
  std::vector<std::pair<AcGePoint3d,AcGePoint3d>> strokes;
  bool read{};
 };
 AcDbDatabase& db_;
 bool protectUnread_;
 std::vector<Row> rows_;
};

bool productPlanDetailHits(AcDbDatabase& db,const std::vector<AcGePoint3d>& path,const AcDbExtents& box,
 const std::set<std::string>& excluded){
 return ProductPlanDetailObstacles(db,excluded).hits(path,box);
}

std::vector<ProductPlanDetailNote> productGenerateDetailNotes(ProductJob& job,AcDbDatabase& db,
 const std::vector<bcpproduct::GenericView>& views,const std::vector<bool>& generated){
 if(!job.unifiedPlan)return {};
 std::ifstream profile(job.profile/L"plan-policy/detail-notes.tsv");
 if(!profile)return {};
 std::string tag,hash;
 require(bool(profile>>tag>>hash)&&tag=="PLANDETAILNOTES1","valid detail-note source profile");
 if(hash!=job.sourceHash)return {};
 std::vector<ProductPlanDetailNote> rows;
 std::set<std::pair<std::size_t,std::string>> issued;
 while(profile>>tag&&tag!="END"){
  ProductPlanDetailNote row;
  std::string state,styleName,text;
  double x{},y{},px{},py{};
  require((tag=="LOCAL"||tag=="GENERAL"||tag=="INDEX")&&
   bool(profile>>row.panel>>row.source>>state>>x>>y>>px>>py>>std::quoted(styleName)>>std::quoted(text))&&
   row.panel>0&&row.panel<=views.size()&&generated[row.panel-1]&&std::isfinite(x)&&std::isfinite(y)&&
   std::isfinite(px)&&std::isfinite(py)&&!styleName.empty()&&!text.empty()&&(state=="CONFIRMED"||state=="PENDING"),
   "detail note panel, source, style and placement");
  row.index=tag=="INDEX";
  row.pendingText=state=="PENDING";
  row.text=bcpproduct::decodeMessage(text)+(row.pendingText?L"（待确认）":L"");
  row.position={x,y,0};
  const auto panel=bcpproduct::nearestGenericView(views,{x,y});
  const auto markPending=[&](const std::string& issue){row.issue=issue;rows.push_back(std::move(row));};
  if(panel!=row.panel-1){markPending(u8"说明落点与指定图幅不一致");continue;}
  AcDbObjectId source;
  if(row.source!="-"){
   const auto handle=bcpproduct::decodeMessage(row.source);
   if(db.getAcDbObjectId(source,false,AcDbHandle(handle.c_str()))!=Acad::eOk||source.isErased()){
    markPending(u8"找不到该说明指向的源构件");continue;
   }
  }
  if(tag!="GENERAL"){
   if(source.isNull()||bcpproduct::nearestGenericView(views,{px,py})!=panel){markPending(u8"局部说明指向缺实体或跨幅");continue;}
   const auto entity=open<AcDbEntity>(source);
   AcDbExtents ext;
   if(entity->getGeomExtents(ext)!=Acad::eOk||px<ext.minPoint().x-.01||px>ext.maxPoint().x+.01||
      py<ext.minPoint().y-.01||py>ext.maxPoint().y+.01){markPending(u8"指向点不在该源构件几何范围");continue;}
   if(row.position.distanceTo({px,py,0})<.01){markPending(u8"说明文字和引线尖点重合");continue;}
   row.path={{px,py,0},{x,y,0}};
  }
  if(row.index){
   const auto split=text.find('/');
   if(row.pendingText||split==std::string::npos||split==0||split+1==text.size()||
      !issued.emplace(row.panel,text).second){markPending(u8"详图索引缺项目图号和唯一详图号，或索引重复");continue;}
   const auto donorPath=job.profile/L"plan-policy"/fs::u8path(styleName);
   if(!fs::is_regular_file(donorPath)){markPending(u8"详图号及目标图号已登记，缺项目原生索引原型");continue;}
   try{
    AcDbDatabase donor(false,true);
    productStatus(donor.readDwgFile(donorPath.c_str(),AcDbDatabase::kForReadAndAllShare,false),"read approved native index library");
    productStatus(donor.closeInput(true),"close index library input");
    AcDbObjectId prototype;
    {
     Working donorContext(&donor);
     for(const auto& [handle,type]:modelTypes(donor)){
      if(type!=L"TDbSymbDrawingIndex")continue;
      const auto id=find(donor,ascii(handle));
      const auto entity=open<AcDbEntity>(id);
      if(entity->isAProxy())continue;
      const auto splitLabel=row.text.find(L'/');
      const auto printed=productPlanDetailContents(*entity);
      const auto left=row.text.substr(0,splitLabel),right=row.text.substr(splitLabel+1);
      if(printed!=left+L"\n"+right&&printed!=right+L"\n"+left&&printed!=row.text)continue;
      require(prototype.isNull(),"index library has multiple identical symbol prototypes");
      prototype=id;
     }
    }
    require(!prototype.isNull(),"index library lacks a matching native symbol");
    const auto id=cloneOne(donor,prototype,db);
    try{
     const auto points=chainGrips(id);
     require(!points.empty(),"native index missing placement grip");
     {
      auto entity=editObject<AcDbEntity>(id);
      productStatus(entity->transformBy(AcGeMatrix3d::translation(row.position-points.front())),"place native detail index symbol");
      AcDbExtents ink;
      productStatus(entity->getGeomExtents(ink),"native index ink");
      require(!productPlanDetailHits(db,row.path,ink,{row.source,axisHandle(id)}),"native index or leader collides with annotations");
     }
     row.textObject=axisHandle(id);
     row.nativeIndex=true;
    }catch(...){productStatus(editObject<AcDbEntity>(id)->erase(),"remove failed native index clone");throw;}
   }catch(const std::exception& error){markPending(error.what());continue;}
  }
  const auto layer=productPlanLayer(db,L"DIM_SYMB",3);
  AcDbObjectId textId;
  if(!row.nativeIndex){
   AcDbObjectId style;
  const auto styleWide=bcpproduct::decodeMessage(styleName);
  if(open<AcDbTextStyleTable>(db.textStyleTableId())->getAt(styleWide.c_str(),style)!=Acad::eOk){
   markPending(u8"该项目说明的已确认文字样式未找到");continue;
  }
  auto label=std::make_unique<AcDbMText>();
  label->setDatabaseDefaults(&db);
  productStatus(label->setTextStyle(style),"detail note confirmed text style");
  productStatus(label->setLayer(layer),"detail note layer");
  productStatus(label->setTextHeight(3*job.unifiedScale/job.unifiedUnitMM),"detail note paper text height");
  productStatus(label->setLocation(row.position),"detail note location");
  productStatus(label->setAttachment(AcDbMText::kBottomLeft),"horizontal detail note");
  label->setContents(row.text.c_str());
  AcDbExtents textBox;
  if(label->getGeomExtents(textBox)!=Acad::eOk||productPlanDetailHits(db,row.path,textBox,{row.source})){
   markPending(u8"说明文字或引线路径与实际字形或符号相碰，需在同区另找位置");continue;
  }
   textId=productAppendPlanDetail(db,std::move(label));
   row.textObject=axisHandle(textId);
  }else textId=find(db,row.textObject);
  if(!row.path.empty()){
   try{
    auto leader=std::make_unique<AcDbLeader>();
    leader->setDatabaseDefaults(&db);
    productStatus(leader->setLayer(layer),"detail leader layer");
    require(leader->appendVertex(row.path.front())&&leader->appendVertex(row.path.back()),"actual detail leader vertices");
    leader->enableArrowHead();
    row.leaderObject=axisHandle(productAppendPlanDetail(db,std::move(leader)));
   }catch(...){productStatus(editObject<AcDbEntity>(textId)->erase(),"remove failed note pair");throw;}
  }
  rows.push_back(std::move(row));
 }
 require(tag=="END","complete detail note profile");
 profile>>std::ws;
 require(profile.eof(),"unexpected trailing detail note rows");
 return rows;
}

void productVerifyDetailNotes(ProductJob& job,AcDbDatabase& saved,const std::vector<ProductPlanDetailNote>& rows,
 const GenericNativeInput& input,const std::vector<bcpproduct::GenericView>& views){
 if(!job.unifiedPlan)return;
 Working work(&saved);
 for(const auto& row:rows){
  auto issue=row.issue;
  if(issue.empty()){
   const auto label=open<AcDbEntity>(find(saved,row.textObject));
   AcDbExtents box;
   bool good=false;
   if(row.nativeIndex){
    const auto points=chainGrips(label->objectId());
    const auto printed=productPlanDetailContents(*label);
    const auto slash=row.text.find(L'/');
    const auto left=row.text.substr(0,slash),right=row.text.substr(slash+1);
    good=std::wstring(label->isA()->name())==L"TDbSymbDrawingIndex"&&!label->isAProxy()&&!points.empty()&&
     points.front().distanceTo(row.position)<1e-5&&(printed==row.text||printed==left+L"\n"+right||printed==right+L"\n"+left);
   }else if(const auto* text=AcDbMText::cast(label.get())){
    const auto actual=productPlanDetailContents(*label);
    good=text->location().distanceTo(row.position)<1e-5&&actual==row.text;
    if(!good)issue=u8"重开文字内容或锚点不一致";
   }
   good=good&&label->getGeomExtents(box)==Acad::eOk;
   if(good&&!row.leaderObject.empty()){
    const auto entity=open<AcDbEntity>(find(saved,row.leaderObject));
    const auto* leader=AcDbLeader::cast(entity.get());
    good=leader&&leader->hasArrowHead()&&leader->numVertices()==2&&leader->firstVertex().distanceTo(row.path.front())<1e-5&&
      leader->lastVertex().distanceTo(row.path.back())<1e-5;
   }
   if(good){
    good=!productPlanDetailHits(saved,row.path,box,{row.source,row.textObject,row.leaderObject});
    if(!good)issue=u8"说明文字或引线路径与最终标注相碰";
   }
   if(!good&&issue.empty())issue=u8"独立重开后说明文字范围或引线尖点、末端未通过";
  }
  const auto result=issue.empty()?row.pendingText?"pending-design-with-drawn-notice":"verified-detail-note-layout":"pending-detail-note-layout";
  const auto fields="\"source\":"+jsonUtf8(bcpproduct::decodeMessage(row.source))+",\"leader\":\""+row.leaderObject+
   "\",\"position\":"+wholePoints({row.position})+",\"text\":"+jsonUtf8(row.text);
  if(row.index)productPlanAuditRow(job,{"B09","B10"},row.panel,row.source,"detail-index-source-and-clearance",result,issue,fields);
  else {
   productPlanAuditRow(job,{"B02","B03"},row.panel,row.textObject.empty()?row.source:row.textObject,
    "construction-note-and-leader",result,issue.empty()&&row.pendingText?u8"已出待确认图面说明；构造参数待本项目核定":issue,fields);
   if(row.pendingText)productPlanAuditRow(job,{"Q08"},row.panel,row.textObject.empty()?row.source:row.textObject,
    "confirmed-plan-review-notice",issue.empty()?"verified-confirmed-notice-visible":"pending-required-notice-layout",issue,fields);
  }
 }
 if(std::none_of(rows.begin(),rows.end(),[](const auto& row){return row.index;}))
  productPlanAuditRow(job,{"B09","B10"},0,"","confirmed-detail-index-input","pending-detail-library-association",
   u8"项目暂无详图原型、目标图号及可核指向，待对应楼梯、门廊、厨卫和设备详图确认");
 if(rows.empty()){
  for(const auto& note:input.notes)if(note.text==L"门廊"||note.text==L"厨房"||note.text==L"公卫"||
                                  note.text==L"卫生间"||note.text.find(L"通窗")!=std::wstring::npos){
   const auto panel=bcpproduct::nearestGenericView(views,{note.anchor.x,note.anchor.y})+1;
   productPlanAuditRow(job,{"B02","B03"},panel,note.handle,"construction-note-applicability",
    "pending-construction-note-instruction",u8"该处已定位，需项目指定说明模板、材料或参数及引线所指构件",
    "\"position\":"+wholePoints({note.anchor}));
  }
  productPlanAuditRow(job,{"Q08"},0,"","project-review-notice-instruction","pending-review-notice-applicability",
   u8"屋面候选待复核说明另按 A29 核；其他待确认和待调整说明需明确本项目部位及落位");
 }
}
