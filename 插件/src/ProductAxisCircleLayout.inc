constexpr wchar_t productAxisFoldApp[] = L"PLAN_AXIS_FOLD";
#include "PlanRadialAxisPolicy.h"

// Acquire every old object before erasing any of them. Keep database-owned
// objects open through rollback so a failed later erase cannot lose a lead or
// half of a logical axis. The operation parameter also permits native failure
// injection without modifying or mocking the stored source entities.
template<class Retire>
void productRetireAxisGroup(AcDbDatabase& db,const std::vector<AcDbObjectId>& ids,Retire retire){
 std::vector<std::unique_ptr<AcDbEntity,Close<AcDbEntity>>> previous;std::set<AcDbObjectId> unique;
 for(const auto id:ids)if(unique.insert(id).second){
  require(!id.isNull()&&!id.isErased()&&id.database()==&db,"source axis group member is missing or belongs to another drawing");
  previous.push_back(editObject<AcDbEntity>(id));
 }
 try{for(auto& object:previous)status(retire(*object),"retire fully verified axis group");}
 catch(...){
  const auto failure=std::current_exception();bool restored=true;
  for(auto& object:previous)if(object->isErased())restored&=object->erase(false)==Acad::eOk;
  require(restored,"axis group erase failed and native rollback could not restore every previous member");
  std::rethrow_exception(failure);
 }
}
void productRetireAxisGroup(AcDbDatabase& db,const std::vector<AcDbObjectId>& ids){
 productRetireAxisGroup(db,ids,[](AcDbEntity& entity){return entity.erase();});
}

void productExpectPlanAxis(ProductPlanLayoutConfig& c,int panel,bool vertical,const std::string& number,
                          double station,bool negative,bool positive){
 if(number.empty()||(!negative&&!positive))return;
 require(std::isfinite(station),"finite expected real axis station");
 const auto existing=std::find_if(c.expectedAxes.begin(),c.expectedAxes.end(),[&](const auto& row){
  return row.panel==panel&&row.vertical==vertical&&row.number==number;
 });
 if(existing!=c.expectedAxes.end()){
  require(std::abs(existing->station-station)<.01&&existing->negative==negative&&existing->positive==positive,
          "conflicting expected axis identity or visible sides");return;
 }
 c.expectedAxes.push_back({panel,vertical,number,station,negative,positive,{},{},{}});
}

void productBindExpectedPlanAxis(ProductPlanLayoutConfig& c,int panel,bool vertical,const std::string& number,
                                const std::string& handle){
 const auto row=std::find_if(c.expectedAxes.begin(),c.expectedAxes.end(),[&](const auto& item){
  return item.panel==panel&&item.vertical==vertical&&item.number==number;
 });
 require(row!=c.expectedAxes.end(),"expected axis must be frozen before source handle is bound");row->original=handle;
}

double productAxisStation(const AcGePoint3d& point,bool vertical){
 return vertical?point.x:point.y;
}

AcGePoint3d productAxisPoint(AcGePoint3d point,bool vertical,double normal,double station){
 if(vertical){point.x=station;point.y=normal;}
 else{point.x=normal;point.y=station;}
 return point;
}

std::vector<AcGePoint3d> productFoldPoints(AcDbDatabase& db,const ProductPlanLayoutConfig& c,
        const std::string& handle,int side){
 const auto found=c.axisFolds.find({handle,side});
 if(found==c.axisFolds.end())return {};
 const auto fold=open<AcDbPolyline>(find(db,found->second));
 require(fold->visibility()==AcDb::kVisible&&!fold->isClosed()&&(fold->numVerts()==2||fold->numVerts()==3),
         "visible associated axis leader");
 auto* marker=fold->xData(productAxisFoldApp);
 const std::wstring expected(handle.begin(),handle.end());
 const bool linked=marker&&marker->rbnext&&marker->rbnext->restype==AcDb::kDxfXdHandle&&
      marker->rbnext->resval.rstring&&expected==marker->rbnext->resval.rstring&&
      marker->rbnext->rbnext&&marker->rbnext->rbnext->resval.rint==side;
 if(marker)acutRelRb(marker);
 require(linked,"fold must reference its native axis label and side");
 std::vector<AcGePoint3d> points(fold->numVerts());
 for(unsigned i=0;i<points.size();++i)status(fold->getPointAt(i,points[i]),"read saved axis fold vertex");
 return points;
}

AcGePoint3d productRealAxisLead(AcDbDatabase& db,const ProductPlanLayoutConfig& c,
        const std::string& handle,int side,const AcGePoint3d& nativeLead){
 const auto points=productFoldPoints(db,c,handle,side);
 if(points.empty())return nativeLead;
 require(points.back().distanceTo(nativeLead)<1e-5,"axis fold must meet its native label lead");
 return points.front();
}

double productAxisFoldBandStart(AcDbDatabase& db,const ProductPlanLayoutConfig& c,
        int panel,int side,const bcpproduct::PlanAnnotationLayout& spacing,
        const std::function<AcDbObjectId(const std::string&)>& id){
 const auto sign=side%2?1:-1;
 double outer=sign*productPlacementDatum(c,panel,side)+spacing.total;
 for(const auto& dim:c.dimensions){
  if(dim.panel!=panel||dim.side!=side)continue;
  AcDbExtents extent;
  const auto entity=open<AcDbEntity>(id(dim.handle));
  status(entity->getGeomExtents(extent),"read dimension bounds before placing axis fold");
  const auto point=sign>0?extent.maxPoint():extent.minPoint();
  outer=std::max(outer,sign*(side<2?point.x:point.y));
 }
 return outer;
}

std::pair<double,double> productAxisCircleLimits(AcDbDatabase& db,const ProductPlanBounds& panel,
        bool vertical,double normal,double radius){
 const double infinity=std::numeric_limits<double>::infinity();
 std::pair<double,double> limits{-infinity,infinity};
 double smallest=infinity;
 for(const auto& item:modelTypes(db)){
  if(item.second!=L"AcDbPolyline")continue;
  const auto frame=open<AcDbPolyline>(find(db,ascii(item.first)));
  if(!frame->isClosed()||frame->numVerts()!=4||symbolName(frame->layerId())!=L"楼层范围")continue;
  AcDbExtents box;
  if(frame->getGeomExtents(box)!=Acad::eOk)continue;
  const auto lo=box.minPoint(),hi=box.maxPoint();
  if(lo.x>panel.xmin||lo.y>panel.ymin||hi.x<panel.xmax||hi.y<panel.ymax)continue;
  if(productAxisNormal(lo,vertical)>normal-radius||productAxisNormal(hi,vertical)<normal+radius)continue;
  const auto area=(hi.x-lo.x)*(hi.y-lo.y);
  if(area>=smallest)continue;
  smallest=area;
  limits={productAxisStation(lo,vertical)+radius,productAxisStation(hi,vertical)-radius};
 }
 return limits;
}

AcDbObjectId productCloneAxisLabel(AcDbDatabase& db,AcDbObjectId source){
 const auto entity=open<AcDbEntity>(source);
 std::unique_ptr<AcDbEntity> clone(AcDbEntity::cast(entity->clone()));
 require(clone&&std::wstring(clone->isA()->name())==L"TDbAxisLabelSet","real native axis label clone");
 auto model=editObject<AcDbBlockTableRecord>(modelId(db));
 AcDbObjectId id;
 status(model->appendAcDbEntity(id,clone.get()),"append native axis label");
 status(clone.release()->close(),"close native axis label");
 return id;
}

