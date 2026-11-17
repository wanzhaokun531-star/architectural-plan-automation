struct ProductAxisProjectRow {
 std::string kind;
 int view{},otherView{},side{-1};
 bool vertical{};
 std::vector<std::string> handles;
 std::vector<double> values;
 std::string reason;
};

struct ProductAxisProjectResult {
 ProductAxisProjectRow source;
 std::vector<std::string> rules,objects;
 std::vector<AcGePoint3d> points;
 std::optional<double> targetStation,otherTargetStation;
};

struct ProductProjectAxes {
 std::vector<ProductAxisProjectResult> results;
 std::map<std::tuple<std::size_t,std::string,bool>,double> columns,walls;
};

struct ProductAxisOccupant {
 std::string handle;
 std::set<std::string> numbers;
 bool visible{};
};

std::vector<ProductAxisOccupant> productAxisOccupancy(AcDbDatabase& db){
 std::vector<ProductAxisOccupant> records;
 const auto collect=[&](const auto& self,const AcDbEntity& entity,std::set<std::string>& numbers,unsigned depth)->void{
  if(depth>5)return;
  if(const auto text=AcDbText::cast(&entity)){
   const auto* content=text->textString();
   if(!content||!*content)return;
   const std::wstring value(content);
   if(value.find_first_not_of(L"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz/")!=std::wstring::npos)return;
   std::string label(value.begin(),value.end());
   std::transform(label.begin(),label.end(),label.begin(),[](char c){return c>='a'&&c<='z'?static_cast<char>(c-'a'+'A'):c;});
   numbers.insert(std::move(label));return;
  }
  const auto type=std::wstring(entity.isA()->name());
  if(type!=L"TDbAxisLabelSet"&&type!=L"TDbSymbDrawingIndex")return;
  TemplateParts parts(&entity);
  if(entity.explode(parts.values)!=Acad::eOk)return;
  for(int i=0;i<parts.values.length();++i)
   if(const auto child=AcDbEntity::cast(static_cast<AcRxObject*>(parts.values[i])))self(self,*child,numbers,depth+1);
 };
 for(const auto& [raw,type]:modelTypes(db)){
  if(type!=L"TDbAxisLabelSet")continue;
  const auto handle=ascii(raw);
  const auto entity=open<AcDbEntity>(find(db,handle));
  const auto layer=open<AcDbLayerTableRecord>(entity->layerId());
  ProductAxisOccupant row{handle,{},entity->visibility()==AcDb::kVisible&&!layer->isOff()&&!layer->isFrozen()};
  collect(collect,*entity,row.numbers,0);
  records.push_back(std::move(row));
 }
 return records;
}

std::vector<ProductAxisProjectRow> productReadAxisProject(ProductJob& job){
 const auto path=job.profile/L"plan-policy/axis-project.tsv";
 if(!fs::is_regular_file(path))return {};
 std::ifstream input(path);
 input.imbue(std::locale::classic());
 std::string format,digest,line;
 require(bool(input>>format>>digest)&&format=="PLANAXISPROJECT1","axis project confirmation header");
 if(digest!=job.sourceHash)return {};
 std::getline(input,line);
 std::vector<ProductAxisProjectRow> rows;
 bool end=false;
 const auto side=[](const std::string& s){
  const auto at=std::string("WESN").find(s);
  return s.size()==1&&at!=std::string::npos?static_cast<int>(at):-1;
 };
 while(std::getline(input,line)){
  std::istringstream stream(line);
  stream.imbue(std::locale::classic());
  ProductAxisProjectRow row;
  if(!(stream>>row.kind))continue;
  if(row.kind=="END"){end=true;break;}
  require(bool(stream>>row.view)&&row.view>0,"axis project source panel");
  --row.view;
  std::string direction;
  if(row.kind=="COMMON_COLUMN"){
   require(bool(stream>>row.otherView)&&row.otherView>0&&row.view!=row.otherView-1,"two distinct column source panels");
   --row.otherView;
  }
  if(row.kind=="RETREAT"||row.kind=="BATH_PARTITION"){
   require(bool(stream>>direction)&&(row.side=side(direction))>=0,"axis project side");
   row.vertical=row.side>=2;
  }else if(row.kind!="LOCAL_DIM"&&row.kind!="COURTYARD"){
   require(bool(stream>>direction)&&(direction=="X"||direction=="Y"),"axis project direction");
   row.vertical=direction=="X";
  }
  const std::size_t count=row.kind=="SHARED_WALL"||row.kind=="SHARED_COLUMNS"||row.kind=="BATH_PARTITION"?2:
      row.kind=="COMMON_COLUMN"?4:row.kind=="RETREAT"?2:row.kind=="LOCAL_DIM"||row.kind=="COURTYARD"?1:0;
  require(count!=0,"unknown axis project record");
  for(std::size_t i=0;i<count;++i){
   std::string handle;
   require(bool(stream>>handle)&&(handle=="-"||handle.find_first_not_of("0123456789ABCDEFabcdef")==std::string::npos),
           "axis project source handle");
   std::transform(handle.begin(),handle.end(),handle.begin(),[](char c){return c>='a'&&c<='f'?static_cast<char>(c-'a'+'A'):c;});
   row.handles.push_back(handle);
  }
  const auto measurements=row.kind=="SHARED_COLUMNS"?1:row.kind=="LOCAL_DIM"?3:row.kind=="COURTYARD"?7:0;
  for(int i=0;i<measurements;++i){
   double value{};
   require(bool(stream>>value)&&std::isfinite(value),"axis project finite geometric input");
   row.values.push_back(value);
  }
  require(!(stream>>direction),"axis project trailing fields");
  rows.push_back(std::move(row));
 }
 require(end,"axis project confirmation terminator");
 input>>std::ws;
 require(input.eof(),"axis project trailing records");
 return rows;
}

