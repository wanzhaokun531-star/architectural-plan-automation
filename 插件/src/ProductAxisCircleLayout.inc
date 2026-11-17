constexpr wchar_t productAxisFoldApp[] = L"PLAN_AXIS_FOLD";

double productAxisStation(const AcGePoint3d& point,bool vertical){
 return vertical?point.x:point.y;
}

AcGePoint3d productAxisPoint(AcGePoint3d point,bool vertical,double normal,double station){
 if(vertical){point.x=station;point.y=normal;}
 else{point.x=normal;point.y=station;}
 return point;
}

std::vector<AcGePoint3d> productFoldPoints(AcDbDatabase& db,const ProductPlanLayoutConfig& c,
        const std::string& handle,int side){
 const auto found=c.axisFolds.find({handle,side});
 if(found==c.axisFolds.end())return {};
 const auto fold=open<AcDbPolyline>(find(db,found->second));
 require(fold->visibility()==AcDb::kVisible&&!fold->isClosed()&&fold->numVerts()==3,
         "visible three-point axis fold");
 auto* marker=fold->xData(productAxisFoldApp);
 const std::wstring expected(handle.begin(),handle.end());
 const bool linked=marker&&marker->rbnext&&marker->rbnext->restype==AcDb::kDxfXdHandle&&
      marker->rbnext->resval.rstring&&expected==marker->rbnext->resval.rstring&&
      marker->rbnext->rbnext&&marker->rbnext->rbnext->resval.rint==side;
 if(marker)acutRelRb(marker);
 require(linked,"fold must reference its native axis label and side");
 std::vector<AcGePoint3d> points(3);
 for(unsigned i=0;i<3;++i)status(fold->getPointAt(i,points[i]),"read saved axis fold vertex");
 return points;
}

AcGePoint3d productRealAxisLead(AcDbDatabase& db,const ProductPlanLayoutConfig& c,
        const std::string& handle,int side,const AcGePoint3d& nativeLead){
 const auto points=productFoldPoints(db,c,handle,side);
 if(points.empty())return nativeLead;
 require(points.back().distanceTo(nativeLead)<1e-5,"axis fold must meet its native label lead");
 return points.front();
}

double productAxisFoldBandStart(AcDbDatabase& db,const ProductPlanLayoutConfig& c,
        int panel,int side,const bcpproduct::PlanAnnotationLayout& spacing,
        const std::function<AcDbObjectId(const std::string&)>& id){
 const auto sign=side%2?1:-1;
 double outer=sign*productPlacementDatum(c,panel,side)+spacing.total;
 for(const auto& dim:c.dimensions){
  if(dim.panel!=panel||dim.side!=side)continue;
  AcDbExtents extent;
  const auto entity=open<AcDbEntity>(id(dim.handle));
  status(entity->getGeomExtents(extent),"read dimension bounds before placing axis fold");
  const auto point=sign>0?extent.maxPoint():extent.minPoint();
  outer=std::max(outer,sign*(side<2?point.x:point.y));
 }
 return outer;
}

std::pair<double,double> productAxisCircleLimits(AcDbDatabase& db,const ProductPlanBounds& panel,
        bool vertical,double normal,double radius){
 const double infinity=std::numeric_limits<double>::infinity();
 std::pair<double,double> limits{-infinity,infinity};
 double smallest=infinity;
 for(const auto& item:modelTypes(db)){
  if(item.second!=L"AcDbPolyline")continue;
  const auto frame=open<AcDbPolyline>(find(db,ascii(item.first)));
  if(!frame->isClosed()||frame->numVerts()!=4||symbolName(frame->layerId())!=L"楼层范围")continue;
  AcDbExtents box;
  if(frame->getGeomExtents(box)!=Acad::eOk)continue;
  const auto lo=box.minPoint(),hi=box.maxPoint();
  if(lo.x>panel.xmin||lo.y>panel.ymin||hi.x<panel.xmax||hi.y<panel.ymax)continue;
  if(productAxisNormal(lo,vertical)>normal-radius||productAxisNormal(hi,vertical)<normal+radius)continue;
  const auto area=(hi.x-lo.x)*(hi.y-lo.y);
  if(area>=smallest)continue;
  smallest=area;
  limits={productAxisStation(lo,vertical)+radius,productAxisStation(hi,vertical)-radius};
 }
 return limits;
}

AcDbObjectId productCloneAxisLabel(AcDbDatabase& db,AcDbObjectId source){
 const auto entity=open<AcDbEntity>(source);
 std::unique_ptr<AcDbEntity> clone(AcDbEntity::cast(entity->clone()));
 require(clone&&std::wstring(clone->isA()->name())==L"TDbAxisLabelSet","real native axis label clone");
 auto model=editObject<AcDbBlockTableRecord>(modelId(db));
 AcDbObjectId id;
 status(model->appendAcDbEntity(id,clone.get()),"append native axis label");
 status(clone.release()->close(),"close native axis label");
 return id;
}