AcDbObjectId productAxisLabelWithTail(AcDbDatabase& db,AcDbObjectId label,const AcGePoint3d& target){
 const auto before=approvedgrips::readGeometry(label);
 auto expected=before;
 require(before.grips.size()==4&&before.stretch.size()==2,"single-ended native axis for folded tail");
 const auto lead=before.grips[0];
 require(std::count_if(before.stretch.begin(),before.stretch.end(),[&](const auto& p){
  return p.distanceTo(lead)<approvedgrips::eps;
 })==1,"unique native tail stretch point");
 expected.grips[0]=target;
 for(auto& p:expected.stretch)if(p.distanceTo(lead)<approvedgrips::eps)p=target;
 const auto view=approvedgrips::view();
 int count=1;
 const auto attempt=[&](int index,int second,const AcGeVector3d& correction){
  const auto trial=productCloneAxisLabel(db,label);
  bool keep=false;
  struct Dispose {AcDbObjectId id;bool& keep;~Dispose(){
   if(!keep){AcDbEntity* entity{};if(acdbOpenObject(entity,id,AcDb::kForWrite)==Acad::eOk){entity->erase();entity->close();}}
  }} dispose{trial,keep};
  auto result=approvedgrips::apply(db,trial,index,second,0,before,expected,view,true,correction);
  count=result.count;
  keep=result.committed&&approvedgrips::same(approvedgrips::readGeometry(trial),expected);
  return std::make_pair(keep?trial:AcDbObjectId::kNull,result);
 };
 std::ostringstream diagnostic;
 diagnostic<<"native folded tail could not be shortened; expected "<<approvedgrips::json(expected);
 for(int index=0;index<count;++index){
  AcGeVector3d correction(0,0,0);
  auto [changed,result]=attempt(index,-1,correction);
  if(!changed.isNull())return changed;
  diagnostic<<"; grip "<<index<<", status "<<result.status<<", after "<<approvedgrips::json(result.after);
  if(!result.innerMatched&&result.status==Acad::eOk&&result.after.grips.size()==before.grips.size()&&
     result.after.grips[0].distanceTo(lead)>approvedgrips::eps){
   correction=target-result.after.grips[0];
   std::tie(changed,result)=attempt(index,-1,correction);
   if(!changed.isNull())return changed;
  }
  if(result.innerMatched)for(int second=0;second<count;++second){
   std::tie(changed,result)=attempt(index,second,correction);
   if(!changed.isNull())return changed;
  }
 }
 throw std::runtime_error(diagnostic.str());
}

struct ProductAxisGlyph {std::wstring text;AcGePoint3d anchor;};
void productAxisCaptionLeaves(const AcDbEntity& entity,std::vector<ProductAxisGlyph>& text,int depth=0){
 require(depth<=6,"axis caption nesting");
 if(const auto* leaf=AcDbText::cast(&entity)){AcString value;status(leaf->textString(value),"read native axis caption");
  text.push_back({value.kACharPtr(),leaf->isDefaultAlignment()?leaf->position():leaf->alignmentPoint()});return;}
 if(AcDbCircle::cast(&entity)||AcDbLine::cast(&entity))return;
 require(std::wstring(entity.isA()->name())==L"TDbAxisLabelSet"||std::wstring(entity.isA()->name())==L"TDbSymbDrawingIndex",
         "unsupported axis caption object");
 TemplateParts parts(&entity);status(entity.explode(parts.values),"read native axis display");
 for(int i=0;i<parts.values.length();++i){const auto* child=AcDbEntity::cast(static_cast<AcRxObject*>(parts.values[i]));
  require(child&&child!=&entity&&child->database()==nullptr,"native axis display ownership");productAxisCaptionLeaves(*child,text,depth+1);}
}
std::wstring productAxisCaptionAt(AcDbObjectId id,const AcGePoint3d& centre){
 const auto entity=open<AcDbEntity>(id);AxisInk ink;axisInkLeaves(*entity,ink);
 const auto circle=std::find_if(ink.circles.begin(),ink.circles.end(),[&](const auto& row){return row.centre.distanceTo(centre)<.01;});
 require(circle!=ink.circles.end(),"native axis caption circle absent");
 std::vector<ProductAxisGlyph> all,captionLeaves;productAxisCaptionLeaves(*entity,all);
 for(const auto& value:all)if(value.anchor.distanceTo(centre)<=circle->radius*1.5)captionLeaves.push_back(value);
 if(captionLeaves.size()==1){require(!captionLeaves[0].text.empty()&&captionLeaves[0].text.find(L'/')==std::wstring::npos,"literal slash text is not a native attached axis");return captionLeaves[0].text;}
 require(captionLeaves.size()==2,"native axis caption cannot be unambiguously assigned to its circle");
 std::sort(captionLeaves.begin(),captionLeaves.end(),[](const auto& a,const auto& b){return a.anchor.y>b.anchor.y;});
 const auto digits=[](const std::wstring& value){return !value.empty()&&std::all_of(value.begin(),value.end(),[](wchar_t c){return c>=L'0'&&c<=L'9';});};
 const bool primary=digits(captionLeaves[1].text)||(!captionLeaves[1].text.empty()&&std::all_of(captionLeaves[1].text.begin(),captionLeaves[1].text.end(),[](wchar_t c){return c>=L'A'&&c<=L'Z';}));
 const bool divider=std::any_of(ink.lines.begin(),ink.lines.end(),[&](const auto& line){
  return std::abs(line.first.y-centre.y)<.01&&std::abs(line.second.y-centre.y)<.01&&
    std::abs(line.first.distanceTo(centre)-circle->radius)<.01&&std::abs(line.second.distanceTo(centre)-circle->radius)<.01&&
    (line.first+(line.second-line.first)*.5).distanceTo(centre)<.01;
 });
 require(digits(captionLeaves[0].text)&&primary&&captionLeaves[0].anchor.y>centre.y&&captionLeaves[1].anchor.y<centre.y&&divider,
         "compound caption is not a verified native fractional axis");
 return captionLeaves[0].text+L"/"+captionLeaves[1].text;
}
std::wstring productAxisCaption(AcDbObjectId id){
 const auto g=chainGrips(id);require(g.size()==4||g.size()==8,"native axis caption end geometry");
 const auto caption=productAxisCaptionAt(id,g[1]);
 for(std::size_t k=4;k<g.size();k+=4)require(productAxisCaptionAt(id,g[k+1])==caption,"native axis ends have distinct business numbers");
 return caption;
}
std::size_t productAxisExternalStrokeCount(const AxisInk& ink,const AcGePoint3d& centre,double radius){
 return static_cast<std::size_t>(std::count_if(ink.lines.begin(),ink.lines.end(),[&](const auto& line){
  return line.first.distanceTo(centre)>radius+.01||line.second.distanceTo(centre)>radius+.01;
 }));
}

