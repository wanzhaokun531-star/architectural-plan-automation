// All associations below are reconstructed from this drawing's wall/opening
// geometry. No source handles, floor counts or building coordinates are preset.
struct GenericOpeningHost {
 std::size_t wall{};
 int side{-1};
 bool vertical{};
 double normal{},lower{},upper{};
 std::optional<double> beforeJamb,afterJamb;
 std::optional<std::size_t> oppositeWall;
};
std::map<std::string,int> productConfirmedMainEntranceSides(const ProductJob& job);
std::map<std::string,int> productConfirmedEntranceSides(const ProductJob& job,bool mainOnly=false);
struct ProductPlanDetailConfirmations {
 std::set<std::string> bathroomDoors;
 std::map<std::string,std::array<double,3>> unnumberedWallOpenings;
 std::map<std::string,std::array<double,2>> securityDoors;
 std::map<std::string,std::array<double,2>> elevationBases;
 std::map<std::string,std::array<std::string,4>> exteriorNameBoundaries;
 struct ExteriorProjection {std::array<std::string,3> edges;std::array<std::string,2> walls;};
 std::map<std::string,ExteriorProjection> exteriorNameProjections;
 std::map<std::set<std::string>,double> combinedRooms;
 std::map<std::string,std::vector<std::array<double,3>>> combinedDomains;
 std::map<std::string,std::array<std::string,4>> titleCompanions;
 std::map<std::string,std::array<std::string,2>> frameCompanions;
 std::map<std::string,std::vector<std::string>> buildingAreaOutlines;
 struct GrossVertex {std::string xSource,ySource;unsigned xIndex{},yIndex{};};
 struct GrossPart {std::string key,basis;double factor{};std::vector<GrossVertex> vertices;};
 std::map<std::string,std::vector<GrossPart>> grossParts;
 std::map<std::string,std::vector<std::string>> grossTotals;
 std::map<std::string,std::pair<std::string,std::string>> redundantSourceLines;
};

ProductPlanDetailConfirmations productPlanDetailConfirmations(const ProductJob& job){
 ProductPlanDetailConfirmations confirmations;
 const auto path=job.unifiedDetailConfirmations.empty()?job.profile/L"plan-policy/plan-detail-confirmations.tsv":job.unifiedDetailConfirmations;
 if(!fs::exists(path))return confirmations;
 std::ifstream stream(path);
 std::string type,hash,line;
 require(bool(stream>>type>>hash)&&type=="PLANDETAIL1","valid plan detail confirmation header");
 if(hash!=job.sourceHash)return confirmations;
 std::getline(stream,line);
 while(std::getline(stream,line)){
  std::istringstream row(line);
  std::string handle;
  require(bool(row>>type),"nonempty plan detail confirmation");
  if(type=="END"){
   row>>std::ws;
   stream>>std::ws;
   require(row.eof()&&stream.eof(),"unexpected text after plan detail confirmations");
   return confirmations;
  }
  require(bool(row>>handle),"confirmed plan detail source handle");
  if(type=="BATHDOOR"){
   require(confirmations.bathroomDoors.insert(handle).second,"duplicate bathroom door confirmation");
  }else if(type=="PLANBASE"){
   std::array<double,2> values{};
   require(bool(row>>values[0]>>values[1])&&
     std::all_of(values.begin(),values.end(),[](double v){return std::isfinite(v);})&&
     confirmations.elevationBases.emplace(handle,values).second,
     "confirmed plan base needs original and target model elevations");
  }else if(type=="SECURITYDOOR"){
   std::array<double,2> sizes{};
   require(bool(row>>sizes[0]>>sizes[1])&&
     std::all_of(sizes.begin(),sizes.end(),[](double v){return std::isfinite(v)&&v>0;})&&
     confirmations.securityDoors.emplace(handle,sizes).second,
     "confirmed exterior security door needs actual width and height in millimetres");
  }else if(type=="WALLOPENING"){
   std::array<double,3> sizes{};
   require(bool(row>>sizes[0]>>sizes[1]>>sizes[2])&&
    std::all_of(sizes.begin(),sizes.end(),[](double value){return std::isfinite(value)&&value>0;})&&
    confirmations.unnumberedWallOpenings.emplace(handle,sizes).second,"confirmed wall void width, height and raised sill");
  }else if(type=="OUTDOORNAME"){
   std::array<std::string,4> boundary;
   for(auto& edge:boundary)require(bool(row>>edge),"four confirmed outdoor name boundary edges");
   require(confirmations.exteriorNameBoundaries.emplace(handle,boundary).second,"duplicate outdoor name confirmation");
  }else if(type=="OUTDOORPROJECTION"){
   ProductPlanDetailConfirmations::ExteriorProjection projection;
   for(auto& edge:projection.edges)require(bool(row>>edge),"three confirmed outdoor projection edges");
   for(auto& wall:projection.walls)require(bool(row>>wall),"actual closure wall at each projection end");
   require(confirmations.exteriorNameProjections.emplace(handle,projection).second,"duplicate outdoor projection confirmation");
  }else if(type=="ROOMDOMAIN"){
   std::array<double,3> domain{};
   require(bool(row>>domain[0]>>domain[1]>>domain[2])&&
     std::all_of(domain.begin(),domain.end(),[](double v){return std::isfinite(v);})&&domain[2]>0,
     "confirmed extra net domain needs interior point and measured square metres");
   confirmations.combinedDomains[handle].push_back(domain);
  }else if(type=="ROOMGROUP"){
   double area{};
   std::string member;
   require(bool(row>>area>>member)&&std::isfinite(area)&&area>0&&member!=handle,
           "confirmed continuous room area and two distinct source names");
   require(confirmations.combinedRooms.emplace(std::set<std::string>{handle,member},area).second,
           "duplicate continuous room confirmation");
  }else if(type=="TITLEGROUP"){
   std::array<std::string,4> companions;
   for(auto& companion:companions)require(bool(row>>companion),"four confirmed title group companions");
   require(confirmations.titleCompanions.emplace(handle,companions).second,"duplicate title group confirmation");
  }else if(type=="FRAMEGROUP"){
   std::array<std::string,2> companions;
   for(auto& companion:companions)require(bool(row>>companion),"confirmed inner frame and title block");
   require(confirmations.frameCompanions.emplace(handle,companions).second,"duplicate frame group confirmation");
  }else if(type=="GROSSPART"){
   ProductPlanDetailConfirmations::GrossPart part;
   std::size_t count{};
   require(bool(row>>part.key>>part.factor>>std::quoted(part.basis)>>count)&&!part.basis.empty()&&
    std::isfinite(part.factor)&&part.factor>=0&&part.factor<=1&&count>=3&&count<=256,
    "gross area part needs explicit inclusion factor, source basis and boundary vertices");
   for(std::size_t k=0;k<count;++k){
    ProductPlanDetailConfirmations::GrossVertex v;
    require(bool(row>>v.xSource>>v.xIndex>>v.ySource>>v.yIndex),"gross boundary vertex source references");
    part.vertices.push_back(v);
   }
   auto& parts=confirmations.grossParts[handle];
   require(std::none_of(parts.begin(),parts.end(),[&](const auto& p){return p.key==part.key;}),"unique gross area part key");
   parts.push_back(std::move(part));
  }else if(type=="GROSSTOTAL"){
   std::vector<std::string> members;std::string member;
   while(row>>member){require(member!=handle&&std::find(members.begin(),members.end(),member)==members.end(),"unique gross total member");members.push_back(member);}
   require(!members.empty()&&confirmations.grossTotals.emplace(handle,members).second,"gross total members");
   row.clear();
  }else if(type=="BUILDINGAREA"){
   std::vector<std::string> outlines;
   std::set<std::string> seen;
   for(std::string outline;row>>outline;){
    require(outline.find_first_not_of("0123456789ABCDEF")==std::string::npos&&seen.insert(outline).second,
            "distinct source building area outline");
    outlines.push_back(outline);
   }
   require(!outlines.empty()&&confirmations.buildingAreaOutlines.emplace(handle,std::move(outlines)).second,
           "unique source building area summary and outline registration");
  }else if(type=="DUPLICATELINE"){
   std::string keeper,role;
   require(bool(row>>keeper>>role)&&keeper!=handle&&(role=="AXIS"||role=="OUTDOOR")&&
      confirmations.redundantSourceLines.emplace(handle,std::make_pair(keeper,role)).second,
      "unique source duplicate line, preserved counterpart and confirmed purpose");
  }else{
   require(false,"unknown plan detail confirmation");
  }
  row>>std::ws;
  require(row.eof(),"unexpected plan detail confirmation field");
 }
 throw std::runtime_error("plan detail confirmations are missing END");
}
void productPlanDimensionStrokes(const AcDbEntity&,std::vector<std::pair<AcGePoint3d,AcGePoint3d>>&);
struct GenericOpeningInk {
 bool swing{};
 std::vector<std::pair<AcGePoint3d, AcGePoint3d>> straight;
 std::vector<AcDbExtents> arcs;
 std::vector<std::pair<AcGePoint3d,AcGePoint3d>> curved;
};
void productGenericOpeningInk(const AcDbEntity& e, GenericOpeningInk& ink, int depth=0) {
 if (depth>8||e.visibility()!=AcDb::kVisible) return;
 if(!e.layerId().isNull()){
  const auto layer=open<AcDbLayerTableRecord>(e.layerId());
  if(layer->isOff()||layer->isFrozen()||!layer->isPlottable())return;
 }
 if(AcDbAlignedDimension::cast(&e)){productPlanDimensionStrokes(e,ink.straight);return;}
 if (const auto* line=AcDbLine::cast(&e)) {
  ink.straight.emplace_back(line->startPoint(),line->endPoint());
  return;
 }
 if (const auto* arc=AcDbArc::cast(&e)) {
  ink.swing=true;
  AcDbExtents bounds;
  if (e.getGeomExtents(bounds)==Acad::eOk) ink.arcs.push_back(bounds);
  double a{},b{};
  if(arc->getStartParam(a)==Acad::eOk&&arc->getEndParam(b)==Acad::eOk){
   AcGePoint3d previous;
   if(arc->getPointAtParam(a,previous)==Acad::eOk)for(int k=1;k<=128;++k){
    AcGePoint3d point;if(arc->getPointAtParam(a+(b-a)*k/128,point)!=Acad::eOk)break;
    ink.curved.emplace_back(previous,point);previous=point;
   }
  }
  return;
 }
 if (const auto* poly=AcDbPolyline::cast(&e)) {
  const unsigned count=poly->numVerts();
  for (unsigned i=0;i<count && (poly->isClosed() || i+1<count);++i) {
   double bulge{};
   if (poly->getBulgeAt(i,bulge)!=Acad::eOk) continue;
   if (std::abs(bulge)>1e-6) {
    ink.swing=true;
    AcGeCircArc3d arc;
    if (poly->getArcSegAt(i,arc)==Acad::eOk) {
     AcDbArc part(arc.center(),arc.normal(),arc.radius(),arc.startAng(),arc.endAng());
     productGenericOpeningInk(part,ink,depth+1);
    }
    continue;
   }
   AcGePoint3d a,b;
   if (poly->getPointAt(i,a)==Acad::eOk && poly->getPointAt((i+1)%count,b)==Acad::eOk)
    ink.straight.emplace_back(a,b);
  }
  return;
 }
 if (AcDbText::cast(&e) || AcDbMText::cast(&e)) return;
 TemplateParts parts(&e);
 if (e.explode(parts.values)!=Acad::eOk) return;
 for (int i=0;i<parts.values.length();++i)
  if (const auto* child=AcDbEntity::cast(static_cast<AcRxObject*>(parts.values[i])))
   productGenericOpeningInk(*child,ink,depth+1);
}
bool productGenericSlidingTracks(const GenericOpeningInk& ink,const GenericOpeningHost& host,
                                 double low,double high,double wallThickness) {
 if (ink.swing || high-low<1e-4) return false;
 const double minimumRun=(high-low)*0.2;
 std::vector<double> lanes;
 for (const auto& span:ink.straight) {
  const auto station=[&](const AcGePoint3d& p){return host.vertical?p.y:p.x;};
  const auto normal=[&](const AcGePoint3d& p){return host.vertical?p.x:p.y;};
  const double a=station(span.first),b=station(span.second),n=normal(span.first);
  if (std::abs(n-normal(span.second))>1e-4 || std::abs(n-host.normal)>wallThickness*0.65 ||
      std::min(high,std::max(a,b))-std::max(low,std::min(a,b))<minimumRun) continue;
  if (std::none_of(lanes.begin(),lanes.end(),[&](double lane){return std::abs(n-lane)<1.;})) lanes.push_back(n);
 }
 return lanes.size()>=2;
}
std::optional<GenericOpeningHost> productGenericHost(const GenericNativeOpening& o,const GenericNativeInput& input,const bcpproduct::GenericView& view){
 AcGePoint3d first=o.a,last=o.b;
 if(o.grouped){
  bcp::Opening group;
  group.width=o.width;
  group.endpointCandidates=o.groupSnapEndpoints;
  std::vector<bcp::Wall> walls;
  for(const auto index:view.walls)if(!input.walls[index].low&&input.walls[index].orthogonal)
   walls.push_back(input.walls[index].wall);
  const auto resolved=bcp::resolveGroupEndpoints(group,walls);
  if(!resolved)return {};
  first={resolved->first.x,resolved->first.y,0};last={resolved->second.x,resolved->second.y,0};
 }
 const bool vertical=std::abs(first.x-last.x)<1e-5;if(!vertical&&std::abs(first.y-last.y)>=1e-5)return {};
 const double normal=vertical?(first.x+last.x)/2:(first.y+last.y)/2,lo=vertical?std::min(first.y,last.y):std::min(first.x,last.x),hi=vertical?std::max(first.y,last.y):std::max(first.x,last.x),mid=(lo+hi)/2;
 std::optional<GenericOpeningHost> best;double score=1e100;
 for(auto index:view.walls){const auto& wall=input.walls[index];if(wall.low||!wall.orthogonal)continue;const auto a=bcp::bodyCentreStart(wall.wall);const bcp::Point b{a.x+wall.wall.end.x-wall.wall.start.x,a.y+wall.wall.end.y-wall.wall.start.y};if((std::abs(a.x-b.x)<1e-5)!=vertical)continue;const double n=vertical?a.x:a.y,l=vertical?std::min(a.y,b.y):std::min(a.x,b.x),h=vertical?std::max(a.y,b.y):std::max(a.x,b.x);if(mid<l-1e-5||mid>h+1e-5||std::abs(n-normal)>wall.total/2+1e-4)continue;const double value=std::abs(n-normal);if(value<score){score=value;best=GenericOpeningHost{index,-1,vertical,n,l,h};}}
 if(!best)return {};
 bool negative=false,positive=false;for(auto index:view.walls){const auto& wall=input.walls[index];if(wall.low||!wall.orthogonal)continue;const auto a=bcp::bodyCentreStart(wall.wall);const bcp::Point b{a.x+wall.wall.end.x-wall.wall.start.x,a.y+wall.wall.end.y-wall.wall.start.y};if((std::abs(a.x-b.x)<1e-5)!=vertical)continue;const double n=vertical?a.x:a.y,l=vertical?std::min(a.y,b.y):std::min(a.x,b.x),h=vertical?std::max(a.y,b.y):std::max(a.x,b.x);if(mid<l-1e-5||mid>h+1e-5)continue;if(n<best->normal-1e-5)negative=true;if(n>best->normal+1e-5)positive=true;}
 if(!negative&&positive)best->side=vertical?0:2;else if(negative&&!positive)best->side=vertical?1:3;
 if(o.grouped){best->beforeJamb=lo;best->afterJamb=hi;}
 return best;
}

std::optional<GenericOpeningHost> productConfirmedSplitOpeningHost(const GenericNativeOpening& opening,
 const GenericNativeInput& input,const bcpproduct::GenericView& view,int exteriorSide,double unit){
 const double tolerance=.01/unit;
 const bool vertical=exteriorSide<2;
 const double aNormal=vertical?opening.a.x:opening.a.y;
 const double bNormal=vertical?opening.b.x:opening.b.y;
 if(std::abs(aNormal-bNormal)>tolerance)return {};
 const double normal=(aNormal+bNormal)/2;
 const double aStation=vertical?opening.a.y:opening.a.x;
 const double bStation=vertical?opening.b.y:opening.b.x;
 const auto [lower,upper]=std::minmax(aStation,bStation);
 if(std::abs(upper-lower-opening.width)>tolerance)return {};
 std::vector<bcpproduct::OpeningStationWall> walls;
 std::vector<std::size_t> indices;
 for(const auto index:view.walls){
  const auto& wall=input.walls[index];
  if(wall.low||!wall.orthogonal)continue;
  const auto start=bcp::bodyCentreStart(wall.wall);
  const bcp::Point end{start.x+wall.wall.end.x-wall.wall.start.x,start.y+wall.wall.end.y-wall.wall.start.y};
  const bool wallVertical=std::abs(start.x-end.x)<=tolerance;
  if(wallVertical!=vertical)continue;
  const auto first=vertical?start.y:start.x;
  const auto last=vertical?end.y:end.x;
  walls.push_back({vertical,vertical?start.x:start.y,std::min(first,last),std::max(first,last),wall.total});
  indices.push_back(index);
 }
 const auto jambs=bcpproduct::openingSplitWallJambs(walls,vertical,normal,lower,upper,tolerance);
 if(!jambs)return {};
 const auto& before=walls[jambs->first];
 const auto& after=walls[jambs->second];
 return GenericOpeningHost{indices[jambs->first],exteriorSide,vertical,normal,before.lower,after.upper,
                           before.upper,after.lower,indices[jambs->second]};
}

void productGenericTextLeaves(const AcDbEntity& e,std::vector<AcDbExtents>& text,int depth=0){
 if(depth>8)return;if(AcDbText::cast(&e)||AcDbMText::cast(&e)){AcDbExtents b;if(e.getGeomExtents(b)==Acad::eOk)text.push_back(b);return;}if(AcDbLine::cast(&e)||AcDbArc::cast(&e))return;TemplateParts p(&e);if(e.explode(p.values)!=Acad::eOk)return;for(int k=0;k<p.values.length();++k)if(auto* child=AcDbEntity::cast(static_cast<AcRxObject*>(p.values[k])))productGenericTextLeaves(*child,text,depth+1);
}

void productOpeningDimensionDisplayInk(const AcDbEntity& entity,GenericOpeningInk& ink,int depth=0){
 if(depth>8||entity.visibility()!=AcDb::kVisible)return;
 if(const auto* solid=AcDbSolid::cast(&entity)){
  std::array<AcGePoint3d,4> points;
  for(Adesk::UInt16 i=0;i<points.size();++i)if(solid->getPointAt(i,points[i])!=Acad::eOk)return;
  for(const auto& edge:std::array<std::pair<int,int>,4>{{{0,1},{1,3},{3,2},{2,0}}})
   if(points[edge.first].distanceTo(points[edge.second])>1e-8)
    ink.straight.emplace_back(points[edge.first],points[edge.second]);
  return;
 }
 if(AcDbLine::cast(&entity)||AcDbArc::cast(&entity)||AcDbPolyline::cast(&entity)){
  productGenericOpeningInk(entity,ink,depth);
  return;
 }
 if(AcDbText::cast(&entity)||AcDbMText::cast(&entity))return;
 const auto count=ink.straight.size()+ink.curved.size()+ink.arcs.size();
 TemplateParts parts(&entity);
 if(entity.explode(parts.values)==Acad::eOk)
  for(int i=0;i<parts.values.length();++i)
   if(const auto* child=AcDbEntity::cast(static_cast<AcRxObject*>(parts.values[i]));child&&child!=&entity)
    productOpeningDimensionDisplayInk(*child,ink,depth+1);
 // Some transient native dimension parts do not provide their display block.
 // Keep those conservative until their visible output can be read.
 if(AcDbAlignedDimension::cast(&entity)&&count==ink.straight.size()+ink.curved.size()+ink.arcs.size())
  productPlanDimensionStrokes(entity,ink.straight);
}

bool productGenericBoxesOverlap(const AcDbExtents& a,const AcDbExtents& b,double gap){return a.minPoint().x<b.maxPoint().x+gap&&a.maxPoint().x>b.minPoint().x-gap&&a.minPoint().y<b.maxPoint().y+gap&&a.maxPoint().y>b.minPoint().y-gap;}

bool productGenericSegmentHitsBox(const AcGePoint3d& start, const AcGePoint3d& end, const AcDbExtents& box, double gap) {
 double first = 0, last = 1;
 const auto clip = [&](double origin, double direction, double low, double high) {
  low -= gap;
  high += gap;
  if (std::abs(direction) < 1e-9) return origin >= low && origin <= high;
  const double a = (low - origin) / direction, b = (high - origin) / direction;
  first = std::max(first, std::min(a, b));
  last = std::min(last, std::max(a, b));
  return first <= last;
 };
 return clip(start.x, end.x - start.x, box.minPoint().x, box.maxPoint().x) &&
        clip(start.y, end.y - start.y, box.minPoint().y, box.maxPoint().y);
}

std::optional<AcDbExtents> productGenericDimensionTextBox(AcDbDatabase& db, AcDbAlignedDimension& dimension,
                                                        const AcGePoint3d& position, bool vertical) {
 std::vector<AcDbExtents> displayed;
 productGenericTextLeaves(dimension, displayed);
 if (!displayed.empty()) {
  auto bounds = displayed.front();
  for (const auto& box : displayed) bounds.addExt(box);
  return bounds;
 }
 // A transient dimension may not expose its display block. Lay out its own
 // formatted measurement with the same font and size in a transient MText.
 double measured{};
 AcString text, overrideText;
 if (dimension.measurement(measured) != Acad::eOk || dimension.dimensionText(overrideText) != Acad::eOk ||
     dimension.formatMeasurement(text, measured, overrideText.isEmpty() ? nullptr : overrideText.kACharPtr()) != Acad::eOk ||
     text.isEmpty()) return {};
 AcDbMText sample;
 sample.setDatabaseDefaults(&db);
 const double rotation = dimension.textRotation();
 const double height = dimension.dimtxt() * dimension.dimscale();
 if (height <= 0 || sample.setTextStyle(dimension.dimtxsty()) != Acad::eOk ||
     sample.setTextHeight(height) != Acad::eOk || sample.setAttachment(AcDbMText::kMiddleCenter) != Acad::eOk ||
     sample.setRotation(std::abs(rotation) > 1e-8 ? rotation : (vertical ? std::acos(0.) : 0.)) != Acad::eOk ||
     sample.setLocation(position) != Acad::eOk) return {};
 sample.setContents(text.kACharPtr());
 AcDbExtents bounds;
 if (sample.getGeomExtents(bounds) != Acad::eOk) return {};
 // The transient MText honors the compact text style xScale. The native
 // Tianzheng dimension plots the full-width SHX number. Use its wider envelope
 // for collision checks while leaving the actual dimension style untouched.
 const auto style=open<AcDbTextStyleTableRecord>(dimension.dimtxsty());
 const auto styleName=symbolName(dimension.dimtxsty());
 if((styleName==L"_TCH_DIM"||styleName.rfind(L"_TCH_DIM_",0)==0)&&style->xScale()>0.&&style->xScale()<1.){
  const double horizontal=std::abs(std::cos(sample.rotation()));
  const double verticalPart=std::abs(std::sin(sample.rotation()));
  if(horizontal>1.-1e-7||verticalPart>1.-1e-7){
   const auto span=bounds.maxPoint()-bounds.minPoint();
   const auto tangent=horizontal>verticalPart?AcGeVector3d::kXAxis:AcGeVector3d::kYAxis;
   const auto extra=tangent*(span.dotProduct(tangent)*(1./style->xScale()-1.)*.5);
   bounds=AcDbExtents(bounds.minPoint()-extra,bounds.maxPoint()+extra);
  }
 }
 return bounds;
}

using ProductPerimeterGrips=std::map<std::string,std::vector<AcGePoint3d>>;