struct ProductAxisWallGeometry {
 bool vertical{};
 double station{},lower{},upper{},half{};
};

std::optional<ProductAxisWallGeometry> productAxisWall(const GenericNativeInput& input,
 const bcpproduct::GenericView& view,const std::string& handle){
 const auto at=std::find_if(view.walls.begin(),view.walls.end(),[&](auto index){return input.walls[index].wall.handle==handle;});
 if(at==view.walls.end())return {};
 const auto& wall=input.walls[*at];
 if(wall.low||!wall.orthogonal)return {};
 const auto a=bcp::bodyCentreStart(wall.wall);
 const bcp::Point b{a.x+wall.wall.end.x-wall.wall.start.x,a.y+wall.wall.end.y-wall.wall.start.y};
 const bool vertical=std::abs(a.x-b.x)<1e-5;
 return ProductAxisWallGeometry{vertical,vertical?a.x:a.y,
   vertical?std::min(a.y,b.y):std::min(a.x,b.x),vertical?std::max(a.y,b.y):std::max(a.x,b.x),wall.total/2.};
}

const GenericNativeColumn* productAxisColumn(const std::vector<GenericNativeColumn>& columns,const std::string& handle){
 const auto found=std::find_if(columns.begin(),columns.end(),[&](const auto& column){return column.handle==handle;});
 return found==columns.end()?nullptr:&*found;
}

bool productAxisSectionContains(const GenericNativeColumn& column,bool vertical,double station,double eps){
 const double centre=vertical?column.centre.x:column.centre.y;
 const double half=vertical?column.halfX:column.halfY;
 return half>eps&&std::abs(station-centre)<=half+eps;
}

bool productAxisWallMeetsColumn(const ProductAxisWallGeometry& wall,const GenericNativeColumn& column,double eps){
 const double along=wall.vertical?column.centre.y:column.centre.x;
 const double half=wall.vertical?column.halfY:column.halfX;
 return productAxisSectionContains(column,wall.vertical,wall.station,eps)&&
        along+half>=wall.lower-eps&&along-half<=wall.upper+eps;
}

double productAxisProjectStation(const std::map<std::tuple<std::size_t,std::string,bool>,double>& data,
 std::size_t view,const std::string& source,bool vertical,double station){
 const auto found=data.find({view,source,vertical});
 return found==data.end()?station:found->second;
}

