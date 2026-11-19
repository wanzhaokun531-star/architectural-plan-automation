struct ProductPerimeterChainReadback {
 std::string handle,root;
 std::vector<double> stations,values;
 std::vector<AcGePoint3d> points;
 double baseline{};
 bool native{};
 bool visibleNumbers{};
};

struct ProductPerimeterSourceWitness {
 std::string handle;
 AcGePoint3d point;
 bool exactDefinitionOnly{};
};

std::vector<ProductPerimeterSourceWitness> productPerimeterSourceWitnesses(AcDbDatabase& db,const ProductJob& job){
 std::vector<ProductPerimeterSourceWitness> witnesses;
 std::set<std::string> confirmedStepEdges;
 const auto entranceProfile=job.unifiedEntranceSteps.empty()?job.profile/L"plan-policy/entrance-steps.tsv":job.unifiedEntranceSteps;
 for(const auto& [door,entry]:productEntranceSettings(entranceProfile,job.sourceHash))
  if(entry.state=="EXISTING")confirmedStepEdges.insert(entry.existingLines.begin(),entry.existingLines.end());
 for(const auto& [wide,type]:modelTypes(db)){
  const auto handle=ascii(wide);
  const auto entity=open<AcDbEntity>(find(db,handle));
  const auto name=symbolName(entity->layerId());
  if(type==L"TDbWall"||type==L"TDbColumn"||type==L"TDbOpening"||type==L"TDbGroupOpening"){
   if(!productPlanDimensionVisible(*entity))continue;
   auto points=chainGrips(entity->objectId());
   if(type==L"TDbWall"&&points.size()>=2){
    const auto& a=points[0];
    const auto& b=points[1];
    AcDbExtents ext;
    if(entity->getGeomExtents(ext)==Acad::eOk){
     const auto low=ext.minPoint(),high=ext.maxPoint();
     if(std::abs(a.x-b.x)<1e-5&&std::abs(low.x+high.x-a.x*2)<1e-5)
      for(const auto y:{a.y,b.y})for(const auto x:{low.x,high.x})witnesses.push_back({handle,{x,y,0}});
     if(std::abs(a.y-b.y)<1e-5&&std::abs(low.y+high.y-a.y*2)<1e-5)
      for(const auto x:{a.x,b.x})for(const auto y:{low.y,high.y})witnesses.push_back({handle,{x,y,0}});
    }
   }
   if((type==L"TDbOpening"||type==L"TDbGroupOpening")&&points.size()>3)points.resize(3);
   for(const auto& point:points)witnesses.push_back({handle,point});
  }else if(const auto* poly=AcDbPolyline::cast(entity.get())){
   const bool confirmedStep=confirmedStepEdges.count(handle)&&productPlanDimensionVisible(*entity);
   if(!confirmedStep&&name!=L"SURFACE"&&name!=L"Defpoints"&&name!=L"WALL"&&name!=L"平-外墙轮廓")continue;
   for(unsigned i=0;i<poly->numVerts();++i){
    AcGePoint3d point;
    if(poly->getPointAt(i,point)==Acad::eOk)witnesses.push_back({handle,point,confirmedStep});
   }
  }else if(const auto* line=AcDbLine::cast(entity.get())){
   const bool confirmedStep=confirmedStepEdges.count(handle)&&productPlanDimensionVisible(*entity);
   if(!confirmedStep&&name!=L"SURFACE"&&name!=L"WALL"&&name!=L"DOTE"&&name!=L"平-外墙轮廓")continue;
   witnesses.push_back({handle,line->startPoint(),confirmedStep});
   witnesses.push_back({handle,line->endPoint(),confirmedStep});
  }
 }
 return witnesses;
}