void productGenericPerimeterTextClearance(ProductJob& job, AcDbDatabase& db, const ProductPlanLayoutConfig& layout,
                                         bool readback=false,ProductPerimeterGrips* original=nullptr) {
 struct Label {
  AcDbObjectId chain;
  int index, panel, side, row, movement;
  double low, high, value, baseline, minNormal, maxNormal;
  AcGePoint3d position;
  AcDbExtents box;
  bool moved{};
 };
 struct GraphicObstacle {
  std::string handle;
  AcDbExtents bounds;
  GenericOpeningInk ink;
  std::vector<AcDbExtents> text;
 };
 std::vector<Label> labels;
 std::vector<AcDbExtents> obstacles;
 std::vector<GraphicObstacle> graphicObstacles;
 std::map<std::string,std::set<std::string>> baselineObstacles;
 std::map<std::string,std::pair<AcGePoint3d,AcGePoint3d>> baselines;
 std::map<std::pair<int,int>,std::vector<double>> secondRowStations;
 std::map<AcDbObjectId, std::vector<AcGePoint3d>> definingGrips;
 std::map<AcDbObjectId, std::vector<double>> measurements;
 std::map<std::string,ProductPlanRole> unread;
 const auto station = [](const AcGePoint3d& point, bool vertical) { return vertical ? point.y : point.x; };
 const auto offsets = bcpproduct::planAnnotationLayout(layout.scale, layout.unitMM, layout.paper);
 for (const auto& role : layout.dimensions) {
  const auto id = find(db, role.handle);
  const auto points = chainGrips(id);
  if(original){
   const auto n=chainValues(id).size();
   if(n&&points.size()==2*n+6){
    const std::vector<AcGePoint3d> definition(points.begin(),points.begin()+n+3);
    if(!readback)original->emplace(role.handle,definition);
    else {
     const auto prior=original->find(role.handle);
     require(prior!=original->end()&&prior->second.size()==definition.size()&&
       std::equal(definition.begin(),definition.end(),prior->second.begin(),[](const auto& a,const auto& b){
         return a.distanceTo(b)<1e-5;
       }),"reopened perimeter text placement changed defining geometry");
    }
   }
  }
  const bool vertical = role.side < 2;
  const double outward = role.side == 0 || role.side == 2 ? -1. : 1.;
  const double datum = productPlacementDatum(layout, role.panel, role.side);
  const double inner = datum + outward * (role.row == 1 ? offsets.second : role.row == 2 ? offsets.third : 0.);
  const double outer = datum + outward * (role.row == 1 ? offsets.circleNear : role.row == 2 ? offsets.total : offsets.second);
  auto entity = open<AcDbEntity>(id);
  TemplateParts parts(entity.get());
  if (entity->explode(parts.values) != Acad::eOk || parts.values.isEmpty() ||
      points.size() != static_cast<std::size_t>(2 * parts.values.length() + 6)) {
   unread.emplace(role.handle,role);
   continue;
  }
  const int segments = parts.values.length();
  baselines.emplace(role.handle,std::make_pair(points[0],points[1]));
  if(role.row==2){
   auto& stations=secondRowStations[{role.panel+1,role.side}];
   for(int i=0;i<=segments;++i)stations.push_back(station(points[i+2],vertical));
  }
  definingGrips.emplace(id, std::vector<AcGePoint3d>(points.begin(), points.begin() + segments + 3));
  auto& chainMeasurements = measurements[id];
  chainMeasurements.resize(segments, std::numeric_limits<double>::quiet_NaN());
  const auto addLine = [&](const AcGePoint3d& a, const AcGePoint3d& b) {
   AcDbExtents line;
   line.addPoint(a);
   line.addPoint(b);
   obstacles.push_back(line);
  };
  addLine(points[0], points[1]);
  for (int i = 0; i < segments; ++i) {
   auto* dimension = AcDbAlignedDimension::cast(static_cast<AcRxObject*>(parts.values[i]));
   double value{};
   if (!dimension || dimension->measurement(value) != Acad::eOk) { unread.emplace(role.handle,role); continue; }
   chainMeasurements[i] = value;
   const auto index = segments + 3 + i;
   const auto box = productGenericDimensionTextBox(db, *dimension, points[index], vertical);
   if (box) {
    const double first = station(points[i + 2], vertical), last = station(points[i + 3], vertical);
    const auto baseline = dimension->dimLinePoint();
    labels.push_back({id, index, role.panel+1, role.side, role.row, dimension->dimtmove(), std::min(first, last),
                      std::max(first, last), value, vertical ? baseline.x : baseline.y,
                      std::min(inner, outer), std::max(inner, outer), points[index], *box});
   } else unread.emplace(role.handle,role);
   const auto direction = vertical ? AcGeVector3d(outward, 0, 0) : AcGeVector3d(0, outward, 0);
   for (const auto& point : {dimension->xLine1Point(), dimension->xLine2Point()}) {
    auto end = point;
    if (vertical) end.x = points[0].x; else end.y = points[0].y;
    addLine(point + direction * (dimension->dimexo() * dimension->dimscale()),
            end + direction * (dimension->dimexe() * dimension->dimscale()));
   }
  }
 }
 for (const auto& item : modelTypes(db)) {
  const auto& type = item.second;
  const bool graphic = type == L"TDbWall" || type == L"TDbColumn" || type == L"TDbOpening" || type == L"TDbGroupOpening";
  const bool annotation = type == L"TDbAxisLabelSet" || type == L"TDbText" || type == L"TDbMText" ||
                          type == L"TDbSpace" || type == L"TDbSymbElevation" || type == L"TDbDrawingName" ||
                          type == L"AcDbText" || type == L"AcDbMText";
  auto entity = open<AcDbEntity>(find(db, ascii(item.first)));
  if (entity->visibility() != AcDb::kVisible) continue;
  const auto layer = open<AcDbLayerTableRecord>(entity->layerId());
  if (layer->isOff() || layer->isFrozen()) continue;
  if(type==L"TDbDimension2"||AcDbDimension::cast(entity.get())){
   if(!definingGrips.count(entity->objectId())){
    if(type!=L"TDbDimension2")productGenericTextLeaves(*entity,obstacles);
    else{
     TemplateParts parts(entity.get());
     if(entity->explode(parts.values)==Acad::eOk){
      const auto points=chainGrips(entity->objectId());
      const int count=parts.values.length();
      for(int i=0;i<count;++i){
       auto* segment=AcDbAlignedDimension::cast(static_cast<AcRxObject*>(parts.values[i]));
       if(!segment)continue;
       const auto delta=segment->xLine2Point()-segment->xLine1Point();
       const auto text=points.size()==static_cast<std::size_t>(2*count+6)?points[count+3+i]:segment->textPosition();
       if(const auto box=productGenericDimensionTextBox(db,*segment,text,std::abs(delta.y)>std::abs(delta.x)))
        obstacles.push_back(*box);
      }
     }
    }
   }
   continue;
  }
  const auto layerName=symbolName(entity->layerId());
  const bool axis = type == L"AcDbLine" && layerName == L"DOTE";
  const bool stairGraphic=layerName==L"STAIR"||layerName==L"A-STAIR";
  const bool detailedGraphic=type==L"AcDbBlockReference"||stairGraphic;
  if(detailedGraphic){
   GraphicObstacle obstacle;
   obstacle.handle=ascii(item.first);
   if(entity->getGeomExtents(obstacle.bounds)==Acad::eOk){
    const double trialReach=18*layout.scale/layout.unitMM;
    const bool nearLabel=std::any_of(labels.begin(),labels.end(),[&](const auto& label){
     return productGenericBoxesOverlap(label.box,obstacle.bounds,trialReach);
    });
    const bool nearBaseline=stairGraphic&&std::any_of(baselines.begin(),baselines.end(),[&](const auto& row){
     return productGenericSegmentHitsBox(row.second.first,row.second.second,obstacle.bounds,0);
    });
    if(nearLabel||nearBaseline){
     productGenericOpeningInk(*entity,obstacle.ink);
     productGenericTextLeaves(*entity,obstacle.text);
     if(nearBaseline)for(const auto& [handle,baseline]:baselines){
      const auto& [a,b]=baseline;
      if(!productGenericSegmentHitsBox(a,b,obstacle.bounds,0))continue;
      const auto crosses=[&](const auto& edge){
       if(edge.first.distanceTo(edge.second)<1e-5)return false;
       AcGePoint3d point;
       return AcGeLineSeg3d(a,b).intersectWith(AcGeLineSeg3d(edge.first,edge.second),point);
      };
      if(std::any_of(obstacle.ink.straight.begin(),obstacle.ink.straight.end(),crosses))
       baselineObstacles[handle].insert(obstacle.handle);
     }
     graphicObstacles.push_back(std::move(obstacle));
    }
   }
   continue;
  }
  if (!graphic && !annotation && !axis) continue;
  if (type == L"TDbAxisLabelSet") { productGenericTextLeaves(*entity, obstacles); continue; }
  AcDbExtents bounds;
  if (entity->getGeomExtents(bounds) == Acad::eOk) {
   obstacles.push_back(bounds);
  }
 }
 const double modelPerPaperMm = layout.scale / layout.unitMM;
 const double gap = 0.4 * modelPerPaperMm;
 const auto hitsGraphic = [&](const AcDbExtents& candidate,const GraphicObstacle& obstacle) {
  if(!productGenericBoxesOverlap(candidate,obstacle.bounds,gap))return false;
  const auto overlaps=[&](const auto& box){return productGenericBoxesOverlap(candidate,box,gap);};
  return std::any_of(obstacle.ink.straight.begin(),obstacle.ink.straight.end(),[&](const auto& line){
    return productGenericSegmentHitsBox(line.first,line.second,candidate,gap);
   }) || std::any_of(obstacle.ink.arcs.begin(),obstacle.ink.arcs.end(),overlaps) ||
    std::any_of(obstacle.text.begin(),obstacle.text.end(),overlaps);
 };
 const auto nearSegment = [&](const Label& label,const AcDbExtents& candidate) {
  const bool vertical=label.side<2;
  const double low=station(candidate.minPoint(),vertical),high=station(candidate.maxPoint(),vertical);
  // A glyph can clear the adjacent witness by the same paper gap used
  // to separate symbols and text, while still reading as that segment.
  const double associationGap=gap+0.02*modelPerPaperMm;
  return high>=label.low-associationGap&&low<=label.high+associationGap;
 };
 const auto clear = [&](const Label& current, const AcDbExtents& candidate) {
  return nearSegment(current,candidate)&&std::none_of(labels.begin(), labels.end(), [&](const auto& other) {
          return &other != &current && productGenericBoxesOverlap(candidate, other.box, gap);
         }) && std::none_of(obstacles.begin(), obstacles.end(), [&](const auto& obstacle) {
          return productGenericBoxesOverlap(candidate, obstacle, gap);
         }) && std::none_of(graphicObstacles.begin(),graphicObstacles.end(),[&](const auto& obstacle){
          return hitsGraphic(candidate,obstacle);
         });
 };
 // Give short, crowded segments priority while keeping their neighboring
 // labels as obstacles. Move a label along its own span before staggering it.
 std::vector<std::size_t> order(labels.size());
 std::iota(order.begin(), order.end(), 0);
 std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return labels[a].value < labels[b].value; });
 std::set<AcDbObjectId> movedChains;
 for (const auto i : order) {
  if(readback) break;
  auto& label = labels[i];
  const bool vertical = label.side < 2;
  const double sign = vertical ? -1. : 1.;
  const auto along = vertical ? AcGeVector3d(0, 1, 0) : AcGeVector3d(1, 0, 0);
  const auto outward = vertical ? AcGeVector3d(sign, 0, 0) : AcGeVector3d(0, sign, 0);
  const auto verifyGrips = [&] {
   auto actual = chainGrips(label.chain);
   const auto& expected = definingGrips.at(label.chain);
   require(actual.size() > static_cast<std::size_t>(label.index) &&
           std::equal(expected.begin(), expected.end(), actual.begin(), [](const auto& a, const auto& b) {
            return a.distanceTo(b) < 1e-5;
           }), "perimeter text edit changed baseline or station");
   return actual[label.index];
  };
  const auto readMovedText = [&](const AcGePoint3d& position) -> std::optional<AcDbExtents> {
   auto entity = open<AcDbEntity>(label.chain);
   TemplateParts parts(entity.get());
   const int segments = static_cast<int>(measurements.at(label.chain).size());
   if (entity->explode(parts.values) != Acad::eOk || parts.values.length() != segments) return {};
   auto* dimension = AcDbAlignedDimension::cast(static_cast<AcRxObject*>(parts.values[label.index - segments - 3]));
   if (!dimension) return {};
   const auto baseline = dimension->dimLinePoint();
   if (std::abs((vertical ? baseline.x : baseline.y) - label.baseline) > 1e-5) return {};
   return productGenericDimensionTextBox(db, *dimension, position, vertical);
  };
  const auto tryMove = [&](const AcGeVector3d& offset, bool withinSpan,
                           std::optional<bool> shortSide=std::nullopt) {
   const auto fits = [&](const AcDbExtents& box) {
    if (station(box.minPoint(), !vertical) < label.minNormal + gap ||
        station(box.maxPoint(), !vertical) > label.maxNormal - gap) return false;
    if(shortSide){
     const double center=(station(box.minPoint(),vertical)+station(box.maxPoint(),vertical))*0.5;
     const double middle=(label.low+label.high)*0.5;
     if(*shortSide?center<middle:center>middle)return false;
    }
    return productPlanTextOnRequiredSide(box, vertical, label.baseline) && (!withinSpan || (station(box.minPoint(), vertical) >= label.low + gap &&
                            station(box.maxPoint(), vertical) <= label.high - gap)) && clear(label, box);
   };
   const AcDbExtents candidate(label.box.minPoint() + offset, label.box.maxPoint() + offset);
   if (!fits(candidate)) return false;
   const auto target = label.position + offset;
   if (label.movement != 2) return false;
   moveDimensionGrip(label.chain, label.index, target);
   const auto actual = verifyGrips();
   const auto box = readMovedText(actual);
   if (actual.distanceTo(target) > 1e-5 || !box || !fits(*box)) {
    moveDimensionGrip(label.chain, label.index, label.position);
    require(verifyGrips().distanceTo(label.position) < 1e-5, "restore native perimeter text grip");
    return false;
   }
   label.position = target;
   label.box = *box;
   label.moved = true;
   movedChains.insert(label.chain);
   return true;
  };
  // Neighboring short values prefer opposite ends of their shared witness.
  // Keep ordinary values and already centered readable short values in place.
  const auto shortLabel=[&](const Label& entry){
   return entry.high-entry.low<station(entry.box.maxPoint(),vertical)-
      station(entry.box.minPoint(),vertical)+2*gap;
  };
  const auto sharesCrowdedShortEnd=[&](const Label& other,bool previous){
   if(&other==&label||other.chain!=label.chain||!shortLabel(other))return false;
   const double shared=previous?label.low:label.high;
   const double otherEnd=previous?other.high:other.low;
   if(label.row!=3||std::abs(shared-otherEnd)>=1e-5)return false;
   const auto stations=secondRowStations.find({label.panel,label.side});
   const double first=previous?other.low:label.low;
   const double last=previous?label.high:other.high;
   return stations!=secondRowStations.end()&&std::any_of(stations->second.begin(),stations->second.end(),
    [&](double value){return value>=first-1e-5&&value<=last+1e-5;});
  };
  const bool previousShort=shortLabel(label)&&std::any_of(labels.begin(),labels.end(),
     [&](const auto& other){return sharesCrowdedShortEnd(other,true);});
  const bool nextShort=shortLabel(label)&&std::any_of(labels.begin(),labels.end(),
     [&](const auto& other){return sharesCrowdedShortEnd(other,false);});
  const auto preferred=previousShort!=nextShort?std::optional<bool>{previousShort}:std::nullopt;
  const double center=(station(label.box.minPoint(),vertical)+station(label.box.maxPoint(),vertical))*0.5;
  const bool preferredSide=!preferred||(*preferred?center>=(label.low+label.high)*0.5:center<=(label.low+label.high)*0.5);
  if(productPlanTextOnRequiredSide(label.box,vertical,label.baseline)&&clear(label,label.box)&&preferredSide)continue;
  bool placed = false;
  const double slack = label.high - label.low - station(label.box.maxPoint(), vertical) + station(label.box.minPoint(), vertical) - 2 * gap;
  if (slack > 0) {
   const double toStart = label.low + gap - station(label.box.minPoint(), vertical);
   std::array<double, 3> shifts{toStart, toStart + slack, toStart + slack / 2};
   std::sort(shifts.begin(), shifts.end(), [](double a, double b) { return std::abs(a) < std::abs(b); });
   for (double shift : shifts)
    if (std::abs(shift) > 1e-5 && tryMove(along * shift, true)) { placed = true; break; }
  }
  if(placed)continue;
  // Prefer a stagger within the measured span, on the required side.
  const auto searchStagger=[&](std::optional<bool> preferredShortSide) {
   // Test actual free strips near each obstacle edge as well as small regular offsets.
   std::vector<double> normal{0.},tangent{0.};
   for(int step=1;step<=24;++step){
    normal.push_back(step*0.75*modelPerPaperMm);
    normal.push_back(-step*0.75*modelPerPaperMm);
    tangent.push_back(step*0.75*modelPerPaperMm);
    tangent.push_back(-step*0.75*modelPerPaperMm);
   }
   const auto edgeOffsets=[&](const AcDbExtents& box){
    if(!productGenericBoxesOverlap(label.box,box,18.*modelPerPaperMm))return;
    const auto edge=[&](bool direction,std::vector<double>& options,double directionSign){
     options.push_back(directionSign*(station(box.minPoint(),direction)-
         station(label.box.maxPoint(),direction)-gap-0.01*modelPerPaperMm));
     options.push_back(directionSign*(station(box.maxPoint(),direction)-
         station(label.box.minPoint(),direction)+gap+0.01*modelPerPaperMm));
    };
    edge(vertical,tangent,1.);
    edge(!vertical,normal,sign);
   };
   for(const auto& other:labels)if(&other!=&label&&other.panel==label.panel)edgeOffsets(other.box);
   for(const auto& box:obstacles)edgeOffsets(box);
   const auto nearFirst=[&](std::vector<double>& options){
    std::sort(options.begin(),options.end());
    options.erase(std::unique(options.begin(),options.end(),[](double a,double b){return std::abs(a-b)<1e-5;}),options.end());
    std::stable_sort(options.begin(),options.end(),[](double a,double b){return std::abs(a)<std::abs(b);});
   };
   nearFirst(normal); nearFirst(tangent);
   for(bool allowOutside:{false,true}){
    for(const double shift:tangent){
     const double target=station(label.position,vertical)+shift;
     if((target<label.low-1e-5||target>label.high+1e-5)!=allowOutside)continue;
     for(const double offset:normal){
      if(std::abs(offset)+std::abs(shift)<1e-5)continue;
      if(tryMove(outward*offset+along*shift,false,preferredShortSide))return true;
     }
    }
   }
   return false;
  };
  if(preferred&&searchStagger(preferred))continue;
  if(productPlanTextOnRequiredSide(label.box,vertical,label.baseline)&&clear(label,label.box))continue;
  searchStagger(std::nullopt);
 }
 for (const auto id : movedChains) {
  const auto actual = chainValues(id);
  const auto& expected = measurements.at(id);
  require(actual.size() == expected.size() && std::equal(expected.begin(), expected.end(), actual.begin(), [](double a, double b) {
   return !std::isfinite(a) || std::abs(a - b) < 1e-5;
  }), "perimeter text edit changed measured dimension values");
 }
 std::set<std::string> unresolved;
 std::ostringstream report;
 report << std::setprecision(17) << "{\"labels\":[";
 bool first = true;
 for (const auto& label : labels) {
  const auto handle = axisHandle(label.chain);
  const bool conflict = label.movement != 2 || !productPlanTextOnRequiredSide(label.box, label.side < 2, label.baseline) || !clear(label, label.box);
  std::ostringstream obstacleHandles;
  obstacleHandles<<'[';
  bool firstObstacle=true;
  for(const auto& obstacle:graphicObstacles)if(hitsGraphic(label.box,obstacle)){
   if(!firstObstacle)obstacleHandles<<',';
   firstObstacle=false;
   obstacleHandles<<'"'<<obstacle.handle<<'"';
  }
  obstacleHandles<<']';
  if (conflict) unresolved.insert(handle);
  if (!first) report << ',';
  first = false;
  report << "{\"handle\":\"" << handle << "\",\"panel\":" << label.panel << ",\"side\":" << label.side << ",\"row\":" << label.row << ",\"grip\":" << label.index
         << ",\"value\":" << label.value << ",\"position\":" << wholePoints({label.position})
         << ",\"textBox\":" << wholePoints({label.box.minPoint(), label.box.maxPoint()})
         << ",\"nativeMovementMode\":" << label.movement << ",\"moved\":" << (label.moved ? "true" : "false")
         << ",\"leader\":null"
         << ",\"nearMeasuredSegment\":" << (nearSegment(label,label.box)?"true":"false")
         << ",\"graphicObstacles\":" << obstacleHandles.str()
         << ",\"conflict\":" << (conflict ? "true" : "false") << '}';
  if(readback){
   const auto count=measurements.at(label.chain).size();
   const auto& points=definingGrips.at(label.chain);
   const auto segment=static_cast<std::size_t>(label.index)-count-3;
   std::ostringstream record;
   record<<std::setprecision(17)<<"{\"panel\":"<<label.panel<<",\"side\":"<<label.side<<",\"row\":"<<label.row
         <<",\"object\":\""<<handle<<"\",\"segment\":"<<segment+1<<",\"value\":"<<label.value
         <<",\"stations\":"<<wholePoints({points[segment+2],points[segment+3]})
         <<",\"baseline\":"<<label.baseline<<",\"textBox\":"<<wholePoints({label.box.minPoint(),label.box.maxPoint()})
         <<",\"sameSide\":"<<(productPlanTextOnRequiredSide(label.box,label.side<2,label.baseline)?"true":"false")
         <<",\"noLeader\":"<<(label.movement==2?"true":"false")
         <<",\"nearMeasuredSegment\":"<<(nearSegment(label,label.box)?"true":"false")
         <<",\"graphicObstacles\":"<<obstacleHandles.str()
         <<",\"savedReopened\":true,\"scope\":\"perimeter-segment\",\"report\":\"外围尺寸文字重开核验.json\",\"result\":\""
         <<(conflict?"pending":"verified-perimeter-text")<<"\"}";
   productRecordRuleEvidence(job,"D17",record.str());
   productRecordRuleEvidence(job,"D18",record.str());
  }
 }
 report << "],\"unreadableChains\":" << unread.size() << ",\"unresolvedChains\":" << unresolved.size()
        << ",\"baselineObstacles\":[";
 bool firstBaseline=true;
 for(const auto& role:layout.dimensions){
  const auto found=baselineObstacles.find(role.handle);
  if(found==baselineObstacles.end())continue;
  std::ostringstream evidence;
  evidence<<"{\"object\":\""<<role.handle<<"\",\"panel\":"<<role.panel+1
          <<",\"side\":"<<role.side<<",\"row\":"<<role.row<<",\"scope\":\"perimeter-baseline\",\"objects\":[";
  bool firstObject=true;
  for(const auto& handle:found->second){
   if(!firstObject)evidence<<',';
   firstObject=false;
   evidence<<'"'<<handle<<'"';
  }
  evidence<<"],\"savedReopened\":"<<(readback?"true":"false")
          <<",\"report\":\"外围尺寸文字重开核验.json\",\"result\":\"pending\"}";
  if(!firstBaseline)report<<',';
  firstBaseline=false;
  report<<evidence.str();
  if(readback)productRecordRuleEvidence(job,"D18",evidence.str());
 }
 report<<"]}";
 writeNew(job.root / (readback?L"外围尺寸文字重开核验.json":L"外围尺寸文字核验.json"), report.str());
 if(job.unifiedPlan&&!readback)return;
 for (const auto& [handle,role] : unread) {
  job.detailPending.push_back({u8"外围尺寸避让", handle, u8"部分尺寸数字的文字范围不可读，保留原生尺寸链供复核"});
  if(readback){
   const auto evidence="{\"panel\":"+std::to_string(role.panel+1)+",\"side\":"+std::to_string(role.side)+
      ",\"row\":"+std::to_string(role.row)+",\"object\":\""+handle+
      "\",\"savedReopened\":true,\"result\":\"unreadable\"}";
   productRecordRuleEvidence(job,"D17",evidence);
   productRecordRuleEvidence(job,"D18",evidence);
  }
 }
 for (const auto& handle : unresolved)
  job.detailPending.push_back({u8"外围尺寸避让", handle, u8"仍有文字冲突，保留真实测站和尺寸链供人工调整"});
 for(const auto& [handle,objects]:baselineObstacles){
  std::string joined;
  for(const auto& object:objects){
   if(!joined.empty())joined+="/";
   joined+=object;
  }
  job.detailPending.push_back({u8"外围尺寸避让",handle,u8"基线与原台阶或平台实体 "+joined+
      u8" 相交，需局部协调构造表达，保留按规则定位的整链基线"});
 }
}