ProductProjectAxes productPrepareAxisProject(ProductJob& job,const GenericNativeInput& input,
 const std::vector<bcpproduct::GenericView>& views,const std::vector<std::vector<GenericNativeColumn>>& columns,
 const std::vector<ProductPlanFloorAlignment>& alignment,double unit){
 ProductProjectAxes output;
 if(!job.unifiedPlan)return output;
 const double eps=.01/unit;
 std::vector<ProductAxisProjectRow> rows;
 try{rows=productReadAxisProject(job);}
 catch(const std::exception& error){job.detailPending.push_back({u8"轴网项目确认","axis-project.tsv",error.what()});}
 for(auto& row:rows){
  std::vector<std::string> rules;
  if(row.kind=="SHARED_WALL")rules={"A15","A18"};
  if(row.kind=="SHARED_COLUMNS")rules={"A22"};
  if(row.kind=="COMMON_COLUMN")rules={"A30"};
  if(row.kind=="RETREAT")rules={"A21"};
  if(row.kind=="BATH_PARTITION")rules={"A19"};
  if(row.kind=="LOCAL_DIM"||row.kind=="COURTYARD")rules={"D07"};
  ProductAxisProjectResult result{std::move(row),std::move(rules)};
  auto& source=result.source;
  if(source.view>=views.size()){
   source.reason=u8"本源图不存在指定图幅。";
   output.results.push_back(std::move(result));
   continue;
  }
  const auto view=static_cast<std::size_t>(source.view);
  const auto noConflict=[&](const auto& map,std::size_t panel,const std::string& handle,double station){
   const auto old=map.find({panel,handle,source.vertical});
   return old==map.end()||std::abs(old->second-station)<=eps;
  };
  const auto addColumn=[&](const auto& column,double station){
   output.columns.emplace(std::make_tuple(view,column.handle,source.vertical),station);
  };
  if(source.kind=="SHARED_WALL"){
   const auto* column=productAxisColumn(columns[view],source.handles[0]);
   const auto wall=productAxisWall(input,views[view],source.handles[1]);
   bool outer=false;
   if(wall&&wall->vertical==source.vertical){
    bool lower=false,upper=false;
    for(auto index:views[view].walls){
     const auto other=productAxisWall(input,views[view],input.walls[index].wall.handle);
     if(!other||other->vertical!=wall->vertical)continue;
     lower|=other->station<wall->station-eps;
     upper|=other->station>wall->station+eps;
    }
    outer=lower!=upper;
   }
   if(column&&wall&&outer&&(std::abs(wall->half*2*unit-200)<=.01||std::abs(wall->half*2*unit-240)<=.01)&&
      productAxisSectionContains(*column,source.vertical,wall->station,eps)&&
      noConflict(output.columns,view,column->handle,wall->station)){
    addColumn(*column,wall->station);
    result.targetStation=wall->station;
    result.points={column->centre,productAxisPoint(column->centre,source.vertical,
        productAxisNormal(column->centre,source.vertical),wall->station)};
   }else source.reason=u8"未证实柱实际截面与指定外墙中心轴交叠，保留原轴。";
  }
  if(source.kind=="SHARED_COLUMNS"){
   const auto* first=productAxisColumn(columns[view],source.handles[0]);
   const auto* second=productAxisColumn(columns[view],source.handles[1]);
   const double station=source.values.front();
   if(first&&second&&first!=second&&productAxisSectionContains(*first,source.vertical,station,eps)&&
      productAxisSectionContains(*second,source.vertical,station,eps)&&
      noConflict(output.columns,view,first->handle,station)&&noConflict(output.columns,view,second->handle,station)){
    addColumn(*first,station);addColumn(*second,station);
    result.targetStation=station;
    result.points={first->centre,second->centre};
   }else source.reason=u8"指定轴位不在两个实际柱截面的共同投影。";
  }
  if(source.kind=="COMMON_COLUMN"){
   const auto lower=static_cast<std::size_t>(source.otherView);
   const auto* upperColumn=productAxisColumn(columns[view],source.handles[0]);
   const auto* lowerColumn=lower<views.size()?productAxisColumn(columns[lower],source.handles[1]):nullptr;
   const auto upperWall=productAxisWall(input,views[view],source.handles[2]);
   const auto lowerWall=lower<views.size()?productAxisWall(input,views[lower],source.handles[3]):std::nullopt;
   if(upperColumn&&lowerColumn&&alignment[view].registered&&alignment[lower].registered&&
      alignment[view].floor&&alignment[lower].floor&&*alignment[view].floor>*alignment[lower].floor){
    const auto& u=alignment[view].offset;const auto& l=alignment[lower].offset;
    const AcGeVector3d shift(u.x-l.x,u.y-l.y,0);
    const auto lowerAtUpper=lowerColumn->centre+shift;
    const double shiftStation=source.vertical?shift.x:shift.y;
    const bool sameGroup=std::abs(upperColumn->centre.x-lowerAtUpper.x)<upperColumn->halfX+lowerColumn->halfX-eps&&
                         std::abs(upperColumn->centre.y-lowerAtUpper.y)<upperColumn->halfY+lowerColumn->halfY-eps;
    const bool upperValid=upperWall&&upperWall->vertical==source.vertical&&productAxisWallMeetsColumn(*upperWall,*upperColumn,eps);
    const bool lowerValid=lowerWall&&lowerWall->vertical==source.vertical&&productAxisWallMeetsColumn(*lowerWall,*lowerColumn,eps);
    if(sameGroup&&(upperValid||lowerValid)){
     const double station=lowerValid?lowerWall->station+shiftStation:upperWall->station;
     if(noConflict(output.columns,view,upperColumn->handle,station)&&
        noConflict(output.columns,lower,lowerColumn->handle,station-shiftStation)&&
        (!upperValid||noConflict(output.walls,view,source.handles[2],station))&&
        (!lowerValid||noConflict(output.walls,lower,source.handles[3],station-shiftStation))){
      addColumn(*upperColumn,station);
      output.columns[{lower,lowerColumn->handle,source.vertical}]=station-shiftStation;
      if(upperValid)output.walls[{view,source.handles[2],source.vertical}]=station;
      if(lowerValid)output.walls[{lower,source.handles[3],source.vertical}]=station-shiftStation;
      result.targetStation=station;result.otherTargetStation=station-shiftStation;
      result.points={upperColumn->centre,lowerColumn->centre};
     }
    }
   }
   if(result.points.empty())source.reason=u8"柱截面跨层投影、配准或相接定位墙依据不足。";
  }
  output.results.push_back(std::move(result));
 }
 for(std::size_t view=0;view<views.size();++view){
  const auto& upper=alignment[view];
  if(!upper.tie||!upper.lowerView)continue;
  const auto lower=*upper.lowerView;
  const auto& shift=upper.tie->shift;
  for(const auto& column:columns[view]){
   std::vector<const GenericNativeColumn*> matches;
   for(const auto& earlier:columns[lower]){
    if(std::hypot(column.centre.x-earlier.centre.x-shift.x,column.centre.y-earlier.centre.y-shift.y)<=eps&&
       std::abs(column.halfX-earlier.halfX)<=eps&&std::abs(column.halfY-earlier.halfY)<=eps&&
       column.halfX>eps&&column.halfY>eps)matches.push_back(&earlier);
   }
   if(matches.size()!=1)continue;
   const auto& earlier=*matches.front();
   for(bool vertical:{true,false}){
    if(output.columns.count({view,column.handle,vertical})||output.columns.count({lower,earlier.handle,vertical}))continue;
    ProductAxisProjectResult result;
    result.source.kind="EXACT_COLUMN_PAIR";
    result.source.view=static_cast<int>(view);
    result.source.otherView=static_cast<int>(lower);
    result.source.vertical=vertical;
    result.source.handles={column.handle,earlier.handle};
    result.rules={"A30"};
    result.points={column.centre,earlier.centre};
    result.targetStation=vertical?column.centre.x:column.centre.y;
    result.otherTargetStation=vertical?earlier.centre.x:earlier.centre.y;
    output.results.push_back(std::move(result));
   }
  }
 }
 return output;
}

