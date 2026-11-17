constexpr wchar_t productEntranceStepApp[] = L"PLAN_ENTRANCE_STEP";

struct ProductEntranceSetting {
 std::string kind, state, side;
 double low{}, high{};
 std::optional<double> front, upper, lower;
 int approvedRises{};
 std::vector<std::string> existingLines;
};

std::map<std::string,ProductEntranceSetting> productEntranceSettings(const fs::path& path,const std::string& sourceHash){
 if(path.empty()||!fs::exists(path))return {};
 std::ifstream file(path);
 std::string tag,hash,line;
 require(bool(file>>tag>>hash)&&tag=="PLANENTRANCE1"&&hash==sourceHash,"entrance profile must match source DWG");
 std::getline(file,line);
 const auto number=[](const std::string& text)->std::optional<double>{
  if(text=="-")return {};
  std::size_t end{};const auto value=std::stod(text,&end);
  require(end==text.size()&&std::isfinite(value),"actual entrance parameter");return value;
 };
 std::map<std::string,ProductEntranceSetting> settings;
 while(std::getline(file,line)){
  if(line=="END")return settings;
  std::istringstream row(line);
  std::string handle,front,upper,lower;
  ProductEntranceSetting value;
  require(bool(row>>tag>>handle>>value.kind>>value.state>>value.side>>value.low>>value.high>>front>>upper>>lower)&&tag=="ENTRY"&&
          (value.kind=="MAIN"||value.kind=="OTHER")&&
          (value.state=="NEEDED"||value.state=="CANDIDATE"||value.state=="FLAT"||value.state=="EXISTING"||value.state=="REVIEW")&&
          value.side.size()==1&&std::string("SNWE").find(value.side)!=std::string::npos&&
          std::isfinite(value.low)&&std::isfinite(value.high)&&value.low<value.high,"valid entrance profile row");
  std::string field;
  if(row>>field){
   if(field=="RISES"){
    require(value.state=="NEEDED"&&bool(row>>value.approvedRises)&&value.approvedRises==3,"approved straight stair rise count");
    row>>std::ws;require(row.eof(),"unexpected entrance profile fields");
   }else{
    require(field=="LINES"&&value.state=="EXISTING","existing source tread evidence");
    std::string lineHandle;
    while(row>>lineHandle){
     if(lineHandle=="RISES"){
      require(bool(row>>value.approvedRises)&&value.approvedRises>0,"confirmed existing stair rise count");
      row>>std::ws;
      require(row.eof(),"unexpected existing stair fields");
      break;
     }
     value.existingLines.push_back(lineHandle);
    }
    require(!value.existingLines.empty(),"existing source tread handles");
   }
  }
  value.front=number(front);value.upper=number(upper);value.lower=number(lower);
  require(settings.emplace(handle,value).second,"duplicate entrance profile row");
 }
 throw std::runtime_error("entrance profile is missing END");
}

std::map<std::string,int> productConfirmedEntranceSides(const ProductJob& job,bool mainOnly){
 const auto path=job.unifiedEntranceSteps.empty()?job.profile/L"plan-policy/entrance-steps.tsv":job.unifiedEntranceSteps;
 std::map<std::string,int> sides;
 for(const auto& [handle,entry]:productEntranceSettings(path,job.sourceHash))
  if(!mainOnly||entry.kind=="MAIN")sides.emplace(handle,static_cast<int>(std::string("WESN").find(entry.side)));
 return sides;
}

std::map<std::string,int> productConfirmedMainEntranceSides(const ProductJob& job){
 return productConfirmedEntranceSides(job,true);
}

struct ProductEntranceReceipt {
 std::string door;
 std::wstring number;
 std::string kind,status,missing,side;
 AcGePoint3d anchor;
 std::size_t panel{};
 double unit{},scale{},width{},platform{},tread{},total{};
 std::optional<double> upper,lower;
 int riseCount{};
 std::string verticalNote;
 std::wstring verticalText;
 bool swingChecked{},existingPlatform{};
 std::size_t outwardLeaves{};
 std::vector<std::pair<std::string,std::vector<AcGePoint3d>>> geometry;
 std::vector<std::pair<std::string,std::vector<double>>> dimensions;
 std::vector<std::pair<std::string,std::vector<AcGePoint3d>>> existingLines;
 std::vector<AcGePoint3d> doorGrips;
 std::vector<bcpproduct::PlanStepSwing> doorArcs;
 std::vector<double> treadDepths;
};

void productEntranceArcs(const AcDbEntity& entity,std::vector<bcpproduct::PlanStepSwing>& arcs,int depth=0){
 if(depth>7)return;
 const auto point=[](const AcGePoint3d& p){return bcpproduct::PlanStepPoint{p.x,p.y};};
 if(const auto* arc=AcDbArc::cast(&entity)){
  AcGePoint3d a,b;
  if(std::abs(arc->normal().z)>.99&&arc->getStartPoint(a)==Acad::eOk&&arc->getEndPoint(b)==Acad::eOk)
   arcs.push_back({point(arc->center()),point(a),point(b),arc->normal().z>0});
  return;
 }
 if(AcDbText::cast(&entity)||AcDbMText::cast(&entity)||AcDbLine::cast(&entity)||AcDbHatch::cast(&entity))return;
 TemplateParts parts(&entity);
 if(entity.explode(parts.values)!=Acad::eOk)return;
 for(int i=0;i<parts.values.length();++i)if(const auto* child=AcDbEntity::cast(static_cast<AcRxObject*>(parts.values[i])))
  productEntranceArcs(*child,arcs,depth+1);
}

std::string productEntranceMarker(const AcDbEntity& entity){
 auto* data=entity.xData(productEntranceStepApp);
 if(!data)return {};
 const auto* field=data->rbnext;
 const auto* value=field&&field->restype==AcDb::kDxfXdHandle?field->resval.rstring:nullptr;
 const std::string result=value?ascii(value):std::string{};
 acutRelRb(data);return result;
}

std::string productAppendEntranceEntity(AcDbDatabase& db,std::unique_ptr<AcDbEntity> entity,
 const std::string& handle,AcDbObjectId layer){
 {auto table=open<AcDbRegAppTable>(db.regAppTableId());if(!table->has(productEntranceStepApp)){
   table.reset();auto write=editObject<AcDbRegAppTable>(db.regAppTableId());
   auto app=std::make_unique<AcDbRegAppTableRecord>();status(app->setName(productEntranceStepApp),"name stair application");
   AcDbObjectId id;status(write->add(id,app.get()),"register stair application");
   status(app.release()->close(),"close stair application");
 }}
 status(entity->setLayer(layer),"entrance step layer");
 const std::wstring owner(handle.begin(),handle.end());
 auto model=editObject<AcDbBlockTableRecord>(modelId(db));
 AcDbObjectId id;status(model->appendAcDbEntity(id,entity.get()),"append entrance step entity");
 auto* native=entity.release();
 auto* data=acutBuildList(AcDb::kDxfRegAppName,productEntranceStepApp,AcDb::kDxfXdHandle,owner.c_str(),RTNONE);
 if(!data){native->erase();native->close();throw std::runtime_error("allocate entrance step link");}
 const auto linked=native->setXData(data);acutRelRb(data);
 if(linked!=Acad::eOk)native->erase();
 status(native->close(),"close entrance step entity");status(linked,"link entrance step to door");
 return axisHandle(id);
}

