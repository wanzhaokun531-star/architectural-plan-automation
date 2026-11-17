bcpproduct::PlanConfirmedDimensions readConfirmedPlanDimensionFile(const fs::path& path,
 const std::string& sourceHash,double unit) {
 if(path.empty())return {};
 std::ifstream input(path);
 require(bool(input),"read confirmed dimension stations");
 return bcpproduct::readPlanConfirmedDimensions(input,sourceHash,unit);
}


void applyConfirmedPlanDimensions(ProductJob& job,AcDbDatabase& db,AcDbDatabase& reference,
 const fs::path& path,const std::vector<ProductPlanBounds>& panels,const std::vector<bool>& active,
 double unit,std::vector<PlanNativeDimension>& dimensions) {
 if(path.empty())return;
 const auto requests=readConfirmedPlanDimensionFile(path,job.sourceHash,unit);
 AcDbObjectId prototype;
 for(const auto& row:requests.rows){
  const AcGePoint3d a(row.first[0],row.first[1],0),b(row.second[0],row.second[1],0);
  require(static_cast<std::size_t>(row.panel)<=panels.size()&&active[row.panel-1]&&
   inPanel(a,panels[row.panel-1])&&inPanel(b,panels[row.panel-1]),
   "new dimension must use verified stations in one active plan");
  const auto existing=std::find_if(dimensions.begin(),dimensions.end(),[&](const auto& dim){
   if(dim.panel!=row.panel-1||dim.values.empty())return false;
   const auto same=[&](const auto& first,const auto& second,const auto& direction){
    return bcpproduct::planSameDimensionStations({first.x,first.y},{second.x,second.y},
     {direction.x,direction.y},row.first,row.second,unit);
   };
   if(dim.stock)return dim.projection&&same(dim.projection->a,dim.projection->b,dim.projection->direction);
   if(dim.grips.size()!=2*dim.values.size()+6)return false;
   const auto direction=dim.grips[1]-dim.grips[0];
   for(std::size_t i=0;i<dim.values.size();++i)
    if(same(dim.grips[i+2],dim.grips[i+3],direction))return true;
   return false;
  });
  std::string handle;
  if(existing!=dimensions.end())handle=existing->handle;
  else{
   if(prototype.isNull())prototype=cloneOne(reference,find(reference,"19A0"),db);
   const auto id=productDetailChain(db,prototype,{a,b},row.offset);
   handle=axisHandle(id);
   dimensions.push_back({handle,row.panel-1,std::abs(a.x-b.x)*unit<.01,
    false,true,chainGrips(id),chainValues(id),"",{}});
  }
  // Legacy coordinate files remain accepted, but cannot silently acquire source binding.
  job.drawingChecks.record(static_cast<std::size_t>(row.panel),"confirmed-dimension-input",handle,
   {"D24","Q03"},bcpproduct::PlanReleaseGate::mainPlan,
    requests.sourceBound?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::pending,
    requests.sourceBound?"PLANDIMENSIONS1 source SHA-256":"legacy coordinate input",
    requests.sourceBound?"":u8"新增尺寸坐标文件缺源图指纹；核实测点后以 PLANDIMENSIONS1 绑定当前 DWG");
 }
 if(!prototype.isNull())status(editObject<AcDbEntity>(prototype)->erase(),"retire confirmed dimension prototype");
}
