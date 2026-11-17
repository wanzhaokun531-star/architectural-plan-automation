// Template-authoritative final pass. Never use a project entity as a donor.
struct ProductTemplateObject {
 std::string source,kind,donor,issue;
 std::wstring axisNumber;
 std::vector<std::string> targets;
 std::vector<AcGePoint3d> stations,axisGrips;
 std::vector<double> values;
};
struct ProductTemplateRun {std::string templateHash,dimensionDefinition;std::vector<ProductTemplateObject> objects;std::vector<std::string> retiredHidden;};

AcDbObjectId productTemplateClone(AcDbDatabase& donor,AcDbObjectId source,AcDbDatabase& target){
 AcDbObjectIdArray ids;ids.append(source);AcDbIdMapping mapping;
 status(donor.wblockCloneObjects(ids,modelId(target),mapping,AcDb::kDrcReplace,false),"template wins native dependency import");
 AcDbIdPair pair;pair.setKey(source);require(mapping.compute(pair)&&pair.isCloned(),"template native clone mapping");return pair.value();
}

std::string productTemplateDimensionDefinition(AcDbEntity& e){
 TemplateParts parts(&e);status(e.explode(parts.values),"read actual template dimension segments");
 std::ostringstream out;out<<std::setprecision(12);std::string expected;
 for(int i=0;i<parts.values.length();++i){
  auto* d=AcDbAlignedDimension::cast(static_cast<AcRxObject*>(parts.values[i]));require(d,"true aligned native segment");
  const auto f=productReadDimensionFont(d->dimtxsty());
  out.str("");out.clear();out<<jsonUtf8(symbolName(d->dimensionStyle()))<<jsonUtf8(f.name)
   <<jsonUtf8(f.primary)<<jsonUtf8(f.big)<<f.width<<','<<f.oblique<<','<<f.flags<<','
   <<d->dimtxt()<<','<<d->dimscale()<<','<<d->dimgap()<<','<<d->dimexo()<<','<<d->dimexe()<<','
   <<d->dimdle()<<','<<d->dimlfac()<<','<<d->dimasz()<<','<<jsonUtf8(symbolName(d->dimblk()))
   <<','<<d->dimclrt().colorIndex()<<','<<d->dimtad()<<','<<d->dimdec();
  if(i==0)expected=out.str();else require(expected==out.str(),"mixed actual native segment definitions");
 }require(parts.values.length()>0,"native chain has no segments");return expected;
}

