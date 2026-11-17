struct ProductBalconyEdge {
 std::size_t panel{};
 std::string space,source,wall;
 std::array<ProductOutdoorEdge,4> geometry;
 std::array<std::string,4> objects;
 double wallMM{};
 bool confirmed{};
 std::string issue;
};

bool productSameBalconyLine(const ProductOutdoorEdge& a,const ProductOutdoorEdge& b,double tolerance){
 return (a.first.distanceTo(b.first)<=tolerance&&a.second.distanceTo(b.second)<=tolerance)||
        (a.first.distanceTo(b.second)<=tolerance&&a.second.distanceTo(b.first)<=tolerance);
}

std::vector<ProductBalconyEdge> productGenerateBalconyEdges(ProductJob& job,AcDbDatabase& db,
 const GenericNativeInput& input,const std::vector<bcpproduct::GenericView>& views,double unit){
 if(!job.unifiedPlan)return {};
 const double tolerance=.01/unit;
 const auto confirmations=productPlanDetailConfirmations(job);
 std::vector<ProductBalconyEdge> rows;
 std::map<std::pair<std::string,std::string>,std::tuple<std::string,int,double,double>> confirmed;
 std::ifstream profile(job.profile/L"plan-policy/balcony-edges.tsv");
 if(profile){
  std::string token,hash;
  require(bool(profile>>token>>hash)&&token=="PLANBALCONY1","valid balcony edge profile");
  if(hash==job.sourceHash){
   while(profile>>token&&token!="END"){
    std::string space,edge,wall;
    int inward{};
    double startMM{},endMM{};
    require(token=="STRAIGHT"&&bool(profile>>space>>edge>>wall>>inward>>startMM>>endMM)&&
     (inward==1||inward==-1)&&std::isfinite(startMM)&&std::isfinite(endMM)&&startMM>=0&&endMM>=0,
     "confirmed free edge, inner side and independent end clearances");
    require(confirmed.emplace(std::make_pair(space,edge),std::make_tuple(wall,inward,startMM,endMM)).second,
     "duplicate confirmed balcony edge");
   }
   require(token=="END","complete balcony edge profile");
   profile>>std::ws;
   require(profile.eof(),"unexpected trailing balcony edge input");
  }
 }
 for(const auto& note:input.notes){
  if(note.text!=L"阳台"&&note.text!=L"露台")continue;
  const auto panel=bcpproduct::nearestGenericView(views,{note.anchor.x,note.anchor.y})+1;
  std::set<std::string> edges;
  if(const auto found=confirmations.exteriorNameProjections.find(note.handle);found!=confirmations.exteriorNameProjections.end())
   edges.insert(found->second.edges.begin(),found->second.edges.end());
  if(const auto found=confirmations.exteriorNameBoundaries.find(note.handle);found!=confirmations.exteriorNameBoundaries.end())
   edges.insert(found->second.begin(),found->second.end());
  for(const auto& [pair,setting]:confirmed)if(pair.first==note.handle)edges.insert(pair.second);
  if(edges.empty()){
   ProductBalconyEdge row;row.panel=panel;row.space=note.handle;
   row.issue=u8"缺平台实边清单、临空和通行边界；需按实际围护逐边确认";
   rows.push_back(std::move(row));
  }
  for(const auto& edge:edges){
   ProductBalconyEdge row;row.panel=panel;row.space=note.handle;row.source=edge;
   const auto geometry=productOutdoorEdge(db,edge,tolerance);
   if(geometry)row.geometry[0]=*geometry;
   const auto configured=confirmed.find({note.handle,edge});
   if(configured==confirmed.end()||!geometry){
    row.issue=u8"缺该段临空及开口、反坎外边基准、对应墙厚或端部构造依据";
    rows.push_back(std::move(row));continue;
   }
   const auto [wall,inward,startMM,endMM]=configured->second;
   row.wall=wall;
   const auto found=std::find_if(input.walls.begin(),input.walls.end(),[&](const auto& item){return item.wall.handle==wall;});
   if(found!=input.walls.end())row.wallMM=found->total*unit;
   if(std::abs(row.wallMM-200)>.01&&std::abs(row.wallMM-240)>.01){
    row.issue=u8"当前边段未关联实际 200／240 mm 对应墙体，其他墙厚须项目做法";
    rows.push_back(std::move(row));continue;
   }
   const auto direction=(geometry->second-geometry->first).normal();
   const auto normal=AcGeVector3d(-direction.y,direction.x,0)*inward;
   const auto start=geometry->first+direction*(startMM/unit),end=geometry->second-direction*(endMM/unit);
   if((end-start).dotProduct(direction)<=tolerance||(note.anchor-start).dotProduct(normal)<=row.wallMM/unit){
    row.issue=u8"边段扣除端部净空后无有效长度，或内侧方向与实际平台名称位置冲突";
    rows.push_back(std::move(row));continue;
   }
   const double centre=row.wallMM/2;
   const std::array<double,4> offsets{0,centre-40,centre+40,row.wallMM};
   for(std::size_t i=0;i<offsets.size();++i){
    const auto delta=normal*(offsets[i]/unit);
    row.geometry[i]={start+delta,end+delta};
   }
   row.confirmed=true;
   rows.push_back(std::move(row));
  }
 }
 // Intersect actual planned segments in the drawing plane. An unresolved corner
 // or overlapping run must be handled before writing either side of it.
 const auto intersects=[&](const ProductOutdoorEdge& a,const ProductOutdoorEdge& b){
  if(std::abs(a.first.z-b.first.z)>tolerance)return false;
  const auto spans=[&](double u,double v,double x,double y){return std::min(std::max(u,v),std::max(x,y))>=std::max(std::min(u,v),std::min(x,y))-tolerance;};
  return spans(a.first.x,a.second.x,b.first.x,b.second.x)&&spans(a.first.y,a.second.y,b.first.y,b.second.y);
 };
 std::set<std::size_t> bad;
 for(std::size_t i=0;i<rows.size();++i)if(rows[i].confirmed)
  for(std::size_t j=i+1;j<rows.size();++j)if(rows[j].confirmed&&rows[i].panel==rows[j].panel)
   for(const auto& a:rows[i].geometry)for(const auto& b:rows[j].geometry)if(intersects(a,b)){
    bad.insert(i);bad.insert(j);
   }
 const auto snapshot=modelTypes(db);
 for(std::size_t index=0;index<rows.size();++index){
  auto& row=rows[index];
  if(!row.confirmed)continue;
  if(bad.count(index)){
   row.issue=u8"相邻边段的四线存在转角接触或交叠，需给定交接或分段端点";
   continue;
  }
  const auto roles=std::array<const wchar_t*,4>{L"反坎",L"栏杆",L"栏杆",L"反坎"};
  bool obstructed=false;
  for(const auto& [handle,type]:snapshot){
   const auto key=ascii(handle);
   const auto entity=open<AcDbEntity>(find(db,key));
   const auto layer=symbolName(entity->layerId());
   if((layer!=L"栏杆"&&layer!=L"反坎")||!productPlanDimensionVisible(*entity))continue;
   const auto edge=productOutdoorEdge(db,key,tolerance);
   if(!edge){
    AcDbExtents bounds;
    if(entity->getGeomExtents(bounds)!=Acad::eOk){obstructed=true;continue;}
    for(const auto& line:row.geometry)
     if(intersects({bounds.minPoint(),bounds.maxPoint()},line))obstructed=true;
    continue;
   }
   for(std::size_t i=0;i<roles.size();++i)if(intersects(*edge,row.geometry[i])){
    if(layer==roles[i]&&productSameBalconyLine(*edge,row.geometry[i],tolerance)&&row.objects[i].empty())row.objects[i]=key;
    else obstructed=true;
   }
  }
  if(obstructed){row.issue=u8"现有栏杆或反坎表达与拟布线冲突；保留已有线并逐段复核";continue;}
  for(std::size_t i=0;i<roles.size();++i)if(row.objects[i].empty()){
   auto line=std::make_unique<AcDbLine>(row.geometry[i].first,row.geometry[i].second);
   line->setDatabaseDefaults(&db);
   const auto layer=productPlanLayer(db,roles[i],7);
   productStatus(line->setLayer(layer),"balcony component layer");
   productStatus(line->setColorIndex(256),"balcony component ByLayer");
   auto model=editObject<AcDbBlockTableRecord>(modelId(db));
   AcDbObjectId id;
   productStatus(model->appendAcDbEntity(id,line.get()),"append real balcony detail line");
   line.release()->close();
   row.objects[i]=axisHandle(id);
  }
 }
 return rows;
}

