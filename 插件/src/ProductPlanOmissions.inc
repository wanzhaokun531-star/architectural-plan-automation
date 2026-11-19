#include <regex>
// Classify by measurement stations against occupied room cells and actual
// walls on either side. The dimension-line and text positions are irrelevant.
bool productInteriorStations(const std::vector<AcGePoint3d>& stations,const GenericNativeInput& input,
 const bcpproduct::GenericView& view,const std::optional<ProductRoomGrid>& grid,double unit,const std::set<int>& exterior){
 // A missing or open room grid is unknown, not permission to delete a chain.
 // Four rays crossing walls can also describe an open courtyard or entrance.
 if(stations.size()<2||!grid)return false;
 const double eps=.02/unit;
 for(std::size_t i=1;i<stations.size();++i){
  const auto delta=stations[i]-stations[i-1];
  if(delta.length()<eps)return false;
  for(double f:{.15,.5,.85}){
   const auto p=stations[i-1]+delta*f;
   bool left=false,right=false,above=false,below=false;
   for(const auto idx:view.walls){
    const auto& host=input.walls[idx];
    if(host.low||!host.orthogonal)continue;
    const auto a=bcp::bodyCentreStart(host.wall);
    const double bx=a.x+host.wall.end.x-host.wall.start.x,by=a.y+host.wall.end.y-host.wall.start.y;
    const double half=host.total*.5+eps;
    if(std::abs(a.x-bx)<eps&&p.y>=std::min(a.y,by)-half&&p.y<=std::max(a.y,by)+half){
     left|=a.x<p.x-half;right|=a.x>p.x+half;
    }else if(std::abs(a.y-by)<eps&&p.x>=std::min(a.x,bx)-half&&p.x<=std::max(a.x,bx)+half){
     below|=a.y<p.y-half;above|=a.y>p.y+half;
    }
   }
   if(grid->at(p)<0||exterior.count(grid->at(p)))return false;
  }
 }
 return true;
}

bool productOmissionEditable(AcDbDatabase& db,const std::string& handle){
 AcDbObjectId id;
 if(db.getAcDbObjectId(id,false,AcDbHandle(bcpproduct::decodeMessage(handle).c_str()))!=Acad::eOk||id.isErased())return false;
 const auto entity=open<AcDbEntity>(id);
 return !entity->isAProxy()&&!entity->hasFields()&&!open<AcDbLayerTableRecord>(entity->layerId())->isLocked();
}

void productRetirePlanAnnotation(ProductJob& job,AcDbDatabase& db,const std::string& key,const std::string& rule){
 if(job.planOmissions.count(key))return;
 if(job.protectedFurnitureMembers.count(key)){
  job.detailPending.push_back({rule,key,u8"家具原实例或共享成员受保护；只读发现保留，未删除原对象。"});return;
 }
 if(!productOmissionEditable(db,key)){
  job.detailPending.push_back({rule,key,u8"源标注被锁定、带字段或不可编辑，须核原件。"});return;
 }
 productStatus(editObject<AcDbEntity>(find(db,key))->erase(),"remove confirmed obsolete plan annotation");
 job.planOmissions.emplace(key,rule);
 if(rule=="D23")job.planDimensionExclusions.insert(key);
}

bool productGrossAreaProtected(const std::wstring& text){
 for(const auto* word:{L"建筑面积",L"计容面积",L"占地面积",L"总面积",L"总建面",L"本层面积",L"半面积",L"楼层面积"})
  if(text.find(word)!=std::wstring::npos)return true;
 return productPlanGrossAreaLabel(text)||productPlanGroupedAreaLabel(text);
}