AcDbObjectId productOrientNativeAxis(AcDbDatabase& db,AcDbObjectId prototype,
                                    const AcGePoint3d& lead,const AcGePoint3d& centre){
 const auto original=chainGrips(prototype);require(original.size()==4,"single-ended native axis prototype");
 const auto source=original[0]-original[1],target=lead-centre;
 require(source.length()>1e-5&&target.length()>original[1].distanceTo(original[2])+1e-5,
         "native radial lead starts outside its circle");
 const auto caption=productAxisCaption(prototype);
 auto candidate=productCloneAxisLabel(db,prototype);
 try{
  const double angle=std::atan2(target.y,target.x)-std::atan2(source.y,source.x);
  {auto entity=editObject<AcDbEntity>(candidate);
   status(entity->transformBy(AcGeMatrix3d::rotation(angle,AcGeVector3d::kZAxis,original[1])),"orient native radial axis tail");
   status(entity->transformBy(AcGeMatrix3d::translation(centre-original[1])),"position native axis circle");}
  const auto positioned=chainGrips(candidate);require(positioned.size()==4,"rotated native axis must retain its public end geometry");
  if(positioned[0].distanceTo(lead)>.00001){
   const auto fitted=productAxisLabelWithTail(db,candidate,lead);
   status(editObject<AcDbEntity>(candidate)->erase(),"retire unfitted native axis candidate");candidate=fitted;
  }
  const auto actual=chainGrips(candidate);
  require(actual.size()==4&&actual[0].distanceTo(lead)<.00001&&actual[1].distanceTo(centre)<.00001&&
          std::abs(actual[1].distanceTo(actual[2])-original[1].distanceTo(original[2]))<.00001&&
          productAxisCaption(candidate)==caption,"native axis candidate changed identity or circle geometry");
  AxisInk ink;axisInkLeaves(*open<AcDbEntity>(candidate),ink);
  const auto edge=centre+target.normal()*original[1].distanceTo(original[2]);
  require(ink.circles.size()==1&&productAxisExternalStrokeCount(ink,centre,original[1].distanceTo(original[2]))==1&&
          std::any_of(ink.lines.begin(),ink.lines.end(),[&](const auto& line){return
           (line.first.distanceTo(lead)<.00001&&line.second.distanceTo(edge)<.00001)||
           (line.second.distanceTo(lead)<.00001&&line.first.distanceTo(edge)<.00001);}),
          "native axis tail must draw one straight radial segment to the circle");
  return candidate;
 }catch(...){if(!candidate.isNull()&&!candidate.isErased())editObject<AcDbEntity>(candidate)->erase();throw;}
}

class ProductAxisTemplateBank {
 AcDbDatabase source_{false,true};
 std::map<std::wstring,AcDbObjectId> donors_;
public:
 explicit ProductAxisTemplateBank(const fs::path& path){
  require(sha(path)=="3C6B19FAE21C1C5729D0E0000104DCADD9BA7A81560A31D74F0D576B2BD0DD2F","approved axis template fingerprint");
  status(source_.readDwgFile(path.c_str(),AcDbDatabase::kForReadAndAllShare,false),"read signed axis template");
  status(source_.closeInput(true),"close signed axis template");
  Working work(&source_);
  for(const auto& [raw,type]:modelTypes(source_))if(type==L"TDbAxisLabelSet"){
   const auto id=find(source_,ascii(raw));if(chainGrips(id).size()!=4)continue;
   try{donors_.emplace(productAxisCaption(id),id);}catch(const std::exception&){}
  }
  require(!donors_.empty(),"signed template has no readable native single axis");
 }
 std::string donorHandle(const std::wstring& number)const{
  auto donor=donors_.find(number);if(donor==donors_.end())donor=donors_.find(L"A");
  if(donor==donors_.end())donor=donors_.begin();return axisHandle(donor->second);
 }
 AcDbObjectId create(AcDbDatabase& target,const std::wstring& number,
                     const AcGePoint3d& lead,const AcGePoint3d& centre,AcDbObjectId layer,
                     const std::wstring& preferredDonor={}){
  auto donor=donors_.find(preferredDonor.empty()?number:preferredDonor);if(donor==donors_.end())donor=donors_.find(L"A");
  if(donor==donors_.end())donor=donors_.begin();
  AcDbObjectIdArray ids;ids.append(donor->second);AcDbIdMapping mapping;
  status(source_.wblockCloneObjects(ids,modelId(target),mapping,AcDb::kDrcReplace,false),"import axis template with template-first dependencies");
  AcDbIdPair pair;pair.setKey(donor->second);require(mapping.compute(pair)&&pair.isCloned(),"axis template clone identity");
  auto material=pair.value();AcDbObjectId candidate;
  try{
   if(donor->first!=number){
    if(!acedRegCmds->lookupGlobalCmd(L"TReAxisNo")){
     const auto module=fs::path(productSetting(L"tangentDirectory",L"C:\\Tangent\\TArchT20V8\\SYS23x64"))/L"tch_kerncmd.arx";
     require(acrxDynamicLinker->loadModule(module.c_str(),false,false),"load native axis business numbering");
    }
    const auto before=chainGrips(material);wholeStagingView({before[1]});ads_name selection{};
    status(acdbGetAdsName(selection,material),"select template axis by identity");
    require(acedCommandS(RTSTR,L"TReAxisNo",RTENAME,selection,RTSTR,number.c_str(),RTSTR,L"",RTNONE)==RTNORM,"native template axis business number");
    require(productAxisCaption(material)==number&&approvedgrips::same(before,chainGrips(material)),"native renumber changed geometry");
   }
   candidate=productOrientNativeAxis(target,material,lead,centre);
   {auto e=editObject<AcDbEntity>(candidate);status(e->setLayer(layer),"native axis target layer");status(e->setColorIndex(256),"native axis ByLayer");status(e->setVisibility(AcDb::kVisible),"show native axis candidate");}
   status(editObject<AcDbEntity>(material)->erase(),"retire temporary axis donor");return candidate;
  }catch(...){if(!material.isErased())editObject<AcDbEntity>(material)->erase();if(!candidate.isNull()&&!candidate.isErased())editObject<AcDbEntity>(candidate)->erase();throw;}
 }
};

