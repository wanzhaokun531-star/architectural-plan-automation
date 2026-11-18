// Measure the connected free floor inside the actual orthogonal wall faces.
// The grid uses wall/column edges as cuts, so cell areas are exact, not sampled.
struct ProductRoomGrid {
 std::vector<double> xs,ys;
 std::vector<int> cells;
 std::vector<double> areas;
 static constexpr int blocked=-2, outside=-1, free=-3;
 std::size_t width() const {return xs.size()-1;}
 std::size_t height() const {return ys.size()-1;}
 std::size_t index(std::size_t x,std::size_t y) const {return y*width()+x;}
 AcGePoint3d centre(std::size_t x,std::size_t y) const {
  return {(xs[x]+xs[x+1])/2,(ys[y]+ys[y+1])/2,0};
 }
 int at(double x,double y) const {
  const auto i=std::upper_bound(xs.begin(),xs.end(),x);
  const auto j=std::upper_bound(ys.begin(),ys.end(),y);
  if(i==xs.begin()||i==xs.end()||j==ys.begin()||j==ys.end())return outside;
  return cells[index(static_cast<std::size_t>(i-xs.begin()-1),static_cast<std::size_t>(j-ys.begin()-1))];
 }
 int at(const AcGePoint3d& point) const {return at(point.x,point.y);}
};

struct ProductRoomBarrier {
 std::string handle;
 bool vertical;
 double normal,lo,hi;
};

std::optional<ProductRoomGrid> productRoomGrid(const std::vector<std::array<double,4>>& rectangles,
 const std::vector<ProductRoomBarrier>& barriers={}) {
 if(rectangles.empty())return {};
 ProductRoomGrid grid;
 for(const auto& r:rectangles){
  grid.xs.insert(grid.xs.end(),{r[0],r[2]});
  grid.ys.insert(grid.ys.end(),{r[1],r[3]});
 }
 for(const auto& barrier:barriers){
  (barrier.vertical?grid.xs:grid.ys).push_back(barrier.normal);
  auto& along=barrier.vertical?grid.ys:grid.xs;
  along.insert(along.end(),{barrier.lo,barrier.hi});
 }
 constexpr double eps=1e-4;
 const auto normalize=[eps](std::vector<double>& cuts){
  std::sort(cuts.begin(),cuts.end());
  cuts.erase(std::unique(cuts.begin(),cuts.end(),[eps](double a,double b){return std::abs(a-b)<eps;}),cuts.end());
  cuts.insert(cuts.begin(),cuts.front()-1);
  cuts.push_back(cuts.back()+1);
 };
 normalize(grid.xs);normalize(grid.ys);
 if(grid.width()>1000000/grid.height())return {};
 grid.cells.assign(grid.width()*grid.height(),ProductRoomGrid::free);
 const auto cut=[eps](const std::vector<double>& axis,double value){
  return static_cast<std::size_t>(std::lower_bound(axis.begin(),axis.end(),value-eps)-axis.begin());
 };
 for(const auto& r:rectangles){
  const auto a=cut(grid.xs,r[0]),b=cut(grid.xs,r[2]),c=cut(grid.ys,r[1]),d=cut(grid.ys,r[3]);
  for(auto y=c;y<d;++y)for(auto x=a;x<b;++x)grid.cells[grid.index(x,y)]=ProductRoomGrid::blocked;
 }
 std::vector<unsigned char> right,up;
 if(!barriers.empty()){
  right.resize(grid.cells.size());up.resize(grid.cells.size());
  for(const auto& barrier:barriers){
   const auto across=cut(barrier.vertical?grid.xs:grid.ys,barrier.normal);
   const auto& along=barrier.vertical?grid.ys:grid.xs;
   for(auto k=cut(along,barrier.lo);k<cut(along,barrier.hi);++k){
    if(barrier.vertical)right[grid.index(across-1,k)]=1;
    else up[grid.index(k,across-1)]=1;
   }
  }
 }
 std::vector<std::size_t> queue;
 const auto flood=[&](std::size_t first,int room){
  queue.clear();
  queue.push_back(first);grid.cells[first]=room;
  double area=0;
  for(std::size_t k=0;k<queue.size();++k){
   const auto cell=queue[k],x=cell%grid.width(),y=cell/grid.width();
   area+=(grid.xs[x+1]-grid.xs[x])*(grid.ys[y+1]-grid.ys[y]);
   const auto visit=[&](std::size_t next){
    if(grid.cells[next]==ProductRoomGrid::free){grid.cells[next]=room;queue.push_back(next);}
   };
   if(x>0&&(right.empty()||!right[cell-1]))visit(cell-1);
   if(x+1<grid.width()&&(right.empty()||!right[cell]))visit(cell+1);
   if(y>0&&(up.empty()||!up[cell-grid.width()]))visit(cell-grid.width());
   if(y+1<grid.height()&&(up.empty()||!up[cell]))visit(cell+grid.width());
  }
  return area;
 };
 flood(0,ProductRoomGrid::outside);
 for(std::size_t i=0;i<grid.cells.size();++i)if(grid.cells[i]==ProductRoomGrid::free)
  grid.areas.push_back(flood(i,static_cast<int>(grid.areas.size())));
 return grid;
}

struct ProductRoomExteriorOpening {
 std::string handle,firstWall,secondWall;
 std::array<double,4> bounds;
};

std::vector<ProductRoomExteriorOpening> productCloseRoomExteriorOpenings(const ProductJob& job,
 const GenericNativeInput& input,const std::vector<bcpproduct::GenericView>& views,std::size_t view,double unit,
 std::vector<std::array<double,4>>& rectangles,std::optional<ProductRoomGrid>& grid) {
 if(!grid)return {};
 struct Candidate {
  ProductRoomExteriorOpening opening;
  AcGePoint2d outside,inside;
 };
 std::vector<Candidate> candidates;
 const auto exteriorSides=productConfirmedEntranceSides(job);
 const double sample=.01/unit;
 for(const auto& opening:input.openings){
  const auto side=exteriorSides.find(opening.handle);
  if(side==exteriorSides.end()||opening.window||std::abs(opening.sill)*unit>.01)continue;
  const auto mid=opening.a+(opening.b-opening.a)*.5;
  if(bcpproduct::nearestGenericView(views,{mid.x,mid.y})!=view||productGenericHost(opening,input,views[view]))continue;
  const auto host=productConfirmedSplitOpeningHost(opening,input,views[view],side->second,unit);
  if(!host||!host->beforeJamb||!host->afterJamb||!host->oppositeWall)continue;
  const auto& first=input.walls[host->wall].wall;
  const auto& second=input.walls[*host->oppositeWall].wall;
  const auto a=approvedRect(first),b=approvedRect(second);
  const std::size_t normalAxis=host->vertical?0:1;
  const std::size_t alongAxis=1-normalAxis;
  if(std::abs(a[normalAxis]-b[normalAxis])>sample||std::abs(a[normalAxis+2]-b[normalAxis+2])>sample||
     std::abs(a[alongAxis+2]-*host->beforeJamb)>sample||std::abs(b[alongAxis]-*host->afterJamb)>sample)continue;
  const auto lowFace=std::max(a[normalAxis],b[normalAxis]);
  const auto highFace=std::min(a[normalAxis+2],b[normalAxis+2]);
  const double lowJamb=a[alongAxis+2],highJamb=b[alongAxis];
  if(highFace-lowFace<=sample||highJamb-lowJamb<=sample)continue;
  const std::array<double,4> bounds=host->vertical?std::array<double,4>{lowFace,lowJamb,highFace,highJamb}:
                                                 std::array<double,4>{lowJamb,lowFace,highJamb,highFace};
  const bool positive=side->second%2!=0;
  const auto point=[&](double normal){
   const double along=(lowJamb+highJamb)*.5;
   return host->vertical?AcGePoint2d(normal,along):AcGePoint2d(along,normal);
  };
  const auto outer=point(positive?highFace+sample:lowFace-sample);
  const auto inner=point(positive?lowFace-sample:highFace+sample);
  if(grid->at(outer.x,outer.y)==ProductRoomGrid::outside&&grid->at(inner.x,inner.y)==ProductRoomGrid::outside)
   candidates.push_back({{opening.handle,first.handle,second.handle,bounds},outer,inner});
 }
 // A single connected interior can have several exterior openings. Test their
 // measured thresholds together and keep only thresholds facing true exterior.
 while(!candidates.empty()){
  auto trialRectangles=rectangles;
  for(const auto& candidate:candidates)trialRectangles.push_back(candidate.opening.bounds);
  auto trial=productRoomGrid(trialRectangles);
  if(!trial)return {};
  const auto invalid=std::remove_if(candidates.begin(),candidates.end(),[&](const auto& candidate){
   const int inside=trial->at(candidate.inside.x,candidate.inside.y);
   return trial->at(candidate.outside.x,candidate.outside.y)!=ProductRoomGrid::outside||inside<0||
    !std::isfinite(trial->areas[inside])||trial->areas[inside]<=0;
  });
  if(invalid!=candidates.end()){
   candidates.erase(invalid,candidates.end());
   continue;
  }
  rectangles=std::move(trialRectangles);
  grid=std::move(trial);
  std::vector<ProductRoomExteriorOpening> closed;
  for(auto& candidate:candidates)closed.push_back(std::move(candidate.opening));
  return closed;
 }
 return {};
}

enum class ProductRoomRole { other, indoor, wet, washing, excluded, stair, shaft };
std::wstring productRoomBaseName(std::wstring name) {
 while(!name.empty()&&std::wstring(L" 　1234567890一二三四五六七八九十").find(name.back())!=std::wstring::npos)
  name.pop_back();
 return name;
}
ProductRoomRole productRoomRole(const std::wstring& text) {
 const auto name=productRoomBaseName(text);
 if(name==L"卫"||name==L"卫生间"||name==L"公卫"||name==L"内卫"||name==L"客卫"||name==L"主卫"||name==L"私卫"||name==L"套卫")
  return ProductRoomRole::wet;
 if(name==L"盥洗"||name==L"盥洗间"||name==L"淋浴"||name==L"淋浴间")return ProductRoomRole::washing;
 if(name.find(L"上空")!=std::wstring::npos||name==L"屋顶"||name==L"露台"||name==L"阳台"||
    name==L"门廊"||name==L"庭院"||name==L"花池"||name==L"无障碍坡道"||name==L"坡道"||name==L"晾晒平台")
  return ProductRoomRole::excluded;
 if(name==L"楼梯")return ProductRoomRole::stair;
 if(name==L"观光电梯"||name==L"电梯"||name==L"电梯井")return ProductRoomRole::shaft;
 if(name==L"管井"||name==L"风井")return ProductRoomRole::excluded;
 static const std::set<std::wstring> indoor{
  L"厨房",L"柴火厨房",L"客厅",L"餐厅",L"主卧",L"主卧室",L"次卧",L"次卧室",L"卧室",L"客房",L"儿童房",L"老人房",L"储藏",L"储藏室",L"储物间",L"杂物间",
  L"衣",L"衣帽间",L"更衣间",L"书房",L"起居室",L"起居厅",L"大厅",L"堂屋",L"茶室",L"棋牌室",
  L"玄关",L"门厅",L"水吧",L"茶台",L"洗衣",L"洗衣房",L"家政间",L"活动室",L"烤火房",L"休闲室",L"影音室",L"走廊",L"过道",L"走道"
 };
 return indoor.count(name)?ProductRoomRole::indoor:ProductRoomRole::other;
}

struct ProductRoomName {
 std::string handle;
 std::wstring text;
 ProductRoomRole role{};
 AcDbExtents bounds;
 int region{ProductRoomGrid::outside};
 bool shaftLocated{};
};
bool productRoomHasArea(ProductRoomRole role) {
 return role==ProductRoomRole::indoor||role==ProductRoomRole::wet||role==ProductRoomRole::washing;
}

std::map<int,std::set<std::string>> productRoomNativeStairRegions(AcDbDatabase& db,const ProductRoomGrid& grid,
 const std::vector<bcpproduct::GenericView>& views,std::size_t view,double unit){
 std::map<int,std::set<std::string>> result;
 for(const auto& [handle,type]:modelTypes(db)){
  if(type!=L"TDbMultiStair"&&type!=L"TDbRectStair"&&type!=L"TDbArcStair"&&type!=L"TDbLineStair")continue;
  const auto entity=open<AcDbEntity>(find(db,ascii(handle)));
  if(!productPlanDimensionVisible(*entity))continue;
  GenericOpeningInk ink;
  productGenericOpeningInk(*entity,ink);
  std::map<int,std::vector<AcGePoint3d>> stationPoints;
  for(const auto& [first,last]:ink.straight){
   if(first.distanceTo(last)*unit<300.)continue;
   const auto middle=first+(last-first)*.5;
   if(bcpproduct::nearestGenericView(views,{middle.x,middle.y})!=view)continue;
   const int region=grid.at(middle);
   if(region<0||grid.at(first+(last-first)*.25)!=region||grid.at(first+(last-first)*.75)!=region)continue;
   auto& points=stationPoints[region];
   if(std::none_of(points.begin(),points.end(),[&](const auto& other){return other.distanceTo(middle)*unit<.01;}))
    points.push_back(middle);
  }
  for(const auto& [region,points]:stationPoints)if(points.size()>=2)result[region].insert(ascii(handle));
 }
 return result;
}
int productRoomNamePriority(const ProductRoomName& room) {
 if(room.role==ProductRoomRole::wet)return 3;
 const auto name=productRoomBaseName(room.text);
 if(name==L"客厅"||name==L"起居室"||name==L"起居厅"||name==L"主卧")return 3;
 if(room.role==ProductRoomRole::washing||name==L"衣"||name==L"衣帽"||name==L"衣帽间"||name==L"更衣间")return 1;
 return 2;
}

std::vector<ProductRoomName> productRoomNames(AcDbDatabase& db,const GenericNativeInput& input,
 const std::vector<bcpproduct::GenericView>& views,std::size_t view,const ProductRoomGrid* grid) {
 std::vector<ProductRoomName> result;
 for(const auto& [handle,fields]:input.properties){
  const auto type=fields.find(L"DXF名称"),text=fields.find(L"文字内容");
  if(type==fields.end()||type->second!=L"TCH_TEXT"||text==fields.end())continue;
  const auto role=productRoomRole(text->second);
  if(role==ProductRoomRole::other)continue;
  const auto id=find(db,handle);
  if(id.isErased())continue;
  const auto entity=open<AcDbEntity>(id);
  AcDbExtents box;
  if(entity->visibility()!=AcDb::kVisible||entity->getGeomExtents(box)!=Acad::eOk)continue;
  const auto at=box.minPoint()+(box.maxPoint()-box.minPoint())*0.5;
  if(bcpproduct::nearestGenericView(views,{at.x,at.y})!=view)continue;
  result.push_back({handle,text->second,role,box,grid?grid->at(at):ProductRoomGrid::outside});
 }
 return result;
}

struct ProductRoomShaft {
 std::string outline;
 std::array<double,4> rect;
};
std::vector<ProductRoomShaft> productRoomShafts(AcDbDatabase& db,
 const std::vector<bcpproduct::GenericView>& views,std::size_t view,std::vector<ProductRoomName>& names) {
 if(std::none_of(names.begin(),names.end(),[](const auto& name){return name.role==ProductRoomRole::shaft;}))return {};
 std::vector<ProductRoomShaft> rectangles;
 std::vector<std::pair<AcGePoint3d,AcGePoint3d>> strokes;
 for(const auto& item:modelTypes(db)){
  if(item.second!=L"AcDbPolyline"&&item.second!=L"AcDbLine")continue;
  const auto id=find(db,ascii(item.first));
  const auto object=open<AcDbEntity>(id);
  if(object->visibility()!=AcDb::kVisible||symbolName(object->layerId())!=L"STAIR")continue;
  if(const auto* poly=AcDbPolyline::cast(object.get());poly&&poly->isClosed()&&poly->numVerts()==4){
   std::array<double,4> rect{1e100,1e100,-1e100,-1e100};
   std::array<AcGePoint2d,4> points;
   bool valid=true;
   for(unsigned i=0;i<points.size();++i){
    double bulge{};
    if(poly->getPointAt(i,points[i])!=Acad::eOk||poly->getBulgeAt(i,bulge)!=Acad::eOk||std::abs(bulge)>1e-6){
     valid=false;break;
    }
    const auto p=points[i];
    rect[0]=std::min(rect[0],p.x);rect[1]=std::min(rect[1],p.y);
    rect[2]=std::max(rect[2],p.x);rect[3]=std::max(rect[3],p.y);
   }
   for(unsigned i=0;i<points.size()&&valid;++i){
    const auto a=points[i],b=points[(i+1)%points.size()];
    valid=(std::abs(a.x-b.x)<1e-4)!=(std::abs(a.y-b.y)<1e-4);
   }
   if(valid&&bcpproduct::nearestGenericView(views,{(rect[0]+rect[2])/2,(rect[1]+rect[3])/2})==view)
    rectangles.push_back({ascii(item.first),rect});
   continue;
  }
  const auto* line=AcDbLine::cast(object.get());
  const auto* poly=AcDbPolyline::cast(object.get());
  if(!line&&(!poly||poly->isClosed()||poly->numVerts()!=2))continue;
  const auto* curve=AcDbCurve::cast(object.get());
  AcGePoint3d a,b;
  if(curve&&curve->getStartPoint(a)==Acad::eOk&&curve->getEndPoint(b)==Acad::eOk&&
     std::abs(a.x-b.x)>1e-4&&std::abs(a.y-b.y)>1e-4)strokes.emplace_back(a,b);
 }
 const auto hasStroke=[&](const AcGePoint3d& a,const AcGePoint3d& b){
  return std::any_of(strokes.begin(),strokes.end(),[&](const auto& pair){
   return (pair.first.distanceTo(a)<1e-3&&pair.second.distanceTo(b)<1e-3)||
          (pair.first.distanceTo(b)<1e-3&&pair.second.distanceTo(a)<1e-3);
  });
 };
 std::vector<ProductRoomShaft> proven;
 for(const auto& inner:rectangles){
  const auto& r=inner.rect;
  if(!hasStroke({r[0],r[1],0},{r[2],r[3],0})||!hasStroke({r[0],r[3],0},{r[2],r[1],0}))continue;
  for(const auto& outer:rectangles){
   const auto& o=outer.rect;
   const double inset=r[0]-o[0];
   if(inset>1e-4&&std::abs(r[1]-o[1]-inset)<1e-3&&
      std::abs(o[2]-r[2]-inset)<1e-3&&std::abs(o[3]-r[3]-inset)<1e-3)proven.push_back(outer);
  }
 }
 std::vector<ProductRoomShaft> result;
 for(auto& name:names){
  if(name.role!=ProductRoomRole::shaft)continue;
  const auto p=name.bounds.minPoint()+(name.bounds.maxPoint()-name.bounds.minPoint())*0.5;
  std::vector<std::pair<double,const ProductRoomShaft*>> candidates;
  for(const auto& shaft:proven){
   const auto& r=shaft.rect;
   const double dx=std::max({r[0]-p.x,p.x-r[2],0.});
   const double dy=std::max({r[1]-p.y,p.y-r[3],0.});
   if((dx==0||dy==0)&&std::hypot(dx,dy)<std::min(r[2]-r[0],r[3]-r[1]))
    candidates.emplace_back(std::hypot(dx,dy),&shaft);
  }
  std::sort(candidates.begin(),candidates.end());
  if(candidates.empty()||(candidates.size()>1&&std::abs(candidates[0].first-candidates[1].first)<1e-4))continue;
  const auto& shaft=*candidates.front().second;
  if(std::none_of(result.begin(),result.end(),[&](const auto& known){return known.outline==shaft.outline;}))
   result.push_back(shaft);
  name.shaftLocated=true;
 }
 return result;
}

// A zero-thickness source divider can separate a named void from occupied
// floor, but only when both ends physically meet the wall body and the actual
// room/void labels confirm the split. An isolated detail line never suffices.
std::vector<ProductRoomBarrier> productRoomVoidBarriers(AcDbDatabase& db,
 const std::vector<std::array<double,4>>& rectangles,const std::vector<ProductRoomName>& names,
 ProductRoomGrid& grid) {
 if(std::none_of(names.begin(),names.end(),[](const auto& name){
  return name.role==ProductRoomRole::excluded&&name.text.find(L"上空")!=std::wstring::npos;
 }))return {};
 std::vector<ProductRoomBarrier> lines;
 for(const auto& item:modelTypes(db)){
  if(item.second!=L"AcDbPolyline"&&item.second!=L"AcDbLine")continue;
  const auto id=find(db,ascii(item.first));
  const auto e=open<AcDbEntity>(id);
  if(e->visibility()!=AcDb::kVisible||symbolName(e->layerId())!=L"WALL")continue;
  if(const auto* poly=AcDbPolyline::cast(e.get())){
   double bulge{};
   if(poly->isClosed()||poly->numVerts()!=2||poly->getBulgeAt(0,bulge)!=Acad::eOk||std::abs(bulge)>1e-6)continue;
  }
  const auto* curve=AcDbCurve::cast(e.get());
  AcGePoint3d a,b;
  if(!curve||curve->getStartPoint(a)!=Acad::eOk||curve->getEndPoint(b)!=Acad::eOk||a.distanceTo(b)<1e-4)continue;
  const bool vertical=std::abs(a.x-b.x)<1e-4;
  if(!vertical&&std::abs(a.y-b.y)>=1e-4)continue;
  const double x=vertical?a.y:a.x,y=vertical?b.y:b.x;
  lines.push_back({ascii(item.first),vertical,vertical?a.x:a.y,std::min(x,y),std::max(x,y)});
 }
 std::vector<ProductRoomBarrier> accepted;
 const auto centre=[](const ProductRoomName& name){
  return name.bounds.minPoint()+(name.bounds.maxPoint()-name.bounds.minPoint())*0.5;
 };
 for(const auto& name:names){
  if(name.role!=ProductRoomRole::excluded||name.text.find(L"上空")==std::wstring::npos)continue;
  const auto position=centre(name);
  const int region=grid.at(position);
  if(region<0)continue;
  std::vector<AcGePoint3d> occupied;
  for(const auto& other:names)
   if(productRoomHasArea(other.role)&&grid.at(centre(other))==region)occupied.push_back(centre(other));
  if(occupied.empty())continue;
  std::optional<ProductRoomBarrier> verified;
  std::optional<ProductRoomGrid> replacement;
  for(const auto& line:lines){
   const auto cell=[&](double along,double normal){return line.vertical?grid.at(normal,along):grid.at(along,normal);};
   const auto mid=(line.lo+line.hi)/2;
   constexpr double step=0.01;
   if(cell(line.lo-step,line.normal)!=ProductRoomGrid::blocked||
      cell(line.hi+step,line.normal)!=ProductRoomGrid::blocked||
      cell(mid,line.normal-step)!=region||cell(mid,line.normal+step)!=region)continue;
   auto candidates=accepted;
   candidates.push_back(line);
   auto trial=productRoomGrid(rectangles,candidates);
   if(!trial)continue;
   const int voidRegion=trial->at(position);
   if(voidRegion<0||!std::all_of(occupied.begin(),occupied.end(),[&](const auto& p){
    const int r=trial->at(p);return r>=0&&r!=voidRegion;
   }))continue;
   if(verified){verified.reset();replacement.reset();break;}
   verified=line;replacement=std::move(trial);
  }
  if(verified){accepted.push_back(*verified);grid=std::move(*replacement);}
 }
 return accepted;
}