std::string productEntranceStroke(AcDbDatabase& db,const std::string& door,AcDbObjectId layer,
 const std::vector<AcGePoint3d>& points,bool dashed){
 auto line=std::make_unique<AcDbPolyline>(static_cast<unsigned>(points.size()));
 line->setDatabaseDefaults(&db);
 if(dashed){
  auto table=open<AcDbLinetypeTable>(db.linetypeTableId());
  AcDbObjectId dashedType;
  if(table->getAt(L"DASHED",dashedType)!=Acad::eOk){
   table.reset();db.loadLineTypeFile(L"DASHED",L"acadiso.lin");
   table=open<AcDbLinetypeTable>(db.linetypeTableId());
  }
  if(table->getAt(L"DASHED",dashedType)==Acad::eOk)status(line->setLinetype(dashedType),"candidate step dashed line");
 }
 for(unsigned i=0;i<points.size();++i)status(line->addVertexAt(i,{points[i].x,points[i].y}),"step vertex");
 return productAppendEntranceEntity(db,std::move(line),door,layer);
}

struct ProductEntranceSide {int side{-1};bool enclosedInterior{};};

ProductEntranceSide productEntranceSideFromDomains(const GenericNativeOpening& opening,const GenericOpeningHost& host,
 const GenericNativeInput& input,const ProductRoomGrid& grid,double unit){
 ProductEntranceSide result{host.side,false};
 const auto centre=opening.a+(opening.b-opening.a)*.5;
 const auto rect=approvedRect(input.walls[host.wall].wall);
 const auto sample=2./unit;
 const auto negative=host.vertical?grid.at(rect[0]-sample,centre.y):grid.at(centre.x,rect[1]-sample);
 const auto positive=host.vertical?grid.at(rect[2]+sample,centre.y):grid.at(centre.x,rect[3]+sample);
 if(negative>=0&&positive>=0){
  const auto outdoor=[&](int region){
   return std::any_of(input.notes.begin(),input.notes.end(),[&](const auto& note){
    constexpr std::array<const wchar_t*,4> spaces{L"天井",L"庭院",L"露台",L"阳台"};
    return std::any_of(spaces.begin(),spaces.end(),[&](const auto* text){return note.text==text;})&&
     grid.at(note.anchor.x,note.anchor.y)==region;
   });
  };
  const bool n=outdoor(negative),p=outdoor(positive);
  if(n!=p)result.side=host.vertical?(n?0:1):(n?2:3);
  else result.enclosedInterior=true;
 }
 if(negative==ProductRoomGrid::outside&&positive>=0)result.side=host.vertical?0:2;
 if(positive==ProductRoomGrid::outside&&negative>=0)result.side=host.vertical?1:3;
 return result;
}

