struct ProductRoofItem {
 std::string kind,handle;
 std::vector<AcGePoint3d> points;
 double height{};
};

struct ProductRoofCopy {
 std::string source,handle,kind;
 AcGeVector3d shift;
 int side{-1},row{};
 std::vector<double> stations;
};

struct ProductExistingRoofObject {
 std::string handle,role;
 std::wstring type;
 std::vector<AcGePoint3d> points;
};

struct ProductPlanRoof {
 struct Drain {
  std::string source,output;
  AcGePoint3d position;
  std::string plumbingSource,label;
  std::wstring designation,detail;
  std::string fittingSource,connectionSource,connection,outlet;
  std::optional<AcGePoint3d> inlet;
 };
 struct ExistingAxisSide {
  int side{};
  std::string eave,source;
  std::vector<std::pair<std::string,std::size_t>> references;
  std::array<std::string,2> sourceChains;
  std::vector<double> stations;
  std::array<std::string,2> dimensions;
  std::array<std::vector<AcGePoint3d>,2> written;
  double datum{};
 };
 struct ExistingAxisMark {
  int side{};
  std::string line,label,fold;
 };
 std::string sourceOutline,existingTitle,reason;
 int panel{},sourcePanel{};
 AcGeVector3d shift;
 std::vector<ProductRoofItem> items;
 std::vector<ProductRoofCopy> copies;
 std::array<bool,4> axisDimensions{};
 std::vector<ProductExistingRoofObject> retained;
 std::vector<ProductExistingRoofObject> imported;
 std::vector<Drain> drains;
 std::string drainSchedule;
 std::wstring drainScheduleText;
 std::set<std::string> drainageArrows;
 std::vector<ExistingAxisSide> existingAxisSides;
 std::vector<ExistingAxisMark> existingAxisMarks;
 std::array<std::pair<std::string,std::size_t>,2> roofRegistration;
 std::array<std::string,2> wallRegistration;
 std::array<std::pair<std::string,std::size_t>,2> roofYRegistration;
 std::array<std::string,2> wallYRegistration;
 bool projectRegisteredWallAxes{};
 bool automaticAxisProjection{};
 std::optional<AcDbExtents> existingSourceFrame;
 std::string automaticAxisRegistrationStatus;
 std::vector<AcGePoint3d> existingTitlePosition;
};

bool productRoofTitle(std::wstring text){
 text.erase(std::remove_if(text.begin(),text.end(),[](wchar_t c){return iswspace(c)!=0;}),text.end());
 for(const std::wstring prefix:{L"阁楼",L"顶层"})if(text.rfind(prefix,0)==0)text.erase(0,prefix.size());
 return text==L"屋顶平面图"||text==L"屋面平面图"||text==L"屋顶平面"||text==L"屋面平面"||
        text==L"屋顶层平面图"||text==L"屋面层平面图"||text==L"屋顶平面图（待复核）";
}

std::vector<AcGePoint3d> productRoofSimplify(std::vector<AcGePoint3d> points,double eps){
 for(bool changed=true;changed&&points.size()>3;){
  changed=false;
  for(std::size_t i=0;i<points.size();++i){
   const auto a=points[(i+points.size()-1)%points.size()],b=points[i],c=points[(i+1)%points.size()];
   const auto first=b-a,second=c-b;
   if(first.crossProduct(second).length()<=eps*(first.length()+second.length())){
    points.erase(points.begin()+i);
    changed=true;
    break;
   }
  }
 }
 return points;
}

bool productRoofSameOutline(const std::vector<AcGePoint3d>& first,const std::vector<AcGePoint3d>& second,
                            const AcGeVector3d& shift,double eps){
 if(first.size()!=second.size())return false;
 const auto n=first.size();
 for(std::size_t offset=0;offset<n;++offset)for(bool reversed:{false,true}){
  bool equal=true;
  for(std::size_t i=0;i<n&&equal;++i){
   const auto next=(offset+(reversed?n-i:i))%n;
   equal=(first[i]+shift).distanceTo(second[next])<=eps;
  }
  if(equal)return true;
 }
 return false;
}

std::vector<ProductRoofItem> productReadRoofPlan(const fs::path& file,const std::string& digest,ProductPlanRoof& plan){
 std::ifstream input(file);
 input.imbue(std::locale::classic());
 std::string token,source;
 require(bool(input>>token>>source)&&token=="PLANROOFGEOMETRY1"&&source==digest,
         "roof geometry requires the exact original DWG hash");
 require(bool(input>>token>>plan.sourcePanel>>plan.sourceOutline>>plan.shift.x>>plan.shift.y)&&
         token=="SOURCE"&&plan.sourcePanel>0&&std::isfinite(plan.shift.x)&&std::isfinite(plan.shift.y)&&
         plan.sourceOutline.find_first_not_of("0123456789ABCDEFabcdef")==std::string::npos,
         "roof source view, outline and translation");
 std::vector<ProductRoofItem> items;
 std::map<std::string,int> counts;
 while(input>>token&&token!="END"){
  require(items.size()<10000,"roof geometry record limit");
  ProductRoofItem item{token,{},{}};
  unsigned count=2;
  if(token=="OUTLINE")require(bool(input>>count)&&count>=4&&count<=10000,"roof outline point count");
  else if(token=="TITLE"||token=="NOTE")count=1;
  else require(token=="RIDGE"||token=="HIP"||token=="VALLEY"||token=="ARROW","roof geometry record type");
  ++counts[token];
  for(unsigned i=0;i<count;++i){
   AcGePoint3d point;
   require(bool(input>>point.x>>point.y)&&std::isfinite(point.x)&&std::isfinite(point.y)&&
           (item.points.empty()||point.distanceTo(item.points.back())>1e-5),"roof geometry point");
   item.points.push_back(point);
  }
  if(count==1)require(bool(input>>item.height)&&std::isfinite(item.height)&&item.height>0,"roof note height");
  else require(item.points.front().distanceTo(item.points.back())>1e-5,"roof geometry repeated endpoints");
  items.push_back(std::move(item));
 }
 require(token=="END"&&!(input>>token)&&counts["OUTLINE"]==1&&counts["TITLE"]==1&&counts["NOTE"]==1&&
         counts["HIP"]>=4&&counts["ARROW"]>=4,"roof geometry is incomplete");
 return items;
}

std::vector<AcGePoint3d> productRectangleRoofOutline(const GenericNativeInput& input,
  const bcpproduct::GenericView& view,const ProductPlanBounds& bounds,double eps){
 std::array<double,4> outer{};
 for(int side=0;side<4;++side){
  const bool vertical=side<2;
  const double datum=productPlanDatum(bounds,side);
  const double low=vertical?bounds.ymin:bounds.xmin,high=vertical?bounds.ymax:bounds.xmax;
  std::vector<std::pair<double,double>> spans;
  std::optional<double> half;
  for(auto index:view.walls){
   const auto& wall=input.walls[index];
   if(wall.low||!wall.orthogonal)return {};
   const auto a=bcp::bodyCentreStart(wall.wall);
   const bcp::Point b{a.x+wall.wall.end.x-wall.wall.start.x,a.y+wall.wall.end.y-wall.wall.start.y};
   if((std::abs(a.x-b.x)<=eps)!=vertical||std::abs((vertical?a.x:a.y)-datum)>eps)continue;
   const auto face=wall.total/2.;
   if(half&&std::abs(face-*half)>eps)return {};
   half=face;
   spans.emplace_back(vertical?std::min(a.y,b.y):std::min(a.x,b.x),
                      vertical?std::max(a.y,b.y):std::max(a.x,b.x));
  }
  std::sort(spans.begin(),spans.end());
  double covered=low;
  for(const auto& span:spans){
   if(span.first>covered+eps)break;
   covered=std::max(covered,span.second);
  }
  if(!half||covered<high-eps)return {};
  outer[side]=datum+(side%2?1:-1)* *half;
 }
 return {{outer[0],outer[2],0},{outer[1],outer[2],0},{outer[1],outer[3],0},{outer[0],outer[3],0}};
}

std::vector<ProductRoofItem> productDefaultRectangleRoof(const std::vector<AcGePoint3d>& outline,double paper){
 const auto a=outline[0],b=outline[1],c=outline[2],d=outline[3];
 const double width=b.x-a.x,depth=d.y-a.y;
 if(width<5*paper||depth<5*paper)return {};
 const double hip=std::min(width,depth)/2;
 const auto centre=a+(c-a)*.5;
 const auto reach=width>=depth?AcGeVector3d((width-depth)/2,0,0):AcGeVector3d(0,(depth-width)/2,0);
 const auto first=centre-reach,second=centre+reach;
 std::vector<ProductRoofItem> items{{"OUTLINE",{},outline}};
 if(reach.length()>1e-5)items.push_back({"RIDGE",{},{first,second}});
 for(const auto& corner:outline){
  const auto tip=corner.distanceTo(first)<corner.distanceTo(second)?first:second;
  items.push_back({"HIP",{},{corner,tip}});
 }
 const std::array<AcGeVector3d,4> inward{AcGeVector3d(0,1,0),AcGeVector3d(-1,0,0),
                                        AcGeVector3d(0,-1,0),AcGeVector3d(1,0,0)};
 for(std::size_t i=0;i<outline.size();++i){
  const auto at=outline[i]+(outline[(i+1)%outline.size()]-outline[i])*.5;
  const auto travel=inward[i]*hip;
  items.push_back({"ARROW",{},{at+travel*.65,at+travel*.3}});
 }
 items.push_back({"TITLE",{},{AcGePoint3d(centre.x,a.y-65*paper,0)},4.5*paper});
 items.push_back({"NOTE",{},{AcGePoint3d(centre.x,a.y-72.5*paper,0)},3*paper});
 return items;
}

const wchar_t* productRoofText(const std::string& kind){
 return kind=="TITLE"?L"屋顶平面图（待复核）":L"默认四坡屋面，待复核";
}

const wchar_t* productRoofLayer(const std::string& kind){
 if(kind=="TITLE"||kind=="NOTE")return L"PLAN-ROOF-REVIEW-NOTE";
 if(kind=="VALLEY")return L"PLAN-ROOF-REVIEW-VALLEY";
 if(kind=="ARROW")return L"PLAN-ROOF-REVIEW-SLOPE";
 return L"PLAN-ROOF-REVIEW-OUTLINE";
}

void productDrawRoof(AcDbDatabase& db,std::vector<ProductRoofItem>& items){
 AcDbObjectId style;
 {
  const auto table=open<AcDbTextStyleTable>(db.textStyleTableId());
  if(table->getAt(L"FY_HZ",style)!=Acad::eOk)style=db.textstyle();
 }
 for(auto& item:items){
  std::unique_ptr<AcDbEntity> entity;
  if(item.kind=="TITLE"||item.kind=="NOTE"){
   auto text=std::make_unique<AcDbMText>();
   text->setDatabaseDefaults(&db);
   productStatus(text->setTextStyle(style),"roof text style");
   productStatus(text->setTextHeight(item.height),"roof text height");
   productStatus(text->setAttachment(AcDbMText::kMiddleCenter),"roof text alignment");
   productStatus(text->setLocation(item.points.front()),"roof text location");
   text->setContents(productRoofText(item.kind));
   entity=std::move(text);
  }else if(item.kind=="OUTLINE"||item.kind=="ARROW"){
   auto poly=std::make_unique<AcDbPolyline>();
   poly->setDatabaseDefaults(&db);
   if(item.kind=="ARROW"){
    const auto tip=item.points.back();
    const auto direction=(tip-item.points.front()).normal();
    const AcGeVector3d across(-direction.y,direction.x,0);
    const auto head=tip.distanceTo(item.points.front())*.25;
    item.points.push_back(tip-direction*head+across*head*.5);
    item.points.push_back(tip);
    item.points.push_back(tip-direction*head-across*head*.5);
   }
   for(unsigned i=0;i<item.points.size();++i)
    productStatus(poly->addVertexAt(i,AcGePoint2d(item.points[i].x,item.points[i].y)),"roof polyline vertex");
   poly->setClosed(item.kind=="OUTLINE");
   entity=std::move(poly);
  }else{
   entity=std::make_unique<AcDbLine>(item.points[0],item.points[1]);
   entity->setDatabaseDefaults(&db);
  }
  const bool muted=item.kind=="ARROW"||item.kind=="VALLEY";
  productStatus(entity->setLayer(productPlanLayer(db,productRoofLayer(item.kind),muted?8:7)),"roof object layer");
  auto model=editObject<AcDbBlockTableRecord>(modelId(db));
  AcDbObjectId id;
  productStatus(model->appendAcDbEntity(id,entity.get()),"append native roof object");
  item.handle=axisHandle(id);
  entity.release()->close();
 }
}

std::string productRoofClone(AcDbDatabase& db,const std::string& source,const AcGeVector3d& shift){
 const auto original=open<AcDbEntity>(find(db,source));
 auto copy=std::unique_ptr<AcDbEntity>(AcDbEntity::cast(original->clone()));
 require(copy!=nullptr,"editable roof projection clone");
 productStatus(copy->transformBy(AcGeMatrix3d::translation(shift)),"project roof annotation");
 auto model=editObject<AcDbBlockTableRecord>(modelId(db));
 AcDbObjectId id;
 productStatus(model->appendAcDbEntity(id,copy.get()),"append projected roof annotation");
 copy.release()->close();
 return axisHandle(id);
}