std::vector<AcDbExtents> productPlanChainGlyphs(AcDbDatabase& db,const AcDbEntity& chain){
 std::vector<AcDbExtents> glyphs;
 TemplateParts parts(&chain);
 if(chain.explode(parts.values)!=Acad::eOk)return glyphs;
 const auto grips=chainGrips(chain.objectId());
 const auto count=parts.values.length();
 for(int i=0;i<count;++i){
  auto* segment=AcDbAlignedDimension::cast(static_cast<AcRxObject*>(parts.values[i]));
  if(!segment)continue;
  const auto direction=segment->xLine2Point()-segment->xLine1Point();
  const bool vertical=std::abs(direction.y)>std::abs(direction.x);
  const auto position=grips.size()==static_cast<std::size_t>(2*count+6)?grips[count+3+i]:segment->textPosition();
  if(const auto glyph=productGenericDimensionTextBox(db,*segment,position,vertical))glyphs.push_back(*glyph);
 }
 return glyphs;
}

using GenericDimensionBoxes=std::vector<std::optional<AcDbExtents>>;
GenericDimensionBoxes productGenericChainText(AcDbDatabase& db,AcDbObjectId id,bool vertical,
                                            std::vector<int>* movement=nullptr) {
 auto e=open<AcDbEntity>(id);
 TemplateParts parts(e.get());
 if (e->explode(parts.values)!=Acad::eOk) return {};
 const auto grips=chainGrips(id);
 const auto n=parts.values.length();
 if (n<1 || (grips.size()!=static_cast<std::size_t>(2*n+6)&&grips.size()!=static_cast<std::size_t>(2*n+3))) return {};
 GenericDimensionBoxes result;
 for (int i=0;i<n;++i) {
  auto* dim=AcDbAlignedDimension::cast(static_cast<AcRxObject*>(parts.values[i]));
  result.push_back(dim?productGenericDimensionTextBox(db,*dim,grips[n+3+i],vertical):std::nullopt);
  if (movement) movement->push_back(dim?dim->dimtmove():-1);
 }
 return result;
}
void productSeedPlanDimensionText(ProductJob& job,AcDbDatabase& db,double modelPerPaperMm) {
 for (const auto& [handle,type]:modelTypes(db)) {
  if (type!=L"TDbDimension2" || job.planDimensionExclusions.count(ascii(handle))) continue;
  const auto id=find(db,ascii(handle));
  if (!productPlanDimensionVisible(*open<AcDbEntity>(id))) continue;
  const auto original=chainGrips(id);
  const auto measures=chainValues(id);
  const auto n=measures.size();
  if (!n || original.size()!=2*n+6) continue;
  const bool vertical=std::abs(original[2].x-original[n+2].x)<1e-5;
  if (!vertical && std::abs(original[2].y-original[n+2].y)>=1e-5) continue;
  std::vector<int> modes;
  const auto boxes=productGenericChainText(db,id,vertical,&modes);
  for (std::size_t i=0;i<boxes.size();++i) {
   const double baseline=vertical?original[0].x:original[0].y;
   if (!boxes[i] || modes[i]!=2 || productPlanTextOnRequiredSide(*boxes[i],vertical,baseline)) continue;
   const double gap=0.6*modelPerPaperMm;
   auto point=original[n+3+i];
   if (vertical) point.x+=baseline-gap-boxes[i]->maxPoint().x;
   else point.y+=baseline+gap-boxes[i]->minPoint().y;
   moveDimensionGrip(id,static_cast<int>(n+3+i),point);
  }
  const auto after=chainGrips(id);
  require(after.size()==original.size() &&
          std::equal(original.begin(),original.begin()+n+3,after.begin(),
                     [](const auto& a,const auto& b){return a.distanceTo(b)<1e-5;}),
          "plan text placement changed baseline or measuring points");
  const auto actual=chainValues(id);
  require(actual.size()==n && std::equal(actual.begin(),actual.end(),measures.begin(),
          [](double a,double b){return std::abs(a-b)<1e-5;}),"plan text placement changed measurements");
 }
}

void productAuditPlanDimensionText(ProductJob& job,AcDbDatabase& db,
                                   const std::vector<bcpproduct::GenericView>* views=nullptr) {
 Working context(&db);
 int verified=0, pending=0, verifiedStock=0, pendingStock=0;
 for (const auto& [handle,type]:modelTypes(db)) {
  if (job.planDimensionExclusions.count(ascii(handle))) continue;
  const auto id=find(db,ascii(handle));
  auto entity=open<AcDbEntity>(id);
  const auto* stock=AcDbDimension::cast(entity.get());
  if ((type!=L"TDbDimension2" && !stock) || !productPlanDimensionVisible(*entity)) continue;
  const auto record=[&](bool valid,const std::optional<AcGePoint3d>& at){
   if(!views)return;
   const auto panel=at?bcpproduct::nearestGenericView(*views,{at->x,at->y})+1:0;
   productRecordRuleEvidence(job,"D17","{\"panel\":"+std::to_string(panel)+",\"object\":\""+ascii(handle)+
       "\",\"scope\":\"chain-text-side-and-no-leader\",\"savedReopened\":true,\"report\":\"尺寸同侧无引线核验.json\",\"result\":\""+
       (valid?"verified-text-side-and-mode":"pending")+"\"}");
  };
  if (stock) {
   std::optional<bool> vertical;
   double baseline{};
   if (const auto* aligned=AcDbAlignedDimension::cast(stock)) {
    const auto direction=aligned->xLine2Point()-aligned->xLine1Point();
    if (std::abs(direction.x)<1e-5) vertical=true;
    else if (std::abs(direction.y)<1e-5) vertical=false;
    if (vertical) baseline=*vertical?aligned->dimLinePoint().x:aligned->dimLinePoint().y;
   } else if (const auto* rotated=AcDbRotatedDimension::cast(stock)) {
    const auto angle=rotated->rotation();
    if (std::abs(std::cos(angle))<1e-5) vertical=true;
    else if (std::abs(std::sin(angle))<1e-5) vertical=false;
    if (vertical) baseline=*vertical?rotated->dimLinePoint().x:rotated->dimLinePoint().y;
   }
   std::vector<AcDbExtents> text;
   if (vertical && stock->dimtmove()==2) productGenericTextLeaves(*stock,text);
   const bool valid=!text.empty() && std::all_of(text.begin(),text.end(),[&](const auto& box) {
        return productPlanTextOnRequiredSide(box,*vertical,baseline);
       });
   record(valid,stock->textPosition());
   if(valid) ++verifiedStock;
   else {
    ++pendingStock;
    job.detailPending.push_back({u8"普通CAD尺寸排版",ascii(handle),u8"已保留尺寸，保存重开后文字侧向或移动模式待核"});
   }
   continue;
  }
  entity.reset();
  const auto grips=chainGrips(id);
  const auto at=grips.size()>2?std::optional<AcGePoint3d>{grips[2]}:std::nullopt;
  const auto n=chainValues(id).size();
  bool valid=n && grips.size()==2*n+6;
  if (valid) {
   const bool vertical=std::abs(grips[2].x-grips[n+2].x)<1e-5;
   const double baseline=vertical?grips[0].x:grips[0].y;
   std::vector<int> modes;
   const auto boxes=productGenericChainText(db,id,vertical,&modes);
   valid=(vertical || std::abs(grips[2].y-grips[n+2].y)<1e-5) && boxes.size()==n;
   for (std::size_t i=0;valid && i<n;++i)
    valid=boxes[i] && modes[i]==2 && productPlanTextOnRequiredSide(*boxes[i],vertical,baseline);
  }
  record(valid,at);
  if (valid) ++verified;
  else {
   ++pending;
   job.detailPending.push_back({u8"尺寸同侧无引线",ascii(handle),u8"保存重开后尺寸字形侧向或原生移动模式待核"});
  }
 }
 writeNew(job.root/L"尺寸同侧无引线核验.json",
          "{\"verifiedChains\":"+std::to_string(verified)+",\"pendingChains\":"+std::to_string(pending)+
          ",\"verifiedStock\":"+std::to_string(verifiedStock)+",\"pendingStock\":"+std::to_string(pendingStock)+"}");
}

void productGenericInteriorClearance(ProductJob& job,AcDbDatabase& db,AcDbObjectId id,
                                     bool vertical,double modelPerPaperMm,
                                     const std::vector<std::pair<AcGePoint3d,AcGePoint3d>>* actualInk=nullptr,
                                     bool auditOnly=false,bool keepStationSide=false,bool keepBaseline=false,
                                     const std::vector<AcDbExtents>* annotationObstacles=nullptr) {
 const double gap=0.35*modelPerPaperMm;
 const auto coord=[&](const AcGePoint3d& p){return vertical?p.y:p.x;};
 const auto cross=[&](const AcGePoint3d& p){return vertical?p.x:p.y;};
 const auto tangent=vertical?AcGeVector3d::kYAxis:AcGeVector3d::kXAxis;
 const auto normal=vertical?AcGeVector3d::kXAxis:AcGeVector3d::kYAxis;
 const auto original=chainGrips(id);
 const auto measured=chainValues(id);
 const auto n=measured.size();
 const double station=cross(original[2]);
 const double originalOffset=cross(original[0])-station;
 const auto onStationSide=[&](const AcGePoint3d& point){
  return !keepStationSide||std::abs(originalOffset)<1e-5||(cross(point)-station)*originalOffset>0;
 };
 const auto textOnStationSide=[&](const GenericDimensionBoxes& boxes){
  return !keepStationSide||std::all_of(boxes.begin(),boxes.end(),[&](const auto& box){
   return box&&onStationSide(box->minPoint())&&onStationSide(box->maxPoint());
  });
 };
 std::vector<AcDbExtents> obstacles;
 if(annotationObstacles)obstacles=*annotationObstacles;
  std::vector<std::pair<AcGePoint3d,AcGePoint3d>> fixedStrokes;
  if(actualInk)fixedStrokes=*actualInk;
 bool redundant=false;
 for (const auto& item:modelTypes(db)) {
  const auto& type=item.second;
  const bool dimension=type==L"TDbDimension2";
  const bool opening=type==L"TDbOpening" || type==L"TDbGroupOpening";
  if (!dimension && !opening && type!=L"AcDbLine" && type!=L"AcDbLeader" && type!=L"TDbText" && type!=L"TDbMText" &&
      type!=L"AcDbText" && type!=L"AcDbMText") continue;
  const auto other=find(db,ascii(item.first));
  if (other==id) continue;
  auto e=open<AcDbEntity>(other);
  if (e->visibility()!=AcDb::kVisible) continue;
  const auto layer=open<AcDbLayerTableRecord>(e->layerId());
  if (layer->isOff() || layer->isFrozen()) continue;
  if (const auto* line=AcDbLine::cast(e.get())) {
   if (symbolName(e->layerId())==L"DOTE") fixedStrokes.emplace_back(line->startPoint(),line->endPoint());
   continue;
  }
  if (const auto* leader=AcDbLeader::cast(e.get())) {
   for (int i=1;i<leader->numVertices();++i)
    fixedStrokes.emplace_back(leader->vertexAt(i-1),leader->vertexAt(i));
   continue;
  }
  if (!dimension) {
   productGenericTextLeaves(*e,obstacles);
   continue;
  }
  const auto grips=chainGrips(other);
  if (grips.size()<8) continue;
  const auto count=(grips.size()-6)/2;
  const bool otherVertical=std::abs(grips[0].x-grips[1].x)<1e-5;
  for (const auto& box:productGenericChainText(db,other,otherVertical))
   if (box) obstacles.push_back(*box);
  if (otherVertical!=vertical || redundant) continue;
  const auto first=grips.begin()+2,last=first+count+1;
  redundant=std::all_of(original.begin()+2,original.begin()+n+3,[&](const auto& point){
   return std::any_of(first,last,[&](const auto& otherPoint){
    return std::abs(coord(point)-coord(otherPoint))<1e-4 && std::abs(cross(point)-cross(otherPoint))<1e-4;
   });
  });
 }
 const auto hits=[&](const GenericDimensionBoxes& boxes,std::size_t i){
  if (!boxes[i]) return std::numeric_limits<std::size_t>::max()/4;
  std::size_t count=std::count_if(obstacles.begin(),obstacles.end(),[&](const auto& other){
   return productGenericBoxesOverlap(*boxes[i],other,gap);
  });
  for (const auto& line:fixedStrokes)
   if (productGenericSegmentHitsBox(line.first,line.second,*boxes[i],gap)) ++count;
  for (std::size_t j=0;j<boxes.size();++j)
   if (i!=j && boxes[j] && productGenericBoxesOverlap(*boxes[i],*boxes[j],gap)) ++count;
  return count;
 };
 const auto score=[&](const GenericDimensionBoxes& boxes){
  if (boxes.size()!=n || std::any_of(boxes.begin(),boxes.end(),[](const auto& box){return !box;}))
   return std::numeric_limits<std::size_t>::max();
  std::size_t result=0;
  for (std::size_t i=0;i<n;++i) result+=hits(boxes,i);
  return result;
 };
 if(auditOnly){
  std::vector<int> modes;
  const auto boxes=productGenericChainText(db,id,vertical,&modes);
  for(std::size_t i=0;i<n;++i){
   const auto prefix=u8"第"+std::to_string(i+1)+u8"段";
   if(i>=boxes.size()||!boxes[i]){
    job.detailPending.push_back({u8"室内尺寸字框未验证",axisHandle(id),prefix+u8"字框不可读"});
    continue;
   }
   const auto at=boxes[i]->minPoint()+(boxes[i]->maxPoint()-boxes[i]->minPoint())*0.5;
   const auto count=hits(boxes,i);
   if(count)job.detailPending.push_back({u8"室内尺寸避让",axisHandle(id),prefix+
    u8"重开后字框位置 "+std::to_string(at.x)+","+std::to_string(at.y)+u8"，净空冲突计数 "+std::to_string(count)});
   if(i>=modes.size()||modes[i]!=2)
    job.detailPending.push_back({u8"室内尺寸移动模式待核",axisHandle(id),prefix+u8"原生移动模式无法按同侧无引线编辑"});
  }
  return;
 }
 std::size_t best=std::numeric_limits<std::size_t>::max();
 auto chosen=original[0];
 const std::vector<double> steps=actualInk?
  std::vector<double>{0.,1.,-1.,2.,-2.,3.,-3.,4.,-4.,5.,-5.,6.,-6.,7.5,-7.5,9.,-9.,12.,-12.,15.,-15.}:
  std::vector<double>{0.,3.,-3.,6.,-6.,9.,-9.,12.,-12.,15.,-15.};
 for (double paper:steps) {
  if(keepBaseline&&paper!=0.)break;
  const auto target=original[0]+normal*(paper*modelPerPaperMm);
  if(!onStationSide(target))continue;
  const auto current=chainGrips(id);
  if(std::abs(cross(current[0])-cross(target))>1e-5)moveDimensionGrip(id,0,target);
  const auto actualBaseline=chainGrips(id)[0];
  if(!onStationSide(actualBaseline))continue;
  const auto boxes=productGenericChainText(db,id,vertical);
  if(!textOnStationSide(boxes))continue;
  const auto total=score(boxes);
  if (total<best) {best=total;chosen=actualBaseline;}
  if (total==0) break;
 }
 if(std::abs(cross(chainGrips(id)[0])-cross(chosen))>1e-5)moveDimensionGrip(id,0,chosen);
 const auto base=chainGrips(id);
 const auto sameBaseline=[&](const auto& grips){
  return std::abs(cross(grips[0])-cross(base[0]))<1e-5&&std::abs(cross(grips[1])-cross(base[1]))<1e-5;
 };
 if (best>0 && best<std::numeric_limits<std::size_t>::max()) {
  std::vector<int> movement;
  auto boxes=productGenericChainText(db,id,vertical,&movement);
  best=score(boxes);
  std::vector<std::size_t> order(n);
  std::iota(order.begin(),order.end(),0);
  std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return measured[a]<measured[b];});
  for (std::size_t i:order) {
   if (!boxes[i] || !hits(boxes,i) || movement[i]!=2) continue;
   const auto grips=chainGrips(id);
   const auto index=n+3+i;
   const double low=std::min(coord(grips[i+2]),coord(grips[i+3]));
   const double high=std::max(coord(grips[i+2]),coord(grips[i+3]));
   const double left=low+gap-coord(boxes[i]->minPoint()),right=high-gap-coord(boxes[i]->maxPoint());
   const auto candidate=[&](const AcGeVector3d& shift) {
    moveDimensionGrip(id,static_cast<int>(index),grips[index]+shift);
    auto actual=productGenericChainText(db,id,vertical);
    const auto total=score(actual);
    if (total<best && sameBaseline(chainGrips(id)) && textOnStationSide(actual) && actual[i] &&
        productPlanTextOnRequiredSide(*actual[i],vertical,cross(base[0]))) {
     best=total;
     boxes=std::move(actual);
     return true;
    }
    moveDimensionGrip(id,static_cast<int>(index),grips[index]);
    return false;
   };
   bool placed=false;
   if (left<=right)
    for (double shift:{(left+right)/2,left,right})
     if (std::abs(shift)>1e-5 && candidate(tangent*shift)) {placed=true;break;}
   for (double offset:{2.5,5.,7.5,10.}) {
    if (placed) break;
    for (double shift:{0.,1.25,-1.25,2.5,-2.5,3.75,-3.75,5.,-5.}) {
     if (candidate((normal*(vertical?-offset:offset)+tangent*shift)*modelPerPaperMm)) {placed=true;break;}
    }
   }
  }
 }
 const auto actual=chainValues(id);
 require(actual.size()==n && std::equal(actual.begin(),actual.end(),measured.begin(),
         [](double a,double b){return std::abs(a-b)<1e-5;}),"clearance changed physical dimension measurement");
 const auto after=chainGrips(id);
 require(sameBaseline(after)&&onStationSide(after[0]),"text placement changed interior dimension baseline");
 if(keepStationSide&&!textOnStationSide(productGenericChainText(db,id,vertical)))
  job.detailPending.push_back({u8"新建室内尺寸位置待核",axisHandle(id),u8"已指定室内侧的字框位置仍待人工排位"});
 for (std::size_t i=2;i<n+3;++i)
  require(after[i].distanceTo(original[i])<1e-5,"clearance changed interior dimension station");
  if (best && redundant && !actualInk && !keepBaseline) {
  auto e=editObject<AcDbEntity>(id);
  productStatus(e->erase(),"omit crowded duplicate interior dimension");
  job.detailPending.push_back({u8"室内尺寸省标",axisHandle(id),u8"重复测站已由其他可见尺寸覆盖，省略拥挤局部链"});
  return;
 }
 if (best&&(!actualInk||keepBaseline)) job.detailPending.push_back({u8"室内尺寸避让",axisHandle(id),u8"必要测站仍有图面冲突，保留原生尺寸链供复核"});
}
struct GenericOpeningWork {
 const GenericNativeOpening* opening;
 GenericOpeningHost host;
 GenericOpeningInk ink;
 double low{},high{};
 bool sliding{};
};
bool productGenericOpeningKind(const GenericNativeOpening& opening,const wchar_t* word) {
 return opening.kind.find(word)!=std::wstring::npos || opening.style.find(word)!=std::wstring::npos;
}
bool productConfirmedRaisedWallOpening(const GenericNativeOpening& opening,
 const ProductPlanDetailConfirmations& confirmations,double unitMM) {
 const auto spec=confirmations.unnumberedWallOpenings.find(opening.handle);
 return spec!=confirmations.unnumberedWallOpenings.end()&&opening.kind==L"墙洞"&&
  opening.existing.empty()&&opening.sill>0&&
  std::abs(opening.width*unitMM-spec->second[0])<.01&&
  std::abs(opening.height*unitMM-spec->second[1])<.01&&
  std::abs(opening.sill*unitMM-spec->second[2])<.01;
}
bool productNativeRaisedWallVoid(const GenericNativeOpening& opening,const GenericOpeningInk& ink){
 // A raised plain wall hole is detectable from the native business type and
 // its own plan strokes. Two interior tracks could indicate a glazed unit.
 if(opening.kind!=L"墙洞"||!opening.style.empty()||!opening.existing.empty()||opening.grouped||
    opening.window||opening.sill<=0||opening.width<=0||opening.height<=0||ink.swing||
    !ink.arcs.empty()||!ink.curved.empty())return false;
 const bool vertical=std::abs(opening.a.x-opening.b.x)<1e-5;
 if(!vertical&&std::abs(opening.a.y-opening.b.y)>1e-5)return false;
 const auto along=[&](const auto& p){return vertical?p.y:p.x;};
 const auto [lo,hi]=std::minmax({along(opening.a),along(opening.b)});
 if(hi-lo<1e-5)return false;
 // The jamb marks run across the opening. Long lines along the aperture
 // require separate frame/glazing classification.
 return std::none_of(ink.straight.begin(),ink.straight.end(),[&](const auto& line){
  return std::abs(along(line.first)-along(line.second))>(hi-lo)*.2;
 });
}

std::optional<GenericOpeningHost> productPairedInteriorTrackOpening(const GenericNativeOpening& opening,const GenericOpeningInk& ink,
 const GenericNativeInput& input,const bcpproduct::GenericView& view,double unitMM){
 if(opening.grouped||opening.window||opening.kind!=L"门"||opening.sill*unitMM>.01||ink.swing)return {};
 const double tolerance=1./unitMM;
 const bool vertical=std::abs(opening.a.x-opening.b.x)<tolerance;
 if(!vertical&&std::abs(opening.a.y-opening.b.y)>=tolerance)return {};
 const auto station=[&](const auto& point){return vertical?point.y:point.x;};
 const auto normal=[&](const auto& point){return vertical?point.x:point.y;};
 const auto [low,high]=std::minmax({station(opening.a),station(opening.b)});
 const double datum=(normal(opening.a)+normal(opening.b))/2;
 if(std::abs(high-low-opening.width)>tolerance||high-low<=tolerance)return {};
 std::array<std::vector<std::size_t>,2> jambs;
 for(const auto index:view.walls){
  const auto& wall=input.walls[index];
  if(wall.low||!wall.orthogonal||std::abs(station(wall.wall.start)-station(wall.wall.end))>tolerance)continue;
  const auto body=approvedRect(wall.wall);
  const int cross=vertical?0:1,along=1-cross;
  if(datum<=body[cross]+tolerance||datum>=body[cross+2]-tolerance)continue;
  if(std::abs(low-body[along+2])<=tolerance)jambs[0].push_back(index);
  if(std::abs(high-body[along])<=tolerance)jambs[1].push_back(index);
 }
 if(jambs[0].size()!=1||jambs[1].size()!=1||jambs[0][0]==jambs[1][0])return {};
 const GenericOpeningHost aligned{jambs[0][0],-1,vertical,datum,low,high,low,high,jambs[1][0]};
 if(!productGenericSlidingTracks(ink,aligned,low,high,
    std::max(input.walls[jambs[0][0]].total,input.walls[jambs[1][0]].total)))return {};
 return aligned;
}
struct GenericCaptionGlyph {
 AcDbExtents box;
 double angle;
 AcDbObjectId layer;
 AcCmColor color;
 AcDbObjectId style;
 double height;
};
std::optional<GenericCaptionGlyph> productGenericCaptionGlyph(const AcDbEntity& entity,const std::wstring& label,int depth=0) {
 if (depth>8) return {};
 if (const auto* text=AcDbText::cast(&entity)) {
  AcDbExtents box;
  if (label==text->textString() && text->getGeomExtents(box)==Acad::eOk) return GenericCaptionGlyph{box,text->rotation(),text->layerId(),text->color(),text->textStyle(),text->height()};
  return {};
 }
 if (AcDbLine::cast(&entity) || AcDbArc::cast(&entity) || AcDbPolyline::cast(&entity)) return {};
 TemplateParts parts(&entity);
 if (entity.explode(parts.values)!=Acad::eOk) return {};
 for (int i=0;i<parts.values.length();++i)
  if (const auto* child=AcDbEntity::cast(static_cast<AcRxObject*>(parts.values[i])))
   if (auto glyph=productGenericCaptionGlyph(*child,label,depth+1)) return glyph;
 return {};
}
AcGePoint3d productGenericGlyphCentre(const GenericCaptionGlyph& glyph) {
 return glyph.box.minPoint()+(glyph.box.maxPoint()-glyph.box.minPoint())*.5;
}
struct GenericOpeningCaption {
 AcDbObjectId id;
 std::wstring number;
 AcGePoint3d desired;
 double width{},height{};
 int panel{};
 bool vertical{};
 const char* source{};
 std::optional<double> clearanceDatum;
 bool hasSwing{};
 std::wstring ruleNumber;
 bool window{},grouped{};
 std::optional<double> unnumberedVoidWallDatum;
 std::optional<double> windowWallDatum;
 double windowWallThickness{};
};