AcDbObjectId productAxisLabelWithTail(AcDbDatabase& db,AcDbObjectId label,const AcGePoint3d& target){
 const auto before=approvedgrips::readGeometry(label);
 auto expected=before;
 require(before.grips.size()==4&&before.stretch.size()==2,"single-ended native axis for folded tail");
 const auto lead=before.grips[0];
 require(std::count_if(before.stretch.begin(),before.stretch.end(),[&](const auto& p){
  return p.distanceTo(lead)<approvedgrips::eps;
 })==1,"unique native tail stretch point");
 expected.grips[0]=target;
 for(auto& p:expected.stretch)if(p.distanceTo(lead)<approvedgrips::eps)p=target;
 const auto view=approvedgrips::view();
 int count=1;
 const auto attempt=[&](int index,int second,const AcGeVector3d& correction){
  const auto trial=productCloneAxisLabel(db,label);
  bool keep=false;
  struct Dispose {AcDbObjectId id;bool& keep;~Dispose(){
   if(!keep){AcDbEntity* entity{};if(acdbOpenObject(entity,id,AcDb::kForWrite)==Acad::eOk){entity->erase();entity->close();}}
  }} dispose{trial,keep};
  auto result=approvedgrips::apply(db,trial,index,second,0,before,expected,view,true,correction);
  count=result.count;
  keep=result.committed&&approvedgrips::same(approvedgrips::readGeometry(trial),expected);
  return std::make_pair(keep?trial:AcDbObjectId::kNull,result);
 };
 std::ostringstream diagnostic;
 diagnostic<<"native folded tail could not be shortened; expected "<<approvedgrips::json(expected);
 for(int index=0;index<count;++index){
  AcGeVector3d correction(0,0,0);
  auto [changed,result]=attempt(index,-1,correction);
  if(!changed.isNull())return changed;
  diagnostic<<"; grip "<<index<<", status "<<result.status<<", after "<<approvedgrips::json(result.after);
  if(!result.innerMatched&&result.status==Acad::eOk&&result.after.grips.size()==before.grips.size()&&
     result.after.grips[0].distanceTo(lead)>approvedgrips::eps){
   correction=target-result.after.grips[0];
   std::tie(changed,result)=attempt(index,-1,correction);
   if(!changed.isNull())return changed;
  }
  if(result.innerMatched)for(int second=0;second<count;++second){
   std::tie(changed,result)=attempt(index,second,correction);
   if(!changed.isNull())return changed;
  }
 }
 throw std::runtime_error(diagnostic.str());
}

void productTraceAxisLayout(ProductJob* job,const std::string& handle,const char* stage){
 if(!job||!job->validation)return;
 std::ofstream trace(job->root/L"axis-layout-native-steps.tsv",std::ios::app);
 trace<<GetTickCount64()<<'\t'<<handle<<'\t'<<stage<<'\n';
}

void productKeepAxisLabelEnd(AcDbDatabase& db,AcDbObjectId label,bool vertical,const AcGePoint3d& wanted,bool isolate=true,
                            ProductJob* job=nullptr){
 require(curDoc()&&curDoc()->database()==&db,"axis side editing needs owned active document");
 const auto staging=isolate?AcGeVector3d(200000,200000,0):AcGeVector3d::kIdentity;
 if(isolate){
  {auto entity=editObject<AcDbEntity>(label);status(entity->transformBy(AcGeMatrix3d::translation(staging)),"isolate native side edit");}
  wholeStagingView({wanted+staging});
  require(acedCommandS(RTSTR,L"_.REGEN",RTNONE)==RTNORM,"refresh isolated axis before side edit");
 }
 const auto target=wanted+staging;
 for(int attempt=0;attempt<4;++attempt){
  const auto g=chainGrips(label);
  if(g.size()==4&&g[1].distanceTo(target)<1e-4){
   if(isolate){
    auto entity=editObject<AcDbEntity>(label);
    status(entity->transformBy(AcGeMatrix3d::translation(-staging)),"restore retained axis end");
   }
   return;
  }
  require(g.size()==4||g.size()==8,"native axis side grips");
  const auto radius=g[1].distanceTo(g[2]);
  require(radius>0,"native axis circle selection radius");
  const auto pick=g[1]+(vertical?AcGeVector3d(radius,0,0):AcGeVector3d(0,radius,0));
  productTraceAxisLayout(job,axisHandle(label),"side-edit-enter");
  if(isolate)approvedCommandPoint(L"TObjEdit",pick,L"M");
  else{
   ads_name selected{};
   status(acdbGetAdsName(selected,label),"select exact native axis for side edit");
   wholeStagingView({pick});
   require(acedCommandS(RTSTR,L"TObjEdit",RTENAME,selected,RTSTR,L"M",RTSTR,L"",RTNONE)==RTNORM,
           "edit native axis side by object identity");
  }
  productTraceAxisLayout(job,axisHandle(label),"side-edit-returned");
  require(acedCommandS(RTSTR,L"_.REGEN",RTNONE)==RTNORM,"refresh native axis side edit");
  productTraceAxisLayout(job,axisHandle(label),"side-regen-returned");
 }
 throw std::runtime_error("native axis side edit did not preserve requested end "+axisHandle(label)+
     " expected "+wholePoints({target})+" actual "+wholePoints(chainGrips(label)));
}

