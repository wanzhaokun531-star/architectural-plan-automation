#include "PlanPanelOwnership.h"

const char* productPanelRoleName(ProductPanelRole role){
 switch(role){
 case ProductPanelRole::floor:return "floor";
 case ProductPanelRole::unnamed:return "unnamed";
 case ProductPanelRole::roof:return "roof";
 case ProductPanelRole::detail:return "detail";
 case ProductPanelRole::attached:return "attached-floor-component";
 case ProductPanelRole::outside:return "outside-confirmed-scope";
 default:return "unassigned";
 }
}

std::vector<ProductPanelOwnership> productIdentifyPlanPanels(const GenericNativeInput& input,
 const std::vector<bcpproduct::GenericView>& views,double unit){
 struct Titles {
  std::set<int> floors;
  bool roof{},detail{};
  std::vector<std::string> handles;
 };
 std::vector<Titles> evidence(views.size());
 for(const auto* group:{&input.titles,&input.notes})for(const auto& title:*group){
  const auto floor=bcpproduct::explicitPlanFloor(title.text);
  const bool roof=productRoofTitle(title.text);
  const bool detail=group==&input.titles&&
   (title.text.find(L"详图")!=std::wstring::npos||title.text.find(L"节点")!=std::wstring::npos||
    title.text.find(L"剖面")!=std::wstring::npos||title.text.find(L"立面")!=std::wstring::npos||
    title.text.find(L"局部放大")!=std::wstring::npos);
  if(!floor&&!roof&&!detail)continue;
  const bcp::Point anchor{title.anchor.x,title.anchor.y};
  const auto nearest=bcpproduct::nearestGenericView(views,anchor);
  const double distance=std::sqrt(bcpproduct::genericDistanceSquared(views[nearest].bounds,anchor));
  for(std::size_t i=0;i<views.size();++i){
   if(std::abs(std::sqrt(bcpproduct::genericDistanceSquared(views[i].bounds,anchor))-distance)>.01/unit)continue;
   // A roof drawn entirely with ordinary linework has no wall component.
   // Its distant title must not claim a small pier in the adjacent floor.
   const auto& box=views[i].bounds;
   if(roof&&distance>std::hypot(box.xmax-box.xmin,box.ymax-box.ymin))continue;
   auto& candidate=evidence[i];
   candidate.handles.push_back(title.handle);
   if(floor)candidate.floors.insert(*floor);
   candidate.roof|=roof;
   candidate.detail|=detail;
  }
 }
 std::map<std::string,std::size_t> titleUse;
 bool hasFloorTitles=false;
 for(const auto& candidate:evidence){
  for(const auto& handle:candidate.handles)++titleUse[handle];
  hasFloorTitles|=!candidate.floors.empty();
 }
 std::vector<ProductPanelOwnership> result;
 for(std::size_t i=0;i<views.size();++i){
  const auto& candidate=evidence[i];
  ProductPanelOwnership panel;
  panel.titles=candidate.handles;
  const bool tie=std::any_of(panel.titles.begin(),panel.titles.end(),[&](const auto& h){return titleUse.at(h)>1;});
  const auto kinds=static_cast<int>(!candidate.floors.empty())+candidate.roof+candidate.detail;
  const auto& bounds=views[i].bounds;
  const bool area=bounds.xmax-bounds.xmin>.01/unit&&bounds.ymax-bounds.ymin>.01/unit;
  if(tie||kinds>1||candidate.floors.size()>1)
   panel.reason=u8"源图名与多个组件等距，或同一组件出现冲突的图名，原对象保留";
  else if(candidate.roof){
   panel.role=ProductPanelRole::roof;
   panel.reason=u8"源屋顶图名关联，按屋面专项核范围和尺寸";
  }else if(candidate.detail){
   panel.role=ProductPanelRole::detail;
   panel.reason=u8"源详图、立面或剖面图名关联，保留原表达";
  }else if(area&&!candidate.floors.empty()){
   panel.role=ProductPanelRole::floor;
   panel.floor=*candidate.floors.begin();
   panel.reason=u8"源楼层图名与墙体范围对应";
  }else if(!hasFloorTitles&&area&&views[i].walls.size()>2){
   panel.role=ProductPanelRole::unnamed;
   panel.reason=u8"无明确楼层图名，保留可独立处理的墙体平面，楼层身份待核";
  }else panel.reason=u8"墙体片段缺明确图名或完整的二维范围，保留原对象并待核归属";
  result.push_back(std::move(panel));
 }
 return result;
}

