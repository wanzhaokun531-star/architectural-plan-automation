// Plan-only detail dimensions. Project associations live with the profile;
// geometry and values are always read from the current native objects.
struct ProductOutdoorConfig {
 struct Column {std::string handle;int floor{};};
 struct Step {int floor{};std::string left,right,platform;std::vector<std::string> treads;};
 struct Space {std::string text;int floor{};};
 std::vector<Column> columns;std::vector<Step> steps;std::vector<Space> spaces;std::vector<std::string> replace;
};
ProductOutdoorConfig productOutdoorConfig(const fs::path& file){
 ProductOutdoorConfig c;if(!fs::exists(file))return c;
 std::ifstream in(file);std::string line;require(bool(std::getline(in,line))&&line=="BCPOUTDOOR1","outdoor detail profile header");bool end=false;
 while(std::getline(in,line)){if(line.empty())continue;std::istringstream row(line);std::string tag;row>>tag;if(tag=="END"){end=true;break;}
  if(tag=="COLUMN"){ProductOutdoorConfig::Column x;require(bool(row>>x.handle>>x.floor)&&x.floor>0,"column detail association");c.columns.push_back(x);}
  else if(tag=="SPACE"){ProductOutdoorConfig::Space x;require(bool(row>>x.text>>x.floor)&&x.floor>0,"balcony location association");c.spaces.push_back(x);}
  else if(tag=="STEP"){ProductOutdoorConfig::Step x;require(bool(row>>x.floor>>x.left>>x.right>>x.platform),"actual step association");std::string h;while(row>>h)x.treads.push_back(h);require(x.treads.size()>1,"actual tread edges");c.steps.push_back(x);}
  else if(tag=="REPLACE"){std::string h;require(bool(row>>h),"superseded detail association");c.replace.push_back(h);}
  else require(false,"unknown outdoor detail profile record");
 }require(end,"complete outdoor detail profile");return c;
}
AcDbObjectId productCloneDetail(AcDbDatabase& db,AcDbObjectId prototype){
 auto from=open<AcDbEntity>(prototype);std::unique_ptr<AcDbEntity> value(AcDbEntity::cast(from->clone()));require(value!=nullptr,"clone genuine native detail");from.reset();
 auto model=editObject<AcDbBlockTableRecord>(modelId(db));AcDbObjectId id;productStatus(model->appendAcDbEntity(id,value.get()),"append native detail dimension");value.release()->close();return id;
}
std::array<AcGePoint3d,4> productColumnCorners(AcDbObjectId id){
 auto e=open<AcDbEntity>(id);require(std::wstring(e->isA()->name())==L"TDbColumn"&&!e->isAProxy(),"real column for detail dimensions");
 // Read the actual column boundary, not its world-aligned bounding box.
 TemplateParts parts(e.get());productStatus(e->explode(parts.values),"public column section boundary");require(parts.values.length()==4,"rectangular column section requires four actual edges");
 std::array<AcGePoint3d,4> p;for(int k=0;k<4;++k){auto* edge=AcDbLine::cast(static_cast<AcRxObject*>(parts.values[k]));require(edge!=nullptr,"column section is not a rectangle");p[k]=edge->startPoint();auto* next=AcDbLine::cast(static_cast<AcRxObject*>(parts.values[(k+1)%4]));require(next&&edge->endPoint().distanceTo(next->startPoint())<1e-5,"column section edge continuity");}
 const auto u=p[1]-p[0],v=p[3]-p[0];require(u.length()>0&&v.length()>0&&std::abs(u.normal().dotProduct(v.normal()))<1e-8&&(p[2]-(p[1]+v)).length()<1e-5,"actual rectangular column section");return p;
}
AcDbObjectId productDetailChain(AcDbDatabase& db,AcDbObjectId prototype,const std::vector<AcGePoint3d>& stations,double baselineOffset){
 require(stations.size()>=2,"detail dimension stations");const auto n=stations.size()-1;require(chainValues(prototype).size()==n,"whole detail prototype segment count");
 const auto id=productCloneDetail(db,prototype);auto g=chainGrips(id);require(g.size()==2*n+6||g.size()==2*n+3,"whole native detail grip layout");
 const auto direction=(stations.back()-stations.front()).normal();const AcGeVector3d normal(-direction.y,direction.x,0);const auto original=(g[n+2]-g[2]).normal();
 const double angle=std::atan2(direction.y,direction.x)-std::atan2(original.y,original.x);
 {auto e=editObject<AcDbEntity>(id);productStatus(e->transformBy(AcGeMatrix3d::rotation(angle,AcGeVector3d::kZAxis,g[2])),"orient true detail chain");}
 g=chainGrips(id);{auto e=editObject<AcDbEntity>(id);productStatus(e->transformBy(AcGeMatrix3d::translation(stations.front()-g[2])),"position true detail chain");productStatus(e->setLayer(L"PUB_DIM"),"detail PUB_DIM");productStatus(e->setColorIndex(256),"detail ByLayer");}
 // Shortening the full chain can move some intermediate stations forward.
 // Moving the two directions in opposite orders keeps every interval positive.
 g=chainGrips(id);
 for(std::size_t k=0;k<stations.size();++k)
  if((stations[k]-g[k+2]).dotProduct(direction)<-1e-7)moveDimensionGrip(id,static_cast<int>(k+2),stations[k]);
 for(std::size_t k=stations.size();k-->0;)
  if((stations[k]-g[k+2]).dotProduct(direction)>=-1e-7)moveDimensionGrip(id,static_cast<int>(k+2),stations[k]);
 moveDimensionGrip(id,0,stations.front()+normal*baselineOffset);
 // Keep the interior prototype's actual text offset and orientation.
 const auto pg=chainGrips(prototype);const auto pd=(pg[n+2]-pg[2]).normal();const AcGeVector3d pn(-pd.y,pd.x,0);const double textOffset=(pg[n+3]-pg[0]).dotProduct(pn);
 for(std::size_t k=0;k<n;++k){const auto mid=stations[k]+(stations[k+1]-stations[k])*0.5;moveDimensionGrip(id,static_cast<int>(n+3+k),mid+normal*(baselineOffset+textOffset));}
 const auto actual=chainValues(id);require(actual.size()==n,"detail chain membership");for(std::size_t k=0;k<n;++k)require(std::abs(actual[k]-stations[k].distanceTo(stations[k+1]))<1e-5,"actual detail measurement differs");return id;
}
#include "ProductAxisRanges.inc"
void productOutdoorDetails(ProductJob& job,AcDbDatabase& db,const ProductPlanLayoutConfig& layout){
 Working context(&db);const auto config=job.latestThreeModel?productOutdoorConfig(job.profile/L"plan-policy/outdoor-details.tsv"):ProductOutdoorConfig{};std::map<std::size_t,AcDbObjectId> prototypes;
 for(const auto& item:modelTypes(db))if(item.second==L"TDbDimension2"){const auto id=find(db,ascii(item.first));auto e=open<AcDbEntity>(id);if(e->visibility()==AcDb::kVisible&&symbolName(e->layerId())==L"PUB_DIM"){e.reset();prototypes.emplace(chainValues(id).size(),id);}}
 std::ostringstream proof;proof<<std::setprecision(17)<<"{\"schema\":\"bcp.outdoor-details.v1\",\"columns\":[";bool first=true;const double gap=6*layout.scale/layout.unitMM;
 for(const auto& c:config.columns){const auto p=productColumnCorners(find(db,c.handle));const auto a=productDetailChain(db,prototypes.at(1),{p[3],p[2]},gap),b=productDetailChain(db,prototypes.at(1),{p[1],p[2]},-gap);if(!first)proof<<',';first=false;proof<<"{\"column\":\""<<c.handle<<"\",\"floor\":"<<c.floor<<",\"dimensions\":[\""<<axisHandle(a)<<"\",\""<<axisHandle(b)<<"\"],\"section\":["<<p[0].distanceTo(p[1])<<','<<p[0].distanceTo(p[3])<<"]}";}
 proof<<"],\"steps\":[";first=true;
 for(const auto& step:config.steps){const auto left=productColumnCorners(find(db,step.left)),right=productColumnCorners(find(db,step.right));const auto platform=chainGrips(find(db,step.platform));require(platform.size()>=2,"actual platform edge");
  std::vector<double> depths;for(const auto& h:step.treads){auto e=open<AcDbEntity>(find(db,h));require(AcDbPolyline::cast(e.get())||AcDbLine::cast(e.get()),"source tread geometry");const auto g=chainGrips(e->objectId());require(g.size()>=2,"source tread positions");depths.push_back(g.front().y);}
  std::sort(depths.begin(),depths.end());const double front=depths.front();const std::vector<AcGePoint3d> stations{{left[0].x,front,0},{left[1].x,front,0},{right[0].x,front,0},{right[1].x,front,0}};
  const auto chain=productDetailChain(db,prototypes.at(3),stations,-gap),total=productDetailChain(db,prototypes.at(1),{left[3],right[2]},gap*0.5);
  if(!first)proof<<',';first=false;proof<<"{\"floor\":"<<step.floor<<",\"widthChain\":\""<<axisHandle(chain)<<"\",\"overallWidth\":\""<<axisHandle(total)<<"\",\"sourceTreadPositions\":[";for(std::size_t k=0;k<depths.size();++k){if(k)proof<<',';proof<<depths[k];}proof<<"],\"depthDimensions\":\"pending source tracing and actual tread parameters\"}";
  job.detailPending.push_back({u8"台阶踏面与平台进深",std::to_string(step.floor)+u8"层",u8"源踏面线存在小数且分段不一致；保留真实源几何，踏步及平台进深待确认后生成连续链，未套用等步长"});
 }
 for(const auto& h:config.replace){auto e=editObject<AcDbEntity>(find(db,h));require(std::wstring(e->isA()->name())==L"TDbDimension2","superseded column detail type");productStatus(e->erase(),"replace incomplete one-direction column dimension");}
 proof<<"],\"balconies\":[";first=true;std::set<std::string> configured;
 const auto recordSpace=[&](const std::string& handle,int floor,const std::wstring& name){if(!first)proof<<',';first=false;proof<<"{\"sourceName\":\""<<handle<<"\",\"floor\":"<<floor<<",\"name\":"<<jsonUtf8(name)<<",\"railing\":\"pending\",\"upstand\":\"pending\"}";for(const auto* edge:{u8"左侧边",u8"外侧边",u8"右侧边"})for(const auto* member:{u8"栏杆",u8"反坎"})job.detailPending.push_back({std::string(member),std::to_string(floor)+u8"层/"+(name.find(L"阳台")!=std::wstring::npos?std::string(u8"阳台"):std::string(u8"露台"))+"/"+edge+"/"+handle,u8"待确认临空范围、出入口和构造做法；仅有平台轮廓不能判为已完成，未猜高度、宽度、材料或节点"});};
 for(const auto& space:config.spaces){auto e=open<AcDbEntity>(find(db,space.text));std::vector<std::wstring> text;productOpeningTexts(*e,text);const auto it=std::find_if(text.begin(),text.end(),[](const auto& s){return s.find(L"阳台")!=std::wstring::npos||s.find(L"露台")!=std::wstring::npos;});require(it!=text.end(),"actual balcony/terrace name association");configured.insert(space.text);recordSpace(space.text,space.floor,*it);}
 // Other configured input paths still report named spaces instead of silently
 // omitting the new completeness rule when no project edge adapter exists.
 for(const auto& item:modelTypes(db))if(item.second==L"TDbText"||item.second==L"AcDbText"||item.second==L"AcDbMText"){const auto h=ascii(item.first);if(configured.count(h))continue;auto e=open<AcDbEntity>(find(db,h));std::vector<std::wstring> text;if(auto* t=AcDbText::cast(e.get()))text.emplace_back(t->textString());else if(auto* t=AcDbMText::cast(e.get())){AcString content;t->contents(content);text.emplace_back(content.kwszPtr());}else productOpeningTexts(*e,text);for(const auto& name:text)if(name==L"阳台"||name==L"露台"){job.detailPending.push_back({u8"栏杆及反坎",h,u8"识别到露台或阳台名称；实际楼层、边段及构造参数待适配确认，不能按包围盒自动封闭"});break;}}
 proof<<"],\"missingParametersInvented\":false}";writeNew(job.root/L"室外细部与栏杆反坎核验.json",proof.str());
}
std::string productPlanCsvCell(const std::string& value){
 std::string escaped="\"";
 for(const char c:value){if(c=='"')escaped+='"';escaped+=c;}
 return escaped+'"';
}

