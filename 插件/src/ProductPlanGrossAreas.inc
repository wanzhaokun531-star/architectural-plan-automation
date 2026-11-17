struct ProductGrossAreaReceipt {
 std::string source,output;
 std::size_t panel{};
 std::vector<std::pair<ProductPlanDetailConfirmations::GrossPart,std::vector<AcGePoint2d>>> parts;
};

std::vector<AcGePoint2d> productGrossBoundary(AcDbDatabase& db,const ProductPlanDetailConfirmations::GrossPart& part){
 std::vector<AcGePoint2d> points;
 const auto coordinate=[&](const std::string& source,unsigned index,bool x){
  const auto entity=open<AcDbEntity>(find(db,source));
  const auto* poly=AcDbPolyline::cast(entity.get());
  require(poly&&!entity->isAProxy()&&index<poly->numVerts(),"gross area boundary source vertex exists");
  AcGePoint2d p;productStatus(poly->getPointAt(index,p),"read gross area source vertex");
  return x?p.x:p.y;
 };
 for(const auto& vertex:part.vertices)points.emplace_back(coordinate(vertex.xSource,vertex.xIndex,true),coordinate(vertex.ySource,vertex.yIndex,false));
 // Explicit source coordinates may combine the X and Y of two real vertices
 // (for example, a return corner). The resulting boundary must be simple.
 for(std::size_t i=0;i<points.size();++i){
  const auto& a=points[i];const auto& b=points[(i+1)%points.size()];
  require(a.distanceTo(b)>1e-5,"gross area boundary has no zero-length edge");
  for(std::size_t j=i+1;j<points.size();++j){
   if(j==i+1||(i==0&&j+1==points.size()))continue;
   const auto& c=points[j];const auto& d=points[(j+1)%points.size()];
   const auto side=[](const auto& p,const auto& q,const auto& r){return (q.x-p.x)*(r.y-p.y)-(q.y-p.y)*(r.x-p.x);};
   const bool crosses=side(a,b,c)*side(a,b,d)<=0&&side(c,d,a)*side(c,d,b)<=0&&
    std::min(a.x,b.x)<=std::max(c.x,d.x)&&std::min(c.x,d.x)<=std::max(a.x,b.x)&&
    std::min(a.y,b.y)<=std::max(c.y,d.y)&&std::min(c.y,d.y)<=std::max(a.y,b.y);
   require(!crosses,"gross area boundary must not cross itself");
  }
 }
 return points;
}

double productGrossPolygonArea(const std::vector<AcGePoint2d>& points,double unit){
 // Translate before the shoelace sum, avoiding cancellation in survey WCS.
 long double twice=0;
 for(std::size_t i=0;i<points.size();++i){
  const auto a=points[i]-points.front(),b=points[(i+1)%points.size()]-points.front();
  twice+=static_cast<long double>(a.x)*b.y-static_cast<long double>(a.y)*b.x;
 }
 const double area=static_cast<double>(std::abs(twice)/2)*unit*unit/1e6;
 require(std::isfinite(area)&&area>0,"gross area boundary has positive measured area");
 return area;
}

std::vector<ProductGrossAreaReceipt> productCaptureGrossAreas(ProductJob& job,AcDbDatabase& db,
 const std::vector<ProductPlanAreaSummary>& summaries,const std::vector<bcpproduct::GenericView>& views){
 const auto confirmations=productPlanDetailConfirmations(job);
 std::vector<ProductGrossAreaReceipt> receipts;
 for(const auto& [source,parts]:confirmations.grossParts){
  ProductGrossAreaReceipt receipt;receipt.source=receipt.output=source;
  for(const auto& summary:summaries)if(summary.source==source)receipt.output=summary.output;
  const auto entity=open<AcDbEntity>(find(db,receipt.output));
  const auto anchor=productPlanNameAnchor(*entity);
  require(anchor.has_value(),"gross area source note has a readable anchor");
  receipt.panel=bcpproduct::nearestGenericView(views,{anchor->x,anchor->y})+1;
  for(const auto& part:parts)receipt.parts.emplace_back(part,productGrossBoundary(db,part));
  receipts.push_back(std::move(receipt));
 }
 return receipts;
}

std::optional<double> productGrossDisplayedArea(const std::wstring& text){
 const auto first=text.find_first_of(L"0123456789");
 if(first==std::wstring::npos)return {};
 wchar_t* end{};const double value=std::wcstod(text.c_str()+first,&end);
 if(!std::isfinite(value)||value<=0||end==text.c_str()+first)return {};
 return value;
}