void productApplyPlanPanelScope(ProductJob& job,const GenericNativeInput& input,
 const std::vector<bcpproduct::GenericView>& views,std::vector<ProductPanelOwnership>& panels){
 if(job.unifiedPanelScope.empty())return;
 std::ifstream source(job.unifiedPanelScope);
 std::string tag,digest;
 require(bool(source>>tag>>digest)&&tag=="PLANPANELS1"&&digest==job.sourceHash,
   "panel scope must match the exact source DWG");
 std::set<std::size_t> selected,site;
 std::map<std::size_t,std::size_t> attached;
 const auto componentOf=[&](const std::string& handle){
  for(std::size_t i=0;i<views.size();++i)
   if(std::any_of(views[i].walls.begin(),views[i].walls.end(),[&](auto index){
     return input.walls[index].wall.handle==handle;
   }))return i;
  return views.size();
 };
 std::string handle;
 while(source>>tag&&(tag=="WALL"||tag=="SITE"||tag=="ATTACH")){
  require(bool(source>>handle),"panel scope requires a source wall handle");
  const auto i=componentOf(handle);
  require(i<views.size(),"panel scope wall is absent from source");
  if(tag=="ATTACH"){
   std::string host;
   require(bool(source>>host),"attached component requires a source floor wall");
   const auto parent=componentOf(host);
   require(parent<views.size()&&panels[parent].role==ProductPanelRole::floor&&
    panels[i].role==ProductPanelRole::unassigned&&panels[i].titles.empty()&&
    attached.emplace(i,parent).second,"distinct unattached wall component and identified floor");
  }else if(tag=="SITE"){
    require(panels[i].role==ProductPanelRole::unassigned&&panels[i].titles.empty(),
     "site reference must be outside a titled architectural plan");
    require(site.insert(i).second,"duplicate site component");
   }else{
    require(panels[i].eligible(),"selected wall is not in an identified plan");
    require(selected.insert(i).second,"duplicate selected plan panel");
  }
 }
 require(tag=="END"&&!(source>>tag)&&(!selected.empty()||!site.empty()||!attached.empty()),"complete panel scope");
 for(const auto i:site){
  panels[i].role=ProductPanelRole::detail;
  panels[i].reason=u8"按源图总平面部位核定为场地表达，保留原对象";
 }
 for(const auto& [component,parent]:attached){
  require(!site.count(component)&&(selected.empty()||selected.count(parent)),"attached component floor is in scope");
  panels[component].role=ProductPanelRole::attached;
  panels[component].floor=panels[parent].floor;
  panels[component].parentPanel=parent;
  panels[component].titles=panels[parent].titles;
  panels[component].reason=u8"已按源图定位至本层附属墙片，保留原位置及构造表达，主体测站另验";
 }
 for(std::size_t i=0;i<panels.size();++i)if(!selected.empty()&&panels[i].eligible()&&!selected.count(i)){
  panels[i].role=ProductPanelRole::outside;
  panels[i].floor.reset();
  panels[i].reason=u8"同一源图的其他楼层方案，未列入本次已确认图幅范围";
 }
}

bool productOutsideSelectedPlanWindows(const ProductJob& job,AcDbDatabase& saved,const std::string& handle){
 if(!job.architecturalPlanOnly||job.unifiedPanelScope.empty()||job.planPrintWindows.empty())return false;
 AcDbObjectId id;
 if(saved.getAcDbObjectId(id,false,AcDbHandle(bcpproduct::decodeMessage(handle).c_str()))!=Acad::eOk||id.isErased())return false;
 AcDbExtents bounds;
 if(open<AcDbEntity>(id)->getGeomExtents(bounds)!=Acad::eOk)return false;
 const auto low=bounds.minPoint(),high=bounds.maxPoint();
 const double tolerance=.01/job.unifiedUnitMM;
 return std::all_of(job.planPrintWindows.begin(),job.planPrintWindows.end(),[&](const auto& window){
  return high.x<window[0]-tolerance||low.x>window[2]+tolerance||
         high.y<window[1]-tolerance||low.y>window[3]+tolerance;
 });
}