struct ProductRoomLeaf {
 std::wstring text;
 AcDbObjectId style;
 double rotation{},height{};
 AcGePoint3d anchor;
 double width{};
};
void productRoomLeaves(const AcDbEntity& entity,std::vector<ProductRoomLeaf>& leaves,int depth=0) {
 if(depth>4)return;
 if(const auto* text=AcDbText::cast(&entity)){
  leaves.push_back({text->textString(),text->textStyle(),text->rotation(),text->height(),
   text->isDefaultAlignment()?text->position():text->alignmentPoint(),text->widthFactor()});
  return;
 }
 TemplateParts parts(&entity);
 if(entity.explode(parts.values)!=Acad::eOk)return;
 for(int i=0;i<parts.values.length();++i)
  if(const auto* child=AcDbEntity::cast(static_cast<AcRxObject*>(parts.values[i])))
   productRoomLeaves(*child,leaves,depth+1);
}

std::optional<std::wstring> productRoomFontFile(AcDbDatabase& db,const AcString& file,bool allowConfiguredPath=true) {
 if(file.isEmpty())return std::wstring{};
 const fs::path persisted=file.kACharPtr();
 const auto basename=persisted.filename();
 const auto regular=[](const fs::path& p){std::error_code error;return fs::is_regular_file(p,error);};
 if(persisted.is_absolute()&&regular(persisted))return persisted.wstring();
 if(productDimensionFontAvailable(db,basename.wstring()))return basename.wstring();
 // A saved drawing can already carry a valid absolute path while the isolated
 // host does not expose that directory in its CAD font search list.
 {const auto table=open<AcDbTextStyleTable>(db.textStyleTableId());AcDbObjectId existing;
  if(table->getAt(L"FY_HZ",existing)==Acad::eOk){const auto style=open<AcDbTextStyleTableRecord>(existing);
   AcString primary,big;style->fileName(primary);style->bigFontFileName(big);
   for(const auto& candidate:{primary,big}){const fs::path path=candidate.kACharPtr();
    if(path.is_absolute()&&_wcsicmp(path.filename().c_str(),basename.c_str())==0&&regular(path))return path.wstring();}
  }}
 if(!allowConfiguredPath)return {};
 const fs::path folder=productSetting(L"roomFontDirectory");
 if(folder.is_absolute()&&regular(folder/basename))return (folder/basename).wstring();
 return {};
}

AcDbObjectId productRoomImportStyle(AcDbDatabase& donor,AcDbObjectId source,AcDbDatabase& target) {
 const auto findStyle=[](AcDbDatabase& db,const wchar_t* name){
  AcDbObjectId style;
  const auto table=open<AcDbTextStyleTable>(db.textStyleTableId());
  table->getAt(name,style);
  return style;
 };
 const auto original=open<AcDbTextStyleTableRecord>(source);
 AcString name,font,big;
 if(original->getName(name)!=Acad::eOk||original->fileName(font)!=Acad::eOk||
    original->bigFontFileName(big)!=Acad::eOk)return {};
 const auto primary=productRoomFontFile(target,font),secondary=productRoomFontFile(target,big);
 if(!primary||!secondary)return {};
 auto current=findStyle(target,name.kACharPtr());
 const bool isNew=current.isNull();
 if(current.isNull()){
  AcDbObjectIdArray ids;ids.append(source);AcDbIdMapping mapping;
  if(donor.wblockCloneObjects(ids,target.textStyleTableId(),mapping,AcDb::kDrcIgnore,false)!=Acad::eOk)return {};
  current=findStyle(target,name.kACharPtr());
 }
 if(current.isNull())return {};
 // A source drawing may redefine FY_HZ. Restore the approved definition instead
 // of treating the existing name as authoritative or abandoning normalization.
 if(std::wstring(name.kACharPtr())==L"FY_HZ"){
  auto style=editObject<AcDbTextStyleTableRecord>(current);
  productStatus(style->setFileName(primary->c_str()),"restore approved FY_HZ primary font");
  productStatus(style->setBigFontFileName(secondary->c_str()),"restore approved FY_HZ big font");
  productStatus(style->setXScale(original->xScale()),"restore approved FY_HZ width");
  productStatus(style->setTextSize(original->textSize()),"restore approved FY_HZ fixed height");
  productStatus(style->setObliquingAngle(original->obliquingAngle()),"restore approved FY_HZ oblique");
  style->setFlagBits(original->flagBits());style->setIsVertical(original->isVertical());
 }
 AcString currentPrimary,currentSecondary;
 {
  const auto style=open<AcDbTextStyleTableRecord>(current);
  const auto base=[](const AcString& file){return fs::path(file.kACharPtr()).filename().wstring();};
  if(style->fileName(currentPrimary)!=Acad::eOk||style->bigFontFileName(currentSecondary)!=Acad::eOk||
     _wcsicmp(base(currentPrimary).c_str(),base(font).c_str())!=0||
     _wcsicmp(base(currentSecondary).c_str(),base(big).c_str())!=0||
     std::abs(style->xScale()-original->xScale())>1e-6)return {};
 }
 if(!isNew&&std::wstring(name.kACharPtr())==L"FY_HZ"){
  if(!productRoomFontFile(target,currentPrimary,false)||!productRoomFontFile(target,currentSecondary,false))return {};
  return current;
 }
 // Keep portable font names when AutoCAD already resolves them. Use the
 // verified absolute path only when the host's own search does not find it.
 if(*primary!=currentPrimary.kACharPtr()||*secondary!=currentSecondary.kACharPtr()){
  auto style=editObject<AcDbTextStyleTableRecord>(current);
  if((!primary->empty()&&style->setFileName(primary->c_str())!=Acad::eOk)||
     (!secondary->empty()&&style->setBigFontFileName(secondary->c_str())!=Acad::eOk)){
   if(isNew)style->erase();
   return {};
  }
 }
 return current;
}

AcDbObjectId productRoomVerifiedStyle(AcDbDatabase& db,bool checkFontFiles) {
 AcDbObjectId id;
 {
  const auto table=open<AcDbTextStyleTable>(db.textStyleTableId());
  if(table->getAt(L"FY_HZ",id)!=Acad::eOk)return {};
 }
 const auto style=open<AcDbTextStyleTableRecord>(id);
 AcString font,big;
 if(style->fileName(font)!=Acad::eOk||style->bigFontFileName(big)!=Acad::eOk||
   _wcsicmp(fs::path(font.kACharPtr()).filename().c_str(),L"tssdeng.shx")!=0||
   _wcsicmp(fs::path(big.kACharPtr()).filename().c_str(),L"hztxt.shx")!=0||std::abs(style->xScale()-0.7)>1e-6)return {};
 if(checkFontFiles&&(!productRoomFontFile(db,font,false)||!productRoomFontFile(db,big,false)))return {};
 return id;
}

AcDbObjectId productRoomStyle(AcDbDatabase& donor,AcDbDatabase& target) {
 const auto source=productRoomVerifiedStyle(donor,false);
 return source.isNull()?AcDbObjectId{}:productRoomImportStyle(donor,source,target);
}

bool productRoomRestyle(AcDbDatabase& db,ProductRoomName& room,AcDbObjectId style,AcDbObjectId layer,
                        const wchar_t* styleName=L"FY_HZ",bool preserveAppearance=false,
                        AcDbDatabase* donor=nullptr,AcDbObjectId donorText={}) {
 if(style.isNull())return false;
 const auto id=find(db,room.handle);
 const auto expected=[&](AcDbObjectId target){
  Working targetContext(&db);
  const auto object=open<AcDbEntity>(target);
  if(std::wstring(object->isA()->name())!=L"TDbText")return false;
  std::vector<ProductRoomLeaf> leaves;productRoomLeaves(*object,leaves);
  std::wstring actual;
  for(const auto& leaf:leaves){if(leaf.style!=style)return false;actual+=leaf.text;}
  return actual==room.text;
 };
 AcDbObjectId candidate;
 try{
  if(expected(id)){
   if(preserveAppearance)return true;
   auto object=editObject<AcDbEntity>(id);
   return object->setLayer(layer)==Acad::eOk&&object->setColorIndex(256)==Acad::eOk;
  }
  AcDbDatabase isolated(true,true);
  const auto source=donor?donorText:id;
  auto& sourceDb=donor?*donor:db;
  AcDbObjectIdArray sourceObjects;
  sourceObjects.append(source);
  AcDbIdMapping isolatedMap;
  if(sourceDb.wblockCloneObjects(sourceObjects,modelId(isolated),isolatedMap,AcDb::kDrcMangleName,false)!=Acad::eOk)
   return false;
  AcDbIdPair isolatedText(source,AcDbObjectId{},false);
  if(!isolatedMap.compute(isolatedText)||!isolatedText.isCloned())return false;
  const auto copy=isolatedText.value();
  std::set<AcDbObjectId> originals;
  {
   Working side(&isolated);
   const auto object=open<AcDbEntity>(copy);
   std::vector<ProductRoomLeaf> leaves;productRoomLeaves(*object,leaves);
   if(leaves.empty())return false;
   for(const auto& leaf:leaves){
    if(leaf.style.isNull()||leaf.style.database()!=&isolated)return false;
    originals.insert(leaf.style);
   }
   if(!preserveAppearance&&originals.size()!=1)return false;
  }
  bool renamed=false;
  if(originals.size()==1){
   const auto local=editObject<AcDbTextStyleTableRecord>(*originals.begin());
   renamed=local->setName(styleName)==Acad::eOk;
  }
  if(renamed)candidate=cloneOne(isolated,copy,db);
  else{
   AcDbObjectIdArray objects;
   objects.append(copy);
   AcDbIdMapping map;
   for(const auto& original:originals)map.assign(AcDbIdPair(original,style,false));
   if(isolated.wblockCloneObjects(objects,modelId(db),map,AcDb::kDrcIgnore,false)!=Acad::eOk)return false;
   AcDbIdPair translated(copy,AcDbObjectId{},false);
   if(!map.compute(translated)||!translated.isCloned())return false;
   candidate=translated.value();
  }
  if(!expected(candidate)){editObject<AcDbEntity>(candidate)->erase();return false;}
  const auto sourcePoints=chainGrips(id),targetPoints=chainGrips(candidate);
  if(sourcePoints.empty()||targetPoints.empty()){editObject<AcDbEntity>(candidate)->erase();return false;}
  if(donor) {
   wholeTranslate(candidate,sourcePoints.front()-targetPoints.front());
  }
  if(!preserveAppearance){
   std::vector<ProductRoomLeaf> sourceLeaves,targetLeaves;
   productRoomLeaves(*open<AcDbEntity>(id),sourceLeaves);
   productRoomLeaves(*open<AcDbEntity>(candidate),targetLeaves);
   if(sourceLeaves.size()==1&&targetLeaves.size()==1&&sourceLeaves.front().height>0&&targetLeaves.front().height>0){
    const double factor=sourceLeaves.front().height/targetLeaves.front().height;
    if(std::abs(factor-1.)>1e-8){
     productStatus(editObject<AcDbEntity>(candidate)->transformBy(AcGeMatrix3d::scaling(factor,sourcePoints.front())),
      "match native room name to source glyph height");
     targetLeaves.clear();
     productRoomLeaves(*open<AcDbEntity>(candidate),targetLeaves);
    }
   }
   if(sourceLeaves.size()!=targetLeaves.size()||
      !std::equal(sourceLeaves.begin(),sourceLeaves.end(),targetLeaves.begin(),[](const auto& a,const auto& b){
       return std::abs(a.height-b.height)<=1e-5;
      })) {editObject<AcDbEntity>(candidate)->erase();return false;}
  }
  auto converted=editObject<AcDbEntity>(candidate);
  AcDbExtents box;
  if(converted->setLayer(layer)!=Acad::eOk||(!preserveAppearance&&converted->setColorIndex(256)!=Acad::eOk)||
     converted->getGeomExtents(box)!=Acad::eOk){converted->erase();return false;}
  converted.reset();
  const auto originalGrips=chainGrips(id),convertedGrips=chainGrips(candidate);
  if(originalGrips.empty()||convertedGrips.empty()||originalGrips.front().distanceTo(convertedGrips.front())>1e-5){
   editObject<AcDbEntity>(candidate)->erase();
   return false;
  }
  if(editObject<AcDbEntity>(id)->erase()!=Acad::eOk){editObject<AcDbEntity>(candidate)->erase();return false;}
  room.handle=axisHandle(candidate);
  room.bounds=box;
  return true;
 }catch(const std::exception&){
  if(!candidate.isNull()){try{editObject<AcDbEntity>(candidate)->erase();}catch(const std::exception&){}}
  return false;
 }
}

bool productRoomConvertWithTangent(ProductJob& job,AcDbDatabase& db,ProductRoomName& room,
                                  AcDbObjectId style,AcDbObjectId layer){
 if(!job.allowRoomTextConversion||style.isNull()||!curDoc()||curDoc()!=job.ownedDocument||
    curDoc()->database()!=&db||!job.validation)return false;
 const auto nonce=ini(iniPath(),L"nonce");
 if(nonce.empty())return false;
 isolated(nonce);
 const auto original=find(db,room.handle);
 const auto sourcePoints=chainGrips(original);
 if(sourcePoints.empty())return false;
 std::vector<ProductRoomLeaf> sourceLeaves;
 productRoomLeaves(*open<AcDbEntity>(original),sourceLeaves);
 if(sourceLeaves.size()!=1||sourceLeaves.front().text!=room.text||
    !std::isfinite(sourceLeaves.front().height)||sourceLeaves.front().height<=0.)return false;
 if(!acedRegCmds->lookupGlobalCmd(L"TTextConv")){
  const auto folder=fs::path(productSetting(L"tangentDirectory",L"C:\\Tangent\\TArchT20V8\\SYS23x64"));
  if(!acrxDynamicLinker->loadModule((folder/L"tch_kerncmd.arx").c_str(),false,false))return false;
 }
 if(!acedRegCmds->lookupGlobalCmd(L"TTextConv"))return false;
 const auto before=wholeModelIds(db);
 auto text=std::make_unique<AcDbText>();
 text->setDatabaseDefaults(&db);
 const auto position=sourcePoints.front();
 const double height=sourceLeaves.front().height;
 if(text->setPosition(position)!=Acad::eOk||text->setTextStyle(style)!=Acad::eOk||
    text->setTextString(room.text.c_str())!=Acad::eOk||text->setHeight(height)!=Acad::eOk||
    text->setLayer(layer)!=Acad::eOk)return false;
 AcDbObjectId seed;
 {
  const auto model=editObject<AcDbBlockTableRecord>(modelId(db));
  if(model->appendAcDbEntity(seed,text.get())!=Acad::eOk)return false;
  text.release()->close();
 }
 const auto previousStyle=db.textstyle();
 const auto command=[&]{
  if(db.setTextstyle(style)!=Acad::eOk)return RTERROR;
  struct RestoreTextStyle {
   AcDbDatabase& db;
   AcDbObjectId style;
   ~RestoreTextStyle(){db.setTextstyle(style);}
  } restore{db,previousStyle};
  WholeSelection selected(seed);
  const auto result=acedCommandS(RTSTR,L"TTextConv",RTPICKS,selected.value,RTSTR,L"",RTNONE);
  if(result==RTNORM)++job.planPublicCommands[L"TTextConv"];
  return result;
 }();
 std::vector<AcDbObjectId> added;
 for(const auto id:wholeModelIds(db))if(!before.count(id))added.push_back(id);
 AcDbObjectId candidate;
 if(command==RTNORM)for(const auto id:added){
  const auto entity=open<AcDbEntity>(id);
  if(std::wstring(entity->isA()->name())!=L"TDbText"||entity->isAProxy())continue;
  if(!candidate.isNull()){candidate=AcDbObjectId{};break;}
  candidate=id;
 }
 for(const auto id:added)if(id!=candidate)status(editObject<AcDbEntity>(id)->erase(),"remove room conversion scratch object");
 if(candidate.isNull())return false;
 std::vector<ProductRoomLeaf> leaves;
 {
  const auto entity=open<AcDbEntity>(candidate);
  productRoomLeaves(*entity,leaves);
 }
 const auto grips=chainGrips(candidate);
 if(leaves.size()!=1||leaves.front().text!=room.text||leaves.front().style!=style||grips.empty()||
    !std::isfinite(leaves.front().height)||leaves.front().height<=0.){
  const auto actual=leaves.empty()||leaves.front().style.isNull()?L"":symbolName(leaves.front().style);
  productRecordRuleEvidence(job,"N05","{\"object\":\""+room.handle+
    "\",\"result\":\"pending-public-text-style\",\"actualStyle\":"+jsonUtf8(actual)+"}");
  status(editObject<AcDbEntity>(candidate)->erase(),"remove rejected room conversion");
  return false;
 }
 wholeTranslate(candidate,position-grips.front());
 const auto entity=editObject<AcDbEntity>(candidate);
 const auto factor=height/leaves.front().height;
 AcDbExtents box;
 if((std::abs(factor-1.)>1e-8&&entity->transformBy(AcGeMatrix3d::scaling(factor,position))!=Acad::eOk)||
    entity->setLayer(layer)!=Acad::eOk||entity->setColorIndex(256)!=Acad::eOk||
    entity->getGeomExtents(box)!=Acad::eOk){
  status(entity->erase(),"remove rejected room presentation");
  return false;
 }
 status(editObject<AcDbEntity>(original)->erase(),"replace original room name after native validation");
 room.handle=axisHandle(candidate);
 room.bounds=box;
 return true;
}

bool productRoomAreaText(const std::wstring& value) {
 const auto m=value.find(L'm');
 if(m==std::wstring::npos||m==0||value.front()<L'0'||value.front()>L'9')return false;
 const auto suffix=value.substr(m);
 return value.find_first_not_of(L"0123456789.",0)==m&&
  (suffix==L"m"||suffix==L"m²"||suffix==L"m^U2^U");
}

struct ProductOldNetArea {
 std::string body,exponent;
 double value{};
 int region{ProductRoomGrid::outside};
 bool explicitUnit{};
};

std::vector<ProductOldNetArea> productOldNetAreas(AcDbDatabase& db,const ProductRoomGrid& grid,
 const std::vector<bcpproduct::GenericView>& views,std::size_t view){
 struct Candidate {std::string handle;std::wstring text;AcDbExtents box;AcDbObjectId layer,style;double height{};};
 std::vector<Candidate> bodies,exponents;
 for(const auto& [handle,type]:modelTypes(db)){
  if(type!=L"TDbText"&&type!=L"AcDbText"&&type!=L"AcDbMText")continue;
  const auto entity=open<AcDbEntity>(find(db,ascii(handle)));
  if(!productPlanDimensionVisible(*entity))continue;
  Candidate item;
  item.handle=ascii(handle);
  item.layer=entity->layerId();
  if(entity->getGeomExtents(item.box)!=Acad::eOk)continue;
  const auto center=item.box.minPoint()+(item.box.maxPoint()-item.box.minPoint())*.5;
  if(bcpproduct::nearestGenericView(views,{center.x,center.y})!=view)continue;
  if(const auto* text=AcDbText::cast(entity.get())){
   item.text=text->textString();item.style=text->textStyle();item.height=text->height();
  }else if(const auto* multiple=AcDbMText::cast(entity.get())){
   AcString plain;if(multiple->text(plain)==Acad::eOk)item.text=plain.kACharPtr();
  }else{
   std::vector<ProductRoomLeaf> leaves;productRoomLeaves(*entity,leaves);
   for(const auto& leaf:leaves)item.text+=leaf.text;
  }
  if(productRoomAreaText(item.text))bodies.push_back(std::move(item));
  else if(item.text==L"2"&&item.height>0.)exponents.push_back(std::move(item));
 }
 std::vector<ProductOldNetArea> result;
 for(const auto& body:bodies){
  wchar_t* end=nullptr;
  const double value=std::wcstod(body.text.c_str(),&end);
  if(!std::isfinite(value)||value<=0||end!=body.text.c_str()+body.text.find(L'm'))continue;
  const auto center=body.box.minPoint()+(body.box.maxPoint()-body.box.minPoint())*.5;
  ProductOldNetArea old{body.handle,{},value,grid.at(center),body.text.back()!=L'm'};
  if(!old.explicitUnit&&body.height>0.){
   std::vector<std::string> matched;
   for(const auto& exponent:exponents){
    const auto gap=exponent.box.minPoint().x-body.box.maxPoint().x;
    const auto rise=exponent.box.minPoint().y-body.box.minPoint().y;
    if(exponent.layer==body.layer&&exponent.style==body.style&&std::abs(body.height-2*exponent.height)<1e-5&&
       std::abs(gap)<body.height/2&&rise>0.&&rise<body.height)matched.push_back(exponent.handle);
   }
   if(matched.size()==1){old.exponent=matched.front();old.explicitUnit=true;}
  }
  result.push_back(std::move(old));
 }
 return result;
}

struct ProductPlanNameCheck {
 std::string source,output;
 std::wstring text,type;
 std::optional<AcGePoint3d> anchor,expectedAnchor;
 std::vector<double> sourceGlyphHeights;
 std::size_t view{};
 int panel{};
 std::string pendingReason,conversionError;
};

std::optional<AcGePoint3d> productPlanNameAnchor(const AcDbEntity& entity) {
 if(const auto* text=AcDbText::cast(&entity))return text->isDefaultAlignment()?text->position():text->alignmentPoint();
 if(const auto* text=AcDbMText::cast(&entity))return text->location();
 AcGePoint3dArray points;
 AcDbIntArray modes,ids;
 if(entity.getGripPoints(points,modes,ids)!=Acad::eOk||points.isEmpty())return {};
 return points[0];
}

std::wstring productPlanNameContents(const AcDbEntity& entity) {
 if(const auto* text=AcDbText::cast(&entity))return text->textString();
 if(const auto* text=AcDbMText::cast(&entity)){
  AcString plain;
  if(text->text(plain)==Acad::eOk)return plain.kACharPtr();
  return {};
 }
 std::vector<ProductRoomLeaf> leaves;
 productRoomLeaves(entity,leaves);
 std::wstring contents;
 for(const auto& leaf:leaves)contents+=leaf.text;
 return contents;
}

bool productPlanShortName(const std::wstring& text) {
 const auto first=text.find_first_not_of(L" 　");
 return first!=std::wstring::npos&&text.size()<=12&&text[first]>=L'\u4e00'&&text[first]<=L'\u9fff'&&
  std::all_of(text.begin(),text.end(),[](wchar_t c){return (c>=L'\u4e00'&&c<=L'\u9fff')||(c>=L'0'&&c<=L'9')||c==L' '||c==L'　';});
}

bool productPlanKnownName(const std::wstring& text) {
 if(!productPlanShortName(text))return false;
 const auto base=productRoomBaseName(text);
 const auto voidSuffix=base.find(L"上空");
 if(voidSuffix==std::wstring::npos)
  return productRoomRole(text)!=ProductRoomRole::other||base==L"棋牌"||base==L"洗衣区"||base==L"尊亲房";
 // The area classifier also uses substring matches to exclude open voids.
 // Name styling needs a standalone label such as 客厅上空 or 楼梯上空.
 return voidSuffix+2==base.size()&&
  (voidSuffix==0||productRoomRole(base.substr(0,voidSuffix))!=ProductRoomRole::other);
}

