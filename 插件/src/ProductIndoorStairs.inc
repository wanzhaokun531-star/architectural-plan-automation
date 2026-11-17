struct ProductStairRow {
 std::string key, state, source, object, issue;
 std::size_t panel{};
 std::vector<std::string> treads;
 double width{}, pitch{};
 std::vector<AcGePoint3d> grips;
 std::vector<bcpproduct::PlanStairTread> sourceTreadStrokes;
 int inputDirection{};
 std::wstring nativeType, nativeLayer;
 std::vector<bcpproduct::PlanStairTread> visibleStrokes;
 struct Dimension { std::string handle; double value; double visible; };
 std::vector<Dimension> dimensions;
 std::string verticalLower,verticalUpper,verticalChain;
 std::string verticalNote;
 std::wstring verticalText;
 struct PlanEdge {std::string role,handle,archiveHandle;std::vector<AcGePoint3d> points;};
 std::vector<PlanEdge> planEdges;
 struct GuardPair {std::string inner,outer;double width;bool closed;};
 std::vector<GuardPair> guardPairs;
 double lowerMM{},upperMM{};
 int risers{};
};

struct ProductStairDirectionMarker {
 std::string handle;
 std::size_t panel;
 std::wstring label;
};

std::vector<ProductStairDirectionMarker> productSourceStairDirectionMarkers(AcDbDatabase& db,
 const std::vector<bcpproduct::GenericView>& views,const std::vector<bool>& eligible){
 std::vector<ProductStairDirectionMarker> markers;
 for(const auto& [handle,type]:modelTypes(db)){
  if(type!=L"TDbSymbArrow")continue;
  const auto key=ascii(handle);
  const auto entity=open<AcDbEntity>(find(db,key));
  if(!productPlanDimensionVisible(*entity))continue;
  std::vector<ProductRoomLeaf> leaves;
  productRoomLeaves(*entity,leaves);
  const auto label=std::find_if(leaves.begin(),leaves.end(),[](const auto& leaf){
   return leaf.text==L"上"||leaf.text==L"下"||leaf.text==L"上行"||leaf.text==L"下行";
  });
  if(label==leaves.end())continue;
  AcDbExtents bounds;
  if(entity->getGeomExtents(bounds)!=Acad::eOk)continue;
  const auto center=bounds.minPoint()+(bounds.maxPoint()-bounds.minPoint())*.5;
  const auto view=bcpproduct::nearestGenericView(views,{center.x,center.y});
  if(eligible[view])markers.push_back({key,view+1,label->text});
 }
 return markers;
}

std::pair<std::vector<bcpproduct::PlanGuardPoint>,bool> productStairGuardPath(AcDbDatabase& db,
 const std::string& handle,double tolerance){
 const auto entity=open<AcDbPolyline>(find(db,handle));
 require(productPlanDimensionVisible(*entity)&&entity->numVerts()>=2,"guard boundary must be a visible source polyline");
 std::vector<bcpproduct::PlanGuardPoint> points;
 for(unsigned i=0;i<entity->numVerts();++i){
  AcGePoint2d p;double bulge{};
  status(entity->getPointAt(i,p),"read guard corner");
  status(entity->getBulgeAt(i,bulge),"read guard curve");
  require(bulge==0.,"curved guard needs its own offset construction");
  points.push_back({p.x,p.y});
 }
 const bool closed=entity->isClosed()||std::hypot(points.front().x-points.back().x,
   points.front().y-points.back().y)<=tolerance;
 return {points,closed};
}

struct ProductStairDialog {
 double width, pitch;
 std::size_t treads;
 HWND window{};
 bool applied{};
};

std::wstring productStairNumber(double value) {
 std::wostringstream text;
 text.imbue(std::locale::classic());
 text<<std::fixed<<std::setprecision(8)<<value;
 return text.str();
}

BOOL CALLBACK productSetStairDialog(HWND window, LPARAM value) {
 auto& config=*reinterpret_cast<ProductStairDialog*>(value);
 if(!IsWindowVisible(window)||GetWindowThreadProcessId(window,nullptr)!=GetCurrentThreadId()||
    productControlText(window)!=L"直线梯段")return TRUE;
 config.window=window;
 const auto controls=std::array<std::pair<int,int>,5>{{{5031,0},{5032,1},{5033,1},{5057,0},{5058,0}}};
 const auto fields=std::array<std::pair<int,std::wstring>,4>{{
  {5027,productStairNumber(config.width)},{5030,std::to_wstring(config.treads)},
  {5028,productStairNumber(config.pitch)},{5038,L"0"}}};
 for(const auto& [id,setting]:controls) {
  const auto button=GetDlgItem(window,id);
  if(!button||productControlClass(button)!=L"Button")return FALSE;
  if(SendMessageW(button,BM_GETCHECK,0,0)!=setting)SendMessageW(button,BM_CLICK,0,0);
  if(SendMessageW(button,BM_GETCHECK,0,0)!=setting)return FALSE;
 }
 for(const auto& [id,setting]:fields) {
  const auto edit=GetDlgItem(window,id);
  if(!edit||productControlClass(edit)!=L"Edit"||!SetWindowTextW(edit,setting.c_str()))return FALSE;
  SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,EN_CHANGE),reinterpret_cast<LPARAM>(edit));
  SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,EN_KILLFOCUS),reinterpret_cast<LPARAM>(edit));
 }
 config.applied=true;
 return FALSE;
}

struct ProductStairInputObserver final : AcEdInputContextReactor {
 ProductStairDialog& settings;
 explicit ProductStairInputObserver(ProductStairDialog& config):settings(config){}
 void beginGetPoint(const AcGePoint3d*,const ACHAR*,int,const ACHAR*) override {
  if(!settings.window)EnumThreadWindows(GetCurrentThreadId(),productSetStairDialog,reinterpret_cast<LPARAM>(&settings));
 }
};

std::optional<bcpproduct::PlanStairTread> productStairSourceTread(const AcDbEntity& entity) {
 const auto* curve=AcDbCurve::cast(&entity);
 const auto* polyline=AcDbPolyline::cast(&entity);
 if(!AcDbLine::cast(&entity)&&(!polyline||polyline->isClosed()||polyline->numVerts()!=2||
                              polyline->segType(0)!=AcDbPolyline::kLine))return {};
 AcGePoint3d a,b;
 if(!curve||curve->getStartPoint(a)!=Acad::eOk||curve->getEndPoint(b)!=Acad::eOk||std::abs(a.z-b.z)>.0001)return {};
 return bcpproduct::PlanStairTread{{a.x,a.y},{b.x,b.y}};
}

void productStairInk(const AcDbEntity& entity,std::vector<bcpproduct::PlanStairTread>& strokes,int depth=0) {
 if(const auto line=productStairSourceTread(entity)){strokes.push_back(*line);return;}
 if(depth>5||AcDbText::cast(&entity)||AcDbMText::cast(&entity)||AcDbArc::cast(&entity))return;
 TemplateParts parts(&entity);
 if(entity.explode(parts.values)!=Acad::eOk)return;
 for(int i=0;i<parts.values.length();++i)if(const auto* child=AcDbEntity::cast(static_cast<AcRxObject*>(parts.values[i])))
  productStairInk(*child,strokes,depth+1);
}

bool productStairSameStroke(const bcpproduct::PlanStairTread& a,const bcpproduct::PlanStairTread& b,double epsilon) {
 const auto coincides=[&](const auto& p,const auto& q){return std::hypot(p.x-q.x,p.y-q.y)<=epsilon;};
 return (coincides(a.first,b.first)&&coincides(a.second,b.second))||
        (coincides(a.first,b.second)&&coincides(a.second,b.first));
}