ProductTemplateRun productApplyNativeTemplate(ProductJob& job,AcDbDatabase& db){
 const auto path=job.profile/L"annotation-template/平面标注.dwg";
 ProductTemplateRun run;run.templateHash=sha(path);
 require(run.templateHash=="3C6B19FAE21C1C5729D0E0000104DCADD9BA7A81560A31D74F0D576B2BD0DD2F", "approved template fingerprint differs");
 require(std::abs(job.unifiedScale-100.)<1e-8&&std::abs(job.unifiedUnitMM-1.)<1e-8,"template native adaptation currently requires millimetres at 1:100");
 const auto sourceRows=productTypographyInventory(db);
 const auto sourceRoots=modelTypes(db);
 AcDbDatabase donor(false,true);status(donor.readDwgFile(path.c_str(),AcDbDatabase::kForReadAndAllShare,false),"read native template");status(donor.closeInput(true),"close template input");
 {AcDbObjectIdArray styles;const auto table=open<AcDbDimStyleTable>(donor.dimStyleTableId());
  for(const auto* name:{L"_TCH_ARCH",L"_TCH_ARCH&&100"}){AcDbObjectId id;status(table->getAt(name,id),"template base and ratio definition");styles.append(id);}
  AcDbIdMapping mapping;status(donor.wblockCloneObjects(styles,db.dimStyleTableId(),mapping,AcDb::kDrcReplace,false),"template base and ratio dependency precedence");}
 std::map<std::size_t,std::pair<AcDbObjectId,std::string>> dims;
 std::map<std::wstring,std::pair<AcDbObjectId,std::string>> axes;
 std::vector<AcDbObjectId> materials;
 std::ostringstream donorGeometry;donorGeometry<<"{\"dimensions\":[";bool firstDonor=true;
 for(const auto& [h,type]:modelTypes(donor))if(type==L"TDbDimension2"||type==L"TDbAxisLabelSet"){
  const auto handle=ascii(h);const auto original=find(donor,handle);
  const auto clone=productTemplateClone(donor,original,db);materials.push_back(clone);
  if(type==L"TDbDimension2"){
   const auto values=chainValues(clone);const auto g=chainGrips(clone);const auto n=values.size();
   if(!firstDonor)donorGeometry<<',';firstDonor=false;donorGeometry<<"{\"handle\":\""<<handle<<"\",\"segments\":"<<n<<",\"grips\":"<<wholePoints(g)<<'}';
   if(!dims.count(n)||g.size()==2*n+6)dims[n]={clone,handle};
  }
  else{std::vector<ProductRoomLeaf> leaves;productRoomLeaves(*open<AcDbEntity>(clone),leaves);
   if(chainGrips(clone).size()==4&&leaves.size()==1)axes.emplace(leaves.front().text,std::make_pair(clone,handle));}
  status(editObject<AcDbEntity>(clone)->setVisibility(AcDb::kInvisible),"hide template construction sample");
 }
 donorGeometry<<"]}";writeNew(job.root/L"template-donor-grips.json",donorGeometry.str());
 require(dims.count(1)&&!axes.empty(),"template native prototypes absent");
 const auto signature=productTemplateDimensionDefinition(*open<AcDbEntity>(dims.at(1).first));
 run.dimensionDefinition=signature;
 if(!acedRegCmds->lookupGlobalCmd(L"TMergeDim")){
  const auto module=fs::path(productSetting(L"tangentDirectory",L"C:\\Tangent\\TArchT20V8\\SYS23x64"))/L"tch_annotate.arx";
  require(acrxDynamicLinker->loadModule(module.c_str(),false,false),"load public native chain merge");
 }
 std::set<std::string> handled;
 for(const auto& row:sourceRows)if(row.visible&&handled.insert(row.handle).second&&
    (row.type==L"TDbDimension2"||row.type.find(L"Dimension")!=std::wstring::npos)){
  ProductTemplateObject item;item.source=row.handle;item.kind="dimension";
  const auto before=wholeModelIds(db);
  try{
   auto source=open<AcDbEntity>(find(db,row.handle));
   require(source->ownerId()==modelId(db),"nested dimension requires instance-isolated replacement");
   const auto geometry=productReadSourceDimension(db,row.handle,.01);
   require(geometry.readable&&geometry.automaticText&&!geometry.values.empty(),"unreadable stations or numerical override");
   item.stations=geometry.points;item.values=geometry.values;
   const auto count=item.values.size();require(item.stations.size()==count+1,"incomplete measurement stations");
   const auto oldGrips=chainGrips(source->objectId());const auto layer=symbolName(source->layerId());source.reset();
   const auto direction=(item.stations.back()-item.stations.front()).normal();
   const AcGeVector3d normal(-direction.y,direction.x,0);
   const auto offset=(geometry.baseline-item.stations.front()).dotProduct(normal);
   AcDbObjectId replacement;
   if(dims.count(count)){item.donor=dims.at(count).second;replacement=productDetailChain(db,dims.at(count).first,item.stations,offset);}
   else{item.donor=dims.at(1).second;replacement=productGenericChain(job,db,dims.at(1).first,item.stations,offset);}
   if(oldGrips.size()==2*count+6||oldGrips.size()==2*count+3)for(std::size_t k=0;k<count;++k)moveDimensionGrip(replacement,static_cast<int>(count+3+k),oldGrips[count+3+k]);
   {auto e=editObject<AcDbEntity>(replacement);status(e->setVisibility(AcDb::kVisible),"show template dimension");
    status(e->setLayer(productPlanLayer(db,layer==L"PUB_DIM"?L"PUB_DIM":L"A-AXIS",layer==L"PUB_DIM"?25:3)),"template dimension role layer");}
   const auto actual=productReadSourceDimension(db,axisHandle(replacement),.01);
   require(actual.readable&&actual.values.size()==count&&actual.points.size()==item.stations.size(),"replacement measurement count differs");
   for(std::size_t k=0;k<item.stations.size();++k)if(actual.points[k].distanceTo(item.stations[k])>=.01){
    writeNew(job.root/(L"template-station-mismatch-"+bcpproduct::decodeMessage(item.source)+L".json"),
      "{\"source\":"+wholePoints(item.stations)+",\"actual\":"+wholePoints(actual.points)+",\"grips\":"+wholePoints(chainGrips(replacement))+"}");
    require(false,"template stretch changed station");
   }
   for(std::size_t k=0;k<count;++k)require(std::abs(actual.values[k]-item.values[k])<.01,"template stretch changed measured value");
   require(productTemplateDimensionDefinition(*open<AcDbEntity>(replacement))==signature,"project style displaced native template definition");
   item.targets.push_back(axisHandle(replacement));status(editObject<AcDbEntity>(find(db,item.source))->erase(),"retire original dimension after verified native replacement");
  }catch(const std::exception& error){item.issue=error.what();item.targets.clear();for(auto id:wholeModelIds(db))if(!before.count(id))editObject<AcDbEntity>(id)->erase();}
  run.objects.push_back(std::move(item));
 }
 for(const auto& [h,type]:sourceRoots)if(type==L"TDbAxisLabelSet"){
  ProductTemplateObject item;item.source=ascii(h);item.kind="axis";const auto before=wholeModelIds(db);
  try{
   const auto original=find(db,item.source);auto entity=open<AcDbEntity>(original);if(!productPlanDimensionVisible(*entity))continue;
   std::vector<ProductRoomLeaf> leaves;productRoomLeaves(*entity,leaves);item.axisGrips=chainGrips(original);
   const auto& g=item.axisGrips;require((g.size()==4||g.size()==8)&&!leaves.empty(),"unrecognized native axis shape");
   require(std::all_of(leaves.begin(),leaves.end(),[&](const auto& x){return x.text==leaves.front().text;}),"axis end numbers differ");
   item.axisNumber=leaves.front().text;auto material=axes.find(item.axisNumber);
   if(material==axes.end())material=axes.find(L"A");require(material!=axes.end(),"template native axis donor absent");
   item.donor=material->second.second;entity.reset();
   for(std::size_t k=0;k<g.size();k+=4){
    auto id=productCloneAxisLabel(db,material->second.first);auto pg=chainGrips(id);
    if(material->first!=item.axisNumber){
     status(editObject<AcDbEntity>(id)->setVisibility(AcDb::kVisible),"show template axis for public renumber");
     ads_name selected{};status(acdbGetAdsName(selected,id),"select exact cloned axis");wholeStagingView({pg[1]});
     require(acedCommandS(RTSTR,L"TReAxisNo",RTENAME,selected,RTSTR,item.axisNumber.c_str(),RTSTR,L"",RTNONE)==RTNORM,"renumber actual template axis");
     std::vector<ProductRoomLeaf> actual;productRoomLeaves(*open<AcDbEntity>(id),actual);
     require(actual.size()==1&&actual.front().text==item.axisNumber,"native template axis business number differs");
     const auto numbered=chainGrips(id);require(numbered.size()==pg.size()&&numbered[0].distanceTo(pg[0])<.01&&numbered[1].distanceTo(pg[1])<.01,"renumber moved template axis geometry");
    }
    const auto a=pg[0]-pg[1],b=g[k]-g[k+1];
    const auto angle=std::atan2(b.y,b.x)-std::atan2(a.y,a.x);
    {auto e=editObject<AcDbEntity>(id);status(e->transformBy(AcGeMatrix3d::rotation(angle,AcGeVector3d::kZAxis,pg[1])),"orient template native axis");}
    pg=chainGrips(id);{auto e=editObject<AcDbEntity>(id);status(e->transformBy(AcGeMatrix3d::translation(g[k+1]-pg[1])),"position template axis circle");status(e->setVisibility(AcDb::kVisible),"show template axis");}
    pg=chainGrips(id);if(pg[0].distanceTo(g[k])>.01){const auto stretched=productAxisLabelWithTail(db,id,g[k]);status(editObject<AcDbEntity>(id)->erase(),"retire unstretched axis trial");id=stretched;}
    pg=chainGrips(id);require(pg.size()==4&&pg[0].distanceTo(g[k])<.01&&pg[1].distanceTo(g[k+1])<.01,"template axis moved true tail or circle");
    status(editObject<AcDbEntity>(id)->setLayer(productPlanLayer(db,L"A-AXIS",3)),"template axis role layer");item.targets.push_back(axisHandle(id));
   }
   status(editObject<AcDbEntity>(original)->erase(),"retire old axis after template replacement");
  }catch(const std::exception& error){item.issue=error.what();item.targets.clear();for(auto id:wholeModelIds(db))if(!before.count(id))editObject<AcDbEntity>(id)->erase();}
  run.objects.push_back(std::move(item));
 }
 for(const auto id:materials)status(editObject<AcDbEntity>(id)->erase(),"retire template construction material");
 // Old invisible construction copies are not reusable template resources.
 for(const auto& row:sourceRows)if(!row.visible&&row.type==L"TDbDimension2"&&row.handle==row.root){
  auto entity=editObject<AcDbEntity>(find(db,row.handle));if(!entity->isErased()){
   status(entity->erase(),"retire hidden source dimension");run.retiredHidden.push_back(row.handle);}
 }
 AcDbObjectIdArray unused;
 {const auto table=open<AcDbDimStyleTable>(db.dimStyleTableId());AcDbDimStyleTableIterator* raw{};status(table->newIterator(raw),"retire old dimension styles");
  std::unique_ptr<AcDbDimStyleTableIterator> it(raw);for(;!it->done();it->step()){
   AcDbObjectId id;status(it->getRecordId(id),"old dimension style id");const auto name=symbolName(id);
   if(name!=L"Standard"&&name!=L"_TCH_ARCH"&&name!=L"_TCH_ARCH&&100")unused.append(id);
  }}
 status(db.purge(unused),"find unreferenced source dimension styles");
 for(int k=0;k<unused.length();++k)status(editObject<AcDbDimStyleTableRecord>(unused[k])->erase(),"remove unreferenced source dimension style");
 return run;
}