void productBuildExpectedPlanAxes(ProductJob& job,AcDbDatabase& db,ProductPlanLayoutConfig& c,
                                 AcDbObjectId layer,const bcpproduct::PlanAnnotationLayout& spacing){
 ProductAxisTemplateBank bank(job.profile/L"annotation-template/平面标注.dwg");
 std::vector<ProductPlanRole> roles;
 std::set<std::string> replaced;
 for(auto& expected:c.expectedAxes){
  std::vector<AcDbObjectId> candidates;expected.outputs.clear();expected.issue.clear();expected.pendingSources.clear();
  try{
   require(expected.panel>=0&&static_cast<std::size_t>(expected.panel)<c.panels.size(),"expected axis panel exists");
   const auto panel=productPlacementBounds(c,expected.panel);
   for(const bool positive:{false,true}){
    if(!(positive?expected.positive:expected.negative))continue;
    const int side=(expected.vertical?2:0)+(positive?1:0);const double sign=positive?1.:-1.;
    const double datum=productPlacementDatum(c,expected.panel,side);
    const AcGePoint3d anchor=expected.vertical?AcGePoint3d(expected.station,datum+sign*spacing.start,0):
                                                     AcGePoint3d(datum+sign*spacing.start,expected.station,0);
    const auto centre=productAxisAtNormal(anchor,expected.vertical,datum+sign*spacing.circleCentre);
    const auto id=bank.create(db,bcpproduct::decodeMessage(expected.number),anchor,centre,layer);candidates.push_back(id);
    const auto grips=chainGrips(id);
    require(std::abs(grips[1].distanceTo(grips[2])-spacing.circleDiameter/2)<.01,"template circle radius matches plan scale");
   }
   require(candidates.size()==static_cast<std::size_t>(expected.negative+expected.positive),"all expected visible axis ends built");
   std::set<std::string> retiring(expected.sourceLabels.begin(),expected.sourceLabels.end());
   if(!expected.original.empty())retiring.insert(expected.original);
   for(const auto& handle:retiring){AcDbObjectId id;const auto value=bcpproduct::decodeMessage(handle);
    require(db.getAcDbObjectId(id,false,AcDbHandle(value.c_str()))==Acad::eOk&&!id.isErased(),"frozen source axis disappeared before candidate staging");
    c.axisOriginalGrips[handle]=chainGrips(id);expected.pendingSources.push_back(handle);}
   // The new ends are staging material. Joint A28 arrangement still has to
   // succeed on both sides before productCommitExpectedPlanAxes retires source.
   for(const auto& handle:expected.sourceLabels)replaced.insert(handle);
   if(!expected.original.empty())replaced.insert(expected.original);
   const auto identity=expected.original.empty()?axisHandle(candidates.front()):expected.original;
   if(expected.original.empty())expected.original=identity;
   for(const auto candidate:candidates){const auto handle=axisHandle(candidate);expected.outputs.push_back(handle);
    roles.push_back({handle,expected.panel,expected.vertical?1:0,0});c.axisOriginalHandles[handle]=identity;}
  }catch(const std::exception& error){
   for(const auto candidate:candidates)if(!candidate.isErased())editObject<AcDbEntity>(candidate)->erase();
   expected.issue=error.what();
   expected.pendingSources.clear();
   if(!expected.original.empty())c.axisLayoutFailures[expected.original]=expected.issue;
   for(const auto& handle:expected.sourceLabels)c.axisLayoutFailures[handle]=expected.issue;
   const int source=static_cast<std::size_t>(expected.panel)<job.layoutSourcePanels.size()?job.layoutSourcePanels[expected.panel]:expected.panel+1;
   productRecordPlanAxisFailure(job,source,expected.number,expected.original,
       std::string("template-native candidate failed; retained previous axis if present: ")+expected.issue);
  }
 }
 for(const auto& role:c.axes)if(!replaced.count(role.handle))roles.push_back(role);
 c.axes=std::move(roles);
}

void productWriteExpectedPlanAxes(ProductJob& job,const ProductPlanLayoutConfig& c){
 std::ostringstream out;out<<std::setprecision(17)<<"{\"contract\":\"plan.expected-axis-ends.v1\",\"axes\":[";
 bool first=true;
 for(const auto& row:c.expectedAxes){if(!first)out<<',';first=false;
  out<<"{\"panel\":"<<row.panel+1<<",\"vertical\":"<<(row.vertical?"true":"false")<<",\"number\":"<<jsonUtf8(bcpproduct::decodeMessage(row.number))
     <<",\"station\":"<<row.station<<",\"negative\":"<<(row.negative?"true":"false")<<",\"positive\":"<<(row.positive?"true":"false")
     <<",\"original\":\""<<row.original<<"\",\"outputs\":[";bool firstHandle=true;
  const auto identity=row.original.empty()?(row.outputs.empty()?std::string{}:row.outputs.front()):row.original;
  for(const auto& role:c.axes){const auto root=c.axisOriginalHandles.find(role.handle);
   if(role.panel!=row.panel||(root==c.axisOriginalHandles.end()?role.handle:root->second)!=identity)continue;
   if(!firstHandle)out<<',';firstHandle=false;out<<'"'<<role.handle<<'"';}
  out<<"],\"issue\":"<<jsonUtf8(bcpproduct::decodeMessage(row.issue))<<'}';
 }
 out<<"]}";writeNew(job.root/L"axis-expectations.json",out.str());
}