std::vector<std::string> productMasks(AcDbDatabase& db){
 std::vector<std::string> handles;
 const auto table=open<AcDbBlockTable>(db.blockTableId());
 AcDbBlockTableIterator* raw{};
 productStatus(table->newIterator(raw),"inventory mask block table");
 const std::unique_ptr<AcDbBlockTableIterator> blocks(raw);
 for(;!blocks->done();blocks->step()){
  AcDbObjectId recordId;
  productStatus(blocks->getRecordId(recordId),"inventory mask block record");
  const auto record=open<AcDbBlockTableRecord>(recordId);
  if(record->isFromExternalReference())continue;
  AcDbBlockTableRecordIterator* iterator{};
  productStatus(record->newIterator(iterator),"inventory mask block objects");
  const std::unique_ptr<AcDbBlockTableRecordIterator> objects(iterator);
  for(;!objects->done();objects->step()){
   AcDbObjectId id;
   productStatus(objects->getEntityId(id),"inventory mask object");
   const auto entity=open<AcDbEntity>(id);
   if(AcDbWipeout::cast(entity.get()))handles.push_back(axisHandle(id));
  }
 }
 std::sort(handles.begin(),handles.end());
 return handles;
}

void productWriteHandleArray(std::ostream& out,const std::vector<std::string>& handles){
 out<<'[';
 for(std::size_t i=0;i<handles.size();++i){
  if(i)out<<',';
  out<<jsonUtf8(bcpproduct::decodeMessage(handles[i]));
 }
 out<<']';
}

void productWriteStationArray(std::ostream& out,const std::vector<double>& stations){
 out<<'[';
 for(std::size_t i=0;i<stations.size();++i){if(i)out<<',';out<<stations[i];}
 out<<']';
}

