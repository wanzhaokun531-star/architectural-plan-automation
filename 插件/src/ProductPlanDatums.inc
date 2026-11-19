struct ProductPlanDatum {
 std::size_t panel{};
 std::string object;
 AcGePoint3d point;
 std::wstring display;
 std::string issue;
 std::string donorName, donorHash, donorHandle;
 std::string sourceMark;
 std::optional<double> confirmedMetres;
 std::vector<std::string> storyMarks;
};

struct ProductPlanStairBounds {
 std::string object;
 AcDbExtents bounds;
};

std::vector<ProductPlanStairBounds> productPlanStairBounds(AcDbDatabase& db){
 std::vector<ProductPlanStairBounds> stairs;
 for(const auto& [handle,type]:modelTypes(db)){
  if(type!=L"TDbLineStair"&&type!=L"TDbRectStair"&&type!=L"TDbMultiStair"&&type!=L"TDbArcStair")continue;
  const auto entity=open<AcDbEntity>(find(db,ascii(handle)));
  AcDbExtents box;
  if(productPlanDimensionVisible(*entity)&&entity->getGeomExtents(box)==Acad::eOk)
   stairs.push_back({ascii(handle),box});
 }
 return stairs;
}

bool productPlanPointInXY(const AcGePoint3d& point,const AcDbExtents& bounds){
 const auto lo=bounds.minPoint(),hi=bounds.maxPoint();
 return point.x>=lo.x&&point.x<=hi.x&&point.y>=lo.y&&point.y<=hi.y;
}

std::optional<double> productPlanLevelNumber(std::wstring text){
 const auto start=text.find_first_not_of(L" \t　");
 if(start==std::wstring::npos)return {};
 text.erase(0,start);
 if(text.rfind(L"%%P",0)==0||text.rfind(L"%%p",0)==0)text.replace(0,3,L"+");
 if(text.front()==L'±')text.front()=L'+';
 if(text.front()==L'−')text.front()=L'-';
 const auto end=text.find_last_not_of(L" \t　");
 text.resize(end+1);
 std::wistringstream in(text);
 in.imbue(std::locale::classic());
 double value{};
 wchar_t trailing{};
 if(!(in>>value)||!std::isfinite(value)||(in>>trailing))return {};
 return value;
}

std::optional<AcGePoint3d> productPlanDatumLocation(const ProductJob& job,AcDbDatabase& db,
 AcDbObjectId datum,const AcGePoint3d& tip,const AcGePoint3d& preferred,
 const ProductRoomGrid& roomGrid,const bcpproduct::GenericView& view){
 AcDbExtents ink;
 {
  const auto entity=open<AcDbEntity>(datum);
  status(entity->getGeomExtents(ink),"measure native floor datum ink");
 }
 const double paper=job.unifiedScale/job.unifiedUnitMM;
 const AcGeVector3d clearance(.35*paper,.35*paper,0);
 const auto low=ink.minPoint()-tip-clearance,high=ink.maxPoint()-tip+clearance;
 const auto inkCentre=(low+high)*.5;
 const auto& bounds=view.bounds;
 const double step=std::max({2.*paper,(bounds.xmax-bounds.xmin)/100.,(bounds.ymax-bounds.ymin)/100.});
 std::vector<AcGePoint3d> locations{preferred};
 for(double y=bounds.ymin+step/2.;y<bounds.ymax;y+=step)
  for(double x=bounds.xmin+step/2.;x<bounds.xmax;x+=step)
   if(roomGrid.at(x,y)>=0)locations.push_back(AcGePoint3d(x,y,0)-inkCentre);
 for(std::size_t y=0;y<roomGrid.height();++y)
  for(std::size_t x=0;x<roomGrid.width();++x)
   if(roomGrid.cells[roomGrid.index(x,y)]>=0)locations.push_back(roomGrid.centre(x,y)-inkCentre);
 std::stable_sort(locations.begin(),locations.end(),[&](const auto& a,const auto& b){
  return (a-preferred).lengthSqrd()<(b-preferred).lengthSqrd();
 });
 auto excluded=job.planDimensionExclusions;
 excluded.insert(axisHandle(datum));
 const auto priority=productReadSourceTextPriorityInk(job,db,{});
 for(const auto& row:priority)excluded.insert(row.handle);
 ProductPlanDetailObstacles obstacles(db,excluded,true);
 for(const auto& at:locations){
  const AcDbExtents box(at+low,at+high);
  if(!productSourceTextInsideRoom(roomGrid,roomGrid.at(at),box))continue;
  if(obstacles.hits({},box))continue;
  if(std::any_of(priority.begin(),priority.end(),[&](const auto& row){
   return productSourceTextInkHit(row,box,0.);
  }))continue;
  return at;
 }
 return {};
}