AcDbObjectId productAppendAxisFold(AcDbDatabase& db,AcDbObjectId label,AcDbObjectId layer,int side,
        const std::vector<AcGePoint3d>& points){
 auto polyline=std::make_unique<AcDbPolyline>(3);
 polyline->setDatabaseDefaults(&db);
 status(polyline->setLayer(layer),"axis fold layer");
 status(polyline->setColorIndex(256),"axis fold by layer");
 polyline->setElevation(points.front().z);
 for(unsigned i=0;i<points.size();++i)
  status(polyline->addVertexAt(i,{points[i].x,points[i].y}),"axis fold point");
 {auto table=open<AcDbRegAppTable>(db.regAppTableId());if(!table->has(productAxisFoldApp)){
   table.reset();auto edit=editObject<AcDbRegAppTable>(db.regAppTableId());
   auto app=std::make_unique<AcDbRegAppTableRecord>();
   status(app->setName(productAxisFoldApp),"axis fold application");
   AcDbObjectId id;status(edit->add(id,app.get()),"register axis fold application");
   status(app.release()->close(),"close axis fold application");
 }}
 auto model=editObject<AcDbBlockTableRecord>(modelId(db));
 AcDbObjectId id;
 status(model->appendAcDbEntity(id,polyline.get()),"append editable axis fold");
 auto* entity=polyline.release();
 bool linked=false;
 struct ClosePolyline{AcDbPolyline* value;bool& linked;~ClosePolyline(){if(!linked)value->erase();value->close();}} close{entity,linked};
 const auto handle=axisHandle(label);
 const std::wstring owner(handle.begin(),handle.end());
 auto* xdata=acutBuildList(AcDb::kDxfRegAppName,productAxisFoldApp,AcDb::kDxfXdHandle,owner.c_str(),
                         AcDb::kDxfXdInteger16,side,RTNONE);
 require(xdata!=nullptr,"allocate native fold association");
 const auto set=entity->setXData(xdata);
 acutRelRb(xdata);
 status(set,"attach fold to native axis label");
 linked=true;
 return id;
}