bool productPlanNameLooksLikeNote(const std::wstring& text) {
 const auto base=productRoomBaseName(text);
 const auto voidWord=base.find(L"上空");
 const auto first=text.find_first_not_of(L" 　");
 const bool preserveInstruction=first!=std::wstring::npos&&text.compare(first,2,L"保留")==0;
 return base==L"神龛"||base==L"坡檐"||
  (voidWord!=std::wstring::npos&&(voidWord+2!=base.size()||preserveInstruction))||
  text.find(L"按详图")!=std::wstring::npos||text.find(L"参见")!=std::wstring::npos;
}

std::vector<ProductPlanNameCheck> productPlanNameInventory(AcDbDatabase& db,const GenericNativeInput& input,
 const std::vector<bcpproduct::GenericView>& views,const std::map<std::wstring,std::wstring>& roots,
 bool standalone=false) {
 std::vector<ProductPlanNameCheck> checks;
 struct AreaBody { AcDbExtents box; AcDbObjectId layer,style; double height; };
 std::vector<AreaBody> areaBodies;
 if(standalone)for(const auto& [handle,type]:roots){
  if(type!=L"AcDbText")continue;
  const auto text=open<AcDbText>(find(db,ascii(handle)));
  const std::wstring value=text->textString();
  AcDbExtents box;
  if(productPlanDimensionVisible(*text)&&productRoomAreaText(value)&&value.back()==L'm'&&
     text->getGeomExtents(box)==Acad::eOk)
   areaBodies.push_back({box,text->layerId(),text->textStyle(),text->height()});
 }
 const auto areaExponent=[&](const AcDbEntity& entity,const std::wstring& value){
  const auto* text=AcDbText::cast(&entity);
  AcDbExtents box;
  if(value!=L"2"||!text||text->getGeomExtents(box)!=Acad::eOk)return false;
  return std::any_of(areaBodies.begin(),areaBodies.end(),[&](const auto& body){
   const double gap=box.minPoint().x-body.box.maxPoint().x;
   const double rise=box.minPoint().y-body.box.minPoint().y;
   return body.layer==text->layerId()&&body.style==text->textStyle()&&
    std::abs(body.height-2*text->height())<1e-5&&std::abs(gap)<body.height/2&&rise>0&&rise<body.height;
  });
 };
 const auto candidate=[&](const AcDbEntity& entity,const std::string& source,bool native,bool attribute,bool shown,
  const AcGeMatrix3d* placement=nullptr){
  const auto layer=open<AcDbLayerTableRecord>(entity.layerId());
  if(!shown||entity.visibility()!=AcDb::kVisible||layer->isOff()||layer->isFrozen())return;
  const auto layerName=symbolName(entity.layerId());
  const bool nameLayer=layerName==L"A-房间名称"||layerName==L"房间名称";
  const bool publicText=layerName==L"PUB_TEXT"||layerName==L"A-PUB_TEXT";
  if(!nameLayer&&(layerName==L"AXIS"||layerName==L"WINDOW_TEXT"||layerName==L"PUB_DIM"||layerName==L"A-PUB_DIM"||
    layerName.find(L"DIM_")!=std::wstring::npos||layerName.find(L"说明")!=std::wstring::npos||
    layerName.find(L"备注")!=std::wstring::npos||layerName.find(L"NOTE")!=std::wstring::npos))return;
  if(!standalone&&!native&&!nameLayer&&!publicText)return;
  std::wstring text;
  bool recognizedNative=false;
  if(native){
   const auto properties=input.properties.find(source);
   if(properties!=input.properties.end()){
    const auto type=properties->second.find(L"DXF名称"),contents=properties->second.find(L"文字内容");
    if(type!=properties->second.end()&&type->second==L"TCH_TEXT"&&contents!=properties->second.end()){
     text=contents->second;
     recognizedNative=productPlanKnownName(text);
    }
   }
   if(!recognizedNative&&!nameLayer&&!publicText)return;
  }
  if(text.empty())text=productPlanNameContents(entity);
  if(productRoomAreaText(text)||areaExponent(entity,text)||text.find(L"平面图")!=std::wstring::npos||productPlanNameLooksLikeNote(text))return;
  if(standalone&&!native&&!nameLayer&&!publicText&&!productPlanKnownName(text))return;
  if(!recognizedNative&&!nameLayer&&!productPlanShortName(text))return;

  ProductPlanNameCheck check;
  check.source=check.output=source;
  check.type=entity.isA()->name();
  check.text=std::move(text);
  {
   std::vector<ProductRoomLeaf> sourceLeaves;
   productRoomLeaves(entity,sourceLeaves);
   for(const auto& leaf:sourceLeaves)check.sourceGlyphHeights.push_back(leaf.height);
  }
  check.anchor=check.expectedAnchor=productPlanNameAnchor(entity);
  check.view=views.size();
  AcDbExtents box;
  const bool bounded=entity.getGeomExtents(box)==Acad::eOk;
  if(placement){
   if(check.anchor)check.anchor->transformBy(*placement);
   check.expectedAnchor=check.anchor;
   if(bounded)box.transformBy(*placement);
  }
  if(bounded||check.anchor){
   const auto at=bounded?box.minPoint()+(box.maxPoint()-box.minPoint())*0.5:*check.anchor;
   const bcp::Point point{at.x,at.y};
   const auto view=bcpproduct::nearestGenericView(views,point);
   const auto distance=bcpproduct::genericDistanceSquared(views[view].bounds,point);
   std::size_t ties=0;
   for(const auto& other:views)if(std::abs(bcpproduct::genericDistanceSquared(other.bounds,point)-distance)<1e-5)++ties;
   if(ties==1)check.view=view;
   if(!recognizedNative&&!nameLayer&&distance>1e-5)return;
  }
  if(check.view==views.size()||!bounded||!check.anchor)check.pendingReason=u8"未能唯一确认所属平面或读取文字定位";
  else if(check.text.empty())check.pendingReason=u8"名称层对象的文字内容未读出";
  else if(placement)check.pendingReason=u8"块内文字疑似名称，按实例路径核对可见性、用途和独立替换方式";
  else if(attribute)check.pendingReason=u8"块实例属性疑似名称，保留该实例和共享块定义，待核对用途及转换方式";
  else if(!native){
   if(!(standalone&&AcDbText::cast(&entity)&&productPlanKnownName(check.text)))
    check.pendingReason=u8"普通单行或多行文字疑似名称，保留原文，待核对用途及原生转换方式";
  }
  else if(!recognizedNative&&(!nameLayer||!productPlanShortName(check.text)))check.pendingReason=u8"图内短文字或名称层文字用途待核对，保留原文";
  checks.push_back(std::move(check));
 };
 std::set<AcDbObjectId> blockPath;
 const auto nested=[&](auto&& visit,const AcDbBlockReference& block,const std::string& path,
  const AcGeMatrix3d& world)->void{
  const auto definition=block.blockTableRecord();
  if(definition.isNull()||blockPath.size()>=32||!blockPath.insert(definition).second)return;
  const auto record=open<AcDbBlockTableRecord>(definition);
  AcDbBlockTableRecordIterator* raw{};
  status(record->newIterator(raw),"read nested plan names");
  std::unique_ptr<AcDbBlockTableRecordIterator> it(raw);
  for(;!it->done();it->step()){
   AcDbObjectId id;
   status(it->getEntityId(id),"read nested plan member");
   const auto entity=open<AcDbEntity>(id);
   if(!productPlanDimensionVisible(*entity))continue;
   const auto childPath=path+"/"+axisHandle(id);
   if(const auto* ref=AcDbBlockReference::cast(entity.get())){
    std::unique_ptr<AcDbObjectIterator> attrs(ref->attributeIterator());
    for(;attrs&&!attrs->done();attrs->step()){
     const auto attr=open<AcDbAttribute>(attrs->objectId());
     if(!attr->isInvisible())candidate(*attr,childPath+"/"+axisHandle(attrs->objectId()),false,true,true,&world);
    }
    visit(visit,*ref,childPath,world*ref->blockTransform());
   }else if(!AcDbAttributeDefinition::cast(entity.get())&&
     (AcDbText::cast(entity.get())||AcDbMText::cast(entity.get())||std::wstring(entity->isA()->name())==L"TDbText"))
    candidate(*entity,childPath,false,false,true,&world);
  }
  blockPath.erase(definition);
 };
 for(const auto& [handle,type]:roots){
  if(type!=L"TDbText"&&type!=L"AcDbText"&&type!=L"AcDbMText"&&type!=L"AcDbBlockReference")continue;
  const auto source=ascii(handle);
  const auto entity=open<AcDbEntity>(find(db,source));
  if(const auto* block=AcDbBlockReference::cast(entity.get())){
   const auto layer=open<AcDbLayerTableRecord>(entity->layerId());
   const bool shown=entity->visibility()==AcDb::kVisible&&!layer->isOff()&&!layer->isFrozen();
   std::unique_ptr<AcDbObjectIterator> attributes(block->attributeIterator());
   for(;attributes&&!attributes->done();attributes->step()){
    const auto id=attributes->objectId();
    const auto attribute=open<AcDbAttribute>(id);
    if(!attribute->isInvisible())candidate(*attribute,axisHandle(id),false,true,shown);
   }
   if(standalone&&shown)nested(nested,*block,source,block->blockTransform());
  }else candidate(*entity,source,type==L"TDbText",false,true);
 }
 return checks;
}

using ProductOutdoorEdge=std::pair<AcGePoint3d,AcGePoint3d>;
std::optional<ProductOutdoorEdge> productOutdoorEdge(AcDbDatabase& db,const std::string& reference,double tolerance){
 const auto separator=reference.find(':');
 const auto entity=open<AcDbEntity>(find(db,reference.substr(0,separator)));
 if(!productPlanDimensionVisible(*entity))return {};
 AcGePoint3d first,last;
 if(const auto* line=AcDbLine::cast(entity.get());line&&separator==std::string::npos){
  first=line->startPoint();
  last=line->endPoint();
 }else if(const auto* polyline=AcDbPolyline::cast(entity.get())){
  unsigned start=0,end=1;
  if(separator==std::string::npos){
   if(polyline->numVerts()!=2||polyline->isClosed())return {};
  }else{
   if(!polyline->isClosed()||polyline->numVerts()<4)return {};
   const auto number=reference.substr(separator+1);
   std::size_t consumed{};
   const auto segment=std::stoul(number,&consumed);
   if(consumed!=number.size()||segment==0||segment>polyline->numVerts())return {};
   start=static_cast<unsigned>(segment-1);
   end=static_cast<unsigned>(segment%polyline->numVerts());
  }
  double bulge{};
  if(polyline->getBulgeAt(start,bulge)!=Acad::eOk||std::abs(bulge)>1e-8||
     polyline->getPointAt(start,first)!=Acad::eOk||polyline->getPointAt(end,last)!=Acad::eOk)return {};
 }else return {};
 const bool vertical=std::abs(first.x-last.x)<=tolerance;
 const bool horizontal=std::abs(first.y-last.y)<=tolerance;
 if(vertical==horizontal||std::abs(first.z-last.z)>tolerance)return {};
 return ProductOutdoorEdge{first,last};
}

bool productOutdoorNameInLoop(std::array<ProductOutdoorEdge,4> edges,const AcGePoint3d& anchor,double tolerance){
 std::vector<AcGePoint2d> outline;
 auto edge=edges.front();
 for(std::size_t i=0;i<edges.size();++i){
  outline.emplace_back(edge.first.x,edge.first.y);
  if(i+1==edges.size()){
   if(edge.second.distanceTo(edges.front().first)>tolerance)return false;
   break;
  }
  auto next=std::find_if(edges.begin()+i+1,edges.end(),[&](const auto& candidate){
   return edge.second.distanceTo(candidate.first)<=tolerance||edge.second.distanceTo(candidate.second)<=tolerance;
  });
  if(next==edges.end())return false;
  std::iter_swap(edges.begin()+i+1,next);
  auto joined=edges[i+1];
  if(edge.second.distanceTo(joined.first)>tolerance)std::swap(joined.first,joined.second);
  if(std::abs((edge.second-edge.first).normal().dotProduct((joined.second-joined.first).normal()))>1e-6)
   return false;
  edge=joined;
 }
 return genericInside(outline,{anchor.x,anchor.y});
}

bool productConfirmedExteriorName(AcDbDatabase& db,const std::array<std::string,4>& handles,
 const AcGePoint3d& anchor,double tolerance){
 std::array<ProductOutdoorEdge,4> edges;
 for(std::size_t i=0;i<handles.size();++i){
  const auto edge=productOutdoorEdge(db,handles[i],tolerance);
  if(!edge)return false;
  edges[i]=*edge;
 }
 return productOutdoorNameInLoop(edges,anchor,tolerance);
}

bool productConfirmedExteriorProjection(AcDbDatabase& db,const ProductPlanDetailConfirmations::ExteriorProjection& projection,
 const GenericNativeInput& input,const AcGePoint3d& anchor,double tolerance){
 std::array<ProductOutdoorEdge,4> edges;
 for(std::size_t i=0;i<projection.edges.size();++i){
  const auto edge=productOutdoorEdge(db,projection.edges[i],tolerance);
  if(!edge)return false;
  edges[i]=*edge;
 }
 std::vector<AcGePoint3d> ends;
 for(std::size_t i=0;i<projection.edges.size();++i){
  for(const auto& endpoint:{edges[i].first,edges[i].second}){
   bool joined=false;
   for(std::size_t j=0;j<projection.edges.size();++j){
    if(i==j)continue;
    if(endpoint.distanceTo(edges[j].first)<=tolerance||endpoint.distanceTo(edges[j].second)<=tolerance)
     joined=true;
   }
   if(!joined)ends.push_back(endpoint);
  }
 }
 if(ends.size()!=2)return false;
 const bool vertical=std::abs(ends[0].x-ends[1].x)<=tolerance;
 const bool horizontal=std::abs(ends[0].y-ends[1].y)<=tolerance;
 if(vertical==horizontal)return false;
 const auto meetsWallFace=[&](const std::string& handle,const AcGePoint3d& endpoint){
  const auto wall=std::find_if(input.walls.begin(),input.walls.end(),[&](const auto& candidate){
   return candidate.wall.handle==handle&&candidate.orthogonal&&!candidate.low&&
    (std::abs(candidate.wall.start.x-candidate.wall.end.x)<=tolerance)==vertical;
  });
  if(wall==input.walls.end())return false;
  const auto faces=approvedRect(wall->wall);
  const double along=vertical?endpoint.y:endpoint.x;
  const double normal=vertical?endpoint.x:endpoint.y;
  const auto low=vertical?faces[1]:faces[0],high=vertical?faces[3]:faces[2];
  const auto faceA=vertical?faces[0]:faces[1],faceB=vertical?faces[2]:faces[3];
  return along>=low-tolerance&&along<=high+tolerance&&
   (std::abs(normal-faceA)<=tolerance||std::abs(normal-faceB)<=tolerance);
 };
 const auto matches=[&](std::size_t first,std::size_t second){
  return meetsWallFace(projection.walls[0],ends[first])&&meetsWallFace(projection.walls[1],ends[second]);
 };
 if(!matches(0,1)&&!matches(1,0))return false;
 edges[3]={ends[0],ends[1]};
 return productOutdoorNameInLoop(edges,anchor,tolerance);
}

std::optional<std::string> productPorchNameOutline(AcDbDatabase& db,const GenericNativeInput& input,
 const bcpproduct::GenericView& view,const AcGePoint3d& anchor,double tolerance){
 const auto supportingWalls=[&](const AcGePoint3d& point){
  std::set<std::string> matches;
  for(const auto index:view.walls){
   const auto& wall=input.walls[index];
   if(wall.low||!wall.orthogonal)continue;
   const auto box=approvedRect(wall.wall);
   const bool withinX=point.x>=box[0]-tolerance&&point.x<=box[2]+tolerance;
   const bool withinY=point.y>=box[1]-tolerance&&point.y<=box[3]+tolerance;
   const bool onVertical=std::abs(point.x-box[0])<=tolerance||std::abs(point.x-box[2])<=tolerance;
   const bool onHorizontal=std::abs(point.y-box[1])<=tolerance||std::abs(point.y-box[3])<=tolerance;
   if(withinX&&withinY&&(onVertical||onHorizontal))matches.insert(wall.wall.handle);
  }
  return matches;
 };
 std::optional<std::string> outline;
 for(const auto& [raw,type]:modelTypes(db)){
  if(type!=L"AcDbPolyline")continue;
  const auto handle=ascii(raw);
  const auto polyline=open<AcDbPolyline>(find(db,handle));
  if(polyline->isClosed()||polyline->numVerts()!=4||!productPlanDimensionVisible(*polyline))continue;
  const auto layer=symbolName(polyline->layerId());
  if(layer.find(L"屋顶")==std::wstring::npos&&layer.find(L"门廊")==std::wstring::npos)continue;
  AcDbExtents box;
  if(polyline->getGeomExtents(box)!=Acad::eOk||anchor.x<=box.minPoint().x||anchor.x>=box.maxPoint().x||
     anchor.y<=box.minPoint().y||anchor.y>=box.maxPoint().y)continue;
  std::array<AcGePoint3d,4> points;
  bool straight=true;
  for(unsigned i=0;i<points.size();++i){
   double bulge{};
   if(polyline->getPointAt(i,points[i])!=Acad::eOk||
      (i+1<points.size()&&(polyline->getBulgeAt(i,bulge)!=Acad::eOk||std::abs(bulge)>1e-8))){
    straight=false;
    break;
   }
  }
  if(!straight)continue;
  const auto first=points[1]-points[0],base=points[2]-points[1],last=points[3]-points[2];
  if(first.length()<=tolerance||base.length()<=tolerance||last.length()<=tolerance)continue;
  if(std::abs(first.normal().dotProduct(base.normal()))>1e-6||
     first.normal().dotProduct(last.normal())>-1.+1e-6)continue;
  const auto startWalls=supportingWalls(points.front()),endWalls=supportingWalls(points.back());
  const bool distinct=std::any_of(startWalls.begin(),startWalls.end(),[&](const auto& start){
   return std::any_of(endWalls.begin(),endWalls.end(),[&](const auto& end){return start!=end;});
  });
  if(!distinct)continue;
  std::vector<AcGePoint2d> polygon;
  for(const auto& point:points)polygon.emplace_back(point.x,point.y);
  // The end chord bounds the name check only. The source contour remains open.
  if(!genericInside(polygon,{anchor.x,anchor.y}))continue;
  if(outline)return {};
  outline=handle;
 }
 return outline;
}

std::optional<std::string> productExteriorNameFacade(const GenericNativeInput& input,
 const bcpproduct::GenericView& view,const ProductPlanBounds& bounds,const std::wstring& text,
 const AcGePoint3d& anchor,double band,double tolerance){
 const auto name=productRoomBaseName(text);
 if(name!=L"门廊"&&name!=L"阳台"&&name!=L"露台"&&name!=L"无障碍坡道"&&name!=L"坡道")return {};
 const bool vertical=anchor.x<bounds.xmin||anchor.x>bounds.xmax;
 const bool horizontal=anchor.y<bounds.ymin||anchor.y>bounds.ymax;
 if(vertical==horizontal)return {};
 const double normal=vertical?anchor.x:anchor.y;
 const double along=vertical?anchor.y:anchor.x;
 const double facadeLow=vertical?bounds.xmin:bounds.ymin;
 const double facadeHigh=vertical?bounds.xmax:bounds.ymax;
 const bool low=normal<facadeLow;
 const double sign=low?-1.:1.;
 const double facade=low?facadeLow:facadeHigh;
 if(sign*(normal-facade)>band)return {};

 double closest=band;
 std::optional<std::string> witness;
 const auto consider=[&](const std::string& source,double distance){
  if(distance<-tolerance||distance>closest)return;
  closest=distance;
  witness=source;
 };
 for(const auto index:view.walls){
  const auto& wall=input.walls[index];
  if(wall.low||!wall.orthogonal||(std::abs(wall.wall.start.x-wall.wall.end.x)<=tolerance)!=vertical)continue;
  const auto box=approvedRect(wall.wall);
  const double lo=vertical?box[1]:box[0],hi=vertical?box[3]:box[2];
  if(along<lo-tolerance||along>hi+tolerance)continue;
  const double face=vertical?(low?box[0]:box[2]):(low?box[1]:box[3]);
  consider(wall.wall.handle,sign*(normal-face));
 }
 if(witness||name!=L"门廊")return witness;

 // An open doorway can occupy the entire transverse wall span at a porch.
 for(const auto& opening:input.openings){
  if(opening.window)continue;
  const double n1=vertical?opening.a.x:opening.a.y,n2=vertical?opening.b.x:opening.b.y;
  const double a1=vertical?opening.a.y:opening.a.x,a2=vertical?opening.b.y:opening.b.x;
  if(std::abs(n1-facade)>tolerance||std::abs(n2-facade)>tolerance||
     along<std::min(a1,a2)-tolerance||along>std::max(a1,a2)+tolerance)continue;
  consider(opening.handle,sign*(normal-facade));
 }
 return witness;
}

std::map<std::string,ProductFurnitureExclusion> productPlanConfirmedNonFurnitureOutlines(
 const ProductJob& job,AcDbDatabase& db,const GenericNativeInput& input,double unit){
 std::map<std::string,ProductFurnitureExclusion> result;
 if(!job.unifiedPlan)return result;
 const auto confirmations=productPlanDetailConfirmations(job);
 const double tolerance=.01/unit;
 std::vector<ProductOutdoorEdge> outdoorEdges;
 const auto add=[&](const std::string& handle,const char* role){
  try{
   const auto points=chainGrips(find(db,handle));
   if(!points.empty())result.try_emplace(handle,ProductFurnitureExclusion{role,points});
  }catch(const std::exception&){}
 };
 const auto noteFor=[&](const std::string& handle){
  return std::find_if(input.notes.begin(),input.notes.end(),[&](const auto& note){return note.handle==handle;});
 };
 const auto collect=[&](const std::string& reference){
  try{
   if(auto edge=productOutdoorEdge(db,reference,tolerance)){
    outdoorEdges.push_back(*edge);
    add(reference.substr(0,reference.find(':')),"confirmed-outdoor-boundary");
   }
  }catch(const std::exception&){}
 };
 for(const auto& [name,boundary]:confirmations.exteriorNameBoundaries){
  const auto note=noteFor(name);
  try{
   if(note!=input.notes.end()&&productConfirmedExteriorName(db,boundary,note->anchor,tolerance))
    for(const auto& edge:boundary)collect(edge);
  }catch(const std::exception&){}
 }
 for(const auto& [name,projection]:confirmations.exteriorNameProjections){
  const auto note=noteFor(name);
  try{
   if(note!=input.notes.end()&&productConfirmedExteriorProjection(db,projection,input,note->anchor,tolerance))
    for(const auto& edge:projection.edges)collect(edge);
  }catch(const std::exception&){}
 }
 for(const auto& [note,outlines]:confirmations.buildingAreaOutlines){
  if(noteFor(note)==input.notes.end())continue;
  for(const auto& outline:outlines){
   try{
    const auto entity=open<AcDbEntity>(find(db,outline));
    const auto* polyline=AcDbPolyline::cast(entity.get());
    double area{};
    if(polyline&&polyline->isClosed()&&polyline->getArea(area)==Acad::eOk&&std::isfinite(area)&&area>0.)
     add(outline,"registered-building-footprint");
   }catch(const std::exception&){}
  }
 }
 for(const auto& [handle,type]:modelTypes(db)){
  if(type!=L"AcDbLine"&&type!=L"AcDbPolyline")continue;
  const auto key=ascii(handle);
  if(result.count(key))continue;
  try{
   const auto edge=productOutdoorEdge(db,key,tolerance);
   if(edge&&std::any_of(outdoorEdges.begin(),outdoorEdges.end(),[&](const auto& reference){
    return (edge->first.distanceTo(reference.first)<=tolerance&&edge->second.distanceTo(reference.second)<=tolerance)||
           (edge->first.distanceTo(reference.second)<=tolerance&&edge->second.distanceTo(reference.first)<=tolerance);
   }))add(key,"coincident-confirmed-outdoor-boundary");
  }catch(const std::exception&){}
 }
 return result;
}