void productCleanRoomAreaLines(ProductJob& job,AcDbDatabase& db,const std::vector<bcpproduct::GenericView>& views,
 const std::vector<bool>& selected,const std::vector<std::optional<ProductRoomGrid>>& grids,
 const std::function<bool(const AcGePoint3d&)>& inScope={}){
 // Plain and paired legacy labels, including a separate raised '2'.
 for(std::size_t view=0;view<views.size();++view){
  if(!selected[view])continue;
  ProductRoomGrid empty;
  const auto areas=productOldNetAreas(db,grids[view]?*grids[view]:empty,views,view,true);
  for(const auto& area:areas){
   if(!area.explicitUnit)continue;
   if(job.protectedFurnitureMembers.count(area.body)||(!area.exponent.empty()&&job.protectedFurnitureMembers.count(area.exponent)))continue;
   const auto id=find(db,area.body);auto entity=open<AcDbEntity>(id);
   AcDbExtents areaBounds;if(entity->getGeomExtents(areaBounds)!=Acad::eOk)continue;
   const auto areaAt=areaBounds.minPoint()+(areaBounds.maxPoint()-areaBounds.minPoint())*.5;
   if(inScope&&!inScope(areaAt))continue;
   const auto& viewBounds=views[view].bounds;
   if(areaAt.x<viewBounds.xmin||areaAt.x>viewBounds.xmax||areaAt.y<viewBounds.ymin||areaAt.y>viewBounds.ymax)continue;
   const bool roomLayer=symbolName(entity->layerId()).find(L"房间")!=std::wstring::npos;
   if(area.region<0&&!roomLayer)continue;
   if(!productOmissionEditable(db,area.body)||(!area.exponent.empty()&&!productOmissionEditable(db,area.exponent))){
    job.detailPending.push_back({"S01",area.body,u8"面积正文或配对上标受保护，须整组核实。"});continue;
   }
   entity.reset();
   productRetirePlanAnnotation(job,db,area.body,"S01");
   if(!area.exponent.empty())productRetirePlanAnnotation(job,db,area.exponent,"S01");
  }
 }
 // Explicit area tokens can share an entity with a room name. Keep the
 // existing entity and the exact surrounding name/formatting.
 static const std::wregex token(LR"((?:(?:房间(?:净)?面积|净面积|面积)\s*[：:]?\s*)?[0-9]+(?:\.[0-9]+)?\s*(?:㎡|平方米|[mM](?:²|2|\^U2\^U|\\U\+00[Bb]2)))");
 for(const auto& [h,type]:modelTypes(db)){
  if(type!=L"AcDbText"&&type!=L"AcDbMText"&&type!=L"TDbText"&&type!=L"TDbMText")continue;
  const auto key=ascii(h);const auto id=find(db,key);auto entity=open<AcDbEntity>(id);
  if(job.protectedFurnitureMembers.count(key))continue;
  AcDbExtents bounds;if(entity->getGeomExtents(bounds)!=Acad::eOk)continue;
  const auto p=bounds.minPoint()+(bounds.maxPoint()-bounds.minPoint())*.5;
  if(inScope&&!inScope(p))continue;
  const auto view=bcpproduct::nearestGenericView(views,{p.x,p.y});
  if(!selected[view])continue;
  const auto& viewBounds=views[view].bounds;
  if(p.x<viewBounds.xmin||p.x>viewBounds.xmax||p.y<viewBounds.ymin||p.y>viewBounds.ymax)continue;
  const auto* text=AcDbText::cast(entity.get());const auto* mtext=AcDbMText::cast(entity.get());
  std::wstring value;
  if(text)value=text->textString();
  else if(mtext){AcString contents;mtext->contents(contents);value=contents.kACharPtr();}
  else{std::vector<ProductRoomLeaf> leaves;productRoomLeaves(*entity,leaves);for(const auto& l:leaves)value+=l.text;}
  if(productGrossAreaProtected(value)||!std::regex_search(value,token))continue;
  const auto nameLayer=symbolName(entity->layerId()).find(L"房间")!=std::wstring::npos;
  const auto role=productRoomRole(value);
  if(!nameLayer&&!(grids[view]&&grids[view]->at(p)>=0)&&role==ProductRoomRole::other)continue;
  auto rest=std::regex_replace(value,token,L"");
  // Remove only empty paragraphs and separators bordering the removed suffix.
  while(!rest.empty()&&::iswspace(rest.back()))rest.pop_back();
  if(rest.size()>=2&&rest.substr(rest.size()-2)==L"\\P")rest.resize(rest.size()-2);
  if(rest.size()>=2&&rest.substr(0,2)==L"\\P")rest.erase(0,2);
  if(rest.empty()){entity.reset();productRetirePlanAnnotation(job,db,key,"S01");continue;}
  if((!text&&!mtext)||!productOmissionEditable(db,key)){
   job.detailPending.push_back({"S01",key,u8"面积与其他内容共用受保护文字，须保留其余文字并核实原件。"});continue;
  }
  productStatus(entity->upgradeOpen(),"open mixed area and room label");
  if(text)productStatus(AcDbText::cast(entity.get())->setTextString(rest.c_str()),"retain room name in mixed text");
  else AcDbMText::cast(entity.get())->setContents(rest.c_str());
  job.retainedAreaNames.emplace(key,rest);
 }
}