std::vector<ProductPlanDatum> productGeneratePlanDatums(ProductJob& job,AcDbDatabase& db,
 const GenericNativeInput& input,const std::vector<bcpproduct::GenericView>& views,
 const std::vector<bool>& generated,const std::vector<std::optional<int>>& floors,
 const std::vector<std::optional<ProductRoomGrid>>& roomGrids,
 std::vector<bcpproduct::PlanLevelResult>& levels,bool readOnly=false){
 if(!job.unifiedPlan)return {};
 std::vector<ProductPlanDatum> rows(views.size());
 std::vector<bcpproduct::PlanLevelInput> data(views.size());
 const auto stairs=productPlanStairBounds(db);
 const auto profilePath=job.unifiedFloorLevels.empty()?job.profile/L"plan-policy/plan-datums.tsv":job.unifiedFloorLevels;
 std::map<std::string,std::size_t> localMarks;
 {
  std::ifstream marked(profilePath);
  std::string tag,digest,line;
  if(marked>>tag>>digest&&tag=="PLANDATUM1"&&digest==job.sourceHash){
   while(std::getline(marked,line)){
    std::istringstream fields(line);
    if(!(fields>>tag)||tag!="LOCAL")continue;
    std::string handle,extra;
    std::size_t panel{};
    require(bool(fields>>panel>>handle)&&panel>0&&panel<=views.size()&&!(fields>>extra)&&
            localMarks.emplace(handle,panel).second,"unique source-bound local elevation");
   }
  }
 }
 std::ostringstream evidence;
 evidence<<"{\"sourceSHA256\":\""<<job.sourceHash<<"\",\"marks\":[";
 bool firstMark=true;
 for(std::size_t i=0;i<rows.size();++i){
  rows[i].panel=i+1;
  if(generated[i])data[i].floor=floors[i];
 }
 const auto within=[&](std::size_t i,const AcGePoint3d& point){
  const auto& bounds=views[i].bounds;
  return point.x>=bounds.xmin&&point.x<=bounds.xmax&&point.y>=bounds.ymin&&point.y<=bounds.ymax;
 };
 const auto heights=[&](const std::wstring& text,std::size_t i,const std::string& key){
  static const std::wregex story(LR"(^\s*(?:本层层高|楼层高度|层高)\s*[:：=]?\s*([0-9]+(?:\.[0-9]+)?)\s*(mm|m|毫米|米)?\s*$)",std::regex::icase);
  std::wsmatch match;
  if(!std::regex_match(text,match,story))return;
  auto n=productPlanLevelNumber(match[1].str());
  if(!n||*n<=0)return;
  const auto unit=match[2].str();
  if(unit==L"m"||unit==L"M"||unit==L"米"||(unit.empty()&&*n<30.))*n*=1000.;
  auto& row=data[i];
  if(row.explicitHeightMM&&std::abs(*row.explicitHeightMM-*n)>.01)row.heightConflict=true;
  else row.explicitHeightMM=n;
  rows[i].storyMarks.push_back(key);
 };
 for(const auto& [handle,type]:modelTypes(db)){
  if(type!=L"TDbSymbElevation"&&type!=L"TDbText"&&type!=L"AcDbText"&&type!=L"AcDbMText")continue;
  const auto key=ascii(handle);
  const auto e=open<AcDbEntity>(find(db,key));
  if(e->isAProxy()||!productPlanDimensionVisible(*e))continue;
  AcDbExtents box;
  if(e->getGeomExtents(box)!=Acad::eOk)continue;
  const auto at=box.minPoint()+(box.maxPoint()-box.minPoint())*.5;
  const auto i=bcpproduct::nearestGenericView(views,{at.x,at.y});
  if(!generated[i]||!within(i,at))continue;
  if(type==L"TDbSymbElevation"){
   try{
    const auto text=productNativeFloorText(e->objectId());
    const auto dot=text.find(L'.');
    const auto value=productPlanLevelNumber(text);
    const auto g=chainGrips(e->objectId());
    const bool precision=dot!=std::wstring::npos&&(text.size()-dot==4||
     (floors[i]&&*floors[i]==1&&text.size()-dot==3));
    if(!value||g.empty()||!within(i,g.front()))continue;
    const auto stair=std::find_if(stairs.begin(),stairs.end(),[&](const auto& item){
      return productPlanPointInXY(g.front(),item.bounds);
    });
    const auto local=localMarks.find(key);
    const bool confirmedLocal=local!=localMarks.end()&&local->second==i+1;
    if(confirmedLocal)localMarks.erase(local);
    const bool floorCandidate=!confirmedLocal&&stair==stairs.end()&&precision&&floors[i]&&
      (*floors[i]==1?std::abs(*value)<=.00001:*floors[i]<1?*value<0.:*value>0.);
    if(!firstMark)evidence<<',';
    firstMark=false;
    evidence<<"{\"panel\":"<<i+1<<",\"object\":\""<<key<<"\",\"valueMetres\":"<<std::setprecision(12)<<*value
      <<",\"tip\":"<<wholePoints({g.front()})<<",\"role\":\""
      <<(confirmedLocal?"confirmed-local":stair!=stairs.end()?"stair-local":floorCandidate?"floor-candidate":"other-local")<<"\"";
    if(stair!=stairs.end())evidence<<",\"stair\":\""<<stair->object<<"\"";
    evidence<<'}';
    if(!floorCandidate)continue;
    auto& row=rows[i];
    if(data[i].finishedMetres&&std::abs(*data[i].finishedMetres-*value)>.00001)data[i].datumConflict=true;
    else if(!data[i].finishedMetres){
     data[i].finishedMetres=value;
     row.point=g.front();
     row.sourceMark=key;
    }
   }catch(const std::exception&){}
  }else if(const auto* text=AcDbText::cast(e.get()))heights(text->textString(),i,key);
  else if(const auto* mtext=AcDbMText::cast(e.get())){
   AcString plain;
   if(mtext->text(plain)==Acad::eOk)heights(plain.kACharPtr(),i,key);
  }else if(type==L"TDbText"){
   const auto p=input.properties.find(key);
   if(p!=input.properties.end()&&p->second.count(L"文字内容"))heights(p->second.at(L"文字内容"),i,key);
  }
 }
 std::map<std::size_t,double> confirmedProjectHeights;
 std::ifstream profile(profilePath);
 std::string tag,sourceHash;
 if(profile)require(bool(profile>>tag>>sourceHash)&&tag=="PLANDATUM1","valid floor datum source profile");
 if(!job.unifiedFloorLevels.empty()&&(!profile||sourceHash!=job.sourceHash))
  job.detailPending.push_back({u8"楼层参数",u8"项目",u8"传入楼层参数文件与当前源 DWG 指纹不符或不可读，本次按图纸证据核算"});
 if(profile&&sourceHash==job.sourceHash){
  while(profile>>tag&&tag!="END"){
   std::size_t panel{};
   double x{},y{},metres{};
   std::string donorName,donorHash,handle;
   if(tag=="LOCAL"){
    require(bool(profile>>panel>>handle),"read source-bound local elevation");
    continue;
   }
   if(tag=="HEIGHT"){
    require(bool(profile>>panel>>metres)&&panel>0&&panel<=views.size()&&std::isfinite(metres)&&metres>0.,
            "confirmed storey height in metres");
    auto& dataRow=data[panel-1];
    const double mm=metres*1000.;
    const auto prior=confirmedProjectHeights.find(panel);
    if(prior!=confirmedProjectHeights.end()&&std::abs(prior->second-mm)>.01)dataRow.heightConflict=true;
    else{
     confirmedProjectHeights[panel]=mm;dataRow.explicitHeightMM=mm;
     if(!dataRow.heightConfirmed)dataRow.heightConflict=false;
     dataRow.heightConfirmed=true;
    }
    continue;
   }
   require(tag=="LEVEL"&&bool(profile>>panel>>x>>y>>metres>>std::quoted(donorName)>>donorHash>>handle)&&
           panel>0&&panel<=views.size()&&std::isfinite(x)&&std::isfinite(y)&&std::isfinite(metres),
           "confirmed finished floor, drawing point and native prototype");
   if(!generated[panel-1])continue;
   auto& row=rows[panel-1];
   if(row.confirmedMetres||(!data[panel-1].datumConflict&&data[panel-1].finishedMetres&&
      std::abs(*data[panel-1].finishedMetres-metres)>.00001)){
    data[panel-1].datumConflict=true;
    row.issue=u8"同幅有不同楼面完成面依据";
    continue;
   }
   row.confirmedMetres=metres;
   data[panel-1].finishedMetres=metres;
   data[panel-1].datumConflict=false;
   row.donorName=donorName;row.donorHash=donorHash;row.donorHandle=handle;
   if(row.sourceMark.empty())row.point={x,y,0};
  }
  require(tag=="END","complete floor datum profile");
  profile>>std::ws;
  require(profile.eof(),"unexpected trailing floor datum input");
 }
 for(const auto& [handle,panel]:localMarks)
  job.detailPending.push_back({u8"局部标高",handle,u8"未在指定图幅读回已核局部标高"});
 const auto defaultHeight=job.ruleCatalog?job.ruleCatalog->defaultStoreyHeightMM:std::nullopt;
 levels=bcpproduct::resolvePlanLevels(data,defaultHeight);
 evidence<<"],\"stairs\":[";
 for(std::size_t i=0;i<stairs.size();++i){
  const auto& stair=stairs[i];
  if(i)evidence<<',';
  evidence<<"{\"object\":\""<<stair.object<<"\",\"bounds\":"
   <<wholePoints({stair.bounds.minPoint(),stair.bounds.maxPoint()})<<'}';
 }
 evidence<<"]}";
 writeNew(job.root/L"floor-datum-source-roles.json",evidence.str());
 if(readOnly)return rows;
 for(std::size_t i=0;i<rows.size();++i)if(generated[i]){
   auto& row=rows[i];
   const auto& level=levels[i];
   if(!level.finishedMetres){
    row.issue=data[i].datumConflict?u8"楼面依据冲突，保留原标注":u8"缺可连续对应的楼层及统一完成面零点";
    continue;
   }
   const double metres=*level.finishedMetres;
   std::wostringstream display;
   display.imbue(std::locale::classic());
   display<<std::showpos<<std::fixed<<std::setprecision(3)<<(std::abs(metres)<.0000001?0.:metres);
   row.display=display.str();
   if(row.confirmedMetres&&!within(i,row.point)){
    row.issue=u8"已给标高点在所属图幅外";continue;
   }
   for(const auto& [existing,type]:modelTypes(db)){
    if(row.sourceMark.empty()&&!row.confirmedMetres)break;
    if(type!=L"TDbSymbElevation")continue;
    const auto id=find(db,ascii(existing));
    const auto grips=chainGrips(id);
    if(grips.empty()||grips.front().distanceTo(row.point)*job.unifiedUnitMM>.01)continue;
    try{
     const auto actual=productNativeFloorText(id);
     const auto value=productPlanLevelNumber(actual);
     if(value&&std::abs(*value-metres)<=.0000001){
      row.object=ascii(existing);
      const auto dot=actual.find(L'.');
      if(metres>0.&&dot!=std::wstring::npos&&actual.size()-dot==4&&actual.front()!=L'-')
       row.display=actual;
      if(actual!=row.display)
       row.issue=u8"同一尖点已有原生标高，三位带符号格式或图层待核";
      else if(symbolName(open<AcDbEntity>(id)->layerId())!=L"DIM_ELEV"){
       const auto layer=productPlanLayer(db,L"DIM_ELEV",3);
       if(editObject<AcDbEntity>(id)->setLayer(layer)!=Acad::eOk)
        row.issue=u8"同一尖点原生标高图层修正失败";
      }
     }else row.issue=u8"同一尖点既有原生标高与已核完成面数值不符";
    }catch(const std::exception&){row.issue=u8"同一尖点已有不可读原生标高，保留原对象";}
    break;
   }
   if(!row.object.empty()&&row.issue.empty())continue;
   const bool replacing=!row.sourceMark.empty();
   if(!row.issue.empty()&&!replacing)continue;
   const auto shared=job.profile/L"floor-datum-style.dwg";
   const auto stylePath=row.donorName.empty()?shared:profilePath.parent_path()/fs::u8path(row.donorName);
   if(!row.confirmedMetres&&!replacing){
    const auto& bounds=views[i].bounds;
    const auto anchor=AcGePoint3d((bounds.xmin+bounds.xmax)/2,(bounds.ymin+bounds.ymax)/2,0);
    row.point=anchor;
    if(row.storyMarks.size()==1){
     const auto old=open<AcDbEntity>(find(db,row.storyMarks.front()));
     if(const auto point=productPlanNameAnchor(*old);point&&within(i,*point))row.point=*point;
    }
   }
   try{
    if(!row.donorName.empty())require(sha(stylePath)==row.donorHash,"native datum resource fingerprint");
    AcDbDatabase donor(false,true);
    productStatus(donor.readDwgFile(stylePath.c_str(),AcDbDatabase::kForReadAndAllShare,false),"read native floor datum style");
    productStatus(donor.closeInput(true),"close native floor datum input");
    AcDbObjectId prototype;
    std::optional<double> sourceValue;
    {
     Working work(&donor);
     if(!row.donorHandle.empty())prototype=find(donor,row.donorHandle);
     else for(const auto& [handle,type]:modelTypes(donor))if(type==L"TDbSymbElevation"){
      try{
       const auto candidate=find(donor,ascii(handle));
       if(productNativeFloorText(candidate)==row.display){prototype=candidate;break;}
      }catch(const std::exception&){}
     }
     require(!prototype.isNull(),"no usable native floor datum template");
     sourceValue=productPlanLevelNumber(productNativeFloorText(prototype));
     require(sourceValue.has_value(),"datum template has no numeric value");
    }
    const auto id=cloneOne(donor,prototype,db);
    try{
     const auto grips=chainGrips(id);
     require(!grips.empty(),"native floor datum has no actual tip");
     const auto layer=productPlanLayer(db,L"DIM_ELEV",3);
     require(std::abs(metres-*sourceValue)<=.0000001,"native elevation template has a different value");
     const auto text=productNativeFloorText(id);
     require(text==row.display,
             "native floor datum value or formatting differs");
     row.display=text;
     const auto current=chainGrips(id);
     require(!current.empty(),"edited datum tip is unreadable");
     if(!row.confirmedMetres&&!replacing){
      std::optional<ProductRoomGrid> wallGrid;
      if(!roomGrids[i]){
       std::vector<std::array<double,4>> walls;
       for(const auto index:views[i].walls)
        if(input.walls[index].orthogonal&&!input.walls[index].low)walls.push_back(approvedRect(input.walls[index].wall));
       wallGrid=productRoomGrid(walls);
      }
      const auto* grid=roomGrids[i]?&*roomGrids[i]:(wallGrid?&*wallGrid:nullptr);
      require(grid,"no verified interior position for floor datum");
      const auto clear=productPlanDatumLocation(job,db,id,current.front(),row.point,*grid,views[i]);
      require(clear.has_value(),"no clear interior position for floor datum");
      row.point=*clear;
     }
     {
      auto entity=editObject<AcDbEntity>(id);
      productStatus(entity->transformBy(AcGeMatrix3d::translation(row.point-current.front())),"place native finished floor datum");
      productStatus(entity->setLayer(layer),"set finished floor datum layer");
      productStatus(entity->setColorIndex(256),"set finished floor datum ByLayer");
     }
     if(replacing)status(editObject<AcDbEntity>(find(db,row.sourceMark))->erase(),"replace source datum after native readback");
     row.object=axisHandle(id);
     row.issue.clear();
    }catch(...){productStatus(editObject<AcDbEntity>(id)->erase(),"remove failed datum clone");throw;}
   }catch(const std::exception& error){row.issue=error.what();}
  }
 rows.erase(std::remove_if(rows.begin(),rows.end(),[&](const auto& row){return !generated[row.panel-1];}),rows.end());
 return rows;
}