bool productPlanStandaloneTextWritable(AcDbDatabase& db,const AcDbEntity& entity);

void productRestylePlanNames(ProductJob& job,AcDbDatabase& db,const GenericNativeInput& input,
 std::vector<ProductPlanNameCheck>& checks,std::size_t view,int panel,
 const bcpproduct::GenericView& component,
 const ProductPlanBounds& bounds,const std::vector<std::vector<AcGePoint2d>>& surfaces,
 std::vector<ProductRoomName>& areaNames,AcDbObjectId style,AcDbObjectId layer,double unit,
 const ProductPlanDetailConfirmations& confirmations,AcDbDatabase* material,AcDbDatabase& reference) {
 AcDbDatabase projectNames(false,true);
 const bool projectOpen=!job.unifiedRoomNameReference.empty()&&
  projectNames.readDwgFile(job.unifiedRoomNameReference.c_str(),AcDbDatabase::kForReadAndAllShare,false)==Acad::eOk&&
  projectNames.closeInput(true)==Acad::eOk;
 const auto collect=[](AcDbDatabase& donor){
  Working working(&donor);
  std::map<std::wstring,AcDbObjectId> result;
  for(const auto& [handle,type]:modelTypes(donor)){
   if(type!=L"TDbText")continue;
   const auto id=find(donor,ascii(handle));
   const auto entity=open<AcDbEntity>(id);
   const auto value=productPlanNameContents(*entity);
   if(productPlanShortName(value))result.try_emplace(value,id);
  }
  return result;
 };
 const auto projectIndex=projectOpen?collect(projectNames):std::map<std::wstring,AcDbObjectId>{};
 const auto materialIndex=material?collect(*material):std::map<std::wstring,AcDbObjectId>{};
 const auto referenceIndex=collect(reference);
 for(auto& check:checks){
  if(check.view!=view)continue;
  check.panel=panel+1;
  if(!check.pendingReason.empty())continue;
  const auto& at=*check.anchor;
  constexpr double tolerance=1e-5;
  const bool within=at.x>=bounds.xmin-tolerance&&at.x<=bounds.xmax+tolerance&&
   at.y>=bounds.ymin-tolerance&&at.y<=bounds.ymax+tolerance;
  const bool outdoor=productRoomRole(check.text)==ProductRoomRole::excluded;
  const bool projected=outdoor&&
   std::any_of(surfaces.begin(),surfaces.end(),[&](const auto& surface){return genericInside(surface,{at.x,at.y});});
  bool confirmed=false;
  const auto boundary=confirmations.exteriorNameBoundaries.find(check.source);
  const auto projection=confirmations.exteriorNameProjections.find(check.source);
  if(outdoor&&(boundary!=confirmations.exteriorNameBoundaries.end()||projection!=confirmations.exteriorNameProjections.end())){
   try{
    if(boundary!=confirmations.exteriorNameBoundaries.end())
     confirmed=productConfirmedExteriorName(db,boundary->second,at,.01/unit);
    else confirmed=productConfirmedExteriorProjection(db,projection->second,input,at,.01/unit);
   }
   catch(const std::exception& error){check.pendingReason=error.what();}
   if(!confirmed&&check.pendingReason.empty())check.pendingReason=u8"已登记的室外轮廓未闭合或名称锚点不在轮廓内";
   if(!confirmed)continue;
   productRecordRuleEvidence(job,"N05","{\"panel\":"+std::to_string(check.panel)+
    ",\"source\":"+jsonUtf8(bcpproduct::decodeMessage(check.source))+
    ",\"result\":\"verified-source-outdoor-name-boundary\",\"scope\":\"source-anchor\"}");
  }
  if(!within&&!projected&&!confirmed){
   const auto porch=check.type==L"TDbText"&&productRoomBaseName(check.text)==L"门廊"?
    productPorchNameOutline(db,input,component,at,.01/unit):std::optional<std::string>{};
   // In the absence of a confirmed contour, the original native label on the
   // selected floor can still be restyled where it directly faces the facade.
   // This association supplies neither a boundary nor a measurable area.
   const auto facade=job.unifiedPlan&&outdoor&&check.type==L"TDbText"&&!porch?
    productExteriorNameFacade(input,component,bounds,check.text,at,
     bcpproduct::PlanPaperPolicy{}.start*job.unifiedScale/unit,.01/unit):
    std::optional<std::string>{};
   if(!porch&&!facade){
    check.pendingReason=u8"同文字位置在本层主体及已识别室外投影范围之外，保留原文待核对";
    continue;
   }
   if(porch)productRecordRuleEvidence(job,"N05","{\"panel\":"+std::to_string(check.panel)+
    ",\"source\":\""+check.source+"\",\"outline\":\""+*porch+
    "\",\"result\":\"verified-door-porch-name-on-source-projection\",\"scope\":\"source-anchor\"}");
   else productRecordRuleEvidence(job,"N05","{\"panel\":"+std::to_string(check.panel)+
    ",\"source\":\""+check.source+"\",\"sourceFacadeObject\":\""+*facade+
    "\",\"sourceAnchor\":"+wholePoints({at})+
    ",\"result\":\"verified-outdoor-name-facade-association\",\"scope\":\"annotation-only\"}");
  }
  if(job.unifiedPlan){
   const auto original=open<AcDbEntity>(find(db,check.source));
   if(!productPlanStandaloneTextWritable(db,*original)){
    std::vector<ProductRoomLeaf> leaves;
    productRoomLeaves(*original,leaves);
    const bool alreadyCompliant=check.type==L"TDbText"&&!style.isNull()&&original->layerId()==layer&&
     original->colorIndex()==256&&!leaves.empty()&&std::all_of(leaves.begin(),leaves.end(),[&](const auto& leaf){
      return leaf.style==style;
     });
    if(!alreadyCompliant)check.pendingReason=u8"名称位于锁层或带字段、关联，保留文字原文与所属关系";
    continue;
   }
  }
  const auto areaName=std::find_if(areaNames.begin(),areaNames.end(),[&](const auto& n){return n.handle==check.source;});
  ProductRoomName otherName{check.source,check.text,productRoomRole(check.text),{}};
  auto& name=areaName!=areaNames.end()?*areaName:otherName;
  bool replaced=check.type==L"TDbText"&&productRoomRestyle(db,name,style,layer);
  const auto tryDonor=[&](AcDbDatabase* donor,const auto& index){
   if(replaced||!donor)return;
   const auto found=index.find(check.text);
   if(found!=index.end())replaced=productRoomRestyle(db,name,style,layer,L"FY_HZ",false,donor,found->second);
  };
  tryDonor(&projectNames,projectIndex);
  tryDonor(material,materialIndex);
  tryDonor(&reference,referenceIndex);
  if(!replaced&&check.type==L"TDbText")
   replaced=productRoomConvertWithTangent(job,db,name,style,layer);
  if(replaced){
   check.output=name.handle;
   if(job.architecturalPlanOnly){
    try{
     const auto id=find(db,name.handle);
     auto actual=open<AcDbEntity>(id);
     const auto anchor=productPlanNameAnchor(*actual);
     std::vector<ProductRoomLeaf> leaves;
     productRoomLeaves(*actual,leaves);
     require(anchor&&!leaves.empty()&&leaves.front().height>0,"read native room name height and anchor");
     const auto height=leaves.front().height;
     require(std::all_of(leaves.begin(),leaves.end(),[&](const auto& leaf){
      return std::abs(leaf.height-height)<1e-5;
     }),"native room name has mixed heights");
     actual.reset();
     const auto target=4.*job.unifiedScale/job.unifiedUnitMM;
     if(std::abs(target-height)>1e-5){
      auto edit=editObject<AcDbEntity>(id);
      productStatus(edit->transformBy(AcGeMatrix3d::scaling(target/height,*anchor)),
                    "normalize room name paper height under N06");
      productStatus(edit->getGeomExtents(name.bounds),"read normalized room name bounds");
     }
    }catch(const std::exception& error){
     check.conversionError=u8"房名字高无法统一为纸面 4.0 mm。"+std::string(error.what());
    }
   }
  }
  else check.conversionError=u8"天正名称未能转换，保留原对象";
 }
}

void productVerifyPlanNames(ProductJob& job,AcDbDatabase& db,const std::vector<ProductPlanNameCheck>& checks,
 const std::vector<GenericOpeningCaption>* captions=nullptr) {
 Working work(&db);
 const auto types=modelTypes(db);
 const auto style=productRoomVerifiedStyle(db,true);
 std::map<std::wstring,std::vector<AcGePoint3d>> displayedNames;
 for(const auto& [handle,type]:types){
  if(type!=L"TDbText")continue;
  const auto entity=open<AcDbEntity>(find(db,ascii(handle)));
  if(entity->visibility()!=AcDb::kVisible||symbolName(entity->layerId())!=L"A-房间名称")continue;
  const auto at=productPlanNameAnchor(*entity);
  if(at)displayedNames[productPlanNameContents(*entity)].push_back(*at);
 }
 std::set<std::string> claimed;
 std::vector<std::pair<std::string,AcDbExtents>> captionBoxes;
 if(captions)for(const auto& caption:*captions){
  const auto handle=axisHandle(caption.id);
  const auto entity=open<AcDbEntity>(find(db,handle));
  if(const auto glyph=productGenericCaptionGlyph(*entity,caption.number))captionBoxes.emplace_back(handle,glyph->box);
 }
 std::size_t expected=0,verified=0,pending=0,inScopePending=0;
 std::ostringstream audit;
 audit<<std::setprecision(17)<<"{\"schema\":\"bcp.plan-name-audit.v1\",\"savedReadback\":true,\"names\":[";
 bool first=true;
 for(const auto& check:checks){
  const auto sourcePanel=check.view<job.rulePanels.size()?check.view+1:0;
  if(!check.pendingReason.empty()&&check.source==check.output&&
     productOutsideSelectedPlanWindows(job,db,check.source)){
   const auto reason=u8"已重开确认原文字在选定正式打印图窗之外，保留源图表达";
   if(job.sourceInventory.items.count({sourcePanel,"name",check.source}))
    productCheckPlanSource(job,sourcePanel,"name",check.source,bcpproduct::PlanCheck::omitted,{check.source},reason);
   if(!first)audit<<',';
   first=false;
   audit<<"{\"source\":\""<<check.source<<"\",\"output\":\""<<check.output<<"\",\"panel\":"<<check.panel
        <<",\"text\":"<<jsonUtf8(check.text)<<",\"status\":\"omitted\",\"reason\":"
        <<jsonUtf8(bcpproduct::decodeMessage(reason))<<'}';
   continue;
  }
  std::string reason=check.pendingReason;
  if(!check.panel&&reason.empty())reason=u8"局部平面未纳入本次主体生成，保留名称";
  const bool isPending=!reason.empty();
  std::wstring actualType,actualStyle,actualText,actualLayer;
  std::optional<AcGePoint3d> actualAnchor;
  bool horizontal=false;
  std::vector<double> glyphHeights;
  std::vector<std::string> overlaps;
  if(isPending)++pending;
  else{
   ++expected;
   const auto h=bcpproduct::decodeMessage(check.output);
   const auto actual=types.find(h);
   if(actual==types.end())reason=u8"保存重开后缺少应有的名称对象";
   else{
    actualType=actual->second;
    const auto e=open<AcDbEntity>(find(db,check.output));
    const auto layer=open<AcDbLayerTableRecord>(e->layerId());
    actualLayer=symbolName(e->layerId());
    if(actualType!=L"TDbText")reason=u8"名称对象不是原生天正文字";
    else{
     AcDbExtents bounds;
     if(e->getGeomExtents(bounds)==Acad::eOk)for(const auto& [opening,box]:captionBoxes)
      if(productGenericBoxesOverlap(bounds,box,0.))overlaps.push_back(opening);
     std::vector<ProductRoomLeaf> leaves;
     productRoomLeaves(*e,leaves);
     for(const auto& leaf:leaves)actualText+=leaf.text;
     horizontal=!leaves.empty()&&std::all_of(leaves.begin(),leaves.end(),[](const auto& leaf){
      return std::abs(std::remainder(leaf.rotation,2*std::acos(-1.)))<1e-5;
     });
     for(const auto& leaf:leaves)glyphHeights.push_back(leaf.height);
     if(!leaves.empty()&&!leaves.front().style.isNull())actualStyle=symbolName(leaves.front().style);
     actualAnchor=productPlanNameAnchor(*e);
     if(style.isNull())reason=u8"FY_HZ 字体资源、样式定义或宽度系数未通过实际读回";
     else if(leaves.empty()||std::any_of(leaves.begin(),leaves.end(),[&](const auto& leaf){return leaf.style!=style;}))
      reason=u8"名称未实际引用 FY_HZ";
     else if(actualText!=check.text)reason=u8"名称内容与源图不一致";
     else if(!horizontal)reason=u8"名称文字方向需按水平表达复核";
     else if(!actualAnchor||!check.expectedAnchor||actualAnchor->distanceTo(*check.expectedAnchor)>1e-5)
      reason=u8"名称定位与预期位置不一致";
     else if(actualLayer!=L"A-房间名称"||layer->isOff()||layer->isFrozen()||layer->color().colorIndex()!=7||
       e->visibility()!=AcDb::kVisible||e->colorIndex()!=256)reason=u8"名称图层、有效颜色或可见性不符合规则";
     else if(!claimed.insert(check.output).second||(check.output!=check.source&&types.count(bcpproduct::decodeMessage(check.source))))
      reason=u8"名称输出重复，或替换后源文字仍在图面";
     else if(std::count_if(displayedNames.at(actualText).begin(),displayedNames.at(actualText).end(),
       [&](const auto& point){return point.distanceTo(*actualAnchor)<=1e-5;})!=1)reason=u8"同一位置有重叠的同名文字";
     else if(!check.conversionError.empty())reason=check.conversionError;
    }
   }
   if(reason.empty())++verified;
  }
  if(!reason.empty()){
   const auto location=check.panel?u8"第"+std::to_string(check.panel)+u8"幅 ":u8"未归属主体平面 ";
   job.detailPending.push_back({isPending?u8"平面名称识别":u8"平面名称终核失败",check.source,
    location+reason+(check.conversionError.empty()?std::string{}:u8"。"+check.conversionError)});
  }
  const bool outsideScope=job.unifiedPlan&&!check.panel&&
   !job.sourceInventory.items.count({sourcePanel,"name",check.source});
  if(isPending&&!outsideScope)++inScopePending;
  if(!outsideScope)
   productCheckPlanSource(job,sourcePanel,"name",check.source,
    reason.empty()?bcpproduct::PlanCheck::verified:isPending?bcpproduct::PlanCheck::pending:bcpproduct::PlanCheck::failed,
    reason.empty()?std::vector<std::string>{check.output}:std::vector<std::string>{},reason);
  if(!first)audit<<',';
  first=false;
  audit<<"{\"source\":\""<<check.source<<"\",\"output\":\""<<check.output<<"\",\"panel\":"<<check.panel
       <<",\"sourceType\":"<<jsonUtf8(check.type)<<",\"text\":"<<jsonUtf8(check.text);
  const auto writeAnchor=[&](const char* field,const std::optional<AcGePoint3d>& point){
   audit<<",\""<<field<<"\":";
   if(point)audit<<'['<<point->x<<','<<point->y<<','<<point->z<<']';else audit<<"null";
  };
  writeAnchor("anchor",check.anchor);
  writeAnchor("expectedAnchor",check.expectedAnchor);
  writeAnchor("actualAnchor",actualAnchor);
  audit<<",\"actualType\":"<<jsonUtf8(actualType)<<",\"actualStyle\":"<<jsonUtf8(actualStyle)
       <<",\"actualText\":"<<jsonUtf8(actualText)<<",\"actualLayer\":"<<jsonUtf8(actualLayer)
       <<",\"status\":\""<<(isPending?"pending":reason.empty()?"passed":"failed")<<"\",\"reason\":"
       <<jsonUtf8(bcpproduct::decodeMessage(reason))
       <<",\"horizontal\":"<<(horizontal?"true":"false")<<",\"sourceGlyphHeights\":[";
  for(std::size_t i=0;i<check.sourceGlyphHeights.size();++i){if(i)audit<<',';audit<<check.sourceGlyphHeights[i];}
  audit<<"],\"glyphHeights\":[";
  for(std::size_t i=0;i<glyphHeights.size();++i){if(i)audit<<',';audit<<glyphHeights[i];}
  audit<<"]}";
  const auto evidence="{\"panel\":"+std::to_string(check.panel)+",\"source\":\""+check.source+
    "\",\"object\":\""+check.output+"\",\"result\":\""+(reason.empty()?"verified-name":"pending")+
    "\",\"scope\":\"name\",\"savedReopened\":true,\"report\":\"平面名称文字终核.json\",\"detail\":"+
    jsonUtf8(bcpproduct::decodeMessage(reason))+"}";
  for(const auto* rule:{"N01","N02","N03","N04","N05","QA09"})productRecordRuleEvidence(job,rule,evidence);
  const bool heightsRetained=!glyphHeights.empty()&&glyphHeights.size()==check.sourceGlyphHeights.size()&&
   std::equal(glyphHeights.begin(),glyphHeights.end(),check.sourceGlyphHeights.begin(),
              [](double actual,double expected){return std::abs(actual-expected)<1e-5;});
  const auto targetHeight=4.*job.unifiedScale/job.unifiedUnitMM;
  const bool heightCompliant=job.architecturalPlanOnly?!glyphHeights.empty()&&
   std::all_of(glyphHeights.begin(),glyphHeights.end(),[&](double height){return std::abs(height-targetHeight)<1e-5;}):heightsRetained;
  if(!outsideScope)job.drawingChecks.record(sourcePanel,"room-name-height",check.source,
   job.architecturalPlanOnly?std::vector<std::string>{"N06","TL05"}:std::vector<std::string>{"TL05"},bcpproduct::PlanReleaseGate::mainPlan,
   reason.empty()&&heightCompliant?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::failed,
   job.architecturalPlanOnly?u8"重开读回纸面 4.0 mm 及天正 FY_HZ":u8"重开读回原名义字高及天正 FY_HZ",
   reason.empty()&&heightCompliant?"":u8"实际房名字高或原生表达未达到规则");
  productRecordRuleEvidence(job,job.architecturalPlanOnly?"N06":"TL05","{\"panel\":"+std::to_string(check.panel)+",\"source\":\""+check.source+
   "\",\"object\":\""+check.output+"\",\"result\":\""+(outsideScope?"omitted-outside-confirmed-plan-scope":reason.empty()&&heightCompliant?
    "verified-native-name-font-content-anchor-and-height":"pending-name-style-height-or-position")+
   "\",\"sourceGlyphHeightRetained\":"+(heightsRetained?"true":"false")+",\"savedReopened\":true}");
  if(captions){
   std::string row="{\"panel\":"+std::to_string(check.panel)+",\"object\":\""+check.output+
    "\",\"scope\":\"name-and-native-opening-caption\",\"overlaps\":[";
   for(std::size_t i=0;i<overlaps.size();++i){if(i)row+=',';row+='"'+overlaps[i]+'"';}
   const bool clear=reason.empty()&&overlaps.empty();
   row+="],\"savedReopened\":true,\"result\":\"";
   row+=clear?"verified-name-clearance":"pending-name-or-caption-clearance";
   productRecordRuleEvidence(job,"L06",row+"\"}");
   if(!overlaps.empty())job.detailPending.push_back({u8"房名与门窗编号净空",check.output,u8"最终名称与实际门窗号字框相交，按 L06 留在本房间内联合调整"});
  }
 }
 if(!expected)job.detailPending.push_back({u8"平面名称终核",u8"项目",u8"本次无可独立核验的名称，名称终核未形成覆盖"});
 const bool passed=expected>0&&expected==verified&&inScopePending==0;
 audit<<"],\"expectedCount\":"<<expected<<",\"verifiedCount\":"<<verified<<",\"pendingCount\":"<<pending
      <<",\"inScopePendingCount\":"<<inScopePending
      <<",\"coverage\":\""<<(verified?"verified":"none")<<"\",\"passed\":"<<(passed?"true":"false")<<'}';
 writeNew(job.root/L"平面名称文字终核.json",audit.str());
}

struct ProductPlanAreaSummary {
 std::string source,output;
 std::wstring text;
 AcGePoint3d anchor;
 std::size_t view{};
 bool grossArea{true};
 std::wstring sourceLayer,sourceType;
 std::optional<std::wstring> rawContents;
 std::string stylePending;
};

struct ProductPlanFrameMove {
 std::array<std::string,3> handles;
 std::array<std::vector<AcGePoint3d>,3> points;
 double offset{};
};