std::wstring productGenericOpeningCode(const wchar_t* prefix,double width,double height){
 return bcpproduct::planOpeningCode(prefix,width,height);
}
std::optional<GenericCaptionGlyph> productGenericMoveCaption(AcDbObjectId id,const std::wstring& number,
                                                            const AcGePoint3d& desired) {
 const auto readGlyph=[&](){
  auto e=open<AcDbEntity>(id);
  return productGenericCaptionGlyph(*e,number);
 };
 auto glyph=readGlyph();
 const auto original=chainGrips(id);
 require(original.size()>3,"native opening caption grip unavailable");
 for (int attempt=0;attempt<2;++attempt) {
  const auto grips=chainGrips(id);
  const auto actual=glyph?productGenericGlyphCentre(*glyph):grips.back();
  const auto shift=desired-actual;
  if (shift.length()<1e-5) break;
  moveDimensionGrip(id,static_cast<int>(grips.size()-1),grips.back()+shift);
  glyph=readGlyph();
  if (!glyph) break;
 }
 const auto moved=chainGrips(id);
 require(moved.size()==original.size(),"numbering changed actual opening grip count");
 for (std::size_t k=0;k+1<original.size();++k)
  require(original[k].distanceTo(moved[k])<1e-4,"caption alignment changed actual opening geometry");
 return glyph;
}
std::vector<AcDbExtents> productOpeningDimensionTextInk(AcDbDatabase& db,const AcDbEntity& entity){
 std::vector<AcDbExtents> labels;
 productGenericTextLeaves(entity,labels);
 if(std::wstring_view(entity.isA()->name())==L"TDbDimension2"){
  const auto resolved=productPlanChainGlyphs(db,entity);
  labels.insert(labels.end(),resolved.begin(),resolved.end());
 }
 return labels;
}

std::string productOpeningCaptionGeometryBlock(AcDbDatabase& db,const AcDbExtents& box,bool includeDimensions=false,
 double dimensionMargin=0.){
 for(const auto& [handle,type]:modelTypes(db)){
  const auto entity=open<AcDbEntity>(find(db,ascii(handle)));
  if(!productPlanDimensionVisible(*entity))continue;
  const auto layer=symbolName(entity->layerId());
  const bool dimension=includeDimensions&&(type==L"TDbDimension2"||AcDbDimension::cast(entity.get()));
  const bool geometry=dimension||type==L"TDbWall"||type==L"TDbColumn"||type==L"TDbRectStair"||
   type==L"TDbOpening"||type==L"TDbGroupOpening"||type==L"AcDbBlockReference"||
   layer==L"STAIR"||layer==L"A-STAIR"||productFurnitureLayerName(layer)||layer==L"家具";
  if(!geometry)continue;
  const double margin=dimension?dimensionMargin:0.;
  AcDbExtents bounds;
  if(entity->getGeomExtents(bounds)!=Acad::eOk||!productGenericBoxesOverlap(bounds,box,margin))continue;
  GenericOpeningInk ink;
  if(dimension)productOpeningDimensionDisplayInk(*entity,ink);
  else productGenericOpeningInk(*entity,ink);
  if(dimension){
   const auto labels=productOpeningDimensionTextInk(db,*entity);
   if(std::any_of(labels.begin(),labels.end(),[&](const auto& label){return productGenericBoxesOverlap(label,box,margin);}))
    return ascii(handle);
  }
  const auto hits=[&](const auto& line){return productGenericSegmentHitsBox(line.first,line.second,box,margin-1e-5);};
  if((ink.arcs.empty()&&ink.curved.empty()&&ink.straight.empty())||
     std::any_of(ink.curved.begin(),ink.curved.end(),hits)||
     std::any_of(ink.straight.begin(),ink.straight.end(),hits)||
     (ink.curved.empty()&&std::any_of(ink.arcs.begin(),ink.arcs.end(),[&](const auto& arc){
      return productGenericBoxesOverlap(arc,box,0);
     })))return ascii(handle);
 }
 return {};
}

void productGenericRecenterCaptions(ProductJob& job,AcDbDatabase& db,const std::vector<GenericOpeningCaption>& captions) {
 Working context(&db);
 std::map<int,std::vector<const GenericOpeningCaption*>> panels;
 for (const auto& caption:captions) panels[caption.panel].push_back(&caption);
 for (const auto& panel:panels) {
  struct Placement {
   const GenericOpeningCaption* item;
   AcGePoint3d target;
   std::optional<GenericCaptionGlyph> glyph;
   bool changedSide{};
   bool choseVoidDefaultSide{};
   std::string defaultSideBlock,alternateSideBlock;
  };
  std::vector<Placement> placed;
  for (const auto* item:panel.second) {
   auto desired=item->desired;
   if (item->clearanceDatum) {
    const auto entity=open<AcDbEntity>(item->id);
    if (const auto current=productGenericCaptionGlyph(*entity,item->number)) {
     const bool vertical=item->vertical;
     const double width=vertical?current->box.maxPoint().x-current->box.minPoint().x
                                :current->box.maxPoint().y-current->box.minPoint().y;
     auto& across=vertical?desired.x:desired.y;
     const double side=across>*item->clearanceDatum?1.:-1.;
     across=*item->clearanceDatum+side*(200.+width/2);
    }
   }
   placed.push_back({item,desired,productGenericMoveCaption(item->id,item->number,desired)});
  }
  const auto collides=[&](const Placement& current,const AcDbExtents& box) {
   return std::any_of(placed.begin(),placed.end(),[&](const auto& other) {
    return &current!=&other && other.glyph && productGenericBoxesOverlap(box,other.glyph->box,0);
   });
  };
  for (auto& current:placed) {
   const auto& item=*current.item;
   if (!current.glyph) continue;
   const bool captionConflict=item.clearanceDatum&&item.hasSwing&&collides(current,current.glyph->box);
   // Only a newly numbered interior wall hole can use the actual clear side
   // as its default. Existing numbers keep their documented side policy.
   if(item.unnumberedVoidWallDatum)current.defaultSideBlock=productOpeningCaptionGeometryBlock(db,current.glyph->box);
   const bool chooseVoidDefault=item.unnumberedVoidWallDatum&&!current.defaultSideBlock.empty();
   if (!captionConflict&&!chooseVoidDefault) continue;
   auto target=current.target;
   auto& across=item.vertical?target.x:target.y;
   const double datum=captionConflict?*item.clearanceDatum:*item.unnumberedVoidWallDatum;
   across=2*datum-across;
   const auto shift=target-current.target;
   const AcDbExtents preview(current.glyph->box.minPoint()+shift,current.glyph->box.maxPoint()+shift);
   if(collides(current,preview)){current.alternateSideBlock="opening-caption";continue;}
   current.alternateSideBlock=productOpeningCaptionGeometryBlock(db,preview);
   if(!current.alternateSideBlock.empty())continue;
   auto glyph=productGenericMoveCaption(item.id,item.number,target);
   if (glyph && !collides(current,glyph->box) && productOpeningCaptionGeometryBlock(db,glyph->box).empty()) {
    current.target=target;
    current.glyph=glyph;
    current.changedSide=captionConflict;
    current.choseVoidDefaultSide=chooseVoidDefault;
   } else current.glyph=productGenericMoveCaption(item.id,item.number,current.target);
  }
  std::ostringstream report;
  report<<std::setprecision(17)<<"{\"labels\":[";
  bool first=true;
  for (const auto& current:placed) {
   const auto* item=current.item;
   const auto& glyph=current.glyph;
   const auto& desired=current.target;
   const auto handle=axisHandle(item->id);
   const double target=item->vertical?item->desired.y:item->desired.x;
   const auto actual=glyph?productGenericGlyphCentre(*glyph):AcGePoint3d();
   const bool centered=glyph && std::abs((item->vertical?actual.y:actual.x)-target)<1e-4;
   const bool aligned=glyph && std::abs(item->vertical?std::cos(glyph->angle):std::sin(glyph->angle))<1e-4;
   std::optional<double> clearance;
   if (glyph && item->clearanceDatum) {
    const bool vertical=item->vertical;
    const double side=(vertical?desired.x:desired.y)>*item->clearanceDatum?1.:-1.;
    const auto edge=side>0?glyph->box.minPoint():glyph->box.maxPoint();
    clearance=side*((vertical?edge.x:edge.y)-*item->clearanceDatum);
    if (std::abs(*clearance-200.)>1e-4)
     job.detailPending.push_back({u8"室内门编号净距",handle,u8"实际字形至洞口线未达到200毫米，待复核"});
   }
   if (glyph && collides(current,glyph->box))
    job.detailPending.push_back({u8"门窗编号碰撞",handle,u8"同侧编号相撞且未找到符合换侧条件的位置，保留定位待复核"});
   if (!centered || !aligned) job.detailPending.push_back({u8"门窗编号居中",handle,u8"原生编号已写入，实际文字居中或方向需复核"});
   if (!first) report<<',';
   first=false;
   report<<"{\"handle\":\""<<handle<<"\",\"number\":"<<jsonUtf8(item->number)
         <<",\"width\":"<<item->width<<",\"height\":"<<item->height<<",\"centre\":"<<target
         <<",\"vertical\":"<<(item->vertical?"true":"false")<<",\"centered\":"<<(centered?"true":"false")
         <<",\"aligned\":"<<(aligned?"true":"false")<<",\"source\":\""<<item->source<<"\",\"glyph\":";
   if (glyph) report<<wholePoints({glyph->box.minPoint(),glyph->box.maxPoint()}); else report<<"null";
   report<<",\"collisionSideException\":"<<(current.changedSide?"true":"false")
         <<",\"newVoidDefaultSideChosen\":"<<(current.choseVoidDefaultSide?"true":"false")
         <<",\"defaultSideBlock\":\""<<current.defaultSideBlock
         <<"\",\"alternateSideBlock\":\""<<current.alternateSideBlock
         <<"\",\"interiorClearanceMM\":";
   if (clearance) report<<*clearance; else report<<"null";
   report<<'}';
  }
  report<<"]}";
  writeNew(job.root/(L"通用门窗编号_"+std::to_wstring(panel.first)+L".json"),report.str());
 }
}
void productAdoptMappedOpening(AcDbObjectId id,const AcDbEntity& candidate){
 std::unique_ptr<AcDbEntity> replacement(AcDbEntity::cast(candidate.clone()));
 require(replacement!=nullptr,"clone mapped native opening for identity-preserving replacement");
 auto original=editObject<AcDbEntity>(id);
 const auto status=original->handOverTo(replacement.get(),true,true);
 require(status==Acad::eObjectToBeDeleted,"native opening handover did not transfer object ownership");
 // AutoCAD 2020 SDK samples/entity/polysamp/command.cpp.
 delete original.release();
 productStatus(replacement.release()->close(),"close mapped native opening");
}

AcDbObjectId productCompactCaptionStyle(AcDbDatabase& db,AcDbObjectId source,double factor){
 const auto prior=open<AcDbTextStyleTableRecord>(source);
 const auto name=symbolName(source)+L"_L05_"+std::to_wstring(int(std::lround(factor*100)));
 auto table=editObject<AcDbTextStyleTable>(db.textStyleTableId());
 AcDbObjectId id;
 const double width=prior->xScale()*factor;
 if(table->getAt(name.c_str(),id)==Acad::eOk){
  const auto existing=open<AcDbTextStyleTableRecord>(id);
  AcString font,big,priorFont,priorBig;
  if(existing->fileName(font)!=Acad::eOk||existing->bigFontFileName(big)!=Acad::eOk||
     prior->fileName(priorFont)!=Acad::eOk||prior->bigFontFileName(priorBig)!=Acad::eOk||
     font!=priorFont||big!=priorBig||std::abs(existing->xScale()-width)>1e-8)return {};
  return id;
 }
 std::unique_ptr<AcDbTextStyleTableRecord> style(AcDbTextStyleTableRecord::cast(prior->clone()));
 require(style!=nullptr,"copy public caption text style");
 productStatus(style->setName(name.c_str()),"name local caption width variant");
 productStatus(style->setXScale(width),"adjust only the caption style width factor");
 productStatus(table->add(id,style.get()),"add local caption width variant");
 productStatus(style.release()->close(),"close local caption style");
 return id;
}

AcDbExtents productCaptionInkSafetyBox(const AcDbExtents& box,double padding){
 const AcGeVector3d pad(padding,padding,0.);
 return {box.minPoint()-pad,box.maxPoint()+pad};
}

bool productRemapCaption(AcDbDatabase& db,const GenericOpeningCaption& caption,
 const GenericCaptionGlyph& prior,AcDbObjectId target,AcDbObjectId style,bool includeDimensions,double padding=0.,
 bool requireInkClearance=true){
 auto source=open<AcDbEntity>(caption.id);
 const auto before=chainGrips(caption.id);
 const std::wstring type=source->isA()->name();
 const auto sourceLayer=source->layerId();
 const auto sourceColor=source->color();
 source.reset();
 const auto sameGeometry=[&](AcDbObjectId id,const AcDbEntity& entity){
  const auto after=chainGrips(id);
  return std::wstring(entity.isA()->name())==type&&entity.layerId()==sourceLayer&&entity.color()==sourceColor&&
   before.size()==after.size()&&!before.empty()&&
   std::equal(before.begin(),before.end()-(style.isNull()?0:1),after.begin(),
    [](const auto& a,const auto& b){return a.distanceTo(b)<1e-5;});
 };
 AcDbObjectIdArray objects;objects.append(caption.id);
 AcDbIdMapping mapping;
 if(prior.layer!=target)mapping.assign(AcDbIdPair(prior.layer,target,false));
 if(!style.isNull())mapping.assign(AcDbIdPair(prior.style,style,false));
 productStatus(db.deepCloneObjects(objects,modelId(db),mapping,false),"clone native opening for caption style or layer");
 AcDbIdPair pair(caption.id,AcDbObjectId{},false);
 require(mapping.compute(pair)&&pair.isCloned()&&!pair.value().isNull(),"mapped native caption candidate");
 const auto id=pair.value();
 auto clone=open<AcDbEntity>(id);
 auto glyph=productGenericCaptionGlyph(*clone,caption.number);
 const auto centre=productGenericGlyphCentre(prior);
 if(!style.isNull()&&glyph&&glyph->style==style&&glyph->layer==target){
  clone.reset();
  glyph=productGenericMoveCaption(id,caption.number,centre);
  clone=open<AcDbEntity>(id);
 }
 const bool valid=sameGeometry(id,*clone)&&glyph&&glyph->layer==target&&
  std::abs(glyph->height-prior.height)<1e-5&&
  (style.isNull()||(glyph->style==style&&productGenericGlyphCentre(*glyph).distanceTo(centre)<1e-4&&
    (!requireInkClearance||
     productOpeningCaptionGeometryBlock(db,productCaptionInkSafetyBox(glyph->box,padding),includeDimensions).empty())));
 if(valid)productAdoptMappedOpening(caption.id,*clone);
 clone.reset();
 productStatus(editObject<AcDbEntity>(id)->erase(),"retire temporary caption clone");
 const auto actual=open<AcDbEntity>(caption.id);
 require(sameGeometry(caption.id,*actual),"caption mapping retains original opening geometry and identity");
 return valid;
}

std::optional<double> productTryCompactCaption(AcDbDatabase& db,const GenericOpeningCaption& caption,
 const GenericCaptionGlyph& prior,AcDbObjectId layer,bool includeDimensions){
 if(productJob&&productJob->architecturalPlanOnly)return {};
 const double paper=productJob?productJob->unifiedScale/productJob->unifiedUnitMM:100.;
 const double padding=(includeDimensions?.12:.35)*paper;
 if(prior.style.isNull()||prior.layer.isNull()||
    productOpeningCaptionGeometryBlock(db,productCaptionInkSafetyBox(prior.box,padding),includeDimensions).empty())return {};
 const auto centre=productGenericGlyphCentre(prior);
 const auto a=prior.box.minPoint(),b=prior.box.maxPoint();
 for(const double factor:{.98,.96,.94,.92,.90,.88,.86,.84,.82}){
  if(!includeDimensions&&factor<.90)break;
  if(!caption.window&&factor<.86)break;
  const double half=(caption.vertical?b.y-a.y:b.x-a.x)*factor/2;
  const AcDbExtents box(caption.vertical?AcGePoint3d(a.x,centre.y-half,a.z):AcGePoint3d(centre.x-half,a.y,a.z),
   caption.vertical?AcGePoint3d(b.x,centre.y+half,b.z):AcGePoint3d(centre.x+half,b.y,b.z));
  if(!productOpeningCaptionGeometryBlock(db,productCaptionInkSafetyBox(box,padding),includeDimensions).empty())continue;
  const auto style=productCompactCaptionStyle(db,prior.style,factor);
  if(!style.isNull()&&productRemapCaption(db,caption,prior,layer,style,includeDimensions,padding))return factor;
 }
 return {};
}

AcDbObjectId productRequiredOpeningCaptionStyle(AcDbDatabase& db,AcDbObjectId original){
 if(productJob&&!productJob->approvedOpeningTextStyle.isNull()&&productJob->approvedOpeningTextStyle.database()==&db)
  return productJob->approvedOpeningTextStyle;
 AcDbDatabase reference(false,true);AcDbObjectId donor;
 if(productJob){
  productStatus(reference.readDwgFile((productJob->profile/L"reference.dwg").c_str(),AcDbDatabase::kForReadAndAllShare,false),"read approved opening font definition");
  productStatus(reference.closeInput(true),"close approved opening font input");
  const auto styles=open<AcDbTextStyleTable>(reference.textStyleTableId());
  if(styles->getAt(L"TCH_WINDOW",donor)!=Acad::eOk)productStatus(styles->getAt(L"_TCH_WINDOW",donor),"approved native opening text style");
 }
 require(!donor.isNull(),"opening typography requires an approved reference, not the source style");
 const auto approved=open<AcDbTextStyleTableRecord>(donor);AcString primary,big;
 productStatus(approved->fileName(primary),"read approved opening primary font");productStatus(approved->bigFontFileName(big),"read approved opening big font");
 const auto table=editObject<AcDbTextStyleTable>(db.textStyleTableId());
 AcDbObjectId id;
 if(table->getAt(L"TCH_WINDOW",id)!=Acad::eOk){
  auto style=std::make_unique<AcDbTextStyleTableRecord>();productStatus(style->setName(L"TCH_WINDOW"),"name the required opening caption style");
  productStatus(table->add(id,style.get()),"add required opening caption style");style.release()->close();
 }
 {auto style=editObject<AcDbTextStyleTableRecord>(id);
  productStatus(style->setFileName(primary.kACharPtr()),"restore opening primary font");productStatus(style->setBigFontFileName(big.kACharPtr()),"restore opening big font");
  productStatus(style->setXScale(approved->xScale()),"restore opening width factor");productStatus(style->setTextSize(3.),"restore opening style nominal height");
  productStatus(style->setObliquingAngle(approved->obliquingAngle()),"restore opening oblique");style->setFlagBits(approved->flagBits());style->setIsVertical(false);
 }
 if(productJob)productJob->approvedOpeningTextStyle=id;
 return id;
}

void productPrepareCaptionLayer(AcDbDatabase& db,const std::vector<GenericOpeningCaption>& captions,
                                bool allViewsGenerated) {
 if(captions.empty())return;
 const auto target=productPlanLayer(db,L"A-WINDOW",7);
 const bool exactStyle=productJob&&productJob->architecturalPlanOnly;
 std::ostringstream report;report<<"{\"method\":\"native-object-layer-remap\",\"captions\":[";bool first=true;
 for(const auto& caption:captions){
  const auto prior=productGenericCaptionGlyph(*open<AcDbEntity>(caption.id),caption.number);
  std::optional<double> compact;
  bool verified=prior&&prior->layer==target;
  std::string reason=verified?"already-current":"missing-native-caption-layer";
  if(prior&&!prior->layer.isNull()){
   if(exactStyle&&!prior->style.isNull()){
    const auto style=productRequiredOpeningCaptionStyle(db,prior->style);
    if(prior->style!=style||!verified)
     reason=productRemapCaption(db,caption,*prior,target,style,false,0.,false)?
      "remapped-native-caption-style-and-layer":"native-caption-style-remap-not-applied";
   }else if(!exactStyle&&caption.number.rfind(L"FMD",0)==0&&caption.hasSwing)
    compact=productTryCompactCaption(db,caption,*prior,target,false);
   if(compact){verified=true;reason="remapped-native-caption-width-and-layer";}
   else if(!verified&&(!exactStyle||prior->style.isNull())){
    verified=productRemapCaption(db,caption,*prior,target,{},false);
    reason=verified?"remapped-native-caption-layer":"native-clone-layer-remap-not-applied";
   }
   const auto actual=productGenericCaptionGlyph(*open<AcDbEntity>(caption.id),caption.number);
   verified=actual&&actual->layer==target&&(!exactStyle||(!actual->style.isNull()&&symbolName(actual->style)==L"TCH_WINDOW"));
  }
  if(!first)report<<',';first=false;
  report<<"{\"object\":\""<<axisHandle(caption.id)<<"\",\"priorLayer\":"
   <<jsonUtf8(prior&&!prior->layer.isNull()?symbolName(prior->layer):L"<null>")
   <<",\"compactedWidthFactor\":"<<(compact?std::to_string(*compact):"null")
   <<",\"verified\":"<<(verified?"true":"false")<<",\"result\":\""<<reason<<"\"}";
 }
 report<<"]}";
 if(productJob)writeNew(productJob->root/L"caption-layer-preparation.json",report.str());
}

void productCompactRemainingCaptionConflicts(ProductJob& job,AcDbDatabase& db,
 const std::vector<GenericOpeningCaption>& captions){
 if(job.architecturalPlanOnly)return;
 // Dimensions already tried all applicable placement moves. Fit actual
 // residual wall-opening/door numbers with the same font, height and centre.
 std::ostringstream report;report<<"{\"afterDimensionLayout\":true,\"checks\":[";
 bool first=true;
 for(const auto& caption:captions){
  if(caption.window||caption.grouped)continue;
  const auto prior=productGenericCaptionGlyph(*open<AcDbEntity>(caption.id),caption.number);
  if(!prior||prior->layer.isNull())continue;
  const auto obstacle=productOpeningCaptionGeometryBlock(db,prior->box,true);
  if(obstacle.empty())continue;
  const auto entity=open<AcDbEntity>(find(db,obstacle));
  if(std::wstring(entity->isA()->name())!=L"TDbDimension2"&&!AcDbDimension::cast(entity.get()))continue;
  const auto compact=productTryCompactCaption(db,caption,*prior,prior->layer,true);
  if(!first)report<<',';first=false;
  report<<"{\"opening\":\""<<axisHandle(caption.id)<<"\",\"dimension\":\""<<obstacle
   <<"\",\"widthFactor\":"<<(compact?std::to_string(*compact):"null")<<'}';
 }
 report<<"]}";writeNew(job.root/L"caption-dimension-clearance-placement.json",report.str());
}