std::vector<ProductEntranceReceipt> productEntranceSteps(ProductJob& job,AcDbDatabase& db,
 const GenericNativeInput& input,const std::vector<bcpproduct::GenericView>& views,double unit,
 const fs::path& profile={},const std::string& sourceHash={},AcDbDatabase* materials=nullptr,double scale=100.,
 const std::vector<bool>* active=nullptr){
 Working work(&db);
 require(!active||active->size()==views.size(),"entrance scope must match the source panels");
 const auto inScope=[&](std::size_t panel){return !active||(*active)[panel];};
 const auto settings=productEntranceSettings(profile,sourceHash);
 std::map<std::size_t,AcDbObjectId> prototypes;
 const auto inventoryDimensions=[&](AcDbDatabase& drawing,bool bank){
  Working scope(&drawing);
  for(const auto& [handle,type]:modelTypes(drawing)){
   if(type!=L"TDbDimension2")continue;
   const auto id=find(drawing,ascii(handle));
   const auto e=open<AcDbEntity>(id);
   if(e->visibility()!=AcDb::kVisible||(!bank&&symbolName(e->layerId())!=L"PUB_DIM"))continue;
   const auto texts=productDimensionTexts(*e);
   if(texts.empty()||std::any_of(texts.begin(),texts.end(),[&](const auto& text){
    if(text.style.isNull())return true;
    const auto name=symbolName(text.style);
    return !productDimensionFamily(name,job.dimensionStylePolicy.name)||
           !productDimensionRatioMatches(name,job.dimensionStylePolicy.name,scale);
   }))continue;
   const auto size=chainValues(id).size();
   if(size>=1&&size<=3&&chainGrips(id).size()==2*size+6)prototypes.emplace(size,id);
  }
 };
 const auto fillPrototypes=[&](){
  if(!prototypes.empty())return;
  inventoryDimensions(db,false);
  if(materials)inventoryDimensions(*materials,true);
 };
 const auto annotateExisting=[&](ProductEntranceReceipt& row,const std::vector<AcGePoint3d>& points,double offset){
  if(points.size()==2){
   for(const auto& [handle,type]:modelTypes(db)){
    if(type!=L"TDbDimension2")continue;
    const auto id=find(db,ascii(handle));
    auto dimension=open<AcDbEntity>(id);
    if(!productPlanDimensionVisible(*dimension)||symbolName(dimension->layerId())!=L"PUB_DIM")continue;
    dimension.reset();
    const auto grips=chainGrips(id);
    if(grips.size()!=8)continue;
    const double tolerance=.01/unit;
    const bool same=grips[2].distanceTo(points[0])<tolerance&&grips[3].distanceTo(points[1])<tolerance;
    const bool reverse=grips[3].distanceTo(points[0])<tolerance&&grips[2].distanceTo(points[1])<tolerance;
    if(!same&&!reverse)continue;
    const auto direction=(points[1]-points[0]).normal();
    const AcGeVector3d normal(-direction.y,direction.x,0);
    const auto delta=normal*(offset-(grips[0]-points[0]).dotProduct(normal));
    moveDimensionGrip(id,0,grips[0]+delta);
    const auto current=chainGrips(id);
    const auto textHeight=std::abs((grips[4]-grips[0]).dotProduct(normal));
    moveDimensionGrip(id,4,points[0]+(points[1]-points[0])*.5+normal*(offset+
       (std::abs(direction.x)>std::abs(direction.y)?1.:-1.)*textHeight));
    require(current[2].distanceTo(grips[2])<tolerance&&current[3].distanceTo(grips[3])<tolerance,
            "retained stair dimension stations changed");
    const auto values=chainValues(id);
    require(values.size()==1&&std::abs(values[0]-points[0].distanceTo(points[1]))<tolerance,
            "retained stair dimension measurement changed");
    row.dimensions.emplace_back(ascii(handle),std::vector<double>{values[0]*unit});
    return;
   }
  }
  fillPrototypes();
  const auto prototype=prototypes.find(1);
  if(prototype==prototypes.end()){
   row.missing+=u8"；缺台阶原生尺寸原型";
   return;
  }
  auto donor=prototype->second;
  const bool copied=donor.database()!=&db;
  if(copied)donor=cloneOne(*donor.database(),donor,db);
  const auto id=points.size()==2?productDetailChain(db,donor,points,offset):
                               productGenericChain(job,db,donor,points,offset);
  {auto entity=editObject<AcDbEntity>(id);
   productStatus(entity->setLayer(productPlanLayer(db,L"PUB_DIM",25)),"existing entrance detail dimensions");
   productStatus(entity->setColorIndex(256),"existing entrance detail ByLayer");}
  auto measurements=chainValues(id);
  for(auto& value:measurements)value*=unit;
  row.dimensions.emplace_back(axisHandle(id),std::move(measurements));
  if(copied)productStatus(editObject<AcDbEntity>(donor)->erase(),"retire existing entrance detail prototype");
 };
 std::set<std::size_t> firstFloors;
 // Use the same actual floor-name recognition as source panel ownership.
 // Ordinary title text is in notes; ignoring it incorrectly treats upper-floor doors as site entrances.
 for(const auto* labels:{&input.titles,&input.notes})for(const auto& title:*labels)
  if(bcpproduct::explicitPlanFloor(title.text)==std::optional<int>{1}){
   const auto panel=bcpproduct::nearestGenericView(views,{title.anchor.x,title.anchor.y});
   if(inScope(panel))firstFloors.insert(panel);
  }
 for(const auto& opening:input.openings)if(settings.count(opening.handle)){
  const auto center=opening.a+(opening.b-opening.a)*.5;
  const auto panel=bcpproduct::nearestGenericView(views,{center.x,center.y});
  require(inScope(panel),"entrance profile references a door outside the selected plan scope");
  firstFloors.insert(panel);
 }
 if(views.size()==1&&firstFloors.empty()&&inScope(0))firstFloors.insert(0);
 std::vector<std::optional<ProductRoomGrid>> grids(views.size());
 for(const auto panel:firstFloors){
  std::vector<std::array<double,4>> rectangles;
  for(const auto wall:views[panel].walls)if(!input.walls[wall].low&&input.walls[wall].orthogonal)
   rectangles.push_back(approvedRect(input.walls[wall].wall));
  grids[panel]=productRoomGrid(rectangles);
  if(job.unifiedPlan)productCloseRoomExteriorOpenings(job,input,views,panel,unit,rectangles,grids[panel]);
 }
 std::set<std::string> generated;
 std::vector<AcDbExtents> existingStairs;
 for(const auto& [handle,type]:modelTypes(db)){
  if(type!=L"AcDbLine"&&type!=L"AcDbPolyline"&&type.find(L"Stair")==std::wstring::npos)continue;
  const auto entity=open<AcDbEntity>(find(db,ascii(handle)));
  const auto owner=productEntranceMarker(*entity);
  if(!owner.empty()){generated.insert(owner);continue;}
  const auto layer=symbolName(entity->layerId());
  AcDbExtents extents;
  if(entity->visibility()==AcDb::kVisible&&(layer==L"STAIR"||layer==L"台阶")&&entity->getGeomExtents(extents)==Acad::eOk)
   existingStairs.push_back(extents);
 }
 std::vector<ProductEntranceReceipt> receipts;
 std::set<std::string> matched;
 std::map<std::string,const GenericNativeOpening*> openings;
 for(const auto& opening:input.openings)openings.emplace(opening.handle,&opening);
 std::set<std::string> companionOpenings;
 {
  const auto groups=open<AcDbDictionary>(db.groupDictionaryId());
  std::unique_ptr<AcDbDictionaryIterator> iterator(groups->newIterator());
  for(;!iterator->done();iterator->next()){
   const auto group=open<AcDbGroup>(iterator->objectId());
   AcDbObjectIdArray members;group->allEntityIds(members);
   std::vector<std::string> doors;
   for(int i=0;i<members.length();++i){
    const auto handle=axisHandle(members[i]);
    if(openings.count(handle))doors.push_back(handle);
   }
   if(doors.size()<2)continue;
   const auto assigned=std::find_if(doors.begin(),doors.end(),[&](const auto& handle){return settings.count(handle)!=0;});
   if(assigned==doors.end())continue;
   for(const auto& handle:doors)if(!settings.count(handle))companionOpenings.insert(handle);
  }
 }
 for(const auto& opening:input.openings){
  const auto configured=settings.find(opening.handle);
  const ProductEntranceSetting* setting=configured==settings.end()?nullptr:&configured->second;
  if(!setting&&(opening.window||opening.kind.find(L"门")==std::wstring::npos))continue;
  const auto mid=opening.a+(opening.b-opening.a)*.5;
  const auto panel=bcpproduct::nearestGenericView(views,{mid.x,mid.y});
  if(!inScope(panel))continue;
  if(!setting){
   if(companionOpenings.count(opening.handle))continue;
   // Native combination leaves may be separate entities rather than a CAD group.
   const bool covered=std::any_of(settings.begin(),settings.end(),[&](const auto& entry){
    const auto& [handle,association]=entry;
    if(association.kind!="MAIN"||!openings.count(handle))return false;
    const auto& primary=*openings.at(handle);
    const bool vertical=association.side=="W"||association.side=="E";
    const auto depth=[&](const auto& p){return vertical?p.x:p.y;};
    const auto station=[&](const auto& p){return vertical?p.y:p.x;};
    const auto tolerance=1./unit;
    const auto normal=(depth(primary.a)+depth(primary.b))*.5;
    return std::abs(depth(opening.a)-normal)<tolerance&&std::abs(depth(opening.b)-normal)<tolerance&&
      std::min(station(opening.a),station(opening.b))>=association.low-tolerance&&
      std::max(station(opening.a),station(opening.b))<=association.high+tolerance&&
      (opening.existing.empty()||opening.window||opening.grouped);
   });
   if(covered)continue;
  }
  if(!setting&&!firstFloors.empty()&&!firstFloors.count(panel))continue;
  auto host=productGenericHost(opening,input,views[panel]);
  if(!host&&setting)
   host=productConfirmedSplitOpeningHost(opening,input,views[panel],
        static_cast<int>(std::string("WESN").find(setting->side)),unit);
  int side=host?host->side:-1;
  if(setting){
   matched.insert(opening.handle);
   side=static_cast<int>(std::string("WESN").find(setting->side));
  }else if(host&&grids[panel]){
   const auto classified=productEntranceSideFromDomains(opening,*host,input,*grids[panel],unit);
   side=classified.side;
   if(classified.enclosedInterior&&!generated.count(opening.handle))continue;
  }
  const bool outerNumber=opening.existing.rfind(L"FDM",0)==0||opening.existing.rfind(L"FMD",0)==0;
  if(!setting&&side<0&&!outerNumber&&host)continue;
  ProductEntranceReceipt row;
  row.door=opening.handle;row.number=opening.existing;row.anchor=mid;row.unit=unit;row.status="pending";
  row.panel=panel+1;row.scale=scale;
  {
   const auto door=open<AcDbEntity>(find(db,opening.handle));
   row.doorGrips=chainGrips(door->objectId());
   productEntranceArcs(*door,row.doorArcs);
  }
  const bool main=setting?setting->kind=="MAIN":
   std::any_of(input.notes.begin(),input.notes.end(),[&](const auto& note){
    return (note.text==L"主入口"||note.text==L"大门主入口")&&note.anchor.distanceTo(mid)*unit<=1500.;
   });
  row.kind=main?"main":"other";
  const auto finish=[&](){
   if(!row.missing.empty())job.detailPending.push_back({u8"首层室外台阶",row.door,row.missing});
   receipts.push_back(std::move(row));
  };
  if(setting){row.upper=setting->upper;row.lower=setting->lower;}
  if(setting&&setting->state=="FLAT"){row.status="no-step";finish();continue;}
  if(job.architecturalPlanOnly&&(!setting||setting->state=="REVIEW")){
   row.status="source-design";
   if(side>=0)row.side=std::string(1,"WESN"[side]);
   finish();continue;
  }
  if(generated.count(opening.handle)||(setting&&setting->state=="EXISTING")){
   row.status="existing";
   if(side>=0)row.side=std::string(1,"WESN"[side]);
   if(!row.upper||!row.lower)row.missing=u8"已有台阶保留；缺上平台或台阶脚处室外完成面标高，需复核总高差";
   if(setting)for(const auto& handle:setting->existingLines){
    const auto id=find(db,handle);
    const auto type=open<AcDbEntity>(id);
    require(AcDbLine::cast(type.get())||AcDbPolyline::cast(type.get()),"existing stair source must be a line or polyline");
    const auto points=chainGrips(id);
    require(points.size()>=2,"existing stair source grips");
    row.existingLines.emplace_back(handle,points);
   }
   if(job.unifiedPlan&&setting&&setting->front&&side>=0&&!row.existingLines.empty()){
    const bool vertical=side<2;
    const double sign=side%2?1.:-1.;
    const auto along=[&](const AcGePoint3d& p){return vertical?p.y:p.x;};
    const auto normal=[&](const AcGePoint3d& p){return vertical?p.x:p.y;};
    const auto point=[&](double a,double b){return vertical?AcGePoint3d(b,a,0):AcGePoint3d(a,b,0);};
    const double tolerance=.01/unit;
    std::vector<std::array<double,3>> treads;
    const auto addTread=[&](const AcGePoint3d& a,const AcGePoint3d& b){
     // An existing platform perimeter may continue beyond this entrance. Use
     // only its actual overlap with the source-bound entrance interval.
     const auto low=std::max(std::min(along(a),along(b)),setting->low);
     const auto high=std::min(std::max(along(a),along(b)),setting->high),depth=normal(a);
     if(std::abs(depth-normal(b))<tolerance&&high-low>tolerance&&
        (depth-*setting->front)*sign>=-tolerance)
      treads.push_back({depth,low,high});
    };
    for(const auto& existing:row.existingLines){
     const auto entity=open<AcDbEntity>(find(db,existing.first));
     if(const auto* line=AcDbLine::cast(entity.get()))addTread(line->startPoint(),line->endPoint());
     if(const auto* poly=AcDbPolyline::cast(entity.get());poly&&poly->numVerts()>1){
      const unsigned count=poly->numVerts();
      const unsigned edgeCount=poly->isClosed()?count:count-1;
      for(unsigned i=0;i<edgeCount;++i){
       AcGePoint3d a,b;
       double bulge{};
       if(poly->getBulgeAt(i,bulge)==Acad::eOk&&bulge==0.&&poly->getPointAt(i,a)==Acad::eOk&&
          poly->getPointAt((i+1)%count,b)==Acad::eOk)addTread(a,b);
      }
     }
    }
    const auto front=std::max_element(treads.begin(),treads.end(),[&](const auto& a,const auto& b){
     const auto width=[&](const auto& edge){return std::abs(edge[0]-*setting->front)<tolerance?edge[2]-edge[1]:-1.;};
     return width(a)<width(b);
    });
    if(front!=treads.end()&&std::abs((*front)[0]-*setting->front)<tolerance){
     const double low=(*front)[1],high=(*front)[2];
     std::vector<double> depths;
     for(const auto& edge:treads)
      if(std::abs(edge[1]-low)<tolerance&&std::abs(edge[2]-high)<tolerance)depths.push_back(edge[0]);
     std::sort(depths.begin(),depths.end());
     depths.erase(std::unique(depths.begin(),depths.end(),[&](double a,double b){return std::abs(a-b)<tolerance;}),depths.end());
     if(depths.size()>1){
      const double foot=sign<0?depths.front():depths.back();
      const double gap=4.*scale/unit;
      row.width=(setting->high-setting->low)*unit;
      row.tread=std::abs(depths[1]-depths[0])*unit;
      for(std::size_t k=1;k<depths.size();++k)row.treadDepths.push_back(std::abs(depths[k]-depths[k-1])*unit);
      std::vector<AcGePoint3d> width{point(setting->low,foot)};
      if(low>setting->low+tolerance)width.push_back(point(low,foot));
      if(high<setting->high-tolerance)width.push_back(point(high,foot));
      width.push_back(point(setting->high,foot));
      const auto widthOffset=(vertical?-sign:sign)*gap;
      annotateExisting(row,width,widthOffset);
      if(width.size()>2)annotateExisting(row,{width.front(),width.back()},widthOffset*2.);
      if(host){
       const auto box=approvedRect(input.walls[host->wall].wall);
       const double face=box[side==0?0:side==1?2:side==2?1:3];
       if((face-*setting->front)*sign < -tolerance){
        depths.push_back(face);
        row.platform=std::abs(face-*setting->front)*unit;
        row.total=std::abs(face-foot)*unit;
        row.existingPlatform=true;
        if(!row.doorArcs.empty()){
         double threshold=sign*face;
         bcpproduct::PlanStepInput sweep;
         sweep.unitMM=unit;
         sweep.outwardDirection=vertical?bcpproduct::PlanStepPoint{sign,0}:bcpproduct::PlanStepPoint{0,sign};
         const auto depth=[&](const auto& p){return vertical?p.x:p.y;};
         for(const auto& arc:row.doorArcs){
          const auto hinge=depth(arc.hinge),a=depth(arc.closedTip),b=depth(arc.openTip);
          if(std::max(sign*(a-hinge),sign*(b-hinge))<1./unit)continue;
          sweep.outwardSwings.push_back(arc);
          threshold=std::min({threshold,sign*hinge,sign*a,sign*b});
         }
         sweep.firstAtWall=vertical?bcpproduct::PlanStepPoint{sign*threshold,low}:bcpproduct::PlanStepPoint{low,sign*threshold};
         sweep.secondAtWall=vertical?bcpproduct::PlanStepPoint{sign*threshold,high}:bcpproduct::PlanStepPoint{high,sign*threshold};
         row.outwardLeaves=sweep.outwardSwings.size();
         row.swingChecked=bcpproduct::planStepPlatformContainsSwings(sweep,sign*(*setting->front)-threshold);
        }
       }
      }
      std::sort(depths.begin(),depths.end());
      std::vector<AcGePoint3d> depthPoints;
      for(double depth:depths)depthPoints.push_back(point(high,depth));
      annotateExisting(row,depthPoints,vertical?gap:-gap);
      if(row.existingPlatform)annotateExisting(row,{depthPoints.front(),depthPoints.back()},vertical?gap*2.:-gap*2.);
     }
    }
    if(row.dimensions.empty())row.missing+=u8"；踏面与平台的实际施工尺寸待核";
    if(row.upper&&row.lower&&*row.upper>*row.lower&&row.existingPlatform&&row.swingChecked&&
       setting->approvedRises>0&&static_cast<std::size_t>(setting->approvedRises)==row.treadDepths.size()+1){
     row.riseCount=setting->approvedRises;
     std::wostringstream content;
     const auto sideName=row.side=="E"?L"东侧":row.side=="W"?L"西侧":row.side=="N"?L"北侧":L"南侧";
     content<<(row.kind=="MAIN"?L"主入口台阶":std::wstring(sideName)+L"入口台阶")<<L"\\P";
     content<<std::fixed<<std::setprecision(3)<<*row.upper<<L" → "<<*row.lower
            <<L" m\\P"<<row.riseCount<<L"级×"<<std::setprecision(0)<<(*row.upper-*row.lower)*1000./row.riseCount<<L" mm";
     row.verticalText=content.str();
     std::vector<AcGeVector3d> notePositions;
     const bool vertical=side<2;
     const double sign=side%2?1.:-1.;
     for(const double normal:{12.,20.,28.,36.,44.,52.})
      for(const double tangent:{0.,8.,-8.,16.,-16.,24.,-24.,32.,-32.})
       notePositions.push_back(vertical?AcGeVector3d(sign*normal,tangent,0):AcGeVector3d(tangent,sign*normal,0));
     std::stable_sort(notePositions.begin(),notePositions.end(),[](const auto& a,const auto& b){return a.length()<b.length();});
     for(const auto& delta:notePositions){
      auto note=std::make_unique<AcDbMText>();
      note->setDatabaseDefaults(&db);
      productStatus(note->setLayer(productPlanLayer(db,L"A-TEXT-DESC",7)),"entry height note layer");
      productStatus(note->setTextHeight(2.5*scale/unit),"entry height note size");
      productStatus(note->setWidth(24.*scale/unit),"entry height note wrapping");
      productStatus(note->setAttachment(AcDbMText::kMiddleCenter),"entry height note alignment");
      productStatus(note->setLocation(row.anchor+delta*(scale/unit)),"entry height note position");
      note->setContents(row.verticalText.c_str());
      AcDbExtents ink;
      if(note->getGeomExtents(ink)!=Acad::eOk||productPlanDetailHits(db,{},ink,{}))continue;
      row.verticalNote=axisHandle(productAppendPlanDetail(db,std::move(note)));
      break;
     }
     if(row.verticalNote.empty())row.missing+=u8"；台阶高差说明尚未找到与实际构件及标注不相撞的位置";
    }
   }
   finish();continue;
  }
  if(!host||side<0||(!setting&&firstFloors.empty())){
   row.missing=u8"楼层或室外通行方向未核实；需确认平台完成面及台阶脚处室外地坪";
   finish();continue;
  }
  row.side=std::string(1,"WESN"[side]);
  const bool vertical=side<2;
  if(host->vertical!=vertical){row.missing=u8"已登记的室外方向与宿主墙方向不符";finish();continue;}
  const double sign=side%2?1.:-1.;
  const auto rect=approvedRect(input.walls[host->wall].wall);
  const double face=rect[side==0?0:side==1?2:side==2?1:3];
  const auto station=[&](const auto& p){return vertical?p.y:p.x;};
  const auto normal=[&](const auto& p){return vertical?p.x:p.y;};
  const double endA=station(opening.a),endB=station(opening.b);
  const double openingLow=std::min(endA,endB),openingHigh=std::max(endA,endB);
  const double low=setting?setting->low:openingLow, high=setting?setting->high:openingHigh;
  const auto point=[&](double along,double depth){return vertical?AcGePoint3d(depth,along,0):AcGePoint3d(along,depth,0);};
  const auto p=[](const AcGePoint3d& a){return bcpproduct::PlanStepPoint{a.x,a.y};};
  if(!setting){
   const bool nearby=std::any_of(existingStairs.begin(),existingStairs.end(),[&](const auto& box){
    const auto start=box.minPoint(),end=box.maxPoint();
    const auto a=sign*(normal(start)-face),b=sign*(normal(end)-face);
    return std::max(a,b)>1./unit&&std::min(a,b)<2600./unit&&
     std::min(station(end),high)-std::max(station(start),low)>(high-low)*.6;
   });
   if(nearby){row.status="existing";row.missing=u8"发现该入口附近台阶线，核实际范围及高差";finish();continue;}
  }
  if(setting&&setting->state=="REVIEW"){
   row.missing=u8"需核实此门外通行面及平台完成面、台阶脚处室外地坪标高";
   finish();continue;
  }
  if(low>openingLow+1./unit||high<openingHigh-1./unit){
   row.missing=u8"登记的平台宽度未覆盖门洞";finish();continue;
  }
  bcpproduct::PlanStepInput geometry;
  geometry.kind=main?bcpproduct::PlanStepEntranceKind::mainEntrance:bcpproduct::PlanStepEntranceKind::otherExit;
  geometry.unitMM=unit;geometry.firstAtWall=p(point(low,face));geometry.secondAtWall=p(point(high,face));
  geometry.outwardDirection=vertical?bcpproduct::PlanStepPoint{sign,0}:bcpproduct::PlanStepPoint{0,sign};
  if(setting&&setting->front){geometry.platformDepth=sign*(*setting->front-face);row.existingPlatform=true;}
  std::vector<bcpproduct::PlanStepSwing> arcs;
  {const auto door=open<AcDbEntity>(find(db,opening.handle));productEntranceArcs(*door,arcs);}
  const bool sliding=opening.existing.rfind(L"LM",0)==0||opening.style.find(L"推拉")!=std::wstring::npos;
  if(arcs.empty()&&!sliding){row.missing=u8"门扇开启范围无法读回；需确认门扇和上下完成面高差";finish();continue;}
  double threshold=sign*face;
  for(const auto& arc:arcs){
   const auto along=vertical?arc.hinge.y:arc.hinge.x;
   const auto hinge=normal(arc.hinge),a=normal(arc.closedTip),b=normal(arc.openTip);
   if(along<low-1./unit||along>high+1./unit||std::max(sign*(a-hinge),sign*(b-hinge))<1./unit)continue;
   geometry.outwardSwings.push_back(arc);
   // The swept leaf may start within the actual doorway reveal.
   threshold=std::min({threshold,sign*hinge,sign*a,sign*b});
  }
  const double reveal=sign*face-threshold;
  if(reveal>0){
   geometry.firstAtWall=p(point(low,sign*threshold));geometry.secondAtWall=p(point(high,sign*threshold));
   if(geometry.platformDepth) *geometry.platformDepth+=reveal;
   else if(!main) geometry.platformDepth=1000./unit+reveal;
  }
  row.outwardLeaves=geometry.outwardSwings.size();
  const auto plan=bcpproduct::planEntranceStep(geometry);
  if(!plan){row.missing=u8"平台进深或净宽不足以容纳真实门扇开启范围；需核平台及上下完成面高差";finish();continue;}
  row.swingChecked=bcpproduct::planStepPlatformContainsSwings(geometry,plan->platformDepth);
  require(row.swingChecked,"outward leaf must stay within step platform");
  const auto world=[](const bcpproduct::PlanStepPoint& p){return AcGePoint3d(p.x,p.y,0);};
  const auto& q=plan->platformCorners;
  const auto e1=plan->nosingEdges[0], e2=plan->nosingEdges[1];
  const std::array<AcGePoint3d,4> perimeter{point(low,face),point(high,face),world(e2[1]),world(e2[0])};
  AcDbExtents footprint;
  for(const auto& a:perimeter)footprint.addPoint(a);
  bool blocked=false;
  for(const auto index:views[panel].walls){
   if(index==host->wall||input.walls[index].low||!input.walls[index].orthogonal)continue;
   const auto box=approvedRect(input.walls[index].wall);
   const auto lo=footprint.minPoint(),hi=footprint.maxPoint();const auto tolerance=1./unit;
   if(std::min(hi.x,box[2])-std::max(lo.x,box[0])>tolerance&&
      std::min(hi.y,box[3])-std::max(lo.y,box[1])>tolerance){blocked=true;break;}
  }
  const auto intersects=[&](double xmin,double ymin,double xmax,double ymax){
   const auto lo=footprint.minPoint(),hi=footprint.maxPoint();const auto tolerance=1./unit;
   return std::min(hi.x,xmax)-std::max(lo.x,xmin)>tolerance&&std::min(hi.y,ymax)-std::max(lo.y,ymin)>tolerance;
  };
  for(const auto& column:input.columns)
   if(intersects(column.centre.x-column.halfX,column.centre.y-column.halfY,
                 column.centre.x+column.halfX,column.centre.y+column.halfY)){blocked=true;break;}
  if(blocked){row.missing=u8"拟补台阶与既有墙柱冲突；需复核可用边界及上下完成面高差";finish();continue;}
  row.width=(high-low)*unit;row.platform=(plan->platformDepth-reveal)*unit;
  row.tread=plan->treadDepth*unit;row.total=(plan->totalDepth-reveal)*unit;
  if(row.upper&&row.lower&&std::abs(*row.upper-*row.lower)<.00001){row.status="no-step";finish();continue;}
  const bool riseKnown=row.upper&&row.lower&&*row.upper>*row.lower;
  const bool provisional=!riseKnown||!setting||setting->state!="NEEDED"||setting->approvedRises!=3;
  if(!provisional)row.riseCount=setting->approvedRises;
  row.status=provisional?"candidate":"added";
  if(provisional)row.missing=!row.upper?u8"缺上平台完成面及台阶脚处室外地坪标高或其归属":
                              !row.lower?u8"缺台阶脚处室外地坪标高及台阶总高差":u8"需核台阶总高差方向、竖向级数或做法";
  const auto layer=productPlanLayer(db,provisional?L"A-STEP-CANDIDATE":L"STAIR",provisional?30:2);
  std::vector<std::vector<AcGePoint3d>> paths;
  if(!row.existingPlatform||plan->platformEnlarged)paths.push_back({perimeter[0],world(q[3]),world(q[2]),perimeter[1]});
  paths.push_back({world(q[3]),world(e2[0]),world(e2[1]),world(q[2])});
  paths.push_back({world(e1[0]),world(e1[1])});
  for(const auto& path:paths)row.geometry.emplace_back(productEntranceStroke(db,row.door,layer,path,provisional),path);
  fillPrototypes();
  const auto annotate=[&](const std::vector<AcGePoint3d>& stations,double offset){
   const auto saveChain=[&](AcDbObjectId id){
    auto values=chainValues(id);for(auto& value:values)value*=unit;
    row.dimensions.emplace_back(axisHandle(id),std::move(values));
   };
   const auto makeChain=[&](AcDbObjectId prototype,std::vector<AcGePoint3d> points){
    const auto delta=points.back()-points.front();
    const auto descending=std::abs(delta.y)>std::abs(delta.x)?delta.y<0:delta.x<0;
    if(descending)std::reverse(points.begin(),points.end());
    const auto signedOffset=descending?-offset:offset;
    if(prototype.database()==&db){saveChain(productDetailChain(db,prototype,points,signedOffset));return;}
    const auto donor=cloneOne(*prototype.database(),prototype,db);
    saveChain(productDetailChain(db,donor,points,signedOffset));
    status(editObject<AcDbEntity>(donor)->erase(),"remove temporary step dimension donor");
   };
   productPlanLayer(db,L"PUB_DIM",25);
   const auto whole=prototypes.find(stations.size()-1);
   if(whole!=prototypes.end()){makeChain(whole->second,stations);return;}
   const auto single=prototypes.find(1);
   if(single==prototypes.end()){row.missing+=u8"；缺已核验的天正尺寸样式原型";return;}
   if(curDoc()&&curDoc()->database()==&db){
    const auto prototype=single->second;
    const bool copied=prototype.database()!=&db;
    const auto id=copied?cloneOne(*prototype.database(),prototype,db):prototype;
    saveChain(productGenericChain(job,db,id,stations,offset));
    if(copied)status(editObject<AcDbEntity>(id)->erase(),"remove temporary step dimension donor");
    return;
   }
   for(std::size_t i=1;i<stations.size();++i)makeChain(single->second,{stations[i-1],stations[i]});
   row.missing+=u8"；踏面分段已有真实测量，整体链待合并";
  };
  const auto gap=4.*scale/unit;
  annotate({perimeter[1],world(q[2]),world(e1[1]),world(e2[1])},(vertical?sign:-sign)*gap);
  annotate({world(e2[0]),world(e2[1])},(vertical?-sign:sign)*gap);
  if(provisional){
   auto label=std::make_unique<AcDbText>();label->setDatabaseDefaults(&db);
   label->setTextString(L"台阶平面待核");label->setHeight(180./unit);
   label->setPosition(world(e2[0])+(world(e2[1])-world(e2[0]))*.15+
     (world(e1[0])-world(e2[0])).normal()*(scale/unit));
   productAppendEntranceEntity(db,std::move(label),row.door,layer);
  }
  finish();
 }
 require(matched.size()==settings.size(),"entrance profile references missing native openings");
 return receipts;
}