std::vector<ProductPlanFrameMove> productClearPlanFrameGroups(ProductJob& job,AcDbDatabase& db){
 const auto confirmations=productPlanDetailConfirmations(job);
 std::vector<ProductPlanFrameMove> moved;
 if(confirmations.frameCompanions.empty())return moved;
 std::vector<AcDbExtents> circles;
 for(const auto& [handle,type]:modelTypes(db)){
  if(type!=L"TDbAxisLabelSet")continue;
  const auto id=find(db,ascii(handle));
  if(!productPlanDimensionVisible(*open<AcDbEntity>(id)))continue;
  const auto grips=chainGrips(id);
  for(std::size_t i=1;i+1<grips.size();i+=4){
   const auto radius=grips[i].distanceTo(grips[i+1]);
   if(radius<=1e-5)continue;
   const AcGeVector3d delta(radius,radius,0);
   circles.emplace_back(grips[i]-delta,grips[i]+delta);
  }
 }
 const double margin=2*job.unifiedScale/job.unifiedUnitMM;
 for(const auto& [outside,companions]:confirmations.frameCompanions){
  ProductPlanFrameMove row{{outside,companions[0],companions[1]},{},0};
  std::array<AcDbExtents,3> boxes;
  bool valid=true;
  for(std::size_t i=0;i<row.handles.size();++i){
   const auto entity=open<AcDbEntity>(find(db,row.handles[i]));
   const auto* rectangle=AcDbPolyline::cast(entity.get());
   if(symbolName(entity->layerId())!=L"PUB_TITLE"||!productPlanDimensionVisible(*entity)||
      entity->getGeomExtents(boxes[i])!=Acad::eOk||
      (i<2&&(!rectangle||!rectangle->isClosed()||rectangle->numVerts()!=4))||
      (i==2&&!AcDbBlockReference::cast(entity.get()))){valid=false;break;}
   if(i<2)for(unsigned k=0;k<4;++k){
    AcGePoint2d a,b;
    if(rectangle->segType(k)!=AcDbPolyline::kLine||rectangle->getPointAt(k,a)!=Acad::eOk||
       rectangle->getPointAt((k+1)%4,b)!=Acad::eOk||
       ((std::abs(a.x-b.x)<=1e-5)==(std::abs(a.y-b.y)<=1e-5))){valid=false;break;}
   }
   row.points[i]=chainGrips(entity->objectId());
   if(row.points[i].empty()){valid=false;break;}
  }
  const auto pending=[&](){job.detailPending.push_back({u8"图框避让",outside,
    u8"原图框或图签未能在保持纸面尺寸及组关系时可靠避让轴圈，须复核正式图幅"});};
  if(!valid){pending();continue;}
  const auto left=boxes[1].minPoint().x,right=boxes[1].maxPoint().x;
  const auto top=boxes[1].maxPoint().y,bottom=boxes[1].minPoint().y;
  if(boxes[0].maxPoint().y<top||boxes[0].minPoint().y>bottom){pending();continue;}
  double lowest=std::numeric_limits<double>::infinity();
  for(const auto& circle:circles){
   if(circle.minPoint().x<left||circle.maxPoint().x>right||circle.maxPoint().y<bottom||
      circle.minPoint().y>boxes[0].maxPoint().y+margin)continue;
   row.offset=std::max(row.offset,circle.maxPoint().y+margin-top);
   lowest=std::min(lowest,circle.minPoint().y);
  }
  if(row.offset<=1e-5)continue;
  if(boxes[2].maxPoint().y+row.offset+margin>lowest){pending();continue;}
  const AcGeVector3d delta(0,row.offset,0);
  for(const auto& [handle,type]:modelTypes(db)){
   if(type!=L"TDbDrawingName"&&type!=L"TDbText"&&type!=L"AcDbText")continue;
   const auto entity=open<AcDbEntity>(find(db,ascii(handle)));
   AcDbExtents box;
   if(!productPlanDimensionVisible(*entity)||entity->getGeomExtents(box)!=Acad::eOk)continue;
   if(box.maxPoint().x<left||box.minPoint().x>right||box.maxPoint().y<bottom||box.minPoint().y>top)continue;
   if(box.minPoint().y<boxes[2].maxPoint().y+row.offset+margin){valid=false;break;}
  }
  if(!valid){pending();continue;}
  for(const auto& handle:row.handles){
   auto entity=editObject<AcDbEntity>(find(db,handle));
   productStatus(entity->transformBy(AcGeMatrix3d::translation(delta)),"move confirmed frame without changing its scale");
  }
  moved.push_back(std::move(row));
 }
 return moved;
}

void productVerifyPlanFrameGroups(ProductJob& job,AcDbDatabase& db,const std::vector<ProductPlanFrameMove>& moved){
 Working work(&db);
 std::ostringstream report;
 report<<std::setprecision(17)<<"{\"savedReopened\":true,\"groups\":[";
 bool first=true;
 for(const auto& row:moved){
  const AcGeVector3d delta(0,row.offset,0);
  bool same=true;
  for(std::size_t i=0;i<row.handles.size();++i){
   const auto actual=chainGrips(find(db,row.handles[i]));
   same&=actual.size()==row.points[i].size()&&std::equal(actual.begin(),actual.end(),row.points[i].begin(),
    [&](const auto& a,const auto& b){return a.distanceTo(b+delta)<=1e-5;});
  }
  if(!same)job.detailPending.push_back({u8"图框避让",row.handles[0],u8"重开后原图框组的夹点未同时通过等量平移核验"});
  if(!first)report<<',';
  first=false;
  const auto record="{\"object\":\""+row.handles[0]+"\",\"inner\":\""+row.handles[1]+
    "\",\"titleBlock\":\""+row.handles[2]+"\",\"offsetY\":"+std::to_string(row.offset)+
    ",\"savedReopened\":true,\"result\":\""+(same?"verified-frame-group-clearance":"pending")+"\"}";
  report<<record;
  productRecordRuleEvidence(job,"A14",record);
 }
 report<<"]}";
 if(!moved.empty())writeNew(job.root/L"plan-frame-group-readback.json",report.str());
}

bool productPlanWindowsTextStyle(AcDbDatabase& db,AcDbObjectId id,const wchar_t* name,
 const wchar_t* filename,const wchar_t* alternate,const wchar_t* face,const wchar_t* localFace){
 if(id.isNull()||symbolName(id)!=name)return false;
 const auto style=open<AcDbTextStyleTableRecord>(id);
 AcString primary,big;
 if(style->fileName(primary)==Acad::eOk&&style->bigFontFileName(big)==Acad::eOk&&big.isEmpty()){
  const auto actual=fs::path(primary.kACharPtr()).filename().wstring();
  if((_wcsicmp(actual.c_str(),filename)==0||(alternate&&_wcsicmp(actual.c_str(),alternate)==0))&&
     productRoomFontFile(db,primary,false))return true;
 }
 AcString typeface;
 bool bold{},italic{};
 Charset charset{};
 Autodesk::AutoCAD::PAL::FontUtils::FontPitch pitch{};
 Autodesk::AutoCAD::PAL::FontUtils::FontFamily family{};
 return style->font(typeface,bold,italic,charset,pitch,family)==Acad::eOk&&
  (_wcsicmp(typeface.kACharPtr(),face)==0||typeface==localFace)&&productDimensionFontAvailable(db,filename);
}

bool productAreaSummaryTextStyle(AcDbDatabase& db,AcDbObjectId id){
 return productPlanWindowsTextStyle(db,id,L"黑体",L"simhei.ttf",nullptr,L"SimHei",L"黑体");
}

bool productPlanTitleTextStyle(AcDbDatabase& db,AcDbObjectId id){
 return productPlanWindowsTextStyle(db,id,L"-宋体",L"simsun.ttc",L"simsun.ttf",L"SimSun",L"宋体");
}

AcDbObjectId productPlanTextStyle(AcDbDatabase& db,AcDbDatabase& reference,const wchar_t* name,
 bool (*verified)(AcDbDatabase&,AcDbObjectId)){
 const auto lookup=[name](AcDbDatabase& source){
  AcDbObjectId id;
  open<AcDbTextStyleTable>(source.textStyleTableId())->getAt(name,id);
  return id;
 };
 auto target=lookup(db);
 if(target.isNull()){
  const auto donor=lookup(reference);
  if(verified(reference,donor))target=productRoomImportStyle(reference,donor,db);
 }
 return verified(db,target)?target:AcDbObjectId{};
}

bool productPlanStandaloneTextWritable(AcDbDatabase& db,const AcDbEntity& entity){
 if(entity.isAProxy()||entity.hasFields()||entity.ownerId()!=modelId(db)||
    open<AcDbLayerTableRecord>(entity.layerId())->isLocked())return false;
 if(const auto* reactors=entity.reactors())for(int i=0;i<reactors->length();++i)
  if(acdbIsPersistentReactor(reactors->at(i)))return false;
 return true;
}

std::optional<std::wstring> productPlanMTextOriginal(const AcDbMText& text){
 AcString raw;
 if(text.contents(raw)!=Acad::eOk)return {};
 return raw.kACharPtr();
}

bool productPlanMTextUsesStyle(const AcDbMText& text){
 const auto raw=productPlanMTextOriginal(text);
 // An inline font can override the record even when the base style is correct.
 return raw&&raw->find(L"\\f")==std::wstring::npos&&raw->find(L"\\F")==std::wstring::npos;
}

bool productPlanTextSameLeaves(const std::vector<ProductRoomLeaf>& original,
 const std::vector<ProductRoomLeaf>& actual){
 return original.size()==actual.size()&&std::equal(original.begin(),original.end(),actual.begin(),
  [](const auto& a,const auto& b){return a.text==b.text&&std::abs(a.rotation-b.rotation)<1e-8&&
   std::abs(a.height-b.height)<1e-5&&a.anchor.distanceTo(b.anchor)<1e-5;});
}

bool productPlanDrawingNameUnderline(const AcDbEntity& title,std::vector<std::pair<AcGePoint3d,AcGePoint3d>>& lines){
 TemplateParts parts(&title);
 if(title.explode(parts.values)!=Acad::eOk)return false;
 for(int i=0;i<parts.values.length();++i){
  const auto* part=AcDbEntity::cast(static_cast<AcRxObject*>(parts.values[i]));
  if(!part)return false;
  if(const auto* line=AcDbLine::cast(part))lines.emplace_back(line->startPoint(),line->endPoint());
  else if(const auto* poly=AcDbPolyline::cast(part)){
   const auto n=poly->numVerts();for(unsigned k=0;k+(poly->isClosed()?0:1)<n;++k){double bulge{};AcGePoint3d a,b;
    if(poly->getBulgeAt(k,bulge)!=Acad::eOk||std::abs(bulge)>1e-9||poly->getPointAt(k,a)!=Acad::eOk||poly->getPointAt((k+1)%n,b)!=Acad::eOk)return false;
    lines.emplace_back(a,b);}
  }else if(!AcDbText::cast(part)&&!AcDbMText::cast(part)&&std::wstring(part->isA()->name())!=L"TDbText")return false;
 }
 return !lines.empty();
}

// Map only the text style observed on the intended glyphs. Validate the original
// edit points, proportion text and underline before retaining the source handle.
std::string productRestyleStandalonePlanText(AcDbDatabase& db,AcDbObjectId id,AcDbObjectId style,
 const std::function<bool(const std::wstring&)>& controlled,bool permitTypographyLayout=false){
 if(style.isNull())return u8"当前图与已确认样张均未提供可使用的规则字体";
 auto object=open<AcDbEntity>(id);
 if(const auto* text=AcDbText::cast(object.get())){
  if(text->textStyle()==style)return {};
  if(!productPlanStandaloneTextWritable(db,*text))return u8"文字在锁层或与其他对象关联，保留原文及原关系";
  const auto priorStyle=text->textStyle();
  const auto anchor=text->isDefaultAlignment()?text->position():text->alignmentPoint();
  const std::wstring contents=text->textString();
  const auto height=text->height(),rotation=text->rotation(),width=text->widthFactor();
  const auto horizontal=text->horizontalMode();
  const auto vertical=text->verticalMode();
  const auto setStyle=[&](AcDbText& entity,AcDbObjectId value){
   return entity.setTextStyle(value)==Acad::eOk&&
    (entity.isDefaultAlignment()||entity.adjustAlignment(&db)==Acad::eOk);
  };
  object.reset();
  auto edited=editObject<AcDbText>(id);
  if(setStyle(*edited,style)&&edited->textStyle()==style&&contents==edited->textString()&&
     edited->horizontalMode()==horizontal&&edited->verticalMode()==vertical&&
     (edited->isDefaultAlignment()?edited->position():edited->alignmentPoint()).distanceTo(anchor)<1e-5&&
     std::abs(edited->height()-height)<1e-8&&std::abs(edited->rotation()-rotation)<1e-8&&
     std::abs(edited->widthFactor()-width)<1e-8)return {};
  require(setStyle(*edited,priorStyle),"restore standalone plan text style");
  return u8"公开样式设置未保持原文字锚点和字高，已还原";
 }
 if(const auto* text=AcDbMText::cast(object.get())){
  if(!productPlanMTextUsesStyle(*text))return u8"原多行文字含行内字体覆盖，保留原格式待复核";
  if(text->textStyle()==style)return {};
  if(!productPlanStandaloneTextWritable(db,*text))return u8"文字在锁层或与其他对象关联，保留原文及原关系";
  const auto raw=productPlanMTextOriginal(*text);
  const auto priorStyle=text->textStyle();
  const auto anchor=text->location();
  const auto attachment=text->attachment();
  const auto height=text->textHeight(),width=text->width(),rotation=text->rotation();
  object.reset();
  auto edited=editObject<AcDbMText>(id);
  if(edited->setTextStyle(style)==Acad::eOk&&edited->textStyle()==style&&raw==productPlanMTextOriginal(*edited)&&
     edited->location().distanceTo(anchor)<1e-5&&edited->attachment()==attachment&&
     std::abs(edited->textHeight()-height)<1e-8&&std::abs(edited->width()-width)<1e-8&&
     std::abs(edited->rotation()-rotation)<1e-8)return {};
  productStatus(edited->setTextStyle(priorStyle),"restore standalone plan multiline text style");
  return u8"公开样式设置未保持原多行文字内容及排布参数，已还原";
 }

 const std::wstring type=object->isA()->name();
 if(type!=L"TDbDrawingName"&&type!=L"TDbText")return u8"此文字类型没有已核定的公开样式替换方式";
 std::vector<ProductRoomLeaf> original;
 productRoomLeaves(*object,original);
 std::set<AcDbObjectId> mappedStyles,protectedStyles;
 for(const auto& leaf:original){
  if(leaf.style.isNull()||leaf.style.database()!=&db)return u8"原生文字未能读回本图的实际样式";
  (controlled(leaf.text)?mappedStyles:protectedStyles).insert(leaf.style);
 }
 if(mappedStyles.empty())return u8"未能在原生对象中确认需要改样式的文字正文";
 if(mappedStyles.size()==1&&*mappedStyles.begin()==style)return {};
 bool writable=productPlanStandaloneTextWritable(db,*object);
 if(permitTypographyLayout&&!object->isAProxy()&&!object->hasFields()&&!open<AcDbLayerTableRecord>(object->layerId())->isLocked()){
  writable=true;if(const auto* reactors=object->reactors())for(int i=0;i<reactors->length();++i)
   if(acdbIsPersistentReactor(reactors->at(i)))writable=false;
 }
 if(!writable||!object->extensionDictionary().isNull())
  return u8"原生文字在锁层、有关联或扩展数据，保留原表达";
 if(std::any_of(mappedStyles.begin(),mappedStyles.end(),[&](auto source){
  return source!=style&&protectedStyles.count(source)!=0;
 }))return u8"正文与保留比例文字共用原样式，未能单独映射正文";
 const auto layer=object->layerId();
 const auto linetype=object->linetypeId();
 const auto color=object->color();
 const auto anchor=productPlanNameAnchor(*object);
 const auto originalGrips=chainGrips(id);
 std::vector<std::pair<AcGePoint3d,AcGePoint3d>> underline;
 if(!anchor||(type==L"TDbDrawingName"&&!productPlanDrawingNameUnderline(*object,underline)))
  return u8"未能独立读取图名位置、比例文字及下划线";
 object.reset();

 AcDbDatabase isolated(true,true);
 AcDbObjectIdArray objects;
 objects.append(id);
 AcDbIdMapping firstMap;
 if(db.wblockCloneObjects(objects,modelId(isolated),firstMap,AcDb::kDrcMangleName,false)!=Acad::eOk)
  return u8"未能建立独立原生文字副本";
 AcDbIdPair copied(id,{},false);
 if(!firstMap.compute(copied)||!copied.isCloned())return u8"无法对应原生文字的独立副本";
 AcDbIdMapping styleMap;
 const auto mapResource=[&](AcDbObjectId source,AcDbObjectId target){
  AcDbIdPair mapped(source,{},false);
  if(!firstMap.compute(mapped)||mapped.value().isNull())return false;
  return styleMap.assign(AcDbIdPair(mapped.value(),target,false))==Acad::eOk;
 };
 for(const auto source:mappedStyles)if(!mapResource(source,style))return u8"无法对应正文的原样式资源";
 for(const auto source:protectedStyles)if(!mappedStyles.count(source)&&!mapResource(source,source))
  return u8"无法保持比例文字的实际样式";
 if(!mapResource(layer,layer))return u8"无法保持源文字的所属图层";
 if(!linetype.isNull())mapResource(linetype,linetype);
 objects.setAt(0,copied.value());
 const auto cloneStatus=isolated.wblockCloneObjects(objects,modelId(db),styleMap,AcDb::kDrcIgnore,false);
 AcDbIdPair result(copied.value(),{},false);
 if(cloneStatus!=Acad::eOk||!styleMap.compute(result)||!result.isCloned())
  return u8"公开资源映射未得到可验证的原生文字";
 const auto scratch=result.value();
 bool identical=false;
 try{
  const auto candidate=open<AcDbEntity>(scratch);
  std::vector<ProductRoomLeaf> leaves;
  std::vector<std::pair<AcGePoint3d,AcGePoint3d>> lines;
  productRoomLeaves(*candidate,leaves);
  const auto grips=chainGrips(scratch);
  const auto actualAnchor=productPlanNameAnchor(*candidate);
  identical=std::wstring(candidate->isA()->name())==type&&candidate->layerId()==layer&&
   candidate->linetypeId()==linetype&&candidate->color()==color&&
   candidate->ownerId()==modelId(db)&&productPlanDimensionVisible(*candidate)&&actualAnchor&&
   actualAnchor->distanceTo(*anchor)<1e-5&&
   (permitTypographyLayout?original.size()==leaves.size():grips==originalGrips&&productPlanTextSameLeaves(original,leaves))&&
   (type!=L"TDbDrawingName"||(productPlanDrawingNameUnderline(*candidate,lines)&&
    (permitTypographyLayout?(lines.size()==underline.size()&&std::equal(lines.begin(),lines.end(),underline.begin(),[](const auto& a,const auto& b){return std::abs(a.first.y-a.second.y)<1e-5&&std::abs(b.first.y-b.second.y)<1e-5&&std::abs(a.first.y-b.first.y)<1e-5;})):lines==underline)))&&
   std::equal(original.begin(),original.end(),leaves.begin(),[&](const auto& a,const auto& b){
    return a.text==b.text&&b.style==(controlled(a.text)?style:a.style)&&std::abs(a.rotation-b.rotation)<1e-8;
   });
  if(identical){
   auto replacement=std::unique_ptr<AcDbEntity>(AcDbEntity::cast(candidate->clone()));
   if(!replacement)identical=false;
   else{
    auto source=editObject<AcDbEntity>(id);
    if(source->handOverTo(replacement.get(),true,true)==Acad::eObjectToBeDeleted){
     // AutoCAD 2020 SDK samples/entity/polysamp/command.cpp ownership protocol.
     delete source.release();
     productStatus(replacement.release()->close(),"close style-mapped plan text");
    }else identical=false;
   }
  }
 }catch(...){
  productStatus(editObject<AcDbEntity>(scratch)->erase(),"remove temporary plan text style candidate");
  throw;
 }
 productStatus(editObject<AcDbEntity>(scratch)->erase(),"remove temporary plan text style candidate");
 return identical?std::string{}:u8"原生副本未同时保持正文、比例文字、锚点及原有下划线，保留源文字";
}

std::map<std::string,AcGeVector3d> productPlanTitleGroupClearance(ProductJob& job,AcDbDatabase& db,
 const GenericNativeInput& input,const std::vector<bcpproduct::GenericView>& views,const std::vector<bool>& generated,
 const ProductPlanDetailConfirmations& confirmations){
 std::map<std::string,AcGeVector3d> changes;
 if(confirmations.titleCompanions.empty())return changes;
 std::vector<AcDbExtents> axisLabels;
 for(const auto& [handle,type]:modelTypes(db)){
  if(type!=L"TDbAxisLabelSet")continue;
  const auto axis=open<AcDbEntity>(find(db,ascii(handle)));
  if(!productPlanDimensionVisible(*axis))continue;
  const auto grips=chainGrips(axis->objectId());
  for(std::size_t i=1;i+1<grips.size();i+=4){
   const auto& centre=grips[i];
   const auto radius=centre.distanceTo(grips[i+1]);
   if(radius<=1e-5)continue;
   axisLabels.emplace_back(centre-AcGeVector3d(radius,radius,0),centre+AcGeVector3d(radius,radius,0));
  }
 }
 const double margin=2*job.unifiedScale/job.unifiedUnitMM;
 for(const auto& [handle,companions]:confirmations.titleCompanions){
  const auto note=std::find_if(input.notes.begin(),input.notes.end(),[&](const auto& item){
   return item.handle==handle&&item.text.find(L"平面图")!=std::wstring::npos;
  });
  if(note==input.notes.end()||!generated[bcpproduct::nearestGenericView(views,{note->anchor.x,note->anchor.y})])continue;
  std::vector<AcDbObjectId> members{find(db,handle)};
  for(const auto& companion:companions)members.push_back(find(db,companion));
  AcDbExtents group;
  bool first=true,valid=true;
  for(const auto id:members){
   const auto entity=open<AcDbEntity>(id);
   AcDbExtents box;
   if(!productPlanDimensionVisible(*entity)||entity->getGeomExtents(box)!=Acad::eOk){valid=false;break;}
   if(first){group=box;first=false;}else group.addExt(box);
  }
  if(!valid)continue;
  double shift=0;
  for(const auto& circle:axisLabels){
   if(!productGenericBoxesOverlap(circle,group,margin))continue;
   shift=std::min(shift,circle.minPoint().y-group.maxPoint().y-margin);
  }
  if(shift>=-1e-5)continue;
  const AcGeVector3d delta(0,shift,0);
  const AcDbExtents after(group.minPoint()+delta,group.maxPoint()+delta);
  if(std::any_of(axisLabels.begin(),axisLabels.end(),[&](const auto& circle){
    return productGenericBoxesOverlap(circle,after,margin-1e-5);
  })){
   job.detailPending.push_back({u8"图名避让",handle,u8"按真实轴圈下移仍有另一轴号冲突，保留原图名组待复核"});
   continue;
  }
  for(const auto id:members){
   auto entity=editObject<AcDbEntity>(id);
   productStatus(entity->transformBy(AcGeMatrix3d::translation(delta)),"move confirmed title group below axis circles");
   changes.emplace(axisHandle(id),delta);
  }
 }
 return changes;
}

