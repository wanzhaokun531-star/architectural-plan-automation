struct ProductWallHeight {
 std::size_t panel{};
 std::string handle, issue;
 double original{}, base{};
 std::optional<std::pair<double,double>> thickness;
 std::optional<double> target;
 bool changed{};
};

const wchar_t* productPlanLevelRecordName=L"PLAN_DRAW_STOREY_LEVELS";

void productStorePlanLevels(AcDbDatabase& db,const std::vector<bcpproduct::PlanLevelResult>& levels,
 const std::vector<bool>& generated){
 AcDbDictionary* raw{};
 status(db.getNamedObjectsDictionary(raw,AcDb::kForWrite),"open drawing dictionary for storey data");
 std::unique_ptr<AcDbDictionary,Close<AcDbDictionary>> dictionary(raw);
 AcDbObjectId id;
 if(dictionary->getAt(productPlanLevelRecordName,id)==Acad::eOk){
  auto prior=editObject<AcDbXrecord>(id);
  status(prior->erase(),"replace prior PLAN_DRAW storey record");
 }
 auto record=std::make_unique<AcDbXrecord>();
 AcDbObjectId added;
 status(dictionary->setAt(productPlanLevelRecordName,record.get(),added),"register PLAN_DRAW storey data");
 auto* resident=record.release();
 std::unique_ptr<AcDbXrecord,Close<AcDbXrecord>> owned(resident);
 std::ostringstream table;
 table.imbue(std::locale::classic());
 table<<std::setprecision(17);
 for(std::size_t i=0;i<levels.size();++i)if(generated[i]&&levels[i].heightMM){
  const auto& row=levels[i];
  table<<i+1<<' '<<*row.heightMM<<' '<<row.heightBasis<<' ';
  if(row.finishedMetres)table<<*row.finishedMetres;
  else table<<'-';
  table<<'\n';
 }
 const auto value=bcpproduct::decodeMessage(table.str());
 resbuf data{};
 data.restype=AcDb::kDxfText;
 data.resval.rstring=const_cast<wchar_t*>(value.c_str());
 status(owned->setFromRbChain(data),"save independent storey and floor values in DWG");
}

std::map<std::size_t,std::pair<double,std::string>> productReadPlanLevels(AcDbDatabase& db){
 AcDbDictionary* raw{};
 status(db.getNamedObjectsDictionary(raw,AcDb::kForRead),"open saved storey dictionary");
 std::unique_ptr<AcDbDictionary,Close<AcDbDictionary>> dictionary(raw);
 AcDbObjectId id;
 if(dictionary->getAt(productPlanLevelRecordName,id)!=Acad::eOk)return {};
 resbuf* data{};
 status(open<AcDbXrecord>(id)->rbChain(&data,&db),"read saved PLAN_DRAW storey data");
 std::unique_ptr<resbuf,decltype(&acutRelRb)> chain(data,&acutRelRb);
 require(chain&&chain->restype==AcDb::kDxfText&&chain->resval.rstring,"invalid saved storey data");
 std::wistringstream input(chain->resval.rstring);
 input.imbue(std::locale::classic());
 std::map<std::size_t,std::pair<double,std::string>> values;
 std::wstring basis,datum;
 std::size_t panel{};
 double height{};
 while(input>>panel>>height>>basis>>datum)values.emplace(panel,std::make_pair(height,productUtf8(basis)));
 require(input.eof(),"invalid saved storey row");
 return values;
}

struct ProductHeightGeometry {
 std::vector<AcGePoint3d> grips;
 AcGePoint3d start, end;
 bool wall{};
};

ProductHeightGeometry productHeightGeometry(AcDbObjectId id,bool wall){
 ProductHeightGeometry result;
 result.wall=wall;
 result.grips=chainGrips(id);
 if(!wall)return result;
 const auto entity=open<AcDbEntity>(id);
 const auto curve=AcDbCurve::cast(entity.get());
 require(curve&&curve->getStartPoint(result.start)==Acad::eOk&&curve->getEndPoint(result.end)==Acad::eOk,
         "read native wall baseline for height operation");
 return result;
}