// Public DWG dictionary evidence survives save/reopen and final template replacement.
// It records expected ends independently of whether a native object exists.
struct ProductStoredAxisEnd {
 int panel{},side{};std::wstring number;double station{};
 AcGePoint3d anchor,lead,centre;std::string handle;bool bent{};
};
constexpr wchar_t productAxisExpectedRecord[]=L"PLAN_DRAW_EXPECTED_AXIS_ENDS_V1";
void productSaveAxisExpectedEnds(AcDbDatabase& db,const std::vector<ProductStoredAxisEnd>& ends){
 AcDbDictionary* raw{};status(db.getNamedObjectsDictionary(raw,AcDb::kForWrite),"open axis expectations dictionary");
 std::unique_ptr<AcDbDictionary,Close<AcDbDictionary>> dictionary(raw);AcDbObjectId previous;
 if(dictionary->getAt(productAxisExpectedRecord,previous)==Acad::eOk)status(editObject<AcDbXrecord>(previous)->erase(),"replace axis expectations record");
 auto record=std::make_unique<AcDbXrecord>();AcDbObjectId added;
 status(dictionary->setAt(productAxisExpectedRecord,record.get(),added),"register axis expectations");
 std::unique_ptr<AcDbXrecord,Close<AcDbXrecord>> resident(record.release());
 std::wostringstream table;table.imbue(std::locale::classic());table<<std::setprecision(17);
 for(const auto& row:ends){table<<row.panel<<L' '<<row.side<<L' '<<std::quoted(row.number)<<L' '<<row.station<<L' '<<row.bent<<L' '
   <<std::quoted(bcpproduct::decodeMessage(row.handle));
  for(const auto& p:{row.anchor,row.lead,row.centre})table<<L' '<<p.x<<L' '<<p.y<<L' '<<p.z;table<<L'\n';}
 const auto text=table.str();resbuf data{};data.restype=AcDb::kDxfText;data.resval.rstring=const_cast<wchar_t*>(text.c_str());
 status(resident->setFromRbChain(data),"persist frozen real axis ends");
}
std::vector<ProductStoredAxisEnd> productReadAxisExpectedEnds(AcDbDatabase& db){
 AcDbDictionary* raw{};status(db.getNamedObjectsDictionary(raw,AcDb::kForRead),"open frozen axis expectations");
 std::unique_ptr<AcDbDictionary,Close<AcDbDictionary>> dictionary(raw);AcDbObjectId id;
 if(dictionary->getAt(productAxisExpectedRecord,id)!=Acad::eOk)return {};
 resbuf* data{};status(open<AcDbXrecord>(id)->rbChain(&data,&db),"read frozen expected axis ends");
 std::unique_ptr<resbuf,decltype(&acutRelRb)> chain(data,&acutRelRb);
 require(chain&&chain->restype==AcDb::kDxfText&&chain->resval.rstring,"frozen axis table format");
 std::wistringstream input(chain->resval.rstring);input.imbue(std::locale::classic());std::vector<ProductStoredAxisEnd> result;
 ProductStoredAxisEnd row;std::wstring handle;
 while(input>>row.panel>>row.side>>std::quoted(row.number)>>row.station>>row.bent>>std::quoted(handle)){
  for(auto* p:{&row.anchor,&row.lead,&row.centre})input>>p->x>>p->y>>p->z;
  require(input&&row.panel>=0&&row.side>=0&&row.side<4&&!row.number.empty(),"invalid frozen expected axis end");
  row.handle=productUtf8(handle);result.push_back(row);
 }
 require(input.eof(),"truncated frozen expected axis table");return result;
}
ProductPlanLayoutConfig productFindAxisFolds(AcDbDatabase& db){
 ProductPlanLayoutConfig result;
 for(const auto& [raw,type]:modelTypes(db))if(type==L"AcDbPolyline"){
  const auto e=open<AcDbEntity>(find(db,ascii(raw)));auto* data=e->xData(productAxisFoldApp);
  std::unique_ptr<resbuf,decltype(&acutRelRb)> chain(data,&acutRelRb);
  if(!data||!data->rbnext||data->rbnext->restype!=AcDb::kDxfXdHandle||!data->rbnext->resval.rstring||
      !data->rbnext->rbnext||data->rbnext->rbnext->restype!=AcDb::kDxfXdInteger16)continue;
  const int side=data->rbnext->rbnext->resval.rint;if(side<0||side>3)continue;
  result.axisFolds[{productUtf8(data->rbnext->resval.rstring),side}]=ascii(raw);
 }return result;
}
void productFreezeExpectedAxisEnds(AcDbDatabase& db,const ProductPlanLayoutConfig& c,
                                  const bcpproduct::PlanAnnotationLayout& spacing){
 std::vector<ProductStoredAxisEnd> rows;
 for(const auto& expected:c.expectedAxes)for(const bool positive:{false,true}){
  if(!(positive?expected.positive:expected.negative))continue;
  ProductStoredAxisEnd row;row.panel=expected.panel;row.side=(expected.vertical?2:0)+(positive?1:0);
  row.number=bcpproduct::decodeMessage(expected.number);row.station=expected.station;
  const double sign=positive?1:-1,datum=productPlacementDatum(c,expected.panel,row.side);
  row.anchor=productAxisPoint({},expected.vertical,datum+sign*spacing.start,expected.station);row.lead=row.anchor;
  row.centre=productAxisAtNormal(row.anchor,expected.vertical,datum+sign*spacing.circleCentre);
  // A failed replacement is not a missing end while its previous native object
  // still exists. Retain that identity so the later missing-end repair cannot
  // create a duplicate beside a legally retained source object.
  if(expected.outputs.empty()){const auto source=expected.sourceEndHandles.find(row.side);
   row.handle=source==expected.sourceEndHandles.end()?expected.original:source->second;}
  for(const auto& handle:expected.outputs){
   const auto id=find(db,handle);const auto g=chainGrips(id);
   for(std::size_t k=0;k<g.size();k+=4){
    if(productAxisEnd(productPlacementBounds(c,expected.panel),expected.vertical,g[k],g[k+1]).side!=row.side)continue;
    row.handle=handle;const auto fold=productFoldPoints(db,c,handle,row.side);row.bent=!fold.empty();
    row.anchor=fold.empty()?g[k]:fold.front();row.lead=g[k];row.centre=g[k+1];
   }
  }
  rows.push_back(row);
 }
 productSaveAxisExpectedEnds(db,rows);
}

void productTraceAxisLayout(ProductJob* job,const std::string& handle,const char* stage){
 if(!job||!job->validation)return;
 std::ofstream trace(job->root/L"axis-layout-native-steps.tsv",std::ios::app);
 trace<<GetTickCount64()<<'\t'<<handle<<'\t'<<stage<<'\n';
}

void productKeepAxisLabelEnd(AcDbDatabase& db,AcDbObjectId label,bool vertical,const AcGePoint3d& wanted,bool isolate=true,
                            ProductJob* job=nullptr){
 require(curDoc()&&curDoc()->database()==&db,"axis side editing needs owned active document");
 const auto staging=isolate?AcGeVector3d(200000,200000,0):AcGeVector3d::kIdentity;
 if(isolate){
  {auto entity=editObject<AcDbEntity>(label);status(entity->transformBy(AcGeMatrix3d::translation(staging)),"isolate native side edit");}
  wholeStagingView({wanted+staging});
  require(acedCommandS(RTSTR,L"_.REGEN",RTNONE)==RTNORM,"refresh isolated axis before side edit");
 }
 const auto target=wanted+staging;
 for(int attempt=0;attempt<4;++attempt){
  const auto g=chainGrips(label);
  if(g.size()==4&&g[1].distanceTo(target)<1e-4){
   if(isolate){
    auto entity=editObject<AcDbEntity>(label);
    status(entity->transformBy(AcGeMatrix3d::translation(-staging)),"restore retained axis end");
   }
   return;
  }
  require(g.size()==4||g.size()==8,"native axis side grips");
  const auto radius=g[1].distanceTo(g[2]);
  require(radius>0,"native axis circle selection radius");
  const auto pick=g[1]+(vertical?AcGeVector3d(radius,0,0):AcGeVector3d(0,radius,0));
  productTraceAxisLayout(job,axisHandle(label),"side-edit-enter");
  if(isolate)approvedCommandPoint(L"TObjEdit",pick,L"M");
  else{
   ads_name selected{};
   status(acdbGetAdsName(selected,label),"select exact native axis for side edit");
   wholeStagingView({pick});
   require(acedCommandS(RTSTR,L"TObjEdit",RTENAME,selected,RTSTR,L"M",RTSTR,L"",RTNONE)==RTNORM,
           "edit native axis side by object identity");
  }
  productTraceAxisLayout(job,axisHandle(label),"side-edit-returned");
  require(acedCommandS(RTSTR,L"_.REGEN",RTNONE)==RTNORM,"refresh native axis side edit");
  productTraceAxisLayout(job,axisHandle(label),"side-regen-returned");
 }
 throw std::runtime_error("native axis side edit did not preserve requested end "+axisHandle(label)+
     " expected "+wholePoints({target})+" actual "+wholePoints(chainGrips(label)));
}

AcDbObjectId productAppendAxisFold(AcDbDatabase& db,AcDbObjectId label,AcDbObjectId layer,int side,
        const std::vector<AcGePoint3d>& points){
 auto polyline=std::make_unique<AcDbPolyline>(3);
 polyline->setDatabaseDefaults(&db);
 status(polyline->setLayer(layer),"axis fold layer");
 status(polyline->setColorIndex(256),"axis fold by layer");
 polyline->setElevation(points.front().z);
 for(unsigned i=0;i<points.size();++i)
  status(polyline->addVertexAt(i,{points[i].x,points[i].y}),"axis fold point");
 {auto table=open<AcDbRegAppTable>(db.regAppTableId());if(!table->has(productAxisFoldApp)){
   table.reset();auto edit=editObject<AcDbRegAppTable>(db.regAppTableId());
   auto app=std::make_unique<AcDbRegAppTableRecord>();
   status(app->setName(productAxisFoldApp),"axis fold application");
   AcDbObjectId id;status(edit->add(id,app.get()),"register axis fold application");
   status(app.release()->close(),"close axis fold application");
 }}
 auto model=editObject<AcDbBlockTableRecord>(modelId(db));
 AcDbObjectId id;
 status(model->appendAcDbEntity(id,polyline.get()),"append editable axis fold");
 auto* entity=polyline.release();
 bool linked=false;
 struct ClosePolyline{AcDbPolyline* value;bool& linked;~ClosePolyline(){if(!linked)value->erase();value->close();}} close{entity,linked};
 const auto handle=axisHandle(label);
 const std::wstring owner(handle.begin(),handle.end());
 auto* xdata=acutBuildList(AcDb::kDxfRegAppName,productAxisFoldApp,AcDb::kDxfXdHandle,owner.c_str(),
                         AcDb::kDxfXdInteger16,side,RTNONE);
 require(xdata!=nullptr,"allocate native fold association");
 const auto set=entity->setXData(xdata);
 acutRelRb(xdata);
 status(set,"attach fold to native axis label");
 linked=true;
 return id;
}