void productVerifyBalconyEdges(ProductJob& job,AcDbDatabase& db,const std::vector<ProductBalconyEdge>& rows,double unit){
 if(!job.unifiedPlan)return;
 Working working(&db);
 for(const auto& row:rows){
  const bool verified=row.issue.empty()&&std::all_of(row.objects.begin(),row.objects.end(),[](const auto& h){return !h.empty();});
  bool geometry=verified;
  if(verified)for(std::size_t i=0;i<row.objects.size();++i){
   const auto entity=open<AcDbEntity>(find(db,row.objects[i]));
   const auto line=productOutdoorEdge(db,row.objects[i],.01/unit);
   const auto role=i==0||i==3?L"反坎":L"栏杆";
   geometry&=line&&symbolName(entity->layerId())==role&&productSameBalconyLine(*line,row.geometry[i],.01/unit);
  }
  const auto issue=geometry?std::string{}:row.issue.empty()?u8"重开后线距、端点、对象或图层未通过":row.issue;
  if(job.architecturalPlanOnly&&!geometry&&!row.confirmed){
   const auto preserved=[&](const std::string& handle){
    AcDbObjectId id;
    return !handle.empty()&&db.getAcDbObjectId(id,false,AcDbHandle(bcpproduct::decodeMessage(handle).c_str()))==Acad::eOk&&
     !id.isErased()&&productPlanDimensionVisible(*open<AcDbEntity>(id));
   };
   const auto name=job.sourceInventory.items.find({row.panel,"name",row.space});
   const bool nameRetained=preserved(row.space)||(name!=job.sourceInventory.items.end()&&
    name->second.check==bcpproduct::PlanCheck::verified&&std::any_of(name->second.output.begin(),name->second.output.end(),preserved));
   if(nameRetained){
    job.planPreservedDesign.emplace(row.panel,"balcony",row.space);
    if(preserved(row.source))job.planPreservedDesign.emplace(row.panel,"balcony-edge",row.space+"/"+row.source);
   }
  }
  if(!issue.empty())job.detailPending.push_back({u8"栏杆及反坎",row.source.empty()?row.space:row.source,issue});
  if(!row.source.empty())
   productCheckPlanSource(job,row.panel,"balcony-edge",row.space+"/"+row.source,
    geometry?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::pending,
    geometry?std::vector<std::string>(row.objects.begin(),row.objects.end()):std::vector<std::string>{},issue);
  std::ostringstream evidence;
  evidence<<"{\"panel\":"<<row.panel<<",\"object\":"<<jsonUtf8(bcpproduct::decodeMessage(row.source.empty()?row.space:row.source))
   <<",\"space\":\""<<row.space<<"\",\"wall\":\""<<row.wall<<"\",\"wallMM\":"<<row.wallMM
   <<",\"expectedOuterPoints\":"<<wholePoints({row.geometry[0].first,row.geometry[0].second})
   <<",\"upstand\":[\""<<row.objects[0]<<"\",\""<<row.objects[3]<<"\"],\"railing\":[\""<<row.objects[1]<<"\",\""<<row.objects[2]
   <<"\"],\"savedReopened\":true,\"result\":\""<<(geometry?"verified-plan-balcony-edge":"pending-balcony-edge")
   <<"\",\"detail\":"<<jsonUtf8(bcpproduct::decodeMessage(issue))<<'}';
  productRecordRuleEvidence(job,"D29",evidence.str());
  productRecordRuleEvidence(job,"Q07",evidence.str());
 }
}