bool productSameHeightGeometry(const ProductHeightGeometry& a,const ProductHeightGeometry& b,double eps){
 return a.wall==b.wall&&a.start.distanceTo(b.start)<=eps&&a.end.distanceTo(b.end)<=eps&&
        a.grips.size()==b.grips.size()&&std::equal(a.grips.begin(),a.grips.end(),b.grips.begin(),
          [eps](const auto& p,const auto& q){return p.distanceTo(q)<=eps;});
}

std::vector<ProductWallHeight> productApplyWallHeights(ProductJob& job,AcDbDatabase& db,
 const GenericNativeInput& input,const std::vector<bcpproduct::GenericView>& views,
 const std::vector<bool>& generated,const std::vector<bcpproduct::PlanLevelResult>& levels){
 if(!job.unifiedPlan)return {};
 std::vector<ProductWallHeight> rows;
 std::map<std::tuple<std::size_t,double,double>,std::vector<std::size_t>> groups;
 const double eps=.01/job.unifiedUnitMM;
 for(std::size_t i=0;i<views.size();++i)if(generated[i])for(const auto index:views[i].walls){
  const auto& wall=input.walls[index];
  if(wall.low)continue;
  ProductWallHeight row;
  row.panel=i+1;
  row.handle=wall.wall.handle;
  if(levels[i].heightMM)row.target=*levels[i].heightMM/job.unifiedUnitMM;
  try{
   const auto& p=input.properties.at(row.handle);
   row.original=bcpproduct::propertyNumber(p,L"高度");
   row.base=bcpproduct::propertyNumber(p,L"底标高");
   if(p.count(L"左厚度")&&p.count(L"右厚度"))
    row.thickness=std::make_pair(bcpproduct::propertyNumber(p,L"左厚度"),bcpproduct::propertyNumber(p,L"右厚度"));
   require(row.original>0,"positive public wall height");
   if(!row.target)row.issue=u8"层高来源冲突或当前规则缺可执行的默认参数";
   else if(std::abs(row.original-*row.target)>eps)groups[{row.panel,*row.target,row.base}].push_back(rows.size());
  }catch(const std::exception& error){
   row.issue=u8"原生墙高度或底标高无法读取。"+std::string(error.what());
  }
  rows.push_back(std::move(row));
 }
 if(groups.empty())return rows;
 std::map<std::string,ProductHeightGeometry> protectedGeometry;
 for(const auto& wall:input.walls)
  protectedGeometry.emplace(wall.wall.handle,productHeightGeometry(find(db,wall.wall.handle),true));
 for(const auto& opening:input.openings)
  protectedGeometry.emplace(opening.handle,productHeightGeometry(find(db,opening.handle),false));
 const auto geometryIntact=[&]{
  return std::all_of(protectedGeometry.begin(),protectedGeometry.end(),[&](const auto& item){
   return productSameHeightGeometry(item.second,productHeightGeometry(find(db,item.first),item.second.wall),eps);
  });
 };
 std::set<std::string> openingHandles;
 for(const auto& opening:input.openings)openingHandles.insert(opening.handle);
 const auto openingBefore=openingHandles.empty()?std::map<std::string,bcpproduct::PublicProperties>{}:
    productReadProperties(db,job.root/L"墙高关联门窗基线",&openingHandles);
 const auto openingsIntact=[&](const fs::path& folder){
  if(openingHandles.empty())return true;
  const auto actual=productReadProperties(db,folder,&openingHandles);
  for(const auto& key:openingHandles){
   const auto& before=openingBefore.at(key);
   const auto& after=actual.at(key);
   for(const auto* field:{L"宽度",L"高度",L"离地高度",L"门窗类型",L"门窗编号"})
    if(before.count(field)&&(!after.count(field)||before.at(field)!=after.at(field)))return false;
  }
  return true;
 };
 auto* transaction=db.transactionManager();
 const auto module=fs::path(productSetting(L"tangentDirectory",L"C:\\Tangent\\TArchT20V8\\SYS23x64"))/L"tch_kerncmd.arx";
 if(!acedRegCmds->lookupGlobalCmd(L"TChHeight"))acrxDynamicLinker->loadModule(module.c_str(),false,false);
 const auto apply=[&](const auto& self,std::size_t panel,double height,double base,
                      const std::vector<std::size_t>& indices)->void{
  std::set<std::string> handles;
  for(const auto i:indices)handles.insert(rows[i].handle);
  const auto fieldsMatch=[&](bool target){
   const auto properties=productReadProperties(db,job.root/L"墙高原生属性"/std::to_wstring(panel),&handles);
   return std::all_of(indices.begin(),indices.end(),[&](std::size_t i){
    const auto& row=rows[i];
    const auto& p=properties.at(row.handle);
    const bool thicknessMatch=!row.thickness||
      (std::abs(bcpproduct::propertyNumber(p,L"左厚度")-row.thickness->first)<=eps&&
       std::abs(bcpproduct::propertyNumber(p,L"右厚度")-row.thickness->second)<=eps);
    return thicknessMatch&&std::abs(bcpproduct::propertyNumber(p,L"高度")-(target?height:row.original))<=eps&&
           std::abs(bcpproduct::propertyNumber(p,L"底标高")-base)<=eps;
   });
  };
  bool started=false;
  std::set<AcDbObjectId> beforeIds;
  try{
   require(acedRegCmds->lookupGlobalCmd(L"TChHeight")!=nullptr,"Tianzheng TChHeight unavailable");
   WholeSelection selected(find(db,rows[indices.front()].handle));
   for(std::size_t i=1;i<indices.size();++i){
    ads_name entity{};
    status(acdbGetAdsName(entity,find(db,rows[indices[i]].handle)),"wall height exact selection");
    require(acedSSAdd(entity,selected.value,selected.value)==RTNORM,"append wall height selection");
   }
   beforeIds=wholeModelIds(db);
   require(transaction->startTransaction()!=nullptr,"start wall height transaction");
   started=true;
   require(acedCommandS(RTSTR,L"TChHeight",RTPICKS,selected.value,RTSTR,L"",RTREAL,height,
                        RTSTR,L"",RTSTR,L"N",RTNONE)==RTNORM,"native wall height command");
   const auto openingFolder=job.root/L"墙高关联门窗"/std::to_wstring(panel);
   require(wholeModelIds(db)==beforeIds,"wall height command changed model membership");
   require(fieldsMatch(true),"native height, wall base or wall thickness did not match");
   require(geometryIntact(),"native wall or opening grip geometry changed");
   require(openingsIntact(openingFolder),"native opening width, height, sill, type or label changed");
   status(transaction->endTransaction(),"commit wall height transaction");
   started=false;
   ++job.planPublicCommands[L"TChHeight"];
   for(const auto i:indices)rows[i].changed=true;
  }catch(const std::exception& error){
   if(started){
    status(transaction->abortTransaction(),"roll back wall height transaction");
    require(wholeModelIds(db)==beforeIds&&geometryIntact()&&fieldsMatch(false)&&
            openingsIntact(job.root/L"墙高关联门窗回退"/std::to_wstring(panel)),
            "wall height rollback did not restore protected state");
   }
   if(started&&indices.size()>1){
    const auto middle=indices.begin()+indices.size()/2;
    self(self,panel,height,base,std::vector<std::size_t>(indices.begin(),middle));
    self(self,panel,height,base,std::vector<std::size_t>(middle,indices.end()));
   }else for(const auto i:indices)rows[i].issue=u8"原生改高度未通过，保留原墙。"+std::string(error.what());
  }
 };
 for(const auto& [parameters,indices]:groups){
  const auto [panel,height,base]=parameters;
  apply(apply,panel,height,base,indices);
 }
 return rows;
}