void productArrangeExistingRoofDimensions(ProductJob& job,AcDbDatabase& db,ProductPlanRoof& plan,
                                         double scale,double unit){
 if(plan.imported.empty())return;
 std::vector<AcDbExtents> elevationText;
 for(const auto& item:plan.imported)if(item.type==L"TDbSymbElevation")
  productGenericTextLeaves(*open<AcDbEntity>(find(db,item.handle)),elevationText);
 const auto arrange=[&](ProductExistingRoofObject& item){
  if(item.type!=L"TDbDimension2"||item.points.size()<8)return;
  const auto direction=item.points[3]-item.points[2];
  const bool vertical=std::abs(direction.x)*unit<.01;
  if(!vertical&&std::abs(direction.y)*unit>=.01)return;
  const auto id=find(db,item.handle);
  productGenericInteriorClearance(job,db,id,vertical,scale/unit,nullptr,false,false,true,&elevationText);
  const auto actual=chainGrips(id);
  const auto count=(item.points.size()-6)/2+3;
  require(actual.size()==item.points.size()&&
   std::equal(actual.begin(),actual.begin()+count,item.points.begin(),[&](const auto& a,const auto& b){
    return a.distanceTo(b)*unit<.01;
   }),"roof dimension clearance kept baseline and source stations");
  item.points=actual;
 };
 for(auto& item:plan.retained)arrange(item);
 for(auto& item:plan.imported)arrange(item);
}

void productPlaceRoofDrainSchedule(ProductJob& job,AcDbDatabase& db,ProductPlanRoof& plan,double paper){
 std::map<std::wstring,std::vector<std::wstring>> details;
 for(const auto& drain:plan.drains)if(!drain.detail.empty())details[drain.detail].push_back(drain.designation);
 if(details.empty())return;
 const auto body=std::find_if(plan.retained.begin(),plan.retained.end(),[](const auto& object){return object.role=="BODY";});
 if(body==plan.retained.end()||body->points.empty())return;
 AcDbExtents roof(body->points.front(),body->points.front());
 for(const auto& point:body->points)roof.addPoint(point);
 for(auto& [detail,labels]:details){
  std::sort(labels.begin(),labels.end());
   for(const auto& label:labels){
    if(!plan.drainScheduleText.empty())plan.drainScheduleText+=L"\\P";
    plan.drainScheduleText+=label+L"  "+detail;
   }
 }
 std::vector<AcDbExtents> obstacles;
 std::vector<std::pair<AcGePoint3d,AcGePoint3d>> lines;
 for(const auto& [handle,type]:modelTypes(db)){
  const auto entity=open<AcDbEntity>(find(db,ascii(handle)));
  AcDbExtents box;
  if(!productPlanDimensionVisible(*entity)||entity->getGeomExtents(box)!=Acad::eOk||
     !productGenericBoxesOverlap(roof,box,55*paper))continue;
  if(type==L"TDbDimension2"){
   const auto glyphs=productPlanChainGlyphs(db,*entity);
   obstacles.insert(obstacles.end(),glyphs.begin(),glyphs.end());
  }else productGenericTextLeaves(*entity,obstacles);
  if(type==L"AcDbCircle"||type==L"AcDbBlockReference")obstacles.push_back(box);
  if(type==L"TDbSlopeRoof"||type==L"AcDbPolyline"||type==L"AcDbLine"||type==L"TDbSymbArrow"||
     type==L"TDbSymbElevation"||type==L"TDbSymbMultiLeader"){
   GenericOpeningInk ink;
   productGenericOpeningInk(*entity,ink);
   lines.insert(lines.end(),ink.straight.begin(),ink.straight.end());
  }
 }
 const auto low=roof.minPoint(),high=roof.maxPoint();
 for(const double y:{.76,.67,.57,.45,.34,.24,.81,.72,.62,.53,.49,.40,.29,.20,.15,1.23,-.23}){
  for(const double x:{.66,.56,.44,.34,.75,.25,.81,.71,.61,.50,.39,.30,.20,.15,1.22,-.22}){
   auto note=std::make_unique<AcDbMText>();
   note->setDatabaseDefaults(&db);
   productStatus(note->setLayer(productPlanLayer(db,L"DIM-SYMB-RAIN",3)),"roof rainwater schedule layer");
   productStatus(note->setTextHeight(2.5*paper),"roof rainwater schedule height");
   productStatus(note->setAttachment(AcDbMText::kMiddleCenter),"roof rainwater schedule alignment");
   productStatus(note->setLocation({low.x+x*(high.x-low.x),low.y+y*(high.y-low.y),0}),"roof rainwater schedule location");
   note->setContents(plan.drainScheduleText.c_str());
   AcDbExtents box;
   if(note->getGeomExtents(box)!=Acad::eOk||std::any_of(obstacles.begin(),obstacles.end(),[&](const auto& obstacle){
    return productGenericBoxesOverlap(obstacle,box,paper);
   })||std::any_of(lines.begin(),lines.end(),[&](const auto& line){
    return productGenericSegmentHitsBox(line.first,line.second,box,paper);
   }))continue;
   plan.drainSchedule=axisHandle(productAppendPlanDetail(db,std::move(note)));
   return;
  }
 }
 job.detailPending.push_back({u8"纸面图元净空",std::to_string(plan.panel)+"/"+plan.existingTitle,u8"屋面雨水斗规格说明未找到可用空位"});
}

void productPlaceExistingRoofTitle(AcDbDatabase& db,ProductPlanRoof& plan,double paper){
 if(plan.existingTitle.empty()||!plan.axisDimensions[2])return;
 const auto side=std::find_if(plan.existingAxisSides.begin(),plan.existingAxisSides.end(),[](const auto& row){return row.side==2;});
 if(side==plan.existingAxisSides.end()||side->dimensions[0].empty())return;
 const auto id=find(db,plan.existingTitle);
 AcDbExtents title,dimension;
 if(open<AcDbEntity>(id)->getGeomExtents(title)!=Acad::eOk||
  open<AcDbEntity>(find(db,side->dimensions[0]))->getGeomExtents(dimension)!=Acad::eOk)return;
 const auto offset=std::min(0.,dimension.minPoint().y-5*paper-title.maxPoint().y);
 if(offset==0.)return;
 const AcGeVector3d shift(0,offset,0);
 const AcDbExtents moved(title.minPoint()+shift,title.maxPoint()+shift);
 if(productPlanDetailHits(db,{},moved,{plan.existingTitle}))return;
 auto expected=chainGrips(id);
 for(auto& point:expected)point+=shift;
 wholeTranslate(id,shift);
 const auto actual=chainGrips(id);
 require(actual.size()==expected.size()&&std::equal(actual.begin(),actual.end(),expected.begin(),
  [](const auto& a,const auto& b){return a.distanceTo(b)<1e-5;}),"moved existing roof drawing title only");
 plan.existingTitlePosition=actual;
}