void productApplyRetreatAxes(ProductProjectAxes& project,std::size_t view,const ProductPlanBounds& bounds,
 const GenericNativeInput& input,const bcpproduct::GenericView& group,std::vector<GenericPlanAxis>& axes,double eps){
 for(auto& row:project.results){
  auto& source=row.source;
  if(source.view!=view||source.kind!="RETREAT"||!source.reason.empty())continue;
  const auto wall=productAxisWall(input,group,source.handles[0]);
  const auto note=std::find_if(input.notes.begin(),input.notes.end(),[&](const auto& item){
   return item.handle==source.handles[1]&&(item.text.find(L"退空")!=std::wstring::npos||item.text.find(L"无功能")!=std::wstring::npos);
  });
  if(!wall||wall->vertical!=source.vertical||note==input.notes.end()){
   source.reason=u8"指定主体轴墙或退空标记缺少可核实依据。";continue;
  }
  auto axis=std::find_if(axes.begin(),axes.end(),[&](const auto& candidate){
   return candidate.vertical==source.vertical&&std::abs(candidate.coordinate-wall->station)<=eps;
  });
  const bool negative=source.side%2==0;
  if(axis==axes.end()||axis->negative==axis->positive||negative==axis->negative){
   source.reason=u8"当前并非只有对侧具有效定位的轴。";continue;
  }
  const double datum=productPlanDatum(bounds,source.side);
  const double gap=negative?wall->lower-datum:datum-wall->upper;
  const double anchor=productAxisNormal(note->anchor,source.vertical);
  if(gap<=eps||(negative?(anchor<datum-eps||anchor>wall->lower+eps):(anchor>datum+eps||anchor<wall->upper-eps))){
   source.reason=u8"退空标记与源墙和指定侧边线不形成真实退让。";continue;
  }
  if(negative)axis->showNegative=true;else axis->showPositive=true;
  row.targetStation=wall->station;
 }
}

void productApplyAxisDetails(ProductJob& job,AcDbDatabase& db,ProductProjectAxes& project,std::size_t view,int panel,
 const GenericNativeInput& input,const bcpproduct::GenericView& group,const ProductPlanBounds& bounds,
 const std::vector<GenericPlanAxis>& axes,AcDbObjectId prototype,ProductPlanLayoutConfig& layout){
 const double eps=.01/layout.unitMM;
 const double paper=layout.scale/layout.unitMM;
 const auto dimension=[&](ProductAxisProjectResult& result,const AcGePoint3d& a,const AcGePoint3d& b,double gap){
  if(a.distanceTo(b)<=eps||std::abs(gap)<=eps)return;
  const auto chain=productTryPlanChain(job,db,prototype,{a,b},gap,view+1,
    result.source.handles.empty()?std::string{}:result.source.handles.front(),u8"局部轴线定位尺寸","D07");
  if(!chain){
   result.source.reason=u8"定位尺寸生成失败，原构件及定位依据保留";
   return;
  }
  const auto id=*chain;
  const auto entity=editObject<AcDbEntity>(id);
  productStatus(entity->setLayer(productPlanLayer(db,L"PUB_DIM",25)),"local geometry dimension layer");
  productStatus(entity->setColorIndex(256),"local geometry dimension ByLayer");
  result.objects.push_back(axisHandle(id));
  result.points.push_back(a);
  result.points.push_back(b);
 };
 for(auto& row:project.results){
  auto& source=row.source;
  if(source.view!=view||!source.reason.empty())continue;
  if(source.kind=="RETREAT"){
   const auto note=std::find_if(input.notes.begin(),input.notes.end(),[&](const auto& item){return item.handle==source.handles[1];});
   if(note!=input.notes.end())row.objects.push_back(note->handle);
   continue;
  }
  if(source.kind=="BATH_PARTITION"){
   const auto wall=productAxisWall(input,group,source.handles[0]);
   const auto note=std::find_if(input.notes.begin(),input.notes.end(),[&](const auto& item){
    return item.handle==source.handles[1]&&(item.text.find(L"卫生间")!=std::wstring::npos||item.text.find(L"公卫")!=std::wstring::npos);
   });
   if(!wall||wall->vertical!=source.vertical||note==input.notes.end()||
      (std::abs(wall->half*2*layout.unitMM-100)>0.01&&std::abs(wall->half*2*layout.unitMM-120)>0.01)){
    source.reason=u8"未证实卫生间名称和非主要正交隔墙。";continue;
   }
   const auto normal=productAxisNormal(note->anchor,source.vertical);
   if(normal<wall->lower-eps||normal>wall->upper+eps){
    source.reason=u8"卫生间文字与该隔墙实际范围关系未证实。";continue;
   }
   const bool negative=source.side%2==0;
   const double datum=productPlanDatum(bounds,source.side);
   const double inner=negative?wall->lower:wall->upper;
   const double end=datum+(negative?-1:1)*18*paper;
   if(negative?end>=inner-eps:end<=inner+eps){source.reason=u8"隔墙无可证的指定侧投影。";continue;}
   const auto& expected=layout.expectedPerimeter[{panel,source.side}];
   if(expected.stations.size()<2){source.reason=u8"缺用于局部尺寸的本侧实际轴站。";continue;}
   const auto station=std::min_element(expected.stations.begin(),expected.stations.end(),[&](double a,double b){
    return std::abs(a-wall->station)<std::abs(b-wall->station);
   });
   if(std::abs(*station-wall->station)<=eps){
    source.reason=u8"实际隔墙已经与可用轴站重合，局部投影需人工核定。";continue;
   }
   const auto a=productAxisPoint(AcGePoint3d(),source.vertical,inner,wall->station);
   const auto b=productAxisPoint(AcGePoint3d(),source.vertical,end,wall->station);
   dimension(row,productAxisPoint(b,source.vertical,end,*station),b,(negative?-1:1)*(source.vertical?1:-1)*4*paper);
   auto line=std::make_unique<AcDbLine>(a,b);
   line->setDatabaseDefaults(&db);
   productStatus(line->setLayer(productPlanLayer(db,L"AXIS-LOCAL",8)),"bathroom unnumbered projection layer");
   auto model=editObject<AcDbBlockTableRecord>(modelId(db));
   AcDbObjectId id;
   productStatus(model->appendAcDbEntity(id,line.get()),"draw local unnumbered bathroom projection");
   row.objects.push_back(axisHandle(id));
   row.points.push_back(a);row.points.push_back(b);
   line.release()->close();
   continue;
  }
  if(source.kind!="LOCAL_DIM"&&source.kind!="COURTYARD")continue;
  try{
   const auto boundary=open<AcDbPolyline>(find(db,source.handles.front()));
   require(boundary->ownerId()==modelId(db)&&boundary->visibility()==AcDb::kVisible,"local boundary must be visible source geometry");
   const auto point=[&](double vertex){
    require(vertex>=1&&std::floor(vertex)==vertex&&vertex<=boundary->numVerts(),"local boundary vertex index");
    AcGePoint3d p;
    productStatus(boundary->getPointAt(static_cast<unsigned>(vertex-1),p),"actual local boundary point");
   require(p.x>=bounds.xmin-50*paper&&p.x<=bounds.xmax+50*paper&&
            p.y>=bounds.ymin-50*paper&&p.y<=bounds.ymax+50*paper,"local boundary panel");
    return p;
   };
   const auto supported=[&](const auto& a,const auto& b){
    return a.distanceTo(b)>eps&&(std::abs(a.x-b.x)<=eps||std::abs(a.y-b.y)<=eps);
   };
   const auto a=point(source.values[0]),b=point(source.values[1]);
   require(supported(a,b),"local dimension requires actual orthogonal source vertices");
   if(source.kind=="LOCAL_DIM"){
    dimension(row,a,b,source.values[2]*paper);
   }else{
    require(boundary->isClosed(),"courtyard source boundary must be closed");
    const auto c=point(source.values[2]),d=point(source.values[3]);
    require(supported(c,d)&&(std::abs(a.x-b.x)<=eps)!=(std::abs(c.x-d.x)<=eps),"courtyard net dimensions need both directions");
    const auto [minimum,maximum]=std::minmax(source.values[4],source.values[5]);
    const bool axisDirection=std::abs(a.x-b.x)>eps;
    const auto hasAxis=[&](double station){return std::any_of(axes.begin(),axes.end(),[&](const auto& axis){
     return axis.vertical==axisDirection&&std::abs(axis.coordinate-station)<=eps&&!axis.writtenLines.empty();
    });};
    require(maximum-minimum>eps&&hasAxis(minimum)&&hasAxis(maximum),"courtyard axis span requires two proved logical stations");
    const auto first=axisDirection?AcGePoint3d(minimum,a.y,0):AcGePoint3d(a.x,minimum,0);
    const auto second=axisDirection?AcGePoint3d(maximum,a.y,0):AcGePoint3d(a.x,maximum,0);
    dimension(row,a,b,source.values[6]*paper);
    dimension(row,c,d,source.values[6]*paper);
    dimension(row,first,second,2*source.values[6]*paper);
   }
   if(row.objects.empty())source.reason=u8"局部尺寸缺可靠的纸面布置距离。";
  }catch(const std::exception&){
   source.reason=u8"不能由已确认边界复读净宽、净深或关联轴站，保留源构件。";
  }
 }
}