void productAuditCaptionLayer(ProductJob& job,AcDbDatabase& db,const std::vector<GenericOpeningCaption>& captions,
 const GenericNativeInput* input=nullptr,const std::vector<bcpproduct::GenericView>* views=nullptr,
 double unitMM=1.,const std::map<std::wstring,std::wstring>* sourceTypes=nullptr) {
 Working context(&db);
 int verified=0;
 std::map<std::string,bcpproduct::PublicProperties> fields;
 const auto confirmations=productPlanDetailConfirmations(job);
 const auto* sourceOpenings=input?&input->openings:nullptr;
 if(sourceOpenings&&!sourceOpenings->empty()){
  std::set<std::string> targets;
  for(const auto& caption:captions)targets.insert(axisHandle(caption.id));
  targets.insert(confirmations.bathroomDoors.begin(),confirmations.bathroomDoors.end());
  for(const auto& opening:*sourceOpenings)targets.insert(opening.handle);
  try{fields=productReadProperties(db,job.root/L"门窗编号重开公开属性",&targets);}
  catch(const std::exception& error){
   job.detailPending.push_back({u8"门窗编号重开属性",u8"项目",error.what()});
  }
 }
 if(sourceOpenings&&!confirmations.bathroomDoors.empty()){
  std::ostringstream report;
  report<<"{\"savedReopened\":true,\"doors\":[";
  bool first=true;
  for(const auto& handle:confirmations.bathroomDoors){
   const auto source=std::find_if(sourceOpenings->begin(),sourceOpenings->end(),[&](const auto& opening){
    return opening.handle==handle;
   });
   const auto found=fields.find(handle);
   bool geometry=false,withoutNumber=false;
   try{
    if(source!=sourceOpenings->end()&&found!=fields.end()){
     const auto id=find(db,handle);
     const auto entity=open<AcDbEntity>(id);
     const auto grips=chainGrips(id);
     const auto& properties=found->second;
     const auto field=properties.find(L"门窗编号");
     withoutNumber=field!=properties.end()&&field->second.empty();
     const auto same=[&](const AcGePoint3d& point,AcGePoint3d actual){
      actual.z=0.;
      return point.distanceTo(actual)*job.unifiedUnitMM<=.01;
     };
     geometry=std::wstring(entity->isA()->name())==L"TDbOpening"&&grips.size()>=3&&
      same(source->a,grips[1])&&same(source->b,grips[2]);
    }
   }catch(const std::exception& error){
    job.detailPending.push_back({u8"卫生间内部隔门",handle,error.what()});
   }
   const bool passed=withoutNumber&&geometry;
   if(!passed)job.detailPending.push_back({u8"卫生间内部隔门",handle,u8"独立重开后未确认源洞口端点和自身空编号，待复核 D14"});
   const auto row=std::string("{\"object\":")+jsonUtf8(bcpproduct::decodeMessage(handle))+
    ",\"endpointsRetained\":"+(geometry?"true":"false")+",\"ownNumberEmpty\":"+(withoutNumber?"true":"false")+
    ",\"savedReopened\":true,\"result\":\""+(passed?"verified-bathroom-door-number-omission":"pending")+"\"}";
   if(!first)report<<',';
   first=false;
   report<<row;
   productRecordRuleEvidence(job,"D14",row);
  }
  report<<"]}";
  writeNew(job.root/L"bathroom-door-readback.json",report.str());
 }
 if(!job.interiorDoorMeasurements.empty()){
  struct SavedInterval {AcGePoint3d a,b;std::string handle;};
  std::vector<SavedInterval> intervals;
  for(const auto& [raw,type]:modelTypes(db)){
   if(type!=L"TDbDimension2"&&type!=L"AcDbAlignedDimension"&&type!=L"AcDbRotatedDimension")continue;
   const auto handle=ascii(raw);
   const auto id=find(db,handle);
   const auto entity=open<AcDbEntity>(id);
   if(!productPlanDimensionVisible(*entity))continue;
   if(const auto* dim=AcDbAlignedDimension::cast(entity.get())){
    intervals.push_back({dim->xLine1Point(),dim->xLine2Point(),handle});
   }else if(const auto* dim=AcDbRotatedDimension::cast(entity.get())){
    intervals.push_back({dim->xLine1Point(),dim->xLine2Point(),handle});
   }else{
    const auto values=chainValues(id);
    const auto grips=chainGrips(id);
    if(grips.size()!=2*values.size()+6)continue;
    for(std::size_t k=0;k<values.size();++k)intervals.push_back({grips[k+2],grips[k+3],handle});
   }
  }
  std::ostringstream report;
  report<<std::setprecision(17)<<"{\"savedReopened\":true,\"doors\":[";
  bool first=true;
  const auto unit=job.unifiedUnitMM;
  for(const auto& row:job.interiorDoorMeasurements){
   const auto id=find(db,row.door);
   const auto entity=open<AcDbEntity>(id);
   const auto grips=chainGrips(id);
   const auto station=[&](const auto& point){return row.vertical?point.y:point.x;};
   const auto plane=[&](const auto& point){return row.vertical?point.x:point.y;};
   const auto match=[&](const auto& a,const auto& b){return std::abs(station(a)-station(b))*unit<.01;};
   const bool endpoints=grips.size()>=3&&((match(grips[1],row.first)&&match(grips[2],row.second))||
    (match(grips[1],row.second)&&match(grips[2],row.first)));
   const auto hit=[&](const auto& a,const auto& b){
    const auto result=std::find_if(intervals.begin(),intervals.end(),[&](const auto& dim){
     const bool alongHost=std::abs(plane(dim.a)-plane(row.first))*unit<.01&&std::abs(plane(dim.b)-plane(row.first))*unit<.01;
     return alongHost&&((match(dim.a,a)&&match(dim.b,b))||(match(dim.a,b)&&match(dim.b,a)));
    });
    return result==intervals.end()?std::string():result->handle;
   };
   const auto measured=hit(row.first,row.second);
   const auto nearest=std::abs(station(row.jamb)-station(row.first))<std::abs(station(row.jamb)-station(row.second))?row.first:row.second;
   const auto jambMeasured=row.hasJamb?hit(nearest,row.jamb):std::string();
   bool ownNumberMatches=false;
   if(const auto found=fields.find(row.door);found!=fields.end()){
    const auto number=found->second.find(L"门窗编号");
    ownNumberMatches=number!=found->second.end()&&(row.bathroom?number->second.empty():
       !row.ownNumber.empty()&&row.ownNumber==number->second&&productGenericCaptionGlyph(*entity,row.ownNumber).has_value());
   }
   const double actualJamb=row.hasJamb?std::abs(station(row.jamb)-station(nearest))*unit:0.;
   const bool jambVerified=!row.hasJamb||(row.omitJamb?
    jambMeasured.empty()&&std::abs(actualJamb-row.standardMM)<=.01:!jambMeasured.empty());
   const bool verified=endpoints&&jambVerified&&(row.omit?ownNumberMatches&&measured.empty():!measured.empty());
   const auto result=verified?(row.omit?"verified-door-width-omission":
       row.omitJamb?"verified-jamb-only-omission":"verified-door-width-retained"):"pending";
   if(!verified)job.detailPending.push_back({u8"室内洞口测量",row.door,
    u8"重开后自身编号、洞口测站或应有和省略尺寸不符，复核 D13/TL11"});
   const auto change=job.doorJambChanges.find(row.door);
   std::ostringstream evidence;
   evidence<<std::setprecision(17)<<"{\"panel\":"<<row.panel<<",\"object\":\""<<row.door<<"\",\"host\":\""<<row.hostWall
    <<"\",\"adjacentWall\":\""<<row.adjacentWall<<"\",\"stationCoordinates\":"<<wholePoints({row.first,row.second})
    <<",\"netJambMM\":"<<(row.hasJamb?std::to_string(actualJamb):"null")
    <<",\"omittedJamb\":"<<(row.omitJamb?"true":"false")
    <<",\"priorNetJambMM\":"<<(change!=job.doorJambChanges.end()&&!change->second.adjacent.empty()?std::to_string(change->second.priorMM):"null")
    <<",\"translationMM\":"<<(change==job.doorJambChanges.end()?0.:change->second.offset.length()*unit)
    <<",\"reason\":"<<jsonUtf8(change==job.doorJambChanges.end()?L"":bcpproduct::decodeMessage(change->second.reason))
    <<",\"ownNumberVerified\":"<<(ownNumberMatches?"true":"false")<<",\"visibleWidthChain\":\""<<measured
    <<"\",\"visibleJambChain\":\""<<jambMeasured<<"\",\"standardJambMM\":"<<row.standardMM
    <<",\"widthMM\":"<<std::abs(station(row.first)-station(row.second))*unit
    <<",\"scope\":\""<<(row.bathroom?"bathroom-door-width-and-number":"host-door-width-and-jamb-intervals")
    <<"\",\"savedReopened\":true,\"result\":\""<<result<<"\"}";
   if(!first)report<<',';
   first=false;
   report<<evidence.str();
   productRecordRuleEvidence(job,row.bathroom?"D14":"D13",evidence.str());
   if(!row.bathroom)productRecordRuleEvidence(job,"TL11",evidence.str());
  }
  report<<"]}";
  writeNew(job.root/L"interior-door-measurement-readback.json",report.str());
 }
 for (const auto& caption:captions) {
  AcDbObjectId id;
  const auto handle=caption.id.handle();
  const auto key=axisHandle(caption.id);
  bool layerVerified=false,bodyLayer=false,native=false,matches=false,aligned=false,centered=false,clearanceOK=false;
  std::optional<double> clearance;
  std::vector<double> heights;
  std::optional<GenericCaptionGlyph> glyph;
  std::wstring property;
  if (db.getAcDbObjectId(id,false,handle)==Acad::eOk) {
   const auto entity=open<AcDbEntity>(id);
   const std::wstring type=entity->isA()->name();
   native=!entity->isAProxy()&&(type==L"TDbOpening"||type==L"TDbGroupOpening");
   const auto effective=entity->color().isByLayer()?open<AcDbLayerTableRecord>(entity->layerId())->color():entity->color();
   bodyLayer=symbolName(entity->layerId())==L"WINDOW"&&effective.isByACI()&&effective.colorIndex()==2;
   glyph=productGenericCaptionGlyph(*entity,caption.number);
   const auto row=fields.find(key);
   if(row!=fields.end()){
    const auto number=row->second.find(L"门窗编号");
    if(number!=row->second.end())property=number->second;
   }
   matches=native&&glyph&&property==caption.number;
   if(glyph){
    const auto centre=productGenericGlyphCentre(*glyph);
    centered=std::abs(caption.vertical?centre.y-caption.desired.y:centre.x-caption.desired.x)<1e-4;
    aligned=std::abs(caption.vertical?std::cos(glyph->angle):std::sin(glyph->angle))<1e-4;
    if(caption.clearanceDatum){
     const auto cross=[&](const AcGePoint3d& point){return caption.vertical?point.x:point.y;};
     const double side=cross(centre)>*caption.clearanceDatum?1.:-1.;
     clearance=side*(cross(side>0?glyph->box.minPoint():glyph->box.maxPoint())-*caption.clearanceDatum);
    }
    clearanceOK=!caption.clearanceDatum||(clearance&&std::abs(*clearance-200.)<1e-4);
   }
   if(sourceOpenings||job.architecturalPlanOnly)productOpeningTextHeights(*entity,heights);
   if (glyph && !glyph->layer.isNull() && symbolName(glyph->layer)==L"A-WINDOW") {
    const auto effective=glyph->color.isByLayer()?open<AcDbLayerTableRecord>(glyph->layer)->color():glyph->color;
    layerVerified=(effective.isByACI()||effective.isForeground())&&effective.colorIndex()==7;
   }
  }
  const auto sourcePanel=caption.panel>=0&&static_cast<std::size_t>(caption.panel)<job.layoutSourcePanels.size()?
   job.layoutSourcePanels[caption.panel]:static_cast<std::size_t>(caption.panel+1);
  job.drawingChecks.record(sourcePanel,"opening-caption-layer",key,{"Y01"},bcpproduct::PlanReleaseGate::mainPlan,
   bodyLayer&&layerVerified?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::failed,
   u8"原生门窗本体WINDOW及自身号A-WINDOW的实际显示层色",bodyLayer&&layerVerified?"":u8"门窗本体或编号实际显示层色不符合现行规则");
  if(layerVerified)++verified;
  else job.detailPending.push_back({u8"门窗编号图层",key,u8"保存重开后的原生编号显示层或有效7色待核，保留当前原生表达"});
  if(job.architecturalPlanOnly){
   const auto style=glyph&&!glyph->style.isNull()?symbolName(glyph->style):std::wstring{};
   const bool passed=style==L"TCH_WINDOW"&&heights.size()==1&&std::abs(heights.front()-255.)<1e-5;
   job.drawingChecks.record(sourcePanel,"opening-caption-typography",key,{"L08"},bcpproduct::PlanReleaseGate::reopened,
    passed?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::failed,
    u8"重开读回原生门窗编号的实际文字样式和纸面 3.0 mm 对应字高",
    passed?"":u8"实际编号未引用 TCH_WINDOW 或编号字高未达到规则");
   productRecordRuleEvidence(job,"L08","{\"object\":\""+key+"\",\"savedReopened\":true,\"textStyle\":"+
     jsonUtf8(style)+",\"verified\":"+(passed?"true":"false")+"}");
  }
  if(!sourceOpenings)continue;
  std::ostringstream record;
  record<<std::setprecision(17)<<"{\"panel\":"<<caption.panel+1<<",\"object\":\""<<key
        <<"\",\"expected\":"<<jsonUtf8(caption.number)<<",\"nativeProperty\":"<<jsonUtf8(property)
        <<",\"native\":"<<(native?"true":"false")<<",\"glyphMatches\":"<<(glyph?"true":"false")
        <<",\"centered\":"<<(centered?"true":"false")<<",\"aligned\":"<<(aligned?"true":"false")
        <<",\"layerVerified\":"<<(layerVerified?"true":"false")<<",\"clearanceMM\":";
  if(clearance)record<<*clearance;else record<<"null";
  record<<",\"glyphBox\":";
  if(glyph)record<<wholePoints({glyph->box.minPoint(),glyph->box.maxPoint()});else record<<"null";
  record<<",\"savedReopened\":true,\"result\":\"";
  const auto prefix=record.str();
  productRecordRuleEvidence(job,"O06",prefix+(matches?"verified-opening-number":"pending")+"\"}");
  productRecordRuleEvidence(job,"TL02",prefix+(matches&&centered&&aligned?"verified-native-number-placement":"pending")+"\"}");
  productRecordRuleEvidence(job,"L04",prefix+(centered&&aligned&&clearanceOK?"verified-caption-alignment-and-clearance":"pending")+"\"}");
  productRecordRuleEvidence(job,"Y01","{\"panel\":"+std::to_string(caption.panel+1)+",\"object\":\""+key+
   "\",\"scope\":\"opening-body-and-caption-layer\",\"bodyLayerVerified\":"+(bodyLayer?"true":"false")+
   ",\"captionLayerVerified\":"+(layerVerified?"true":"false")+",\"savedReopened\":true,\"result\":\""+
   (bodyLayer&&layerVerified?"verified-window-and-caption-layer-colors":"pending-opening-layer-colors")+"\"}");
  std::ostringstream height;
  height<<std::setprecision(17)<<"{\"panel\":"<<caption.panel+1<<",\"object\":\""<<key
        <<"\",\"glyphHeight\":";
  if(heights.size()==1)height<<heights.front();else height<<"null";
  height<<",\"heightField\":\"文字高度\",\"savedReopened\":true,\"result\":\""
        <<(heights.size()==1&&std::abs(heights.front()-255.)<1e-5?"verified-reopened-glyph-height":"pending")<<"\"}";
  productRecordRuleEvidence(job,"TL04",height.str());
 }
 if(sourceOpenings&&views){
  const auto mainSides=productConfirmedEntranceSides(job);
  std::map<std::string,const GenericOpeningCaption*> registered;
  for(const auto& caption:captions)registered.emplace(axisHandle(caption.id),&caption);
  std::map<int,std::vector<std::pair<std::string,GenericCaptionGlyph>>> displayed;
  std::vector<std::pair<std::wstring,AcDbExtents>> externalNumbers;
  for(const auto& [handle,type]:modelTypes(db)){
   if(type!=L"AcDbText"&&type!=L"AcDbMText"&&type!=L"TDbText")continue;
   const auto entity=open<AcDbEntity>(find(db,ascii(handle)));
   if(!productPlanDimensionVisible(*entity))continue;
   std::wstring text;
   if(const auto* one=AcDbText::cast(entity.get()))text=one->textString();
   else if(const auto* multiple=AcDbMText::cast(entity.get())){AcString plain;if(multiple->text(plain)==Acad::eOk)text=plain.kACharPtr();}
   else{std::vector<std::wstring> leaves;productOpeningTexts(*entity,leaves);for(const auto& leaf:leaves)text+=leaf;}
   AcDbExtents bounds;
   if(!text.empty()&&entity->getGeomExtents(bounds)==Acad::eOk)externalNumbers.emplace_back(std::move(text),bounds);
  }
  for(const auto& [handle,caption]:registered){
   try{
    const auto entity=open<AcDbEntity>(find(db,handle));
    if(auto glyph=productGenericCaptionGlyph(*entity,caption->number))
     displayed[caption->panel].emplace_back(handle,std::move(*glyph));
   }catch(const std::exception&){}
  }
  std::set<std::string> sourceGroups;
  std::size_t fractionalCount=0;
  std::map<std::wstring,std::pair<double,double>> observedCodes;
  for(const auto& opening:*sourceOpenings){
   const auto mid=opening.a+(opening.b-opening.a)*.5;
   const auto view=bcpproduct::nearestGenericView(*views,{mid.x,mid.y});
   const auto panelRole=job.sourceInventory.items.find({view+1,"panel",""});
   if(panelRole!=job.sourceInventory.items.end()&&panelRole->second.basis!="floor"&&panelRole->second.basis!="unnamed"){
    productRecordRuleEvidence(job,"O01","{\"panel\":"+std::to_string(view+1)+",\"object\":\""+opening.handle+
     "\",\"result\":\"not-applicable-outside-main-plan-scope\",\"basis\":"+
     jsonUtf8(bcpproduct::decodeMessage(panelRole->second.basis))+"}");
    continue;
   }
   const auto planned=registered.find(opening.handle);
   const auto* caption=planned==registered.end()?nullptr:planned->second;
   auto host=productGenericHost(opening,*input,(*views)[view]);
   const auto main=mainSides.find(opening.handle);
   if(!host&&main!=mainSides.end())host=productConfirmedSplitOpeningHost(opening,*input,(*views)[view],main->second,unitMM);
   bool retained=false,business=false,semanticVoid=false,pairedInteriorTracks=false;
   bool groupSegmentsVisible=false,groupSnapsRetained=false;
   std::size_t groupDoorLeaves=0,groupWindowLeaves=0;
   std::wstring actualNumber;
   std::optional<GenericCaptionGlyph> glyph;
   try{
    const auto id=find(db,opening.handle);
    const auto entity=open<AcDbEntity>(id);
    const auto grips=chainGrips(id);
    const auto row=fields.find(opening.handle);
    const std::wstring type=entity->isA()->name();
    const auto equal=[&](const auto& point,const auto& expected){return std::hypot(point.x-expected.x,point.y-expected.y)*unitMM<.01;};
    retained=!entity->isAProxy()&&type==(opening.grouped?L"TDbGroupOpening":L"TDbOpening")&&grips.size()>=3&&
     ((equal(grips[1],opening.a)&&equal(grips[2],opening.b))||(equal(grips[1],opening.b)&&equal(grips[2],opening.a)));
    if(row!=fields.end()){
     const auto& p=row->second;
     if(p.count(L"门窗编号"))actualNumber=p.at(L"门窗编号");
     const auto numeric=[&](const wchar_t* key,double expected){return p.count(key)&&
       std::abs(bcpproduct::propertyNumber(p,key)-expected)*unitMM<.01;};
     business=numeric(L"宽度",opening.width)&&numeric(L"高度",opening.height)&&
      numeric(L"离地高度",opening.sill)&&p.count(L"门窗类型")&&p.at(L"门窗类型")==opening.kind;
    }
    if(caption)glyph=productGenericCaptionGlyph(*entity,caption->number);
    if(opening.kind==L"墙洞"&&opening.sill>0&&actualNumber.empty()){
     GenericOpeningInk ink;
     productGenericOpeningInk(*entity,ink);
     semanticVoid=productNativeRaisedWallVoid(opening,ink);
    }
    if(!host&&opening.kind==L"门"){
     GenericOpeningInk ink;
     productGenericOpeningInk(*entity,ink);
     host=productPairedInteriorTrackOpening(opening,ink,*input,(*views)[view],unitMM);
     pairedInteriorTracks=host.has_value();
    }
    if(opening.grouped){
     sourceGroups.insert(opening.handle);
     AcGePoint3dArray endpoints;AcDbIntArray ids;
     if(!grips.empty()&&entity->getOsnapPoints(AcDb::kOsModeEnd,0,grips[0],grips[0],AcGeMatrix3d::kIdentity,endpoints,ids)==Acad::eOk){
      auto reopen=opening;
      reopen.groupSnapEndpoints.clear();
      for(int i=0;i<endpoints.length();++i)reopen.groupSnapEndpoints.push_back({endpoints[i].x,endpoints[i].y});
      const auto hostAgain=productGenericHost(reopen,*input,(*views)[view]);
      groupSnapsRetained=host&&hostAgain&&host->wall==hostAgain->wall&&host->beforeJamb&&hostAgain->beforeJamb&&
       host->afterJamb&&hostAgain->afterJamb&&std::abs(*host->beforeJamb-*hostAgain->beforeJamb)*unitMM<.01&&
       std::abs(*host->afterJamb-*hostAgain->afterJamb)*unitMM<.01;
     }
     TemplateParts parts(entity.get());
     if(entity->explode(parts.values)==Acad::eOk)for(int i=0;i<parts.values.length();++i){
      const auto* child=AcDbEntity::cast(static_cast<AcRxObject*>(parts.values[i]));
      if(!child||std::wstring_view(child->isA()->name())!=L"TDbOpening")continue;
      GenericOpeningInk ink;productGenericOpeningInk(*child,ink);
      if(ink.swing)++groupDoorLeaves;
      else if(host&&host->beforeJamb&&host->afterJamb&&productGenericSlidingTracks(ink,*host,*host->beforeJamb,
        *host->afterJamb,input->walls[host->wall].total))++groupWindowLeaves;
     }
     groupSegmentsVisible=groupDoorLeaves>0&&groupWindowLeaves>0;
    }
   }catch(const std::exception&){}
   const bool omitted=confirmations.bathroomDoors.count(opening.handle)&&(!host||host->side<0)&&
    !opening.window&&!opening.grouped&&std::abs(opening.sill)*unitMM<=.01&&
    (opening.kind==L"门"||opening.kind==L"墙洞")&&retained&&business&&
    fields.count(opening.handle)&&fields.at(opening.handle).count(L"门窗编号")&&actualNumber.empty();
   const bool unnumberedVoid=(semanticVoid||productConfirmedRaisedWallOpening(opening,confirmations,unitMM))&&retained&&business&&
    actualNumber.empty()&&fields.count(opening.handle)&&fields.at(opening.handle).count(L"门窗编号");
   const char* omission=unnumberedVoid?"not-applicable-native-raised-wall-void":omitted?"not-applicable-D14":nullptr;
   const bool nativeNumber=caption&&glyph&&caption->number==actualNumber;
   const bool code=nativeNumber&&host&&business&&!caption->ruleNumber.empty()&&actualNumber==caption->ruleNumber;
   const bool collision=glyph&&std::any_of(displayed[static_cast<int>(view)].begin(),displayed[static_cast<int>(view)].end(),
    [&](const auto& other){return other.first!=opening.handle&&productGenericBoxesOverlap(glyph->box,other.second.box,0.);});
   const bool duplicate=glyph&&std::any_of(externalNumbers.begin(),externalNumbers.end(),[&](const auto& text){
    return actualNumber==text.first&&productGenericBoxesOverlap(glyph->box,text.second,200./unitMM);
   });
   const auto normal=[&](const auto& p){return caption&&caption->vertical?p.x:p.y;};
   const bool outward=caption&&glyph&&host&&host->side>=0&&
    (normal(productGenericGlyphCentre(*glyph))-host->normal)*(host->side%2?1.:-1.)>0;
   const bool centered=caption&&glyph&&std::abs(caption->vertical?productGenericGlyphCentre(*glyph).y-caption->desired.y:
     productGenericGlyphCentre(*glyph).x-caption->desired.x)<1e-4;
   const bool aligned=caption&&glyph&&std::abs(caption->vertical?std::cos(glyph->angle):std::sin(glyph->angle))<1e-4;
   const bool flipped=caption&&glyph&&(normal(productGenericGlyphCentre(*glyph))-normal(mid))*(normal(caption->desired)-normal(mid))<0;
   const bool preferredSide=caption&&glyph&&std::abs(normal(caption->desired)-normal(mid))>1e-5&&!flipped;
   std::ostringstream row;
   row.imbue(std::locale::classic());
   row<<std::setprecision(17)<<"{\"panel\":"<<view+1<<",\"object\":\""<<opening.handle
      <<"\",\"sourceType\":"<<jsonUtf8(opening.kind)<<",\"sourceNumber\":"<<jsonUtf8(opening.existing)
      <<",\"widthMM\":"<<opening.width*unitMM<<",\"heightMM\":"<<opening.height*unitMM
      <<",\"sillMM\":"<<opening.sill*unitMM<<",\"actualNumber\":"<<jsonUtf8(actualNumber)
      <<",\"ruleNumber\":"<<jsonUtf8(caption?caption->ruleNumber:std::wstring{})
      <<",\"host\":"<<(host?jsonUtf8(bcpproduct::decodeMessage(input->walls[host->wall].wall.handle)):"null")
      <<",\"endpointsRetained\":"<<(retained?"true":"false")<<",\"parametersRetained\":"<<(business?"true":"false")
      <<",\"collision\":"<<(collision?"true":"false")<<",\"nearbyDuplicateRootNumber\":"<<(duplicate?"true":"false")<<",\"glyphBox\":"
      <<(glyph?wholePoints({glyph->box.minPoint(),glyph->box.maxPoint()}):"null")<<",\"savedReopened\":true";
   const auto evidence=[&](const char* rule,const char* result){
    productRecordRuleEvidence(job,rule,row.str()+",\"result\":\""+result+"\"}");
   };
   if(opening.grouped){
    const auto combination=row.str()+",\"sourceNativeDoorLeaves\":"+std::to_string(groupDoorLeaves)+
     ",\"sourceNativeWindowLeafCandidates\":"+std::to_string(groupWindowLeaves)+
     ",\"reopenedOverallJambsAndHostVerified\":"+(groupSnapsRetained?"true":"false");
    const auto groupEvidence=[&](const char* rule,const char* result){
     productRecordRuleEvidence(job,rule,combination+",\"result\":\""+result+"\"}");
    };
    groupEvidence("NC01",retained&&groupSegmentsVisible&&groupSnapsRetained?
     "pending-confirmed-segment-category":"pending-group-members-or-actual-host");
    groupEvidence("NC02",retained&&groupSnapsRetained?"verified-native-group-preserved":"pending-native-group-host-or-type");
    groupEvidence("NC03","pending-public-group-segment-dimensions-and-sill");
    groupEvidence("NC04","pending-confirmed-group-number-prefix-and-segments");
    groupEvidence("C09","pending-confirmed-walkable-door-threshold-and-window-segment");
    groupEvidence("C10","pending-door-threshold-and-window-sill-datum-and-detail");
   }
   const char* codeResult=omission?omission:code?"verified-current-number-code":"pending-classification-or-code";
   evidence("O01",codeResult);
   evidence("O02",omission?omission:code&&caption->ruleNumber.size()>=5&&
    caption->ruleNumber.substr(caption->ruleNumber.find_first_of(L"0123456789")).size()==4?"verified-hundred-code":
    code?"not-applicable-fractional-code":"pending-classification-or-code");
   if(std::abs(std::round(opening.width*unitMM/100)*100-opening.width*unitMM)>1e-5||
      std::abs(std::round(opening.height*unitMM/100)*100-opening.height*unitMM)>1e-5){
    ++fractionalCount;
    evidence("O03",omission?omission:code?"verified-confirmed-fractional-code":"pending-fractional-code");
   }
   evidence("O04",pairedInteriorTracks&&retained&&business?"verified-interior-sliding-by-paired-jambs-and-tracks":codeResult);
   evidence("O05",omission?omission:code&&!opening.grouped?"verified-independent-native-opening":
    "pending-original-number-category-or-combination");
   evidence("O07",unnumberedVoid?"verified-confirmed-raised-wall-void-number-omission":
    omitted?"verified-D14-number-omission":nativeNumber&&retained&&!duplicate?
    "verified-native-number-no-nearby-duplicate-root-text":"pending-native-number-or-duplicate");
   bool sharedCode=true;
   if(nativeNumber){
    const auto dimensions=std::make_pair(caption->width*unitMM,caption->height*unitMM);
    const auto [known,inserted]=observedCodes.emplace(actualNumber,dimensions);
    sharedCode=inserted||(std::abs(known->second.first-dimensions.first)<.01&&
                         std::abs(known->second.second-dimensions.second)<.01);
   }
   evidence("Q04",unnumberedVoid?"verified-confirmed-unnumbered-raised-wall-void":
    omitted?"verified-D14-native-number-omission":code&&retained&&centered&&aligned&&sharedCode&&!duplicate?
     "verified-opening-type-measurement-identity-and-display":"pending-opening-code-type-or-display");
   const bool checked=code&&retained&&centered&&aligned&&sharedCode&&!duplicate&&!collision;
   const auto fractionalAssembly=std::find_if(job.detailPending.begin(),job.detailPending.end(),[&](const auto& item){
    return item[0]==u8"非整百组合洞口编号"&&item[1]==opening.handle;
   });
   std::string reason;
   if(unnumberedVoid)reason=u8"原生带台高素墙洞无需门窗编号，端点、尺寸及台高重开核验通过";
   else if(omitted)reason=u8"符合 D14 的省标条件";
   else if(!retained||!business)reason=u8"重开未能核对原生洞口类型、端点或公开宽高及离地值";
   else if(fractionalAssembly!=job.detailPending.end())reason=(*fractionalAssembly)[2];
   else if(pairedInteriorTracks&&!caption)reason=u8"两侧真实墙面和原生轨道核实为室内推拉门 M 类，现行编号规则无此非整百洞口尾码";
   else if(!host)reason=u8"洞口未与可靠的原生宿主墙定位关联；需核构造及实际洞口编码";
   else if(!caption&&opening.kind==L"墙洞"&&opening.sill*unitMM>.01)
    reason=u8"有实测离地台高的原生墙洞；需核用途及是否应设自身号";
   else if(!caption&&opening.kind==L"门"&&host->side>=0){
    const auto joined=std::any_of(sourceOpenings->begin(),sourceOpenings->end(),[&](const auto& adjacent){
     if(adjacent.handle==opening.handle||adjacent.kind!=opening.kind||adjacent.grouped||
        std::abs(adjacent.height-opening.height)*unitMM>=.01||std::abs(adjacent.sill-opening.sill)*unitMM>=.01)
      return false;
     const auto across=[&](const auto& p){return host->vertical?p.x:p.y;};
     if(std::abs(across(adjacent.a)-across(mid))*unitMM>=.01||
        std::abs(across(adjacent.b)-across(mid))*unitMM>=.01)return false;
     const auto coincides=[&](const auto& a,const auto& b){return a.distanceTo(b)*unitMM<.01;};
     return coincides(opening.a,adjacent.a)||coincides(opening.a,adjacent.b)||
            coincides(opening.b,adjacent.a)||coincides(opening.b,adjacent.b);
    });
    reason=joined?u8"同一外墙相接的多个门段；需核整樘宽高、分段类别及组合编号":
                  u8"已核外墙原生洞口宽高，需确认外门类别和编号前缀";
   }else if(!code)reason=u8"原生洞口类别、实际宽高编码或自身编号尚需核实";
   else if(!centered||!aligned)reason=u8"重开后的自身号未通过洞口居中或方向校验";
   else if(!sharedCode)reason=u8"重复使用同一自身编号的洞口宽高不一致";
   else if(duplicate)reason=u8"附近存在与原生自身号相同的重复文字";
   else if(collision)reason=u8"原生自身号与邻近门窗编号相交";
   productCheckPlanSource(job,view+1,"opening-number",opening.handle,
     omitted||unnumberedVoid?bcpproduct::PlanCheck::omitted:checked?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::pending,
     {opening.handle},reason);
   if(!opening.window&&!opening.grouped){
    evidence("L01",omission?omission:centered&&aligned?"verified-caption-center-and-angle":"pending-caption");
    evidence("L02",omission?omission:preferredSide?"verified-preferred-caption-side":
     flipped?"pending-flipped-side-geometry-audit":"pending-caption-side");
   }
   if(opening.window||opening.grouped)
    evidence("L03",outward&&centered&&aligned?"verified-exterior-caption-side":"pending-courtyard-combination-or-side");
   evidence("L07",omission?omission:collision?"pending-caption-collision":glyph&&centered&&aligned?
    "verified-native-caption-alignment-and-other-caption-clearance":"pending-caption-placement");
  }
  if(sourceTypes){
   if(!fractionalCount)productRecordRuleEvidence(job,"O03","{\"scope\":\"source-native-opening-inventory\",\"result\":\"not-applicable-no-fractional-width-or-height\",\"openingCount\":"+std::to_string(sourceOpenings->size())+"}");
   std::size_t groupCount=0;
   for(const auto& [handle,type]:*sourceTypes)if(type==L"TDbGroupOpening"){
    ++groupCount;
    if(sourceGroups.count(ascii(handle)))continue;
    const auto evidence="{\"object\":\""+ascii(handle)+"\",\"result\":\"pending-unreadable-source-native-group\"}";
    for(const auto* rule:{"NC01","NC02","NC03","NC04","C09","C10"})productRecordRuleEvidence(job,rule,evidence);
   }
   std::size_t unresolvedAdjacent=0;
   for(std::size_t first=0;first<sourceOpenings->size();++first)for(std::size_t second=first+1;second<sourceOpenings->size();++second){
    const auto& a=(*sourceOpenings)[first];const auto& b=(*sourceOpenings)[second];
    if(a.grouped||b.grouped||a.window==b.window||a.kind==L"墙洞"||b.kind==L"墙洞")continue;
    const auto close=[&](const auto& p,const auto& q){return std::hypot(p.x-q.x,p.y-q.y)*unitMM<.01;};
    if(!close(a.a,b.a)&&!close(a.a,b.b)&&!close(a.b,b.a)&&!close(a.b,b.b))continue;
    const auto midpoint=a.a+(a.b-a.a)*.5;
    const auto panel=bcpproduct::nearestGenericView(*views,{midpoint.x,midpoint.y});
    const auto hostA=productGenericHost(a,*input,(*views)[panel]);
    const auto hostB=productGenericHost(b,*input,(*views)[panel]);
    if(!hostA||!hostB||hostA->wall!=hostB->wall||hostA->vertical!=hostB->vertical)continue;
    ++unresolvedAdjacent;
    const auto evidence="{\"panel\":"+std::to_string(panel+1)+",\"object\":\""+a.handle+
     "\",\"other\":\""+b.handle+"\",\"result\":\"pending-adjacent-door-window-combination-basis\"}";
    for(const auto* rule:{"NC01","NC02","NC03","NC04","O05","C09","C10"})productRecordRuleEvidence(job,rule,evidence);
    job.detailPending.push_back({u8"门联窗组合依据",a.handle+"/"+b.handle,u8"同墙门窗洞端重合，需用项目门窗表核实组合及分段；保留真实源对象"});
   }
   if(!groupCount&&!unresolvedAdjacent){
    const auto evidence="{\"scope\":\"source-native-opening-inventory\",\"nativeGroups\":0,"
      "\"coincidentAdjacentDoorWindowCandidates\":0,\"result\":\"not-applicable-no-native-or-coincident-group-candidate\"}";
    for(const auto* rule:{"NC01","NC02","NC03","NC04","C09","C10"})productRecordRuleEvidence(job,rule,evidence);
   }
  }
 }
 writeNew(job.root/L"门窗编号显示层核验.json",
          "{\"verified\":"+std::to_string(verified)+",\"pending\":"+std::to_string(captions.size()-verified)+"}");
}