ProductPlanRoof productPlanRoofGeometry(ProductJob& job,AcDbDatabase& db,const GenericNativeInput& input,
 const std::vector<bcpproduct::GenericView>& views,const std::vector<bool>& eligible,
 const std::vector<ProductPlanFloorAlignment>& alignment,const std::vector<ProductPlanBounds>& bounds,
 double scale,double unitMM){
 ProductPlanRoof plan;
 if(!job.unifiedPlan)return plan;
 for(const auto* names:{&input.titles,&input.notes})for(const auto& name:*names)if(productRoofTitle(name.text)){
  plan.existingTitle=name.handle;
  plan.reason=u8"源图有独立屋顶平面图名，保留既有图幅。边段用途、源构造与尺寸待逐段核对。";
  if(!job.unifiedRoofPlan.empty()){
   std::ifstream source(job.unifiedRoofPlan);
   std::string token,digest,title,handle;
   if(source>>token>>digest&&token=="PLANEXISTINGROOF1"){
    require(digest==job.sourceHash&&bool(source>>title)&&title==name.handle,
     "existing roof review must bind its original drawing and roof title");
    std::set<std::string> handles;
    while(source>>token&&token!="END"){
     if(token=="PROJECTREGISTEREDWALLAXES"){
      require(!plan.projectRegisteredWallAxes,"one roof projection from registered wall axes");
      plan.projectRegisteredWallAxes=true;
      continue;
     }
     if(token=="XREGISTER"){
      require(plan.wallRegistration[0].empty(),"one horizontal roof registration");
      for(std::size_t i=0;i<2;++i){
       auto& anchor=plan.roofRegistration[i];
       require(bool(source>>plan.wallRegistration[i]>>anchor.first>>anchor.second)&&anchor.second>0,
         "roof registration needs two wall centres and matching roof body points");
      }
      continue;
     }
     if(token=="YREGISTER"){
      require(plan.wallYRegistration[0].empty(),"one vertical roof registration");
      for(std::size_t i=0;i<2;++i){
       auto& anchor=plan.roofYRegistration[i];
       require(bool(source>>plan.wallYRegistration[i]>>anchor.first>>anchor.second)&&anchor.second>0,
         "roof Y registration needs two horizontal wall centres and roof dimension grips");
      }
      continue;
     }
     if(token=="AXISDIM"){
      ProductPlanRoof::ExistingAxisSide side;
      std::size_t count{};
      require(bool(source>>side.side>>side.eave>>side.source>>count)&&side.side>=0&&side.side<4&&
        count>=2&&count<=30&&std::none_of(plan.existingAxisSides.begin(),plan.existingAxisSides.end(),
          [&](const auto& item){return item.side==side.side;}),"unique roof dimension side and source stations");
      for(std::size_t i=0;i<count;++i){
       std::string dimension;
       std::size_t point{};
       require(bool(source>>dimension>>point),"roof axis station must reference a source wall or dimension definition point");
       side.references.emplace_back(dimension,point);
      }
      plan.existingAxisSides.push_back(std::move(side));
      continue;
     }
     if(token=="AXISMARK"){
      ProductPlanRoof::ExistingAxisMark mark;
      require(bool(source>>mark.side>>mark.line>>mark.label)&&mark.side>=0&&mark.side<4&&
       std::none_of(plan.existingAxisMarks.begin(),plan.existingAxisMarks.end(),[&](const auto& known){
        return known.side==mark.side&&known.line==mark.line;
       }),"unique roof axis line and side with a registered native label or hidden mark");
      plan.existingAxisMarks.push_back(std::move(mark));
      continue;
     }
     if(token=="DRAINLABELS"){
      std::string relative,archiveHash;
      std::size_t count{};
      require(bool(source>>std::quoted(relative)>>archiveHash>>count)&&count>=2&&count==plan.drains.size(),
       "registered roof drain labels must cover the located pipes");
      const auto file=job.unifiedRoofPlan.parent_path()/fs::u8path(relative);
      require(fs::is_regular_file(file)&&sha(file)==archiveHash,"roof plumbing archive digest");
      AcDbDatabase plumbing(false,true);
      productStatus(plumbing.readDwgFile(file.c_str(),AcDbDatabase::kForReadAndAllShare,false),"read roof plumbing archive");
      productStatus(plumbing.closeInput(true),"close roof plumbing archive input");
      std::optional<AcGeVector3d> shift;
      std::set<std::wstring> designations;
      for(std::size_t i=0;i<count;++i){
       std::string station,reference;
       require(bool(source>>station>>reference),"roof drain station and plumbing label");
       const auto drain=std::find_if(plan.drains.begin(),plan.drains.end(),[&](const auto& item){return item.source==station;});
       require(drain!=plan.drains.end()&&drain->plumbingSource.empty(),"unique roof drain station");
       const auto id=find(plumbing,reference);
       std::vector<std::wstring> leaves;
       AcGePoint3d point;
       {
        Working context(&plumbing);
        const auto entity=open<AcDbEntity>(id);
        const auto grips=chainGrips(id);
        require(std::wstring(entity->isA()->name())==L"TDbVPipeDim"&&!grips.empty(),"native plumbing plan riser label");
        point=grips.front();
        productOpeningTexts(*entity,leaves);
       }
       require(leaves.size()==1&&!leaves.front().empty()&&designations.insert(leaves.front()).second,
        "unique visible plumbing riser designation");
       const AcGeVector3d delta(drain->position.x-point.x,drain->position.y-point.y,0.);
       require(!shift||(*shift-delta).length()<=.01/unitMM,"plumbing drain labels must match a common roof translation");
       shift=delta;
       drain->plumbingSource=reference;
       drain->designation=leaves.front();
      }
      continue;
     }
     if(token=="DRAINSYSTEM"){
      std::string relative,digest;
      std::size_t count{};
      require(bool(source>>std::quoted(relative)>>digest>>count)&&count>0&&count<=plan.drains.size(),
       "roof drain system records");
      const auto file=job.unifiedRoofPlan.parent_path()/fs::u8path(relative);
      require(fs::is_regular_file(file)&&sha(file)==digest,"roof drain system archive digest");
      AcDbDatabase plumbing(false,true);
      productStatus(plumbing.readDwgFile(file.c_str(),AcDbDatabase::kForReadAndAllShare,false),"read roof drain system");
      productStatus(plumbing.closeInput(true),"close roof drain system input");
      Working context(&plumbing);
      const auto projected=[](AcGePoint3d p){p.z=0;return p;};
      const double tolerance=.01/unitMM;
      for(std::size_t i=0;i<count;++i){
       std::string station,leader,pipe;
       require(bool(source>>station>>leader>>pipe),"roof rainwater system leader and connected pipe");
       const auto drain=std::find_if(plan.drains.begin(),plan.drains.end(),[&](const auto& item){return item.source==station;});
       require(drain!=plan.drains.end()&&!drain->designation.empty()&&drain->detail.empty(),"unique roof drain detail");
       const auto label=open<AcDbEntity>(find(plumbing,leader));
       const auto segment=open<AcDbEntity>(find(plumbing,pipe));
       require(std::wstring(label->isA()->name())==L"TDbSymbMultiLeader"&&
        std::wstring(segment->isA()->name())==L"TDbPipe"&&symbolName(segment->layerId())==L"PIPE-雨水",
        "native rainwater leader and pipe");
       const auto arrows=chainGrips(label->objectId()),grips=chainGrips(segment->objectId());
       require(arrows.size()>=3&&grips.size()>=2&&
        (projected(arrows[2]).distanceTo(projected(grips[0]))<=tolerance||
         projected(arrows[2]).distanceTo(projected(grips[1]))<=tolerance),"rainwater leader is anchored at the cited pipe end");
       std::vector<std::wstring> labels,sizes;
       productOpeningTexts(*label,labels);
       productOpeningTexts(*segment,sizes);
       const auto fitting=std::accumulate(labels.begin(),labels.end(),std::wstring{});
       require(!fitting.empty()&&sizes.size()==1&&!sizes.front().empty(),"read rainwater fitting and diameter from source");
       drain->detail=fitting+L" / "+sizes.front();
       drain->fittingSource=leader;
       drain->connectionSource=pipe;
      }
      continue;
     }
     if(token=="DRAINOFFSET"){
      std::string relative,digest,station,pipe,link,fitting;
      require(bool(source>>std::quoted(relative)>>digest>>station>>pipe>>link>>fitting),"offset roof drain source objects");
      const auto drain=std::find_if(plan.drains.begin(),plan.drains.end(),[&](const auto& item){return item.source==station;});
      require(drain!=plan.drains.end()&&!drain->plumbingSource.empty()&&!drain->inlet,"unique registered offset drain");
      const auto file=job.unifiedRoofPlan.parent_path()/fs::u8path(relative);
      require(fs::is_regular_file(file)&&sha(file)==digest,"offset roof drain archive digest");
      AcDbDatabase plumbing(false,true);
      productStatus(plumbing.readDwgFile(file.c_str(),AcDbDatabase::kForReadAndAllShare,false),"read roof drain offset");
      productStatus(plumbing.closeInput(true),"close roof drain offset input");
      Working context(&plumbing);
      const auto line=open<AcDbLine>(find(plumbing,link));
      const auto pipeEntity=open<AcDbEntity>(find(plumbing,pipe));
      const auto fittingEntity=open<AcDbEntity>(find(plumbing,fitting));
      require(std::wstring(pipeEntity->isA()->name())==L"TDbPipe"&&symbolName(pipeEntity->layerId())==L"VPIPE-雨水"&&
       std::wstring(fittingEntity->isA()->name())==L"TDbEquipment"&&symbolName(fittingEntity->layerId())==L"EQUIP_雨水斗"&&
       symbolName(line->layerId())==L"VPIPE-雨水","roof offset must connect a rainwater riser and rainwater inlet");
      const auto riser=chainGrips(pipeEntity->objectId()),inlet=chainGrips(fittingEntity->objectId()),
       tag=chainGrips(find(plumbing,drain->plumbingSource));
      require(!riser.empty()&&!inlet.empty()&&!tag.empty(),"roof offset points are readable");
      const auto xy=[](AcGePoint3d p){p.z=0;return p;};
      const double tolerance=.01/unitMM;
      auto atRiser=xy(line->startPoint()),atInlet=xy(line->endPoint());
      if(atRiser.distanceTo(xy(riser.front()))>tolerance)std::swap(atRiser,atInlet);
      require(atRiser.distanceTo(xy(riser.front()))<=tolerance&&atRiser.distanceTo(xy(tag.front()))<=tolerance&&
       atInlet.distanceTo(atRiser)>tolerance&&atInlet.distanceTo(xy(inlet.front()))<=2./unitMM,
       "roof offset joins the registered riser and inlet symbol");
      drain->inlet=drain->position+(atInlet-atRiser);
      drain->fittingSource=fitting;
      drain->connectionSource=link;
      continue;
     }
     if(token=="ANNOTATIONS"||token=="DRAINAGE"){
      const bool drainage=token=="DRAINAGE";
      std::string relative,archiveHash,archiveBody,sourceBody;
      std::size_t count{};
      require(bool(source>>std::quoted(relative)>>archiveHash>>archiveBody>>sourceBody>>count)&&
        count>0&&count<=500&&(drainage?plan.drains.empty():plan.imported.empty()),"registered roof annotations and source registration");
      const auto file=job.unifiedRoofPlan.parent_path()/fs::u8path(relative);
      require(fs::is_regular_file(file)&&sha(file)==archiveHash,"roof annotation archive digest");
      AcDbDatabase archive(false,true);
      productStatus(archive.readDwgFile(file.c_str(),AcDbDatabase::kForReadAndAllShare,false),"read roof annotation archive");
      productStatus(archive.closeInput(true),"close roof annotation archive");
      const auto sourceId=find(db,sourceBody),archiveId=find(archive,archiveBody);
      require(std::wstring(open<AcDbEntity>(sourceId)->isA()->name())==L"TDbSlopeRoof"&&
        std::wstring(open<AcDbEntity>(archiveId)->isA()->name())==L"TDbSlopeRoof","matching native roof bodies");
      const auto expected=chainGrips(sourceId);
      const auto prior=chainGrips(archiveId);
      require(expected.size()>=4&&expected.size()==prior.size(),"roof annotation registration grip count");
      const auto shift=expected.front()-prior.front();
      for(std::size_t i=0;i<prior.size();++i)
       require(expected[i].distanceTo(prior[i]+shift)<=.01/unitMM,"roof annotation registration mismatch");
      for(std::size_t i=0;i<count;++i){
       require(bool(source>>handle)&&handles.insert(handle).second,"unique registered roof annotation handle");
       const auto original=find(archive,handle);
       const auto originalEntity=open<AcDbEntity>(original);
       const std::wstring type=originalEntity->isA()->name();
       if(drainage&&type==L"TDbPipe"){
        require(symbolName(originalEntity->layerId())==L"VPIPE-雨水","roof drainage reference must be a rainwater pipe");
        const auto points=chainGrips(original);
        require(!points.empty(),"rainwater pipe has a readable plan station");
        auto point=points.front()+shift;
        point.z=0;
        require(std::all_of(points.begin(),points.end(),[&](const auto& p){
          return std::hypot(p.x+shift.x-point.x,p.y+shift.y-point.y)<=.01/unitMM;
         })&&std::none_of(plan.drains.begin(),plan.drains.end(),[&](const auto& p){return p.position.distanceTo(point)<=.01/unitMM;}),
         "each rainwater outlet must be a distinct vertical pipe station");
        auto marker=std::make_unique<AcDbCircle>(point,AcGeVector3d::kZAxis,scale/unitMM);
        marker->setDatabaseDefaults(&db);
        productStatus(marker->setLayer(productPlanLayer(db,L"DIM-SYMB-RAIN",3)),"roof rainwater outlet mark layer");
        const auto id=productAppendPlanDetail(db,std::move(marker));
        plan.drains.push_back({handle,axisHandle(id),point});
        continue;
       }
       if(drainage){
        std::vector<std::wstring> text;
        productOpeningTexts(*originalEntity,text);
        require(type==L"TDbSymbArrow"&&std::any_of(text.begin(),text.end(),[](const auto& label){
          return label.find(L'%')!=std::wstring::npos;
         }),"drainage arrows need archived slope text");
        plan.drainageArrows.insert(handle);
       }
       require(type==L"TDbSymbArrow"||type==L"TDbSymbElevation"||type==L"TDbText"||
               type==L"TDbDimension2"||type==L"AcDbBlockReference"||type==L"TDbSymbMultiLeader",
               "registered roof annotation class");
       const auto id=cloneOne(archive,original,db);
       wholeTranslate(id,shift);
       plan.imported.push_back({axisHandle(id),handle,type,chainGrips(id)});
      }
      continue;
     }
     require(bool(source>>handle)&&handles.insert(handle).second,"distinct existing roof source objects");
     const auto id=find(db,handle);
     const auto entity=open<AcDbEntity>(id);
     const std::wstring type=entity->isA()->name();
     require((token=="BODY"&&type==L"TDbSlopeRoof")||
             (token=="BOUNDARY"&&AcDbPolyline::cast(entity.get())&&AcDbPolyline::cast(entity.get())->isClosed())||
             (token=="LEVEL"&&type==L"TDbSymbElevation")||(token=="SLOPE"&&type==L"TDbSymbArrow")||
             (token=="DIMENSION"&&type==L"TDbDimension2"),"existing roof object type and role");
     const auto points=chainGrips(id);
     require(productPlanDimensionVisible(*entity)&&points.size()>=2,"existing roof object must have readable geometry");
     plan.retained.push_back({handle,token,type,points});
    }
    require(token=="END"&&!(source>>token)&&!plan.retained.empty(),"complete existing roof review profile");
    if(!plan.drains.empty()){
     const double paper=scale/unitMM;
     const double centreX=std::accumulate(plan.drains.begin(),plan.drains.end(),0.,
      [](double sum,const auto& item){return sum+item.position.x;})/plan.drains.size();
     for(auto& drain:plan.drains){
      if(drain.designation.empty())continue;
      if(drain.inlet){
       auto line=std::make_unique<AcDbLine>(drain.position,*drain.inlet);
       line->setDatabaseDefaults(&db);
       const auto layer=productPlanLayer(db,L"DIM-SYMB-RAIN",3);
       productStatus(line->setLayer(layer),"roof drain offset layer");
       drain.connection=axisHandle(productAppendPlanDetail(db,std::move(line)));
       auto marker=std::make_unique<AcDbCircle>(*drain.inlet,AcGeVector3d::kZAxis,.7*paper);
       marker->setDatabaseDefaults(&db);
       productStatus(marker->setLayer(layer),"roof rainwater inlet layer");
       drain.outlet=axisHandle(productAppendPlanDetail(db,std::move(marker)));
      }
      auto note=std::make_unique<AcDbMText>();
      note->setDatabaseDefaults(&db);
      productStatus(note->setLayer(productPlanLayer(db,L"DIM-SYMB-RAIN",3)),"roof rainwater label layer");
      productStatus(note->setTextHeight(2.5*paper),"roof rainwater label height");
      const bool left=drain.position.x<centreX;
      productStatus(note->setAttachment(left?AcDbMText::kMiddleLeft:AcDbMText::kMiddleRight),"roof rainwater label alignment");
      productStatus(note->setLocation(drain.position+AcGeVector3d(left?2*paper:-2*paper,0,0)),"roof rainwater label station");
      note->setContents(drain.designation.c_str());
      drain.label=axisHandle(productAppendPlanDetail(db,std::move(note)));
     }
    }
    plan.reason=plan.drains.empty()?u8"独立屋面实体、轮廓及源尺寸已登记；分边轴位、施工定位及排水节点仍按实际依据验收。":
     plan.drains.front().label.empty()?u8"归档坡向及雨水立管位置已按原屋面配准落图；落水口规格和与檐沟的接驳做法待核。":
     u8"已配准建施坡向、水施立管编号及已确认雨水斗资料；屋面轴号和剩余檐沟接驳待核。";
   }
  }
  if(job.architecturalPlanOnly&&plan.retained.empty()){
   std::optional<AcDbExtents> roofFrame;
   double smallest=std::numeric_limits<double>::infinity();
   for(const auto& [handle,type]:modelTypes(db)){
    if(type!=L"AcDbPolyline")continue;
    const auto edge=open<AcDbPolyline>(find(db,ascii(handle)));
    AcDbExtents ext;
    if(!edge->isClosed()||symbolName(edge->layerId())!=L"PUB_TITLE"||
       !productPlanDimensionVisible(*edge)||edge->getGeomExtents(ext)!=Acad::eOk)continue;
    const auto a=ext.minPoint(),b=ext.maxPoint();
    const auto size=(b.x-a.x)*(b.y-a.y);
    if(name.anchor.x>=a.x&&name.anchor.x<=b.x&&name.anchor.y>=a.y&&name.anchor.y<=b.y&&size<smallest){
     roofFrame=ext;
     smallest=size;
    }
   }
   plan.existingSourceFrame=roofFrame;
   for(std::size_t view=0;view<views.size();++view){
    if(!eligible[view]||!alignment[view].floor)continue;
    if(!plan.sourcePanel||*alignment[view].floor>*alignment[plan.sourcePanel-1].floor)
     plan.sourcePanel=static_cast<int>(view+1);
   }
   if(plan.sourcePanel&&std::count_if(alignment.begin(),alignment.end(),[&](const auto& row){
      return row.floor==alignment[plan.sourcePanel-1].floor;
     })>1)plan.sourcePanel=0;
   if(roofFrame)for(const auto& [handle,type]:modelTypes(db)){
    const auto key=ascii(handle);
    const auto id=find(db,key);
    const auto entity=open<AcDbEntity>(id);
    const auto layer=symbolName(entity->layerId());
    const auto* outline=AcDbPolyline::cast(entity.get());
    const bool body=type==L"TDbSlopeRoof";
    if(!body&&!(outline&&outline->isClosed()&&(layer==L"平-屋顶"||layer==L"ROOF")))continue;
    AcDbExtents ext;
    if(!productPlanDimensionVisible(*entity)||entity->getGeomExtents(ext)!=Acad::eOk)continue;
    const auto a=ext.minPoint(),b=ext.maxPoint(),lo=roofFrame->minPoint(),hi=roofFrame->maxPoint();
    if(a.x<lo.x||a.y<lo.y||b.x>hi.x||b.y>hi.y)continue;
    const auto points=chainGrips(id);
    if(points.size()>=2)plan.retained.push_back({key,body?"BODY":"BOUNDARY",type,points});
   }
  }
  return plan;
 }
 const auto path=job.unifiedRoofPlan.empty()?job.profile/L"plan-policy/roof-plan.tsv":job.unifiedRoofPlan;
 const double eps=.01/unitMM,paper=scale/unitMM;
 std::size_t sourceView=views.size();
 try{
  if(fs::is_regular_file(path)){
   std::ifstream header(path);
   std::string tag,digest;
   if(bool(header>>tag>>digest)&&tag=="PLANROOFGEOMETRY1"&&digest==job.sourceHash){
    plan.items=productReadRoofPlan(path,job.sourceHash,plan);
    sourceView=static_cast<std::size_t>(plan.sourcePanel-1);
    require(sourceView<views.size()&&eligible[sourceView],"roof source panel is outside the confirmed plan scope");
    const auto outline=open<AcDbPolyline>(find(db,plan.sourceOutline));
    require(outline->isClosed()&&outline->visibility()==AcDb::kVisible,"actual roof source outline");
    std::vector<AcGePoint3d> original;
    for(unsigned i=0;i<outline->numVerts();++i){
     double bulge{};
     AcGePoint3d point;
     productStatus(outline->getBulgeAt(i,bulge),"roof source edge shape");
     productStatus(outline->getPointAt(i,point),"roof source point");
     require(std::abs(bulge)<1e-8,"source-bound roof outline requires straight segments");
     original.push_back(point);
    }
    const auto chosen=std::find_if(plan.items.begin(),plan.items.end(),[](const auto& item){return item.kind=="OUTLINE";});
    require(productRoofSameOutline(productRoofSimplify(std::move(original),eps),
        productRoofSimplify(chosen->points,eps),plan.shift,eps),"roof projection differs from original outline");
   }else if(!job.unifiedRoofPlan.empty())require(false,"roof-plan input does not bind this original DWG");
  }else if(!job.unifiedRoofPlan.empty())require(false,"explicit roof-plan input is absent");
  if(plan.items.empty()){
   if(job.architecturalPlanOnly){
    plan.reason=u8"源方案中未识别到独立屋顶图及可绑定的屋面几何。";
    return plan;
   }
   for(std::size_t i=0;i<views.size();++i){
    if(!eligible[i]||!alignment[i].floor)continue;
    if(sourceView==views.size()||*alignment[i].floor>*alignment[sourceView].floor)sourceView=i;
   }
   if(sourceView==views.size()&&views.size()==1&&eligible[0])sourceView=0;
   if(sourceView==views.size())require(false,"no unique identifiable top floor");
   for(std::size_t i=0;i<views.size();++i)if(i!=sourceView&&eligible[i])
    require(alignment[sourceView].floor&&alignment[i].floor&&
            *alignment[i].floor<*alignment[sourceView].floor,"top floor could not be uniquely identified");
   plan.sourcePanel=static_cast<int>(sourceView)+1;
   auto outline=productRectangleRoofOutline(input,views[sourceView],bounds[sourceView],eps);
   require(outline.size()==4,"top-floor wall geometry is not a simple closed rectangular perimeter");
   plan.items=productDefaultRectangleRoof(outline,paper);
   require(!plan.items.empty(),"insufficient default roof geometry");
  }
 }catch(const std::exception&){
  plan.items.clear();
  plan.reason=u8"缺可核实的独立屋顶图或顶层完整覆盖范围。需核对顶层图幅、外墙/檐边、屋面分区和退让；有来源时提供与本 DWG 指纹匹配的屋面几何。";
 }
 return plan;
}