void productVerifyPlanTitleClearance(ProductJob& job,AcDbDatabase& db,const GenericNativeInput& input,
 const std::vector<bcpproduct::GenericView>& views){
 if(!job.unifiedPlan)return;
 Working work(&db);
 std::vector<std::pair<std::string,AcDbExtents>> annotations;
 for(const auto& [handle,type]:modelTypes(db)){
  if(type!=L"TDbAxisLabelSet"&&type!=L"TDbDimension2"&&type.find(L"Dimension")==std::wstring::npos)continue;
  const auto entity=open<AcDbEntity>(find(db,ascii(handle)));
  if(!productPlanDimensionVisible(*entity))continue;
  std::vector<AcDbExtents> boxes;
  productGenericTextLeaves(*entity,boxes);
  if(type==L"TDbAxisLabelSet"){
   const auto grips=chainGrips(entity->objectId());
   for(std::size_t i=1;i+1<grips.size();i+=4){
    const auto radius=grips[i].distanceTo(grips[i+1]);
    if(radius>1e-5)boxes.emplace_back(grips[i]-AcGeVector3d(radius,radius,0),grips[i]+AcGeVector3d(radius,radius,0));
   }
  }
  for(const auto& box:boxes)annotations.emplace_back(ascii(handle),box);
 }
 auto titles=input.titles;
 for(const auto& note:input.notes)if(bcpproduct::explicitPlanFloor(note.text))titles.push_back(note);
 for(const auto& title:titles){
  const auto entity=open<AcDbEntity>(find(db,title.handle));
  AcDbExtents box;
  const bool read=productPlanDimensionVisible(*entity)&&entity->getGeomExtents(box)==Acad::eOk;
  const auto panel=bcpproduct::nearestGenericView(views,{title.anchor.x,title.anchor.y})+1;
  std::set<std::string> overlaps;
  if(read)for(const auto& [handle,obstacle]:annotations)
   if(productGenericBoxesOverlap(box,obstacle,0.))overlaps.insert(handle);
  std::string row="{\"panel\":"+std::to_string(panel)+",\"object\":\""+title.handle+
   "\",\"scope\":\"title-axis-and-dimension-text\",\"overlaps\":[";
  bool first=true;
  for(const auto& handle:overlaps){if(!first)row+=',';first=false;row+='"'+handle+'"';}
  row+="] ,\"savedReopened\":true,\"result\":\"";
  row+=read&&overlaps.empty()?"verified-title-clearance":"pending-title-clearance";
  productRecordRuleEvidence(job,"L06",row+"\"}");
  if(!read||!overlaps.empty())job.detailPending.push_back({u8"图名图面净空",title.handle,u8"图名与轴圈或实际尺寸文字有重叠或公开字框未能读取"});
 }
}

bool productPlanGrossAreaLabel(const std::wstring& text){
 return text.rfind(L"本层建筑面积",0)==0||text.rfind(L"总建筑面积",0)==0||
  text.rfind(L"项目总建筑面积",0)==0||text.rfind(L"建筑总面积",0)==0||
  text.rfind(L"建筑面积：",0)==0||text.rfind(L"建筑面积:",0)==0;
}

bool productPlanGroupedAreaLabel(const std::wstring& text){
 return text.rfind(L"占地面积",0)==0||text.rfind(L"建筑占地面积",0)==0||
  text.rfind(L"门廊面积",0)==0||text.rfind(L"主楼滴水面积",0)==0;
}

std::vector<ProductPlanAreaSummary> productRestylePlanAreaSummaries(ProductJob& job,AcDbDatabase& db,
 AcDbDatabase& reference,const GenericNativeInput& input,const std::vector<bcpproduct::GenericView>& views,
 const std::vector<bool>& generated){
 std::vector<ProductPlanAreaSummary> receipts;
 const auto confirmations=productPlanDetailConfirmations(job);
 productPlanTitleGroupClearance(job,db,input,views,generated,confirmations);
 const auto style=productPlanTextStyle(db,reference,L"黑体",productAreaSummaryTextStyle);
 auto notes=input.notes;
 if(job.unifiedPlan)for(const auto& [wide,type]:modelTypes(db)){
  if(type!=L"AcDbText"&&type!=L"AcDbMText"&&type!=L"TDbMText")continue;
  const auto handle=ascii(wide);
  const auto entity=open<AcDbEntity>(find(db,handle));
  if(!productPlanDimensionVisible(*entity))continue;
  const auto text=productPlanNameContents(*entity);
  if(!productPlanGrossAreaLabel(text)&&!productPlanGroupedAreaLabel(text))continue;
  if(const auto anchor=productPlanNameAnchor(*entity))notes.push_back({handle,*anchor,text});
 }
 std::map<std::string,AcDbObjectId> sourceSummaryLayers;
 for(const auto& note:notes)if(productPlanGrossAreaLabel(note.text)){
  const auto entity=open<AcDbEntity>(find(db,note.handle));
  if(productPlanDimensionVisible(*entity))sourceSummaryLayers.emplace(note.handle,entity->layerId());
 }
 const auto paper=job.unifiedScale/job.unifiedUnitMM;
 for(const auto& note:notes){
  const bool grossArea=productPlanGrossAreaLabel(note.text);
  if(!grossArea&&!productPlanGroupedAreaLabel(note.text))continue;
  const auto view=bcpproduct::nearestGenericView(views,{note.anchor.x,note.anchor.y});
  if(!generated[view])continue;
  const auto id=find(db,note.handle);
  auto source=open<AcDbEntity>(id);
  if(!productPlanDimensionVisible(*source))continue;
  const auto actual=productPlanNameContents(*source);
  const auto anchor=productPlanNameAnchor(*source);
  if(actual.empty()||!anchor)continue;
  const auto layer=source->layerId();
  const auto layerName=symbolName(layer);
  const std::wstring sourceType=source->isA()->name();
  const auto* mtext=AcDbMText::cast(source.get());
  const auto raw=mtext?productPlanMTextOriginal(*mtext):std::optional<std::wstring>{};
  source.reset();
  if(!grossArea&&!std::any_of(notes.begin(),notes.end(),[&](const auto& other){
   if(!productPlanGrossAreaLabel(other.text)||std::abs(other.anchor.x-note.anchor.x)>paper||
      std::abs(other.anchor.y-note.anchor.y)>12*paper||
      bcpproduct::nearestGenericView(views,{other.anchor.x,other.anchor.y})!=view)return false;
   const auto original=sourceSummaryLayers.find(other.handle);
   return original!=sourceSummaryLayers.end()&&original->second==layer;
  }))continue;
  auto output=note.handle;
  std::string pending;
  if(sourceType==L"TDbText"){
   ProductRoomName summary{note.handle,actual,ProductRoomRole::other,{}};
   auto entity=open<AcDbEntity>(id);
   const bool writable=productPlanStandaloneTextWritable(db,*entity)&&entity->extensionDictionary().isNull();
   entity.reset();
   if(!writable){
    const auto existing=open<AcDbEntity>(id);
    std::vector<ProductRoomLeaf> leaves;
    productRoomLeaves(*existing,leaves);
    if(leaves.empty()||std::any_of(leaves.begin(),leaves.end(),[&](const auto& leaf){return leaf.style!=style;}))
     pending=u8"面积说明处于锁层或有对象关联，未能安全替换字体";
   }else if(!productRoomRestyle(db,summary,style,layer,L"黑体",true))
    pending=u8"未能接入有效黑体或保真替换天正文字";
   output=summary.handle;
  }else if(job.unifiedPlan){
   try{pending=productRestyleStandalonePlanText(db,id,style,[](const auto&){return true;});}
   catch(const std::exception& error){pending=error.what();}
  }
  receipts.push_back({note.handle,output,actual,*anchor,view,grossArea,layerName,sourceType,raw,pending});
 }
 return receipts;
}

void productVerifyPlanAreaSummaries(ProductJob& job,AcDbDatabase& db,const std::vector<ProductPlanAreaSummary>& receipts,double unitMM){
 Working work(&db);
 const auto confirmations=productPlanDetailConfirmations(job);
 std::ostringstream report;
 report<<"{\"savedReopened\":true,\"summaries\":[";
 bool first=true;
 for(const auto& receipt:receipts){
  bool textPreserved=false,styleVerified=false,anchorPreserved=false,layerPreserved=false;
  try{
   const auto entity=open<AcDbEntity>(find(db,receipt.output));
   textPreserved=std::wstring(entity->isA()->name())==receipt.sourceType&&
    productPlanNameContents(*entity)==receipt.text;
   if(const auto* text=AcDbMText::cast(entity.get())){
    textPreserved=textPreserved&&receipt.rawContents&&receipt.rawContents==productPlanMTextOriginal(*text);
    styleVerified=productPlanMTextUsesStyle(*text)&&productAreaSummaryTextStyle(db,text->textStyle());
   }else{
    std::vector<ProductRoomLeaf> leaves;
    productRoomLeaves(*entity,leaves);
    styleVerified=!leaves.empty()&&std::all_of(leaves.begin(),leaves.end(),[&](const auto& leaf){
     return productAreaSummaryTextStyle(db,leaf.style);
    });
   }
   const auto anchor=productPlanNameAnchor(*entity);
   anchorPreserved=anchor&&anchor->distanceTo(receipt.anchor)<=1e-5;
   layerPreserved=symbolName(entity->layerId())==receipt.sourceLayer;
  }catch(const std::exception& error){
   job.detailPending.push_back({u8"建筑面积说明字体",receipt.source,error.what()});
  }
  const bool verified=receipt.stylePending.empty()&&textPreserved&&styleVerified&&anchorPreserved&&layerPreserved;
  const auto reason=verified?std::string{}:receipt.stylePending.empty()?
   u8"重开后黑体字体、说明原文、图层或定位未达到要求":receipt.stylePending;
  if(job.unifiedPlan)job.drawingChecks.record(receipt.view+1,"area-summary-font",receipt.source,{"P05"},
   bcpproduct::PlanReleaseGate::mainPlan,verified?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::pending,
   u8"原生建筑面积说明、原文和实际黑体字体",
   reason);
  const std::string result=verified?(receipt.grossArea?"pending-project-area-recalculation":
   "verified-grouped-area-summary-font"):"pending";
  if(!verified)job.detailPending.push_back({u8"建筑面积说明字体",receipt.source,reason});
  const auto boolText=[](bool value){return value?"true":"false";};
  const auto record=std::string("{\"source\":\"")+receipt.source+"\",\"object\":\""+receipt.output+
   "\",\"panel\":"+std::to_string(receipt.view+1)+",\"text\":"+jsonUtf8(receipt.text)+
   ",\"nativeTextAndContentPreserved\":"+boolText(textPreserved)+",\"anchorPreserved\":"+boolText(anchorPreserved)+
   ",\"blackFontVerified\":"+boolText(styleVerified)+",\"sourceLayerPreserved\":"+boolText(layerPreserved)+
   ",\"buildingAreaRecalculated\":false,\"savedReopened\":true,\"detail\":"+
   jsonUtf8(bcpproduct::decodeMessage(reason))+",\"result\":\""+result+"\"}";
  if(!first)report<<',';
  first=false;
  report<<record;
  productRecordRuleEvidence(job,"P05",record);
  if(!receipt.grossArea)continue;
  if(confirmations.grossParts.count(receipt.source))continue; // Full boundary/factor readback follows below.
  std::ostringstream gross;
  gross.imbue(std::locale::classic());
  gross<<std::setprecision(17)<<"{\"panel\":"<<receipt.view+1<<",\"source\":\""<<receipt.source
       <<"\",\"object\":\""<<receipt.output<<"\",\"sourceAreaNote\":"<<jsonUtf8(receipt.text)<<",\"outlines\":[";
  const auto candidates=confirmations.buildingAreaOutlines.find(receipt.source);
  bool firstOutline=true;
  if(candidates!=confirmations.buildingAreaOutlines.end())for(const auto& handle:candidates->second){
   if(!firstOutline)gross<<',';
   firstOutline=false;
   std::optional<double> area;
   try{
    const auto entity=open<AcDbEntity>(find(db,handle));
    const auto* outline=AcDbPolyline::cast(entity.get());
    if(outline&&outline->isClosed()&&outline->numVerts()>=3){
     double measured{};
     if(outline->getArea(measured)==Acad::eOk&&std::isfinite(measured)&&measured>0.)area=measured*unitMM*unitMM/1e6;
    }
   }catch(const std::exception&){}
   gross<<"{\"source\":\""<<handle<<"\",\"rawM2\":";
   if(area)gross<<*area;else gross<<"null";
   gross<<'}';
  }
  const auto grossReason=candidates==confirmations.buildingAreaOutlines.end()?
   u8"缺该图幅经核实的建筑面积边界、各部分计入范围和折算依据":
   u8"已登记的外轮廓仅记录实测原始面积；待确认阳台、门廊、坡道、挑空及重合的计入范围和折算依据";
  job.detailPending.push_back({u8"本层总建筑面积复算",receipt.source,grossReason});
  gross<<"],\"savedReopened\":true,\"result\":\"pending-gross-area-inclusion-and-factors\",\"detail\":"
       <<jsonUtf8(bcpproduct::decodeMessage(grossReason))<<'}';
  productRecordRuleEvidence(job,"S06",gross.str());
  productRecordRuleEvidence(job,"Q05",gross.str());
 }
 report<<"]}";
 writeNew(job.root/L"plan-area-summary-font-readback.json",report.str());
}

struct ProductRoomAreaPair {
 AcDbObjectId body,exponent;
 double gap{},rise{};
};
std::optional<ProductRoomAreaPair> productRoomAreaPair(AcDbDatabase& donor) {
 Working work(&donor);
 struct Piece{AcDbObjectId id;double size;AcDbExtents box;};
 std::vector<Piece> bodies,exponents;
 for(const auto& item:modelTypes(donor)){
  if(item.second!=L"AcDbText")continue;
  const auto id=find(donor,ascii(item.first));
  const auto text=open<AcDbText>(id);
  if(symbolName(text->layerId())!=L"A-房间名称"||std::abs(text->rotation())>1e-5)continue;
  const std::wstring content=text->textString();
  AcDbExtents box;
  if(text->getGeomExtents(box)!=Acad::eOk)continue;
  if(content==L"2")exponents.push_back({id,text->height(),box});
  else if(productRoomAreaText(content)&&content.back()==L'm')bodies.push_back({id,text->height(),box});
 }
 for(const auto& body:bodies)for(const auto& exponent:exponents){
  const double gap=exponent.box.minPoint().x-body.box.maxPoint().x;
  const double rise=exponent.box.minPoint().y-body.box.minPoint().y;
  if(std::abs(exponent.size-body.size/2)<1e-5&&std::abs(gap)<body.size/2&&rise>0&&rise<body.size)
   return ProductRoomAreaPair{body.id,exponent.id,gap,rise};
 }
 return {};
}

struct ProductRoomAreaPlacement {
 std::string body,exponent;
 AcGeVector3d nameShift;
 std::string qualifier;
};

void productRoomFurnitureInk(const AcDbEntity& entity,std::vector<AcDbExtents>& boxes,int depth=0){
 if(depth>8||entity.visibility()!=AcDb::kVisible)return;
 // Transient explosion leaves inherit their parent's layer; they can have
 // a null layer id and are not independently database-resident objects.
 if(!entity.layerId().isNull()){
  const auto layer=open<AcDbLayerTableRecord>(entity.layerId());
  if(!layer->isPlottable()||layer->isOff()||layer->isFrozen())return;
 }
 if(const auto* curve=AcDbCurve::cast(&entity)){
  double a{},b{};
  if(curve->getStartParam(a)!=Acad::eOk||curve->getEndParam(b)!=Acad::eOk)return;
  const auto* line=AcDbLine::cast(curve);
  const int count=line?1:128;
  AcGePoint3d previous;
  if(curve->getPointAtParam(a,previous)!=Acad::eOk)return;
  for(int k=1;k<=count;++k){
   AcGePoint3d point;
   if(curve->getPointAtParam(a+(b-a)*k/count,point)!=Acad::eOk)return;
   AcDbExtents box(previous,previous);box.addPoint(point);boxes.push_back(box);previous=point;
  }
  return;
 }
 if(AcDbText::cast(&entity)||AcDbMText::cast(&entity)){
  AcDbExtents box;if(entity.getGeomExtents(box)==Acad::eOk)boxes.push_back(box);return;
 }
 TemplateParts parts(&entity);
 if(entity.explode(parts.values)==Acad::eOk)for(int k=0;k<parts.values.length();++k)
  if(const auto* child=AcDbEntity::cast(static_cast<AcRxObject*>(parts.values[k])))productRoomFurnitureInk(*child,boxes,depth+1);
}

void productRoomGraphicObstacles(AcDbDatabase& db,std::vector<AcDbExtents>& boxes){
 for(const auto& [handle,type]:modelTypes(db)){
  const auto entity=open<AcDbEntity>(find(db,ascii(handle)));
  if(!productPlanDimensionVisible(*entity))continue;
  const auto* block=AcDbBlockReference::cast(entity.get());
  const bool furniture=productFurnitureLayerName(symbolName(entity->layerId()))||
   (block&&productFurnitureDefinitionName(symbolName(block->blockTableRecord())));
  if(!furniture&&type!=L"TDbLineStair"&&type!=L"TDbColumn")continue;
  if(furniture)productRoomFurnitureInk(*entity,boxes);
  else{
   AcDbExtents box;
   if(entity->getGeomExtents(box)==Acad::eOk)boxes.push_back(box);
  }
 }
}

std::optional<ProductRoomAreaPlacement> productRoomPlaceArea(
 AcDbDatabase& db,AcDbDatabase& donor,const ProductRoomAreaPair& material,AcDbObjectId layer,
 const ProductRoomGrid& grid,int region,const ProductRoomName* name,double area,double drawingScale,
 const std::vector<AcDbExtents>& obstacles,
 const std::vector<AcGePoint2d>* confirmedBoundary=nullptr,
 const std::wstring& confirmedCaption={}) {
 std::array<AcDbObjectId,3> ids;
 const auto eraseCopies=[&](){
  for(auto id:ids)if(!id.isNull()){try{editObject<AcDbEntity>(id)->erase();}catch(const std::exception&){}}
 };
 try{
  ids[0]=cloneOne(donor,material.body,db);ids[1]=cloneOne(donor,material.exponent,db);
  const auto numeric=bcpproduct::decodeMessage(bcpproduct::formatArea(area));
  const auto caption=confirmedCaption.empty()?numeric:confirmedCaption;
  const bool qualified=caption.size()>numeric.size()&&caption.substr(caption.size()-numeric.size())==numeric;
  const auto qualifier=qualified?caption.substr(0,caption.size()-numeric.size()):std::wstring{};
  if(qualified)ids[2]=cloneOne(donor,material.body,db);
  AcDbObjectId captionStyle;
  if(qualified){
   const auto styles=open<AcDbTextStyleTable>(db.textStyleTableId());
   productStatus(styles->getAt(L"FY_HZ",captionStyle),"Chinese area qualifier font");
  }
  std::array<AcDbExtents,3> boxes;
  for(std::size_t i=0;i<ids.size();++i){
   if(ids[i].isNull())continue;
   auto text=editObject<AcDbText>(ids[i]);
   productStatus(text->setLayer(layer),"set room area layer");
   productStatus(text->setColorIndex(256),"room area ByLayer");
   productStatus(text->setHeight(text->height()*drawingScale),"scale area material");
   if(i==2){
    productStatus(text->setTextStyle(captionStyle),"use Chinese-capable area qualifier style");
    productStatus(text->setHorizontalMode(AcDb::kTextLeft),"area qualifier natural width");
    productStatus(text->setVerticalMode(AcDb::kTextBase),"area qualifier baseline");
    productStatus(text->setWidthFactor(1.),"area qualifier character width");
    productStatus(text->setTextString(qualifier.c_str()),"write area scope qualifier");
   }else if(i==0)productStatus(text->setTextString((qualified?numeric:caption).c_str()),"write computed room area");
   productStatus(text->adjustAlignment(&db),"align room area material");
   productStatus(text->getGeomExtents(boxes[i]),"read rendered room area extent");
  }
  const auto& body=boxes[0];
  const auto& exponent=boxes[1];
  const double bodyWidth=body.maxPoint().x-body.minPoint().x;
  const double exponentWidth=exponent.maxPoint().x-exponent.minPoint().x;
  const double gap=material.gap*drawingScale;
  const double rise=material.rise*drawingScale;
  const double numericWidth=bodyWidth+gap+exponentWidth;
  const double numericHeight=std::max(body.maxPoint().y-body.minPoint().y,rise+exponent.maxPoint().y-exponent.minPoint().y);
  const double qualifierWidth=qualified?boxes[2].maxPoint().x-boxes[2].minPoint().x:0.;
  const double width=std::max(numericWidth,qualifierWidth);
  const double height=numericHeight+(qualified?80*drawingScale+boxes[2].maxPoint().y-boxes[2].minPoint().y:0.);
  const double clearance=30*drawingScale;
  const AcDbExtents searchBounds({grid.xs.front(),grid.ys.front(),0},{grid.xs.back(),grid.ys.back(),0});
  std::vector<const AcDbExtents*> nearby;
  for(const auto& other:obstacles){
   if(!productGenericBoxesOverlap(searchBounds,other,clearance))continue;
   if(name&&!(other.minPoint().distanceTo(name->bounds.minPoint())>1e-5||
              other.maxPoint().distanceTo(name->bounds.maxPoint())>1e-5))continue;
   nearby.push_back(&other);
  }
  std::stable_sort(nearby.begin(),nearby.end(),[](const auto* a,const auto* b){return a->minPoint().x<b->minPoint().x;});
  std::vector<AcDbExtents> furnitureFootprints;
  for(const auto& [handle,type]:modelTypes(db)){
   if(type!=L"AcDbBlockReference")continue;
   const auto block=open<AcDbBlockReference>(find(db,ascii(handle)));
   if(!productPlanDimensionVisible(*block)||!open<AcDbLayerTableRecord>(block->layerId())->isPlottable())continue;
   if(!productFurnitureLayerName(symbolName(block->layerId()))&&!productFurnitureDefinitionName(symbolName(block->blockTableRecord())))continue;
   AcDbExtents box;if(block->getGeomExtents(box)==Acad::eOk&&productGenericBoxesOverlap(searchBounds,box,clearance))
    furnitureFootprints.push_back(box);
  }
  bool preferFurnitureFootprints=true;
  const auto fits=[&](const AcDbExtents& box){
   const auto bottom=box.minPoint(),top=box.maxPoint();
   if(confirmedBoundary){
    for(double x:{bottom.x,top.x})for(double y:{bottom.y,top.y})
     if(!genericInside(*confirmedBoundary,{x,y}))return false;
    for(std::size_t i=0;i<confirmedBoundary->size();++i){
     const auto& a=(*confirmedBoundary)[i];
     const auto& b=(*confirmedBoundary)[(i+1)%confirmedBoundary->size()];
     if(productGenericSegmentHitsBox({a.x,a.y,0},{b.x,b.y,0},box,0))return false;
    }
   }
   const bool inside=grid.at(bottom)==region&&grid.at(top)==region&&
    grid.at(bottom.x,top.y)==region&&grid.at(top.x,bottom.y)==region;
   if(!inside)return false;
   const auto firstX=std::upper_bound(grid.xs.begin(),grid.xs.end(),bottom.x)-grid.xs.begin()-1;
   const auto firstY=std::upper_bound(grid.ys.begin(),grid.ys.end(),bottom.y)-grid.ys.begin()-1;
   for(auto y=firstY;y+1<static_cast<std::ptrdiff_t>(grid.ys.size())&&grid.ys[y]<top.y;++y)
    for(auto x=firstX;x+1<static_cast<std::ptrdiff_t>(grid.xs.size())&&grid.xs[x]<top.x;++x)
     if(grid.cells[grid.index(x,y)]!=region)return false;
   if(preferFurnitureFootprints&&std::any_of(furnitureFootprints.begin(),furnitureFootprints.end(),
    [&](const auto& other){return productGenericBoxesOverlap(box,other,clearance);}))return false;
   for(const auto* other:nearby){
    if(top.x<=other->minPoint().x-clearance)break;
    if(productGenericBoxesOverlap(box,*other,clearance))return false;
   }
   return true;
  };
  const auto areaFits=[&](const AcGePoint3d& bottom){
   return fits(AcDbExtents(bottom,bottom+AcGeVector3d(width,height,0)));
  };
  std::optional<AcGePoint3d> location;
  AcGeVector3d nameShift(0,0,0);
  std::vector<AcGePoint3d> candidates;
  AcGePoint3d desired;
  // Prefer space outside furniture footprints. If the drawing uses a
  // composite block spanning the room, fall back to its actual visible ink.
  // In both passes the area stays directly beneath its own room name.
  for(const bool relaxed:{false,true}){
   preferFurnitureFootprints=!relaxed;
  if(name&&fits(name->bounds)){
   const auto& box=name->bounds;
   const double x=(box.minPoint().x+box.maxPoint().x-width)/2;
   for(double gap:{80.,150.,220.,300.,450.}){
    const AcGePoint3d bottom(x,box.minPoint().y-height-gap*drawingScale,box.minPoint().z);
    if(areaFits(bottom)){location=bottom;break;}
   }
  }
  if(!location&&candidates.empty()){
   AcGePoint3d weighted(0,0,0);
   for(std::size_t y=0;y<grid.height();++y)for(std::size_t x=0;x<grid.width();++x){
    if(grid.cells[grid.index(x,y)]!=region)continue;
    const auto p=grid.centre(x,y);
    const double weight=(grid.xs[x+1]-grid.xs[x])*(grid.ys[y+1]-grid.ys[y])/grid.areas[region];
    weighted.x+=p.x*weight;weighted.y+=p.y*weight;
    candidates.push_back(p);
    // Wall-cut grids can have a single large cell. Search the free portions
    // of that cell, rather than treating its centre as the only valid site.
    const double step=100*drawingScale;
    for(double cy=grid.ys[y]+step/2;cy<grid.ys[y+1];cy+=step)
     for(double cx=grid.xs[x]+step/2;cx<grid.xs[x+1];cx+=step)
      candidates.emplace_back(cx,cy,0);
   }
   desired=name?name->bounds.minPoint()+(name->bounds.maxPoint()-name->bounds.minPoint())*0.5:weighted;
   candidates.push_back(desired);
   std::sort(candidates.begin(),candidates.end(),[&](const auto& a,const auto& b){
    return a.distanceTo(desired)<b.distanceTo(desired);
   });
  }
  if(!location){
   for(const auto& candidate:candidates){
    AcGePoint3d bottom(candidate.x-width/2,candidate.y-height/2,desired.z);
    AcGeVector3d shift(0,0,0);
    if(name){
     shift={candidate.x-desired.x,candidate.y-desired.y,0};
     const auto from=name->bounds.minPoint()+shift,to=name->bounds.maxPoint()+shift;
     if(!fits(AcDbExtents(from,to)))continue;
     bottom.y=from.y-height-80*drawingScale;
    }
    if(areaFits(bottom)){location=bottom;nameShift=shift;break;}
   }
  }
   if(location)break;
  }
  if(!location){
   if(productJob&&name){
    std::ostringstream diag;diag<<std::setprecision(17)<<"{\"name\":"<<jsonUtf8(name->text)
     <<",\"size\":["<<width<<','<<height<<"],\"nameBox\":"<<wholePoints({name->bounds.minPoint(),name->bounds.maxPoint()})
     <<",\"region\":"<<region<<",\"cells\":[";
    bool first=true;
    for(std::size_t y=0;y<grid.height();++y)for(std::size_t x=0;x<grid.width();++x)if(grid.cells[grid.index(x,y)]==region){
     if(!first)diag<<',';first=false;diag<<'['<<grid.xs[x]<<','<<grid.ys[y]<<','<<grid.xs[x+1]<<','<<grid.ys[y+1]<<']';
    }
    diag<<"],\"obstacles\":[";first=true;
    for(const auto& o:obstacles){
     if(o.maxPoint().x<grid.xs.front()||o.minPoint().x>grid.xs.back()||o.maxPoint().y<grid.ys.front()||o.minPoint().y>grid.ys.back())continue;
     if(!first)diag<<',';first=false;diag<<wholePoints({o.minPoint(),o.maxPoint()});
    }
    diag<<"]}";
    const auto diagnostic=productJob->root/fs::u8path("room-placement-"+name->handle+".json");
    if(!fs::exists(diagnostic))writeNew(diagnostic,diag.str());
   }
   eraseCopies();return {};
  }
  const auto numericBottom=*location+AcGeVector3d((width-numericWidth)/2,0,0);
  const auto end=numericBottom+AcGeVector3d(bodyWidth+gap,rise,0);
  wholeTranslate(ids[0],numericBottom-body.minPoint());
  wholeTranslate(ids[1],end-exponent.minPoint());
  if(qualified)wholeTranslate(ids[2],*location+AcGeVector3d((width-qualifierWidth)/2,numericHeight+80*drawingScale,0)-boxes[2].minPoint());
  const ProductRoomAreaPlacement result{axisHandle(ids[0]),axisHandle(ids[1]),nameShift,qualified?axisHandle(ids[2]):std::string{}};
  if(name&&nameShift.length()>1e-5)wholeTranslate(find(db,name->handle),nameShift);
  return result;
 }catch(const std::exception&){
  eraseCopies();return {};
 }
}