void productAuditPlanPanels(ProductJob& job,AcDbDatabase& saved,
 const GenericNativeInput& input,const std::vector<bcpproduct::GenericView>& views,
 const std::vector<ProductPanelOwnership>& ownership,const std::vector<bool>& generated){
 if(!job.unifiedPlan)return;
 std::ostringstream out;
 out<<"{\"savedReopened\":true,\"sourceSHA256\":\""<<job.sourceHash<<"\",\"components\":[";
 for(std::size_t i=0;i<ownership.size();++i){
  const auto& panel=ownership[i];
  const auto panelNumber=i+1;
  const auto retained=[&](const std::string& handle){
   AcDbObjectId id;
   return saved.getAcDbObjectId(id,false,AcDbHandle(bcpproduct::decodeMessage(handle).c_str()))==Acad::eOk&&!id.isErased();
  };
  std::vector<std::string> walls,missing;
  for(const auto index:views[i].walls){
   const auto& handle=input.walls[index].wall.handle;
   walls.push_back(handle);
   if(!retained(handle))missing.push_back(handle);
  }
  for(const auto& title:panel.titles)if(!retained(title))missing.push_back(title);
  auto status=bcpproduct::PlanCheck::pending;
  if(missing.empty()&&((panel.role==ProductPanelRole::floor&&generated[i])||
     (panel.role==ProductPanelRole::attached&&!generated[i]&&panel.floor.has_value())))
   status=bcpproduct::PlanCheck::verified;
  if(missing.empty()&&(panel.role==ProductPanelRole::roof||panel.role==ProductPanelRole::detail||
     panel.role==ProductPanelRole::outside)&&!generated[i])
   status=bcpproduct::PlanCheck::omitted;
  auto reason=panel.reason;
  if(panel.role==ProductPanelRole::unassigned&&!generated[i]&&missing.empty()&&!walls.empty()&&
     std::all_of(walls.begin(),walls.end(),[&](const auto& handle){return productOutsideSelectedPlanWindows(job,saved,handle);})){
   status=bcpproduct::PlanCheck::omitted;
   reason=u8"源墙均在已选建筑平面打印窗口之外，重开核原句柄保留，墙组用途暂未归类";
  }
  if(!missing.empty())reason+=u8"；部分源墙或图名句柄未在重开图找到";
  if(panel.role==ProductPanelRole::floor&&!generated[i])reason+=u8"；主体生成未完成";
  productCheckPlanSource(job,panelNumber,"panel","",status,panel.titles,reason);
  std::ostringstream row;
  row<<"{\"component\":"<<panelNumber<<",\"role\":\""<<productPanelRoleName(panel.role)
     <<"\",\"floor\":";
  if(panel.floor)row<<*panel.floor;else row<<"null";
  row<<",\"parentPanel\":"<<(panel.parentPanel?std::to_string(*panel.parentPanel+1):"null")
     <<",\"generated\":"<<(generated[i]?"true":"false")<<",\"titles\":";
  productWriteHandleArray(row,panel.titles);
  row<<",\"walls\":";
  productWriteHandleArray(row,walls);
  row<<",\"missingSourceObjects\":";
  productWriteHandleArray(row,missing);
  row<<",\"reason\":"<<jsonUtf8(bcpproduct::decodeMessage(reason))
     <<",\"savedReopened\":true,\"result\":\""<<productPlanCheckName(status)<<"\"}";
  if(i)out<<',';
  out<<row.str();
  for(const auto* rule:{"P02","T07"})productRecordRuleEvidence(job,rule,row.str());
 }
 out<<"]}";
 writeNew(job.root/L"plan-panel-ownership.json",out.str());
}