struct ProductRoofNativeChain {
 std::string handle;
 double baseline{};
 std::vector<double> stations;
};

std::optional<ProductRoofNativeChain> productReadRoofNativeChain(AcDbDatabase& db,
 const std::string& handle,bool alongY,double eps){
 const auto id=find(db,handle);
 const auto entity=open<AcDbEntity>(id);
 if(std::wstring(entity->isA()->name())!=L"TDbDimension2"||!productPlanDimensionVisible(*entity))return {};
 const auto grips=chainGrips(id);
 const auto values=chainValues(id);
 // A dimension measuring Y describes stations of horizontal axes.
 const auto normal=[&](const auto& point){return alongY?point.x:point.y;};
 const auto station=[&](const auto& point){return alongY?point.y:point.x;};
 if(values.empty()||grips.size()!=2*values.size()+6||std::abs(normal(grips[0])-normal(grips[1]))>eps||
    std::abs(station(grips[0])-station(grips[1]))<=eps)return {};
 ProductRoofNativeChain chain{handle,normal(grips[0]),{}};
 for(std::size_t i=0;i<=values.size();++i)chain.stations.push_back(station(grips[i+2]));
 for(std::size_t i=0;i<values.size();++i)
  if(std::abs(std::abs(chain.stations[i+1]-chain.stations[i])-values[i])>eps)return {};
 if(chain.stations.front()>chain.stations.back())std::reverse(chain.stations.begin(),chain.stations.end());
 if(std::adjacent_find(chain.stations.begin(),chain.stations.end(),[&](double a,double b){return b-a<=eps;})!=chain.stations.end())return {};
 return chain;
}

bool productAutoRegisterExistingRoof(AcDbDatabase& db,ProductPlanRoof& plan,
 const std::vector<bool>& generated,const ProductPlanLayoutConfig& layout,
 const std::vector<std::vector<GenericPlanAxis>>& axes){
 const auto reject=[&](const std::string& reason,int side=-1){
  plan.automaticAxisRegistrationStatus=reason;
  if(side>=0)plan.automaticAxisRegistrationStatus+="-"+std::string(1,"WESN"[side]);
  return false;
 };
 if(!plan.existingSourceFrame)return reject("source-frame-unresolved");
 if(plan.sourcePanel<=0||!generated.at(plan.sourcePanel-1))return reject("top-floor-unresolved");
 const double eps=.01/layout.unitMM;
 const auto compare=[&](double a,double b){return std::abs(a-b)<=eps;};
 const auto closes=[&](const auto& total,const auto& series){
  return total.stations.size()==2&&series.stations.size()>2&&
   compare(total.stations.front(),series.stations.front())&&compare(total.stations.back(),series.stations.back());
 };
 const auto view=static_cast<std::size_t>(plan.sourcePanel-1);
 const int panel=static_cast<int>(std::count(generated.begin(),generated.begin()+view,true));
 const auto& floor=axes.at(view);
 const ProductExistingRoofObject* outline=nullptr;
 double biggest=0.;
 for(const auto& item:plan.retained)if(item.role=="BOUNDARY"){
  const auto polygon=open<AcDbPolyline>(find(db,item.handle));
  double area{};
  if(polygon->getArea(area)!=Acad::eOk||area<=eps)continue;
  if(area>biggest+eps){biggest=area;outline=&item;}
  else if(std::abs(area-biggest)<=eps)outline=nullptr;
 }
 if(!outline)return reject("unique-outer-eave-unresolved");
 const auto sourceEave=open<AcDbPolyline>(find(db,outline->handle));
 AcDbExtents eave;
 if(sourceEave->getGeomExtents(eave)!=Acad::eOk)return reject("outer-eave-extents-unreadable");
 std::array<std::vector<ProductRoofNativeChain>,4> roofChains,floorChains;
 const auto boxLow=plan.existingSourceFrame->minPoint(),boxHigh=plan.existingSourceFrame->maxPoint();
 for(const auto& [handle,type]:modelTypes(db)){
  if(type!=L"TDbDimension2")continue;
  const auto key=ascii(handle);
  const auto entity=open<AcDbEntity>(find(db,key));
  AcDbExtents box;
  if(entity->getGeomExtents(box)!=Acad::eOk||box.minPoint().x<boxLow.x||box.maxPoint().x>boxHigh.x||
     box.minPoint().y<boxLow.y||box.maxPoint().y>boxHigh.y)continue;
  for(const bool alongY:{true,false}){
   const auto actual=productReadRoofNativeChain(db,key,alongY,eps);
   if(!actual)continue;
   const auto lo=eave.minPoint(),hi=eave.maxPoint();
   const double low=alongY?lo.x:lo.y,high=alongY?hi.x:hi.y;
   const double first=alongY?lo.y:lo.x,last=alongY?hi.y:hi.x;
   if(actual->stations.front()<first-eps||actual->stations.back()>last+eps)continue;
   const int side=actual->baseline<low-eps?(alongY?0:2):actual->baseline>high+eps?(alongY?1:3):-1;
   if(side>=0)roofChains[side].push_back(*actual);
  }
 }
 for(const auto& role:layout.dimensions){
  if(role.panel!=panel||role.row>2||role.row<1||role.side<0||role.side>3)continue;
  const auto found=productReadRoofNativeChain(db,role.handle,role.side<2,eps);
  if(found)floorChains[role.side].push_back(*found);
 }
 std::array<ProductRoofNativeChain,4> roofRows,floorRows;
 std::array<std::array<std::string,2>,4> originals;
 for(int side=0;side<4;++side){
  const auto pair=[&](const auto& chains,ProductRoofNativeChain& series,std::array<std::string,2>* source){
   int found=0;
   for(const auto& total:chains)for(const auto& row:chains)if(closes(total,row)){
    series=row;
    if(source)*source={total.handle,row.handle};
    ++found;
   }
   return found==1;
  };
  if(!pair(roofChains[side],roofRows[side],&originals[side]))return reject("unique-roof-chain-pair-unresolved",side);
  if(!pair(floorChains[side],floorRows[side],nullptr))return reject("unique-top-floor-chain-pair-unresolved",side);
 }
 AcGeVector3d translation=AcGeVector3d::kIdentity;
 for(const bool vertical:{true,false}){
  const int lower=vertical?2:0,upper=lower+1;
  const auto extrema=[&](const auto& rows){
   return std::make_pair(std::min(rows[lower].stations.front(),rows[upper].stations.front()),
    std::max(rows[lower].stations.back(),rows[upper].stations.back()));
  };
  const auto old=extrema(roofRows),verified=extrema(floorRows);
  const double shift=old.first-verified.first;
  if(!compare(old.second-verified.second,shift))return reject(vertical?"x-boundary-translation-mismatch":"y-boundary-translation-mismatch");
  for(const int side:{lower,upper}){
   const auto& nearby=roofRows[side].stations;
   const auto& current=floorRows[side].stations;
   std::size_t shared=0;
   for(const auto station:current){
    if(std::count_if(floor.begin(),floor.end(),[&](const auto& axis){
       return axis.vertical==vertical&&!axis.labelHandle.empty()&&!axis.writtenLines.empty()&&compare(axis.coordinate,station);
      })!=1)return reject("unique-top-floor-axis-unresolved",side);
    shared+=std::any_of(nearby.begin(),nearby.end(),[&](double existing){return compare(station+shift,existing);});
   }
   // Require distinct evidence on both sides. No approximate match chooses a station.
   if(shared<std::min<std::size_t>(3,current.size())||shared*2<=nearby.size())return reject("native-station-tie-unresolved",side);
  }
  if(vertical)translation.x=shift;else translation.y=shift;
 }
 for(int side=0;side<4;++side){
  const bool alongY=side<2;
  const double shift=alongY?translation.y:translation.x;
  const double low=alongY?eave.minPoint().y:eave.minPoint().x,high=alongY?eave.maxPoint().y:eave.maxPoint().x;
  const auto& stations=floorRows[side].stations;
  if(stations.front()+shift<low-eps||stations.back()+shift>high+eps)return reject("projected-stations-outside-eave",side);
 }
 for(int side=0;side<4;++side){
  ProductPlanRoof::ExistingAxisSide row;
  row.side=side;
  row.eave=outline->handle;
  row.source=roofRows[side].handle;
  row.sourceChains=originals[side];
  const double shift=side<2?translation.y:translation.x;
  for(const auto station:floorRows[side].stations)row.stations.push_back(station+shift);
  plan.existingAxisSides.push_back(std::move(row));
 }
 plan.shift=translation;
 plan.automaticAxisProjection=true;
 plan.automaticAxisRegistrationStatus="matched-native-chain-pairs";
 return true;
}

bool productVerifyExistingRoofAxis(AcDbDatabase& db,const ProductPlanRoof::ExistingAxisMark& mark,
 const ProductPlanRoof::ExistingAxisSide& side,double station,double eps);