std::vector<bcpproduct::PlanStairTread> productStairVisibleStrokes(const AcDbEntity& entity,double tolerance) {
 GenericOpeningInk ink;
 productGenericOpeningInk(entity,ink);
 std::vector<bcpproduct::PlanStairTread> strokes;
 for(const auto& [a,b]:ink.straight){
  if(std::abs(a.z-b.z)>tolerance||a.distanceTo(b)<=tolerance)continue;
  const bcpproduct::PlanStairTread tread{{a.x,a.y},{b.x,b.y}};
  if(std::none_of(strokes.begin(),strokes.end(),[&](const auto& prior){
   return productStairSameStroke(tread,prior,tolerance);
  }))strokes.push_back(tread);
 }
 return strokes;
}

std::vector<bcpproduct::PlanStairFlight> productStairVisibleRuns(
 const std::vector<bcpproduct::PlanStairTread>& strokes,double tolerance) {
 using namespace bcpproduct::plan_step_detail;
 std::vector<bcpproduct::PlanStairFlight> runs;
 std::vector<bool> used(strokes.size());
 for(std::size_t i=0;i<strokes.size();++i){
  if(used[i])continue;
  const auto& reference=strokes[i];
  const auto along=difference(reference.second,reference.first);
  const double length=std::hypot(along.x,along.y);
  const bcpproduct::PlanStepPoint across{along.x/length,along.y/length};
  std::vector<bcpproduct::PlanStairTread> candidate;
  for(std::size_t k=i;k<strokes.size();++k){
   if(used[k])continue;
   auto [a,b]=strokes[k];
   if(dot(difference(b,a),across)<0)std::swap(a,b);
   const auto direction=difference(b,a);
   if(std::hypot(direction.x-along.x,direction.y-along.y)>tolerance||
      std::abs(dot(difference(a,reference.first),across))>tolerance)continue;
   candidate.push_back({a,b});
   used[k]=true;
  }
  // Retain only a complete equal-pitch group of at least three public strokes.
  if(const auto run=bcpproduct::planStairFlight(candidate,tolerance))runs.push_back(*run);
 }
 return runs;
}

std::optional<double> productStairVisibleLength(AcDbObjectId id) {
 const auto entity=open<AcDbEntity>(id);
 TemplateParts parts(entity.get());
 if(entity->explode(parts.values)!=Acad::eOk||parts.values.length()!=1)return {};
 const auto* part=AcDbAlignedDimension::cast(static_cast<AcRxObject*>(parts.values[0]));
 if(!part)return {};
 const auto stations=part->xLine2Point()-part->xLine1Point();
 if(stations.length()<=1e-8)return {};
 const auto normal=stations.normal().crossProduct(AcGeVector3d::kZAxis);
 const auto distance=std::abs((part->dimLinePoint()-part->xLine1Point()).dotProduct(normal));
 return distance-part->dimexo()*part->dimscale();
}

void productStairDimension(AcDbDatabase& db,AcDbDatabase* material,ProductStairRow& row,
 const AcGePoint3d& a,const AcGePoint3d& b,double side,double unit,double scale,const ProductJob& job) {
 const auto paper=job.ruleCatalog?job.ruleCatalog->stairExtensionPaperMM:std::optional<double>{5.5};
 if(!material||!paper||std::abs(scale-100.)>1e-8)return;
 const auto expectedVisible=*paper*scale;
 const double tolerance=.01/unit;
 for(const auto& [handle,type]:modelTypes(db)){
  if(type!=L"TDbDimension2")continue;
  const auto id=find(db,ascii(handle));
  if(!productPlanDimensionVisible(*open<AcDbEntity>(id)))continue;
  const auto grips=chainGrips(id);
  if(grips.size()==8&&((grips[2].distanceTo(a)<tolerance&&grips[3].distanceTo(b)<tolerance)||
                      (grips[3].distanceTo(a)<tolerance&&grips[2].distanceTo(b)<tolerance)))return;
 }
 productPlanLayer(db,L"PUB_DIM",25);
 const auto donor=cloneOne(*material,find(*material,"19A0"),db);
 const auto grips=chainGrips(donor);
 const auto visible=productStairVisibleLength(donor);
 require(visible.has_value()&&grips.size()==8,"read visible stair dimension witness");
 const auto tangent=(grips[3]-grips[2]).normal();
 const auto normal=tangent.crossProduct(AcGeVector3d::kZAxis);
 const auto extensionGap=std::abs((grips[0]-grips[2]).dotProduct(normal))-*visible;
 const auto id=productDetailChain(db,donor,{a,b},side*(expectedVisible/unit+extensionGap));
 row.dimensions.push_back({axisHandle(id),a.distanceTo(b)*unit,expectedVisible});
 status(editObject<AcDbEntity>(donor)->erase(),"retire stair dimension donor");
}

AcDbObjectId productCreateStraightStair(AcDbDatabase& output,const bcpproduct::PlanStairFlight& flight,
 const std::vector<bcpproduct::PlanStairTread>& treads,int direction,double unit) {
 if(!curDoc())return {};
 auto& document=*curDoc()->database();
 Working work(&document);
 if(!acedRegCmds->lookupGlobalCmd(L"TLStair")) {
  const auto folder=fs::path(productSetting(L"tangentDirectory",L"C:\\Tangent\\TArchT20V8\\SYS23x64"));
  if(!acrxDynamicLinker->loadModule((folder/L"tch_kerncmd.arx").c_str(),false,false))return {};
 }
 if(!acedRegCmds->lookupGlobalCmd(L"TLStair"))return {};
 const auto before=wholeModelIds(document);
 ProductStairDialog dialog{flight.width,flight.pitch,flight.visibleTreads};
 ProductStairInputObserver observer(dialog);
 auto* manager=curDoc()->inputPointManager();
 manager->addInputContextReactor(&observer);
 acedCommandS(RTSTR,L"TLStair",RTSTR,L"",RTNONE);
 manager->removeInputContextReactor(&observer);
 if(dialog.applied) {
  ads_point point{flight.first.x,flight.first.y,0};
  acedCommandS(RTSTR,L"TLStair",RTPOINT,point,RTSTR,L"",RTNONE);
 }
 if(dialog.window&&IsWindow(dialog.window))SendMessageW(dialog.window,WM_CLOSE,0,0);
 std::vector<AcDbObjectId> added;
 for(const auto id:wholeModelIds(document))if(!before.count(id))added.push_back(id);
 const auto erase=[](AcDbObjectId id){status(editObject<AcDbEntity>(id)->erase(),"retire trial stair entity");};
 if(!dialog.applied||added.size()!=1) {
  for(const auto id:added)erase(id);
  return {};
 }
 const auto id=added.front();
 bool fit=false;
 {
  auto entity=editObject<AcDbEntity>(id);
  AcDbExtents extents;
  if(std::wstring(entity->isA()->name())==L"TDbLineStair"&&entity->getGeomExtents(extents)==Acad::eOk){
   const auto extent=extents.maxPoint()-extents.minPoint();
   const double depth=flight.pitch*(flight.visibleTreads-1);
   if(std::abs(extent.x-flight.width)*unit<.35&&std::abs(extent.y-depth)*unit<.35){
    const AcGePoint3d base(flight.first.x,flight.first.y,0);
    const auto target=direction>0?base:base+AcGeVector3d(flight.across.x*flight.width+flight.travel.x*depth,
                                                        flight.across.y*flight.width+flight.travel.y*depth,0);
    const auto angle=std::atan2(flight.across.y,flight.across.x)+(direction<0?std::acos(-1.):0.);
    status(entity->transformBy(AcGeMatrix3d::translation(target-extents.minPoint())),"place native stair");
    status(entity->transformBy(AcGeMatrix3d::rotation(angle,AcGeVector3d::kZAxis,target)),"orient native stair");
    std::vector<bcpproduct::PlanStairTread> ink;
    productStairInk(*entity,ink);
    fit=std::all_of(treads.begin(),treads.end(),[&](const auto& source){
     return std::any_of(ink.begin(),ink.end(),[&](const auto& line){return productStairSameStroke(source,line,.35/unit);});
    });
   }
  }
 }
 if(!fit){erase(id);return {};}
 if(&output==&document)return id;
 AcDbObjectId copied;
 try {copied=cloneOne(document,id,output);}catch(...){erase(id);throw;}
 erase(id);
 return copied;
}

