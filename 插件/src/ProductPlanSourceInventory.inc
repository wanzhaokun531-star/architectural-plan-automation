void productCapturePlanInventory(ProductJob& job,AcDbDatabase& db,const GenericNativeInput& input,
 const std::vector<bcpproduct::GenericView>& views,const std::map<std::wstring,std::wstring>& original,
 const std::vector<ProductPlanNameCheck>& names,const std::vector<ProductPanelOwnership>& ownership){
 if(!job.unifiedPlan)return;
 auto& inventory=job.sourceInventory;
 const auto panelAt=[&](const AcGePoint3d& point){
  return bcpproduct::nearestGenericView(views,{point.x,point.y})+1;
 };
 const auto panelOf=[&](const AcDbEntity& entity){
  AcDbExtents extents;
  if(entity.getGeomExtents(extents)!=Acad::eOk)return std::size_t{};
  return panelAt(extents.minPoint()+(extents.maxPoint()-extents.minPoint())*.5);
 };
 const auto add=[&](std::size_t panel,const char* scope,const std::string& handle,
    std::vector<std::string> rules,const std::string& basis,const char* reason){
  inventory.expect(panel,scope,handle,std::move(rules),basis,reason);
 };
 const auto details=productPlanDetailConfirmations(job);
 add(0,"roof","",{"A29","D27","QA10"},u8"整套平面屋顶图必查",
     u8"须核独立屋顶图、实际檐边、各侧有效轴距及施工定位");
 for(std::size_t i=0;i<views.size();++i){
  const auto panel=i+1;
  if(ownership[i].floor)job.planSourceFloorNames.emplace(panel,std::to_string(*ownership[i].floor)+u8"层");
  else if(ownership[i].role==ProductPanelRole::roof)job.planSourceFloorNames.emplace(panel,u8"屋顶平面");
  add(panel,"panel","",{"P02","T07"},productPanelRoleName(ownership[i].role),ownership[i].reason.c_str());
  if(!ownership[i].eligible()){
   if(ownership[i].parentPanel)for(const auto index:views[i].walls){
    const auto& wall=input.walls[index];
    if(wall.low)continue;
    const auto parent=*ownership[i].parentPanel+1;
    add(parent,"wall-height",wall.wall.handle,{"B04","SE02"},u8"本层已关联附属原生墙",
        u8"按所属主体楼层核墙高和基标高");
    const double thickness=wall.total*job.unifiedUnitMM;
    if(std::abs(thickness-200)<.01||std::abs(thickness-240)<.01)
     add(parent,"wall-axis",wall.wall.handle,{"A01","A09","A16"},u8"已关联附属主要墙体中线",
         u8"按所属主体楼层核轴位和有效范围");
   }
   continue;
  }
  add(panel,"floor-datum","",{"B04","SE02"},u8"本幅完成面标高",u8"缺楼层身份、完成面依据或原生标高回读");
  add(panel,"story-height","",{"B04","SE02"},u8"本幅层高独立于完成面标高",
      u8"须核该图幅楼层归属、已确认层高、相邻楼面差或规则默认值");
  add(panel,"net-domain","",{"S01","S03","S06","QA09"},u8"本幅墙内边、无名区及计量口径",
      u8"需核闭合净域、无名区用途、连通分区以及建筑面积口径");
  for(int side=0;side<4;++side)
   add(panel,"perimeter",std::string(1,"WESN"[side]),{"D01","D02","D03","D04","D06","QA10"},
       u8"源平面四侧逐侧核定位轴及洞口",u8"须核前两道闭合、第三道适用条件及保留链");
  for(const auto index:views[i].walls){
   const auto& wall=input.walls[index];
   const auto& handle=wall.wall.handle;
   if(wall.low)continue;
   add(panel,"wall-height",handle,{"B04","SE02"},u8"本层原生墙独立核高",
       u8"须按所属楼层的层高写入并独立重开核墙高和原底标高");
   const double thickness=wall.total*job.unifiedUnitMM;
   if(std::abs(thickness-200)<.01||std::abs(thickness-240)<.01)
    add(panel,"wall-axis",handle,{"A01","A09","A16"},u8"主要墙体中线",
        wall.orthogonal?u8"主要墙轴位置及服务范围待读回":u8"斜墙轴需按实际几何核适用及编号");
  }
 }
 for(const auto& column:input.columns)
  for(const auto* scope:{"column-axis-X","column-axis-Y"})
   if(ownership[panelAt(column.centre)-1].eligible())
    add(panelAt(column.centre),scope,column.handle,{"A01","A09","A16"},u8"源柱双方向定位",
       u8"实际柱中心、共轴条件及独立轴服务待读回");
 const auto entranceSettings=productEntranceSettings(
  job.unifiedEntranceSteps.empty()?job.profile/L"plan-policy/entrance-steps.tsv":job.unifiedEntranceSteps,job.sourceHash);
 std::vector<std::optional<ProductRoomGrid>> entranceGrids(views.size());
 for(std::size_t i=0;i<views.size();++i){
  if(!ownership[i].eligible()||ownership[i].floor!=std::optional<int>{1})continue;
  std::vector<std::array<double,4>> rectangles;
  for(const auto index:views[i].walls)if(!input.walls[index].low&&input.walls[index].orthogonal)
   rectangles.push_back(approvedRect(input.walls[index].wall));
  entranceGrids[i]=productRoomGrid(rectangles);
  productCloseRoomExteriorOpenings(job,input,views,i,job.unifiedUnitMM,rectangles,entranceGrids[i]);
 }
 for(const auto& opening:input.openings){
  const auto mid=opening.a+(opening.b-opening.a)*.5;
  const auto panel=panelAt(mid);
  if(!ownership[panel-1].eligible())continue;
  add(panel,"opening-number",opening.handle,{"O01","Q04","TL02"},u8"源原生洞口 "+productUtf8(opening.existing),
      u8"待核本洞口的宿主、编码、显示与省标条件");
  const bool configured=entranceSettings.count(opening.handle)!=0;
  if(!configured&&(opening.window||opening.kind.find(L"门")==std::wstring::npos))continue;
  if(!configured&&ownership[panel-1].floor&&*ownership[panel-1].floor!=1)continue;
  const auto host=productGenericHost(opening,input,views[panel-1]);
  const auto number=opening.existing;
  const auto& grid=entranceGrids[panel-1];
  const auto side=host&&grid?productEntranceSideFromDomains(opening,*host,input,*grid,job.unifiedUnitMM):
   ProductEntranceSide{host?host->side:-1,false};
  if(configured||!host||(!side.enclosedInterior&&side.side>=0)||number.rfind(L"FDM",0)==0||number.rfind(L"FMD",0)==0)
   add(panel,"entrance",opening.handle,{"C01","C02","C20"},u8"外墙或宿主待判的源门",
       u8"须核首层室外出入口归属、台阶、平台门扇及室内外完成面");
 }
 for(const auto& name:names){
  const auto panel=name.view<views.size()?name.view+1:0;
  if(panel&&!ownership[panel-1].eligible())continue;
  add(panel,"name",name.source,{"N01","N05","QA09"},productUtf8(name.text),u8"须重开核实际名称对象与字体");
  const auto role=productRoomRole(name.text);
  const bool confirmedArea=std::any_of(job.confirmedRooms.begin(),job.confirmedRooms.end(),[&](const auto& room){
   return room.panel==panel&&room.source==name.source;
  });
  if(confirmedArea||role==ProductRoomRole::indoor||role==ProductRoomRole::wet||role==ProductRoomRole::washing)
   add(panel,"room-area",name.source,{"S03","S05","QA09"},productUtf8(name.text),
       u8"未证实本用途对应的独立或已确认合算净域、净面积及成图落字");
  if(name.text!=L"阳台"&&name.text!=L"露台")continue;
  add(panel,"balcony",name.source,{"D29","Q07"},productUtf8(name.text),u8"须核平台临空实边及各边栏杆反坎");
  std::set<std::string> edges;
  const auto projected=details.exteriorNameProjections.find(name.source);
  if(projected!=details.exteriorNameProjections.end())edges.insert(projected->second.edges.begin(),projected->second.edges.end());
  const auto boundary=details.exteriorNameBoundaries.find(name.source);
  if(boundary!=details.exteriorNameBoundaries.end())edges.insert(boundary->second.begin(),boundary->second.end());
  for(const auto& edge:edges)
   add(panel,"balcony-edge",name.source+"/"+edge,{"D29","Q07"},u8"源平台实边引用",
       u8"须核该边临空、墙厚、内侧方向、收口和四线实测");
 }
 for(const auto& [handle,type]:original){
  const auto key=ascii(handle);
  const auto entity=open<AcDbEntity>(find(db,key));
  if(!productPlanDimensionVisible(*entity))continue;
  if(type.find(L"Dimension")!=std::wstring::npos){
   auto dimension=productReadSourceDimension(db,key,.01/job.unifiedUnitMM);
   const auto panel=productDimensionSourcePanel(dimension,views,.01/job.unifiedUnitMM);
   job.sourceDimensions.emplace(key,std::move(dimension));
   add(panel,"source-dimension",key,{"D22","Q03"},productUtf8(type),
       u8"须核原测点、测值与保留、替换或省标去向");
   add(panel,"source-dimension-disposition",key,{"G06","Y06"},productUtf8(type),
       u8"须核原句柄是否保留或有据退出及对应成果对象");
  }else if(type.find(L"Stair")!=std::wstring::npos||
           (type==L"AcDbPolyline"&&symbolName(entity->layerId())==L"楼梯间边界")){
   add(panelOf(*entity),"stair",key,{"SE01","SE11","SE16"},productUtf8(type),
       u8"须核每跑、踏步、平台、方向及上下端完成面");
  }else if(entity->isAProxy()||
           (type==L"TDbWall"&&std::none_of(input.walls.begin(),input.walls.end(),[&](const auto& wall){return wall.wall.handle==key;}))||
           (type==L"TDbColumn"&&std::none_of(input.columns.begin(),input.columns.end(),[&](const auto& column){return column.handle==key;}))||
           ((type==L"TDbOpening"||type==L"TDbGroupOpening")&&
            std::none_of(input.openings.begin(),input.openings.end(),[&](const auto& opening){return opening.handle==key;}))){
   add(panelOf(*entity),"unreadable-object",key,{"G05","Q02"},productUtf8(type),
       u8"公开属性或几何不可读，须补识别；保留原对象");
  }
 }
 const auto stairProfile=job.unifiedIndoorStairs.empty()?job.profile/L"plan-policy/indoor-stairs.tsv":job.unifiedIndoorStairs;
 std::ifstream profile(stairProfile);
 std::string tag,hash,line;
 if(profile>>tag>>hash&&tag=="PLANSTAIRS1"&&hash==job.sourceHash){
  std::getline(profile,line);
  while(std::getline(profile,line)&&line!="END"){
   std::istringstream record(line);
   std::string key;
   if(record>>tag>>key&&tag=="STRAIGHT")
    add(0,"stair-flight",key,{"SE06","SE07","SE11","SE16"},line,u8"源踏步关联、原生梯跑和竖向参数待核");
  }
 }
 inventory.sealed=true;
 productWritePlanInventory(job);
}