void productArrangeAutomaticRoofAxisCircles(ProductJob& job,AcDbDatabase& db,ProductPlanRoof& plan,
 const ProductPlanLayoutConfig& layout,const bcpproduct::PlanAnnotationLayout& spacing){
 if(plan.existingAxisSides.size()!=4||plan.existingAxisMarks.empty())return;
 std::array<double,4> datums{};
 ProductPlanLayoutConfig roofLayout;
 roofLayout.scale=layout.scale;
 roofLayout.unitMM=layout.unitMM;
 roofLayout.paper=layout.paper;
 std::array<bcpproduct::PlanSideDatum,4> placement;
 for(const auto& side:plan.existingAxisSides){
  datums.at(side.side)=side.datum;
  placement.at(side.side)={side.datum,side.eave};
  for(std::size_t row=0;row<side.dimensions.size();++row)
   if(!side.dimensions[row].empty())
    roofLayout.dimensions.push_back({side.dimensions[row],0,side.side,static_cast<int>(row+1)});
 }
 roofLayout.panels.push_back({datums[0],datums[2],datums[1],datums[3]});
 roofLayout.sidePlacement.push_back(std::move(placement));
 for(const auto& mark:plan.existingAxisMarks)if(mark.label!="-")
  roofLayout.axes.push_back({mark.label,0,mark.side>=2?1:0,0});
 const auto id=[&](const std::string& handle){return find(db,handle);};
 productArrangeAxisCircles(db,roofLayout,productPlanLayer(db,L"AXIS",3),spacing,id,&job);
 for(const auto& role:roofLayout.axes){
  const auto original=roofLayout.axisOriginalHandles.find(role.handle);
  if(original==roofLayout.axisOriginalHandles.end())continue;
  const auto mark=std::find_if(plan.existingAxisMarks.begin(),plan.existingAxisMarks.end(),[&](const auto& item){
   return item.label==original->second;
  });
  if(mark==plan.existingAxisMarks.end())continue;
  mark->label=role.handle;
  const auto fold=roofLayout.axisFolds.find({role.handle,mark->side});
  if(fold!=roofLayout.axisFolds.end())mark->fold=fold->second;
 }
 for(const auto& [key,reason]:roofLayout.axisCircleConflicts)
  job.detailPending.push_back({u8"屋面轴圈净距",std::string(1,"WESN"[key.second]),reason});
}