std::vector<std::pair<std::string,AcDbExtents>> productOpeningExteriorNoteInk(AcDbDatabase& db){
 std::vector<std::pair<std::string,AcDbExtents>> result;
 const std::set<std::wstring> outsideNames{L"阳台",L"露台",L"雨棚",L"门廊",L"坡道",L"庭院"};
 for(const auto& [handle,type]:modelTypes(db)){
  const bool arrow=type==L"TDbSymbArrow";
  if(!arrow&&type!=L"AcDbText"&&type!=L"TDbText"&&type!=L"AcDbMText")continue;
  const auto entity=open<AcDbEntity>(find(db,ascii(handle)));
  if(!productPlanDimensionVisible(*entity))continue;
  const auto collect=[&](auto&& collect,const AcDbEntity& part,int depth)->void{
   if(depth>6)return;
   std::wstring value;
   if(const auto* text=AcDbText::cast(&part))value=text->textString();
   else if(const auto* text=AcDbMText::cast(&part)){
    AcString plain;
    if(text->text(plain)!=Acad::eOk)return;
    value=plain.kACharPtr();
   }else{
    if(std::wstring(part.isA()->name()).rfind(L"TDb",0)!=0)return;
    TemplateParts children(&part);
    if(part.explode(children.values)!=Acad::eOk)return;
    for(int n=0;n<children.values.length();++n)
     if(const auto* child=AcDbEntity::cast(static_cast<AcRxObject*>(children.values[n])))collect(collect,*child,depth+1);
    return;
   }
   AcDbExtents glyph;
   if(outsideNames.count(value)&&part.getGeomExtents(glyph)==Acad::eOk)result.emplace_back(ascii(handle),glyph);
  };
  collect(collect,*entity,0);
 }
 return result;
}

void productArrangeWindowExteriorNoteClearance(ProductJob& job,AcDbDatabase& db,
 const std::vector<GenericOpeningCaption>& captions,double paper){
 const auto notes=productOpeningExteriorNoteInk(db);
 std::map<AcDbObjectId,AcDbExtents> openingBoxes;
 for(const auto& caption:captions){
  const auto glyph=productGenericCaptionGlyph(*open<AcDbEntity>(caption.id),caption.number);
  if(glyph)openingBoxes.emplace(caption.id,glyph->box);
 }
 const double gap=.15*paper;
 for(const auto& caption:captions){
  if(!caption.windowWallDatum)continue;
  const auto found=openingBoxes.find(caption.id);
  if(found==openingBoxes.end())continue;
  const auto noteHit=[&](const AcDbExtents& box){
   return std::any_of(notes.begin(),notes.end(),[&](const auto& row){return productGenericBoxesOverlap(box,row.second,gap);});
  };
  const auto obstacles=[&](const AcDbExtents& box) {
   if(noteHit(box))return std::string("exterior-note");
   return productOpeningCaptionGeometryBlock(db,productCaptionInkSafetyBox(box,gap),true,.35*paper);
  };
  const auto initialObstacle=obstacles(found->second);
  if(initialObstacle.empty())continue;
  const auto origin=found->second.minPoint()+(found->second.maxPoint()-found->second.minPoint())*.5;
  const auto across=[&](const AcGePoint3d& p){return caption.vertical?p.x:p.y;};
  const double datum=*caption.windowWallDatum;
  const double side=across(origin)>datum?1.:-1.;
  const auto acceptable=[&](const AcDbExtents& box){
   const auto nearestEdge=side>0?box.minPoint():box.maxPoint();
   return side*(across(nearestEdge)-datum)>=caption.windowWallThickness/2+gap&&obstacles(box).empty()&&
    std::none_of(openingBoxes.begin(),openingBoxes.end(),[&](const auto& neighbor){
     return neighbor.first!=caption.id&&productGenericBoxesOverlap(box,neighbor.second,gap);
    });
  };
  std::optional<double> displacement;
  for(const double mm:{.5,-.5,1.,-1.,1.5,-1.5,2.,-2.,2.5,-2.5,3.,-3.,4.,-4.,5.,6.,7.,8.,9.,9.5,10.}){
   const double shift=side*mm*paper;
   const AcGeVector3d delta(caption.vertical?shift:0,caption.vertical?0:shift,0);
   const AcDbExtents preview(found->second.minPoint()+delta,found->second.maxPoint()+delta);
   if(!acceptable(preview))continue;
   const auto actual=productGenericMoveCaption(caption.id,caption.number,origin+delta);
   if(actual&&acceptable(actual->box)){found->second=actual->box;displacement=shift;break;}
   productGenericMoveCaption(caption.id,caption.number,origin);
  }
  std::optional<double> widthFactor;
  if(!displacement&&initialObstacle!="exterior-note"){
   // At a return, the available width can be set by perpendicular wall ink.
   // Retain the centre, outside face and 3 mm height of the native number.
   const auto prior=productGenericCaptionGlyph(*open<AcDbEntity>(caption.id),caption.number);
   if(prior&&!prior->layer.isNull()){
    widthFactor=productTryCompactCaption(db,caption,*prior,prior->layer,true);
    if(widthFactor){
     const auto compact=productGenericCaptionGlyph(*open<AcDbEntity>(caption.id),caption.number);
     if(compact)found->second=compact->box;
    }
   }
  }
  if(!displacement&&!widthFactor&&initialObstacle=="exterior-note")
   job.detailPending.push_back({u8"窗号与室外说明",axisHandle(caption.id),u8"同一外侧未找到同时避让构件、尺寸和室外说明的位置"});
  if(displacement||widthFactor){
   std::ostringstream row;
   row<<std::setprecision(17)<<"{\"panel\":"<<caption.panel+1<<",\"object\":\""<<axisHandle(caption.id)
      <<"\",\"initialObstacle\":\""<<initialObstacle<<"\",\"sameOutsideFace\":true,\"normalShift\":"
      <<(displacement?*displacement:0.)<<",\"widthFactor\":";
   if(widthFactor)row<<*widthFactor;else row<<"null";
   row<<",\"result\":\"verified-window-caption-same-face-placement\"}";
   productRecordRuleEvidence(job,"L05",row.str());
  }
 }
}

void productVerifyOpeningInkClearance(ProductJob& job,AcDbDatabase& db,
 const std::vector<GenericOpeningCaption>& captions){
 if(!job.unifiedPlan)return;
 Working work(&db);
 struct Label {const GenericOpeningCaption* caption;std::string handle;std::optional<GenericCaptionGlyph> glyph;};
 std::vector<Label> labels;
 for(const auto& caption:captions){
  const auto handle=axisHandle(caption.id);
  const auto entity=open<AcDbEntity>(find(db,handle));
  labels.push_back({&caption,handle,productGenericCaptionGlyph(*entity,caption.number)});
 }
 const auto exteriorNotes=productOpeningExteriorNoteInk(db);
 struct Obstacle {std::string handle,category;AcDbExtents bounds;GenericOpeningInk ink;std::vector<AcDbExtents> text;};
 std::vector<Obstacle> obstacles;
 for(const auto& [handle,type]:modelTypes(db)){
  const auto key=ascii(handle);
  const auto entity=open<AcDbEntity>(find(db,key));
  if(!productPlanDimensionVisible(*entity))continue;
  const auto layer=symbolName(entity->layerId());
  const char* category=nullptr;
  if(type==L"TDbDimension2"||AcDbDimension::cast(entity.get()))category="dimension";
  else if(type==L"TDbOpening"||type==L"TDbGroupOpening")category="opening";
  else if(type==L"TDbWall"||type==L"TDbColumn")category="wall-or-column";
  else if(layer==L"STAIR"||layer==L"A-STAIR")category="stair-or-platform";
  else if(type==L"AcDbBlockReference"||productFurnitureLayerName(layer)||layer==L"家具")category="furniture-or-equipment";
  if(!category)continue;
  Obstacle obstacle{key,category};
  if(entity->getGeomExtents(obstacle.bounds)!=Acad::eOk||std::none_of(labels.begin(),labels.end(),[&](const auto& label){
   return label.glyph&&productGenericBoxesOverlap(obstacle.bounds,label.glyph->box,0.);
  }))continue;
  if(obstacle.category=="dimension")productOpeningDimensionDisplayInk(*entity,obstacle.ink);
  else productGenericOpeningInk(*entity,obstacle.ink);
  if(obstacle.category=="dimension")obstacle.text=productOpeningDimensionTextInk(db,*entity);
  obstacles.push_back(std::move(obstacle));
 }
 for(const auto& label:labels){
  std::ostringstream row;
  row<<"{\"panel\":"<<label.caption->panel+1<<",\"object\":\""<<label.handle
     <<"\",\"glyphBox\":"<<(label.glyph?wholePoints({label.glyph->box.minPoint(),label.glyph->box.maxPoint()}):"null")
     <<",\"collisions\":[";
  bool first=true;
  if(label.glyph)for(const auto& obstacle:obstacles){
   const auto& box=label.glyph->box;
   if(!productGenericBoxesOverlap(box,obstacle.bounds,0.))continue;
   const auto overlap=[&](const auto& bounds){return productGenericBoxesOverlap(box,bounds,0.);};
   const auto lineHits=[&](const auto& line){return productGenericSegmentHitsBox(line.first,line.second,box,-1e-5);};
   const bool strokes=std::any_of(obstacle.ink.straight.begin(),obstacle.ink.straight.end(),lineHits)||
    std::any_of(obstacle.ink.curved.begin(),obstacle.ink.curved.end(),lineHits);
   const bool text=std::any_of(obstacle.text.begin(),obstacle.text.end(),overlap);
   const bool possibleArc=obstacle.ink.curved.empty()&&std::any_of(obstacle.ink.arcs.begin(),obstacle.ink.arcs.end(),overlap);
   const bool unreadable=obstacle.ink.straight.empty()&&obstacle.ink.arcs.empty()&&obstacle.text.empty();
   if(!strokes&&!text&&!possibleArc&&!unreadable)continue;
   if(!first)row<<',';
   first=false;
   row<<"{\"object\":\""<<obstacle.handle<<"\",\"category\":\""<<obstacle.category<<"\",\"scope\":\""
      <<(strokes||text?"actual-ink":possibleArc?"arc-envelope-review":"unreadable-visible-geometry")<<"\"}";
  }
  if(label.glyph&&label.caption->windowWallDatum)
   for(const auto& [handle,box]:exteriorNotes)if(productGenericBoxesOverlap(label.glyph->box,box,0.)){
    if(!first)row<<',';
    first=false;
    row<<"{\"object\":\""<<handle<<"\",\"category\":\"outdoor-name-or-ramp-caption\",\"scope\":\"actual-glyph-box\"}";
   }
  if(label.glyph&&!label.glyph->style.isNull()&&symbolName(label.glyph->style).find(L"_L05_")!=std::wstring::npos){
   const double paper=job.unifiedScale/job.unifiedUnitMM;
   const bool entrance=label.caption->number.rfind(L"FMD",0)==0;
   const double allowance=(entrance?.35:.12)*paper;
   const auto padded=productCaptionInkSafetyBox(label.glyph->box,allowance);
   const auto collision=productOpeningCaptionGeometryBlock(db,padded,!entrance);
   if(!collision.empty()){
    if(!first)row<<',';
    first=false;
    row<<"{\"object\":\""<<collision<<"\",\"category\":\"paper-lineweight-clearance\",\"scope\":\"compact-glyph-margin\"}";
   }
  }
  const bool verified=label.glyph&&first;
  row<<"],\"savedReopened\":true,\"result\":\""<<(verified?"verified-caption-clearance":"pending-caption-clearance")<<"\"}";
  productRecordRuleEvidence(job,"L05",row.str());
  const auto panel=label.caption->panel;
  const auto sourcePanel=panel>=0&&static_cast<std::size_t>(panel)<job.layoutSourcePanels.size()?
   job.layoutSourcePanels[panel]:static_cast<std::size_t>(panel+1);
  job.drawingChecks.record(sourcePanel,"opening-caption-clearance",label.handle,{"L05"},bcpproduct::PlanReleaseGate::mainPlan,
   verified?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::failed,u8"门窗自身号对全部实际可打印线迹的净空",
   verified?"":u8"门窗编号与实际构件或尺寸有相交，见L05逐对象碰撞证据");
  if(!verified)job.detailPending.push_back({u8"门窗编号图面净空",label.handle,
   u8"重开后有尺寸、门扇开启、墙柱或家具等实际相交或未能辨认的近邻构件，见 L05 碰撞句柄复核"});
 }
}