void productAuditAxisProject(ProductJob& job,AcDbDatabase& db,const ProductProjectAxes& project,
 const std::vector<std::vector<GenericPlanAxis>>& all,const std::vector<bool>& generated,
 const ProductPlanLayoutConfig& layout){
 if(!job.unifiedPlan)return;
 Working work(&db);
 const double eps=.01/layout.unitMM;
 std::ostringstream report;
 report<<std::setprecision(17)<<"{\"sourceSHA256\":\""<<job.sourceHash<<"\",\"savedReopened\":true,\"rows\":[";
 bool first=true;
 for(const auto& row:project.results){
  const auto& source=row.source;
  auto handles=row.objects;
  const auto match=[&](int view,std::optional<double> station)->const GenericPlanAxis*{
   if(view<0||view>=all.size()||!station)return nullptr;
   const auto found=std::find_if(all[view].begin(),all[view].end(),[&](const auto& axis){
    return axis.vertical==source.vertical&&std::abs(axis.coordinate-*station)<=eps&&!axis.writtenLines.empty();
   });
   return found==all[view].end()?nullptr:&*found;
  };
  const auto* axis=match(source.view,row.targetStation);
  const auto* other=match(source.otherView,row.otherTargetStation);
  bool verified=source.reason.empty();
  if(source.kind=="SHARED_WALL"||source.kind=="SHARED_COLUMNS"||source.kind=="COMMON_COLUMN"||source.kind=="EXACT_COLUMN_PAIR"){
   verified&=axis!=nullptr;
   if(source.kind=="COMMON_COLUMN"||source.kind=="EXACT_COLUMN_PAIR")
    verified&=other&&axis&&!axis->label.empty()&&axis->label==other->label;
   if(axis)for(const auto& line:axis->writtenLines)handles.push_back(line.first);
   if(other)for(const auto& line:other->writtenLines)handles.push_back(line.first);
   for(const auto& handle:handles){
    const auto line=open<AcDbLine>(find(db,handle));
    verified&=line->ownerId()==modelId(db)&&symbolName(line->layerId())==L"DOTE";
   }
   for(std::size_t i=0;i<row.points.size()&&i<2;++i){
    if(source.kind=="SHARED_WALL"&&i==1)break;
    const auto object=open<AcDbEntity>(find(db,source.handles[i]));
    const auto grips=chainGrips(object->objectId());
    verified&=std::wstring(object->isA()->name())==L"TDbColumn"&&!grips.empty()&&grips.front().distanceTo(row.points[i])<=eps;
   }
  }else if(source.kind=="RETREAT"){
   const bool negative=source.side%2==0;
   verified&=axis&&axis->showNegative&&axis->showPositive&&(negative?!axis->negative:!axis->positive);
   if(axis&&source.view<generated.size()&&generated[source.view]){
    const auto panel=static_cast<int>(std::count(generated.begin(),generated.begin()+source.view,true));
    const auto expected=layout.expectedPerimeter.find({panel,source.side});
    verified&=expected!=layout.expectedPerimeter.end()&&std::none_of(expected->second.stations.begin(),expected->second.stations.end(),
       [&](double station){return std::abs(station-axis->coordinate)<=eps;});
    bool sideCircle=false;
    const auto placement=productPlacementBounds(layout,panel);
    for(const auto& role:layout.axes){
     if(role.panel!=panel)continue;
     const auto original=layout.axisOriginalHandles.find(role.handle);
     if((original==layout.axisOriginalHandles.end()?role.handle:original->second)!=axis->labelHandle)continue;
     const auto grips=chainGrips(find(db,role.handle));
     for(std::size_t i=0;i+1<grips.size();i+=4){
      const auto side=productAxisEnd(placement,axis->vertical,grips[i],grips[i+1]).side;
      if(side!=source.side)continue;
      sideCircle=true;
      const auto anchor=productRealAxisLead(db,layout,role.handle,side,grips[i]);
      for(const auto& line:axis->writtenLines){
       const auto actual=open<AcDbLine>(find(db,line.first));
       const AcGeLineSeg3d segment(actual->startPoint(),actual->endPoint());
       verified&=segment.closestPointTo(anchor).distanceTo(anchor)>eps;
      }
      handles.push_back(role.handle);
     }
    }
    verified&=sideCircle;
   }
  }else{
   const auto expectedCount=source.kind=="COURTYARD"?3:source.kind=="BATH_PARTITION"?2:1;
   verified&=row.objects.size()==expectedCount;
   for(std::size_t i=0;i<row.objects.size();++i){
    const auto id=find(db,row.objects[i]);
    const auto entity=open<AcDbEntity>(id);
    const auto a=row.points[2*i],b=row.points[2*i+1];
    if(source.kind=="BATH_PARTITION"&&i==1){
     const auto line=AcDbLine::cast(entity.get());
     verified&=line&&line->startPoint().distanceTo(a)<=eps&&line->endPoint().distanceTo(b)<=eps&&
          symbolName(entity->layerId())==L"AXIS-LOCAL";
    }else{
     const auto points=chainGrips(id);
     const auto values=chainValues(id);
     const bool vertical=std::abs(a.x-b.x)<=eps;
     const auto station=[&](const auto& p){return vertical?p.y:p.x;};
     verified&=std::wstring(entity->isA()->name())==L"TDbDimension2"&&entity->visibility()==AcDb::kVisible&&
       values.size()==1&&points.size()==8&&std::abs(values.front()-a.distanceTo(b))<=eps&&
       std::abs(std::min(station(points[2]),station(points[3]))-std::min(station(a),station(b)))<=eps&&
       std::abs(std::max(station(points[2]),station(points[3]))-std::max(station(a),station(b)))<=eps;
    }
   }
  }
  std::ostringstream proof;
  proof<<std::setprecision(17)<<"{\"panel\":"<<source.view+1<<",\"kind\":\""<<source.kind<<"\",\"side\":"<<source.side
       <<",\"savedReopened\":true,\"report\":\"axis-project-readback.json\",\"sourceObjects\":";
  productWriteHandleArray(proof,source.handles);
  proof<<",\"objects\":";productWriteHandleArray(proof,handles);
  proof<<",\"geometry\":"<<wholePoints(row.points);
  if(row.targetStation)proof<<",\"targetStation\":"<<*row.targetStation;
  if(row.otherTargetStation)proof<<",\"otherPanel\":"<<source.otherView+1<<",\"otherTargetStation\":"<<*row.otherTargetStation;
  const auto reason=!source.reason.empty()?source.reason:verified?"":u8"保存重开后未复得全部轴、构件或局部尺寸证据。";
  proof<<",\"reason\":"<<jsonUtf8(bcpproduct::decodeMessage(reason))<<",\"result\":\""
       <<(verified?"verified-source-bound-geometry":"pending-source-bound-geometry")<<"\"}";
  if(!first)report<<',';
  first=false;
  report<<proof.str();
  for(const auto& rule:row.rules){
   productRecordRuleEvidence(job,rule,proof.str());
   if(!verified)job.detailPending.push_back({rule,std::to_string(source.view+1)+"/"+source.handles.front(),reason});
  }
 }
 report<<"]}";
 writeNew(job.root/L"axis-project-readback.json",report.str());
}