ProductPlanRoof productWritePreparedRoof(ProductJob& job,AcDbDatabase& db,ProductPlanRoof plan,
 const std::vector<bool>& generated,const ProductPlanLayoutConfig& layout,
 const std::vector<std::vector<GenericPlanAxis>>& axes,AcDbObjectId prototype){
 if(!job.unifiedPlan)return plan;
 plan.panel=static_cast<int>(layout.panels.size())+1;
 if(!plan.existingTitle.empty()){
  if(job.architecturalPlanOnly&&plan.existingAxisSides.empty())productAutoRegisterExistingRoof(db,plan,generated,layout,axes);
  const auto spacing=bcpproduct::planAnnotationLayout(layout.scale,layout.unitMM,layout.paper);
  std::optional<double> registeredShiftX;
  if(plan.automaticAxisProjection)registeredShiftX=plan.shift.x;
  for(auto& side:plan.existingAxisSides){
   try{
    const auto retained=[&](const std::string& handle,const std::string& role)->const ProductExistingRoofObject&{
     const auto entry=std::find_if(plan.retained.begin(),plan.retained.end(),[&](const auto& item){
      return item.handle==handle&&item.role==role;
     });
     require(entry!=plan.retained.end(),"roof axis dimensions must reference registered source geometry");
     return *entry;
    };
    const auto& eave=retained(side.eave,"BOUNDARY");
    const bool vertical=side.side<2;
    const int sign=side.side%2?1:-1;
    const auto along=[&](const auto& point){return vertical?point.y:point.x;};
    const auto across=[&](const auto& point){return vertical?point.x:point.y;};
    const auto exterior=std::max_element(eave.points.begin(),eave.points.end(),[&](const auto& a,const auto& b){
     return sign*across(a)<sign*across(b);
    });
    const auto edge=across(*exterior);
    side.datum=edge;
    const double eps=.01/layout.unitMM;
    const auto range=std::minmax_element(eave.points.begin(),eave.points.end(),[&](const auto& a,const auto& b){
     return along(a)<along(b);
    });
    if(!plan.automaticAxisProjection){
     const auto& evidence=retained(side.source,"DIMENSION");
     require(std::abs(across(evidence.points[0])-across(evidence.points[1]))<=eps&&
       sign*(across(evidence.points.front())-edge)>0,"source roof dimension must belong to the declared eave side");
    }
    const auto wallX=[&](const std::string& handle){
     const auto id=find(db,handle);
     const auto entity=open<AcDbEntity>(id);
     require(!entity->isAProxy()&&std::wstring(entity->isA()->name())==L"TDbWall","roof projection needs a native wall centre");
     const auto points=chainGrips(id);
     require(points.size()>=2&&std::abs(points[0].x-points[1].x)<=eps&&
       std::abs(points[0].y-points[1].y)>eps,"roof projection needs a vertical wall centre");
     return points[0].x;
    };
    std::optional<double> roofShiftX;
    for(const auto& [handle,index]:side.references){
     double station{};
     if(index==0){
      require(!vertical&&!plan.wallRegistration[0].empty(),"wall station needs a horizontal roof registration");
      if(!roofShiftX){
       std::array<double,2> wall{},shift{};
       for(std::size_t i=0;i<2;++i){
        const auto& [roof,index]=plan.roofRegistration[i];
        const auto& body=retained(roof,"BODY");
        require(index<=body.points.size(),"registration point on roof body");
        wall[i]=wallX(plan.wallRegistration[i]);
        shift[i]=body.points[index-1].x-wall[i];
       }
       require(std::abs(wall[0]-wall[1])>eps&&std::abs(shift[0]-shift[1])<=eps,
         "roof wall references must agree on translation");
       roofShiftX=shift[0];
       registeredShiftX=shift[0];
      }
      station=wallX(handle)+*roofShiftX;
     }else{
      const auto& source=retained(handle,"DIMENSION");
      const auto segments=chainValues(find(db,handle)).size();
      require(index<=segments+1&&source.points.size()==2*segments+6&&
       std::abs(across(source.points[0])-across(source.points[1]))<=eps,"readable source roof station index and direction");
      station=along(source.points[index+1]);
     }
     require(station>=along(*range.first)-eps&&station<=along(*range.second)+eps,"roof station is outside the verified eave range");
     side.stations.push_back(station);
    }
    std::sort(side.stations.begin(),side.stations.end());
    require(std::adjacent_find(side.stations.begin(),side.stations.end(),[&](double a,double b){return b-a<=eps;})==side.stations.end(),
      "roof axis stations must be distinct");
    std::vector<AcGePoint3d> points;
    for(const auto station:side.stations)points.push_back(vertical?AcGePoint3d(edge,station,0):AcGePoint3d(station,edge,0));
    for(std::size_t row=0;row<2;++row){
     const auto stations=row?points:std::vector<AcGePoint3d>{points.front(),points.back()};
     const auto offset=(vertical?-sign:sign)*(row?spacing.second:spacing.total);
     const auto id=productTryPlanChain(job,db,prototype,stations,offset,plan.panel,side.source,u8"屋面轴距", "D27");
     if(!id)break;
     side.dimensions[row]=axisHandle(*id);
     double gap{};
     {
      auto entity=editObject<AcDbEntity>(*id);
      productStatus(entity->setLayer(productPlanLayer(db,L"AXIS",3)),"roof axis dimension layer");
      TemplateParts parts(entity.get());
      productStatus(entity->explode(parts.values),"read roof dimension witness offset");
      require(parts.values.length()>0,"roof dimension display parts");
      const auto* segment=AcDbDimension::cast(static_cast<AcRxObject*>(parts.values[0]));
      require(segment,"roof dimension display type");
      gap=segment->dimexo()*segment->dimscale();
     }
     const auto before=chainGrips(*id);
     const auto definition=edge+sign*(spacing.start-gap);
     for(std::size_t i=0;i<stations.size();++i){
      auto point=before[i+2];
      if(vertical)point.x=definition;else point.y=definition;
      moveDimensionGrip(*id,static_cast<int>(i+2),point);
     }
     moveDimensionGrip(*id,0,before[0]);
     side.written[row]=chainGrips(*id);
    }
    if(side.dimensions[1].empty()){
     if(!side.dimensions[0].empty())productStatus(editObject<AcDbEntity>(find(db,side.dimensions[0]))->erase(),"remove incomplete roof axis dimension pair");
     side.dimensions={};
     continue;
    }
    plan.axisDimensions[side.side]=true;
   }catch(const std::runtime_error& error){
    for(auto& handle:side.dimensions)if(!handle.empty()){
     productStatus(editObject<AcDbEntity>(find(db,handle))->erase(),"remove failed roof axis dimension");
     handle.clear();
    }
    job.detailPending.push_back({u8"屋面轴距",side.source,error.what()});
   }
  }
  std::optional<double> registeredShiftY;
  if(plan.automaticAxisProjection)registeredShiftY=plan.shift.y;
  if(plan.projectRegisteredWallAxes&&!plan.wallYRegistration[0].empty()){
   std::array<double,2> centres{},shifts{};
   for(std::size_t i=0;i<2;++i){
    const auto wall=open<AcDbEntity>(find(db,plan.wallYRegistration[i]));
    const auto points=chainGrips(wall->objectId());
    require(std::wstring(wall->isA()->name())==L"TDbWall"&&points.size()>=2&&
      std::abs(points[0].y-points[1].y)<.01/layout.unitMM&&
      std::abs(points[0].x-points[1].x)>.01/layout.unitMM,"roof Y tie requires horizontal native walls");
    const auto& [handle,index]=plan.roofYRegistration[i];
    const auto ref=std::find_if(plan.retained.begin(),plan.retained.end(),[&](const auto& item){
      return item.handle==handle&&item.role=="DIMENSION";
    });
    require(ref!=plan.retained.end()&&index<=ref->points.size(),"roof Y tie needs a retained dimension grip");
    centres[i]=points[0].y;shifts[i]=ref->points[index-1].y-centres[i];
   }
   require(std::abs(centres[0]-centres[1])>.01/layout.unitMM&&
     std::abs(shifts[0]-shifts[1])<.01/layout.unitMM,"roof Y references disagree on translation");
   registeredShiftY=shifts[0];
  }
  if((plan.projectRegisteredWallAxes||plan.automaticAxisProjection)&&registeredShiftX)for(const bool vertical:{true,false}){
   if(!vertical&&!registeredShiftY)continue;
   const int negativeSide=vertical?2:0,positiveSide=negativeSide+1;
   const auto normal=[&](const AcGePoint3d& p){return vertical?p.x:p.y;};
   const auto along=[&](const AcGePoint3d& p){return vertical?p.y:p.x;};
   const auto point=[&](double n,double a){return vertical?AcGePoint3d(n,a,0):AcGePoint3d(a,n,0);};
   const auto south=std::find_if(plan.existingAxisSides.begin(),plan.existingAxisSides.end(),[&](const auto& side){return side.side==negativeSide;});
   const auto north=std::find_if(plan.existingAxisSides.begin(),plan.existingAxisSides.end(),[&](const auto& side){return side.side==positiveSide;});
   if(south!=plan.existingAxisSides.end()&&north!=plan.existingAxisSides.end()&&plan.axisDimensions[negativeSide]&&plan.axisDimensions[positiveSide]){
    const auto outline=std::find_if(plan.retained.begin(),plan.retained.end(),[&](const auto& item){return item.handle==north->eave;});
    std::vector<AcGePoint2d> projection;
    for(const auto& point:outline->points)projection.emplace_back(point.x,point.y);
    const double eps=.01/layout.unitMM;
    for(std::size_t floorIndex=0;floorIndex<axes.size();++floorIndex){
     const auto& floor=axes[floorIndex];
     const auto hasWall=[&](const auto& wall){return std::any_of(floor.begin(),floor.end(),[&](const auto& axis){
      return axis.vertical&&std::find(axis.sourceWalls.begin(),axis.sourceWalls.end(),wall)!=axis.sourceWalls.end();
     });};
     if(plan.automaticAxisProjection){
      if(floorIndex+1!=static_cast<std::size_t>(plan.sourcePanel))continue;
     }else if(!hasWall(plan.wallRegistration[0])||!hasWall(plan.wallRegistration[1]))continue;
     for(const auto& axis:floor){
      if(axis.vertical!=vertical||axis.label.empty()||axis.labelHandle.empty()||axis.writtenLines.empty())continue;
      const double x=axis.coordinate+(vertical?*registeredShiftX:*registeredShiftY);
      const auto matches=[&](const auto& side){return std::any_of(side.stations.begin(),side.stations.end(),
       [&](double station){return std::abs(x-station)<=eps;});};
      const bool low=matches(*south),high=matches(*north);
      if(!low&&!high)continue;
      const auto spans=productProjectedAxisRanges({projection},vertical,x);
      if(spans.size()!=1||spans.front().second-spans.front().first<=eps)continue;
      const double start=low?south->datum-spacing.start:spans.front().first;
      const double finish=high?north->datum+spacing.start:spans.front().second;
      std::array<AcDbObjectId,2> labels;
      AcDbObjectId line;
      try{
       std::vector<std::string> sources;
       if(plan.automaticAxisProjection){
        const int floorPanel=static_cast<int>(std::count(generated.begin(),generated.begin()+floorIndex,true));
        for(const auto& role:layout.axes){
         const auto before=layout.axisOriginalHandles.find(role.handle);
         if(role.panel==floorPanel&&(before==layout.axisOriginalHandles.end()?role.handle:before->second)==axis.labelHandle)
          sources.push_back(role.handle);
        }
       }else sources.push_back(axis.labelHandle);
       for(const auto& handle:sources){
        const auto source=find(db,handle);
        const auto before=chainGrips(source);
        require(before.size()==4||before.size()==8,"registered roof axis label grips");
        for(std::size_t i=0;i<before.size();i+=4){
         const bool top=along(before[i+1])>along(before[i]);
         if(!(top?high:low))continue;
         auto& label=labels[top];
         require(label.isNull(),"one native source label per projected axis side");
         label=productCloneAxisLabel(db,source);
         if(before.size()==8)productKeepAxisLabelEnd(db,label,vertical,before[i+1]);
         const auto grips=chainGrips(label);
         require(grips.size()==4&&(along(grips[1])>along(grips[0]))==top,"registered roof axis label side");
         const auto anchor=point(x,top?finish:start);
         if(plan.automaticAxisProjection){
          // A floor label can already have a shortened tail from its own fold.
          const auto centre=point(x,top?north->datum+spacing.circleCentre:south->datum-spacing.circleCentre);
          wholeTranslate(label,centre-grips[1]);
          if(chainGrips(label).front().distanceTo(anchor)>eps){
           const auto straight=label;
           label=productAxisLabelWithTail(db,straight,anchor);
           productStatus(editObject<AcDbEntity>(straight)->erase(),"retire relocated roof axis label");
          }
         }else wholeTranslate(label,anchor-grips[0]);
         const auto moved=chainGrips(label);
         require(moved.size()==4&&std::abs(normal(moved[0])-x)<=eps&&std::abs(along(moved[0])-(top?finish:start))<=eps,
          "registered roof label must attach to the actual station");
        }
       }
       if(labels[0].isNull()&&labels[1].isNull()&&!plan.automaticAxisProjection)continue;
       auto entity=std::make_unique<AcDbLine>(point(x,start),point(x,finish));
       entity->setDatabaseDefaults(&db);
       const auto sourceLine=open<AcDbEntity>(find(db,axis.writtenLines.front().first));
       productStatus(entity->setLayer(sourceLine->layerId()),"roof logical axis layer");
       productStatus(entity->setLinetype(sourceLine->linetypeId()),"roof logical axis linetype");
       entity->setColor(sourceLine->color());
       line=productAppendPlanDetail(db,std::move(entity));
       if(!labels[0].isNull())plan.existingAxisMarks.push_back({negativeSide,axisHandle(line),axisHandle(labels[0])});
       if(!labels[1].isNull())plan.existingAxisMarks.push_back({positiveSide,axisHandle(line),axisHandle(labels[1])});
       if(plan.automaticAxisProjection){
        if(low&&labels[0].isNull())plan.existingAxisMarks.push_back({negativeSide,axisHandle(line),"-"});
        if(high&&labels[1].isNull())plan.existingAxisMarks.push_back({positiveSide,axisHandle(line),"-"});
       }
      }catch(const std::exception& error){
       for(const auto& id:{labels[0],labels[1],line})if(!id.isNull()&&!id.isErased())editObject<AcDbEntity>(id)->erase();
       job.detailPending.push_back({u8"屋面轴网",axis.label,error.what()});
      }
     }
     break;
    }
   }
  }
  if(plan.automaticAxisProjection){
   productArrangeAutomaticRoofAxisCircles(job,db,plan,layout,spacing);
   std::set<std::string> lines,labelled,outputs;
   for(const auto& mark:plan.existingAxisMarks){
    lines.insert(mark.line);
    outputs.insert(mark.line);
    if(mark.label!="-"){
     outputs.insert(mark.label);
     labelled.insert(mark.line);
    }
    if(!mark.fold.empty())outputs.insert(mark.fold);
   }
   bool complete=lines==labelled;
   for(const auto& side:plan.existingAxisSides){
    complete&=plan.axisDimensions[side.side];
    for(const auto& handle:side.dimensions)if(!handle.empty())outputs.insert(handle);
    for(const auto station:side.stations)complete&=std::count_if(plan.existingAxisMarks.begin(),plan.existingAxisMarks.end(),
     [&](const auto& mark){
      return mark.side==side.side&&productVerifyExistingRoofAxis(db,mark,side,station,.01/layout.unitMM);
     })==1;
   }
   if(complete){
    for(const auto& side:plan.existingAxisSides)for(const auto& handle:side.sourceChains){
     productStatus(editObject<AcDbEntity>(find(db,handle))->erase(),"replace reconciled roof source axis chain");
     job.planDimensionExclusions.insert(handle);
    }
    plan.reason=u8"已从原屋顶图相对侧轴距与顶层真实轴网投影，按同一平移重建原生屋面轴距。";
   }else{
    for(const auto& handle:outputs)
     productStatus(editObject<AcDbEntity>(find(db,handle))->erase(),"remove incomplete automatic roof projection");
    plan.existingAxisSides.clear();
    plan.existingAxisMarks.clear();
    plan.axisDimensions={};
    plan.automaticAxisProjection=false;
    plan.automaticAxisRegistrationStatus="projection-readback-incomplete";
    plan.reason=u8"屋面源轴距和顶层轴网已配准，未逐站生成通过，保留原屋面轴距。";
   }
  }
  return plan;
 }
 if(plan.items.empty())return plan;
 const auto sourceView=static_cast<std::size_t>(plan.sourcePanel-1);
 if(!generated.at(sourceView)){
  plan.items.clear();
  plan.reason=u8"屋面关联的主体图幅未完成，保留屋面依据并列待核。";
  return plan;
 }
 const double eps=.01/layout.unitMM,paper=layout.scale/layout.unitMM;
 if(plan.sourceOutline.empty()){
  double right=-std::numeric_limits<double>::infinity();
  for(const auto& [raw,type]:modelTypes(db)){
   const auto entity=open<AcDbEntity>(find(db,ascii(raw)));
   AcDbExtents ext;
   if(entity->visibility()==AcDb::kVisible&&entity->getGeomExtents(ext)==Acad::eOk)right=std::max(right,ext.maxPoint().x);
  }
  require(std::isfinite(right),"roof sheet has no finite drawing extent");
  const auto& outline=plan.items.front().points;
  plan.shift=AcGeVector3d(right+80*paper-outline.front().x,0,0);
  for(auto& item:plan.items)for(auto& point:item.points)point+=plan.shift;
 }
 const int sourcePanel=static_cast<int>(std::count(generated.begin(),generated.begin()+sourceView,true));
 const auto polygon=std::find_if(plan.items.begin(),plan.items.end(),[](const auto& item){return item.kind=="OUTLINE";});
 std::vector<AcGePoint2d> outline;
 ProductPlanBounds outlineBounds{1e100,1e100,-1e100,-1e100};
 for(const auto& p:polygon->points){
  outline.emplace_back(p.x,p.y);
  outlineBounds.xmin=std::min(outlineBounds.xmin,p.x);outlineBounds.xmax=std::max(outlineBounds.xmax,p.x);
  outlineBounds.ymin=std::min(outlineBounds.ymin,p.y);outlineBounds.ymax=std::max(outlineBounds.ymax,p.y);
 }
 const auto wallDatum=productPlacementBounds(layout,sourcePanel);
 std::array<std::vector<double>,4> endStations;
 double southCircles=std::numeric_limits<double>::infinity();
 for(const auto& role:layout.axes){
  if(role.panel!=sourcePanel)continue;
  const auto source=layout.axisOriginalHandles.find(role.handle);
  const auto axis=std::find_if(axes[sourceView].begin(),axes[sourceView].end(),[&](const auto& a){
   return a.labelHandle==(source==layout.axisOriginalHandles.end()?role.handle:source->second);
  });
  if(axis==axes[sourceView].end())continue;
  const bool vertical=axis->vertical;
  const auto grips=chainGrips(find(db,role.handle));
  if(grips.size()!=4&&grips.size()!=8)continue;
  std::vector<std::pair<int,double>> ends;
  for(std::size_t i=0;i<grips.size();i+=4){
   const auto side=productAxisEnd(wallDatum,vertical,grips[i],grips[i+1]).side;
   if(!productFoldPoints(db,layout,role.handle,side).empty()||
      std::abs(productAxisStation(grips[i],vertical)-axis->coordinate)>eps)break;
   ends.emplace_back(side,productAxisNormal(grips[i],vertical));
  }
  if(ends.size()!=grips.size()/4)continue;
  const double shiftNormal=vertical?plan.shift.y:plan.shift.x;
  const double shiftStation=vertical?plan.shift.x:plan.shift.y;
  const auto projected=productProjectedAxisRanges({outline},vertical,axis->coordinate+shiftStation);
  for(const auto& [handle,endpoints]:axis->writtenLines){
   const double lo=productAxisNormal(endpoints.first,vertical),hi=productAxisNormal(endpoints.second,vertical);
   if(!std::all_of(ends.begin(),ends.end(),[&](const auto& end){return end.second>=lo-eps&&end.second<=hi+eps;}))continue;
   if(!std::any_of(projected.begin(),projected.end(),[&](const auto& span){
    return std::min(hi+shiftNormal,span.second)>std::max(lo+shiftNormal,span.first)+eps;
   }))continue;
   plan.copies.push_back({role.handle,{},"axis-label",plan.shift});
   const bool linePlanned=std::any_of(plan.copies.begin(),plan.copies.end(),[&](const auto& copy){return copy.source==handle;});
   if(!linePlanned)plan.copies.push_back({handle,{},"axis-line",plan.shift});
   for(std::size_t i=0;i<ends.size();++i){
    const auto side=ends[i].first;
    endStations[side].push_back(axis->coordinate+shiftStation);
    if(side==2)southCircles=std::min(southCircles,grips[4*i+1].y+plan.shift.y-4*paper);
   }
   break;
  }
 }
 const auto spacing=bcpproduct::planAnnotationLayout(layout.scale,layout.unitMM,layout.paper);
 for(int side=0;side<4;++side){
  auto& stations=endStations[side];
  std::sort(stations.begin(),stations.end());
  stations.erase(std::unique(stations.begin(),stations.end(),[&](double a,double b){return std::abs(a-b)<=eps;}),stations.end());
  if(stations.size()<2)continue;
  const bool vertical=side<2;
  const double shiftNormal=vertical?plan.shift.x:plan.shift.y;
  const double shiftStation=vertical?plan.shift.y:plan.shift.x;
  const double datum=productPlanDatum(wallDatum,side)+shiftNormal;
  const int sign=side%2?1:-1;
  if(sign*(productPlanDatum(outlineBounds,side)-datum)>=spacing.start-eps)continue;
  std::vector<ProductRoofCopy> pair;
  for(int row:{1,2}){
   const auto item=std::find_if(layout.dimensions.begin(),layout.dimensions.end(),[&](const auto& dimension){
    return dimension.panel==sourcePanel&&dimension.side==side&&dimension.row==row;
   });
   if(item==layout.dimensions.end())break;
   const auto id=find(db,item->handle);
   const auto grips=chainGrips(id);
   const auto values=chainValues(id);
   auto expected=row==1?std::vector<double>{stations.front(),stations.back()}:stations;
   if(grips.size()!=2*values.size()+6||values.size()+1!=expected.size()||
      std::abs((vertical?grips.front().x:grips.front().y)+shiftNormal-datum-
       sign*(row==1?spacing.total:spacing.second))>eps)break;
   bool same=true;
   for(std::size_t i=0;i<expected.size();++i)
    same&=std::abs((vertical?grips[i+2].y:grips[i+2].x)+shiftStation-expected[i])<=eps;
   if(!same||std::abs(std::accumulate(values.begin(),values.end(),0.)-(expected.back()-expected.front()))>eps)break;
   pair.push_back({item->handle,{},"axis-dimension",plan.shift,side,row,std::move(expected)});
  }
  if(pair.size()!=2)continue;
  plan.axisDimensions[side]=true;
  plan.copies.insert(plan.copies.end(),pair.begin(),pair.end());
 }
 if(std::isfinite(southCircles)){
  auto title=std::find_if(plan.items.begin(),plan.items.end(),[](const auto& item){return item.kind=="TITLE";});
  auto note=std::find_if(plan.items.begin(),plan.items.end(),[](const auto& item){return item.kind=="NOTE";});
  const double move=std::min(0.,southCircles-10*paper-title->points.front().y);
  title->points.front().y+=move;
  note->points.front().y+=move;
 }
 productDrawRoof(db,plan.items);
 for(auto& copy:plan.copies)copy.handle=productRoofClone(db,copy.source,copy.shift);
 plan.reason=u8"默认四坡为顶层实际覆盖范围的候选。正式檐口出挑、坡度、檐口及屋脊标高、高低屋面交接和排水位置待确认。";
 job.rulePanels.push_back({outlineBounds.xmin,outlineBounds.ymin,outlineBounds.xmax,outlineBounds.ymax});
 return plan;
}