struct ProductRoomWriteReceipt {
 int panel;
 std::string name,body,exponent;
 double area;
 std::wstring caption;
 std::size_t subregions{};
 double thresholdArea{};
 std::vector<std::string> mergedDoors;
 std::string oldBody,oldExponent;
 double oldM2{};
 bool existingRetained{};
 std::vector<std::string> combinedNames;
 std::string qualifier;
};

void productWriteRoomAreaReadback(ProductJob& job,AcDbDatabase& saved,
 const std::vector<ProductRoomWriteReceipt>& areas,const fs::path& path,
 const std::vector<ProductPlanNameCheck>& names={}) {
 Working working(&saved);
 std::ostringstream out;
 out.imbue(std::locale::classic());
 out<<std::setprecision(17)<<"{\"savedReadback\":true,\"areas\":[";
 bool first=true;
 for(const auto& area:areas){
  const auto sourceName=std::find_if(names.begin(),names.end(),[&](const auto& name){return name.output==area.name;});
  const auto verifySource=[&](const std::vector<std::string>& objects){
   if(sourceName==names.end())return;
   productCheckPlanSource(job,sourceName->view+1,"room-area",sourceName->source,bcpproduct::PlanCheck::verified,
    objects,u8"已独立重开核净面积及图面落字");
   for(const auto& member:area.combinedNames)
    productCheckPlanSource(job,sourceName->view+1,"room-area",member,bcpproduct::PlanCheck::verified,
     objects,u8"同一已核连续净域合计落字一次，成员房名保留");
  };
  if(area.existingRetained){
   const auto entity=open<AcDbEntity>(find(saved,area.oldBody));
   const auto text=productPlanNameContents(*entity);
   wchar_t* end=nullptr;
   const double actual=std::wcstod(text.c_str(),&end);
   const auto m=text.find(L'm');
   bool verified=productPlanDimensionVisible(*entity)&&productRoomAreaText(text)&&m!=std::wstring::npos&&
    end==text.c_str()+m&&std::abs(actual-area.oldM2)<1e-6;
   if(!area.oldExponent.empty()){
    const auto exponent=open<AcDbEntity>(find(saved,area.oldExponent));
    verified=verified&&productPlanDimensionVisible(*exponent)&&productPlanNameContents(*exponent)==L"2";
   }
   const auto evidence="{\"panel\":"+std::to_string(area.panel)+",\"source\":\""+area.oldBody+
    "\",\"object\":\""+area.oldBody+"\",\"recalculatedM2\":"+std::to_string(area.area)+
    ",\"actualDisplayedM2\":"+std::to_string(actual)+",\"savedReopened\":true,\"result\":\""+
    (verified?"verified-existing-area-label-retained":"pending-existing-area-readback")+"\"}";
   productRecordRuleEvidence(job,"S10",evidence);
   productRecordRuleEvidence(job,"Q05",evidence);
   if(verified){
    auto objects=std::vector<std::string>{area.oldBody};
    if(!area.oldExponent.empty())objects.push_back(area.oldExponent);
    verifySource(objects);
   }else if(job.unifiedPlan){
    if(sourceName==names.end())++job.planUnverifiedNetAreaLabels;
    job.detailPending.push_back({u8"房间面积标注",area.oldBody,u8"保留的净面积文字重开后未核实"});
   }
   continue;
  }
  const auto body=open<AcDbText>(find(saved,area.body));
  const auto exponent=open<AcDbText>(find(saved,area.exponent));
  AcDbExtents bodyBox,exponentBox;
  const auto expected=area.caption.empty()?bcpproduct::decodeMessage(bcpproduct::formatArea(area.area)):area.caption;
  std::wstring actual=body->textString();
  std::optional<AcDbExtents> qualifierBox;
  if(!area.qualifier.empty()){
   const auto qualifier=open<AcDbText>(find(saved,area.qualifier));
   AcDbExtents bounds;
   require(productPlanDimensionVisible(*qualifier)&&symbolName(qualifier->layerId())==L"A-房间名称"&&
    qualifier->getGeomExtents(bounds)==Acad::eOk,"saved area qualifier remains visible");
   qualifierBox=bounds;actual=std::wstring(qualifier->textString())+actual;
  }
  require(productPlanDimensionVisible(*body)&&productPlanDimensionVisible(*exponent)&&
      actual==expected&&std::wstring(exponent->textString())==L"2"&&
      symbolName(body->layerId())==L"A-房间名称"&&symbolName(exponent->layerId())==L"A-房间名称"&&
      body->getGeomExtents(bodyBox)==Acad::eOk&&exponent->getGeomExtents(exponentBox)==Acad::eOk&&
      exponentBox.minPoint().y>bodyBox.minPoint().y&&exponent->height()<body->height(),
      "saved area label, superscript or location differs");
  std::optional<double> centerDelta,verticalGap;
  try{
   const auto room=open<AcDbEntity>(find(saved,area.name));
   AcDbExtents nameBox;
   if(room->getGeomExtents(nameBox)==Acad::eOk){
    AcDbExtents labelBox=bodyBox;labelBox.addExt(exponentBox);
    if(qualifierBox)labelBox.addExt(*qualifierBox);
    centerDelta=(nameBox.minPoint().x+nameBox.maxPoint().x-labelBox.minPoint().x-labelBox.maxPoint().x)/2;
    verticalGap=nameBox.minPoint().y-labelBox.maxPoint().y;
   }
  }catch(const std::exception&){}
  const auto point=[&](const AcGePoint3d& p){out<<'['<<p.x<<','<<p.y<<','<<p.z<<']';};
  if(!first)out<<',';
  first=false;
  out<<"{\"panel\":"<<area.panel<<",\"room\":\""<<area.name<<"\",\"body\":\""<<area.body
     <<"\",\"superscript\":\""<<area.exponent<<"\",\"m2\":"<<area.area<<",\"actualText\":"
     <<jsonUtf8(actual)<<",\"actualSuperscript\":"<<jsonUtf8(exponent->textString())<<",\"bodyAnchor\":";
  point(bodyBox.minPoint());
  out<<",\"superscriptAnchor\":";
  point(exponentBox.minPoint());
  out<<",\"nameCenterDelta\":";if(centerDelta)out<<*centerDelta;else out<<"null";
  out<<",\"nameVerticalGap\":";if(verticalGap)out<<*verticalGap;else out<<"null";
  out<<'}';
  std::ostringstream record;
  record.imbue(std::locale::classic());
  record<<std::setprecision(17)<<"{\"panel\":"<<area.panel<<",\"source\":\""<<area.name
        <<"\",\"object\":\""<<area.body<<"\",\"superscript\":\""<<area.exponent
        <<"\",\"scope\":\"area-label\",\"savedReopened\":true,\"report\":\"room-area-readback.json\",\"m2\":"<<area.area;
  const auto labelEvidence=record.str()+",\"result\":\"verified-area-label\"}";
  auto labelObjects=std::vector<std::string>{area.body,area.exponent};
  if(!area.qualifier.empty())labelObjects.push_back(area.qualifier);
  verifySource(labelObjects);
  productRecordRuleEvidence(job,"S03",labelEvidence);
  productRecordRuleEvidence(job,"S05",labelEvidence);
  if(area.subregions){
   const auto netEvidence=record.str()+",\"result\":\"verified-wall-face-area-recalculation\",\"subregions\":"+
     std::to_string(area.subregions)+"}";
   productRecordRuleEvidence(job,"S01",netEvidence);
   productRecordRuleEvidence(job,"S08",netEvidence);
  }
  if(!area.mergedDoors.empty()){
   auto merge=record.str()+",\"result\":\"verified-wet-region-merge-and-threshold\",\"thresholdM2\":"+
    std::to_string(area.thresholdArea)+",\"doors\":[";
   for(std::size_t i=0;i<area.mergedDoors.size();++i){if(i)merge+=',';merge+='"'+area.mergedDoors[i]+'"';}
   productRecordRuleEvidence(job,"S02",merge+"]}");
  }
  const bool pair=centerDelta&&verticalGap&&std::abs(*centerDelta)<1e-4&&*verticalGap>0.;
  productRecordRuleEvidence(job,"S04",record.str()+",\"result\":\""+(pair?"verified-two-line-room-label":
   area.name.find('-')!=std::string::npos?"verified-unlabelled-room-placement":"pending-room-area-alignment")+"\"}");
  productRecordRuleEvidence(job,"QA09",labelEvidence);
  productRecordRuleEvidence(job,"Q05",labelEvidence);
  productRecordRuleEvidence(job,"L06",record.str()+",\"scope\":\"area-name-alignment\",\"result\":\""+
   (pair?"verified-room-name-area-pair":"pending-room-name-area-pair")+"\"}");
  if(!area.oldBody.empty()){
   const auto present=[&](const std::string& handle){
    if(handle.empty())return false;
    const auto wide=bcpproduct::decodeMessage(handle);
    AcDbObjectId id;
    return saved.getAcDbObjectId(id,false,AcDbHandle(wide.c_str()))==Acad::eOk&&!id.isErased();
   };
   const bool retired=!present(area.oldBody)&&!present(area.oldExponent);
   productRecordRuleEvidence(job,"S10",record.str()+",\"previousBody\":\""+area.oldBody+
    "\",\"previousDisplayedM2\":"+std::to_string(area.oldM2)+",\"result\":\""+
    (retired?"verified-incorrect-old-area-replaced":"pending-old-area-retirement")+"\"}");
  }
 }
 out<<"]}";
 writeNew(path,out.str());
}

