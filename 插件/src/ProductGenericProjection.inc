struct GenericPlanAxis {
 bool vertical{};
 double coordinate{};
 bool negative{},positive{},showNegative{},showPositive{};
 std::string label;
 std::vector<std::pair<double,double>> projectedRanges;
 std::string labelHandle;
 std::vector<std::pair<std::string,std::pair<AcGePoint3d,AcGePoint3d>>> writtenLines;
 std::vector<std::string> sourceWalls,sourceColumns;
 std::vector<std::pair<double,double>> sourceRanges;
};

struct GenericFloorGrid {
 ProductPlanBounds bounds;
 std::vector<GenericPlanAxis> axes;
 std::size_t sourceView{};
};

struct GenericFloorProjection {
 ProductPlanBounds bounds;
 std::size_t reference{};
 AcGeVector3d shift;
};

bool genericInside(const std::vector<AcGePoint2d>& polygon,AcGePoint2d point){
 bool inside=false;
 for(std::size_t i=0,j=polygon.size()-1;i<polygon.size();j=i++){
  const auto a=polygon[j],b=polygon[i];
  if((a.y>point.y)!=(b.y>point.y)&&point.x<a.x+(b.x-a.x)*(point.y-a.y)/(b.y-a.y))inside=!inside;
 }
 return inside;
}

std::vector<std::vector<AcGePoint2d>> productGenericProjectionSurfaces(
 AcDbDatabase& db,const GenericNativeInput& input,const std::vector<bcpproduct::GenericView>& views,
 std::size_t view,const ProductPlanBounds& bounds){
 std::vector<AcGePoint2d> anchors;
 for(const auto& note:input.notes){
  if(note.text!=L"屋顶"&&note.text!=L"屋面"&&note.text!=L"露台"&&note.text!=L"阳台")continue;
  const auto& p=note.anchor;
  if(p.x>=bounds.xmin&&p.x<=bounds.xmax&&p.y>=bounds.ymin&&p.y<=bounds.ymax)continue;
  if(bcpproduct::nearestGenericView(views,{p.x,p.y})==view)anchors.emplace_back(p.x,p.y);
 }
 if(anchors.empty())return {};
 std::vector<std::vector<AcGePoint2d>> surfaces(anchors.size());
 std::vector<double> areas(anchors.size());
 struct Outline {std::vector<AcGePoint2d> points;bool closed{};};
 std::vector<Outline> outlines;
 for(const auto& item:modelTypes(db)){
  if(item.second!=L"AcDbPolyline")continue;
  auto poly=open<AcDbPolyline>(find(db,ascii(item.first)));
  const auto layer=symbolName(poly->layerId());
  if(poly->visibility()!=AcDb::kVisible||(layer!=L"0"&&layer!=L"WALL"&&layer!=L"STAIR"))continue;
  std::vector<AcGePoint2d> points;
  const auto count=poly->numVerts();
  if(count<4)continue;
  for(unsigned i=0;i<count;++i){
   AcGePoint3d point;double bulge{};
   if(poly->getPointAt(i,point)!=Acad::eOk)break;
   if((i+1<count||poly->isClosed())&&(poly->getBulgeAt(i,bulge)!=Acad::eOk||std::abs(bulge)>1e-8))break;
   points.emplace_back(point.x,point.y);
  }
  if(points.size()!=count)continue;
  // A projection belongs to one plan. Shared drawing borders cannot provide
  // support for a terrace just because its caption falls inside the border.
  const auto containsOtherPlan=[&](const auto& candidate){
   const auto& b=candidate.bounds;
   const bool area=b.xmax>b.xmin&&b.ymax>b.ymin;
   if(!area)return false;
   return genericInside(points,{b.xmin,b.ymin})&&genericInside(points,{b.xmax,b.ymin})&&
          genericInside(points,{b.xmax,b.ymax})&&genericInside(points,{b.xmin,b.ymax});
  };
  bool sharedBorder=false;
  for(std::size_t other=0;other<views.size();++other){
   if(other!=view&&containsOtherPlan(views[other])){sharedBorder=true;break;}
  }
  if(sharedBorder)continue;
  outlines.push_back({std::move(points),poly->isClosed()});
 }
 // Open projection outlines may meet an upper wall or another outline
 // anchored there. Loose furnishing and auxiliary frames are excluded.
 const auto adjacent=[&](const AcGePoint2d& p){
  constexpr double junctionAllowance=300.;
  return p.x>=bounds.xmin-junctionAllowance&&p.x<=bounds.xmax+junctionAllowance&&
         p.y>=bounds.ymin-junctionAllowance&&p.y<=bounds.ymax+junctionAllowance;
 };
 const auto joined=[&](const Outline& outline,const AcGePoint2d& p){
  if(adjacent(p))return true;
  for(const auto& other:outlines){
   if(&outline==&other||(!adjacent(other.points.front())&&!adjacent(other.points.back())))continue;
   for(std::size_t k=1;k<other.points.size();++k){
    const auto start=other.points[k-1],end=other.points[k];
    const auto direction=end-start;
    const double length=direction.lengthSqrd();
    if(length<=1e-10)continue;
    const double t=(p-start).dotProduct(direction)/length;
    if(t>=0&&t<=1&&p.distanceTo(start+direction*t)<1e-4)return true;
   }
  }
  return false;
 };
 for(const auto& outline:outlines){
  const auto& points=outline.points;
  if(!outline.closed&&(!joined(outline,points.front())||!joined(outline,points.back())))continue;
  double twiceArea{};
  const auto origin=points.front();
  for(std::size_t i=0,j=points.size()-1;i<points.size();j=i++){
   const auto a=points[j]-origin,b=points[i]-origin;
   twiceArea+=a.x*b.y-a.y*b.x;
  }
  const auto area=std::abs(twiceArea)/2;
  for(std::size_t i=0;i<anchors.size();++i){
   if(area>areas[i]&&genericInside(points,anchors[i])){
    surfaces[i]=points;areas[i]=area;
   }
  }
 }
 surfaces.erase(std::remove_if(surfaces.begin(),surfaces.end(),[](const auto& p){return p.empty();}),surfaces.end());
 return surfaces;
}

