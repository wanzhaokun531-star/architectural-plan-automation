// Treat a confirmed table as one plot object; never assign its cells independently.
std::optional<AcDbExtents> productTableBounds(AcDbDatabase& db,const ProductPlanTableGroup& table){
 std::optional<AcDbExtents> result;
 for(const auto& h:table.members){AcDbObjectId id;
  if(db.getAcDbObjectId(id,false,AcDbHandle(bcpproduct::decodeMessage(h).c_str()))!=Acad::eOk||id.isErased())return {};
  const auto e=open<AcDbEntity>(id);AcDbExtents box;if(e->getGeomExtents(box)!=Acad::eOk)return {};
  if(result)result->addExt(box);else result=box;
 }return result;
}

bool productTableGridInk(const GenericOpeningInk& ink,double tolerance){
 std::vector<std::pair<AcGePoint3d,AcGePoint3d>> horizontal,vertical;
 for(const auto& line:ink.straight){const auto d=line.second-line.first;
  if(std::abs(d.y)<tolerance&&std::abs(d.x)>10*tolerance)horizontal.push_back(line);
  else if(std::abs(d.x)<tolerance&&std::abs(d.y)>10*tolerance)vertical.push_back(line);
 }
 if(horizontal.size()<3||vertical.size()<3)return false;
 int rows=0;for(const auto& h:horizontal){int intersections=0;
  for(const auto& v:vertical)if(v.first.x>=std::min(h.first.x,h.second.x)-tolerance&&v.first.x<=std::max(h.first.x,h.second.x)+tolerance&&
    h.first.y>=std::min(v.first.y,v.second.y)-tolerance&&h.first.y<=std::max(v.first.y,v.second.y)+tolerance)++intersections;
  if(intersections>=3)++rows;
 }return rows>=3;
}

void productReadTableGridInk(const AcDbEntity& e,GenericOpeningInk& ink,int depth=0){
 if(depth>12)return;
 if(const auto* line=AcDbLine::cast(&e)){ink.straight.emplace_back(line->startPoint(),line->endPoint());return;}
 if(const auto* poly=AcDbPolyline::cast(&e)){
  const auto count=poly->numVerts();if(count<2)return;
  for(unsigned k=0;k<count-(poly->isClosed()?0:1);++k)if(poly->segType(k)==AcDbPolyline::kLine){
   AcGePoint3d a,b;if(poly->getPointAt(k,a)==Acad::eOk&&poly->getPointAt((k+1)%count,b)==Acad::eOk)ink.straight.emplace_back(a,b);
  }return;
 }
 if(AcDbText::cast(&e)||AcDbMText::cast(&e))return;
 TemplateParts parts(&e);if(e.explode(parts.values)!=Acad::eOk)return;
 for(int k=0;k<parts.values.length();++k)if(const auto* child=AcDbEntity::cast(static_cast<AcRxObject*>(parts.values[k])))productReadTableGridInk(*child,ink,depth+1);
}

bool productTableGrid(const AcDbEntity& e,double tolerance){
 GenericOpeningInk ink;productReadTableGridInk(e,ink);return productTableGridInk(ink,tolerance);
}

bool productTableMembersPrintable(AcDbDatabase& db,const ProductPlanTableGroup& table,AcDbObjectId viewport={}){
 std::set<AcDbObjectId> stack;
 const auto check=[&](auto&& self,AcDbObjectId id,AcDbObjectId parentLayer,int depth)->bool{
  if(depth>16||id.isErased())return false;
  const auto e=open<AcDbEntity>(id);if(e->visibility()!=AcDb::kVisible||e->isAProxy())return false;
  auto layer=e->layerId();if(symbolName(layer)==L"0"&&!parentLayer.isNull())layer=parentLayer;
  const auto record=open<AcDbLayerTableRecord>(layer);
  if(record->isOff()||record->isFrozen()||!record->isPlottable()||
     (!viewport.isNull()&&open<AcDbViewport>(viewport)->isLayerFrozenInViewport(layer)))return false;
  if(const auto* attribute=AcDbAttribute::cast(e.get());attribute&&attribute->isInvisible())return false;
  if(const auto* ref=AcDbBlockReference::cast(e.get())){
   std::unique_ptr<AcDbObjectIterator> attrs(ref->attributeIterator());
   if(attrs)for(;!attrs->done();attrs->step())if(!self(self,attrs->objectId(),layer,depth+1))return false;
   const auto definition=ref->blockTableRecord();if(!stack.insert(definition).second)return false;
   const auto block=open<AcDbBlockTableRecord>(definition);AcDbBlockTableRecordIterator* raw{};
   if(block->isUnloaded()||block->newIterator(raw)!=Acad::eOk)return false;
   std::unique_ptr<AcDbBlockTableRecordIterator> it(raw);
   for(;!it->done();it->step()){AcDbObjectId child;if(it->getEntityId(child)!=Acad::eOk||!self(self,child,layer,depth+1))return false;}
   stack.erase(definition);
  }
  return true;
 };
 for(const auto& h:table.members){AcDbObjectId id;
  if(db.getAcDbObjectId(id,false,AcDbHandle(bcpproduct::decodeMessage(h).c_str()))!=Acad::eOk||!check(check,id,{},0))return false;
 }
 return !table.members.empty();
}