void productAuditPerimeterReconciliation(ProductJob& job,AcDbDatabase& db,const ProductPlanLayoutConfig& config,
                                        const std::vector<std::string>& sourceMasks,
                                        const std::set<std::string>& entranceDimensions,
                                        const std::vector<ProductPerimeterSourceWitness>& sourceWitnesses){
 if(!job.unifiedPlan)return;
 Working work(&db);
 const double eps=.01/config.unitMM;
 using Side=std::pair<int,int>;
 std::map<Side,std::vector<ProductPerimeterChainReadback>> sides;
 std::map<std::string,std::pair<Side,int>> roles;
 for(const auto& role:config.dimensions)roles.emplace(role.handle,std::make_pair(Side{role.panel,role.side},role.row));
 std::vector<std::string> unreadable;
 std::vector<std::string> verifiedInteriors;
 const auto orientation=[&](const AcGePoint3d& a,const AcGePoint3d& b)->std::optional<bool>{
  if(a.distanceTo(b)<=eps)return {};
  if(std::abs(a.x-b.x)<=eps)return true;
  if(std::abs(a.y-b.y)<=eps)return false;
  return {};
 };
 const auto visit=[&](const auto& self,AcDbObjectId id,const AcGeMatrix3d& matrix,
                     const std::string& parent,const std::string& root,unsigned depth)->void{
  if(depth>16){unreadable.push_back(parent);return;}
  const auto object=open<AcDbEntity>(id);
  const auto layer=open<AcDbLayerTableRecord>(object->layerId());
  if(object->visibility()!=AcDb::kVisible||layer->isOff()||layer->isFrozen())return;
  const auto key=axisHandle(id);
  const auto handle=parent.empty()?key:parent+"/"+key;
  const auto owner=root.empty()?key:root;
  if(config.projectedPerimeter.count(owner)||entranceDimensions.count(owner))return;
  if(const auto block=AcDbBlockReference::cast(object.get())){
   const auto definition=open<AcDbBlockTableRecord>(block->blockTableRecord());
   if(definition->isFromExternalReference())return;
   AcDbBlockTableRecordIterator* iterator{};
   productStatus(definition->newIterator(iterator),"perimeter block inventory");
   const std::unique_ptr<AcDbBlockTableRecordIterator> children(iterator);
   for(;!children->done();children->step()){
    AcDbObjectId child;
    productStatus(children->getEntityId(child),"perimeter block member");
    self(self,child,matrix*block->blockTransform(),handle,owner,depth+1);
   }
   return;
  }
  const auto type=std::wstring(object->isA()->name());
  const auto* ordinary=AcDbDimension::cast(object.get());
  const bool native=type==L"TDbDimension2"&&!object->isAProxy();
  if(!native&&!ordinary){
   if(type.find(L"Dimension")!=std::wstring::npos)unreadable.push_back(handle);
   return;
  }
  try{
   std::vector<AcGePoint3d> points;
   AcGePoint3d baseline;
   std::vector<double> values;
   bool readable=false;
   if(native){
    const auto grips=chainGrips(id);
    values=chainValues(id);
    if(values.empty()||grips.size()!=2*values.size()+6){unreadable.push_back(handle);return;}
    baseline=grips.front();
    points.assign(grips.begin()+2,grips.begin()+3+values.size());
    TemplateParts parts(object.get());
    if(object->explode(parts.values)==Acad::eOk&&parts.values.length()==values.size()){
     readable=true;
     for(int i=0;i<parts.values.length();++i){
      auto* segment=AcDbDimension::cast(static_cast<AcRxObject*>(parts.values[i]));
      AcString overrideText,shown;
      double measured{};
      readable&=segment&&segment->measurement(measured)==Acad::eOk&&
          std::abs(measured-values[i])<=eps&&segment->dimensionText(overrideText)==Acad::eOk&&
          (overrideText.isEmpty()||overrideText==L"<>")&&
          segment->formatMeasurement(shown,measured,overrideText.isEmpty()?nullptr:overrideText.kACharPtr())==Acad::eOk&&
          !shown.isEmpty();
     }
    }
   }else if(const auto* dim=AcDbAlignedDimension::cast(object.get())){
    points={dim->xLine1Point(),dim->xLine2Point()};baseline=dim->dimLinePoint();
   }else if(const auto* dim=AcDbRotatedDimension::cast(object.get())){
    points={dim->xLine1Point(),dim->xLine2Point()};baseline=dim->dimLinePoint();
   }else return;
   baseline.transformBy(matrix);
   for(auto& point:points)point.transformBy(matrix);
   const auto vertical=orientation(points.front(),points.back());
   if(!vertical)return;
   const auto station=[&](const auto& point){return *vertical?point.y:point.x;};
   const auto normal=[&](const auto& point){return *vertical?point.x:point.y;};
   std::vector<double> stations;
   for(const auto& point:points)stations.push_back(station(point));
   if(stations.front()>stations.back()){
    std::reverse(stations.begin(),stations.end());
    std::reverse(values.begin(),values.end());
    std::reverse(points.begin(),points.end());
   }
   const auto expectedRole=roles.find(handle);
   if(native&&readable&&parent.empty()&&expectedRole==roles.end()&&values.size()+1==points.size()){
    const auto belongsToInterior=[&](std::size_t panel){
     const auto sourcePanel=job.layoutSourcePanels.at(panel);
     const auto measured=job.planVerifiedInteriorMeasurementPanels.find(handle);
     if(measured!=job.planVerifiedInteriorMeasurementPanels.end()&&measured->second==sourcePanel)return true;
     const auto check=job.drawingChecks.items.find({sourcePanel,"interior-baseline",handle});
     if(check==job.drawingChecks.items.end()||check->second.state!=bcpproduct::PlanCheck::verified||
        check->second.basis.rfind("saved-reopened",0)!=0)return false;
     const auto& bounds=config.panels[panel];
     if(!std::all_of(points.begin(),points.end(),[&](const auto& point){
       return point.x>=bounds.xmin-eps&&point.x<=bounds.xmax+eps&&point.y>=bounds.ymin-eps&&point.y<=bounds.ymax+eps;
      }))return false;
     for(std::size_t segment=0;segment<values.size();++segment)
      if(values[segment]<=eps||std::abs(stations[segment+1]-stations[segment]-values[segment])>eps)return false;
     return true;
    };
    for(std::size_t panel=0;panel<config.panels.size();++panel)if(belongsToInterior(panel)){
     verifiedInteriors.push_back(handle);
     return;
    }
   }
   Side side{-1,-1};
   if(expectedRole!=roles.end())side=expectedRole->second.first;
   else{
    double nearest=std::numeric_limits<double>::infinity();
    for(std::size_t panel=0;panel<config.panels.size();++panel){
     const auto& bounds=config.panels[panel];
     const double low=*vertical?bounds.ymin:bounds.xmin,high=*vertical?bounds.ymax:bounds.xmax;
     if(stations.back()<low-eps||stations.front()>high+eps)continue;
     for(int candidate=*vertical?0:2;candidate<(*vertical?2:4);++candidate){
      const double distance=(candidate%2?1:-1)*(normal(baseline)-productPlacementDatum(config,static_cast<int>(panel),candidate));
      if(distance<=eps||distance>80*config.scale/config.unitMM||distance>=nearest)continue;
      nearest=distance;
      side={static_cast<int>(panel),candidate};
     }
    }
   }
   if(side.first>=0)sides[side].push_back({handle,owner,std::move(stations),std::move(values),std::move(points),normal(baseline),native,readable});
  }catch(const std::exception&){unreadable.push_back(handle);}
 };
 for(const auto& id:wholeModelIds(db))visit(visit,id,AcGeMatrix3d::kIdentity,{},{},0);
 std::ostringstream report;
 report<<std::setprecision(17)<<"{\"savedReopened\":true,\"sourceSHA256\":\""<<job.sourceHash
       <<"\",\"logicalAxisLayer\":"<<jsonUtf8(config.logicalAxisLayer)<<",\"sides\":[";
 bool firstSide=true;
 for(const auto& [side,expected]:config.expectedPerimeter){
  const auto [panel,index]=side;
  const auto& chains=sides[side];
  const auto same=[&](const auto& first,const auto& second){
   return first.size()==second.size()&&std::equal(first.begin(),first.end(),second.begin(),
     [&](double a,double b){return std::abs(a-b)<=eps;});
  };
  const auto closing=expected.stations.size()>=2;
  std::vector<double> boundary;
  if(closing)boundary={expected.stations.front(),expected.stations.back()};
  std::array<std::string,3> selected;
  std::array<bool,3> pass{};
  std::vector<std::string> duplicates,unassigned,reviewedRetained,verifiedSupplemental;
  const auto& third=expected.openingStations;
  std::vector<double> missingAxisStations;
  for(const auto station:expected.stations){
   const auto lineGroup=expected.axisLines.find(station);
   const bool found=lineGroup!=expected.axisLines.end()&&
    std::any_of(lineGroup->second.begin(),lineGroup->second.end(),[&](const auto& handle){
     AcDbObjectId id;
     if(db.getAcDbObjectId(id,false,AcDbHandle(bcpproduct::decodeMessage(handle).c_str()))!=Acad::eOk||id.isErased())return false;
     const auto entity=open<AcDbEntity>(id);
     const auto* line=AcDbLine::cast(entity.get());
     if(!line||!productPlanDimensionVisible(*entity)||symbolName(entity->layerId())!=config.logicalAxisLayer)return false;
     const auto measure=[&](const auto& p){return index<2?p.y:p.x;};
     return std::abs(measure(line->startPoint())-station)<=eps&&std::abs(measure(line->endPoint())-station)<=eps;
    });
   if(!found)missingAxisStations.push_back(station);
  }
  for(const auto& chain:chains){
   const auto role=roles.find(chain.handle);
   if(role==roles.end()){
    if((closing&&(same(chain.stations,boundary)||same(chain.stations,expected.stations)))||
       (!third.empty()&&same(chain.stations,third)))duplicates.push_back(chain.handle);
    else unassigned.push_back(chain.handle);
    continue;
   }
   const int row=role->second.second;
   if(row<1||row>3)continue;
   const auto& target=row==1?boundary:row==2?expected.stations:third;
   const double sum=std::accumulate(chain.values.begin(),chain.values.end(),0.);
   bool values=target.size()>=2&&chain.values.size()+1==target.size()&&
       std::abs(sum-(target.back()-target.front()))<=eps;
   if(values)for(std::size_t segment=0;segment<chain.values.size();++segment)
    values&=std::abs(chain.values[segment]-(target[segment+1]-target[segment]))<=eps;
   const bool exact=values&&same(chain.stations,target)&&chain.native&&chain.visibleNumbers;
   if(selected[row-1].empty()){selected[row-1]=chain.handle;pass[row-1]=exact;}
   else duplicates.push_back(chain.handle);
  }
  std::ostringstream retainedSegments;
  bool firstRetained=true;
  for(const auto& handle:unassigned){
   const auto source=job.sourceDimensions.find(handle);
   if(source==job.sourceDimensions.end()||!source->second.readable)continue;
   const auto actual=std::find_if(chains.begin(),chains.end(),[&](const auto& row){return row.handle==handle;});
   const auto& before=source->second;
   if(actual==chains.end()||actual->points.size()!=before.points.size()||
      actual->values.size()!=before.values.size())continue;
   const bool reverse=before.points.front().distanceTo(actual->points.back())<=eps;
   bool retained=actual->values.size()+1==actual->points.size();
   for(std::size_t i=0;i<before.points.size();++i)
    retained&=before.points[i].distanceTo(actual->points[reverse?before.points.size()-1-i:i])<=eps;
   for(std::size_t i=0;i<before.values.size();++i)
    retained&=std::abs(before.values[i]-actual->values[reverse?before.values.size()-1-i:i])<=eps;
   if(!retained)continue;
   reviewedRetained.push_back(handle);
   const auto& panelBounds=config.panels.at(panel);
   const auto band=80*config.scale/config.unitMM;
   const auto evidence=[&](std::size_t indexAt){
    std::set<std::string> matches;
    const auto& point=actual->points[indexAt];
    for(const auto& witness:sourceWitnesses){
     if(witness.exactDefinitionOnly&&witness.point.distanceTo(point)>eps)continue;
     if(witness.point.x<panelBounds.xmin-band||witness.point.x>panelBounds.xmax+band||
        witness.point.y<panelBounds.ymin-band||witness.point.y>panelBounds.ymax+band)continue;
     const auto station=index<2?witness.point.y:witness.point.x;
     const auto normal=index<2?witness.point.x:witness.point.y;
     const auto dimensionOrigin=index<2?point.x:point.y;
     if(std::abs(station-actual->stations[indexAt])<=eps&&std::abs(normal-dimensionOrigin)<=30*config.scale/config.unitMM)
      matches.insert(witness.handle);
    }
    return std::vector<std::string>(matches.begin(),matches.end());
   };
   std::vector<std::vector<std::string>> witnesses;
   for(std::size_t i=0;i<actual->points.size();++i)witnesses.push_back(evidence(i));
   const bool supplemental=actual->visibleNumbers&&before.automaticText&&
    std::all_of(witnesses.begin(),witnesses.end(),[](const auto& handles){return !handles.empty();});
   if(supplemental)verifiedSupplemental.push_back(handle);
   for(std::size_t i=0;i<actual->values.size();++i){
    if(!firstRetained)retainedSegments<<',';
    firstRetained=false;
    retainedSegments<<std::setprecision(17)<<"{\"source\":\""<<handle<<"\",\"points\":"
      <<wholePoints({actual->points[i],actual->points[i+1]})<<",\"value\":"<<actual->values[i]<<",\"matches\":[";
    bool firstMatch=true;
    for(const auto& target:chains){
     if(std::find(selected.begin(),selected.end(),target.handle)==selected.end())continue;
     for(std::size_t j=0;j<target.values.size();++j){
      if(std::abs(actual->values[i]-target.values[j])>eps||
         std::abs(actual->stations[i]-target.stations[j])>eps||
         std::abs(actual->stations[i+1]-target.stations[j+1])>eps)continue;
      if(!firstMatch)retainedSegments<<',';
      firstMatch=false;
      const bool exact=actual->points[i].distanceTo(target.points[j])<=eps&&
         actual->points[i+1].distanceTo(target.points[j+1])<=eps;
      retainedSegments<<"{\"target\":\""<<target.handle<<"\",\"segment\":"<<j+1
         <<",\"sameDefinitionPoints\":"<<(exact?"true":"false")<<'}';
     }
    }
    retainedSegments<<"],\"sourcePointWitnesses\":[";
    productWriteHandleArray(retainedSegments,witnesses[i]);
    retainedSegments<<',';
    productWriteHandleArray(retainedSegments,witnesses[i+1]);
    retainedSegments<<"],\"disposition\":\""<<(supplemental?"retained-source-construction-measurement":"unresolved-source-measurement")<<"\"}";
   }
  }
  const bool axisPass=closing&&pass[0]&&pass[1]&&duplicates.empty()&&missingAxisStations.empty();
  const bool thirdOmitted=!expected.openingFreeWalls.empty()&&selected[2].empty();
  const bool openingPass=(expected.openingScopeKnown&&pass[2])||thirdOmitted;
  std::vector<std::string> reasons;
  if(!closing)reasons.push_back(u8"本侧不足两个有据定位轴");
  if(closing&&!pass[0])reasons.push_back(u8"第一道总轴距、首末站或显示值不合格");
  if(closing&&!pass[1])reasons.push_back(u8"第二道分段测站、单段值、总和或显示值不合格");
  if(!missingAxisStations.empty())reasons.push_back(u8"部分测站缺对应的可见真实轴线");
  if(!expected.axisScopeKnown)reasons.push_back(expected.axisScopeReason.empty()?
    u8"投影或退台范围仍需核对":expected.axisScopeReason);
  if(!openingPass)reasons.push_back(u8"第三道洞口链或无洞口省标依据待核");
  if(!duplicates.empty())reasons.push_back(u8"仍有重复链候选");
  const bool mainChainsVerified=axisPass&&openingPass&&expected.axisScopeKnown;
  const bool otherDimensionsVerified=unassigned.size()==verifiedSupplemental.size();
  if(!otherDimensionsVerified)reasons.push_back(u8"本侧仍有保留尺寸未从源构件端点或边线取得完整测站依据");
  const auto core=[&]{
   std::ostringstream row;
   row<<std::setprecision(17)<<"{\"panel\":"<<panel+1<<",\"side\":"<<index
      <<",\"savedReopened\":true,\"report\":\"perimeter-source-reconciliation.json\",\"sourceAxisStations\":";
   productWriteStationArray(row,expected.stations);
   row<<",\"sourceGeometry\":";
   productWriteHandleArray(row,expected.sourceObjects);
   row<<",\"sourceOpenings\":";
   productWriteHandleArray(row,expected.openings);
   row<<",\"sourceOpeningFreeFacadeWalls\":";
   productWriteHandleArray(row,expected.openingFreeWalls);
   row<<",\"expectedThirdStations\":";
   productWriteStationArray(row,third);
   row<<",\"selectedChains\":";
   productWriteHandleArray(row,std::vector<std::string>(selected.begin(),selected.end()));
   row<<",\"duplicateCandidates\":";
   productWriteHandleArray(row,duplicates);
   row<<",\"otherSideDimensions\":";
   productWriteHandleArray(row,unassigned);
   row<<",\"preservedSourceChains\":";
   productWriteHandleArray(row,reviewedRetained);
   row<<",\"verifiedSupplementalChains\":";
   productWriteHandleArray(row,verifiedSupplemental);
   row<<",\"preservedSourceSegments\":["<<retainedSegments.str()<<']';
   row<<",\"missingAxisStations\":";
   productWriteStationArray(row,missingAxisStations);
   row<<",\"rowChecks\":["<<(pass[0]?"true":"false")<<','<<(pass[1]?"true":"false")<<','
      <<(pass[2]?"true":"false")<<"],\"thirdOmittedWithEvidence\":"<<(thirdOmitted?"true":"false")
      <<",\"mainChainsVerified\":"<<(mainChainsVerified?"true":"false")<<",\"reasons\":[";
   for(std::size_t i=0;i<reasons.size();++i){
    if(i)row<<',';
    row<<jsonUtf8(bcpproduct::decodeMessage(reasons[i]));
   }
   row<<']';
   return row.str();
  }();
  if(!firstSide)report<<',';
  firstSide=false;
  const bool complete=mainChainsVerified&&otherDimensionsVerified;
  std::vector<std::string> removedMainChains;
  for(auto& [handle,old]:job.oldPerimeterDimensions){
   if(old.panel!=panel||old.side!=index||old.replacement.empty())continue;
   old.verified=mainChainsVerified&&std::all_of(old.replacement.begin(),old.replacement.end(),[&](const auto& target){
    return std::find(selected.begin(),selected.end(),target)!=selected.end();
   });
   removedMainChains.push_back(handle);
  }
  auto output=std::vector<std::string>(selected.begin(),selected.end());
  output.erase(std::remove(output.begin(),output.end(),""),output.end());
  output.insert(output.end(),verifiedSupplemental.begin(),verifiedSupplemental.end());
  productCheckPlanSource(job,job.layoutSourcePanels.at(panel),"perimeter",std::string(1,"WESN"[index]),
   complete?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::pending,std::move(output),
   complete?"":u8"实际链、闭合、显示值、源测站或第三道适用范围未全部证实，见 perimeter-source-reconciliation.json");
  report<<core<<",\"removedSourceMainChains\":";
  productWriteHandleArray(report,removedMainChains);
  report<<",\"result\":\""<<(complete?"verified-source-station-and-visible-chain-reconciliation":"pending-scope-or-chain")<<"\"}";
  for(const auto* id:{"D01","D02","D03","D04","D11"})
   productRecordRuleEvidence(job,id,core+",\"result\":\""+
      (axisPass&&expected.axisScopeKnown?"verified-source-axis-stations-and-closure":"pending-source-axis-stations-or-chain")+"\"}");
  productRecordRuleEvidence(job,"D06",core+",\"result\":\""+
     (thirdOmitted?"verified-omission-on-opening-free-facade":
      openingPass?"verified-opening-bearing-side-retained":"pending-opening-free-segment-scope")+"\"}");
  productRecordRuleEvidence(job,"D27",core+",\"result\":\""+
     (complete?"verified-floor-perimeter-source-scope":"pending-eave-or-chain-scope")+"\"}");
  productRecordRuleEvidence(job,"QA10",core+",\"result\":\""+
     (complete?"verified-source-station-and-visible-chain-reconciliation":"pending-scope-or-chain")+"\"}");
  if(!complete)job.detailPending.push_back({u8"QA10 / 外围逐侧对账",std::to_string(panel+1)+"/"+std::to_string(index),
   u8"存在未独立核定的边段、保留尺寸或链源测站；参见 perimeter-source-reconciliation.json 的实际对象及应有清单。"});
 }
 report<<"],\"unreadableOrUnlocatedDimensions\":";
 productWriteHandleArray(report,unreadable);
 report<<",\"verifiedInteriorDimensions\":";
 productWriteHandleArray(report,verifiedInteriors);
 const auto allMasks=productMasks(db);
 std::vector<std::string> savedMasks,originalFurnitureMasks;
 for(const auto& handle:allMasks){
  if(job.currentPlanRules&&job.protectedFurnitureMembers.count(handle))originalFurnitureMasks.push_back(handle);
  else savedMasks.push_back(handle);
 }
 std::ostringstream masks;
 masks<<"{\"scope\":\"all-local-block-definitions-including-model-space\",\"sourceWipeouts\":";
 productWriteHandleArray(masks,sourceMasks);
 masks<<",\"reopenedWipeouts\":";
 productWriteHandleArray(masks,savedMasks);
 masks<<",\"preservedFurnitureMembers\":";productWriteHandleArray(masks,originalFurnitureMasks);
 masks<<",\"savedReopened\":true,\"report\":\"perimeter-source-reconciliation.json\",\"result\":\""
      <<(savedMasks.empty()?"verified-no-local-wipeouts":"pending-local-wipeouts")<<"\"}";
 productRecordRuleEvidence(job,"D19",masks.str());
 report<<",\"wipeouts\":"<<masks.str()<<'}';
 writeNew(job.root/L"perimeter-source-reconciliation.json",report.str());
}