void productArrangeEntranceDimensions(ProductJob& job,AcDbDatabase& db,
 const std::vector<ProductEntranceReceipt>& rows,double scale,double unit){
 for(const auto& row:rows)for(const auto& [handle,values]:row.dimensions){
  if(values.size()!=3)continue;
  const auto id=find(db,handle);
  const auto before=chainGrips(id);
  require(before.size()==12,"step depth chain grip layout");
  const bool vertical=std::abs(before[5].y-before[2].y)>std::abs(before[5].x-before[2].x);
  if(row.side.empty()||vertical!=(row.side=="N"||row.side=="S"))continue;
  const bool platformFirst=std::abs(values.front()-row.platform)<.01;
  const std::size_t outerTread=platformFirst?2:0;
  const std::size_t innerTread=1;
  const auto across=vertical?AcGeVector3d(-1,0,0):AcGeVector3d(0,1,0);
  const auto stationsAway=(before[outerTread+2]+(before[outerTread+3]-before[outerTread+2])*.5)
                       -(before[innerTread+2]+(before[innerTread+3]-before[innerTread+2])*.5);
  const auto normal=stationsAway.normal();
  const auto paper=scale/unit;
  auto target=before[6+outerTread]+across*(4.8*paper)+normal*(1.8*paper);
  moveDimensionGrip(id,static_cast<int>(6+outerTread),target);
  const auto after=chainGrips(id);
  require(std::equal(before.begin(),before.begin()+6,after.begin(),[&](const auto& a,const auto& b){
   return a.distanceTo(b)*unit<.01;
  }),"stair text adjustment changed measurement stations");
  if(job.unifiedPlan&&row.status=="existing")
   productGenericInteriorClearance(job,db,id,vertical,paper,nullptr,false,false,true);
 }
}