void productVerifyWallHeights(ProductJob& job,AcDbDatabase& saved,const std::vector<ProductWallHeight>& walls,
 const std::vector<bcpproduct::PlanLevelResult>& levels,const std::vector<bool>& generated){
 if(!job.unifiedPlan)return;
 const double eps=.01/job.unifiedUnitMM;
 std::set<std::string> handles;
 for(const auto& row:walls)handles.insert(row.handle);
 const auto properties=productReadProperties(saved,job.root/L"墙高重开属性",&handles);
 const auto persisted=productReadPlanLevels(saved);
 std::ostringstream report;
 report<<std::setprecision(17)<<"{\"savedReopened\":true,\"sourceSHA256\":\""<<job.sourceHash<<"\",\"levels\":[";
 bool first=true;
 const auto value=[](const std::optional<double>& n){return n?std::to_string(*n):"null";};
 for(std::size_t i=0;i<levels.size();++i)if(generated[i]){
  const auto& row=levels[i];
  const auto stored=persisted.find(i+1);
  const bool verified=row.heightMM&&stored!=persisted.end()&&std::abs(stored->second.first-*row.heightMM)<=.01&&
    stored->second.second==row.heightBasis;
  const auto reason=verified?row.heightBasis=="rule-default"?u8"缺已确认层高及相邻完成面差，采用现行规则默认值":"":
    u8"层高来源冲突、缺少适用的规则参数或 DWG 层高存录未通过";
  const auto measured="\"heightMM\":"+value(row.heightMM)+",\"finishedMetres\":"+value(row.finishedMetres)+
    ",\"heightBasis\":\""+row.heightBasis+"\",\"datumBasis\":\""+row.datumBasis+"\"";
  if(!first)report<<',';
  first=false;
  report<<"{\"panel\":"<<i+1<<','<<measured<<'}';
  productPlanAuditRow(job,{"B04","SE02"},i+1,"","storey-height",
    verified?"verified-resolved-storey-height":"pending-storey-height",reason,measured);
  productCheckPlanSource(job,i+1,"story-height","",verified?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::pending,
    {},reason);
  if(!verified)job.detailPending.push_back({u8"层高",std::to_string(i+1),reason});
 }
 report<<"],\"walls\":[";
 first=true;
 for(const auto& row:walls){
  std::optional<double> height,base;
  auto issue=row.issue;
  try{
   const auto& p=properties.at(row.handle);
   height=bcpproduct::propertyNumber(p,L"高度");
   base=bcpproduct::propertyNumber(p,L"底标高");
   if(issue.empty()&&(!row.target||std::abs(*height-*row.target)>eps||std::abs(*base-row.base)>eps))
    issue=u8"重开实际墙高或底标高与目标不一致";
   if(issue.empty()&&row.thickness&&
      (std::abs(bcpproduct::propertyNumber(p,L"左厚度")-row.thickness->first)>eps||
       std::abs(bcpproduct::propertyNumber(p,L"右厚度")-row.thickness->second)>eps))
    issue=u8"重开墙厚与源值不一致";
  }catch(const std::exception& error){
   if(issue.empty())issue=u8"重开未读取原生墙高度或底标高。"+std::string(error.what());
  }
  const auto measured="\"originalHeight\":"+std::to_string(row.original)+",\"targetHeight\":"+value(row.target)+
    ",\"actualHeight\":"+value(height)+",\"actualBase\":"+value(base)+
    ",\"changed\":"+(row.changed?"true":"false");
  if(!first)report<<',';
  first=false;
  report<<"{\"panel\":"<<row.panel<<",\"object\":\""<<row.handle<<"\","<<measured
        <<",\"issue\":"<<jsonUtf8(bcpproduct::decodeMessage(issue))<<'}';
  productPlanAuditRow(job,{"B04","SE02"},row.panel,row.handle,"wall-height",
    issue.empty()?"verified-native-wall-height":"pending-native-wall-height",issue,measured);
  productCheckPlanSource(job,row.panel,"wall-height",row.handle,
    issue.empty()?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::pending,{row.handle},issue);
  if(!issue.empty())job.detailPending.push_back({u8"墙高",row.handle,issue});
 }
 report<<"]}";
 writeNew(job.root/L"storey-wall-height-readback.json",report.str());
}