void productWriteDetailIssues(ProductJob& job){
 if(job.unifiedPlan&&job.genericInput){
  std::ostringstream issues;
  issues<<"\xEF\xBB\xBF"<<u8"类别,对象,状态,说明\r\n散水明沟,项目,暂停,未生成\r\n";
  std::set<std::array<std::string,3>> written;
  for(const auto& issue:job.detailPending)if(written.insert(issue).second)
   issues<<productPlanCsvCell(issue[0])<<','<<productPlanCsvCell(issue[1])<<u8",待复核,"<<productPlanCsvCell(issue[2])<<"\r\n";
  for(const auto& [key,item]:job.sourceInventory.items){
   if(item.check!=bcpproduct::PlanCheck::pending&&item.check!=bcpproduct::PlanCheck::failed)continue;
   const auto& [panel,scope,handle]=key;
   issues<<productPlanCsvCell(scope)<<','<<productPlanCsvCell(std::to_string(panel)+"/"+handle)<<','
         <<(item.check==bcpproduct::PlanCheck::failed?u8"局部失败":u8"待复核")<<','<<productPlanCsvCell(item.reason)<<"\r\n";
  }
  writeNew(job.root/L"待确认清单.csv",issues.str());
  return;
 }
 if(job.detailPending.empty())return;
 const auto path=job.root/L"待确认清单.csv";
 std::ofstream csv(path,std::ios::binary|std::ios::app);
 require(bool(csv),"append detail issues");
 for(const auto& row:job.detailPending)
  csv<<productPlanCsvCell(row[0])<<','<<productPlanCsvCell(row[1])
     <<(row[0]==u8"通用后续模块"?u8",未接入,":u8",待确认,")<<productPlanCsvCell(row[2])<<"\r\n";
 require(bool(csv),"write detail issues");
}