bool productStoredAxisEndMatches(AcDbDatabase& db,const ProductStoredAxisEnd& row,
        const ProductPlanLayoutConfig& folds,std::string& reason){
 try{
  AcDbObjectId id;const auto handle=bcpproduct::decodeMessage(row.handle);
  require(!row.handle.empty()&&db.getAcDbObjectId(id,false,AcDbHandle(handle.c_str()))==Acad::eOk&&!id.isErased(),"expected native axis end is missing");
  const auto entity=open<AcDbEntity>(id);
  require(!entity->isAProxy()&&std::wstring(entity->isA()->name())==L"TDbAxisLabelSet"&&entity->visibility()==AcDb::kVisible,"expected axis is not a visible true native label");
  const auto g=chainGrips(id);require(g.size()==4||g.size()==8,"expected axis grips unreadable");
  const bool vertical=row.side>=2;const double sign=row.side%2?1:-1;
  require(std::abs(productAxisStation(row.anchor,vertical)-row.station)<.01,"frozen anchor differs from true station");
  const auto circle=std::find_if(g.begin(),g.end(),[&](const auto& point){return point.distanceTo(row.centre)<.01;});
  require(circle!=g.end(),"expected axis circle position changed");
  bool found=false;for(std::size_t k=0;k<g.size();k+=4)if(g[k].distanceTo(row.lead)<.01&&g[k+1].distanceTo(row.centre)<.01){
   require(std::abs(g[k+1].distanceTo(g[k+2])-400.)<.01,"template native axis circle size changed");found=true;}
  require(found&&productAxisCaption(id)==row.number,"expected native axis geometry or number changed");
  const auto points=productFoldPoints(db,folds,row.handle,row.side);
  if(row.bent)require(points.size()==2&&points.front().distanceTo(row.anchor)<.01&&points.back().distanceTo(row.lead)<.01&&
    std::abs(productAxisStation(row.lead,vertical)-row.station)<.01&&
    sign*(productAxisNormal(row.lead,vertical)-productAxisNormal(row.anchor,vertical))>.01&&
    std::abs(std::abs(row.centre.x-row.lead.x)-std::abs(row.centre.y-row.lead.y))<.01,"expected single-bend connector missing or altered");
  else require(points.empty()&&row.anchor.distanceTo(row.lead)<.01&&std::abs(productAxisStation(row.centre,vertical)-row.station)<.01,"straight expected axis left its real station");
  AxisInk ink;axisInkLeaves(*entity,ink);const auto edge=row.centre+(row.lead-row.centre).normal()*400.;
  require(row.lead.distanceTo(row.centre)>400.01&&std::any_of(ink.lines.begin(),ink.lines.end(),[&](const auto& line){
    return (line.first.distanceTo(row.lead)<.01&&line.second.distanceTo(edge)<.01)||(line.second.distanceTo(row.lead)<.01&&line.first.distanceTo(edge)<.01);
  }),"actual native ink does not terminate radially at circle");
  if(row.bent)require(productAxisExternalStrokeCount(ink,row.centre,400.)==1,"extra native straight tail after radial bend");
  reason.clear();return true;
 }catch(const std::exception& e){reason=e.what();return false;}
}
std::vector<std::pair<std::string,std::string>> productRepairStoredAxisEnds(ProductJob& job,AcDbDatabase& db){
 auto expected=productReadAxisExpectedEnds(db);std::vector<std::pair<std::string,std::string>> issues;if(expected.empty())return issues;
 auto folds=productFindAxisFolds(db);ProductAxisTemplateBank bank(job.profile/L"annotation-template/平面标注.dwg");
 for(auto& row:expected){std::string reason;if(productStoredAxisEndMatches(db,row,folds,reason))continue;
  // A changed existing native object needs layout/semantic resolution. Only a
  // genuinely absent end may be reconstructed from the frozen true geometry.
  AcDbObjectId old;const auto handle=bcpproduct::decodeMessage(row.handle);
  if(!row.handle.empty()&&db.getAcDbObjectId(old,false,AcDbHandle(handle.c_str()))==Acad::eOk&&!old.isErased()){
   issues.emplace_back(row.handle,reason);continue;
  }
  AcDbObjectId candidate,connector;const auto oldHandle=row.handle;
  try{
   candidate=bank.create(db,row.number,row.lead,row.centre,productPlanLayer(db,L"A-AXIS",3));
   row.handle=axisHandle(candidate);
   if(row.bent){connector=productAppendAxisFold(db,candidate,productPlanLayer(db,L"A-AXIS",3),row.side,{row.anchor,row.lead});folds.axisFolds[{row.handle,row.side}]=axisHandle(connector);}
   require(productStoredAxisEndMatches(db,row,folds,reason),reason.c_str());
   // Remove an orphan connector only after its exact replacement has passed.
   const auto orphan=folds.axisFolds.find({oldHandle,row.side});if(orphan!=folds.axisFolds.end()){
    status(editObject<AcDbEntity>(find(db,orphan->second))->erase(),"retire repaired axis orphan connector");folds.axisFolds.erase(orphan);}
  }catch(const std::exception& e){if(!connector.isNull()&&!connector.isErased())editObject<AcDbEntity>(connector)->erase();
   if(!candidate.isNull()&&!candidate.isErased())editObject<AcDbEntity>(candidate)->erase();row.handle=oldHandle;issues.emplace_back(oldHandle,e.what());}
 }
 productSaveAxisExpectedEnds(db,expected);return issues;
}
std::vector<std::pair<std::string,std::string>> productAuditStoredAxisEnds(AcDbDatabase& db,const fs::path& report){
 const auto expected=productReadAxisExpectedEnds(db);const auto folds=productFindAxisFolds(db);
 std::vector<std::pair<std::string,std::string>> issues;std::ostringstream out;out<<std::setprecision(17)
   <<"{\"contract\":\"plan.expected-axis-ends.v1\",\"expectedCoverageKnown\":"<<(expected.empty()?"false":"true")<<",\"ends\":[";
 bool first=true;for(const auto& row:expected){std::string reason;const auto good=productStoredAxisEndMatches(db,row,folds,reason);
  if(!good)issues.emplace_back(row.handle,reason);if(!first)out<<',';first=false;
  out<<"{\"panel\":"<<row.panel+1<<",\"side\":"<<row.side<<",\"number\":"<<jsonUtf8(row.number)<<",\"station\":"<<row.station
     <<",\"handle\":\""<<row.handle<<"\",\"singleBend\":"<<(row.bent?"true":"false")<<",\"verified\":"<<(good?"true":"false")
     <<",\"reason\":"<<jsonUtf8(bcpproduct::decodeMessage(reason))<<'}';}
 out<<"]}";writeNew(report,out.str());
 if(expected.empty())issues.emplace_back("axis-coverage","no frozen source expected-axis table; existing ends can be checked but source completeness is unverified");
 return issues;
}