void productCleanNestedPlanAnnotations(ProductJob& job,AcDbDatabase& db,const GenericNativeInput& input,
 const std::vector<bcpproduct::GenericView>& views,const std::vector<bool>& active,
 const std::vector<std::optional<ProductRoomGrid>>& grids,const std::vector<std::set<int>>& exterior,
 const std::function<bool(const AcGePoint3d&)>& inScope={}){
 struct Use {std::string rule;bool retire{};};
 std::map<std::string,std::vector<Use>> uses;
 std::set<std::string> paperMembers;
 const auto dictionary=open<AcDbDictionary>(db.layoutDictionaryId());
 std::unique_ptr<AcDbDictionaryIterator> it(dictionary->newIterator());
 for(;!it->done();it->next()){
  const auto layout=open<AcDbLayout>(it->objectId());if(layout->modelType())continue;
  for(const auto& row:productReadOldAnnotations(db,nullptr,layout->getBlockTableRecordId()))paperMembers.insert(row.handle);
 }
 const auto& rows=job.planSourceAnnotations;
 std::map<std::string,std::string> pairs;
 for(const auto& body:rows){
  if(body.root==body.handle||!body.bounds||body.glyphs.size()!=1||!productRoomAreaText(body.glyphs[0].text)||body.glyphs[0].text.back()!=L'm')continue;
  const auto parent=productAnnotationParent(body.path);
  std::vector<std::string> matches;
  for(const auto& exp:rows){
   if(!exp.bounds||exp.glyphs.size()!=1||exp.glyphs[0].text!=L"2"||exp.layer!=body.layer||
      exp.glyphs[0].style!=body.glyphs[0].style||productAnnotationParent(exp.path)!=parent)continue;
   const double h=body.glyphs[0].height;
   const double gap=exp.bounds->minPoint().x-body.bounds->maxPoint().x;
   const double rise=exp.bounds->minPoint().y-body.bounds->minPoint().y;
   if(h>0&&std::abs(h-exp.glyphs[0].height*2)<.01&&std::abs(gap)<h/2&&rise>0&&rise<h)matches.push_back(exp.path);
  }
  if(matches.size()==1){pairs.emplace(body.path,matches.front());pairs.emplace(matches.front(),body.path);}
 }
 for(const auto& row:rows){
  if(row.root==row.handle)continue;
  const bool dimension=row.kind=="dimension";
  std::wstring label;for(const auto& glyph:row.glyphs)label+=glyph.text;
  const bool area=pairs.count(row.path)||(!label.empty()&&productRoomAreaText(label)&&label.back()!=L'm');
  if(!dimension&&!area)continue;
  const auto rule=dimension?"D23":"S01";
  bool eligible=false;
  if(!row.external&&row.position&&(!inScope||inScope(*row.position))&&row.panel&&row.panel<=views.size()&&active[row.panel-1]&&
     !job.protectedFurnitureMembers.count(row.handle)&&!job.protectedFurnitureMembers.count(row.root)){
   const auto v=row.panel-1;
   if(dimension){
    auto dim=productReadSourceDimension(db,row.handle,.02/job.unifiedUnitMM);
    AcGeMatrix3d world;std::size_t start=0;bool local=true;
    for(auto slash=row.path.find('/');slash!=std::string::npos;slash=row.path.find('/',start)){
     const auto h=row.path.substr(start,slash-start);start=slash+1;
     if(!productOmissionEditable(db,h)){local=false;break;}
     const auto entity=open<AcDbEntity>(find(db,h));const auto* ref=AcDbBlockReference::cast(entity.get());
     if(!ref){local=false;break;}world=world*ref->blockTransform();
    }
    for(auto& p:dim.points)p.transformBy(world);
    eligible=local&&dim.readable&&(!inScope||std::all_of(dim.points.begin(),dim.points.end(),inScope))&&
     productInteriorStations(dim.points,input,views[v],grids[v],job.unifiedUnitMM,exterior[v]);
   }else{
    const auto& bounds=views[v].bounds;const auto& at=*row.position;
    eligible=at.x>=bounds.xmin&&at.x<=bounds.xmax&&at.y>=bounds.ymin&&at.y<=bounds.ymax&&
     !productGrossAreaProtected(label)&&
     (row.layer.find(L"房间")!=std::wstring::npos||(grids[v]&&grids[v]->at(at)>=0))&&productOmissionEditable(db,row.root);
   }
  }
  uses[row.handle].push_back({rule,eligible});
 }
 std::set<std::string> approved;
 for(const auto& [h,instances]:uses){
  if(!std::any_of(instances.begin(),instances.end(),[](const auto& u){return u.retire;}))continue;
  if(paperMembers.count(h)||!productOmissionEditable(db,h)||!std::all_of(instances.begin(),instances.end(),[](const auto& u){return u.retire;})){
   job.detailPending.push_back({instances.front().rule,h,u8"块内标注跨用途、图幅或图纸空间共用，保留原定义并登记。"});continue;
  }
  approved.insert(h);
 }
 for(const auto& [path,peer]:pairs){
  const auto first=path.substr(path.rfind('/')+1),second=peer.substr(peer.rfind('/')+1);
  if(!approved.count(first)||!approved.count(second)){approved.erase(first);approved.erase(second);}
 }
 for(const auto& h:approved)productRetirePlanAnnotation(job,db,h,uses.at(h).front().rule);
}