void productVerifyPlanDatums(ProductJob& job,AcDbDatabase& saved,const std::vector<ProductPlanDatum>& rows){
 Working work(&saved);
 for(const auto& row:rows){
  auto reason=row.issue;
  std::wstring actual;
  if(!row.object.empty()){
   const auto id=find(saved,row.object);
   const auto grips=chainGrips(id);
   actual=productNativeFloorText(id);
   if(reason.empty()&&(grips.empty()||grips.front().distanceTo(row.point)*job.unifiedUnitMM>.01||
      actual!=row.display||symbolName(open<AcDbEntity>(id)->layerId())!=L"DIM_ELEV"))
    reason=u8"独立重开未证实真实标高类型、尖点、显示值或图层";
  }
  const auto result=reason.empty()?"verified-finished-floor-datum":"pending-finished-floor-datum";
  const auto evidence="{\"panel\":"+std::to_string(row.panel)+",\"object\":\""+row.object+
   "\",\"position\":"+wholePoints({row.point})+",\"display\":"+jsonUtf8(row.display)+",\"actualDisplay\":"+jsonUtf8(actual)+
   ",\"savedReopened\":true,\"result\":\""+result+"\",\"detail\":"+jsonUtf8(bcpproduct::decodeMessage(reason))+"}";
  productRecordRuleEvidence(job,"B04",evidence);
  productRecordRuleEvidence(job,"SE02",evidence);
  productCheckPlanSource(job,row.panel,"floor-datum","",reason.empty()?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::pending,
   row.object.empty()?std::vector<std::string>{}:std::vector<std::string>{row.object},reason);
  if(!reason.empty())job.detailPending.push_back({u8"楼面标高",row.object.empty()?std::to_string(row.panel)+u8"图幅":row.object,reason});
 }
}