void productArrangeAxisCircles(AcDbDatabase& db,ProductPlanLayoutConfig& c,AcDbObjectId layer,
        const bcpproduct::PlanAnnotationLayout& spacing,const std::function<AcDbObjectId(const std::string&)>& id,
        ProductJob* job=nullptr){
 struct End {std::string handle;AcGePoint3d anchor,centre;bool folded{};};
 using Side=std::pair<int,int>;
 std::map<Side,std::vector<End>> groups;
 for(const auto& role:c.axes){
  if(c.axisLayoutFailures.count(role.handle))continue;
  const auto g=chainGrips(id(role.handle));
  for(std::size_t i=0;i<g.size();i+=4){
   const int side=productAxisEnd(productPlacementBounds(c,role.panel),role.side!=0,g[i],g[i+1]).side;
   const auto fold=productFoldPoints(db,c,role.handle,side);
   groups[{role.panel,side}].push_back({role.handle,fold.empty()?g[i]:fold.front(),g[i+1],!fold.empty()});
  }
 }
 struct Move {double position;AcGePoint3d anchor;};
 std::map<std::pair<std::string,int>,Move> moves;
 const auto scale=c.scale/c.unitMM;
 const auto gap=spacing.circleDiameter+scale*bcpproduct::PlanPaperPolicy::circleClearance;
 const auto maximum=scale*bcpproduct::PlanPaperPolicy::circleMaxShift;
 const auto radius=spacing.circleDiameter/2;
 // Leave at least one paper millimetre of native diagonal beyond the circle.
 // A smaller shift can put a nominal 45-degree bend inside the circle.
 const auto minimumBent=(radius+scale*bcpproduct::PlanPaperPolicy::circleClearance)/std::sqrt(2.);
 for(auto& [key,ends]:groups){
  const auto [panel,side]=key;const bool vertical=side>=2;const double sign=side%2?1:-1;
  std::sort(ends.begin(),ends.end(),[&](const End& a,const End& b){
   return productAxisStation(a.anchor,vertical)<productAxisStation(b.anchor,vertical);
  });
  const auto retainConflict=[&](const std::string& reason){c.axisCircleConflicts[key]=reason;
   for(const auto& end:ends){c.axisLayoutFailures[end.handle]=reason;
    if(job)productRecordPlanAxisFailure(*job,static_cast<std::size_t>(panel)+1,{},end.handle,reason);}};
  std::vector<double> stations;for(const auto& end:ends)stations.push_back(productAxisStation(end.anchor,vertical));
  if(std::adjacent_find(stations.begin(),stations.end(),[](double a,double b){return b-a<=1e-5;})!=stations.end()){
   retainConflict("coincident real axis stations require service or duplication review");continue;
  }
  const bool crowded=std::adjacent_find(stations.begin(),stations.end(),[&](double a,double b){return b-a<gap-1e-5;})!=stations.end();
  if(!crowded&&std::none_of(ends.begin(),ends.end(),[](const End& end){return end.folded;}))continue;
  const auto datum=productPlacementDatum(c,panel,side);
  const auto limits=productAxisCircleLimits(db,c.panels[panel],vertical,datum+sign*spacing.circleCentre,radius);
  const double outer=productAxisFoldBandStart(db,c,panel,side,spacing,id);
  const double allowed=std::min(maximum,sign*datum+spacing.circleCentre-outer-.01);
  const auto solution=planradial::packPlanRadialAxisCircles(stations,gap,allowed,minimumBent,limits.first,limits.second);
  if(!solution){retainConflict("no single-bend radial fit within dimension band, shift limit and frame");continue;}
  for(std::size_t i=0;i<ends.size();++i)if(ends[i].folded||std::abs((*solution)[i]-stations[i])>1e-5)
   moves[{ends[i].handle,side}]={(*solution)[i],ends[i].anchor};
 }
 if(moves.empty())return;
 std::vector<ProductPlanRole> result;
 for(const auto& role:c.axes){
  if(c.axisLayoutFailures.count(role.handle)){result.push_back(role);continue;}
  const bool vertical=role.side!=0;const auto native=id(role.handle);const auto g=chainGrips(native);
  const auto panel=productPlacementBounds(c,role.panel);std::vector<int> sides;
  for(std::size_t i=0;i<g.size();i+=4)sides.push_back(productAxisEnd(panel,vertical,g[i],g[i+1]).side);
  if(std::none_of(sides.begin(),sides.end(),[&](int side){return moves.count({role.handle,side})!=0;})){
   result.push_back(role);continue;
  }
  std::vector<AcDbObjectId> created;std::vector<ProductPlanRole> proposed;
  std::map<std::pair<std::string,int>,std::string> proposedFolds;
  try{
   for(std::size_t i=0;i<sides.size();++i){
    const int side=sides[i];const auto move=moves.find({role.handle,side});
    auto prototype=productCloneAxisLabel(db,native);created.push_back(prototype);
    if(sides.size()==2)productKeepAxisLabelEnd(db,prototype,vertical,g[4*i+1],false,job);
    auto label=prototype;
    if(move!=moves.end()){
     const auto& anchor=move->second.anchor;const auto shift=move->second.position-productAxisStation(anchor,vertical);
     const auto centre=productAxisPoint(g[4*i+1],vertical,productAxisNormal(g[4*i+1],vertical),move->second.position);
     const double sign=side%2?1:-1;
     const auto bend=std::abs(shift)<1e-5?anchor:productAxisPoint(anchor,vertical,
               productAxisNormal(centre,vertical)-sign*std::abs(shift),productAxisStation(anchor,vertical));
     productTraceAxisLayout(job,role.handle,"radial-native-candidate-enter");
     label=productOrientNativeAxis(db,prototype,bend,centre);created.push_back(label);
     status(editObject<AcDbEntity>(prototype)->erase(),"retire radial positioning prototype");
     status(editObject<AcDbEntity>(label)->setLayer(layer),"radial native axis layer");
     if(std::abs(shift)>1e-5){
      const auto fold=productAppendAxisFold(db,label,layer,side,{anchor,bend});created.push_back(fold);
      proposedFolds[{axisHandle(label),side}]=axisHandle(fold);
     }
    }
    proposed.push_back({axisHandle(label),role.panel,role.side,role.row});
   }
   // Commit one logical axis only after every required visible end is proven.
   const auto previousRoot=c.axisOriginalHandles.find(role.handle);
   const auto identity=previousRoot==c.axisOriginalHandles.end()?role.handle:previousRoot->second;
   std::vector<AcDbObjectId> previous{native};
   for(const int side:sides){const auto old=c.axisFolds.find({role.handle,side});if(old!=c.axisFolds.end())previous.push_back(find(db,old->second));}
   productRetireAxisGroup(db,previous);
   for(const int side:sides)c.axisFolds.erase({role.handle,side});
   if(!c.axisOriginalGrips.count(identity))c.axisOriginalGrips[identity]=g;
   for(const auto& item:proposed){c.axisOriginalHandles[item.handle]=identity;result.push_back(item);}
   c.axisFolds.insert(proposedFolds.begin(),proposedFolds.end());
  }catch(const std::exception& error){
   for(const auto object:created)if(!object.isErased())editObject<AcDbEntity>(object)->erase();
   result.push_back(role);
   for(const int side:sides)if(moves.count({role.handle,side}))c.axisCircleConflicts[{role.panel,side}]=error.what();
   if(job){const int source=static_cast<std::size_t>(role.panel)<job->layoutSourcePanels.size()?job->layoutSourcePanels[role.panel]:role.panel+1;
    productRecordPlanAxisFailure(*job,source,{},role.handle,std::string("radial candidate failed; previous native axis retained: ")+error.what());}
  }
 }
 c.axes=std::move(result);
 // Keep the in-memory expected table valid after native replacements as well.
 for(auto& expected:c.expectedAxes){const auto identity=expected.original.empty()?
    (expected.outputs.empty()?std::string{}:expected.outputs.front()):expected.original;
  expected.outputs.clear();for(const auto& role:c.axes){const auto root=c.axisOriginalHandles.find(role.handle);
   if(role.panel==expected.panel&&(root==c.axisOriginalHandles.end()?role.handle:root->second)==identity)expected.outputs.push_back(role.handle);}}
}