void productArrangeAxisCircles(AcDbDatabase& db,ProductPlanLayoutConfig& c,AcDbObjectId layer,
        const bcpproduct::PlanAnnotationLayout& spacing,const std::function<AcDbObjectId(const std::string&)>& id,
        ProductJob* job=nullptr){
 struct End {std::string handle;AcGePoint3d lead,centre;};
 using Side=std::pair<int,int>;
 std::map<Side,std::vector<End>> groups;
 for(const auto& role:c.axes){
  const auto g=chainGrips(id(role.handle));
  for(std::size_t i=0;i<g.size();i+=4){
   const int side=productAxisEnd(productPlacementBounds(c,role.panel),role.side!=0,g[i],g[i+1]).side;
   groups[{role.panel,side}].push_back({role.handle,g[i],g[i+1]});
  }
 }
 struct Move {double position,begin,end;};
 std::map<std::pair<std::string,int>,Move> moves;
 const auto scale=c.scale/c.unitMM;
 const auto gap=spacing.circleDiameter+scale*bcpproduct::PlanPaperPolicy::circleClearance;
 const auto maximum=scale*bcpproduct::PlanPaperPolicy::circleMaxShift;
 for(auto& [key,ends]:groups){
  const auto [panel,side]=key;
  const bool vertical=side>=2;
  const double sign=side%2?1:-1;
  std::sort(ends.begin(),ends.end(),[&](const End& a,const End& b){
   return productAxisStation(a.lead,vertical)<productAxisStation(b.lead,vertical);
  });
  std::vector<double> stations;
  for(const auto& end:ends)stations.push_back(productAxisStation(end.lead,vertical));
  if(std::adjacent_find(stations.begin(),stations.end(),[](double a,double b){return b-a<=1e-5;})!=stations.end()){
   c.axisCircleConflicts[key]="coincident real axis stations require service or duplication review";
   continue;
  }
  const auto conflict=std::adjacent_find(stations.begin(),stations.end(),
          [&](double a,double b){return b-a<gap-1e-5;});
  if(conflict==stations.end())continue;
  const auto datum=productPlacementDatum(c,panel,side);
  const auto limits=productAxisCircleLimits(db,c.panels[panel],vertical,
     datum+sign*spacing.circleCentre,spacing.circleDiameter/2);
  const auto solution=bcpproduct::packPlanAxisCircles(stations,gap,maximum,limits.first,limits.second);
  const double outer=productAxisFoldBandStart(db,c,panel,side,spacing,id);
  const double room=sign*datum+spacing.circleNear-outer;
  if(!solution||room<=1e-5){
   c.axisCircleConflicts[key]=solution?"dimension text occupies the fold band":"no fit on the same line within allowed shift or frame";
   continue;
  }
  for(std::size_t i=0;i<ends.size();++i)
   if(std::abs((*solution)[i]-stations[i])>1e-5)
    moves[{ends[i].handle,side}]={(*solution)[i],sign*(outer+room/3),sign*(outer+room*2/3)};
 }
 if(moves.empty())return;
 std::vector<ProductPlanRole> result;
 std::vector<AcDbObjectId> created,replaced;
 try{
  for(const auto& role:c.axes){
   const bool vertical=role.side!=0;
   const auto native=id(role.handle);
   const auto g=chainGrips(native);
   const auto panel=productPlacementBounds(c,role.panel);
   std::vector<int> sides;
   for(std::size_t i=0;i<g.size();i+=4)sides.push_back(productAxisEnd(panel,vertical,g[i],g[i+1]).side);
   if(std::none_of(sides.begin(),sides.end(),[&](int side){return moves.count({role.handle,side})!=0;})){
    result.push_back(role);continue;
   }
   for(std::size_t i=0;i<sides.size();++i){
    const int side=sides[i];
    auto label=productCloneAxisLabel(db,native);
    created.push_back(label);
    productTraceAxisLayout(job,role.handle,"fold-label-cloned");
    // Pick the clone by its native identity. A fixed spatial offset can land
    // on another drawing in a multi-scheme source and select its geometry.
    if(sides.size()==2)productKeepAxisLabelEnd(db,label,vertical,g[4*i+1],false,job);
    const auto move=moves.find({role.handle,side});
    if(move!=moves.end()){
     const auto anchor=g[4*i];
     const auto& route=move->second;
     const auto shift=route.position-productAxisStation(anchor,vertical);
     {auto entity=editObject<AcDbEntity>(label);
      status(entity->transformBy(AcGeMatrix3d::translation(vertical?AcGeVector3d(shift,0,0):AcGeVector3d(0,shift,0))),
             "slide native circle along the same row");}
     const auto nativeLead=productAxisPoint(anchor,vertical,route.end,route.position);
     const auto straight=label;
     productTraceAxisLayout(job,role.handle,"fold-tail-enter");
     label=productAxisLabelWithTail(db,straight,nativeLead);
     productTraceAxisLayout(job,role.handle,"fold-tail-returned");
     created.push_back(label);
     {auto entity=editObject<AcDbEntity>(straight);status(entity->erase(),"remove straight trial axis label");}
     const auto fold=productAppendAxisFold(db,label,layer,side,{anchor,
           productAxisPoint(anchor,vertical,route.begin,productAxisStation(anchor,vertical)),nativeLead});
     created.push_back(fold);
     c.axisFolds[{axisHandle(label),side}]=axisHandle(fold);
    }
    result.push_back({axisHandle(label),role.panel,role.side,role.row});
    c.axisOriginalHandles[axisHandle(label)]=role.handle;
   }
   c.axisOriginalGrips[role.handle]=g;
   replaced.push_back(native);
  }
 }catch(const std::exception& error){
  for(auto object:created)if(!object.isErased()){auto entity=editObject<AcDbEntity>(object);status(entity->erase(),"roll back proposed axis fold");}
  c.axisFolds.clear();
  c.axisOriginalHandles.clear();
  c.axisOriginalGrips.clear();
  for(const auto& [key,ends]:groups)if(std::any_of(ends.begin(),ends.end(),[&](const End& end){
   return moves.count({end.handle,key.second})!=0;}))c.axisCircleConflicts[key]=error.what();
  return;
 }
 for(auto object:replaced){auto entity=editObject<AcDbEntity>(object);status(entity->erase(),"replace original axis label after fold succeeds");}
 c.axes=std::move(result);
}