void productApplyPlanOmissions(ProductJob& job,AcDbDatabase& db,const GenericNativeInput& input,
 const std::vector<bcpproduct::GenericView>& views,const std::vector<bool>& active,
 const std::vector<std::optional<ProductRoomGrid>>& grids,const std::map<std::wstring,std::wstring>& original,
 const std::function<bool(const AcGePoint3d&)>& inScope={}){
 if(!job.currentPlanRules)return;
 Working context(&db);
 productCleanRoomAreaLines(job,db,views,active,grids,inScope);
 std::vector<std::set<int>> exterior(views.size());
 for(std::size_t v=0;v<views.size();++v)if(active[v]&&grids[v]){
  for(const auto& name:productRoomNames(db,input,views,v,nullptr)){
   if(name.role!=ProductRoomRole::excluded)continue;
   const auto p=name.bounds.minPoint()+(name.bounds.maxPoint()-name.bounds.minPoint())*.5;
   const auto region=grids[v]->at(p);if(region>=0)exterior[v].insert(region);
  }
 }
 productCleanNestedPlanAnnotations(job,db,input,views,active,grids,exterior,inScope);
 for(const auto& [handle,type]:original){
  if(type!=L"TDbDimension2"&&type!=L"AcDbRotatedDimension"&&type!=L"AcDbAlignedDimension")continue;
  const auto key=ascii(handle);
  if(job.protectedFurnitureMembers.count(key))continue;
  const auto dimension=productReadSourceDimension(db,key,.02/job.unifiedUnitMM);
  if(!dimension.readable||dimension.points.size()<2)continue;
  if(inScope&&!std::all_of(dimension.points.begin(),dimension.points.end(),inScope))continue;
  const auto& a=dimension.points.front();
  const auto v=bcpproduct::nearestGenericView(views,{a.x,a.y});
  if(active[v]&&productInteriorStations(dimension.points,input,views[v],grids[v],job.unifiedUnitMM,exterior[v]))
   productRetirePlanAnnotation(job,db,key,"D23");
 }
 for(const auto& [path,row]:job.planLooseDimensions)if(!row.stations.empty()){
  if(inScope&&!std::all_of(row.stations.begin(),row.stations.end(),inScope))continue;
  const auto& p=row.stations.front();const auto v=bcpproduct::nearestGenericView(views,{p.x,p.y});
  if(active[v]&&productInteriorStations(row.stations,input,views[v],grids[v],job.unifiedUnitMM,exterior[v]))job.interiorLooseLabels.insert(path);
 }
}

void productVerifyPlanOmissions(ProductJob& job,AcDbDatabase& saved){
 if(!job.currentPlanRules)return;
 Working context(&saved);std::ostringstream out;
 out<<"{\"savedReopened\":true,\"removed\":[";bool first=true;
 for(const auto& [key,rule]:job.planOmissions){
  AcDbObjectId id;
  const bool absent=saved.getAcDbObjectId(id,false,AcDbHandle(bcpproduct::decodeMessage(key).c_str()))!=Acad::eOk||id.isErased();
  require(absent,"retired interior dimension or room area survived reopening");
  const std::string row="{\"handle\":\""+key+"\",\"rule\":\""+rule+"\",\"result\":\"verified-absent\"}";
  if(!first)out<<',';first=false;out<<row;productRecordRuleEvidence(job,rule,row);
 }
 out<<"],\"preservedMixedLabels\":[";first=true;
 for(const auto& [key,expected]:job.retainedAreaNames){
  const auto entity=open<AcDbEntity>(find(saved,key));std::wstring value;
  if(const auto* text=AcDbText::cast(entity.get()))value=text->textString();
  else if(const auto* text=AcDbMText::cast(entity.get())){AcString s;text->contents(s);value=s.kACharPtr();}
  require(value==expected,"remaining mixed room name changed on reopen");
  if(!first)out<<',';first=false;out<<"\""<<key<<"\"";
 }
 out<<"],\"remainingDimensions\":[";first=true;
 for(const auto& [wide,type]:modelTypes(saved)){
  if(type!=L"TDbDimension2"&&type!=L"AcDbRotatedDimension"&&type!=L"AcDbAlignedDimension")continue;
  const auto key=ascii(wide);const auto entity=open<AcDbEntity>(find(saved,key));
  const auto measured=productReadSourceDimension(saved,key,.02/job.unifiedUnitMM);
  if(!first)out<<',';first=false;
  out<<"{\"handle\":\""<<key<<"\",\"type\":"<<jsonUtf8(type)<<",\"visible\":"<<(productPlanDimensionVisible(*entity)?"true":"false")
     <<",\"readable\":"<<(measured.readable?"true":"false")<<",\"stations\":"<<wholePoints(measured.points)<<"}";
 }
 out<<"]}";writeNew(job.root/L"plan-omissions-readback.json",out.str());
}