void productCapturePlanTables(ProductJob& job,AcDbDatabase& db,const std::vector<bcpproduct::GenericView>& views){
 job.planTables.clear();if(views.empty())return;
 const auto capture=[&](ProductPlanTableGroup table){
  const auto box=productTableBounds(db,table);if(!box)return;
  const auto centre=box->minPoint()+(box->maxPoint()-box->minPoint())*.5;
  std::vector<std::pair<double,std::size_t>> distances;
  for(std::size_t k=0;k<views.size();++k)distances.emplace_back(std::sqrt(bcpproduct::genericDistanceSquared(views[k].bounds,{centre.x,centre.y})),k);
  std::sort(distances.begin(),distances.end());table.view=distances.front().second;
  table.assigned=distances.size()==1||distances[1].first-distances[0].first>3*job.unifiedScale/job.unifiedUnitMM;
  if(!table.assigned)job.detailPending.push_back({u8"表格归属",table.members.front(),u8"整表与相邻图幅归属接近，保留整表并列清单，未按单元格分配"});
  job.planTables.push_back(std::move(table));
 };
 for(const auto& [raw,type]:modelTypes(db)){
  const auto h=ascii(raw);if(job.protectedFurnitureMembers.count(h))continue;
  const auto e=open<AcDbEntity>(find(db,h));
  const bool native=type==L"AcDbTable"||type==L"TDbTable";
  if(!native&&type!=L"AcDbBlockReference")continue;
  std::vector<ProductRoomLeaf> text;productRoomLeaves(*e,text);
  bool subject=native;
  for(const auto& leaf:text)subject|=leaf.text.find(L"技术指标")!=std::wstring::npos||leaf.text.find(L"建筑面积")!=std::wstring::npos||leaf.text.find(L"门窗表")!=std::wstring::npos;
  if(!native&&(!subject||!productTableGrid(*e,.01/job.unifiedUnitMM)))continue;
  capture({native?"native-table":"confirmed-table-block",{h}});
 }
 // An existing ordinary CAD group is authoritative membership evidence.
 // Never combine nearby loose lines or regroup/explode furniture heuristically.
 const auto dictionary=open<AcDbDictionary>(db.groupDictionaryId());
 std::unique_ptr<AcDbDictionaryIterator> it(dictionary->newIterator());
 for(;!it->done();it->next()){
  const auto group=open<AcDbGroup>(it->objectId());AcDbObjectIdArray members;group->allEntityIds(members);
  if(members.length()<7)continue;
  ProductPlanTableGroup table{"confirmed-existing-table-group",{}};GenericOpeningInk ink;bool valid=true,subject=false;
  for(int k=0;k<members.length();++k){
   if(members[k].isErased()){valid=false;break;}
   const auto h=axisHandle(members[k]);const auto e=open<AcDbEntity>(members[k]);
   const auto type=std::wstring(e->isA()->name());
   if(e->ownerId()!=modelId(db)||job.protectedFurnitureMembers.count(h)||
      (type!=L"AcDbLine"&&type!=L"AcDbPolyline"&&type!=L"AcDbText"&&type!=L"AcDbMText"&&type!=L"TDbText"&&type!=L"TDbMText")){
    valid=false;break;
   }
   std::vector<ProductRoomLeaf> text;productRoomLeaves(*e,text);
   for(const auto& leaf:text)subject|=leaf.text.find(L"技术指标")!=std::wstring::npos||
    leaf.text.find(L"建筑面积")!=std::wstring::npos||leaf.text.find(L"门窗表")!=std::wstring::npos;
   productReadTableGridInk(*e,ink);table.members.push_back(h);
  }
  if(valid&&subject&&productTableGridInk(ink,.01/job.unifiedUnitMM))capture(std::move(table));
 }
}