GenericOpeningCaption productGenericLabelOpening(ProductJob& job,AcDbDatabase& db,
 const GenericOpeningWork& item,const std::wstring& number,double widthCentre,double wallThickness) {
 const auto& o=*item.opening;
 const auto& h=item.host;
 const auto id=find(db,o.handle);
 const auto before=chainGrips(id);
 {
  WholeSelection selected(id);
  require(acedCommandS(RTSTR,L"TChWinLab",RTPICKS,selected.value,RTSTR,L"",RTSTR,number.c_str(),RTNONE)==RTNORM,
          "write current native opening number");
 }
 const auto now=chainGrips(id);
 require(now.size()>=before.size() && now.size()>3,"native opening label grip");
 const std::size_t physical=std::min(before.size(),now.size()-1);
 const auto checkGeometry=[&](const auto& points) {
  require(points.size()==now.size(),"numbering changed actual opening grip count");
  for (std::size_t k=0;k<physical;++k)
   require(before[k].distanceTo(points[k])<1e-4,"numbering changed actual opening geometry");
 };
 checkGeometry(now);
 if(job.unifiedPlan)++job.planPublicCommands[L"TChWinLab"];
 const auto glyph=[&](){
  auto e=open<AcDbEntity>(id);
  return productGenericCaptionGlyph(*e,number);
 }();
 const auto perpendicular=[&](const AcGePoint3d& p){return h.vertical?p.x:p.y;};
 int side=0;
 // Doors follow the actual swing, including doors on an exterior wall.
 // Only windows/group openings use the facade side ahead of swing evidence.
 if ((o.window||o.grouped) && h.side>=0) side=(h.side==0||h.side==2)?-1:1;
 if (!side && !item.ink.arcs.empty()) {
  double negative=0,positive=0;
  for (const auto& box:item.ink.arcs) {
   negative=std::max(negative,h.normal-perpendicular(box.minPoint()));
   positive=std::max(positive,perpendicular(box.maxPoint())-h.normal);
  }
  if (std::abs(positive-negative)>1e-5) side=positive>negative?-1:1;
 }
 if (!side && h.side>=0) side=(h.side==0||h.side==2)?-1:1;
 const auto anchor=glyph?productGenericGlyphCentre(*glyph):now.back();
 if (!side && std::abs(perpendicular(anchor)-h.normal)>1e-5) side=perpendicular(anchor)>h.normal?1:-1;
 const double across=side?h.normal+side*(wallThickness/2+350):perpendicular(anchor);
 const auto desired=h.vertical?AcGePoint3d(across,widthCentre,anchor.z):AcGePoint3d(widthCentre,across,anchor.z);
 productGenericMoveCaption(id,number,desired);
 checkGeometry(chainGrips(id));
 {
  auto e=editObject<AcDbEntity>(id);
  productStatus(e->setLayer(productPlanLayer(db,L"WINDOW",2)),"native opening layer");
  productStatus(e->setColorIndex(256),"native opening color");
 }
 ++job.captions;
 job.genericCaptionIds.push_back(id);
 const double openingDatum=(perpendicular(o.a)+perpendicular(o.b))/2;
 const bool interiorCode=number.size()>1 && number[0]==L'M' && number[1]>=L'0' && number[1]<=L'9';
 const bool interiorDoor=h.side<0 && !o.window && !o.grouped &&
                         (item.sliding || item.ink.swing || (interiorCode && productGenericOpeningKind(o,L"门")));
 if (interiorDoor && !side)
  job.detailPending.push_back({u8"室内门编号净距",o.handle,u8"门号侧向缺少依据，保留原生位置并核对200毫米净距"});
 if (interiorDoor && item.sliding && number.rfind(L"LM",0)==0)
  job.detailPending.push_back({u8"室内推拉门编号",o.handle,u8"保留源编号，按室内推拉门核对M类编码"});
 GenericOpeningCaption caption{id,number,desired,0.,0.,0,h.vertical,nullptr,
         interiorDoor && side ? std::optional<double>(openingDatum) : std::nullopt,item.ink.swing};
 if(h.side<0&&o.kind==L"墙洞"&&o.existing.empty()&&!item.ink.swing)caption.unnumberedVoidWallDatum=h.normal;
 if(o.window&&h.side>=0){caption.windowWallDatum=h.normal;caption.windowWallThickness=wallThickness;}
 return caption;
}
struct ProductDoorJambFace {
 double station{},gap{};
 std::string wall;
 bool lower{};
};

std::optional<ProductDoorJambFace> productDoorJambFace(const GenericNativeInput& input,
 const bcpproduct::GenericView& view,const GenericNativeOpening& door,const GenericOpeningHost& host,double tolerance){
 const auto station=[&](const auto& p){return host.vertical?p.y:p.x;};
 const auto [lo,hi]=std::minmax({station(door.a),station(door.b)});
 std::vector<ProductDoorJambFace> candidates;
 for(const auto index:view.walls){
  const auto& wall=input.walls[index];
  if(wall.low||!wall.orthogonal)continue;
  const bool vertical=std::abs(wall.wall.start.x-wall.wall.end.x)<=tolerance;
  if(vertical==host.vertical)continue;
  const auto rect=approvedRect(wall.wall);
  const int normal=host.vertical?0:1,along=1-normal;
  if(host.normal<rect[normal]-tolerance||host.normal>rect[normal+2]+tolerance)continue;
  if(rect[along+2]<=lo+tolerance&&rect[along+2]>=host.lower-tolerance)
   candidates.push_back({rect[along+2],lo-rect[along+2],wall.wall.handle,true});
  if(rect[along]>=hi-tolerance&&rect[along]<=host.upper+tolerance)
   candidates.push_back({rect[along],rect[along]-hi,wall.wall.handle,false});
 }
 std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.gap<b.gap;});
 if(candidates.empty())return {};
 const auto face=candidates.front();
 if(std::any_of(candidates.begin()+1,candidates.end(),[&](const auto& other){
  return other.gap-face.gap<=tolerance&&(other.lower!=face.lower||std::abs(other.station-face.station)>tolerance);
 }))return {};
 const auto edge=face.lower?lo:hi;
 for(const auto& other:input.openings){
  if(other.handle==door.handle)continue;
  const auto otherHost=productGenericHost(other,input,view);
  if(!otherHost||otherHost->wall!=host.wall)continue;
  const auto [a,b]=std::minmax({station(other.a),station(other.b)});
  if(a<std::max(edge,face.station)-tolerance&&b>std::min(edge,face.station)+tolerance)return {};
 }
 return face;
}

constexpr double productMaxCandidateJambMM=450.;

void productNormalizeDoorJambs(ProductJob& job,AcDbDatabase& db,GenericNativeInput& input,
 const std::vector<bcpproduct::GenericView>& views,const std::map<std::wstring,std::wstring>& original,
 const std::vector<bool>& eligible){
 const auto unit=job.unifiedUnitMM,tolerance=.01/unit;
 const auto entrances=productConfirmedEntranceSides(job);
 const auto confirmations=productPlanDetailConfirmations(job);
 struct Witness{std::string handle;AcGePoint3d point;bool vertical;};
 std::vector<Witness> witnesses;
 struct Obstruction{std::string handle;AcDbExtents box;};
 std::vector<Obstruction> obstructions;
 for(const auto& [raw,type]:original){
  const auto handle=ascii(raw);
  const auto id=find(db,handle);
  const auto entity=open<AcDbEntity>(id);
  if(!productPlanDimensionVisible(*entity))continue;
  const auto add=[&](const AcGePoint3d& a,const AcGePoint3d& b){
   if(a.distanceTo(b)<=tolerance)return;
   const bool vertical=std::abs(a.x-b.x)<=tolerance;
   if(!vertical&&std::abs(a.y-b.y)>tolerance)return;
   witnesses.push_back({handle,a,vertical});
   witnesses.push_back({handle,b,vertical});
  };
  if(const auto* dim=AcDbAlignedDimension::cast(entity.get()))add(dim->xLine1Point(),dim->xLine2Point());
  else if(const auto* dim=AcDbRotatedDimension::cast(entity.get()))add(dim->xLine1Point(),dim->xLine2Point());
  else if(type==L"TDbDimension2"){
   const auto values=chainValues(id);
   const auto grips=chainGrips(id);
   if(grips.size()==2*values.size()+6)
    for(std::size_t k=0;k<values.size();++k)add(grips[k+2],grips[k+3]);
  }else if(type==L"AcDbBlockReference"||type==L"TDbFurniture"||type==L"TDbColumn"){
   AcDbExtents box;
   if(entity->getGeomExtents(box)==Acad::eOk)obstructions.push_back({handle,box});
  }
 }
 for(const auto& wall:input.walls){
  if(!wall.orthogonal)continue;
  const auto r=approvedRect(wall.wall);
  obstructions.push_back({wall.wall.handle,AcDbExtents({r[0],r[1],0},{r[2],r[3],0})});
 }
 for(auto& door:input.openings){
  if(door.window||door.grouped||std::abs(door.sill)>tolerance||entrances.count(door.handle)||
     confirmations.bathroomDoors.count(door.handle)||productGenericOpeningKind(door,L"推拉"))continue;
  const auto middle=door.a+(door.b-door.a)/2;
  const auto panel=bcpproduct::nearestGenericView(views,{middle.x,middle.y});
  if(!eligible[panel])continue;
  const auto& view=views[panel];
  const auto host=productGenericHost(door,input,view);
  if(!host||host->side>=0)continue;
  const auto& wall=input.walls[host->wall];
  const double thickness=wall.total*unit;
  const double standard=std::abs(thickness-200)<=.01?100:std::abs(thickness-240)<=.01?120:0;
  if(standard==0)continue;
  const auto id=find(db,door.handle);
  GenericOpeningInk ink;
  {const auto entity=open<AcDbEntity>(id);productGenericOpeningInk(*entity,ink);}
  if(!ink.swing)continue;
  const auto face=productDoorJambFace(input,view,door,*host,tolerance);
  auto& receipt=job.doorJambChanges[door.handle];
  receipt.host=wall.wall.handle;receipt.panel=panel+1;receipt.standardMM=standard;
  if(!face){
   receipt.reason=u8"邻接墙面缺失、等距或有其他洞口分隔，无法唯一建立净垛";
   continue;
  }
  receipt.adjacent=face->wall;receipt.priorMM=face->gap*unit;
  if(receipt.priorMM>productMaxCandidateJambMM){
   receipt.reason=u8"最近实测墙面超出局部门垛范围，保留必要洞口宽及定位";
   continue;
  }
  const auto delta=(standard/unit-face->gap)*(face->lower?1:-1);
  if(std::abs(delta)<=tolerance)continue;
  const auto station=[&](const auto& p){return host->vertical?p.y:p.x;};
  const auto normal=[&](const auto& p){return host->vertical?p.x:p.y;};
  const auto [lo,hi]=std::minmax({station(door.a),station(door.b)});
  if(lo+delta<host->lower-tolerance||hi+delta>host->upper+tolerance){
   receipt.reason=u8"目标洞口超出原生宿主墙有效段";
   continue;
  }
  const auto shared=std::find_if(witnesses.begin(),witnesses.end(),[&](const auto& witness){
   return witness.vertical==host->vertical&&std::abs(normal(witness.point)-host->normal)<=wall.total/2+tolerance&&
    (std::abs(station(witness.point)-lo)<=tolerance||std::abs(station(witness.point)-hi)<=tolerance);
  });
  if(shared!=witnesses.end()){
   receipt.reason=u8"原尺寸 "+shared->handle+u8" 引用原洞口边，需确认共享测点后联动调整";
   continue;
  }
  const auto offset=host->vertical?AcGeVector3d(0,delta,0):AcGeVector3d(delta,0,0);
  const bool overlapsOpening=std::any_of(input.openings.begin(),input.openings.end(),[&](const auto& other){
   if(other.handle==door.handle)return false;
   const auto h=productGenericHost(other,input,view);
   if(!h||h->wall!=host->wall)return false;
   const auto [a,b]=std::minmax({station(other.a),station(other.b)});
   return a<hi+delta-tolerance&&b>lo+delta+tolerance;
  });
  if(overlapsOpening){receipt.reason=u8"目标洞口与同墙其他洞口相交";continue;}
  const auto collision=std::find_if(obstructions.begin(),obstructions.end(),[&](const auto& obstacle){
   if(obstacle.handle==wall.wall.handle||obstacle.handle==face->wall)return false;
   const auto hits=[&](const AcGeVector3d& shift){
    for(const auto& stroke:ink.straight)
     if(productGenericSegmentHitsBox(stroke.first+shift,stroke.second+shift,obstacle.box,-tolerance))return true;
    for(const auto& arc:ink.arcs)
     if(productGenericBoxesOverlap(AcDbExtents(arc.minPoint()+shift,arc.maxPoint()+shift),obstacle.box,-tolerance))return true;
    return false;
   };
   return hits(offset)&&!hits(AcGeVector3d::kIdentity);
  });
  if(collision!=obstructions.end()){
   receipt.reason=u8"目标门扇与相邻构件 "+collision->handle+u8" 的净空待核";
   continue;
  }
  const auto before=chainGrips(id);
  {auto entity=editObject<AcDbEntity>(id);
   productStatus(entity->transformBy(AcGeMatrix3d::translation(offset)),"translate native interior hinged door");
  }
  const auto after=chainGrips(id);
  if(before.size()<3||after.size()<3||!std::equal(before.begin(),before.begin()+3,after.begin(),[&](const auto& a,const auto& b){
   return (a+offset).distanceTo(b)<=tolerance;
  })){
   {auto entity=editObject<AcDbEntity>(id);
    productStatus(entity->transformBy(AcGeMatrix3d::translation(-offset)),"restore unsuccessful native door translation");}
   require(approvedgrips::same(chainGrips(id),before),"failed native door move did not restore");
   receipt.reason=u8"天正公开变换未能保持洞口几何，已恢复源位置";
   continue;
  }
  door.a=after[1];door.b=after[2];door.a.z=door.b.z=0;
  receipt.offset=offset;
  auto nativeHost=editObject<AcDbEntity>(find(db,wall.wall.handle));
  nativeHost->recordGraphicsModified(true);
 }
}