std::vector<ProductStairRow> productIndoorStairs(ProductJob& job,AcDbDatabase& db,
 const std::vector<bcpproduct::GenericView>& views,AcDbDatabase* material,double unit,double scale,
 const fs::path& profile,const std::string& sourceHash) {
 Working scope(&db);
 std::vector<ProductStairRow> rows;
 std::set<std::string> used;
 if(!profile.empty()&&fs::exists(profile)){
  std::ifstream input(profile);
  std::string tag,hash;
  require(bool(input>>tag>>hash)&&tag=="PLANSTAIRS1"&&hash==sourceHash,"stair plan must match source DWG");
  std::string line;std::getline(input,line);
  std::unique_ptr<AcDbDatabase> archive;
  bool ended=false;
  while(std::getline(input,line)){
   if(line=="END"){ended=true;break;}
   if(line.empty())continue;
   ProductStairRow row;
   int direction{};
   std::istringstream record(line);
   require(bool(record>>tag),"stair profile record");
   if(tag=="SECTION"){
    std::string relative,digest,extra;
    require(!archive&&bool(record>>std::quoted(relative)>>digest)&&!(record>>extra),"unique stair section archive");
    const auto file=profile.parent_path()/fs::u8path(relative);
    require(fs::is_regular_file(file)&&sha(file)==digest,"stair section archive digest");
    archive=std::make_unique<AcDbDatabase>(false,true);
    productStatus(archive->readDwgFile(file.c_str(),AcDbDatabase::kForReadAndAllShare,false),"read stair section archive");
    productStatus(archive->closeInput(true),"close stair section archive input");
    continue;
   }
   if(tag=="RISE"){
    std::string extra;
    require(archive&&bool(record>>row.key>>row.verticalChain>>row.verticalLower>>row.verticalUpper>>row.risers)&&
        row.risers>0&&!(record>>extra),"stair riser range requires section chain and endpoint levels");
    const auto flight=std::find_if(rows.begin(),rows.end(),[&](const auto& prior){return prior.key==row.key;});
    require(flight!=rows.end()&&flight->risers==0,"unique registered straight flight for section");
    Working section(archive.get());
    const auto readLevel=[&](const std::string& handle){
     const auto id=find(*archive,handle);
     const auto elevation=open<AcDbEntity>(id);
     require(std::wstring(elevation->isA()->name())==L"TDbSymbElevation","stair section elevation type");
     return chainGrips(id).at(0).y;
    };
    const double low=readLevel(row.verticalLower),high=readLevel(row.verticalUpper);
    const auto chainId=find(*archive,row.verticalChain);
    const auto values=chainValues(chainId);
    const auto points=chainGrips(chainId);
    require(points.size()==values.size()*2+6&&high>low,"stair section dimension geometry");
    const auto hasStation=[&](double y){
     return std::any_of(points.begin()+2,points.begin()+3+values.size(),[&](const auto& p){return std::abs(p.y-y)*unit<.01;});
    };
    require(hasStation(low)&&hasStation(high),"stair levels do not match archived section dimension stations");
    flight->risers=row.risers;
    const double datum=points[2].y;
    flight->lowerMM=(low-datum)*unit;
    flight->upperMM=(high-datum)*unit;
    flight->verticalChain=row.verticalChain;
    flight->verticalLower=row.verticalLower;
    flight->verticalUpper=row.verticalUpper;
    continue;
   }
   if(tag=="PLANEDGE"||tag=="REGISTEREDGE"){
    std::string flight,role,handle,extra;
    const bool registered=tag=="REGISTEREDGE";
    require(bool(record>>flight>>role>>handle)&&
      (role=="LANDING"||role=="BREAK"||role=="GUARD"||(registered&&role=="PARTIALTREAD")),"stair plan edge role and source object");
    const auto entry=std::find_if(rows.begin(),rows.end(),[&](const auto& prior){return prior.key==flight;});
    require(entry!=rows.end(),"stair plan edge must identify a registered flight");
    const auto id=find(db,handle);
    const auto entity=open<AcDbEntity>(id);
    const auto points=chainGrips(id);
    require((AcDbPolyline::cast(entity.get())||AcDbLine::cast(entity.get())||
       (role=="BREAK"&&std::wstring(entity->isA()->name())==L"TDbSymbRupture"))&&
      productPlanDimensionVisible(*entity)&&points.size()>=2,"stair plan edge must be actual visible source linework");
    const auto centre=points.front()+(points.back()-points.front())*.5;
    require(bcpproduct::nearestGenericView(views,{centre.x,centre.y})+1==entry->panel,"stair edge belongs to the same plan");
    std::string archiveHandle;
    if(registered){
     std::string anchorA,anchorB,archiveA,archiveB;
     require(archive&&bool(record>>archiveHandle>>anchorA>>archiveA>>anchorB>>archiveB)&&anchorA!=anchorB,
      "stair registration needs two corresponding tread edges");
     const auto registrationFlight=std::find_if(rows.begin(),rows.end(),[&](const auto& candidate){
      return candidate.panel==entry->panel&&
       std::find(candidate.treads.begin(),candidate.treads.end(),anchorA)!=candidate.treads.end()&&
       std::find(candidate.treads.begin(),candidate.treads.end(),anchorB)!=candidate.treads.end();
     });
     require(registrationFlight!=rows.end(),"stair registration uses a same-plan flight's source treads");
     const auto first=std::find(registrationFlight->treads.begin(),registrationFlight->treads.end(),anchorA);
     const auto second=std::find(registrationFlight->treads.begin(),registrationFlight->treads.end(),anchorB);
     const auto xy=[](const auto& p){return AcGePoint3d(p.x,p.y,0);};
     const auto eps=.01/unit;
     std::optional<AcGeVector3d> shift;
     for(const auto& [tread,reference]:{std::make_pair(first,archiveA),std::make_pair(second,archiveB)}){
      const auto& raw=registrationFlight->sourceTreadStrokes.at(tread-registrationFlight->treads.begin());
      const auto ref=chainGrips(find(*archive,reference));
      require(ref.size()>=2,"archived stair registration stroke");
      const auto delta=xy(raw.first)-xy(ref.front());
      require((xy(raw.second)-xy(ref[1])-delta).length()<=eps&&(!shift||(*shift-delta).length()<=eps),
       "stair treads disagree on plan translation");
      shift=delta;
     }
     const auto other=chainGrips(find(*archive,archiveHandle));
     require(other.size()==points.size()&&std::equal(points.begin(),points.end(),other.begin(),
      [&](const auto& a,const auto& b){return xy(a).distanceTo(xy(b)+*shift)<=eps;}),
      "stair edge must coincide with the archived plan after registration");
    }
    require(!(record>>extra),"unexpected stair edge fields");
    if(role=="PARTIALTREAD"){
     const auto partial=productStairSourceTread(*entity);
     const auto run=bcpproduct::planStairFlight(entry->sourceTreadStrokes,.01/unit);
     require(partial&&run,"partial stair segment needs a registered straight run");
     const AcGePoint3d base(run->first.x,run->first.y,0);
     const AcGeVector3d across(run->across.x,run->across.y,0),travel(run->travel.x,run->travel.y,0);
     const AcGePoint3d a(partial->first.x,partial->first.y,0),b(partial->second.x,partial->second.y,0);
     const auto u=(a-base).dotProduct(across),v=(b-base).dotProduct(across),s=(a-base).dotProduct(travel);
     const auto eps=.01/unit;
     require(std::abs((a-b).dotProduct(travel))<=eps&&std::abs(u-v)>eps&&std::abs(u-v)<run->width-eps&&
      std::min(u,v)>=-eps&&std::max(u,v)<=run->width+eps&&
      std::abs(s-std::round(s/run->pitch)*run->pitch)<=eps,"partial tread follows the same pitch and clear width");
     const bool clipped=std::any_of(entry->planEdges.begin(),entry->planEdges.end(),[&](const auto& edge){
      if(edge.role!="BREAK"||edge.points.size()<2)return false;
      const AcGeLineSeg3d cut(edge.points[0],edge.points[1]);
      return (cut.distanceTo(a)<=eps&&(std::abs(v)<=eps||std::abs(v-run->width)<=eps))||
             (cut.distanceTo(b)<=eps&&(std::abs(u)<=eps||std::abs(u-run->width)<=eps));
     });
     require(clipped,"retained partial tread must terminate at the actual registered cut and full run edge");
    }
    entry->planEdges.push_back({role,handle,archiveHandle,points});
    continue;
   }
   if(tag=="GUARDPAIR"){
    std::string flight,inner,outer,mode,extra;double width{};
    require(bool(record>>flight>>inner>>outer>>width>>mode)&&!(record>>extra)&&
      inner!=outer&&(mode=="KEEP"||mode=="CLOSE")&&std::isfinite(width)&&width>0,
      "guard pair needs two source boundaries, actual width and closure intent");
    const auto entry=std::find_if(rows.begin(),rows.end(),[&](const auto& prior){return prior.key==flight;});
    require(entry!=rows.end(),"guard pair must identify a registered flight");
    const auto inside=productStairGuardPath(db,inner,.01/unit),outside=productStairGuardPath(db,outer,.01/unit);
    const bool close=mode=="CLOSE";
    require(close==inside.second&&(!outside.second||close)&&
      bcpproduct::planGuardPairMatches(inside.first,outside.first,close,width/unit,.01/unit),
      "guard boundaries do not form the specified constant-width open or closed path");
    for(const auto& handle:{inner,outer}){
     const auto g=chainGrips(find(db,handle));const auto centre=g.front()+(g.back()-g.front())*.5;
     require(bcpproduct::nearestGenericView(views,{centre.x,centre.y})+1==entry->panel,
       "guard boundaries belong to another floor");
    }
    if(close&&!outside.second){
     auto boundary=editObject<AcDbPolyline>(find(db,outer));
     boundary->setClosed(true);
     require(boundary->isClosed(),"complete confirmed guard return");
    }
    for(const auto& handle:{inner,outer}){
     auto edge=editObject<AcDbEntity>(find(db,handle));
     status(edge->setLayer(productPlanLayer(db,L"STAIR",2)),"stair guard layer");
     status(edge->setColorIndex(256),"stair guard layer colour");
    }
    for(const auto& handle:{inner,outer})entry->planEdges.push_back({"GUARD",handle,"",chainGrips(find(db,handle))});
    entry->guardPairs.push_back({inner,outer,width,close});
    continue;
   }
   require(tag=="STRAIGHT"&&bool(record>>row.key>>direction)&&(direction==1||direction==-1),"straight flight association");
   row.inputDirection=direction;
   std::string handle;
   std::vector<bcpproduct::PlanStairTread> treads;
   while(record>>handle){
    require(used.insert(handle).second,"a tread can serve only one flight");
    const auto entity=open<AcDbEntity>(find(db,handle));
    const auto tread=productStairSourceTread(*entity);
    require(tread.has_value(),"associated tread must be a single straight edge");
    row.treads.push_back(handle);treads.push_back(*tread);
   }
   require(!treads.empty(),"source stair run has no treads");
   row.sourceTreadStrokes=treads;
   row.panel=bcpproduct::nearestGenericView(views,{treads.front().first.x,treads.front().first.y})+1;
   const auto flight=bcpproduct::planStairFlight(treads,.01/unit);
   row.state="pending";
   row.issue=u8"已定位源踏步；踏步线不能唯一拟合成等宽等距直线梯跑";
   if(flight){
    row.width=flight->width*unit;row.pitch=flight->pitch*unit;
    auto id=productCreateStraightStair(db,*flight,treads,direction,unit);
    // The dialog can retain the previous run's settings on its first use.
    // Retry the same verified run. The factory removes every failed trial.
    if(id.isNull())id=productCreateStraightStair(db,*flight,treads,direction,unit);
    if(!id.isNull()){
     const auto layer=productPlanLayer(db,L"STAIR",2);
     {auto entity=editObject<AcDbEntity>(id);status(entity->setLayer(layer),"native stair layer");status(entity->setColorIndex(256),"stair ByLayer");}
     row.object=axisHandle(id);row.grips=chainGrips(id);
     row.state="plan-written";
     row.issue=u8"已替换平面直段；整跑竖向级数、级高及上下端完成面待复核，保留原剖断、上下行及平台表达";
     for(const auto& h:row.treads)status(editObject<AcDbEntity>(find(db,h))->erase(),"retire exactly reproduced tread");
     const auto& p=flight->first;
     const AcGePoint3d a(p.x,p.y,0),b(p.x+flight->across.x*flight->width,p.y+flight->across.y*flight->width,0);
     productStairDimension(db,material,row,a,b,-1,unit,scale,job);
    }else row.issue=u8"直段尺寸已从源图核实；天正公开命令未得到与源踏步吻合的构件，保留原线";
   }
   if(!job.architecturalPlanOnly||row.state!="plan-written")
    job.detailPending.push_back({u8"室内楼梯逐跑",row.key,row.issue});
   rows.push_back(std::move(row));
  }
  require(ended,"stair plan must end with END");
 }
 for(const auto& [handle,type]:modelTypes(db)){
  const auto key=ascii(handle);
  if(std::any_of(rows.begin(),rows.end(),[&](const auto& row){return row.object==key;}))continue;
  if(type.find(L"Stair")==std::wstring::npos&&type!=L"AcDbPolyline")continue;
  const auto entity=open<AcDbEntity>(find(db,key));
  if(entity->isAProxy()||entity->visibility()!=AcDb::kVisible)continue;
  const bool boundary=type==L"AcDbPolyline"&&symbolName(entity->layerId())==L"楼梯间边界";
  const bool native=type.rfind(L"TDb",0)==0&&type.find(L"Stair")!=std::wstring::npos;
  if(!boundary&&!native)continue;
  AcDbExtents extents;if(entity->getGeomExtents(extents)!=Acad::eOk)continue;
  const auto center=extents.minPoint()+(extents.maxPoint()-extents.minPoint())*.5;
  ProductStairRow row;
  row.key=key;row.source=key;row.panel=bcpproduct::nearestGenericView(views,{center.x,center.y})+1;
  row.state=native?"native-preserved":"boundary-pending";
  row.grips=chainGrips(entity->objectId());
  if(native){
   row.object=key;
   row.nativeType=type;
   row.nativeLayer=symbolName(entity->layerId());
   const auto layer=open<AcDbLayerTableRecord>(entity->layerId());
   if(!layer->isOff()&&!layer->isFrozen())row.visibleStrokes=productStairVisibleStrokes(*entity,.01/unit);
   row.issue=u8"已保留原生梯组；需按本层剖断和平台核单跑上下端完成面、实际级数、级高及衔接";
   job.detailPending.push_back({u8"室内楼梯逐跑",key,row.issue});
  }
  else{
   row.issue=u8"已定位楼梯间边界；梯跑、实际踏步、平台、剖断和上下端的对应关系待核";
   job.detailPending.push_back({u8"室内楼梯逐跑",key,row.issue});
   const auto* poly=AcDbPolyline::cast(entity.get());
   if(poly&&poly->isClosed()&&poly->numVerts()==4){
    std::vector<AcGePoint3d> points;
    for(unsigned i=0;i<4;++i){
     AcGePoint3d point;
     if(poly->segType(i)!=AcDbPolyline::kLine||poly->getPointAt(i,point)!=Acad::eOk)break;
     points.push_back(point);
    }
    if(points.size()==4){
     const auto a=points[1]-points[0],b=points[3]-points[0];
     if(a.length()>.01/unit&&b.length()>.01/unit&&std::abs(a.normal().dotProduct(b.normal()))<1e-8&&
        points[2].distanceTo(points[0]+a+b)<.01/unit){
      const auto side=a.crossProduct(b).z>0?-1.:1.;
      productStairDimension(db,material,row,points[0],points[1],side,unit,scale,job);
      productStairDimension(db,material,row,points[0],points[3],-side,unit,scale,job);
     }
    }
   }
  }
  rows.push_back(std::move(row));
 }
 return rows;
}

