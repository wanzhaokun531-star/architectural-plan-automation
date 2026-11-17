namespace {
struct PlanPostcheckFinding {
 std::size_t panel{};
 std::string source,code,reason;
 std::vector<std::string> related;
 std::optional<AcGePoint3d> position;
 bool confirmed{};
};

std::size_t productPostcheckPanel(const ProductJob& job,const std::optional<AcGePoint3d>& point){
 if(!point)return 0;
 for(std::size_t i=0;i<job.planPrintWindows.size();++i){
  const auto& w=job.planPrintWindows[i];
  if(point->x>=w[0]&&point->x<=w[2]&&point->y>=w[1]&&point->y<=w[3])
   return i<job.layoutSourcePanels.size()?job.layoutSourcePanels[i]:i+1;
 }
 return 0;
}

void productPostcheckLooseInk(const ProductJob& job,AcDbDatabase& db,
 const std::vector<ProductSourceAnnotation>& annotations,const std::vector<ProductLooseDimensionStroke>& strokes,
 std::vector<PlanPostcheckFinding>& findings){
 std::set<std::string> known;
 for(const auto& [key,_]:job.planLooseDimensions)known.insert(key);
 std::map<long,std::vector<ProductSourceAnnotation>> angleGroups;
 for(const auto& note:annotations){
  if(!note.visible||!note.textDirection||!productPostcheckPanel(job,note.position)||known.count(note.path)||
     !productLooseDimensionValue(note))continue;
  const auto& direction=*note.textDirection;
  constexpr double quarterTurn=1.5707963267948966;
  const double angle=std::remainder(std::atan2(direction.y,direction.x),quarterTurn);
  const auto key=std::abs(angle)<.001?0L:std::lround(angle*10000.);
  angleGroups[key].push_back(note);
 }
 if(angleGroups.empty())return;
 const auto types=modelTypes(db);
 auto anchors=productPerimeterSourceWitnesses(db,job);
 anchors.erase(std::remove_if(anchors.begin(),anchors.end(),[&](const auto& anchor){
  const auto type=types.find(bcpproduct::decodeMessage(anchor.handle));
  return type==types.end()||(type->second!=L"TDbWall"&&type->second!=L"TDbColumn"&&
     type->second!=L"TDbOpening"&&type->second!=L"TDbGroupOpening");
 }),anchors.end());
 for(const auto& [angleKey,notes]:angleGroups){
  auto projected=notes;
  const auto rotation=AcGeMatrix3d::rotation(-angleKey/10000.,AcGeVector3d::kZAxis,AcGePoint3d::kOrigin);
  std::set<std::string> parents;
  for(auto& note:projected){
   parents.insert(productAnnotationParent(note.path));
   note.position->transformBy(rotation);
   note.textDirection->transformBy(rotation);
  }
  std::vector<ProductLooseDimensionStroke> edges;
  for(const auto& stroke:strokes)if(parents.count(productAnnotationParent(stroke.path))){
   auto edge=stroke;
   edge.start.transformBy(rotation);edge.end.transformBy(rotation);
   edges.push_back(std::move(edge));
  }
  auto placedAnchors=anchors;
  for(auto& anchor:placedAnchors)anchor.point.transformBy(rotation);
  const auto hits=productRecognizeLooseDimensions(projected,edges,placedAnchors,
   job.unifiedScale/job.unifiedUnitMM,job.unifiedUnitMM);
  for(const auto& original:notes){
   const auto hit=hits.find(original.path);
   if(hit==hits.end()||hit->second.stations.size()!=2)continue;
   const auto& row=hit->second;
   if(row.anchors.size()!=2&&!row.dimensionLayerHint)continue;
   auto related=row.strokes;
   related.insert(related.end(),row.anchors.begin(),row.anchors.end());
   findings.push_back({productPostcheckPanel(job,original.position),original.path,"unreconciled-loose-dimension",
       row.reason,std::move(related),original.position,row.anchors.size()==2});
  }
 }
}

void productAuditSavedPlan(ProductJob& job,const fs::path& drawing,const std::string& beforePrintHash,
 const std::string& finalHash,ULONGLONG generationMilliseconds){
 const auto start=GetTickCount64();
 std::vector<PlanPostcheckFinding> findings;
 std::size_t annotationCount=0;
 std::string error;
 try{
  require(sha(drawing)==finalHash,"postcheck drawing version");
  AcDbDatabase reading(false,true);
  productStatus(reading.readDwgFile(drawing.c_str(),AcDbDatabase::kForReadAndAllShare,false),"open plan for read-only postcheck");
  productStatus(reading.closeInput(true),"complete read-only plan input");
  Working working(&reading);
  std::vector<ProductLooseDimensionStroke> strokes;
  const auto annotations=productReadOldAnnotations(reading,&strokes);
  annotationCount=annotations.size();
  const auto types=modelTypes(reading);
  std::map<std::string,std::vector<std::size_t>> panelsByFloor;
  for(const auto panel:job.layoutSourcePanels){
   const auto owner=job.sourceInventory.items.find({panel,"panel",""});
   const auto floor=job.planSourceFloorNames.find(panel);
   if(owner!=job.sourceInventory.items.end()&&owner->second.basis=="floor"&&floor!=job.planSourceFloorNames.end())
    panelsByFloor[floor->second].push_back(panel);
  }
  for(const auto& [floor,panels]:panelsByFloor)if(panels.size()>1)
   for(const auto panel:panels)findings.push_back({panel,"panel","multiple-printed-panels-for-floor",
      floor+u8"存在多幅主体平面，应核定本次采用的源图范围",{},{}});
  auto bound=job.planPostcheckKnownDimensions;
  for(const auto& [key,item]:job.sourceInventory.items){
   const auto& [panel,scope,source]=key;
   if((item.check==bcpproduct::PlanCheck::pending&&!job.planPreservedDesign.count(key))||
      item.check==bcpproduct::PlanCheck::failed)
    findings.push_back({panel,source,"open-source-"+scope,item.reason,item.output,{}});
   bound.insert(item.output.begin(),item.output.end());
   if(item.check!=bcpproduct::PlanCheck::verified||item.output.empty()||
      (scope!="name"&&scope!="opening-number"&&scope!="source-dimension"&&
       scope!="wall-axis"&&scope!="column-axis-X"&&scope!="column-axis-Y"))continue;
   const auto exists=[&](const std::string& handle){
    const auto root=handle.substr(0,handle.find('/'));
    return types.count(bcpproduct::decodeMessage(root))!=0;
   };
   if(std::none_of(item.output.begin(),item.output.end(),exists))
    findings.push_back({panel,source,"missing-linked-"+scope,u8"已核应有项在最终重开的模型中找不到对应成果对象",
        item.output,{},true});
  }
  for(const auto& [key,item]:job.drawingChecks.items){
   const auto& [panel,scope,path]=key;
   bound.insert(path.substr(0,path.find('/')));
   if((item.state==bcpproduct::PlanCheck::pending||item.state==bcpproduct::PlanCheck::failed)&&
      (scope=="interior-clearance"||scope=="interior-baseline"||scope=="source-text-clearance"||
       scope=="opening-caption-clearance"||scope=="printed-dimension-ink"||scope=="dimension-style"))
    findings.push_back({panel,path,"open-drawing-"+scope,item.reason,{},{}});
  }
  for(const auto& row:annotations){
   const auto panel=productPostcheckPanel(job,row.position);
   if(!row.visible||!panel)continue;
   if(row.kind.rfind("unreadable-",0)==0)
    findings.push_back({panel,row.path,"unreadable-visible-annotation",u8"打印范围内的旧标注或块实例无法完整读取",{},row.position});
   else if(row.kind=="dimension"&&!bound.count(row.path)&&!job.planDimensionExclusions.count(row.path))
    findings.push_back({panel,row.path,"unmatched-visible-dimension",u8"输出中可见的尺寸缺对应源项或独立尺寸核验记录",{},row.position});
  }
  productPostcheckLooseInk(job,reading,annotations,strokes,findings);
  require(sha(drawing)==finalHash,"read-only postcheck observed a change to the candidate");
 }catch(const std::exception& failure){error=failure.what();}
 if(job.planPostcheckMode==L"repair"){
  if(!error.empty())job.drawingChecks.record(0,"independent-annotation-postcheck","dwg",{"P04","P06"},
   bcpproduct::PlanReleaseGate::reopened,bcpproduct::PlanCheck::failed,u8"独立重开查漏",error);
  else for(const auto& item:findings)if(item.confirmed)
   job.drawingChecks.record(item.panel,item.code,item.source,{"P04","P06"},
     bcpproduct::PlanReleaseGate::reopened,bcpproduct::PlanCheck::failed,
     u8"独立重开查漏已核实际标注或关联测站",item.reason);
 }
 std::ostringstream out;
 const auto quoted=[](const std::string& value){return jsonUtf8(bcpproduct::decodeMessage(value));};
 out<<std::setprecision(17)<<"{\"contract\":\"plan.postcheck.v1\",\"sourceSHA256\":\""<<job.sourceHash
    <<"\",\"drawingSHA256\":\""<<finalHash<<"\",\"prePrintCandidateSHA256\":\""<<beforePrintHash
    <<"\",\"mode\":"<<jsonUtf8(job.planPostcheckMode)<<",\"readOnly\":true"
    <<",\"generationMilliseconds\":"<<generationMilliseconds<<",\"auditMilliseconds\":"<<GetTickCount64()-start
    <<",\"annotationsRead\":"<<annotationCount<<",\"error\":"<<quoted(error)<<",\"findings\":[";
 bool first=true;
 for(const auto& item:findings){
  if(!first)out<<',';
  first=false;
  out<<"{\"panel\":"<<item.panel<<",\"source\":"<<quoted(item.source)<<",\"issueCode\":"<<quoted(item.code)
     <<",\"reason\":"<<quoted(item.reason)<<",\"position\":"
     <<wholePoints(item.position?std::vector<AcGePoint3d>{*item.position}:std::vector<AcGePoint3d>{})<<",\"related\":";
  productWriteHandleArray(out,item.related);
  out<<'}';
 }
 out<<"]}";
 writeNew(job.root/L"plan-postcheck.json",out.str());
}
}