bool productVerifyExistingRoofAxis(AcDbDatabase& db,const ProductPlanRoof::ExistingAxisMark& mark,
 const ProductPlanRoof::ExistingAxisSide& side,double station,double eps){
 const bool vertical=side.side>=2;
 const int sign=side.side%2?1:-1;
 const auto line=open<AcDbLine>(find(db,mark.line));
 const auto a=line->startPoint(),b=line->endPoint();
 if(!productPlanDimensionVisible(*line)||line->ownerId()!=modelId(db)||
    std::abs(productAxisStation(a,vertical)-station)>eps||std::abs(productAxisStation(b,vertical)-station)>eps)return false;
 const auto outer=sign*(productAxisNormal(a,vertical)-side.datum)>sign*(productAxisNormal(b,vertical)-side.datum)?a:b;
 const auto inner=outer==a?b:a;
 if(sign*(productAxisNormal(inner,vertical)-side.datum)>=-eps)return false;
 if(mark.label=="-")return sign*(productAxisNormal(outer,vertical)-side.datum)>=-eps;
 const auto id=find(db,mark.label);
 const auto entity=open<AcDbEntity>(id);
 if(std::wstring(entity->isA()->name())!=L"TDbAxisLabelSet"||!productPlanDimensionVisible(*entity)||
    entity->ownerId()!=modelId(db))return false;
 const auto grips=chainGrips(id);
 if(grips.size()!=4&&grips.size()!=8)return false;
 AxisInk ink;
 axisInkLeaves(*entity,ink);
 std::vector<std::wstring> labels;
 productOpeningTexts(*entity,labels);
 if(labels.empty())return false;
 for(std::size_t i=0;i<grips.size();i+=4){
  const auto& lead=grips[i];
  const auto& centre=grips[i+1];
  bool connected=outer.distanceTo(lead)<=eps&&std::abs(productAxisStation(centre,vertical)-station)<=eps;
  if(!mark.fold.empty()){
   ProductPlanLayoutConfig proof;
   proof.axisFolds[{mark.label,mark.side}]=mark.fold;
   const auto vertices=productFoldPoints(db,proof,mark.label,mark.side);
   const auto normal=[&](const auto& point){return productAxisNormal(point,vertical);};
   connected=vertices.size()==3&&vertices.front().distanceTo(outer)<=eps&&vertices.back().distanceTo(lead)<=eps&&
    std::abs(productAxisStation(vertices[1],vertical)-station)<=eps&&
    sign*(normal(vertices[1])-normal(vertices[0]))>eps&&sign*(normal(vertices[2])-normal(vertices[1]))>eps&&
    std::abs(productAxisStation(lead,vertical)-productAxisStation(centre,vertical))<=eps;
  }
  if(sign*(productAxisNormal(centre,vertical)-productAxisNormal(lead,vertical))<=eps||
     sign*(productAxisNormal(lead,vertical)-side.datum)<-eps||!connected)continue;
  const auto circle=std::find_if(ink.circles.begin(),ink.circles.end(),[&](const auto& item){return item.centre.distanceTo(centre)<=eps;});
  if(circle==ink.circles.end())continue;
  const auto edge=productAxisAtNormal(centre,vertical,productAxisNormal(centre,vertical)-sign*circle->radius);
  if(std::any_of(ink.lines.begin(),ink.lines.end(),[&](const auto& stroke){
   return (stroke.first.distanceTo(lead)<=eps&&stroke.second.distanceTo(edge)<=eps)||
          (stroke.second.distanceTo(lead)<=eps&&stroke.first.distanceTo(edge)<=eps);
  }))return true;
 }
 return false;
}