void productArrangeIndoorStairDimensions(ProductJob& job,AcDbDatabase& db,
 const std::vector<ProductStairRow>& rows,double scale,double unit) {
 Working scope(&db);
 if(std::none_of(rows.begin(),rows.end(),[](const auto& row){return !row.dimensions.empty();}))return;
 std::vector<std::pair<AcGePoint3d,AcGePoint3d>> stairInk;
 for(const auto& [handle,type]:modelTypes(db)){
  if(type!=L"AcDbLine"&&type!=L"AcDbPolyline"&&type!=L"AcDbBlockReference"&&type!=L"TDbSymbArrow")continue;
  const auto entity=open<AcDbEntity>(find(db,ascii(handle)));
  if(entity->visibility()!=AcDb::kVisible)continue;
  const auto layer=open<AcDbLayerTableRecord>(entity->layerId());
  if(layer->isOff()||layer->isFrozen()||!layer->isPlottable())continue;
  if(symbolName(entity->layerId())==L"DOTE")continue;
  std::vector<bcpproduct::PlanStairTread> lines;
  productStairInk(*entity,lines);
  for(const auto& line:lines)stairInk.emplace_back(AcGePoint3d(line.first.x,line.first.y,0),
                                                  AcGePoint3d(line.second.x,line.second.y,0));
 }
 for(const auto& row:rows)for(const auto& dimension:row.dimensions){
  const auto id=find(db,dimension.handle);
  const auto grips=chainGrips(id);
  const auto direction=grips[3]-grips[2];
  const bool vertical=std::abs(direction.x)*unit<.01;
  if(!vertical&&std::abs(direction.y)*unit>=.01){
   job.detailPending.push_back({u8"楼梯尺寸避让",dimension.handle,u8"斜向梯宽的文字净空待复核"});
   continue;
  }
  productGenericInteriorClearance(job,db,id,vertical,scale/unit,&stairInk,false,false,true);
 }
}