std::vector<std::pair<double,double>> productProjectedAxisRanges(
 const std::vector<std::vector<AcGePoint2d>>& surfaces,bool vertical,double coordinate){
 std::vector<std::pair<double,double>> ranges;
 for(const auto& surface:surfaces){
  std::vector<double> intersections;
  for(std::size_t i=0,j=surface.size()-1;i<surface.size();j=i++){
   const auto a=surface[j],b=surface[i];
   const double na=vertical?a.x:a.y,nb=vertical?b.x:b.y;
   if((na>coordinate)==(nb>coordinate))continue;
   const double ta=vertical?a.y:a.x,tb=vertical?b.y:b.x;
   intersections.push_back(ta+(tb-ta)*(coordinate-na)/(nb-na));
  }
  std::sort(intersections.begin(),intersections.end());
  for(std::size_t k=1;k<intersections.size();k+=2)
   if(intersections[k]-intersections[k-1]>1e-5)ranges.emplace_back(intersections[k-1],intersections[k]);
 }
 return ranges;
}

std::optional<GenericFloorProjection> productMatchGenericProjection(
 const ProductPlanBounds& bounds,std::vector<GenericPlanAxis>& axes,
 const std::vector<GenericFloorGrid>& previous,const std::vector<std::vector<AcGePoint2d>>& surfaces){
 if(surfaces.empty())return {};
 constexpr double tolerance=1.;
 for(std::size_t k=previous.size();k-->0;){
  const auto& base=previous[k];
  const auto match=[&](bool vertical,double low,double high,double baseLow,double baseHigh)->std::optional<double>{
   const auto count=[&](double shift){
    if(baseLow+shift>low+tolerance||baseHigh+shift<high-tolerance)return 0;
    return static_cast<int>(std::count_if(axes.begin(),axes.end(),[&](const auto& a){
     return a.vertical==vertical&&std::any_of(base.axes.begin(),base.axes.end(),[&](const auto& b){
      return b.vertical==vertical&&std::abs(a.coordinate-b.coordinate-shift)<tolerance;
     });
    }));
   };
   const double fromLow=low-baseLow,fromHigh=high-baseHigh;
   const int lowMatches=count(fromLow),highMatches=count(fromHigh);
   if(std::max(lowMatches,highMatches)<3)return {};
   if(lowMatches==highMatches&&std::abs(fromLow-fromHigh)>tolerance)return {};
   return lowMatches>=highMatches?fromLow:fromHigh;
  };
  const auto dx=match(true,bounds.xmin,bounds.xmax,base.bounds.xmin,base.bounds.xmax);
  const auto dy=match(false,bounds.ymin,bounds.ymax,base.bounds.ymin,base.bounds.ymax);
  if(!dx||!dy)continue;
  ProductPlanBounds projected{base.bounds.xmin+*dx,base.bounds.ymin+*dy,base.bounds.xmax+*dx,base.bounds.ymax+*dy};
  const auto covers=[&](bool vertical,double position){
   return !productProjectedAxisRanges(surfaces,vertical,position).empty();
  };
  if((projected.xmin<bounds.xmin-tolerance&&!covers(true,projected.xmin))||
     (projected.xmax>bounds.xmax+tolerance&&!covers(true,projected.xmax))||
     (projected.ymin<bounds.ymin-tolerance&&!covers(false,projected.ymin))||
     (projected.ymax>bounds.ymax+tolerance&&!covers(false,projected.ymax)))continue;
  for(const auto& source:base.axes){
   const auto position=source.coordinate+(source.vertical?*dx:*dy);
   auto current=std::find_if(axes.begin(),axes.end(),[&](const auto& a){
    return a.vertical==source.vertical&&std::abs(a.coordinate-position)<tolerance;
   });
   auto ranges=productProjectedAxisRanges(surfaces,source.vertical,position);
   if(current==axes.end()&&ranges.empty())continue;
   const double mid=source.vertical?(projected.ymin+projected.ymax)/2:(projected.xmin+projected.xmax)/2;
   const bool negative=source.negative&&std::any_of(ranges.begin(),ranges.end(),[&](auto r){return r.first<mid;});
   const bool positive=source.positive&&std::any_of(ranges.begin(),ranges.end(),[&](auto r){return r.second>mid;});
   if(current==axes.end()){
    axes.push_back({source.vertical,position,negative,positive,negative,positive,source.label,std::move(ranges)});
   }else{
    current->label=source.label;
    current->negative|=negative;current->positive|=positive;
    current->showNegative|=negative;current->showPositive|=positive;
    current->projectedRanges=std::move(ranges);
   }
  }
  return GenericFloorProjection{projected,base.sourceView,AcGeVector3d(*dx,*dy,0)};
 }
 return {};
}