void productExpandPlanTables(ProductJob& job,AcDbDatabase& db){
 const auto margin=6*job.unifiedScale/job.unifiedUnitMM;
 for(const auto& table:job.planTables){if(!table.assigned)continue;
  const auto panel=std::find(job.layoutSourcePanels.begin(),job.layoutSourcePanels.end(),table.view+1);
  if(panel==job.layoutSourcePanels.end())continue;
  const auto i=static_cast<std::size_t>(panel-job.layoutSourcePanels.begin());if(i>=job.planPrintWindows.size())continue;
  const auto bounds=productTableBounds(db,table);if(!bounds){job.detailPending.push_back({u8"表格完整性",table.members.front(),u8"整表成员丢失或范围不可读"});continue;}
  auto& w=job.planPrintWindows[i];w[0]=std::min(w[0],bounds->minPoint().x-margin);w[1]=std::min(w[1],bounds->minPoint().y-margin);
  w[2]=std::max(w[2],bounds->maxPoint().x+margin);w[3]=std::max(w[3],bounds->maxPoint().y+margin);
 }
}

void productAuditPlanTables(ProductJob& job,AcDbDatabase& db){
 std::ostringstream out;out<<"{\"savedReopened\":true,\"tables\":[";bool first=true;
 for(const auto& table:job.planTables){
  const auto bounds=productTableBounds(db,table);bool inside=false,printable=false;AcDbObjectId viewport;
  const auto panel=std::find(job.layoutSourcePanels.begin(),job.layoutSourcePanels.end(),table.view+1);
  if(table.assigned&&bounds&&panel!=job.layoutSourcePanels.end()){
   const auto i=static_cast<std::size_t>(panel-job.layoutSourcePanels.begin());
   if(i<job.planPrintWindows.size()){auto w=job.planPrintWindows[i];bool viewportUsable=true;
#ifdef PLAN_STANDALONE_ENTRY
    viewportUsable=false;
    const auto sheet=job.planSheetLayouts.find(static_cast<int>(i+1));if(sheet!=job.planSheetLayouts.end()&&!sheet->second.viewport.empty()){
     viewport=find(db,sheet->second.viewport);const auto vp=open<AcDbViewport>(viewport);
     viewportUsable=vp->isOn()&&!vp->isPerspectiveOn()&&vp->viewDirection().isEqualTo(AcGeVector3d::kZAxis)&&std::abs(vp->twistAngle())<1e-8&&vp->customScale()>0;
     if(viewportUsable){const auto c=vp->viewTarget();const auto p=vp->viewCenter();
      const auto x=vp->width()/(2*vp->customScale()),y=vp->height()/(2*vp->customScale());w={c.x+p.x-x,c.y+p.y-y,c.x+p.x+x,c.y+p.y+y};}
    }
#endif
    inside=viewportUsable&&bounds->minPoint().x>=w[0]&&bounds->minPoint().y>=w[1]&&bounds->maxPoint().x<=w[2]&&bounds->maxPoint().y<=w[3];
    printable=viewportUsable&&productTableMembersPrintable(db,table,viewport);
   }
  }
  if(!inside||!printable){++job.planPrintDefects;job.detailPending.push_back({u8"整表打印范围",table.members.front(),u8"表格归属未定、成员缺失、被隐藏/设为不打印/视口冻结或整表未完全入幅；完全在窗口外也计入缺失"});}
  if(!first)out<<',';first=false;out<<"{\"object\":\""<<table.members.front()<<"\",\"kind\":\""<<table.kind<<"\",\"sourcePanel\":"<<table.view+1<<",\"assigned\":"<<(table.assigned?"true":"false")<<",\"boundsInside\":"<<(inside?"true":"false")<<",\"membersPrintable\":"<<(printable?"true":"false")<<",\"completeInSheet\":"<<(inside&&printable?"true":"false")<<'}';
 }out<<"]}";writeNew(job.root/L"plan-table-readback.json",out.str());
}