void productAuditOpeningAxisTerminals(ProductJob& job,AcDbDatabase& db,const ProductPlanLayoutConfig& layout){
 if(!job.unifiedPlan)return;
 const double eps=.01/layout.unitMM;
 Working work(&db);
 struct Chain {std::string handle;bool vertical;std::vector<AcGePoint3d> points;std::vector<double> values;};
 std::vector<Chain> chains;
 for(const auto& [raw,type]:modelTypes(db)){
  if(type!=L"TDbDimension2")continue;
  const auto handle=ascii(raw);
  const auto id=find(db,handle);
  const auto entity=open<AcDbEntity>(id);
  if(entity->visibility()!=AcDb::kVisible||symbolName(entity->layerId())!=L"PUB_DIM")continue;
  const auto points=chainGrips(id);
  const auto values=chainValues(id);
  if(values.empty()||points.size()!=2*values.size()+6)continue;
  chains.push_back({handle,std::abs(points[0].x-points[1].x)<=eps,points,values});
 }
 for(const auto& check:layout.openingAxisChecks){
  std::string matched;
  const auto measure=[&](const auto& p){return check.vertical?p.y:p.x;};
  const auto normal=[&](const auto& p){return check.vertical?p.x:p.y;};
  for(const auto& chain:chains){
   if(chain.vertical!=check.vertical||std::abs(normal(chain.points[2])-check.normal)>eps)continue;
   std::optional<std::size_t> terminal,edge;
   for(std::size_t i=0;i<=chain.values.size();++i){
    const double value=measure(chain.points[2+i]);
    if(std::abs(value-check.terminal)<=eps)terminal=i;
    if(std::abs(value-check.openingEdge)<=eps)edge=i;
   }
   if(!terminal||!edge||(*terminal!=0&&*terminal!=chain.values.size()))continue;
   const auto [first,last]=std::minmax(*terminal,*edge);
   const double sum=std::accumulate(chain.values.begin()+first,chain.values.begin()+last,0.);
   if(std::abs(sum-std::abs(check.terminal-check.openingEdge))<=eps){matched=chain.handle;break;}
  }
  std::ostringstream row;
  row<<std::setprecision(17)<<"{\"panel\":"<<check.panel+1<<",\"object\":\""<<check.opening
     <<"\",\"sourceWall\":\""<<check.axisSource<<"\",\"generatedChain\":\""<<check.originalChain
     <<"\",\"reopenedChain\":\""<<matched<<"\",\"axisStation\":"<<check.terminal
     <<",\"openingEdge\":"<<check.openingEdge<<",\"savedReopened\":true,\"result\":\""
     <<(matched.empty()?"pending-terminal-chain":"verified-opening-to-logical-axis-terminal")<<"\"}";
  productRecordRuleEvidence(job,"TL06",row.str());
  if(matched.empty())job.detailPending.push_back({"TL06",std::to_string(check.panel+1)+"/"+check.opening,
      u8"保存后未找到真实轴站与洞口边共同在有效链上的证据。"});
 }
}