void productVerifyRoof(ProductJob& job,AcDbDatabase& db,const ProductPlanRoof& plan,const ProductPlanLayoutConfig& layout){
 if(!job.unifiedPlan)return;
 Working work(&db);
 const auto unitMM=layout.unitMM;
 const double eps=.01/unitMM;
 const auto report=job.root/L"roof-plan-readback.json";
 std::ostringstream out;
 out<<std::setprecision(17)<<"{\"savedReopened\":true,\"sourceSHA256\":\""<<job.sourceHash
    <<"\",\"panel\":"<<plan.panel<<",\"sourcePanel\":"<<plan.sourcePanel<<",\"sourceOutline\":\""<<plan.sourceOutline
    <<"\",\"existingTitle\":\""<<plan.existingTitle<<"\",\"automaticAxisProjection\":"<<(plan.automaticAxisProjection?"true":"false")
    <<",\"automaticAxisRegistrationStatus\":"<<jsonUtf8(bcpproduct::decodeMessage(plan.automaticAxisRegistrationStatus))
    <<",\"shiftXY\":["<<plan.shift.x<<','<<plan.shift.y
    <<"],\"items\":[";
 bool first=true;
 for(const auto& item:plan.items){
  const auto entity=open<AcDbEntity>(find(db,item.handle));
  require(entity->ownerId()==modelId(db)&&entity->visibility()==AcDb::kVisible&&
          symbolName(entity->layerId())==productRoofLayer(item.kind),"saved native roof object ownership and layer");
  const auto at=[&](unsigned i,const AcGePoint3d& actual){return item.points[i].distanceTo(actual)<=eps;};
  if(item.kind=="TITLE"||item.kind=="NOTE"){
   const auto text=AcDbMText::cast(entity.get());
   AcString value;
   require(text&&text->contents(value)==Acad::eOk&&value==productRoofText(item.kind)&&
           at(0,text->location())&&std::abs(text->textHeight()-item.height)<=eps,"saved roof pending caption");
  }else if(item.kind=="OUTLINE"||item.kind=="ARROW"){
   const auto poly=AcDbPolyline::cast(entity.get());
   require(poly&&poly->numVerts()==item.points.size()&&poly->isClosed()==(item.kind=="OUTLINE"),"saved roof polyline");
   for(unsigned i=0;i<poly->numVerts();++i){
    AcGePoint3d point;
    require(poly->getPointAt(i,point)==Acad::eOk&&at(i,point),"saved roof polyline station");
   }
  }else{
   const auto line=AcDbLine::cast(entity.get());
   require(line&&at(0,line->startPoint())&&at(1,line->endPoint()),"saved roof line endpoints");
  }
  if(!first)out<<',';
  first=false;
  out<<"{\"kind\":\""<<item.kind<<"\",\"object\":\""<<item.handle<<"\",\"points\":"<<wholePoints(item.points)<<'}';
 }
 out<<"],\"copies\":[";
 first=true;
 for(const auto& copy:plan.copies){
  const auto sourceId=find(db,copy.source),id=find(db,copy.handle);
  const auto original=open<AcDbEntity>(sourceId);
  const auto saved=open<AcDbEntity>(id);
  require(saved->ownerId()==modelId(db)&&saved->visibility()==AcDb::kVisible&&!saved->isAProxy()&&
          original->isA()==saved->isA()&&original->layerId()==saved->layerId(),"saved roof native annotation");
  const auto before=chainGrips(sourceId),after=chainGrips(id);
  const bool axisLine=copy.kind=="axis-line";
  if(axisLine){
   const auto old=AcDbLine::cast(original.get()),line=AcDbLine::cast(saved.get());
   require(old&&line&&old->linetypeId()==line->linetypeId()&&
      line->startPoint().distanceTo(old->startPoint()+copy.shift)<=eps&&
      line->endPoint().distanceTo(old->endPoint()+copy.shift)<=eps,"saved roof independent DOTE axis");
  }else{
   require(before.size()==after.size()&&!before.empty(),"saved roof annotation grip coverage");
   const auto values=copy.row?chainValues(id):std::vector<double>{};
   for(std::size_t i=0;i<before.size();++i){
    // Dimension text can legitimately receive an independent clearance move.
    if(copy.row&&i>=values.size()+3)break;
    require(after[i].distanceTo(before[i]+copy.shift)<=eps,"saved roof annotation grip position");
   }
   if(copy.row){
    require(values.size()+1==copy.stations.size(),"saved roof dimension segment count");
    for(std::size_t i=0;i<copy.stations.size();++i){
     require(std::abs((copy.side<2?after[i+2].y:after[i+2].x)-copy.stations[i])<=eps,"saved roof dimension station");
     if(i>0)require(std::abs(values[i-1]-(copy.stations[i]-copy.stations[i-1]))<=eps,"saved roof dimension value");
    }
   }
  }
  if(!first)out<<',';
  first=false;
  out<<"{\"kind\":\""<<copy.kind<<"\",\"source\":\""<<copy.source<<"\",\"object\":\""<<copy.handle<<"\",\"side\":"
     <<copy.side<<",\"row\":"<<copy.row<<",\"stations\":[";
  for(std::size_t i=0;i<copy.stations.size();++i){if(i)out<<',';out<<copy.stations[i];}
  out<<"]}";
 }
 out<<"],\"retainedOriginal\":[";
 first=true;
 bool retainedVerified=true,hasRoofBody=false,hasRoofBoundary=false,hasRoofLevels=false,hasRoofSlopes=false;
 for(const auto& item:plan.retained){
  const auto id=find(db,item.handle);
  const auto entity=open<AcDbEntity>(id);
  const auto points=chainGrips(id);
  const bool same=item.type==entity->isA()->name()&&productPlanDimensionVisible(*entity)&&points.size()==item.points.size()&&
   std::equal(points.begin(),points.end(),item.points.begin(),[&](const auto& a,const auto& b){return a.distanceTo(b)<=eps;});
  retainedVerified&=same;
  hasRoofBody|=item.role=="BODY";
  hasRoofBoundary|=item.role=="BOUNDARY";
  hasRoofLevels|=item.role=="LEVEL";
  hasRoofSlopes|=item.role=="SLOPE";
  if(!first)out<<',';
  first=false;
  out<<"{\"object\":\""<<item.handle<<"\",\"role\":\""<<item.role<<"\",\"sameGeometry\":"
     <<(same?"true":"false")<<",\"points\":"<<wholePoints(points)<<'}';
 }
 out<<"],\"registeredArchiveAnnotations\":[";
 first=true;
 bool annotationsVerified=true;
 for(const auto& item:plan.imported){
  const auto id=find(db,item.handle);
  const auto entity=open<AcDbEntity>(id);
  const auto points=chainGrips(id);
  const bool same=item.type==entity->isA()->name()&&productPlanDimensionVisible(*entity)&&points.size()==item.points.size()&&
   std::equal(points.begin(),points.end(),item.points.begin(),[&](const auto& a,const auto& b){return a.distanceTo(b)<=eps;});
  annotationsVerified&=same;
  hasRoofLevels|=item.type==L"TDbSymbElevation";
  hasRoofSlopes|=item.type==L"TDbSymbArrow";
  if(!first)out<<',';
  first=false;
  out<<"{\"object\":\""<<item.handle<<"\",\"archiveSource\":\""<<item.role<<"\",\"sameGeometry\":"
     <<(same?"true":"false")<<'}';
 }
 out<<"],\"drainageOutlets\":[";
 first=true;
 for(const auto& outlet:plan.drains){
  const auto marker=open<AcDbCircle>(find(db,outlet.output));
  require(productPlanDimensionVisible(*marker)&&marker->center().distanceTo(outlet.position)<=eps,
   "reopened rainwater outlet station");
  if(!first)out<<',';
  first=false;
  out<<"{\"source\":\""<<outlet.source<<"\",\"object\":\""<<outlet.output
     <<"\",\"position\":"<<wholePoints({marker->center()});
  if(!outlet.label.empty()){
   const auto note=open<AcDbMText>(find(db,outlet.label));
   AcString text;
   require(productPlanDimensionVisible(*note)&&note->contents(text)==Acad::eOk&&text==outlet.designation.c_str(),
    "reopened roof rainwater riser designation");
   out<<",\"plumbingSource\":\""<<outlet.plumbingSource<<"\",\"labelObject\":\""<<outlet.label
      <<"\",\"designation\":"<<jsonUtf8(outlet.designation);
  }
  if(!outlet.fittingSource.empty())out<<",\"fittingSource\":\""<<outlet.fittingSource
    <<"\",\"connectionSource\":\""<<outlet.connectionSource<<"\",\"detail\":"<<jsonUtf8(outlet.detail);
  if(outlet.inlet){
   const auto line=open<AcDbLine>(find(db,outlet.connection));
   const auto inlet=open<AcDbCircle>(find(db,outlet.outlet));
   require(productPlanDimensionVisible(*line)&&productPlanDimensionVisible(*inlet)&&
    line->startPoint().distanceTo(outlet.position)<=eps&&line->endPoint().distanceTo(*outlet.inlet)<=eps&&
    inlet->center().distanceTo(*outlet.inlet)<=eps,"reopened offset roof inlet and connection");
   out<<",\"inletObject\":\""<<outlet.outlet<<"\",\"linkObject\":\""<<outlet.connection
      <<"\",\"inletPosition\":"<<wholePoints({inlet->center()});
  }
  out<<'}';
 }
 out<<"],\"drainSchedule\":";
 if(plan.drainSchedule.empty())out<<"null";
 else{
  const auto note=open<AcDbMText>(find(db,plan.drainSchedule));
  AcString text;
  require(productPlanDimensionVisible(*note)&&note->contents(text)==Acad::eOk&&text==plan.drainScheduleText.c_str(),
   "reopened roof rainwater schedule");
  out<<"{\"object\":\""<<plan.drainSchedule<<"\",\"contents\":"<<jsonUtf8(plan.drainScheduleText)<<'}';
 }
 out<<",\"drainageArrows\":[";
 first=true;
 for(const auto& item:plan.imported)if(plan.drainageArrows.count(item.role)){
  const auto id=find(db,item.handle);
  const auto actual=chainGrips(id);
  require(productPlanDimensionVisible(*open<AcDbEntity>(id))&&actual.size()==item.points.size()&&
   std::equal(actual.begin(),actual.end(),item.points.begin(),[&](const auto& a,const auto& b){return a.distanceTo(b)<=eps;}),
   "reopened gutter slope arrow position and direction");
  if(!first)out<<',';
  first=false;
  out<<"{\"source\":\""<<item.role<<"\",\"object\":\""<<item.handle<<"\"}";
 }
 out<<"],\"existingAxisDimensions\":[";
 first=true;
 std::array<bool,4> axisSidesVerified{};
 std::set<std::string> verifiedAxisLines,visibleAxisLines;
 std::vector<std::string> axisObjects;
 for(const auto& side:plan.existingAxisSides){
  if(!first)out<<',';
  first=false;
  const bool written=plan.axisDimensions[side.side];
  bool verified=written;
  const auto axis=[&](const AcGePoint3d& p){return side.side<2?p.y:p.x;};
  const auto normal=[&](const AcGePoint3d& p){return side.side<2?p.x:p.y;};
  const int sign=side.side%2?1:-1;
  const auto spacing=bcpproduct::planAnnotationLayout(layout.scale,unitMM,layout.paper);
  if(written)for(std::size_t row=0;row<2;++row){
   const auto id=find(db,side.dimensions[row]);
   const auto entity=open<AcDbEntity>(id);
   const auto grips=chainGrips(id);
   const auto values=chainValues(id);
   const auto stations=row?side.stations:std::vector<double>{side.stations.front(),side.stations.back()};
   bool good=productPlanDimensionVisible(*entity)&&std::wstring(entity->isA()->name())==L"TDbDimension2"&&
    grips.size()==2*values.size()+6&&values.size()+1==stations.size()&&
    std::abs(std::accumulate(values.begin(),values.end(),0.)-(stations.back()-stations.front()))<=eps;
   for(std::size_t i=0;good&&i<stations.size();++i){
    good&=std::abs(axis(grips[i+2])-stations[i])<=eps;
    if(i)good&=std::abs(values[i-1]-(stations[i]-stations[i-1]))<=eps;
   }
   TemplateParts parts(entity.get());
   good&=entity->explode(parts.values)==Acad::eOk&&static_cast<std::size_t>(parts.values.length())==values.size();
   for(int i=0;good&&i<parts.values.length();++i){
    const auto* segment=AcDbAlignedDimension::cast(static_cast<AcRxObject*>(parts.values[i]));
    good=segment&&std::abs(sign*(normal(segment->xLine1Point())-side.datum)+segment->dimexo()*segment->dimscale()-spacing.start)<=eps&&
     std::abs(sign*(normal(segment->xLine2Point())-side.datum)+segment->dimexo()*segment->dimscale()-spacing.start)<=eps;
   }
   const auto& before=side.written[row];
   if(good)for(std::size_t i=0;i<values.size()+3;++i)good&=grips[i].distanceTo(before[i])<=eps;
   verified&=good;
  }
  std::vector<std::pair<double,const ProductPlanRoof::ExistingAxisMark*>> marks;
  std::vector<double> circleStations;
  bool circlePlacementVerified=true;
  for(const auto station:side.stations){
   const ProductPlanRoof::ExistingAxisMark* matched=nullptr;
   bool ambiguous=false;
   for(const auto& mark:plan.existingAxisMarks)if(mark.side==side.side){
    try{
     if(!productVerifyExistingRoofAxis(db,mark,side,station,eps))continue;
     if(matched){ambiguous=true;break;}
     matched=&mark;
    }catch(const std::exception&){}
   }
   if(matched&&!ambiguous){
    marks.emplace_back(station,matched);
    verifiedAxisLines.insert(matched->line);
    if(matched->label!="-"){
     axisObjects.push_back(matched->label);
     if(!matched->fold.empty())axisObjects.push_back(matched->fold);
     visibleAxisLines.insert(matched->line);
     if(plan.automaticAxisProjection){
      const auto grips=chainGrips(find(db,matched->label));
      const auto centre=productAxisStation(grips[1],side.side>=2);
      const auto maxShift=layout.scale/layout.unitMM*bcpproduct::PlanPaperPolicy::circleMaxShift;
      const auto normalDistance=sign*(productAxisNormal(grips[1],side.side>=2)-side.datum);
      circlePlacementVerified&=std::abs(centre-station)<=maxShift+eps&&
       std::abs(normalDistance-spacing.circleCentre)<=eps&&
       std::abs(grips[1].distanceTo(grips[2])-spacing.circleDiameter/2)<=eps;
      circleStations.push_back(centre);
     }
    }
   }
  }
  if(plan.automaticAxisProjection){
   std::sort(circleStations.begin(),circleStations.end());
   const auto minimumGap=spacing.circleDiameter+layout.scale/layout.unitMM*bcpproduct::PlanPaperPolicy::circleClearance;
   circlePlacementVerified&=std::adjacent_find(circleStations.begin(),circleStations.end(),[&](double a,double b){
    return b-a<minimumGap-eps;
   })==circleStations.end();
  }
  axisSidesVerified[side.side]=verified&&marks.size()==side.stations.size()&&circlePlacementVerified;
  if(plan.automaticAxisProjection){
   for(const auto& source:side.sourceChains){
    AcDbObjectId id;
    const bool removed=db.getAcDbObjectId(id,false,AcDbHandle(bcpproduct::decodeMessage(source).c_str()))!=Acad::eOk||id.isErased();
    axisSidesVerified[side.side]&=removed;
   }
  }
  std::ostringstream row;
  row<<std::setprecision(17)<<"{\"panel\":"<<plan.panel<<",\"side\":"<<side.side
     <<",\"eave\":\""<<side.eave<<"\",\"source\":\""<<side.source<<"\",\"sourceChains\":[\""
     <<side.sourceChains[0]<<"\",\""<<side.sourceChains[1]<<"\"],\"stations\":[";
  for(std::size_t i=0;i<side.stations.size();++i){if(i)row<<',';row<<side.stations[i];}
  row<<"],\"dimensions\":[\""<<side.dimensions[0]<<"\",\""<<side.dimensions[1]
     <<"\"],\"axisMarks\":[";
  for(std::size_t i=0;i<marks.size();++i){
   if(i)row<<',';
   row<<"{\"station\":"<<marks[i].first<<",\"line\":\""<<marks[i].second->line
      <<"\",\"label\":\""<<marks[i].second->label<<"\",\"fold\":\""<<marks[i].second->fold<<"\"}";
  }
  row<<"],\"circleStations\":[";
  for(std::size_t i=0;i<circleStations.size();++i){if(i)row<<',';row<<circleStations[i];}
  row<<"],\"circlePlacementVerified\":"<<(circlePlacementVerified?"true":"false")
     <<",\"axisMarksVerified\":"<<(axisSidesVerified[side.side]?"true":"false")
     <<",\"savedReopened\":true,\"result\":\""<<(verified?"verified-two-native-axis-distance-chains":"pending-roof-axis-dimension-readback")<<"\"}";
  out<<row.str();
  productRecordRuleEvidence(job,"D27",row.str());
  if(plan.automaticAxisProjection){
   productRecordRuleEvidence(job,"A14",row.str());
   productRecordRuleEvidence(job,"A28",row.str());
  }
  if(!verified)job.detailPending.push_back({u8"屋面轴距",side.source,u8"重开后两道原生链、源站及闭合未全部核实"});
 }
 const bool allAxesVerified=std::all_of(axisSidesVerified.begin(),axisSidesVerified.end(),[](bool verified){return verified;})&&
  verifiedAxisLines==visibleAxisLines;
 axisObjects.insert(axisObjects.end(),verifiedAxisLines.begin(),verifiedAxisLines.end());
 const bool existingRoofVerified=!plan.existingTitle.empty()&&plan.items.empty()&&retainedVerified&&annotationsVerified&&allAxesVerified&&
  (job.architecturalPlanOnly?(hasRoofBody||hasRoofBoundary):(hasRoofBody&&hasRoofBoundary&&hasRoofLevels&&hasRoofSlopes));
 if(plan.automaticAxisProjection&&existingRoofVerified)for(const auto& side:plan.existingAxisSides){
  for(std::size_t row=0;row<side.sourceChains.size();++row){
   const auto& source=side.sourceChains[row];
   const auto item=std::find_if(job.sourceInventory.items.begin(),job.sourceInventory.items.end(),[&](const auto& obligation){
    return std::get<1>(obligation.first)=="source-dimension"&&std::get<2>(obligation.first)==source;
   });
   if(item!=job.sourceInventory.items.end())
    productCheckPlanSource(job,std::get<0>(item->first),"source-dimension",source,bcpproduct::PlanCheck::verified,
     {side.dimensions[row]},u8"原屋面链已退出，已按配准后的实际墙柱轴位独立重开核验替换链及闭合。");
  }
 }
 out<<"],\"existingRoofVerified\":"<<(existingRoofVerified?"true":"false")
    <<",\"existingTitlePosition\":"<<wholePoints(plan.existingTitlePosition)
    <<",\"reason\":"<<jsonUtf8(bcpproduct::decodeMessage(plan.reason))<<'}';
 if(!plan.existingTitle.empty())require(!find(db,plan.existingTitle).isNull(),"original roof title retained");
 if(!plan.existingTitlePosition.empty()){
  const auto actual=chainGrips(find(db,plan.existingTitle));
  require(actual.size()==plan.existingTitlePosition.size()&&std::equal(actual.begin(),actual.end(),plan.existingTitlePosition.begin(),
   [&](const auto& a,const auto& b){return a.distanceTo(b)<=eps;}),"roof title position retained on reopening");
 }
 writeNew(report,out.str());
 std::vector<std::string> objects;
 if(!plan.existingTitle.empty())objects.push_back(plan.existingTitle);
 for(const auto& item:plan.retained)objects.push_back(item.handle);
 for(const auto& item:plan.imported)objects.push_back(item.handle);
 for(const auto& outlet:plan.drains){
  objects.push_back(outlet.output);
  if(!outlet.label.empty())objects.push_back(outlet.label);
  if(!outlet.connection.empty())objects.push_back(outlet.connection);
  if(!outlet.outlet.empty())objects.push_back(outlet.outlet);
 }
 if(!plan.drainSchedule.empty())objects.push_back(plan.drainSchedule);
 for(const auto& item:plan.items)objects.push_back(item.handle);
 for(const auto& copy:plan.copies)objects.push_back(copy.handle);
 for(const auto& side:plan.existingAxisSides)
  for(const auto& handle:side.dimensions)if(!handle.empty())objects.push_back(handle);
 objects.insert(objects.end(),axisObjects.begin(),axisObjects.end());
 const std::string reason=existingRoofVerified?(plan.automaticAxisProjection?u8"源屋面构件、各侧有效轴号、轴线和两道轴距已重开核验":
  u8"屋面构件、标高、坡向、各侧有效轴号、轴线及两道轴距已重开核验"):
  !plan.existingAxisSides.empty()&&!allAxesVerified?u8"屋面仍有定位测站缺可核轴线或轴号，逐侧见屋面重开报告":
  plan.reason.empty()?u8"独立屋顶图的设计及各侧实际檐边需确认":plan.reason;
 if(!plan.existingTitle.empty())job.planReleaseLocations[{0,"roof",""}]={static_cast<std::size_t>(plan.panel),plan.existingTitle};
 productCheckPlanSource(job,0,"roof","",existingRoofVerified?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::pending,
    std::move(objects),reason);
 if(plan.existingTitle.empty()&&!plan.items.empty()&&plan.sourceOutline.empty())
  job.sourceInventory.needsProjectInput(0,"roof","",u8"默认四坡仅由顶层轮廓生成；补屋面坡度、檐脊标高、出挑、交接与排水依据");
 std::ostringstream evidence;
 evidence<<"{\"panel\":"<<plan.panel<<",\"sourcePanel\":"<<plan.sourcePanel<<",\"object\":\""
         <<(plan.items.empty()?plan.existingTitle:plan.items.front().handle)<<"\",\"savedReopened\":true,\"defaultGeometry\":"
         <<(!plan.items.empty()?"true":"false")<<",\"report\":\"roof-plan-readback.json\",\"reason\":"
         <<jsonUtf8(bcpproduct::decodeMessage(reason))<<",\"result\":\""
         <<(existingRoofVerified?"verified-existing-roof-main-plan":"pending-roof-design")<<"\"}";
 for(const auto* id:{"A29","QA10"})productRecordRuleEvidence(job,id,evidence.str());
 if(!existingRoofVerified)
  job.detailPending.push_back({u8"A29 / 屋顶平面",plan.panel?std::to_string(plan.panel):plan.existingTitle,reason});
 if(plan.items.empty())return;
 for(int side=0;side<4;++side){
  std::ostringstream row;
  row<<"{\"panel\":"<<plan.panel<<",\"side\":"<<side
     <<",\"axisDistanceChainsVerified\":"<<(plan.axisDimensions[side]?"true":"false")
     <<",\"row3\":\"omitted-pure-roof-candidate-no-walls-columns-or-openings\",\"savedReopened\":true,"
     <<"\"report\":\"roof-plan-readback.json\",\"result\":\""
     <<(plan.axisDimensions[side]?"verified-candidate-roof-axis-dimensions":"pending-roof-side-service")<<"\"}";
  productRecordRuleEvidence(job,"D27",row.str());
 }
}