void productWriteStairVerticalNotes(ProductJob& job,AcDbDatabase& db,std::vector<ProductStairRow>& rows,
 const std::vector<bcpproduct::GenericView>& views,double paper){
 Working work(&db);
 if(std::none_of(rows.begin(),rows.end(),[](const auto& row){return row.risers>0&&!row.sourceTreadStrokes.empty();}))return;
 const auto frames=productPlanDrawingFrames(db,paper);
 std::vector<AcDbExtents> obstacles;
 std::vector<std::pair<AcGePoint3d,AcGePoint3d>> annotationStrokes;
 const auto recordDimension=[&](const AcDbAlignedDimension& segment){
  const auto first=segment.xLine1Point(),second=segment.xLine2Point();
  const auto along=second-first;
  if(along.length()<=1e-8)return;
  const auto tangent=along.normal();
  const auto projected=[&](const AcGePoint3d& point){
   return segment.dimLinePoint()+tangent*((point-segment.dimLinePoint()).dotProduct(tangent));
  };
  const auto firstBaseline=projected(first),secondBaseline=projected(second);
  annotationStrokes.emplace_back(firstBaseline,secondBaseline);
  for(const auto& station:{first,second}){
   const auto baseline=projected(station);
   const auto extension=baseline-station;
   const double offset=segment.dimexo()*segment.dimscale();
   if(extension.length()<=std::max(1e-8,offset))continue;
   const auto direction=extension.normal();
   annotationStrokes.emplace_back(station+direction*offset,baseline+direction*(segment.dimexe()*segment.dimscale()));
  }
 };
 for(const auto& [handle,type]:modelTypes(db)){
  if(type.find(L"Dimension")==std::wstring::npos&&type!=L"AcDbText"&&type!=L"AcDbMText"&&
     type!=L"TDbText"&&type!=L"TDbMText"&&type!=L"TDbDrawingName"&&type!=L"TDbAxisLabelSet"&&
     type!=L"TDbSymbDrawingIndex"&&type!=L"TDbSymbElevation"&&type!=L"TDbSymbMultiLeader"&&
     type!=L"TDbOpening"&&type!=L"AcDbLine"&&type!=L"AcDbPolyline"&&type!=L"AcDbBlockReference")continue;
  const auto entity=open<AcDbEntity>(find(db,ascii(handle)));
  if(!productPlanDimensionVisible(*entity)||!open<AcDbLayerTableRecord>(entity->layerId())->isPlottable())continue;
  if(type==L"AcDbBlockReference"){
   AcDbExtents bounds;
   if(entity->getGeomExtents(bounds)==Acad::eOk&&
      (bounds.maxPoint()-bounds.minPoint()).length()<75*paper)obstacles.push_back(bounds);
  }else if(type==L"AcDbLine"||type==L"AcDbPolyline"){
   if(symbolName(entity->layerId())!=L"DOTE")continue;
   GenericOpeningInk ink;
   productGenericOpeningInk(*entity,ink);
   annotationStrokes.insert(annotationStrokes.end(),ink.straight.begin(),ink.straight.end());
  }else if(type==L"TDbDimension2"){
   const auto glyphs=productPlanChainGlyphs(db,*entity);
   obstacles.insert(obstacles.end(),glyphs.begin(),glyphs.end());
   TemplateParts parts(entity.get());
   if(entity->explode(parts.values)==Acad::eOk)
    for(int i=0;i<parts.values.length();++i)
     if(const auto* segment=AcDbAlignedDimension::cast(static_cast<AcRxObject*>(parts.values[i])))recordDimension(*segment);
  }else productGenericTextLeaves(*entity,obstacles);
  if(const auto* segment=AcDbAlignedDimension::cast(entity.get()))recordDimension(*segment);
 }
 for(auto& row:rows){
  if(row.risers<=0||row.sourceTreadStrokes.empty())continue;
  const auto& edge=row.sourceTreadStrokes.front();
  const AcGePoint3d a(edge.first.x,edge.first.y,0),b(edge.second.x,edge.second.y,0);
  const auto across=(b-a).normal();
  const auto normal=AcGeVector3d(-across.y,across.x,0);
  std::wostringstream value;
  value<<std::fixed<<std::setprecision(4)<<row.lowerMM/1000.<<L" → "<<row.upperMM/1000.
       <<L" m\\P"<<row.risers<<L"级，级高 "<<std::setprecision(2)<<(row.upperMM-row.lowerMM)/row.risers<<L" mm";
  row.verticalText=value.str();
  const auto middle=a+(b-a)*.5;
  const auto place=[&](const AcGePoint3d& point){
   auto note=std::make_unique<AcDbMText>();
   note->setDatabaseDefaults(&db);
   productStatus(note->setLayer(productPlanLayer(db,L"A-TEXT-DESC",7)),"stair level note layer");
   productStatus(note->setTextStyle(db.textstyle()),"stair level note style");
   productStatus(note->setTextHeight(2.5*paper),"stair level note height");
   productStatus(note->setAttachment(AcDbMText::kMiddleCenter),"stair level note attachment");
   productStatus(note->setLocation(point),"stair level note position");
   note->setContents(row.verticalText.c_str());
   AcDbExtents ext;
   const auto hits=[&](const auto& glyph){return productGenericBoxesOverlap(ext,glyph,.5*paper);};
   const auto clear=[&]{
    return std::none_of(obstacles.begin(),obstacles.end(),hits)&&
     std::none_of(annotationStrokes.begin(),annotationStrokes.end(),[&](const auto& line){
      return productGenericSegmentHitsBox(line.first,line.second,ext,.3*paper);
     });
   };
   if(note->getGeomExtents(ext)!=Acad::eOk||!productPlanInsideFrame(frames,middle,ext,5*paper)||!clear())return false;
   const auto id=productAppendPlanDetail(db,std::move(note));
   if(open<AcDbMText>(id)->getGeomExtents(ext)!=Acad::eOk||!clear()){
    productStatus(editObject<AcDbEntity>(id)->erase(),"discard stair note with annotation collision");
    return false;
   }
   obstacles.push_back(ext);
   row.verticalNote=axisHandle(id);
   return true;
  };
  if(row.panel>0&&row.panel<=views.size()){
   const auto& bounds=views[row.panel-1].bounds;
   for(const double gap:{10.,8.,12.,14.,16.,18.,20.,23.,26.,30.,34.,38.,42.,46.,50.,56.,62.,68.,74.,80.,86.,92.}){
    for(const double shift:{0.,-4.,4.,-8.,8.,-12.,12.,-16.,16.,-20.,20.,-24.,24.,-28.,28.,
                            -36.,36.,-44.,44.,-52.,52.,-60.,60.,-68.,68.}){
     const AcGePoint3d point(middle.x+shift*paper,bounds.ymax+gap*paper,0);
     if((!frames.empty()||bcpproduct::nearestGenericView(views,{point.x,point.y})+1==row.panel)&&place(point))break;
    }
    if(!row.verticalNote.empty())break;
   }
   if(!row.verticalNote.empty())continue;
   for(const double side:{-80.,80.,-100.,100.,-120.,120.}){
    const double x=(side<0?bounds.xmin:bounds.xmax)+side*paper;
    for(double y=bounds.ymax;y>=bounds.ymin;y-=5*paper){
     const AcGePoint3d point(x,y,0);
     if((!frames.empty()||bcpproduct::nearestGenericView(views,{point.x,point.y})+1==row.panel)&&place(point))break;
    }
    if(!row.verticalNote.empty())break;
   }
   if(!row.verticalNote.empty())continue;
  }
  std::vector<AcGePoint3d> anchors{middle,a,b};
  for(const double shift:{5.,10.,15.,20.,25.,30.,35.,40.}){
   anchors.push_back(a-across*(shift*paper));
   anchors.push_back(b+across*(shift*paper));
  }
  for(const double offset:{6.,-6.,8.,-8.,10.,-10.,12.,-12.,14.,-14.,16.,-16.,18.,-18.,
                           20.,-20.,22.,-22.,25.,-25.,28.,-28.,32.,-32.,36.,-36.,40.,-40.,42.,-42.}){
   for(const auto& anchor:anchors){
    if(place(anchor+normal*(offset*paper)))break;
   }
   if(!row.verticalNote.empty())break;
  }
  if(row.verticalNote.empty())job.detailPending.push_back({u8"楼梯竖向落字",row.key,u8"已核实剖面级数与标高，邻近注释无合适净空"});
 }
}

