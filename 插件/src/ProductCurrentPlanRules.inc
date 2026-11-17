// Shared finalization runs before publication on every supported product route.
void productExposeNativeAxes(ProductJob& job,AcDbDatabase& db){
 std::vector<AcDbObjectId> wrappers;
 for(const auto& item:modelTypes(db))if(item.second==L"AcDbBlockReference"){const auto id=find(db,ascii(item.first));auto e=open<AcDbBlockReference>(id);if(symbolName(e->blockTableRecord()).rfind(L"BCP_AXIS_",0)==0)wrappers.push_back(id);}
 for(auto id:wrappers){
  auto ref=open<AcDbBlockReference>(id);const auto matrix=ref->blockTransform();std::optional<AcGePoint3d> keep;
  AcDbSpatialFilter* filterRaw{};std::unique_ptr<AcDbSpatialFilter,Close<AcDbSpatialFilter>> filter;
  if(!ref->extensionDictionary().isNull()){
   auto d=open<AcDbDictionary>(ref->extensionDictionary());AcDbObjectId filters,spatial;
   if(d->getAt(L"ACAD_FILTER",filters)==Acad::eOk){auto f=open<AcDbDictionary>(filters);if(f->getAt(L"SPATIAL",spatial)==Acad::eOk&&acdbOpenObject(filterRaw,spatial,AcDb::kForRead)==Acad::eOk)filter.reset(filterRaw);}
  }
  auto block=open<AcDbBlockTableRecord>(ref->blockTableRecord());AcDbBlockTableRecordIterator* raw{};productStatus(block->newIterator(raw),"read native axis wrapper contents");std::unique_ptr<AcDbBlockTableRecordIterator> it(raw);
  std::vector<std::unique_ptr<AcDbEntity>> clones;
  for(;!it->done();it->step()){AcDbObjectId source;productStatus(it->getEntityId(source),"native axis wrapper member");auto e=open<AcDbEntity>(source);if(std::wstring(e->isA()->name())==L"TDbAxisLabelSet"&&filter){const auto g=chainGrips(source);AcGePoint2dArray p;AcGeVector3d n;double z{},front{},back{};Adesk::Boolean on=false;productStatus(filter->getDefinition(p,n,z,front,back,on),"axis side clipping definition");if(g.size()==8&&p.length()==2&&on){const auto inside=[&](const auto& c){const bool contained=c.x>=std::min(p[0].x,p[1].x)&&c.x<=std::max(p[0].x,p[1].x)&&c.y>=std::min(p[0].y,p[1].y)&&c.y<=std::max(p[0].y,p[1].y);return filter->isInverted()?!contained:contained;};const bool low=inside(g[1]),high=inside(g[5]);if(low!=high){auto selected=low?g[1]:g[5];keep=selected.transformBy(matrix);}}}
   auto* entity=AcDbEntity::cast(e->clone());require(entity!=nullptr,"clone native axis object");clones.emplace_back(entity);productStatus(entity->transformBy(matrix),"native axis wrapper world transform");
  }
  block.reset();ref.reset();filter.reset();AcDbObjectId native;
  for(auto& e:clones){auto model=editObject<AcDbBlockTableRecord>(modelId(db));AcDbObjectId added;const bool axis=std::wstring(e->isA()->name())==L"TDbAxisLabelSet";productStatus(model->appendAcDbEntity(added,e.get()),"expose native editable axis");e.release()->close();if(axis)native=added;}
  require(!native.isNull(),"axis wrapper contained no native axis");{auto e=editObject<AcDbEntity>(id);productStatus(e->erase(),"remove obsolete ordinary axis container");}
  if(keep){bool matched=false;for(int attempt=0;attempt<4;++attempt){const auto g=chainGrips(native);if(g.size()==4&&g[1].distanceTo(*keep)<1e-4){matched=true;break;}require(g.size()==4||g.size()==8,"native axis side edit grips");job.axisSideCommandUsed=true;approvedCommandPoint(L"TObjEdit",g[1]+(std::abs(g[1].x-g[0].x)<1e-5?AcGeVector3d(400,0,0):AcGeVector3d(0,400,0)),L"M");}require(matched,"native axis visibility was not preserved");}
 }
 std::vector<AcDbObjectId> groups;{auto d=open<AcDbDictionary>(db.groupDictionaryId());std::unique_ptr<AcDbDictionaryIterator> it(d->newIterator());for(;!it->done();it->next())if(std::wstring(it->name()).rfind(L"BCP_GRID_",0)==0)groups.push_back(it->objectId());}
 for(auto id:groups){auto g=editObject<AcDbGroup>(id);productStatus(g->clear(),"release directly stretchable axis lines from ordinary groups");}
 for(const auto& item:modelTypes(db))if(item.second==L"AcDbLine"){auto e=editObject<AcDbEntity>(find(db,ascii(item.first)));if(symbolName(e->layerId())==L"A-DOTE")productStatus(e->setLayer(productPlanLayer(db,L"DOTE",24)),"current independent axis layer");}
}
ProductPlanLayoutConfig productDiscoverPlanLayout(ProductJob& job,AcDbDatabase& db){
 ProductPlanLayoutConfig c;c.projectAssociations=false;
 if(job.threeLayout){
  auto configured=productReadPlanLayout(job.profile/L"plan-policy/plan-layout.tsv");
  c.scale=configured.scale;
  c.unitMM=configured.unitMM;
  c.panels=std::move(configured.panels);
 }
 else{
  const auto service=productAxisServices(db,{-1e90,-1e90,1e90,1e90});ProductPlanBounds p{1e100,1e100,-1e100,-1e100};
  for(const auto& w:service.walls){if(w.vertical){p.xmin=std::min(p.xmin,w.coordinate);p.xmax=std::max(p.xmax,w.coordinate);p.ymin=std::min(p.ymin,w.lower);p.ymax=std::max(p.ymax,w.upper);}else{p.ymin=std::min(p.ymin,w.coordinate);p.ymax=std::max(p.ymax,w.coordinate);p.xmin=std::min(p.xmin,w.lower);p.xmax=std::max(p.xmax,w.upper);}}
  for(const auto& col:service.columns){p.xmin=std::min(p.xmin,col.x);p.xmax=std::max(p.xmax,col.x);p.ymin=std::min(p.ymin,col.y);p.ymax=std::max(p.ymax,col.y);}
  require(p.xmin<p.xmax&&p.ymin<p.ymax,"actual wall/column axes for common plan rules");c.panels.push_back(p);
 }
 const auto panelFor=[&](AcGePoint3d p){int index=0;double best=1e100;for(std::size_t i=0;i<c.panels.size();++i){const auto& b=c.panels[i];const double dx=std::max({b.xmin-p.x,0.,p.x-b.xmax}),dy=std::max({b.ymin-p.y,0.,p.y-b.ymax});if(dx*dx+dy*dy<best){best=dx*dx+dy*dy;index=static_cast<int>(i);}}return index;};
 std::map<std::pair<int,int>,std::vector<std::pair<double,ProductPlanRole>>> dimensions;
 std::map<int,std::vector<std::pair<double,std::string>>> frames;
 for(const auto& item:modelTypes(db)){
  const auto h=ascii(item.first);const auto id=find(db,h);auto e=open<AcDbEntity>(id);if(e->visibility()!=AcDb::kVisible)continue;
  if(item.second==L"TDbAxisLabelSet"){
   const auto g=chainGrips(id);if(g.size()!=4&&g.size()!=8)continue;const int panel=panelFor(g[0]);const bool vertical=std::abs(g[1].x-g[0].x)<1e-5;c.axes.push_back({h,panel,vertical?1:0,0});
  }else if(item.second==L"TDbDimension2"){
   const auto g=chainGrips(id);const auto n=chainValues(id).size();if(g.size()!=2*n+6||!n)continue;
   const auto mid=g[2]+(g[n+2]-g[2])*0.5;const int panel=panelFor(mid);const auto& p=c.panels.at(panel);const bool vertical=std::abs(g[2].x-g[n+2].x)<1e-5;
   int side=-1;double distance=0;if(vertical&&g[0].x<p.xmin){side=0;distance=p.xmin-g[0].x;}else if(vertical&&g[0].x>p.xmax){side=1;distance=g[0].x-p.xmax;}else if(!vertical&&g[0].y<p.ymin){side=2;distance=p.ymin-g[0].y;}else if(!vertical&&g[0].y>p.ymax){side=3;distance=g[0].y-p.ymax;}
   if(side>=0)dimensions[{panel,side}].push_back({distance,{h,panel,side,0}});
  }else if(item.second==L"AcDbPolyline"&&symbolName(e->layerId())==L"Defpoints"){
   auto* line=AcDbPolyline::cast(e.get());if(!line||!line->isClosed()||line->numVerts()!=4)continue;AcDbExtents b;if(e->getGeomExtents(b)!=Acad::eOk)continue;const auto mid=b.minPoint()+(b.maxPoint()-b.minPoint())*0.5;frames[panelFor(mid)].push_back({(b.maxPoint().x-b.minPoint().x)*(b.maxPoint().y-b.minPoint().y),h});
  }
 }
 for(auto& group:dimensions){auto& rows=group.second;std::sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){return a.first>b.first;});double previous=-1;int row=0;for(auto& item:rows){if(previous<0||std::abs(previous-item.first)>1e-4){++row;previous=item.first;}item.second.row=std::min(row,3);c.dimensions.push_back(item.second);}}
 for(std::size_t panel=0;panel<c.panels.size();++panel){auto& old=frames[static_cast<int>(panel)];std::sort(old.begin(),old.end());
  if(old.size()==4){for(int row=0;row<4;++row)c.frames.push_back({old[row].second,static_cast<int>(panel),0,row});}
  else for(int row=0;row<4;++row){auto frame=std::make_unique<AcDbPolyline>(4);frame->setDatabaseDefaults(&db);for(int k=0;k<4;++k)frame->addVertexAt(k,AcGePoint2d(k%2,k/2));frame->setClosed(true);auto model=editObject<AcDbBlockTableRecord>(modelId(db));AcDbObjectId id;productStatus(model->appendAcDbEntity(id,frame.get()),"append common plan auxiliary frame");frame.release()->close();c.frames.push_back({axisHandle(id),static_cast<int>(panel),0,row});}
 }
 return c;
}
void productTrimExteriorLegs(ProductJob& job,AcDbDatabase& db,ProductPlanLayoutConfig& config){
 std::vector<ProductPlanRole> kept;
 for(auto role:config.dimensions){
  const auto id=find(db,role.handle);const auto g=chainGrips(id);const auto n=chainValues(id).size();const bool vertical=role.side<2;const auto& panel=config.panels.at(role.panel);const double lo=vertical?panel.ymin:panel.xmin,hi=vertical?panel.ymax:panel.xmax;
  std::vector<AcGePoint3d> stations;for(std::size_t k=2;k<=n+2;++k){auto p=g[k];if(vertical)p.y=std::max(lo,std::min(hi,p.y));else p.x=std::max(lo,std::min(hi,p.x));if(stations.empty()||stations.back().distanceTo(p)>1e-5)stations.push_back(p);}
  bool changed=stations.size()!=n+1;if(!changed)for(std::size_t k=0;k<stations.size();++k)changed=changed||stations[k].distanceTo(g[k+2])>1e-5;
  if(!changed){kept.push_back(role);continue;}
  if(stations.size()<2){auto e=editObject<AcDbEntity>(id);productStatus(e->erase(),"remove external axis-to-wall-edge end leg");continue;}
  if(stations.size()==n+1){for(std::size_t k=0;k<stations.size();++k)moveDimensionGrip(id,static_cast<int>(k+2),stations[k]);kept.push_back(role);continue;}
  require(curDoc()&&curDoc()->database()==&db,"native chain rebuilding requires owned work document");AcDbObjectId prototype;
  const auto oldLayer=[&](){auto e=open<AcDbEntity>(id);return e->layerId();}();
  for(const auto& item:modelTypes(db))if(item.second==L"TDbDimension2"&&chainValues(find(db,ascii(item.first))).size()==1){const auto candidate=find(db,ascii(item.first));auto e=open<AcDbEntity>(candidate);if(prototype.isNull()||e->layerId()==oldLayer)prototype=candidate;if(e->layerId()==oldLayer)break;}
  require(!prototype.isNull(),"native one-segment prototype for perimeter chain");
  const auto direction=(stations.back()-stations.front()).normal();const AcGeVector3d normal(-direction.y,direction.x,0);const double baseline=(g[0]-stations.front()).dotProduct(normal);
  std::vector<AcDbObjectId> pieces;for(std::size_t k=1;k<stations.size();++k)pieces.push_back(productDetailChain(db,prototype,{stations[k-1],stations[k]},baseline));
  if(!acedRegCmds->lookupGlobalCmd(L"TMergeDim")){const auto module=fs::path(productSetting(L"tangentDirectory",L"C:\\Tangent\\TArchT20V8\\SYS23x64"))/L"tch_annotate.arx";require(acrxDynamicLinker->loadModule(module.c_str(),false,false),"load authorized dimension connection");}
  auto result=pieces.front();for(std::size_t k=1;k<pieces.size();++k){const auto before=wholeModelIds(db);WholeSelection a(result),b(pieces[k]);require(acedCommandS(RTSTR,L"TMergeDim",RTPICKS,a.value,RTPICKS,b.value,RTSTR,L"",RTNONE)==RTNORM,"reconnect trimmed real perimeter chain");const auto after=wholeModelIds(db);std::vector<AcDbObjectId> added;for(auto p:after)if(!before.count(p)||p==result||p==pieces[k])added.push_back(p);require(after.size()+1==before.size()&&added.size()==1,"trimmed chain did not reconnect as one native object");result=added.front();}
  require(chainValues(result).size()==stations.size()-1,"trimmed native chain membership");{auto e=editObject<AcDbEntity>(id);productStatus(e->erase(),"replace perimeter chain without outer edge legs");}role.handle=axisHandle(result);kept.push_back(role);job.dimensionMergeCommandUsed=true;
 }
 config.dimensions=std::move(kept);
}
void productPlanFrameAppearance(AcDbDatabase& db,const ProductPlanLayoutConfig& layout){
 // Real visible dash definition, independent from the global drawing scale.
 AcDbObjectId dash;{auto table=open<AcDbLinetypeTable>(db.linetypeTableId());if(table->getAt(L"BCP_DEFP_DASH",dash)!=Acad::eOk){table.reset();auto t=editObject<AcDbLinetypeTable>(db.linetypeTableId());auto line=std::make_unique<AcDbLinetypeTableRecord>();line->setName(L"BCP_DEFP_DASH");line->setNumDashes(2);line->setDashLengthAt(0,6);line->setDashLengthAt(1,-3);line->setPatternLength(9);productStatus(t->add(dash,line.get()),"create real nonplot dash");line.release()->close();}}
 {auto line=editObject<AcDbLinetypeTableRecord>(dash);line->setNumDashes(2);line->setDashLengthAt(0,6);line->setDashLengthAt(1,-3);line->setPatternLength(9);}
 const auto layerId=productPlanLayer(db,L"Defpoints",8,false);{auto l=editObject<AcDbLayerTableRecord>(layerId);productStatus(l->setLinetypeObjectId(dash),"nonplot frame layer dash");}
 for(const auto& r:layout.frames){auto e=editObject<AcDbEntity>(find(db,r.handle));productStatus(e->setLinetype(dash),"actual frame dash");productStatus(e->setLinetypeScale(layout.scale/layout.unitMM/db.ltscale()),"paper frame dash scale");}
}
void productApplyCurrentPlanRules(ProductJob& job,AcDbDatabase& db){
 Working context(&db);productExposeNativeAxes(job,db);auto layout=job.latestThreeModel?productReadPlanLayout(job.profile/L"plan-policy/plan-layout.tsv"):productDiscoverPlanLayout(job,db);
 layout.paper=productLoadPlanPaperPolicy(job,layout.scale,layout.unitMM);
 productTrimExteriorLegs(job,db,layout);
 if(!job.latestThreeModel)productApplyPlanDimensionStyle(job,db,layout.scale,layout.unitMM);
 productResolvePlanPlacement(job,db,layout);
 productPlanLayout(db,layout,[&](const auto& h){return find(db,h);},job.root/L"外围间距核验.json");
 productContinuousMainAxes(job,db,layout);
 productPlanFrameAppearance(db,layout);
 writeNew(job.root/L"规则执行入口.json",std::string("{\"version\":\"0.2.2\",\"commonPlanFinalization\":true,\"latestModelRoute\":")+(job.latestThreeModel?"true":"false")+",\"axisRangeInference\":\"current native wall/column geometry plus reviewed overrides\",\"allRulesUniversallyImplemented\":false}");
 if(!job.latestThreeModel)approvedTitleClearance(job,db);
 // Counts must describe the current final chains after end-leg removal/rejoin,
 // rather than the foundation's pre-revision inventory.
 job.chains=0;job.dimensions=0;
 for(const auto& item:modelTypes(db))if(item.second==L"TDbDimension2"){
  const auto id=find(db,ascii(item.first));auto e=open<AcDbEntity>(id);auto layer=open<AcDbLayerTableRecord>(e->layerId());
  if(e->visibility()!=AcDb::kVisible||layer->isOff()||layer->isFrozen())continue;
  ++job.chains;job.dimensions+=static_cast<int>(chainValues(id).size());
 }
}