void productVerifyEntranceSteps(ProductJob& job,AcDbDatabase& saved,const std::vector<ProductEntranceReceipt>& rows,const fs::path& file){
 Working work(&saved);
 std::ostringstream out;
 out<<std::setprecision(17)<<"{\"savedReopened\":true,\"entries\":[";
 bool first=true;
 for(const auto& row:rows){
  if(!first)out<<',';
  first=false;
  out<<"{\"door\":\""<<row.door<<"\",\"number\":"<<jsonUtf8(row.number)<<",\"kind\":\""<<row.kind
     <<"\",\"status\":\""<<row.status<<"\",\"side\":\""<<row.side<<"\",\"missing\":"
     <<jsonUtf8(bcpproduct::decodeMessage(row.missing))<<",\"anchor\":"<<wholePoints({row.anchor})
     <<",\"upperM\":"<<(row.upper?std::to_string(*row.upper):"null")
     <<",\"lowerM\":"<<(row.lower?std::to_string(*row.lower):"null")<<",\"riseCount\":"<<row.riseCount
     <<",\"swingChecked\":"<<(row.swingChecked?"true":"false")<<",\"outwardLeaves\":"<<row.outwardLeaves
     <<",\"existingPlatform\":"<<(row.existingPlatform?"true":"false")
     <<",\"planMM\":{\"width\":"<<row.width<<",\"platform\":"<<row.platform<<",\"tread\":"<<row.tread<<",\"total\":"<<row.total<<"},\"entities\":[";
  bool firstEntity=true;
  bool correctStairLayer=true,correctDimensionStyle=true;
  for(const auto& [handle,points]:row.geometry){
   if(!firstEntity)out<<',';firstEntity=false;
   const auto poly=open<AcDbPolyline>(find(saved,handle));
   require(productEntranceMarker(*poly)==row.door&&poly->numVerts()==points.size(),"saved stair identity");
   const auto layer=open<AcDbLayerTableRecord>(poly->layerId());
   const auto color=poly->colorIndex()==256?layer->color().colorIndex():poly->colorIndex();
   correctStairLayer=correctStairLayer&&symbolName(poly->layerId())==L"STAIR"&&color==2;
   std::vector<AcGePoint3d> actual;
   for(unsigned i=0;i<points.size();++i){
    AcGePoint3d point;status(poly->getPointAt(i,point),"saved stair vertex");
    require(point.distanceTo(points[i])*row.unit<.01,"saved stair geometry changed");actual.push_back(point);
   }
   out<<"{\"handle\":\""<<handle<<"\",\"points\":"<<wholePoints(actual)<<'}';
  }
  out<<"],\"existingLines\":[";
  for(std::size_t i=0;i<row.existingLines.size();++i){
   if(i)out<<',';
   const auto& [handle,points]=row.existingLines[i];
   const auto id=find(saved,handle);
   const auto entity=open<AcDbEntity>(id);
   const auto layer=open<AcDbLayerTableRecord>(entity->layerId());
   const auto color=entity->colorIndex()==256?layer->color().colorIndex():entity->colorIndex();
   correctStairLayer=correctStairLayer&&symbolName(entity->layerId())==L"STAIR"&&color==2;
   const auto actual=chainGrips(id);
   require(points.size()==actual.size()&&std::equal(points.begin(),points.end(),actual.begin(),
    [&](const auto& a,const auto& b){return a.distanceTo(b)*row.unit<.01;}),"saved original stair evidence changed");
   out<<"{\"handle\":\""<<handle<<"\",\"points\":"<<wholePoints(actual)<<'}';
  }
  out<<"],\"dimensions\":[";
  for(std::size_t i=0;i<row.dimensions.size();++i){
   if(i)out<<',';
   const auto& [handle,values]=row.dimensions[i];
   const auto id=find(saved,handle);
   auto entity=open<AcDbEntity>(id);
   require(std::wstring(entity->isA()->name())==L"TDbDimension2"&&symbolName(entity->layerId())==L"PUB_DIM",
           "saved step dimension type/layer");
   const auto layer=open<AcDbLayerTableRecord>(entity->layerId());
   const auto color=entity->colorIndex()==256?layer->color().colorIndex():entity->colorIndex();
   const auto styles=productDimensionTexts(*entity);
   const auto& policy=job.dimensionStylePolicy;
   correctDimensionStyle=correctDimensionStyle&&color==25&&styles.size()==values.size()&&
    std::all_of(styles.begin(),styles.end(),[&](const auto& style){
     return productDimensionMatches(style,policy.name,row.scale,policy.nominalPaperHeightMM*row.scale/row.unit);
    });
   entity.reset();const auto actual=chainValues(id);
   require(actual.size()==values.size(),"saved step dimension count");
   out<<"{\"handle\":\""<<handle<<"\",\"measuredMM\":[";
   for(std::size_t k=0;k<values.size();++k){
    require(std::abs(actual[k]*row.unit-values[k])<.01,"saved step measurement");
    if(k)out<<',';out<<actual[k]*row.unit;
   }
   job.planPostcheckKnownDimensions.insert(handle);
   out<<"]}";
  }
  out<<"]}";
  productRecordRuleEvidence(job,"C01","{\"object\":\""+row.door+"\",\"result\":\""+row.status+
    "\",\"detail\":"+jsonUtf8(bcpproduct::decodeMessage(row.missing))+",\"anchor\":"+wholePoints({row.anchor})+"}");
  const auto id=find(saved,row.door);
  const auto door=open<AcDbEntity>(id);
  const auto grips=chainGrips(id);
  std::vector<bcpproduct::PlanStepSwing> arcs;
  productEntranceArcs(*door,arcs);
  const auto matchPoint=[&](const auto& a,const auto& b){return std::hypot(a.x-b.x,a.y-b.y)*row.unit<.01;};
  const bool openingRetained=grips.size()>=3&&row.doorGrips.size()>=3&&
   grips[1].distanceTo(row.doorGrips[1])*row.unit<.01&&grips[2].distanceTo(row.doorGrips[2])*row.unit<.01&&
   arcs.size()==row.doorArcs.size()&&std::equal(arcs.begin(),arcs.end(),row.doorArcs.begin(),[&](const auto& a,const auto& b){
    return a.counterclockwise==b.counterclockwise&&matchPoint(a.hinge,b.hinge)&&matchPoint(a.closedTip,b.closedTip)&&matchPoint(a.openTip,b.openTip);
   });
  const bool stairGeometry=!row.geometry.empty()||!row.existingLines.empty();
  const bool heightKnown=row.upper&&row.lower&&row.riseCount>0&&*row.upper>*row.lower;
  const bool actualDepth=!row.treadDepths.empty()&&row.platform>0&&
   std::abs(row.total-row.platform-std::accumulate(row.treadDepths.begin(),row.treadDepths.end(),0.))<.01;
  const bool defaultsBuilt=(row.status=="added"||row.status=="candidate")&&stairGeometry&&row.swingChecked;
  const bool flat=row.status=="no-step";
  const bool sourceDesign=row.status=="source-design";
  if(sourceDesign&&openingRetained)job.planPreservedDesign.emplace(row.panel,"entrance",row.door);
  bool verticalNote=false;
  if(!row.verticalNote.empty()){
   const auto note=open<AcDbMText>(find(saved,row.verticalNote));
   AcString text;
   verticalNote=productPlanDimensionVisible(*note)&&note->contents(text)==Acad::eOk&&text==row.verticalText.c_str();
  }
  {
   std::ostringstream diagnostic;
   diagnostic<<"{\"object\":\""<<row.door<<"\",\"scope\":\"entrance-saved-components\",\"savedReopened\":true"
     <<",\"openingRetained\":"<<(openingRetained?"true":"false")
     <<",\"actualDepth\":"<<(actualDepth?"true":"false")
     <<",\"heightKnown\":"<<(heightKnown?"true":"false")
     <<",\"verticalNote\":\""<<row.verticalNote<<"\",\"verticalNoteVerified\":"<<(verticalNote?"true":"false")
     <<",\"result\":\""<<(openingRetained&&actualDepth&&heightKnown&&verticalNote?"verified":"pending")<<"\"}";
   productRecordRuleEvidence(job,"C20",diagnostic.str());
  }
  const bool existingVerified=row.status=="existing"&&row.existingPlatform&&row.swingChecked&&actualDepth&&verticalNote;
  const bool existingPlanVerified=job.architecturalPlanOnly&&row.status=="existing"&&row.existingPlatform&&
   row.swingChecked&&actualDepth&&correctStairLayer&&correctDimensionStyle&&!row.dimensions.empty();
  const bool stepVerified=openingRetained&&stairGeometry&&((heightKnown&&(defaultsBuilt||existingVerified))||existingPlanVerified);
  std::vector<std::string> stepObjects;
  for(const auto& [handle,points]:row.geometry)stepObjects.push_back(handle);
  for(const auto& [handle,points]:row.existingLines)stepObjects.push_back(handle);
  if(verticalNote)stepObjects.push_back(row.verticalNote);
  productCheckPlanSource(job,row.panel,"entrance",row.door,
   sourceDesign&&!openingRetained?bcpproduct::PlanCheck::failed:
   flat&&openingRetained?bcpproduct::PlanCheck::omitted:stepVerified?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::pending,
   std::move(stepObjects),flat&&openingRetained?u8"已确认等高或明确省略台阶":
      stepVerified?(existingPlanVerified?u8"原踏步及平台、洞口开启和真实平面尺寸在重开图中核实":u8""):
      sourceDesign?(openingRetained?u8"按源图保留门及出入口方案，门洞测点和开启范围已独立重开核对":u8"出入口门洞测点或开启范围重开核对失败"):
      row.missing.empty()?u8"仍需核实高差、门扇、平台或既有踏步":row.missing);
  if(row.missing.rfind(u8"缺",0)==0||row.missing.rfind(u8"已有台阶保留；缺",0)==0)
   job.sourceInventory.needsProjectInput(row.panel,"entrance",row.door,row.missing);
  const auto evidence=[&](const char* rule,const char* result,const char* scope){
   std::ostringstream record;
   record<<"{\"object\":\""<<row.door<<"\",\"panel\":"<<row.panel<<",\"anchor\":"<<wholePoints({row.anchor})
    <<",\"scope\":\""<<scope<<"\",\"savedReopened\":true,\"report\":\"exterior-entrance-steps.json\",\"result\":\""<<result
    <<"\",\"detail\":"<<jsonUtf8(bcpproduct::decodeMessage(row.missing))<<'}';
   productRecordRuleEvidence(job,rule,record.str());
  };
  evidence("SE22",flat?"not-applicable":stairGeometry&&heightKnown?"verified-entrance-step":"pending","entry-step-and-levels");
  evidence("C02",flat?"not-applicable":heightKnown?"verified-confirmed-rise":"pending","project-rise-input");
  evidence("C03",flat?"not-applicable":(actualDepth||defaultsBuilt)?"verified-plan-depth-closure":"pending","horizontal-depth-closure");
  evidence("C04",flat?"not-applicable":row.existingPlatform&&stairGeometry?"verified-platform-plan-reuse":"pending","plan-platform-reuse");
  evidence("C08",openingRetained?"verified-door-plan-preservation":"pending","door-stations-and-swept-arcs");
  evidence("C11",row.status=="pending"&&row.geometry.empty()&&openingRetained?"verified-blocked-entry-preserved":"not-applicable","blocked-entry-preservation");
  evidence("C20",flat?"not-applicable":stairGeometry&&row.status=="existing"?"verified-retained-existing-step":defaultsBuilt?"verified-default-plan-candidate":"pending","confirmed-existing-or-default-plan");
  evidence("TL01",flat?"not-applicable":stairGeometry&&correctStairLayer?"verified-stair-plan-and-layer":"pending","stair-boundary-and-layer");
  evidence("TL03",flat?"not-applicable":!row.dimensions.empty()&&correctDimensionStyle?"verified-native-step-dimensions":"pending","native-measurement-and-style");
 }
 out<<"]}";
 writeNew(file,out.str());
}