void productInspectAxisCandidates(ProductJob& job,AcDbDatabase& db,const GenericNativeInput& input,
 const std::vector<bcpproduct::GenericView>& views,const std::vector<std::vector<GenericNativeColumn>>& columns,
 const std::vector<std::vector<GenericPlanAxis>>& axes,const ProductPlanLayoutConfig& layout,
 const ProductProjectAxes& project,const std::map<std::wstring,std::wstring>& original){
 if(!job.unifiedPlan)return;
 const double eps=.01/layout.unitMM;
 const auto emit=[&](const char* rule,std::size_t view,const std::string& handle,const std::string& scope,
                     const std::vector<std::string>& related,const std::vector<AcGePoint3d>& points){
  std::ostringstream row;
  row<<std::setprecision(17)<<"{\"panel\":"<<view+1<<",\"object\":\""<<handle<<"\",\"scope\":\""<<scope
     <<"\",\"relatedSourceObjects\":";
  productWriteHandleArray(row,related);
  row<<",\"sourceGeometry\":"<<wholePoints(points)
     <<",\"savedReopened\":true,\"result\":\"pending-project-role-or-supplement-purpose\"}";
  productRecordRuleEvidence(job,rule,row.str());
 };
 const auto covered=[&](const char* rule,std::size_t view,const std::string& handle){
  return std::any_of(project.results.begin(),project.results.end(),[&](const auto& record){
   return record.source.view==view&&record.source.reason.empty()&&
          std::find(record.rules.begin(),record.rules.end(),rule)!=record.rules.end()&&
          std::find(record.source.handles.begin(),record.source.handles.end(),handle)!=record.source.handles.end();
  });
 };
 for(std::size_t view=0;view<views.size();++view){
  if(axes[view].empty())continue;
  for(const auto& axis:axes[view]){
   if(axis.negative==axis.positive||axis.sourceWalls.empty()||covered("A21",view,axis.sourceWalls.front()))continue;
   const auto w=productAxisWall(input,views[view],axis.sourceWalls.front());
   if(!w)continue;
   emit("A21",view,axis.labelHandle,axis.negative?"positive-side-unproved-void":"negative-side-unproved-void",
        axis.sourceWalls,{productAxisPoint(AcGePoint3d(),axis.vertical,w->lower,axis.coordinate),
                          productAxisPoint(AcGePoint3d(),axis.vertical,w->upper,axis.coordinate)});
  }
  for(const auto& column:columns[view]){
   for(bool vertical:{true,false}){
    double minimum=std::numeric_limits<double>::infinity();
    std::optional<ProductAxisWallGeometry> nearest;
    std::string handle;
    const double position=vertical?column.centre.x:column.centre.y;
    const double along=vertical?column.centre.y:column.centre.x;
    double outside=std::numeric_limits<double>::infinity();
    double inside=-std::numeric_limits<double>::infinity();
    for(auto index:views[view].walls){
     const auto& name=input.walls[index].wall.handle;
     const auto w=productAxisWall(input,views[view],name);
     if(!w||w->vertical!=vertical||(std::abs(w->half*2*layout.unitMM-200)>.01&&std::abs(w->half*2*layout.unitMM-240)>.01))continue;
     outside=std::min(outside,w->station);inside=std::max(inside,w->station);
     if(along>=w->lower-eps&&along<=w->upper+eps)continue;
     const auto delta=std::abs(w->station-position);
     if(delta<minimum){minimum=delta;nearest=w;handle=name;}
    }
    if(!nearest)continue;
    const bool out=position<outside-eps||position>inside+eps;
    const double half=vertical?column.halfX:column.halfY;
    const bool closeOffset=minimum>eps&&minimum<=half+nearest->half+eps;
    if((out||closeOffset)&&!covered("A18",view,column.handle)){
     const auto projected=productAxisPoint(column.centre,vertical,along,nearest->station);
     emit("A18",view,column.handle,out?"outside-major-wall-axis-envelope":"near-wall-section-projection-porch-role-unproved",{handle},{column.centre,projected});
     emit("A15",view,column.handle,"actual-column-offset-to-closest-major-wall",{handle},{column.centre,projected});
    }
    if(!covered("A22",view,column.handle)&&std::none_of(axes[view].begin(),axes[view].end(),[&](const auto& axis){
      return axis.vertical==vertical&&std::abs(axis.coordinate-position)<=eps&&!axis.sourceWalls.empty();
    })){
     const auto projected=productAxisPoint(column.centre,vertical,along,nearest->station);
     emit("A22",view,column.handle,"independent-native-column-station-porch-role-unproved",{handle},{column.centre,projected});
    }
   }
  }
  for(std::size_t i=0;i<columns[view].size();++i)for(std::size_t j=i+1;j<columns[view].size();++j){
   const auto& a=columns[view][i];const auto& b=columns[view][j];
   if(covered("A22",view,a.handle)||covered("A22",view,b.handle))continue;
   for(bool vertical:{true,false}){
    const double x=vertical?a.centre.x:a.centre.y,y=vertical?b.centre.x:b.centre.y;
    const double u=vertical?a.halfX:a.halfY,v=vertical?b.halfX:b.halfY;
    if(std::abs(x-y)>eps&&std::abs(x-y)<u+v-eps)
     emit("A22",view,a.handle,vertical?"overlapping-x-column-projections":"overlapping-y-column-projections",
          {b.handle},{a.centre,b.centre});
   }
  }
  for(const auto& note:input.notes){
   if(note.text.find(L"卫生间")==std::wstring::npos&&note.text.find(L"公卫")==std::wstring::npos)continue;
   if(bcpproduct::nearestGenericView(views,{note.anchor.x,note.anchor.y})!=view||covered("A19",view,note.handle))continue;
   std::optional<std::pair<std::string,AcGePoint3d>> partition;
   double best=std::numeric_limits<double>::infinity();
   for(auto index:views[view].walls){
    const auto& name=input.walls[index].wall.handle;
    const auto w=productAxisWall(input,views[view],name);
    if(!w||(std::abs(w->half*2*layout.unitMM-100)>.01&&std::abs(w->half*2*layout.unitMM-120)>.01))continue;
    const auto along=productAxisNormal(note.anchor,w->vertical);
    if(along<w->lower-eps||along>w->upper+eps)continue;
    const auto projected=productAxisPoint(note.anchor,w->vertical,along,w->station);
    const auto distance=projected.distanceTo(note.anchor);
    if(distance<best){best=distance;partition=std::make_pair(name,projected);}
   }
   if(partition)emit("A19",view,partition->first,"bath-name-and-thin-wall-projection-candidate",{note.handle},{note.anchor,partition->second});
  }
 }
 for(const auto& [raw,type]:original){
  if(type!=L"AcDbPolyline")continue;
  const auto handle=ascii(raw);
  AcDbObjectId id;
  if(db.getAcDbObjectId(id,false,AcDbHandle(raw.c_str()))!=Acad::eOk||id.isErased())continue;
  const auto boundary=open<AcDbPolyline>(id);
  if(!boundary->isClosed()||boundary->numVerts()<6||boundary->visibility()!=AcDb::kVisible)continue;
  AcDbExtents extent;
  if(boundary->getGeomExtents(extent)!=Acad::eOk)continue;
  const auto& a=extent.minPoint();const auto& b=extent.maxPoint();
  const auto view=bcpproduct::nearestGenericView(views,{(a.x+b.x)/2,(a.y+b.y)/2});
  if(covered("D07",view,handle))continue;
  const auto& actual=views[view].bounds;
  if(b.x-a.x<(actual.xmax-actual.xmin)*.75||b.y-a.y<(actual.ymax-actual.ymin)*.75)continue;
  for(unsigned i=0;i<boundary->numVerts();++i){
   AcGePoint3d start,end;
   double bulge{};
   if(boundary->getPointAt(i,start)!=Acad::eOk||boundary->getPointAt((i+1)%boundary->numVerts(),end)!=Acad::eOk||
      boundary->getBulgeAt(i,bulge)!=Acad::eOk||std::abs(bulge)>1e-8||start.distanceTo(end)<=eps)continue;
   const bool innerX=std::abs(start.x-end.x)<=eps&&start.x>a.x+eps&&start.x<b.x-eps;
   const bool innerY=std::abs(start.y-end.y)<=eps&&start.y>a.y+eps&&start.y<b.y-eps;
   if(innerX||innerY)emit("D07",view,handle+":"+std::to_string(i+1),"actual-outline-interior-return-needs-dimension-purpose",{}, {start,end});
  }
 }
}