void productVerifyGrossAreas(ProductJob& job,AcDbDatabase& saved,const std::vector<ProductGrossAreaReceipt>& receipts){
 if(receipts.empty())return;
 Working work(&saved);
 const auto confirmations=productPlanDetailConfirmations(job);
 std::map<std::string,double> roundedParts;
 std::ostringstream report;report<<std::setprecision(17)<<"{\"savedReopened\":true,\"areas\":[";
 bool first=true;
 for(const auto& receipt:receipts){
  double total=0;bool geometry=true;
  std::ostringstream parts;parts<<std::setprecision(17)<<'[';
  for(std::size_t k=0;k<receipt.parts.size();++k){
   const auto& [part,before]=receipt.parts[k];
   const auto actual=productGrossBoundary(saved,part);
   geometry=geometry&&actual.size()==before.size()&&std::equal(actual.begin(),actual.end(),before.begin(),
    [&](const auto& a,const auto& b){return a.distanceTo(b)*job.unifiedUnitMM<=.01;});
   const auto raw=productGrossPolygonArea(actual,job.unifiedUnitMM);
   total+=raw*part.factor;
   if(k)parts<<',';
   parts<<"{\"part\":"<<jsonUtf8(bcpproduct::decodeMessage(part.key))<<",\"basis\":"<<jsonUtf8(bcpproduct::decodeMessage(part.basis))
    <<",\"rawM2\":"<<raw<<",\"factor\":"<<part.factor<<",\"includedM2\":"<<raw*part.factor<<",\"vertices\":[";
   for(std::size_t i=0;i<actual.size();++i){if(i)parts<<',';parts<<'['<<actual[i].x<<','<<actual[i].y<<']';}
   parts<<"]}";
  }
  parts<<']';
  const auto entity=open<AcDbEntity>(find(saved,receipt.output));
  const auto text=productPlanNameContents(*entity);
  const auto actual=productGrossDisplayedArea(text);
  const auto displayed=std::round((total+1e-8)*100)/100;
  const bool verified=geometry&&productPlanDimensionVisible(*entity)&&actual&&std::abs(*actual-displayed)<1e-6;
  if(verified)roundedParts.emplace(receipt.source,displayed);
  std::ostringstream row;row<<std::setprecision(17)<<"{\"panel\":"<<receipt.panel<<",\"source\":\""<<receipt.source
   <<"\",\"object\":\""<<receipt.output<<"\",\"scope\":\"gross-area-inclusions\",\"savedReopened\":true,\"parts\":"<<parts.str()
   <<",\"calculatedM2\":"<<total<<",\"expectedDisplayM2\":"<<displayed<<",\"actualText\":"<<jsonUtf8(text)
   <<",\"result\":\""<<(verified?"verified-source-bound-gross-area-and-factors":"pending-gross-area-geometry-or-label")<<"\"}";
  if(!first)report<<',';first=false;report<<row.str();
  for(const auto* rule:{"S06","P05","Q05"})productRecordRuleEvidence(job,rule,row.str());
  job.drawingChecks.record(receipt.panel,"gross-area",receipt.source,{"S06","P05","Q05"},bcpproduct::PlanReleaseGate::mainPlan,
   verified?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::failed,
   u8"本项目源绑定计量边界、用途与折算依据，独立重开复算",verified?"":u8"建筑面积边界、折算或图面数值未通过复算");
 }
 report<<"],\"totals\":[";first=true;
 for(const auto& [source,members]:confirmations.grossTotals){
  double total=0;bool complete=true;
  for(const auto& member:members){const auto at=roundedParts.find(member);if(at==roundedParts.end())complete=false;else total+=at->second;}
  const auto entity=open<AcDbEntity>(find(saved,source));
  const auto actual=productGrossDisplayedArea(productPlanNameContents(*entity));
  const bool verified=complete&&actual&&std::abs(*actual-total)<1e-6;
  if(!first)report<<',';first=false;
  report<<"{\"source\":\""<<source<<"\",\"roundingBasis\":\"sum-of-verified-displayed-component-areas\",\"m2\":"<<total
   <<",\"verified\":"<<(verified?"true":"false")<<'}';
  job.drawingChecks.record(0,"gross-area-total",source,{"S06","Q05"},bcpproduct::PlanReleaseGate::mainPlan,
   verified?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::failed,u8"按源图逐层及门廊已列两位面积求和",
   verified?"":u8"全楼面积未与逐项已复算的显示面积闭合");
 }
 report<<"]}";writeNew(job.root/L"gross-area-readback.json",report.str());
}