void productGenericOpenings(ProductJob& job,AcDbDatabase& db,const GenericNativeInput& input,const std::vector<bcpproduct::GenericView>& views,std::size_t view,const ProductPlanBounds& bounds,int panel,AcDbObjectId prototype,ProductPlanLayoutConfig& layout,std::vector<GenericOpeningCaption>& captions,std::vector<std::pair<AcDbObjectId,bool>>& interior,const std::vector<ProductOpeningAxisTerminal>& axisTerminals){
 struct HostGroup{GenericOpeningHost host;std::vector<const GenericNativeOpening*> openings;std::vector<double> junctions;};std::map<std::size_t,HostGroup> internal;std::array<std::vector<double>,4> exterior;
 const auto sortUnique=[](std::vector<double>& values){std::sort(values.begin(),values.end());values.erase(std::unique(values.begin(),values.end(),[](double a,double b){return std::abs(a-b)<1e-5;}),values.end());};
 const auto mainSides=productConfirmedMainEntranceSides(job);
 const auto entranceSides=productConfirmedEntranceSides(job);
 const auto confirmations=productPlanDetailConfirmations(job);
 std::vector<GenericOpeningWork> work;
 for(const auto& o:input.openings){
  const auto middle=o.a+(o.b-o.a)/2;
  if(bcpproduct::nearestGenericView(views,{middle.x,middle.y})!=view)continue;
  const auto main=entranceSides.find(o.handle);
  auto host=productGenericHost(o,input,views[view]);
  if(!host&&main!=entranceSides.end()&&!o.window&&!o.grouped)
   host=productConfirmedSplitOpeningHost(o,input,views[view],main->second,layout.unitMM);
  if(!host){
   if(confirmations.bathroomDoors.count(o.handle)&&!o.window&&!o.grouped&&o.existing.empty()&&
      std::abs(o.sill)*layout.unitMM<=.01&&(o.kind==L"门"||o.kind==L"墙洞"))continue;
   if(o.kind==L"墙洞"&&o.sill>0){
    GenericOpeningInk ink;
    productGenericOpeningInk(*open<AcDbEntity>(find(db,o.handle)),ink);
    if(productNativeRaisedWallVoid(o,ink)||productConfirmedRaisedWallOpening(o,confirmations,layout.unitMM))continue;
   }
   if(o.kind==L"门"){
    GenericOpeningInk ink;
    productGenericOpeningInk(*open<AcDbEntity>(find(db,o.handle)),ink);
    if(const auto paired=productPairedInteriorTrackOpening(o,ink,input,views[view],layout.unitMM)){
     // Jamb walls locate the door but do not establish a continuous wall from
     // which to derive extra interior dimensions.
     work.push_back({&o,*paired,std::move(ink),paired->lower,paired->upper,true});
     continue;
    }
   }
   job.detailPending.push_back({u8"门窗宿主",o.handle,u8"实际洞口未唯一关联正交墙域；原门窗保留，不借用其他项目定位"});
   continue;
  }
  const auto& h=*host;
  const double lo=o.grouped?*h.beforeJamb:h.vertical?std::min(o.a.y,o.b.y):std::min(o.a.x,o.b.x);
  const double hi=o.grouped?*h.afterJamb:h.vertical?std::max(o.a.y,o.b.y):std::max(o.a.x,o.b.x);
  GenericOpeningInk ink;
  {auto e=open<AcDbEntity>(find(db,o.handle));productGenericOpeningInk(*e,ink);}
  const bool sliding=!o.window && (productGenericOpeningKind(o,L"推拉") ||
      (o.kind==L"门" && productGenericSlidingTracks(ink,h,lo,hi,input.walls[h.wall].total)));
  if(confirmations.bathroomDoors.count(o.handle)&&h.side<0&&!o.window&&!o.grouped&&
     std::abs(o.sill)*layout.unitMM<=.01&&(ink.swing||sliding||o.kind==L"门"||o.kind==L"墙洞")){
   if(!o.existing.empty())job.detailPending.push_back({u8"卫生间内部隔门",o.handle,u8"已确认 D14 省标，源对象已有门号，待用公开方式清除自身编号后重开核对"});
   job.interiorDoorMeasurements.push_back({o.handle,input.walls[h.wall].wall.handle,"",view+1,
    o.a,o.b,{},h.vertical,true,true,false,0,{}});
   continue;
  }
  if(h.side>=0){auto& stations=exterior[h.side];stations.insert(stations.end(),{h.lower,lo,hi,h.upper});}else{auto& group=internal[h.wall];group.host=h;group.openings.push_back(&o);group.junctions.push_back(h.lower);group.junctions.push_back(h.upper);}
  work.push_back({&o,h,std::move(ink),lo,hi,sliding});
 }
 // A central pair of leaves with two contiguous narrow matching leaves is one
 // exterior entry. Keep native components intact and label the central object.
 std::set<std::string> assemblySides;
 std::map<std::string,std::pair<double,double>> assemblies;
 const auto eligible=[](const GenericOpeningWork& item) {
  return item.host.side>=0 && item.ink.swing && !item.sliding && !item.opening->window &&
         item.opening->existing.empty() && item.opening->kind==L"门";
 };
 for (const auto& centre:work) {
  if (!eligible(centre)||!mainSides.count(centre.opening->handle)) continue;
  std::vector<const GenericOpeningWork*> left,right;
  for (const auto& other:work) {
   if (&other==&centre || !eligible(other) || other.host.side!=centre.host.side ||
       other.host.vertical!=centre.host.vertical || std::abs(other.host.normal-centre.host.normal)>1e-4 ||
       std::abs(other.opening->height-centre.opening->height)>1e-4 ||
       other.opening->width*2>centre.opening->width+1e-4) continue;
   if (std::abs(other.high-centre.low)<1.) left.push_back(&other);
   if (std::abs(other.low-centre.high)<1.) right.push_back(&other);
  }
  if (left.size()!=1 || right.size()!=1 || std::abs(left[0]->opening->width-right[0]->opening->width)>1e-4 ||
      assemblySides.count(left[0]->opening->handle) || assemblySides.count(right[0]->opening->handle)) continue;
  const double lo=left[0]->low,hi=right[0]->high;
  const double combined=left[0]->opening->width+centre.opening->width+right[0]->opening->width;
  if (std::abs(hi-lo-combined)>1.) continue;
  assemblies.emplace(centre.opening->handle,std::make_pair(combined,(lo+hi)/2));
  assemblySides.insert(left[0]->opening->handle);
  assemblySides.insert(right[0]->opening->handle);
 }
 // Independently drafted leaves can share one aperture. Establish the group
 // first so a valid leaf size cannot accidentally become a standalone number.
 std::map<std::size_t,std::vector<const GenericOpeningWork*>> leavesByWall;
 for(const auto& item:work)if(eligible(item))leavesByWall[item.host.wall].push_back(&item);
 std::set<std::string> unsupportedAssemblies;
 const double jointTolerance=1./layout.unitMM;
 for(auto& [wall,leaves]:leavesByWall){
  std::sort(leaves.begin(),leaves.end(),[](const auto* a,const auto* b){return a->low<b->low;});
  for(std::size_t i=0;i+3<leaves.size();++i){
   const auto& left=*leaves[i];const auto& first=*leaves[i+1];
   const auto& second=*leaves[i+2];const auto& right=*leaves[i+3];
   const std::array members{&left,&first,&second,&right};
   if(i&&std::abs(leaves[i-1]->high-left.low)<=jointTolerance)continue;
   if(i+4<leaves.size()&&std::abs(right.high-leaves[i+4]->low)<=jointTolerance)continue;
   if(std::abs(left.high-first.low)>jointTolerance||std::abs(first.high-second.low)>jointTolerance||
      std::abs(second.high-right.low)>jointTolerance||left.opening->width>=first.opening->width-jointTolerance||
      std::abs(left.opening->width-right.opening->width)>jointTolerance||
      std::abs(first.opening->width-second.opening->width)>jointTolerance)continue;
   if(std::any_of(members.begin(),members.end(),[&](const auto* leaf){
    return std::abs(leaf->opening->height-first.opening->height)>jointTolerance||
      std::abs(leaf->opening->sill)>jointTolerance||std::abs(leaf->host.normal-first.host.normal)>jointTolerance||
      assemblies.count(leaf->opening->handle)||assemblySides.count(leaf->opening->handle);
   }))continue;
   const auto width=(right.high-left.low)*layout.unitMM;
   const auto height=first.opening->height*layout.unitMM;
   if(!productGenericOpeningCode(L"FMD",width,height).empty())continue;
   for(const auto* leaf:members){
    const auto& handle=leaf->opening->handle;
    unsupportedAssemblies.insert(handle);
    productRecordRuleEvidence(job,"O05","{\"panel\":"+std::to_string(panel)+",\"object\":\""+handle+
     "\",\"result\":\"observed-four-contiguous-swing-leaves\",\"overallWidthMM\":"+std::to_string(width)+
     ",\"overallHeightMM\":"+std::to_string(height)+"}");
   }
   i+=3;
  }
 }
 for (const auto& item:work) {
  const auto& o=*item.opening;
  const auto& h=item.host;
  if (assemblySides.count(o.handle)) continue;
  if(unsupportedAssemblies.count(o.handle)){
   job.detailPending.push_back({u8"非整百组合洞口编号",o.handle,u8"同宿主、等高的四扇对称平开扇已合并核读为一个洞口；O03 无适用整体尾码，保留原生分扇参数"});
   continue;
  }
  const auto assembly=assemblies.find(o.handle);
  const auto confirmed=mainSides.find(o.handle);
  const bool confirmedMain=confirmed!=mainSides.end()&&confirmed->second==h.side&&
   !o.window&&!o.grouped&&item.ink.swing&&!item.sliding&&std::abs(o.sill)*layout.unitMM<=.01;
  const bool main=confirmedMain||assembly!=assemblies.end() || (h.side>=0 && (productGenericOpeningKind(o,L"防盗") ||
                  productGenericOpeningKind(o,L"大门") || productGenericOpeningKind(o,L"入户门")));
  const auto security=confirmations.securityDoors.find(o.handle);
  const bool securityDoor=security!=confirmations.securityDoors.end()&&h.side>=0&&
    !o.window&&!o.grouped&&item.ink.swing&&!item.sliding&&std::abs(o.sill)*layout.unitMM<=.01&&
    std::abs(o.width*layout.unitMM-security->second[0])<.01&&
    std::abs(o.height*layout.unitMM-security->second[1])<.01;
  if(security!=confirmations.securityDoors.end()&&!securityDoor)
   throw std::runtime_error("confirmed security door does not match exterior swing and actual source dimensions");
  const double width=assembly==assemblies.end()?o.width:assembly->second.first;
  const double center=assembly==assemblies.end()?(item.low+item.high)/2:assembly->second.second;
  const wchar_t* prefix=o.grouped?nullptr:o.window?L"LC":(main||securityDoor)?L"FMD":item.sliding?(h.side<0?L"M":L"LM"):
      h.side<0 && item.ink.swing?L"M":h.side<0 && o.kind==L"墙洞" && std::abs(o.sill)*layout.unitMM<=.01?L"DK":nullptr;
  const auto ruleNumber=productGenericOpeningCode(prefix,width*layout.unitMM,o.height*layout.unitMM);
  std::wstring number=o.existing;
  if (number.empty()) {
   if (!prefix) {
    if(productNativeRaisedWallVoid(o,item.ink)||productConfirmedRaisedWallOpening(o,confirmations,layout.unitMM))continue;
    job.detailPending.push_back({u8"门型编号",o.handle,u8"公开类型与图形未能确认适用编号，保留原洞口"});
    continue;
   }
   if (ruleNumber.empty()) {
    job.detailPending.push_back({u8"非整百门窗编号",o.handle,u8"洞口真实宽高保留，未推定未确认的尾码"});
    continue;
   }
   number=ruleNumber;
  }else if(ruleNumber.empty()||number!=ruleNumber){
   job.detailPending.push_back({u8"既有门窗编号",o.handle,u8"源号与本轮可确定编码不一致，或源类别／组合口径未证实；保留源号和原洞口参数，待核门窗表及分段关系"});
  }
  if (main) {
   for (const auto& note:input.notes) {
    const auto marker=note.text.find(L"大门高度");
    if (marker==std::wstring::npos || bcpproduct::nearestGenericView(views,{note.anchor.x,note.anchor.y})!=view) continue;
    const auto digit=note.text.find_first_of(L"0123456789",marker);
    if (digit==std::wstring::npos) continue;
    const double noted=std::wcstod(note.text.c_str()+digit,nullptr);
    if (noted>0 && std::abs(noted-o.height)>1e-4) {
     job.detailPending.push_back({u8"大门高度核对",o.handle,u8"组合编号采用原生洞口高度，方案说明高度不一致，保留两处原始数据供核对"});
     break;
    }
   }
  }
  auto caption=productGenericLabelOpening(job,db,item,number,center,input.walls[h.wall].total);
  caption.width=width;
  caption.height=o.height;
  caption.panel=panel;
  caption.ruleNumber=ruleNumber;
  caption.window=o.window;
  caption.grouped=o.grouped;
  caption.source=!o.existing.empty()?"existing":confirmedMain?"confirmedMainEntrance":assembly!=assemblies.end()?"entryAssembly":item.sliding?"sliding":"native";
  captions.push_back(std::move(caption));
 }
 std::vector<bcpproduct::OpeningStationWall> transverseWalls;
 std::vector<std::string> transverseHandles;
 for(const auto index:views[view].walls){
  const auto& item=input.walls[index];
  if(item.low||!item.orthogonal)continue;
  const auto a=bcp::bodyCentreStart(item.wall);
  const bcp::Point b{a.x+item.wall.end.x-item.wall.start.x,a.y+item.wall.end.y-item.wall.start.y};
  const bool vertical=std::abs(a.x-b.x)<1e-5;
  transverseWalls.push_back({vertical,vertical?a.x:a.y,vertical?std::min(a.y,b.y):std::min(a.x,b.x),
      vertical?std::max(a.y,b.y):std::max(a.x,b.x),item.total});
  transverseHandles.push_back(item.wall.handle);
 }
 for (int side = 0; side < 4; ++side) {
  auto& stations = exterior[side];
  const double tolerance=.01/layout.unitMM;
  std::vector<double> axes;
  const auto row=std::find_if(layout.dimensions.begin(),layout.dimensions.end(),[&](const auto& role){
   return role.panel==panel&&role.side==side&&role.row==2;
  });
  if(row!=layout.dimensions.end()){
   const auto id=find(db,row->handle);const auto grips=chainGrips(id);
   const auto count=chainValues(id).size();
   require(grips.size()==2*count+6,"second-row source axis station topology");
   for(std::size_t i=2;i<count+3;++i)axes.push_back(side<2?grips[i].y:grips[i].x);
  }
  std::map<std::size_t,bcpproduct::OpeningStationWall> hosts;
  for(const auto& item:work){
   const auto& h=item.host;
   if(h.side!=side)continue;
   hosts.emplace(h.wall,bcpproduct::OpeningStationWall{h.vertical,h.normal,h.lower,h.upper,input.walls[h.wall].total});
  }
  for(auto first=hosts.begin();first!=hosts.end();++first)
   for(auto second=std::next(first);second!=hosts.end();++second){
    const auto& a=first->second;
    const auto& b=second->second;
    if(std::abs(a.normal-b.normal)>tolerance||
       std::min(std::abs(a.upper-b.lower),std::abs(b.upper-a.lower))>tolerance)continue;
    std::vector<double> hostEdges;
    for(const auto& item:work)
     if(item.host.wall==first->first||item.host.wall==second->first)
      hostEdges.insert(hostEdges.end(),{item.low,item.high});
    const auto axis=bcpproduct::openingStationAtWallJoin(a,b,hostEdges,transverseWalls,axes,tolerance);
    if(!axis)continue;
    const double split=std::min(a.upper,b.upper);
    const bool anotherOpeningUsesStation=std::any_of(work.begin(),work.end(),[&](const auto& item){
     return item.host.side==side&&
       (std::abs(item.low-split)<=tolerance||std::abs(item.high-split)<=tolerance);
    });
    if(anotherOpeningUsesStation){
     stations.push_back(*axis);
     continue;
    }
    stations.erase(std::remove_if(stations.begin(),stations.end(),[&](double value){return std::abs(value-split)<=tolerance;}),stations.end());
    stations.push_back(*axis);
   }
  for(const auto& item:work){
   const auto& host=item.host;
   if(host.side!=side||!host.beforeJamb||!host.afterJamb)continue;
   for(const double jamb:{*host.beforeJamb,*host.afterJamb}){
    if(jamb<=item.low+tolerance||jamb>=item.high-tolerance)continue;
    const bool independent=std::any_of(work.begin(),work.end(),[&](const auto& other){
     return &item!=&other&&other.host.side==side&&
      (std::abs(other.low-jamb)<=tolerance||std::abs(other.high-jamb)<=tolerance);
    })||std::any_of(axes.begin(),axes.end(),[&](double axis){return std::abs(axis-jamb)<=tolerance;});
    if(!independent)stations.erase(std::remove_if(stations.begin(),stations.end(),[&](double value){
     return std::abs(value-jamb)<=tolerance;
    }),stations.end());
   }
  }
  sortUnique(stations);
  if(job.unifiedPlan){
   auto& expected=layout.expectedPerimeter[{panel,side}];
   if(!work.empty())expected.openingStations=stations;
   for(const auto& item:work)if(item.host.side==side)expected.openings.push_back(item.opening->handle);
   expected.openingScopeKnown=!expected.openings.empty();
   if(expected.openings.empty()&&expected.axisScopeKnown){
    const bool vertical=side<2;
    const double low=vertical?bounds.ymin:bounds.xmin,high=vertical?bounds.ymax:bounds.xmax;
    std::vector<double> intervals{low,high};
    for(const auto& wall:transverseWalls)if(wall.vertical==vertical){
     if(wall.lower>low+tolerance&&wall.lower<high-tolerance)intervals.push_back(wall.lower);
     if(wall.upper>low+tolerance&&wall.upper<high-tolerance)intervals.push_back(wall.upper);
    }
    sortUnique(intervals);
    std::set<std::size_t> facade;
    bool covered=true;
    for(std::size_t i=1;i<intervals.size();++i){
     if(intervals[i]-intervals[i-1]<=tolerance)continue;
     const double mid=(intervals[i]+intervals[i-1])/2;
     std::optional<std::size_t> edge;
     for(std::size_t k=0;k<transverseWalls.size();++k){
      const auto& wall=transverseWalls[k];
      if(wall.vertical!=vertical||mid<wall.lower||mid>wall.upper)continue;
      if(!edge||(side%2?wall.normal>transverseWalls[*edge].normal:wall.normal<transverseWalls[*edge].normal))edge=k;
     }
     // A porch gap has no facade wall and therefore cannot require an
     // opening chain. Check every actual wall interval on this side.
     if(!edge)continue;
     facade.insert(*edge);
    }
    const auto mightHaveOpening=[&](const auto& wall,const AcGePoint3d& a,const AcGePoint3d& b){
     const double minNormal=vertical?std::min(a.x,b.x):std::min(a.y,b.y);
     const double maxNormal=vertical?std::max(a.x,b.x):std::max(a.y,b.y);
     const double minStation=vertical?std::min(a.y,b.y):std::min(a.x,b.x);
     const double maxStation=vertical?std::max(a.y,b.y):std::max(a.x,b.x);
     const double half=wall.thickness/2+tolerance;
     return minNormal<=wall.normal+half&&maxNormal>=wall.normal-half&&
            std::min(maxStation,wall.upper)>std::max(minStation,wall.lower)+tolerance;
    };
    for(const auto& [raw,type]:modelTypes(db)){
     if(type!=L"TDbOpening"&&type!=L"TDbGroupOpening")continue;
     const auto handle=ascii(raw);
     const auto found=std::find_if(input.openings.begin(),input.openings.end(),[&](const auto& item){return item.handle==handle;});
     if(found!=input.openings.end()){
      if((vertical?std::abs(found->a.x-found->b.x):std::abs(found->a.y-found->b.y))>tolerance)continue;
      for(const auto k:facade)if(mightHaveOpening(transverseWalls[k],found->a,found->b))covered=false;
     }else{
      const auto opening=open<AcDbEntity>(find(db,handle));
      AcDbExtents ext;
      if(opening->getGeomExtents(ext)!=Acad::eOk)covered=false;
      else for(const auto k:facade)if(mightHaveOpening(transverseWalls[k],ext.minPoint(),ext.maxPoint()))covered=false;
     }
    }
    if(covered&&!facade.empty())for(const auto k:facade)expected.openingFreeWalls.push_back(transverseHandles[k]);
   }
  }
  if (stations.size() < 2) continue;
  const bool vertical = side < 2;
  const double datum = productPlanDatum(bounds, side);
  // This row terminates at the actual walls and openings contributing to it.
  // The overall dimension's outermost column axis is not a third-row station.
  std::vector<AcGePoint3d> points;
  for (double station : stations)
   points.push_back(vertical ? AcGePoint3d(datum, station, 0) : AcGePoint3d(station, datum, 0));
  const auto before=job.unifiedPlan?wholeModelIds(db):std::set<AcDbObjectId>{};
  try{
   const auto id=productGenericChain(job,db,prototype,points,1000);
   const auto handle=axisHandle(id);
   layout.dimensions.push_back({handle,panel,side,3});
   layout.sourceOpeningStations.emplace(handle,stations);
  }catch(const std::runtime_error& error){
   if(!job.unifiedPlan)throw;
   for(const auto& id:wholeModelIds(db))if(!before.count(id))
    productStatus(editObject<AcDbEntity>(id)->erase(),"roll back failed opening chain");
   require(wholeModelIds(db)==before,"opening dimension command affected an existing model object");
   const auto sideKey=std::string(1,"WESN"[side]);
   const auto reason=std::string(u8"第三道洞口尺寸生成失败，保留原尺寸。")+error.what();
   productCheckPlanSource(job,view+1,"perimeter",sideKey,bcpproduct::PlanCheck::failed,{},reason);
   job.detailPending.push_back({u8"外围尺寸局部失败",std::to_string(view+1)+"/"+sideKey,reason});
  }
 }
 std::vector<HostGroup> runs;for(const auto& item:internal)runs.push_back(item.second);bool changed=true;while(changed){changed=false;for(std::size_t i=0;i<runs.size()&&!changed;++i)for(std::size_t k=i+1;k<runs.size();++k){auto& a=runs[i];const auto& b=runs[k];if(a.host.vertical!=b.host.vertical||std::abs(a.host.normal-b.host.normal)>1e-5||a.host.lower>b.host.upper+1e-5||b.host.lower>a.host.upper+1e-5)continue;a.host.lower=std::min(a.host.lower,b.host.lower);a.host.upper=std::max(a.host.upper,b.host.upper);a.openings.insert(a.openings.end(),b.openings.begin(),b.openings.end());a.junctions.insert(a.junctions.end(),b.junctions.begin(),b.junctions.end());runs.erase(runs.begin()+k);changed=true;break;}}
 std::ostringstream jambReport;
 jambReport<<std::setprecision(17)<<"{\"panel\":"<<panel+1<<",\"doors\":[";
 bool firstJamb=true;
 const double tolerance=.01/layout.unitMM;
 for(const auto& group:runs){
  const auto& host=group.host;
  const auto station=[&](const AcGePoint3d& point){return host.vertical?point.y:point.x;};
  const auto openingEdges=[&](const GenericNativeOpening& opening){
   const auto a=station(opening.a),b=station(opening.b);
   return std::make_pair(std::min(a,b),std::max(a,b));
  };
  std::vector<double> stations=group.junctions;
  stations.insert(stations.end(),{host.lower,host.upper});
  std::vector<std::pair<const ProductOpeningAxisTerminal*,double>> connections;
  if(job.unifiedPlan)for(bool positive:{false,true}){
   const double endpoint=positive?host.upper:host.lower;
   const auto axis=std::find_if(axisTerminals.begin(),axisTerminals.end(),[&](const auto& candidate){
    return candidate.axisVertical!=host.vertical&&std::abs(candidate.station-endpoint)<=tolerance&&
           host.normal>=candidate.first-tolerance&&host.normal<=candidate.last+tolerance;
   });
   if(axis!=axisTerminals.end()){
    stations.push_back(axis->station);
    connections.emplace_back(&*axis,axis->station);
   }
  }
  std::vector<std::pair<double,double>> omissions;
  for(const auto* opening:group.openings){
   const auto [lower,upper]=openingEdges(*opening);
   stations.insert(stations.end(),{lower,upper});
   const auto item=std::find_if(work.begin(),work.end(),[&](const auto& row){return row.opening==opening;});
   if(item==work.end()||opening->window||opening->grouped||item->sliding||!item->ink.swing||
      std::abs(opening->sill)>tolerance)continue;
   const auto& hostWall=input.walls[item->host.wall];
   const double thickness=hostWall.total*layout.unitMM;
   const double standard=std::abs(thickness-200)<=.01?100:std::abs(thickness-240)<=.01?120:0;
   if(standard==0)continue;
   const auto face=productDoorJambFace(input,views[view],*opening,host,tolerance);
   const double gap=face?face->gap:0.;
   const bool clearFace=face&&gap*layout.unitMM<=productMaxCandidateJambMM;
   const double doorEdge=clearFace&&face->lower?lower:upper;
   const auto caption=std::find_if(captions.begin(),captions.end(),[&](const auto& record){
    return record.id==find(db,opening->handle);
   });
   std::wostringstream expectedNumber;
   const double actualWidth=(upper-lower)*layout.unitMM;
   const double actualHeight=opening->height*layout.unitMM;
   expectedNumber<<L'M'<<std::setfill(L'0')<<std::setw(2)<<static_cast<int>(std::round(actualWidth/100))
                 <<std::setw(2)<<static_cast<int>(std::round(actualHeight/100));
   bool widthNumberVerified=false;
   if(caption!=captions.end()&&caption->number==expectedNumber.str()&&
      std::abs(actualWidth-std::round(actualWidth/100)*100)<=.01&&
      std::abs(actualHeight-std::round(actualHeight/100)*100)<=.01){
    const auto entity=open<AcDbEntity>(caption->id);
    widthNumberVerified=productGenericCaptionGlyph(*entity,caption->number).has_value();
   }
   const bool omitJamb=clearFace&&std::abs(gap*layout.unitMM-standard)<=.01;
   const bool omit=omitJamb&&widthNumberVerified;
   const auto onHost=[&](double value){
    return host.vertical?AcGePoint3d(host.normal,value,0):AcGePoint3d(value,host.normal,0);
   };
   job.interiorDoorMeasurements.push_back({opening->handle,hostWall.wall.handle,clearFace?face->wall:"",view+1,
    onHost(lower),onHost(upper),clearFace?onHost(face->station):AcGePoint3d(),host.vertical,omit,false,clearFace,standard,
    widthNumberVerified?caption->number:std::wstring{},omitJamb});
   std::optional<double> redundantWallReturn;
   if(omit)omissions.emplace_back(lower,upper);
   if(clearFace){
    stations.push_back(face->station);
    if(omitJamb)omissions.emplace_back(std::min(face->station,doorEdge),std::max(face->station,doorEdge));
    // Once the standard jamb and numbered door width are omitted, the
    // synthetic face-to-centre remainder only measures half of the adjoining
    // wall. Retain its real axis as a station for any other opening.
    if(job.unifiedPlan&&omit){
     const auto adjoining=std::find_if(input.walls.begin(),input.walls.end(),[&](const auto& wall){
      return wall.wall.handle==face->wall;
     });
     if(adjoining!=input.walls.end()){
      const auto centre=bcp::bodyCentreStart(adjoining->wall);
      const double axis=host.vertical?centre.y:centre.x;
      const auto axisExists=std::any_of(axisTerminals.begin(),axisTerminals.end(),[&](const auto& terminal){
       return terminal.axisVertical!=host.vertical&&std::abs(axis-terminal.station)<=tolerance&&
        host.normal>=terminal.first-tolerance&&host.normal<=terminal.last+tolerance;
      });
      const double first=std::min(face->station,axis),last=std::max(face->station,axis);
      const bool servesAnotherOpening=std::any_of(group.openings.begin(),group.openings.end(),[&](const auto* other){
       if(other==opening)return false;
       const auto [a,b]=openingEdges(*other);
       return a<last-tolerance&&b>first+tolerance;
      });
      if(axisExists&&!servesAnotherOpening&&
         (axis-face->station)*(face->station-doorEdge)>0&&
         std::abs(last-first-adjoining->total*.5)<=tolerance){
       stations.push_back(axis);
       omissions.emplace_back(first,last);
       redundantWallReturn=axis;
      }
     }
    }
   }
   const auto change=job.doorJambChanges.find(opening->handle);
   const auto reason=change==job.doorJambChanges.end()?std::string{}:change->second.reason;
   if(!omitJamb)job.detailPending.push_back({u8"室内门净垛",opening->handle,
    !reason.empty()?reason:u8"相邻墙内面或可调整条件待复核，保留实测定位"});
   if(!firstJamb)jambReport<<',';
   firstJamb=false;
   std::ostringstream row;
   row<<std::setprecision(17)<<"{\"panel\":"<<panel+1
    <<",\"object\":"<<jsonUtf8(bcpproduct::decodeMessage(opening->handle))
    <<",\"door\":"<<jsonUtf8(bcpproduct::decodeMessage(opening->handle))
    <<",\"hostWall\":"<<jsonUtf8(bcpproduct::decodeMessage(hostWall.wall.handle))
    <<",\"standardMM\":"<<standard<<",\"netJambMM\":";
   if(clearFace)row<<gap*layout.unitMM;else row<<"null";
   row<<",\"wallFace\":";
   if(clearFace)row<<face->station;else row<<"null";
   row<<",\"adjacentWall\":"<<jsonUtf8(clearFace?bcpproduct::decodeMessage(face->wall):L"")
    <<",\"widthNumberVerified\":"<<(widthNumberVerified?"true":"false")
    <<",\"omittedJamb\":"<<(omitJamb?"true":"false")
    <<",\"reason\":"<<jsonUtf8(bcpproduct::decodeMessage(reason))
    <<",\"omittedDuplicateWidths\":"<<(omit?"true":"false")
    <<",\"omittedAdjoiningHalfWall\":"<<(redundantWallReturn?"true":"false")
    <<",\"adjoiningAxisStation\":";
   if(redundantWallReturn)row<<*redundantWallReturn;else row<<"null";
   row
    <<",\"savedReopened\":false,\"result\":\""<<(omit?"omission-applied":clearFace?"retained-measured":"pending")<<"\"}";
   jambReport<<row.str();
  }
  sortUnique(stations);
  const double centre=host.vertical?(bounds.xmin+bounds.xmax)/2:(bounds.ymin+bounds.ymax)/2;
  const double offset=(host.normal<centre?1:-1)*(host.vertical?-1000:1000);
  std::vector<AcGePoint3d> points;
  const auto append=[&](double value){
   points.push_back(host.vertical?AcGePoint3d(host.normal,value,0):AcGePoint3d(value,host.normal,0));
  };
  const auto emit=[&](){
   if(points.size()<2)return;
   const auto result=productTryPlanChain(job,db,prototype,points,offset,view+1,
     input.walls[host.wall].wall.handle,u8"室内洞口定位尺寸","D13");
   if(!result)return;
   const auto id=*result;
   auto entity=editObject<AcDbEntity>(id);
   entity->setLayer(productPlanLayer(db,L"PUB_DIM",25));
   entity->setColorIndex(256);
   interior.emplace_back(id,host.vertical);
   for(const auto& connection:connections){
    const auto* axis=connection.first;
    const double terminal=connection.second;
    const auto opening=std::min_element(group.openings.begin(),group.openings.end(),[&](const auto* a,const auto* b){
     const auto [aLow,aHigh]=openingEdges(*a);const auto [bLow,bHigh]=openingEdges(*b);
     return std::min(std::abs(aLow-terminal),std::abs(aHigh-terminal))<std::min(std::abs(bLow-terminal),std::abs(bHigh-terminal));
    });
    if(opening==group.openings.end())continue;
    const auto [low,high]=openingEdges(**opening);
    const double edge=std::abs(low-terminal)<std::abs(high-terminal)?low:high;
    const bool hasTerminal=std::any_of(points.begin(),points.end(),[&](const auto& p){return std::abs(station(p)-terminal)<=tolerance;});
    const bool hasEdge=std::any_of(points.begin(),points.end(),[&](const auto& p){return std::abs(station(p)-edge)<=tolerance;});
    if(hasTerminal&&hasEdge)layout.openingAxisChecks.push_back({panel,(*opening)->handle,axis->source,axisHandle(id),terminal,edge,host.normal,host.vertical});
   }
  };
  for(std::size_t k=1;k<stations.size();++k){
   const double a=stations[k-1],b=stations[k];
   if(std::any_of(omissions.begin(),omissions.end(),[&](const auto& range){
    return a>=range.first-tolerance&&b<=range.second+tolerance;
   })){
    emit();
    points.clear();
    continue;
   }
   if(points.empty())append(a);
   append(b);
  }
  emit();
 }
 jambReport<<"]}";
 writeNew(job.root/(L"interior-door-jambs-"+std::to_wstring(panel+1)+L".json"),jambReport.str());
}
void productGenericTitleClearance(ProductJob& job,AcDbDatabase& db){
 std::vector<AcDbExtents> texts;std::vector<AcDbObjectId> titles;for(const auto& item:modelTypes(db)){const auto id=find(db,ascii(item.first));if(item.second==L"TDbDrawingName"){titles.push_back(id);continue;}if(item.second!=L"TDbText"&&item.second!=L"TDbMText"&&item.second!=L"TDbSpace"&&item.second!=L"AcDbText"&&item.second!=L"AcDbMText")continue;auto e=open<AcDbEntity>(id);if(e->visibility()==AcDb::kVisible)productGenericTextLeaves(*e,texts);}
 for(auto id:titles){bool clear=false;for(int step=0;step<30;++step){AcDbExtents box;{auto e=open<AcDbEntity>(id);if(e->getGeomExtents(box)!=Acad::eOk)break;}if(std::none_of(texts.begin(),texts.end(),[&](const auto& text){return productGenericBoxesOverlap(box,text,100);})){clear=true;break;}auto e=editObject<AcDbEntity>(id);productStatus(e->transformBy(AcGeMatrix3d::translation({0,-400,0})),"move native title below area text");}if(!clear)job.detailPending.push_back({u8"图名避让",axisHandle(id),u8"图名已就近向下排布，仍有冲突需人工调整"});}
}