void productVerifyIndoorStairs(ProductJob& job,AcDbDatabase& db,const std::vector<ProductStairRow>& rows,double unit,
 const fs::path& path,const std::vector<ProductStairDirectionMarker>& markers={}){
 Working scope(&db);
 std::ostringstream out;
 out<<std::setprecision(17)<<"{\"savedReopened\":true,\"flights\":[";
 bool first=true;
 for(const auto& row:rows){
  if(!first)out<<',';
  first=false;
  const auto handle=row.object.empty()?row.source:row.object;
  std::vector<bcpproduct::PlanStairFlight> visibleRuns;
  std::wstring nativeType,nativeLayer;
  std::size_t publicStrokeCount{};
  bool nativePlanVerified=false;
  bool sourceLinesReplaced=false;
  if(!handle.empty()){
   const auto id=find(db,handle);
   const auto actual=chainGrips(id);
   require(actual.size()==row.grips.size()&&std::equal(actual.begin(),actual.end(),row.grips.begin(),
       [unit](const auto& a,const auto& b){return a.distanceTo(b)*unit<.01;}),"saved stair actual points");
   if(row.state=="native-preserved"){
    const auto entity=open<AcDbEntity>(id);
    const auto layer=open<AcDbLayerTableRecord>(entity->layerId());
    nativeType=entity->isA()->name();
    nativeLayer=symbolName(entity->layerId());
    require(!entity->isAProxy()&&entity->visibility()==AcDb::kVisible&&row.nativeType==nativeType&&
            row.nativeLayer==nativeLayer,"saved original native stair type and layer");
    const auto strokes=layer->isOff()||layer->isFrozen()?std::vector<bcpproduct::PlanStairTread>{}:
        productStairVisibleStrokes(*entity,.01/unit);
    require(strokes.size()==row.visibleStrokes.size()&&std::all_of(strokes.begin(),strokes.end(),[&](const auto& stroke){
      return std::any_of(row.visibleStrokes.begin(),row.visibleStrokes.end(),[&](const auto& original){
       return productStairSameStroke(stroke,original,.01/unit);
      });
     }),"saved native stair public plan strokes");
    nativePlanVerified=!strokes.empty()&&layer->isPlottable()&&nativeLayer==L"STAIR";
    publicStrokeCount=strokes.size();
    visibleRuns=productStairVisibleRuns(strokes,.01/unit);
   }
   if(row.state=="plan-written"){
    const auto entity=open<AcDbEntity>(id);
    nativeType=entity->isA()->name();
    nativeLayer=symbolName(entity->layerId());
    require(nativeType==L"TDbLineStair"&&!entity->isAProxy(),"saved stair native type");
    const auto strokes=productStairVisibleStrokes(*entity,.01/unit);
    const auto layer=open<AcDbLayerTableRecord>(entity->layerId());
    nativePlanVerified=nativeLayer==L"STAIR"&&layer->isPlottable()&&productPlanDimensionVisible(*entity)&&
     !row.sourceTreadStrokes.empty()&&std::all_of(row.sourceTreadStrokes.begin(),row.sourceTreadStrokes.end(),[&](const auto& source){
     return std::any_of(strokes.begin(),strokes.end(),[&](const auto& actual){return productStairSameStroke(source,actual,.35/unit);});
    });
    publicStrokeCount=strokes.size();
    visibleRuns=productStairVisibleRuns(strokes,.01/unit);
    for(const auto& tread:row.treads){
     AcDbObjectId old;
     const auto wide=bcpproduct::decodeMessage(tread);
     const auto result=db.getAcDbObjectId(old,false,AcDbHandle(wide.c_str()));
     require(result!=Acad::eOk||old.isErased(),"source tread remains after confirmed replacement");
    }
    sourceLinesReplaced=nativePlanVerified;
   }
  }
  auto retained=handle.empty()?std::vector<std::string>{}:std::vector<std::string>{handle};
  std::set<std::string> verifiedRoles;
  bool allEdgesVerified=true;
  for(const auto& edge:row.planEdges){
   const auto id=find(db,edge.handle);
   const auto entity=open<AcDbEntity>(id);
   const auto actual=chainGrips(id);
   if(productPlanDimensionVisible(*entity)&&actual.size()==edge.points.size()&&
      std::equal(actual.begin(),actual.end(),edge.points.begin(),[&](const auto& a,const auto& b){return a.distanceTo(b)*unit<.01;})){
    verifiedRoles.insert(edge.role);
    retained.push_back(edge.handle);
   }else allEdgesVerified=false;
  }
  for(const auto& pair:row.guardPairs){
   const auto inner=productStairGuardPath(db,pair.inner,.01/unit),outer=productStairGuardPath(db,pair.outer,.01/unit);
   const bool verified=inner.second==pair.closed&&outer.second==pair.closed&&
    bcpproduct::planGuardPairMatches(inner.first,outer.first,pair.closed,pair.width/unit,.01/unit);
   if(!verified){
    verifiedRoles.erase("GUARD");
    allEdgesVerified=false;
   }
   std::ostringstream evidence;
   evidence<<"{\"panel\":"<<row.panel<<",\"inner\":\""<<pair.inner<<"\",\"outer\":\""<<pair.outer
    <<"\",\"widthMM\":"<<pair.width<<",\"closed\":"<<(pair.closed?"true":"false")
    <<",\"scope\":\"paired-guard-boundaries-and-turns\",\"savedReopened\":true,\"result\":\""
    <<(verified?"verified":"pending")<<"\"}";
   productRecordRuleEvidence(job,"SE19",evidence.str());
  }
  bool verticalWritten=false;
  if(!row.verticalNote.empty()){
   const auto entity=open<AcDbEntity>(find(db,row.verticalNote));
   const auto* text=AcDbMText::cast(entity.get());
   AcString actual;
   verticalWritten=text&&productPlanDimensionVisible(*entity)&&text->contents(actual)==Acad::eOk&&actual==row.verticalText.c_str();
   if(verticalWritten)retained.push_back(row.verticalNote);
  }
  const bool constructionFlightVerified=sourceLinesReplaced&&verticalWritten&&allEdgesVerified&&verifiedRoles.count("LANDING")&&
   verifiedRoles.count("BREAK")&&verifiedRoles.count("GUARD");
  const bool existingPlanVerified=row.state=="native-preserved"&&nativePlanVerified&&!visibleRuns.empty();
  const bool planFlightVerified=(sourceLinesReplaced||existingPlanVerified)&&allEdgesVerified&&(row.risers==0||verticalWritten);
  const bool flightVerified=job.architecturalPlanOnly?planFlightVerified:constructionFlightVerified;
  const bool failedPlanReadback=!allEdgesVerified||(row.state=="plan-written"&&!sourceLinesReplaced)||
   (!row.verticalNote.empty()&&!verticalWritten);
  auto flightIssue=row.issue;
  if(failedPlanReadback)flightIssue=u8"已写图元保存重开未通过。须核实际梯段、楼梯层及本次已核定的边线或标注。";
  else if(constructionFlightVerified)flightIssue=u8"原踏步、剖面竖向及本幅平台、剖断和栏杆边线重开核验通过";
  else if(verticalWritten){
   flightIssue=u8"剖面竖向落字已重开核验";
   if(nativePlanVerified)flightIssue+=u8"，原踏步已核";
   if(verifiedRoles.count("LANDING"))flightIssue+=u8"，平台边已核";
   if(verifiedRoles.count("BREAK"))flightIssue+=u8"，剖断线已核";
   flightIssue+=u8"。待核";
   if(!verifiedRoles.count("LANDING"))flightIssue+=u8"平台边、";
   if(!verifiedRoles.count("BREAK"))flightIssue+=u8"剖断适用性、";
   flightIssue+=verifiedRoles.count("GUARD")?u8"各项衔接":u8"临空栏杆起止与转角衔接";
  }
  if(!row.source.empty()){
   const bool retainedPlan=job.architecturalPlanOnly&&planFlightVerified&&existingPlanVerified;
   productCheckPlanSource(job,row.panel,"stair",row.source,retainedPlan?bcpproduct::PlanCheck::verified:
    failedPlanReadback?bcpproduct::PlanCheck::failed:bcpproduct::PlanCheck::pending,retained,
    retainedPlan?u8"同一原生梯组在 STAIR 层可打印，重开核实实际踏步及平面线迹与源图一致。":flightIssue);
  }
  // Source native groups are covered by their root obligation. Configured
  // individual flights have their own pre-registered source-independent keys.
  if(row.source.empty()){
   const auto result=flightVerified?bcpproduct::PlanCheck::verified:
    failedPlanReadback?bcpproduct::PlanCheck::failed:bcpproduct::PlanCheck::pending;
   const auto reason=job.architecturalPlanOnly&&planFlightVerified?
    u8"原踏步已逐段替换为可打印的 STAIR 层天正梯段，源踏步退出及本次已核定的边线、标注重开核验通过。":flightIssue;
   productCheckPlanSource(job,0,"stair-flight",row.key,result,retained,reason);
  }
  const auto landing=std::find_if(row.planEdges.begin(),row.planEdges.end(),[](const auto& edge){return edge.role=="LANDING";});
  job.planReleaseLocations[{0,"stair-flight",row.key}]={row.panel,landing==row.planEdges.end()?row.key:landing->handle};
  auto& flights=job.ruleEvidence["SE23"];
  if(!flights.empty())flights+=',';
  flights+="{\"panel\":"+std::to_string(row.panel)+",\"object\":\""+handle+"\",\"key\":"+
    jsonUtf8(bcpproduct::decodeMessage(row.key))+",\"result\":\""+(failedPlanReadback?"failed-stair-plan-readback":
     row.state=="native-preserved"?(planFlightVerified?"verified-preserved-native-stair-plan":"pending"):row.state)+
    "\",\"nativePlanVerified\":"+(nativePlanVerified?"true":"false")+",\"visibleRunCount\":"+std::to_string(visibleRuns.size())+",\"detail\":"+
    jsonUtf8(bcpproduct::decodeMessage(row.issue))+"}";
  const auto evidence=[&](const char* rule,const char* scope,const char* result){
   std::ostringstream proof;
   proof<<"{\"panel\":"<<row.panel<<",\"object\":"<<jsonUtf8(bcpproduct::decodeMessage(handle.empty()?row.key:handle))
    <<",\"scope\":\""<<scope<<"\",\"savedReopened\":true,\"nativePlanVerified\":"<<(nativePlanVerified?"true":"false")
    <<",\"visibleGroupCount\":"<<visibleRuns.size()<<",\"confirmedDirectionInput\":"<<row.inputDirection
    <<",\"report\":\"indoor-stairs.json\",\"result\":\""<<result<<"\",\"detail\":"
    <<jsonUtf8(bcpproduct::decodeMessage(row.issue))<<'}';
   productRecordRuleEvidence(job,rule,proof.str());
  };
  evidence("D20","stair-visible-width-and-tread-pitch","pending");
  evidence("SE01","stair-type-and-floor-expression","pending");
  evidence("SE03","stair-project-vertical-and-plan-parameters","pending");
  evidence("SE06","exact-source-stroke-replacement",sourceLinesReplaced?"verified-source-stroke-replacement":row.state=="native-preserved"?"not-applicable":"pending");
  evidence("SE07","native-dialog-insertion-outcome",row.state=="plan-written"&&nativePlanVerified?"verified-native-stair-command-outcome":row.state=="native-preserved"?"not-applicable":"pending");
  evidence("SE08","source-digest-bound-flight-association",row.inputDirection?"verified-project-flight-profile":"not-applicable");
  evidence("SE10","stair-and-landing-plan-coordination",verifiedRoles.count("LANDING")?
   "verified-source-landing-linework":"pending");
  evidence("SE11","stair-actual-riser-and-level-parameters",row.risers>0&&verticalWritten?
   "verified-archived-section-range-and-visible-note":"pending");
  evidence("SE13","exact-source-tread-cleanup",sourceLinesReplaced?"verified-exact-source-tread-cleanup":row.state=="native-preserved"?"not-applicable":"pending");
  evidence("SE14","preserved-native-stair-plan",row.state=="native-preserved"&&nativePlanVerified?"verified-preserved-native-stair-plan":row.state=="plan-written"?"not-applicable":"pending");
  evidence("SE15","native-stair-supported-type-and-placement","pending");
  evidence("SE16","native-stair-public-plan-readback",nativePlanVerified?"verified-native-stair-public-plan-readback":"pending");
  evidence("SE18","stair-direction-and-level-expression","pending");
  evidence("SE19","stair-guard-and-handrail-coordination",verifiedRoles.count("GUARD")?
   "verified-source-guard-linework":"pending");
  out<<"{\"key\":"<<jsonUtf8(bcpproduct::decodeMessage(row.key))<<",\"panel\":"<<row.panel
     <<",\"state\":\""<<row.state<<"\",\"native\":\""<<row.object<<"\",\"source\":\""<<row.source
     <<"\",\"visibleTreads\":";
  if(row.state=="native-preserved")out<<"null,\"widthMM\":null,\"pitchMM\":null";
  else out<<row.treads.size()<<",\"widthMM\":"<<row.width<<",\"pitchMM\":"<<row.pitch;
  out<<",\"nativeType\":"<<jsonUtf8(nativeType)<<",\"layer\":"<<jsonUtf8(nativeLayer)
     <<",\"nativePlanVerified\":"<<(nativePlanVerified?"true":"false")<<",\"publicStrokeCount\":"<<publicStrokeCount
     <<",\"visibleEqualPitchStrokeGroups\":[";
  bool firstRun=true;
  for(const auto& run:visibleRuns){
   if(!firstRun)out<<',';
   firstRun=false;
   out<<"{\"firstWCS\":["<<run.first.x<<','<<run.first.y<<"],\"across\":["<<run.across.x<<','<<run.across.y
      <<"],\"travel\":["<<run.travel.x<<','<<run.travel.y<<"],\"visibleStrokes\":"<<run.visibleTreads
      <<",\"spanMM\":"<<run.width*unit<<",\"pitchMM\":"<<run.pitch*unit<<'}';
  }
  out<<"],\"archivedSection\":";
  if(row.risers==0)out<<"null";
  else out<<"{\"chain\":\""<<row.verticalChain<<"\",\"lower\":\""<<row.verticalLower
   <<"\",\"upper\":\""<<row.verticalUpper<<"\",\"risers\":"<<row.risers
   <<",\"lowerLevelM\":"<<row.lowerMM/1000.<<",\"upperLevelM\":"<<row.upperMM/1000.
   <<",\"riseMM\":"<<(row.upperMM-row.lowerMM)/row.risers<<"}";
  out<<",\"verticalNote\":\""<<row.verticalNote<<"\",\"verticalNoteVerified\":"<<(verticalWritten?"true":"false")
     <<",\"verifiedPlanRoles\":[";
  bool firstRole=true;
  for(const auto& role:verifiedRoles){
   if(!firstRole)out<<',';
   firstRole=false;
   out<<'"'<<role<<'"';
  }
  out<<"],\"planEdgeSources\":[";
  for(std::size_t i=0;i<row.planEdges.size();++i){
   if(i)out<<',';
   const auto& edge=row.planEdges[i];
   out<<"{\"role\":\""<<edge.role<<"\",\"source\":\""<<edge.handle<<"\",\"archive\":\""<<edge.archiveHandle<<"\"}";
  }
  const bool reportDesignContext=job.architecturalPlanOnly&&planFlightVerified&&!constructionFlightVerified;
  out<<"],\"pending\":"<<jsonUtf8(reportDesignContext?L"":bcpproduct::decodeMessage(flightIssue))
     <<",\"designContext\":"<<jsonUtf8(reportDesignContext?bcpproduct::decodeMessage(flightIssue):L"")<<",\"dimensions\":[";
  for(std::size_t k=0;k<row.dimensions.size();++k){
   if(k)out<<',';
    const auto& [dimension,value,expectedVisible]=row.dimensions[k];
    const auto id=find(db,dimension);
    const auto measured=chainValues(id);
    require(measured.size()==1&&std::abs(measured.front()*unit-value)<.01,"saved stair dimension measurement");
    const auto visible=productStairVisibleLength(id);
    const bool defaultLength=visible&&std::abs(*visible*unit-expectedVisible)<.01;
    const bool adjusted=std::any_of(job.drawingChecks.items.begin(),job.drawingChecks.items.end(),[&](const auto& item){
     return std::get<1>(item.first)=="interior-baseline"&&std::get<2>(item.first)==dimension&&
       item.second.state==bcpproduct::PlanCheck::verified&&item.second.basis=="saved-reopened-collision-adjusted";
    });
    if(!defaultLength&&!adjusted)
     job.detailPending.push_back({u8"楼梯尺寸界线",dimension,u8"默认界线长度或实际冲突避让后的净空未通过"});
    job.drawingChecks.record(row.panel,"stair-dimension-baseline",dimension,{"D23","QA09"},
     bcpproduct::PlanReleaseGate::mainPlan,defaultLength||adjusted?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::failed,
     "saved-reopened",defaultLength?u8"默认界线长度通过":adjusted?u8"与室内联合排版的冲突避让复验一致":u8"默认界线长度及避让依据未通过");
    auto& witnesses=job.ruleEvidence["D23"];
    if(!witnesses.empty())witnesses+=',';
    witnesses+="{\"panel\":"+std::to_string(row.panel)+",\"object\":\""+dimension+
       "\",\"result\":\""+(defaultLength?"verified-visible-extension":adjusted?"verified-collision-adjusted-baseline":"pending")+
       "\",\"measuredMM\":"+(visible?std::to_string(*visible*unit):"null")+
       ",\"requiredMM\":"+std::to_string(expectedVisible)+"}";
    out<<"{\"handle\":\""<<dimension<<"\",\"measuredMM\":"<<measured.front()*unit
       <<",\"visibleExtensionMM\":"<<(visible?std::to_string(*visible*unit):"null")
       <<",\"visibleExtensionVerified\":"<<(defaultLength?"true":"false")
       <<",\"collisionAdjusted\":"<<(adjusted?"true":"false")<<'}';
  }
  out<<"]}";
 }
 out<<"],\"unassociatedDirectionMarkers\":[";
 first=true;
 for(const auto& marker:markers){
  if(std::any_of(rows.begin(),rows.end(),[&](const auto& row){
   return row.panel==marker.panel&&(!row.object.empty()||!row.treads.empty());
  }))continue;
  if(!first)out<<',';
  first=false;
  const auto issue=u8"源图有楼梯上下行箭头，本幅未识别到可关联的原生梯组或已核实的源踏步。须逐跑核踏步、平台及剖断，保留源图表达。";
  job.detailPending.push_back({u8"室内楼梯逐跑",std::to_string(marker.panel)+"/"+marker.handle,issue});
  job.drawingChecks.record(marker.panel,"stair-source-association",marker.handle,{"SE23","P04"},
   bcpproduct::PlanReleaseGate::mainPlan,bcpproduct::PlanCheck::pending,"visible-source-direction-marker",issue);
  const auto evidence="{\"panel\":"+std::to_string(marker.panel)+",\"object\":\""+marker.handle+
   "\",\"label\":"+jsonUtf8(marker.label)+",\"result\":\"pending-source-stair-flight-association\",\"detail\":"+
   jsonUtf8(bcpproduct::decodeMessage(issue))+"}";
  productRecordRuleEvidence(job,"SE23",evidence);
  out<<evidence;
 }
 out<<"]}";
 writeNew(path,out.str());
}
