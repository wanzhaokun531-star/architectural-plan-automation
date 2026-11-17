#include "PlanAxisRangePolicy.h"
#include "ProductAxisServices.inc"

void productContinuousMainAxes(ProductJob& job,AcDbDatabase& db,const ProductPlanLayoutConfig& config){
 Working context(&db);
 const auto spacing=bcpproduct::planAnnotationLayout(config.scale,config.unitMM,config.paper);
 const auto file=job.profile/L"plan-policy/axis-ranges.tsv";
 std::map<std::string,bcpproduct::AxisRangeRule> rules;
 if(config.projectAssociations&&fs::exists(file)){std::ifstream input(file);require(bool(input),"read per-axis effective ranges");rules=bcpproduct::readAxisRangeRules(input);}
 struct Axis {std::string label,role;int panel{};bool vertical{};double coordinate{};bcpproduct::AxisRangeSpan span;};
 std::vector<Axis> axes;std::vector<std::string> pending;
 std::vector<ProductAxisServices> services;for(const auto& panel:config.panels)services.push_back(productAxisServices(db,panel));
 for(const auto& r:config.axes){
  const auto g=chainGrips(find(db,r.handle));require(g.size()==4||g.size()==8,"actual axis circle association");
  const bool vertical=r.side!=0;const auto& panel=config.panels.at(r.panel);
  const auto original=config.axisOriginalHandles.find(r.handle);
  const auto rule=rules.find(original==config.axisOriginalHandles.end()?r.handle:original->second);
  const int side=productAxisEnd(productPlacementBounds(config,r.panel),vertical,g[0],g[1]).side;
  const auto anchor=productRealAxisLead(db,config,r.handle,side,g[0]);
  const double coordinate=productAxisStation(anchor,vertical);
  const std::optional<bcpproduct::AxisRangeRule> association=rule==rules.end()||rule->second.panel!=r.panel?std::optional<bcpproduct::AxisRangeRule>{}:rule->second;
  const auto point=[&](const std::string& handle,int index)->std::optional<double>{
   const auto replaced=config.axisOriginalGrips.find(handle);
   if(replaced!=config.axisOriginalGrips.end()){
    if(index<0||static_cast<std::size_t>(index)>=replaced->second.size())return {};
    const auto& p=replaced->second[index];
    return vertical?p.y:p.x;
   }
   const std::wstring text(handle.begin(),handle.end());AcDbObjectId id;
   if(db.getAcDbObjectId(id,false,AcDbHandle(text.c_str()))!=Acad::eOk||id.isNull())return {};
   auto e=open<AcDbEntity>(id);if(e->isAProxy())return {};
   const std::wstring type=e->isA()->name();if(type!=L"TDbAxisLabelSet"&&type!=L"TDbWall"&&type!=L"TDbColumn"&&type!=L"AcDbLine")return {};
   AcGePoint3dArray p;AcDbIntArray a,b;if(e->getGripPoints(p,a,b)!=Acad::eOk||index<0||index>=p.length())return {};
   return vertical?p[index].y:p[index].x;
  };
  auto range=bcpproduct::resolveAxisRange(association,vertical?panel.ymin:panel.xmin,vertical?panel.ymax:panel.xmax,spacing.start,point);
  std::string role=association?association->role:"unresolved";
  // Explicit reviewed ranges take precedence. Retained/full-envelope records
  // are not a review: infer each from this floor's actual serving components.
  if(!association||association->role=="retained"){
   const auto& service=services.at(r.panel);const auto inferred=bcpproduct::inferAxisService(vertical,coordinate,vertical?panel.ymin:panel.xmin,vertical?panel.ymax:panel.xmax,spacing.start,service.walls,service.columns);
   range=inferred?std::optional<bcpproduct::AxisRangeSpan>(inferred->span):std::optional<bcpproduct::AxisRangeSpan>{};if(inferred)role=inferred->role;
  }
  if(!range){pending.push_back(r.handle);job.detailPending.push_back({u8"独立轴线有效范围",r.handle,u8"服务范围映射或交接点未确定；保留该轴原有可靠线段，未按整图包围框拉满，轴号和尺寸测站保持"});continue;}
  auto same=std::find_if(axes.begin(),axes.end(),[&](const auto& a){return a.panel==r.panel&&a.vertical==vertical&&std::abs(a.coordinate-coordinate)<1e-5;});
  if(same!=axes.end()){require(std::abs(same->span.lower-range->lower)<1e-5&&std::abs(same->span.upper-range->upper)<1e-5,"conflicting ranges for the same logical axis");continue;}
  axes.push_back({r.handle,role,r.panel,vertical,coordinate,*range});
 }
 // Replace only identified old segments belonging to resolved logical axes.
 // Unresolved axes and unrelated local DOTE geometry retain their source state.
 double axisScale=1;bool haveAxisScale=false;std::vector<AcDbObjectId> replace;
 for(const auto& item:modelTypes(db))if(item.second==L"AcDbLine"){
  auto e=open<AcDbLine>(find(db,ascii(item.first)));if(symbolName(e->layerId())!=L"DOTE")continue;
  const auto a=e->startPoint(),b=e->endPoint();
  for(const auto& axis:axes){const auto& p=config.panels.at(axis.panel);
   const bool belongs=axis.vertical?
    std::abs(a.x-axis.coordinate)<1e-5&&std::abs(b.x-axis.coordinate)<1e-5&&std::min(a.y,b.y)>=p.ymin-spacing.start-1e-4&&std::max(a.y,b.y)<=p.ymax+spacing.start+1e-4:
    std::abs(a.y-axis.coordinate)<1e-5&&std::abs(b.y-axis.coordinate)<1e-5&&std::min(a.x,b.x)>=p.xmin-spacing.start-1e-4&&std::max(a.x,b.x)<=p.xmax+spacing.start+1e-4;
   if(belongs){if(!haveAxisScale){axisScale=e->linetypeScale();haveAxisScale=true;}replace.push_back(e->objectId());break;}
  }
 }
 for(auto id:replace){auto e=editObject<AcDbEntity>(id);productStatus(e->erase(),"replace identified logical axis segments");}
 std::ostringstream proof;proof<<std::setprecision(17)<<"{\"schema\":\"bcp.axis-effective-ranges.v1\",\"resolved\":[";bool first=true;
 for(const auto& axis:axes){
  const AcGePoint3d a=axis.vertical?AcGePoint3d(axis.coordinate,axis.span.lower,0):AcGePoint3d(axis.span.lower,axis.coordinate,0),b=axis.vertical?AcGePoint3d(axis.coordinate,axis.span.upper,0):AcGePoint3d(axis.span.upper,axis.coordinate,0);
  auto line=std::make_unique<AcDbLine>(a,b);line->setDatabaseDefaults(&db);productStatus(line->setLayer(L"DOTE"),"effective axis layer");productStatus(line->setColorIndex(256),"effective axis ByLayer");
  {auto layer=open<AcDbLayerTableRecord>(line->layerId());productStatus(line->setLinetype(layer->linetypeObjectId()),"effective axis linetype");}
  productStatus(line->setLinetypeScale(axisScale),"preserve effective axis linetype scale");
  auto model=editObject<AcDbBlockTableRecord>(modelId(db));AcDbObjectId id;productStatus(model->appendAcDbEntity(id,line.get()),"append directly editable effective axis");line.release()->close();
  if(!first)proof<<',';first=false;proof<<"{\"axis\":\""<<axis.label<<"\",\"role\":\""<<axis.role<<"\",\"panel\":"<<axis.panel<<",\"line\":\""<<axisHandle(id)<<"\",\"vertical\":"<<(axis.vertical?"true":"false")<<",\"endpoints\":"<<wholePoints({a,b})<<'}';
 }
 proof<<"],\"unresolved\":[";for(std::size_t k=0;k<pending.size();++k){if(k)proof<<',';proof<<'\"'<<pending[k]<<'\"';}proof<<"],\"wholeEnvelopeFallback\":false,\"labelsAndDimensionStationsModified\":false}";
 writeNew(job.root/L"逐轴有效范围核验.json",proof.str());
}