void productGenericRooms(ProductJob& job,AcDbDatabase& db,const GenericNativeInput& input,
 const std::vector<bcpproduct::GenericView>& views,std::size_t view,const ProductPlanBounds& bounds,
 const std::vector<std::vector<AcGePoint2d>>& surfaces,
 int panel,AcDbDatabase& reference,double scale,double unitMM,std::vector<ProductPlanNameCheck>& nameChecks,
 bool standalone=false,std::vector<ProductRoomWriteReceipt>* written=nullptr,const std::vector<ConfirmedPlanRoom>& confirmedAreas={},
 std::optional<ProductRoomGrid>* layoutGrid=nullptr) {
 std::vector<std::array<double,4>> rectangles;
 bool exact=true;
 for(const auto index:views[view].walls){
  const auto& wall=input.walls[index];
  if(wall.low)continue;
  if(!wall.orthogonal){exact=false;continue;}
  rectangles.push_back(approvedRect(wall.wall));
 }
 for(const auto& column:input.columns){
  if(bcpproduct::nearestGenericView(views,{column.centre.x,column.centre.y})!=view)continue;
  try{
   if(standalone){
    const auto id=find(db,column.handle);
    const auto grips=chainGrips(id);
    if(grips.size()==9){
     const auto& a=grips[1];const auto& b=grips[2];const auto& c=grips[3];const auto& d=grips[4];
     const auto boxX=std::minmax(a.x,c.x),boxY=std::minmax(a.y,c.y);
     const auto equal=[](double x,double y){return std::abs(x-y)<1e-5;};
     const bool horizontal=equal(a.y,b.y)&&equal(b.x,c.x)&&equal(c.y,d.y)&&equal(d.x,a.x);
     const bool vertical=equal(a.x,b.x)&&equal(b.y,c.y)&&equal(c.x,d.x)&&equal(d.y,a.y);
     if((horizontal||vertical)&&boxX.second-boxX.first>1e-5&&boxY.second-boxY.first>1e-5&&
        (a+(c-a)*0.5).distanceTo(grips[0])<1e-5&&
        (a+(b-a)*0.5).distanceTo(grips[5])<1e-5&&
        (b+(c-b)*0.5).distanceTo(grips[6])<1e-5&&
        (c+(d-c)*0.5).distanceTo(grips[7])<1e-5&&
        (d+(a-d)*0.5).distanceTo(grips[8])<1e-5){
      rectangles.push_back({boxX.first,boxY.first,boxX.second,boxY.second});
      continue;
     }
    }
   }
   const auto points=productColumnCorners(find(db,column.handle));
   const auto alongX=[](const auto& a,const auto& b){return std::abs(a.x-b.x)<1e-5||std::abs(a.y-b.y)<1e-5;};
   if(!alongX(points[0],points[1])||!alongX(points[1],points[2])){exact=false;continue;}
   rectangles.push_back({std::min(points[0].x,points[2].x),std::min(points[0].y,points[2].y),
                         std::max(points[0].x,points[2].x),std::max(points[0].y,points[2].y)});
  }catch(const std::exception& error){
   exact=false;
   if(standalone)job.detailPending.push_back({u8"净域柱轮廓",column.handle,error.what()});
  }
 }
 auto names=productRoomNames(db,input,views,view,nullptr);
 for(const auto& name:nameChecks){
  if(name.view!=view||!name.pendingReason.empty()||name.type!=L"AcDbText")continue;
  const auto role=productRoomRole(name.text);
  AcDbExtents bounds;
  const auto entity=open<AcDbEntity>(find(db,name.source));
  if(role!=ProductRoomRole::other&&entity->getGeomExtents(bounds)==Acad::eOk)
   names.push_back({name.source,name.text,role,bounds});
 }
 const auto shafts=productRoomShafts(db,views,view,names);
 for(const auto& shaft:shafts)rectangles.push_back(shaft.rect);
 auto grid=exact?productRoomGrid(rectangles):std::optional<ProductRoomGrid>{};
 const auto exteriorClosures=standalone?productCloseRoomExteriorOpenings(job,input,views,view,unitMM,rectangles,grid):
                                      std::vector<ProductRoomExteriorOpening>{};
 const auto voidCuts=grid?productRoomVoidBarriers(db,rectangles,names,*grid):std::vector<ProductRoomBarrier>{};
 if(layoutGrid&&grid)*layoutGrid=*grid;
 if(grid)for(auto& name:names)
  name.region=grid->at(name.bounds.minPoint()+(name.bounds.maxPoint()-name.bounds.minPoint())*0.5);
 const auto level=std::to_string(panel+1);
 if(!grid)job.detailPending.push_back({u8"房间面积",level,u8"未能确认本幅平面的完整正交净边界，保留源图面积"});
 AcDbDatabase material(false,true);
 const auto donor=job.profile/L"room-material.dwg";
 const bool materialOpen=material.readDwgFile(donor.c_str(),AcDbDatabase::kForReadAndAllShare,false)==Acad::eOk&&
                         material.closeInput(true)==Acad::eOk;
 if(!materialOpen)job.detailPending.push_back({u8"房间制图资源",level,u8"room-material.dwg 无法读取，检查当前作业资源目录"});
 auto style=productRoomVerifiedStyle(db,true);
 if(style.isNull()&&materialOpen)style=productRoomStyle(material,db);
 if(style.isNull())style=productRoomStyle(reference,db);
 const auto layer=productPlanLayer(db,L"A-房间名称",7);
 const auto confirmations=productPlanDetailConfirmations(job);
 productRestylePlanNames(job,db,input,nameChecks,view,panel,views[view],bounds,surfaces,names,style,layer,unitMM,confirmations,
  materialOpen?&material:nullptr,reference);
 std::size_t renamed=0;
 std::vector<AcDbExtents> obstacles;
 if(standalone)productRoomGraphicObstacles(db,obstacles);
 std::ostringstream audit;
 audit<<std::setprecision(17)<<"{\"panel\":"<<panel<<",\"wallFaceGrid\":"<<(grid?"true":"false")<<",\"deductedShafts\":[";
 bool first=true;
 for(const auto& shaft:shafts){
  if(!first)audit<<',';
  first=false;
  audit<<"{\"outline\":\""<<shaft.outline<<"\",\"areaM2\":"
       <<(shaft.rect[2]-shaft.rect[0])*(shaft.rect[3]-shaft.rect[1])*unitMM*unitMM/1000000<<'}';
 }
 audit<<"],\"voidBoundaries\":[";
 first=true;
 for(const auto& cut:voidCuts){
  if(!first)audit<<',';
  first=false;
  audit<<'"'<<cut.handle<<'"';
 }
 audit<<"],\"virtualExteriorOpenings\":[";
 first=true;
 for(const auto& closure:exteriorClosures){
  if(!first)audit<<',';
  first=false;
  const auto& b=closure.bounds;
  audit<<"{\"opening\":\""<<closure.handle<<"\",\"jambWalls\":[\""<<closure.firstWall<<"\",\""
       <<closure.secondWall<<"\"],\"bounds\":["<<b[0]<<','<<b[1]<<','<<b[2]<<','<<b[3]<<"]}";
 }
 audit<<"],\"names\":[";
 first=true;
 for(const auto& name:names){
  const auto check=std::find_if(nameChecks.begin(),nameChecks.end(),[&](const auto& candidate){return candidate.output==name.handle;});
  const auto& before=check==nameChecks.end()?name.handle:check->source;
  const bool changed=check!=nameChecks.end()&&check->panel==panel+1&&check->pendingReason.empty()&&check->conversionError.empty();
  renamed+=changed?1:0;
  if(!first)audit<<',';
  first=false;
  audit<<"{\"source\":"<<jsonUtf8(bcpproduct::decodeMessage(before))<<",\"handle\":"
       <<jsonUtf8(bcpproduct::decodeMessage(name.handle))<<",\"text\":"<<jsonUtf8(name.text)
       <<",\"region\":"<<name.region<<",\"FY_HZ\":"<<(changed?"true":"false")<<'}';
  obstacles.push_back(name.bounds);
 }
 audit<<"],\"styledNames\":"<<renamed<<",\"areas\":[";
 first=true;
 std::ostringstream withheld;
 withheld<<std::setprecision(17);
 bool firstWithheld=true;
 if(grid){
  auto areaMaterial=materialOpen?productRoomAreaPair(material):std::optional<ProductRoomAreaPair>{};
  if(areaMaterial){
   for(auto id:{areaMaterial->body,areaMaterial->exponent}){
    const auto source=open<AcDbText>(id);
    if(productRoomImportStyle(material,source->textStyle(),db).isNull()){
     areaMaterial.reset();break;
    }
   }
  }
  if(!areaMaterial)
   job.detailPending.push_back({u8"房间面积文字",level,u8"未读到已验证的数值及平方米上标原型，保留可测面积供确认"});
  struct Room {
   std::vector<const ProductRoomName*> labels;
   std::vector<ProductOldNetArea> existingAreas;
    bool excluded{},circulation{},stair{},wet{},washing{},preexisting{},interiorAccess{};
  };
  std::vector<Room> rooms(grid->areas.size());
  const auto stairRegions=standalone?productRoomNativeStairRegions(db,*grid,views,view,unitMM):
                                     std::map<int,std::set<std::string>>{};
  for(const auto& [region,stairs]:stairRegions){rooms[region].stair=true;rooms[region].circulation=true;}
  const auto oldAreas=standalone?productOldNetAreas(db,*grid,views,view):std::vector<ProductOldNetArea>{};
  if(standalone&&oldAreas.empty())productRecordRuleEvidence(job,"S10","{\"panel\":"+std::to_string(panel+1)+
   ",\"scope\":\"visible-source-net-area-labels\",\"result\":\"not-applicable-no-old-net-area-label\"}");
  for(const auto& old:oldAreas){
   if(old.region<0){
    job.detailPending.push_back({u8"旧房间面积归属",old.body,u8"原面积未能唯一匹配闭合净域，保留原对象"});
    productRecordRuleEvidence(job,"S10","{\"panel\":"+std::to_string(panel+1)+",\"object\":\""+old.body+
     "\",\"result\":\"pending-old-area-outside-verified-room\"}");
   }else{
    rooms[old.region].existingAreas.push_back(old);
    rooms[old.region].preexisting=true;
   }
  }
  for(const auto& name:names){
   if(name.region<0){
    const bool confirmed=std::any_of(confirmedAreas.begin(),confirmedAreas.end(),[&](const auto& room){
     if(room.panel!=view+1)return false;
     return std::any_of(nameChecks.begin(),nameChecks.end(),[&](const auto& check){
      return check.output==name.handle&&check.source==room.source;
     });
    });
    if(productRoomHasArea(name.role)&&!confirmed){
     job.detailPending.push_back({u8"房间面积",name.handle,u8"房名未落在可确认的墙内边闭合净域"});
     for(const auto* rule:{"S01","S03","S09"})productRecordRuleEvidence(job,rule,
      "{\"panel\":"+std::to_string(panel+1)+",\"object\":\""+name.handle+"\",\"result\":\"pending-unbounded-room\"}");
    }
    continue;
   }
   auto& room=rooms[name.region];
   room.excluded|=name.role==ProductRoomRole::excluded||(name.role==ProductRoomRole::shaft&&!name.shaftLocated);
   room.circulation|=name.role==ProductRoomRole::stair||name.role==ProductRoomRole::shaft;
    room.stair|=name.role==ProductRoomRole::stair;
   room.wet|=name.role==ProductRoomRole::wet;
   room.washing|=name.role==ProductRoomRole::washing;
   if(productRoomHasArea(name.role))room.labels.push_back(&name);
  }
  for(const auto& item:modelTypes(db)){
   if(item.second!=L"TDbText"&&item.second!=L"AcDbText"&&item.second!=L"TDbOpening"&&item.second!=L"TDbGroupOpening")continue;
   const auto id=find(db,ascii(item.first));
   const auto object=open<AcDbEntity>(id);
   if(object->visibility()!=AcDb::kVisible)continue;
   if(item.second==L"TDbOpening"||item.second==L"TDbGroupOpening"){productGenericTextLeaves(*object,obstacles);continue;}
   const auto type=AcDbText::cast(object.get());
   const auto property=input.properties.find(ascii(item.first));
   const auto field=property!=input.properties.end()?property->second.find(L"文字内容"):bcpproduct::PublicProperties::const_iterator{};
   const std::wstring label=type?type->textString():
    property!=input.properties.end()&&field!=property->second.end()?field->second:L"";
   AcDbExtents box;
   if(standalone||!productRoomAreaText(label)||object->getGeomExtents(box)!=Acad::eOk)continue;
   const auto at=box.minPoint()+(box.maxPoint()-box.minPoint())*0.5;
   const int region=grid->at(at);
   if(region>=0)rooms[region].preexisting=true;
  }
  std::vector<std::size_t> group(rooms.size());
  std::iota(group.begin(),group.end(),0);
  const auto parent=[&](std::size_t k){while(group[k]!=k)k=group[k];return k;};
  struct WetDoor {int a,b;double area;std::string source;};
  std::vector<WetDoor> wetDoors;
  for(const auto& opening:input.openings){
   if(opening.window)continue;
   const auto middle=opening.a+(opening.b-opening.a)*0.5;
   if(bcpproduct::nearestGenericView(views,{middle.x,middle.y})!=view)continue;
   const auto host=productGenericHost(opening,input,views[view]);
   if(!host)continue;
   const auto wall=approvedRect(input.walls[host->wall].wall);
   const auto a=host->vertical?grid->at(wall[0]-0.01,middle.y):grid->at(middle.x,wall[1]-0.01);
   const auto b=host->vertical?grid->at(wall[2]+0.01,middle.y):grid->at(middle.x,wall[3]+0.01);
   if(a<0||b<0||a==b||rooms[a].excluded||rooms[b].excluded)continue;
   if(host->side<0&&!rooms[a].circulation&&!rooms[b].circulation&&(rooms[a].labels.empty()!=rooms[b].labels.empty())){
    const auto entity=open<AcDbEntity>(find(db,opening.handle));
    GenericOpeningInk ink;
    productGenericOpeningInk(*entity,ink);
    if(ink.swing)rooms[rooms[a].labels.empty()?a:b].interiorAccess=true;
   }
   const bool recognizedWetPair=(rooms[a].wet&&rooms[b].washing)||(rooms[b].wet&&rooms[a].washing);
   const bool confirmedWetPair=confirmations.bathroomDoors.count(opening.handle)&&host->side<0&&
    !opening.grouped&&std::abs(opening.sill)*unitMM<=.01&&
    ((rooms[a].wet&&rooms[b].labels.empty())||(rooms[b].wet&&rooms[a].labels.empty()));
   if(!recognizedWetPair&&!confirmedWetPair)continue;
   group[parent(b)]=parent(a);
   wetDoors.push_back({a,b,opening.a.distanceTo(opening.b)*input.walls[host->wall].total,opening.handle});
  }
  for(const auto& [source,domains]:confirmations.combinedDomains){
   const auto check=std::find_if(nameChecks.begin(),nameChecks.end(),[&](const auto& item){return item.source==source;});
   if(check!=nameChecks.end()&&check->view!=view)continue;
   const auto named=check==nameChecks.end()?names.end():std::find_if(names.begin(),names.end(),
    [&](const auto& item){return item.handle==check->output;});
   std::vector<double> measured;
   std::vector<bool> excluded;
   for(std::size_t i=0;i<rooms.size();++i){
    measured.push_back(grid->areas[i]*unitMM*unitMM/1000000.);
    excluded.push_back(rooms[i].excluded||rooms[i].circulation||rooms[i].stair);
   }
   for(std::size_t i=0;i<domains.size();++i){
    const auto& domain=domains[i];
    const int extra=grid->at(domain[0],domain[1]);
    bool passed=named!=names.end()&&named->region>=0&&extra>=0;
    if(passed){
     const auto sourceRoot=parent(static_cast<std::size_t>(named->region));
     const auto targetRoot=parent(static_cast<std::size_t>(extra));
     const bool otherName=targetRoot!=sourceRoot&&std::any_of(rooms.begin(),rooms.end(),[&](const auto& room){
      return !room.labels.empty()&&parent(static_cast<std::size_t>(&room-rooms.data()))==targetRoot;
     });
     passed=!otherName&&bcpproduct::planJoinRoomDomain(group,measured,excluded,
      static_cast<std::size_t>(named->region),static_cast<std::size_t>(extra),domain[2]);
    }
    job.drawingChecks.record(view+1,"confirmed-net-domain",source+"/"+std::to_string(i+1),
     {"S03","Q05"},bcpproduct::PlanReleaseGate::mainPlan,
     passed?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::failed,
     "PLANDETAIL1 source hash and ROOMDOMAIN measured enclosed region",
     passed?"":u8"合并净域未与本层名称、实际闭合区域及确认面积匹配；未扩大或虚构合并边界");
   }
  }
  std::map<std::size_t,std::vector<std::size_t>> groups;
  for(std::size_t i=0;i<rooms.size();++i)groups[parent(i)].push_back(i);
  for(const auto& [key,members]:groups){
   std::vector<const ProductRoomName*> anchors;
   std::set<std::string> stairSources;
    bool excluded=false,existing=false,stair=false;
   double area=0;
   for(auto member:members){
    const auto& room=rooms[member];
    area+=grid->areas[member];
    excluded|=room.excluded;existing|=room.preexisting;
     stair|=room.stair;
    const auto stairRegion=stairRegions.find(static_cast<int>(member));
    if(stairRegion!=stairRegions.end())stairSources.insert(stairRegion->second.begin(),stairRegion->second.end());
    anchors.insert(anchors.end(),room.labels.begin(),room.labels.end());
   }
    std::set<std::string> sourceNames;
    for(const auto* anchor:anchors){
     const auto original=std::find_if(nameChecks.begin(),nameChecks.end(),[&](const auto& check){
      return check.output==anchor->handle;
     });
     sourceNames.insert(original==nameChecks.end()?anchor->handle:original->source);
    }
    // Only the regions explicitly confirmed are handled by the polygon writer.
    // Other rooms and unnamed domains in the same panel still undergo normal accounting.
    if(std::any_of(confirmedAreas.begin(),confirmedAreas.end(),[&](const auto& room){
     return room.panel==view+1&&sourceNames.count(room.source);
    }))continue;
    const auto combined=confirmations.combinedRooms.find(sourceNames);
    const double calculatedM2=area*unitMM*unitMM/1000000;
    const bool confirmedCombined=combined!=confirmations.combinedRooms.end()&&members.size()==1&&
      std::abs(combined->second-calculatedM2)<=1e-5;
    const bool continuousRoom=job.architecturalPlanOnly&&members.size()==1&&anchors.size()>1&&
      sourceNames.size()==anchors.size()&&!existing&&!excluded&&!stair&&combined==confirmations.combinedRooms.end()&&
      std::all_of(anchors.begin(),anchors.end(),[](const auto* label){return productRoomHasArea(label->role);});
    const bool ambiguous=standalone&&anchors.size()>1&&!confirmedCombined&&!continuousRoom&&
     std::any_of(anchors.begin(),anchors.end(),[](const auto* label){
      return label->role!=ProductRoomRole::wet&&label->role!=ProductRoomRole::washing;
     });
    if(excluded||ambiguous||(standalone&&stair)){
    if(!anchors.empty()){
     AcDbExtents envelope;
     for(std::size_t y=0;y<grid->height();++y)for(std::size_t x=0;x<grid->width();++x){
      const auto region=grid->cells[grid->index(x,y)];
      if(region>=0&&parent(static_cast<std::size_t>(region))==key){
       envelope.addPoint({grid->xs[x],grid->ys[y],0});
       envelope.addPoint({grid->xs[x+1],grid->ys[y+1],0});
      }
     }
     if(!firstWithheld)withheld<<',';
     firstWithheld=false;
     withheld<<"{\"areaM2\":"<<area*unitMM*unitMM/1000000<<",\"hasStair\":"<<(stair?"true":"false")
             <<",\"hasMultipleUses\":"<<(ambiguous?"true":"false")<<",\"hasExcludedUse\":"<<(excluded?"true":"false")
             <<",\"stairSources\":[";
     bool firstStair=true;
     for(const auto& source:stairSources){if(!firstStair)withheld<<',';firstStair=false;withheld<<'"'<<source<<'"';}
     withheld<<"],\"boundaryBounds\":"<<wholePoints({envelope.minPoint(),envelope.maxPoint()})<<",\"boundaryEdges\":[";
     const auto inGroup=[&](std::size_t x,std::size_t y){
      const int region=grid->cells[grid->index(x,y)];
      return region>=0&&parent(static_cast<std::size_t>(region))==key;
     };
     bool firstEdge=true;
     const auto edge=[&](std::size_t ax,std::size_t ay,std::size_t bx,std::size_t by){
      if(!firstEdge)withheld<<',';
      firstEdge=false;
      withheld<<wholePoints({{grid->xs[ax],grid->ys[ay],0},{grid->xs[bx],grid->ys[by],0}});
     };
     for(std::size_t y=0;y<grid->height();++y)for(std::size_t x=0;x<grid->width();++x){
      if(!inGroup(x,y))continue;
      if(y==0||!inGroup(x,y-1))edge(x,y,x+1,y);
      if(x+1==grid->width()||!inGroup(x+1,y))edge(x+1,y,x+1,y+1);
      if(y+1==grid->height()||!inGroup(x,y+1))edge(x+1,y+1,x,y+1);
      if(x==0||!inGroup(x-1,y))edge(x,y+1,x,y);
     }
     withheld<<"],\"labels\":[";
     bool firstLabel=true;
     for(const auto& name:names)if(name.region>=0&&parent(static_cast<std::size_t>(name.region))==key){
      if(!firstLabel)withheld<<',';
      firstLabel=false;
      const auto match=std::find_if(nameChecks.begin(),nameChecks.end(),[&](const auto& check){return check.output==name.handle;});
      withheld<<"{\"source\":\""<<(match==nameChecks.end()?name.handle:match->source)<<"\",\"text\":"<<jsonUtf8(name.text)<<'}';
     }
     withheld<<"]}";
    }
    std::string pending=ambiguous?u8"同一闭合净域内有多个用途，待确认逐用途分界或合并计量口径":std::string{};
    if(excluded||stair){
     if(!pending.empty())pending+=u8"。";
     pending+=u8"净域内有楼梯、井道或室外等应排除对象，待确认扣除边界";
    }
    const auto areaEvidence=u8"已实测同一闭合净域 "+bcpproduct::formatArea(calculatedM2)+u8"²。"+pending;
    for(const auto& source:sourceNames){
     const auto key=std::make_tuple(view+1,std::string("room-area"),source);
     if(job.sourceInventory.items.count(key))productCheckPlanSource(job,view+1,"room-area",source,
       bcpproduct::PlanCheck::pending,{},areaEvidence);
    }
    for(const auto* label:anchors){
     job.detailPending.push_back({u8"房间面积",label->handle,pending});
     for(const auto* rule:{"S01","S03","S09","Q05"})productRecordRuleEvidence(job,rule,
      "{\"panel\":"+std::to_string(panel+1)+",\"object\":\""+label->handle+
      "\",\"result\":\"pending-room-area-basis\",\"detail\":"+jsonUtf8(bcpproduct::decodeMessage(pending))+"}");
    }
    continue;
   }
   double threshold=0.;
   std::vector<std::string> mergedDoors;
   for(const auto& door:wetDoors)if(parent(door.a)==key){threshold+=door.area;mergedDoors.push_back(door.source);}
   area+=threshold;
   area*=unitMM*unitMM/1000000;
   if(anchors.empty()&&(!rooms[key].interiorAccess||area<1.0))continue;
   const auto name=std::max_element(anchors.begin(),anchors.end(),[](const auto* a,const auto* b){
    const auto rankA=productRoomNamePriority(*a),rankB=productRoomNamePriority(*b);
    if(rankA!=rankB)return rankA<rankB;
    return a->bounds.minPoint().y<b->bounds.minPoint().y;
   });
   const auto anchor=name==anchors.end()?nullptr:*name;
   const auto region=anchor?anchor->region:static_cast<int>(key);
   const auto handle=anchor?anchor->handle:level+"-"+std::to_string(region+1);
   std::vector<ProductOldNetArea> existingAreas;
   if(standalone&&existing)for(auto member:members)
    existingAreas.insert(existingAreas.end(),rooms[member].existingAreas.begin(),rooms[member].existingAreas.end());
   const auto* old=existingAreas.size()==1&&existingAreas[0].explicitUnit?&existingAreas[0]:nullptr;
   const bool existingMatches=old&&std::abs(std::round(area*100)/100-old->value)<1e-6;
   const auto editable=[&](const std::string& handle){
    if(handle.empty())return true;
    const auto object=open<AcDbEntity>(find(db,handle));
    const auto layer=open<AcDbLayerTableRecord>(object->layerId());
    return object->ownerId()==modelId(db)&&!layer->isLocked();
   };
   const bool replaceOld=old&&anchor&&!existingMatches&&editable(old->body)&&editable(old->exponent);
   std::wstring combinedCaption;
   if(continuousRoom){
    for(const auto* member:anchors){if(!combinedCaption.empty())combinedCaption+=L"、";combinedCaption+=member->text;}
    combinedCaption+=L"合计 "+bcpproduct::decodeMessage(bcpproduct::formatArea(area));
   }
   auto inserted=areaMaterial&&(!existing||replaceOld)?
    productRoomPlaceArea(db,material,*areaMaterial,layer,*grid,region,anchor,area,scale/unitMM/100,obstacles,nullptr,combinedCaption):
    std::optional<ProductRoomAreaPlacement>{};
   if(!inserted&&continuousRoom&&areaMaterial){
    combinedCaption=L"合计 "+bcpproduct::decodeMessage(bcpproduct::formatArea(area));
    inserted=productRoomPlaceArea(db,material,*areaMaterial,layer,*grid,region,anchor,area,scale/unitMM/100,obstacles,nullptr,combinedCaption);
   }
   if(inserted&&replaceOld){
    productStatus(editObject<AcDbEntity>(find(db,old->body))->erase(),"retire verified superseded room area");
    if(!old->exponent.empty())productStatus(editObject<AcDbEntity>(find(db,old->exponent))->erase(),"retire paired area exponent");
   }
   if(inserted&&written){
    ProductRoomWriteReceipt receipt{panel+1,handle,inserted->body,inserted->exponent,area,combinedCaption,members.size(),
      threshold*unitMM*unitMM/1000000.,mergedDoors};
    if(replaceOld){receipt.oldBody=old->body;receipt.oldExponent=old->exponent;receipt.oldM2=old->value;}
    if(confirmedCombined||continuousRoom)receipt.combinedNames.assign(sourceNames.begin(),sourceNames.end());
    receipt.qualifier=inserted->qualifier;
    written->push_back(std::move(receipt));
   }
   if(existingMatches&&written){
    ProductRoomWriteReceipt receipt{panel+1,handle,old->body,old->exponent,area};
    receipt.oldBody=old->body;receipt.oldExponent=old->exponent;receipt.oldM2=old->value;receipt.existingRetained=true;
    if(confirmedCombined)receipt.combinedNames.assign(sourceNames.begin(),sourceNames.end());
    written->push_back(std::move(receipt));
   }
   if(inserted&&anchor&&inserted->nameShift.length()>1e-5){
    const auto check=std::find_if(nameChecks.begin(),nameChecks.end(),[&](const auto& candidate){return candidate.output==handle;});
    if(check!=nameChecks.end()&&check->expectedAnchor)check->expectedAnchor=*check->expectedAnchor+inserted->nameShift;
   }
   if(existing&&!inserted&&!existingMatches){
    std::ostringstream old;
    old.imbue(std::locale::classic());
    old<<std::setprecision(17)<<"{\"panel\":"<<panel+1<<",\"object\":\""<<handle
       <<"\",\"recalculatedM2\":"<<area<<",\"sourceLabels\":[";
    for(std::size_t i=0;i<existingAreas.size();++i){
     if(i)old<<',';
     old<<"{\"handle\":\""<<existingAreas[i].body<<"\",\"m2\":"<<existingAreas[i].value<<'}';
    }
    old<<"],\"result\":\""<<(existingMatches?"matched-source-area-retained":"pending-source-area-comparison")<<"\"}";
    productRecordRuleEvidence(job,"S10",old.str());
   }
   if(confirmedCombined)productRecordRuleEvidence(job,"S09","{\"panel\":"+std::to_string(panel+1)+
    ",\"object\":\""+handle+"\",\"result\":\"verified-source-area-basis-recalculation\",\"m2\":"+
    std::to_string(area)+"}");
   if(!inserted&&!existingMatches)job.detailPending.push_back({u8"房间面积标注",handle,existing?
    (standalone?u8"旧面积值与复算结果不一致，或原标注未能唯一配对；核对报告中的旧标注句柄及复算值":
     u8"已测墙内边面积，区域已有原面积标注，避免重复落字"):
    u8"已测墙内边面积，未找到本房间可放置的空白处或缺面积原型"});
   if(job.unifiedPlan&&!inserted&&!existingMatches&&!anchor)++job.planUnverifiedNetAreaLabels;
   if(!first)audit<<',';
   first=false;
   audit<<"{\"name\":"<<jsonUtf8(anchor?anchor->text:std::wstring{})<<",\"nameHandle\":"<<jsonUtf8(bcpproduct::decodeMessage(handle))
        <<",\"subregions\":"<<members.size()<<",\"areaM2\":"<<area<<",\"text\":";
   if(inserted)audit<<jsonUtf8(bcpproduct::decodeMessage(inserted->body))<<",\"superscript\":"
                    <<jsonUtf8(bcpproduct::decodeMessage(inserted->exponent));
   else audit<<"null";
   if(standalone&&existing){
    audit<<",\"existingValueMatches\":"<<(existingMatches?"true":"false")<<",\"existingAreas\":[";
    for(std::size_t i=0;i<existingAreas.size();++i){
     if(i)audit<<',';
     audit<<"{\"handle\":\""<<existingAreas[i].body<<"\",\"m2\":"<<existingAreas[i].value<<'}';
    }
    audit<<']';
   }
   audit<<'}';
  }
  std::size_t unnamed=0;
  for(std::size_t region=0;region<rooms.size();++region){
   const auto& room=rooms[region];
   if(room.excluded||room.circulation||!room.labels.empty()||room.interiorAccess)continue;
   const auto members=groups.find(parent(region));
   if(members!=groups.end()&&std::any_of(members->second.begin(),members->second.end(),
      [&](std::size_t member){return !rooms[member].labels.empty();}))continue;
   ++unnamed;
   AcDbExtents bounds;
   bool located=false;
   for(std::size_t y=0;y<grid->height();++y)for(std::size_t x=0;x<grid->width();++x){
    if(grid->cells[grid->index(x,y)]!=static_cast<int>(region))continue;
    const AcGePoint3d a(grid->xs[x],grid->ys[y],0),b(grid->xs[x+1],grid->ys[y+1],0);
    if(!located){bounds=AcDbExtents(a,b);located=true;}
    else {bounds.addPoint(a);bounds.addPoint(b);}
   }
   if(!located)continue;
   if(!firstWithheld)withheld<<',';
   firstWithheld=false;
   withheld<<"{\"areaM2\":"<<grid->areas[region]*unitMM*unitMM/1000000.
           <<",\"reason\":\"unidentified-closed-domain\",\"boundaryBounds\":"
           <<wholePoints({bounds.minPoint(),bounds.maxPoint()})<<'}';
  }
  if(job.unifiedPlan)job.planUnassignedNetDomains.insert_or_assign(view+1,static_cast<std::size_t>(unnamed));
  if(unnamed)job.detailPending.push_back({u8"房间用途",level,u8"存在无明确用途的闭合净域，需区分无名房、室外及井道后落面积"});
 }
 audit<<"],\"withheldRegions\":["<<withheld.str()<<"]}";
 writeNew(job.root/(L"通用房间核验_"+std::to_wstring(panel+1)+L".json"),audit.str());
}