// Migrate a known old three-vertex route using the whole same-row neighbourhood.
// Only associated axis annotations participate; no building line is inferred or moved.
void productMigrateTemplateAxisFolds(ProductJob& job,AcDbDatabase& db){
 const auto originalFolds=productFindAxisFolds(db);
 std::map<std::pair<int,long long>,std::vector<std::string>> groups;
 for(const auto& [key,handle]:originalFolds.axisFolds){try{
  const auto points=productFoldPoints(db,originalFolds,key.first,key.second);if(points.size()!=3)continue;
  const auto g=chainGrips(find(db,key.first));if(g.size()!=4)continue;
  groups[{key.second,std::llround(productAxisNormal(g[1],key.second>=2)*100.)}].push_back(key.first);
 }catch(const std::exception& e){productRecordPlanAxisFailure(job,0,{},key.first,std::string("old route cannot be read; retained: ")+e.what());}}
 std::ostringstream report;report<<"{\"groups\":[";bool first=true;
 for(const auto& [key,members]:groups){
  const int side=key.first;const bool vertical=side>=2;const double sign=side%2?1:-1;
  const auto normal=static_cast<double>(key.second)/100.;ProductPlanLayoutConfig layout;
  const auto spacing=bcpproduct::planAnnotationLayout(layout.scale,layout.unitMM,layout.paper);
  const double datum=normal-sign*spacing.circleCentre;ProductPlanBounds panel{-1e8,-1e8,1e8,1e8};
  if(side==0)panel.xmin=datum;else if(side==1)panel.xmax=datum;else if(side==2)panel.ymin=datum;else panel.ymax=datum;
  layout.panels.push_back(panel);std::array<bcpproduct::PlanSideDatum,4> datums{};
  for(int s=0;s<4;++s)datums[s]={productPlanDatum(panel,s),{}};layout.sidePlacement.push_back(datums);
  layout.axisFolds=productFindAxisFolds(db).axisFolds;
  for(const auto& [raw,type]:modelTypes(db))if(type==L"TDbAxisLabelSet"){
   const auto handle=ascii(raw);const auto id=find(db,handle);const auto g=chainGrips(id);if(g.size()!=4)continue;
   if(std::abs(productAxisNormal(g[1],vertical)-normal)>.01)continue;
   const auto fold=productFoldPoints(db,layout,handle,side);const auto anchor=fold.empty()?g[0]:fold.front();
   if(std::abs(productAxisNormal(anchor,vertical)-(datum+sign*spacing.start))>.01||
      (fold.empty()&&std::abs(productAxisStation(anchor,vertical)-productAxisStation(g[1],vertical))>.01))continue;
   layout.axes.push_back({handle,0,vertical?1:0,0});
  }
  // Actual dimension ink defines the inner boundary of the route band.
  for(const auto& [raw,type]:modelTypes(db))if(type==L"TDbDimension2"){
   const auto entity=open<AcDbEntity>(find(db,ascii(raw)));AcDbExtents box;if(entity->getGeomExtents(box)!=Acad::eOk)continue;
   const auto a=box.minPoint(),b=box.maxPoint();const auto edge=sign>0?b:a;
   const double outside=sign*(productAxisNormal(edge,vertical)-datum);
   if(outside<spacing.start||outside>spacing.circleNear)continue;
   if(std::any_of(layout.axes.begin(),layout.axes.end(),[&](const auto& role){const auto g=chainGrips(find(db,role.handle));
      return productAxisStation(g[1],vertical)>=productAxisStation(a,vertical)-800.&&productAxisStation(g[1],vertical)<=productAxisStation(b,vertical)+800.;}))
    layout.dimensions.push_back({ascii(raw),0,side,1});
  }
  double low=std::numeric_limits<double>::infinity(),high=-low;
  for(const auto& role:layout.axes){const auto g=chainGrips(find(db,role.handle));
   const auto fold=productFoldPoints(db,layout,role.handle,side);const auto anchor=fold.empty()?g[0]:fold.front();
   low=std::min(low,productAxisStation(anchor,vertical));high=std::max(high,productAxisStation(anchor,vertical));}
  if(std::isfinite(low)){
   if(vertical){panel.xmin=low;panel.xmax=std::max(low+1.,high);panel.ymin=sign>0?datum-100.:datum;panel.ymax=sign>0?datum:datum+100.;}
   else{panel.ymin=low;panel.ymax=std::max(low+1.,high);panel.xmin=sign>0?datum-100.:datum;panel.xmax=sign>0?datum:datum+100.;}
   layout.panels[0]=panel;for(int s=0;s<4;++s)layout.sidePlacement[0][s].coordinate=productPlanDatum(panel,s);
  }
  productArrangeAxisCircles(db,layout,productPlanLayer(db,L"A-AXIS",3),spacing,[&](const auto& h){return find(db,h);},&job);
  auto frozen=productReadAxisExpectedEnds(db);
  for(auto& row:frozen){for(const auto& role:layout.axes){const auto prior=layout.axisOriginalHandles.find(role.handle);
    if(prior==layout.axisOriginalHandles.end()||prior->second!=row.handle)continue;
    const auto g=chainGrips(find(db,role.handle));if(productAxisEnd(panel,vertical,g[0],g[1]).side!=row.side)continue;
    const auto fold=productFoldPoints(db,layout,role.handle,row.side);row.handle=role.handle;row.anchor=fold.empty()?g[0]:fold.front();row.lead=g[0];row.centre=g[1];row.bent=!fold.empty();break;
  }}if(!frozen.empty())productSaveAxisExpectedEnds(db,frozen);
  if(!first)report<<',';first=false;report<<"{\"side\":"<<side<<",\"nativeEnds\":"<<layout.axes.size()<<",\"issues\":"<<layout.axisCircleConflicts.size()<<'}';
 }
 report<<"]}";writeNew(job.root/L"axis-legacy-route-migration.json",report.str());
}

