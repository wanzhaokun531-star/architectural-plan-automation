struct ProductPlanBounds {double xmin{},ymin{},xmax{},ymax{};};
struct ProductPlanRole {std::string handle;int panel{},side{},row{};};
struct ProductPerimeterExpected {
 std::vector<double> stations;
 std::vector<double> openingStations;
 std::map<double,std::vector<std::string>> axisLines;
 std::vector<std::string> sourceObjects;
 std::vector<std::string> openings;
 std::vector<std::string> openingFreeWalls;
 bool axisScopeKnown{};
 bool openingScopeKnown{};
 std::string axisScopeReason;
};
struct ProductOpeningAxisTerminal {
 bool axisVertical{};
 double station{},first{},last{};
 std::string source;
};
struct ProductOpeningAxisCheck {
 int panel{};
 std::string opening,axisSource,originalChain;
 double terminal{},openingEdge{},normal{};
 bool vertical{};
};
struct ProductPlanAxisExpectation {
 int panel{};bool vertical{};std::string number;double station{};bool negative{},positive{};
 std::string original;std::vector<std::string> outputs;std::string issue;
 std::vector<std::string> sourceLabels;std::map<int,std::string> sourceEndHandles,sourceFolds;
 std::vector<std::string> pendingSources;
};
struct ProductPlanLayoutConfig {double scale{100},unitMM{1};std::vector<ProductPlanBounds> panels,placementAxes;std::vector<std::array<bcpproduct::PlanSideDatum,4>> sidePlacement;std::vector<ProductPlanRole> dimensions,axes,frames;std::map<std::pair<std::string,int>,std::string> axisFolds;std::map<std::string,std::string> axisOriginalHandles;std::map<std::string,std::vector<AcGePoint3d>> axisOriginalGrips;std::map<std::pair<int,int>,std::string> axisCircleConflicts;bool projectAssociations{true};bcpproduct::PlanPaperPolicy paper;
 std::vector<ProductPlanAxisExpectation> expectedAxes;
 std::map<std::string,std::string> axisLayoutFailures;
 std::map<std::string,bool> keepNegativeAxisEnd;
 std::map<std::string,std::vector<double>> sourceOpeningStations;
 std::map<std::pair<int,int>,ProductPerimeterExpected> expectedPerimeter;
 std::wstring logicalAxisLayer{L"DOTE"};
 std::set<std::string> projectedPerimeter;
 std::vector<ProductOpeningAxisCheck> openingAxisChecks;
};
ProductPlanLayoutConfig productReadPlanLayout(const fs::path& file){
 ProductPlanLayoutConfig c;std::ifstream in(file);std::string tag;require(bool(in>>tag>>c.scale>>c.unitMM)&&tag=="BCPPLANLAYOUT1","plan-only layout configuration");bool end=false;
 while(in>>tag){if(tag=="END"){end=true;break;}if(tag=="PANEL"){ProductPlanBounds p;require(bool(in>>p.xmin>>p.ymin>>p.xmax>>p.ymax)&&p.xmin<p.xmax&&p.ymin<p.ymax,"plan wall-axis bounds");c.panels.push_back(p);}else{require(tag=="DIM"||tag=="AXIS"||tag=="FRAME","plan layout record");ProductPlanRole p;require(bool(in>>p.handle>>p.panel>>p.side>>p.row),"plan layout association");(tag=="DIM"?c.dimensions:tag=="AXIS"?c.axes:c.frames).push_back(p);}}
 require(end&&!c.panels.empty(),"complete plan-only layout config");bcpproduct::planAnnotationLayout(c.scale,c.unitMM,c.paper);return c;
}
AcDbObjectId productPlanLayer(AcDbDatabase& db,const wchar_t* name,int colour,bool plot=true){
 AcDbObjectId id;{auto t=open<AcDbLayerTable>(db.layerTableId());if(t->getAt(name,id)!=Acad::eOk){t.reset();auto w=editObject<AcDbLayerTable>(db.layerTableId());auto l=std::make_unique<AcDbLayerTableRecord>();productStatus(l->setName(name),"plan layer name");productStatus(w->add(id,l.get()),"plan layer add");l.release()->close();}}
 auto l=editObject<AcDbLayerTableRecord>(id);AcCmColor aci;aci.setColorIndex(colour);l->setColor(aci);productStatus(l->setIsPlottable(plot),"plan plotting policy");return id;
}
double productPlanDatum(const ProductPlanBounds& p,int side){require(side>=0&&side<4,"plan side");return side==0?p.xmin:side==1?p.xmax:side==2?p.ymin:p.ymax;}
#include "ProductPlanPlacement.inc"
struct ProductAxisEnd {
 int side{};
 double datum{},sign{};
};
double productAxisNormal(const AcGePoint3d& point,bool vertical){return vertical?point.y:point.x;}
AcGePoint3d productAxisAtNormal(AcGePoint3d point,bool vertical,double normal){
 if(vertical)point.y=normal;else point.x=normal;
 return point;
}
ProductAxisEnd productAxisEnd(const ProductPlanBounds& panel,bool vertical,const AcGePoint3d& lead,const AcGePoint3d& centre){
 const double direction=productAxisNormal(centre,vertical)-productAxisNormal(lead,vertical);
 require(std::abs(direction)>1e-5,"native plan axis lead direction");
 const bool positive=direction>0;
 const int side=(vertical?2:0)+(positive?1:0);
 return {side,productPlanDatum(panel,side),positive?1.:-1.};
}
#include "ProductAxisCircleLayout.inc"
std::array<AcGePoint2d,4> productPlanFrameCorners(const ProductPlanBounds& bounds,double offset){
 return {{{bounds.xmin-offset,bounds.ymin-offset},{bounds.xmax+offset,bounds.ymin-offset},
          {bounds.xmax+offset,bounds.ymax+offset},{bounds.xmin-offset,bounds.ymax+offset}}};
}
void productVerifyPlanLayout(AcDbDatabase& db,const ProductPlanLayoutConfig& c,const std::function<AcDbObjectId(const std::string&)>& id,const fs::path& report,bool savedReadback=false,ProductJob* evidence=nullptr){
 Working work(&db);
 const auto layout=bcpproduct::planAnnotationLayout(c.scale,c.unitMM,c.paper);
 const std::array<double,4> offsets{layout.start,layout.third,layout.second,layout.total};
 constexpr double tolerance=1e-4;
 std::ostringstream proof;
 proof<<std::setprecision(17)<<"{\"schema\":\"bcp.plan-layout-result.v1\",\"savedReadback\":"<<(savedReadback?"true":"false")<<",\"scale\":"<<c.scale<<",\"unitMM\":"<<c.unitMM<<",\"dimensions\":[";
 bool first=true;
 struct ReadbackChain {std::string handle; double first,last; std::vector<double> values;};
 std::map<std::pair<int,int>,std::array<std::optional<ReadbackChain>,2>> axisPairs;
 for(const auto& role:c.dimensions){
  require(role.panel>=0&&role.panel<c.panels.size()&&role.row>=1&&role.row<=3,"dimension role range");
  const auto native=id(role.handle);
  const auto grips=chainGrips(native);
  const bool vertical=role.side<2;
  const double sign=(role.side==0||role.side==2)?-1:1;
  const double datum=productPlacementDatum(c,role.panel,role.side);
  const double expectedStart=datum+sign*layout.start;
  const auto normal=[vertical](const AcGePoint3d& point){return vertical?point.x:point.y;};
  require(grips.size()>=8,"final native dimension grips");
  const double baseline=normal(grips.front());
  const double rowOffset=role.row==1?layout.total:role.row==2?layout.second:layout.third;
  require(std::abs(baseline-(datum+sign*rowOffset))<tolerance,"final plan dimension baseline differs from rules");
  auto entity=open<AcDbEntity>(native);
  TemplateParts parts(entity.get());
  productStatus(entity->explode(parts.values),"read final native dimension display");
  std::vector<AcGePoint3d> origins;
  for(int k=0;k<parts.values.length();++k){
   const auto* dimension=AcDbAlignedDimension::cast(static_cast<AcRxObject*>(parts.values[k]));
   require(dimension!=nullptr,"final native dimension display type");
   const double gap=sign*dimension->dimexo()*dimension->dimscale();
   for(auto point:{dimension->xLine1Point(),dimension->xLine2Point()}){
    if(vertical)point.x+=gap;else point.y+=gap;
    require(std::abs(normal(point)-expectedStart)<tolerance,"final plan dimension witness start differs from rules");
    origins.push_back(point);
   }
  }
  require(origins.size()+6==grips.size(),"final native dimension witness coverage");
  if(!first)proof<<',';
  first=false;
  proof<<"{\"template\":\""<<role.handle<<"\",\"handle\":\""<<axisHandle(native)<<"\",\"panel\":"<<role.panel+1<<",\"side\":"<<role.side<<",\"row\":"<<role.row
       <<",\"baseline\":"<<baseline<<",\"visibleStart\":"<<expectedStart<<",\"witnessDefinition\":"<<normal(grips[2])
       <<",\"actualVisibleStarts\":"<<wholePoints(origins);
  const auto values=chainValues(native);
  const std::vector<AcGePoint3d> stations(grips.begin()+2,grips.begin()+3+values.size());
  if(savedReadback&&evidence){
   const auto coordinate=[vertical](const auto& point){return vertical?point.y:point.x;};
   const bool nativeType=std::wstring(entity->isA()->name())==L"TDbDimension2"&&!entity->isAProxy();
   const bool continuous=stations.size()==values.size()+1&&[&]{
    for(std::size_t i=0;i<values.size();++i)
     if(std::abs(std::abs(coordinate(stations[i+1])-coordinate(stations[i]))-values[i])*c.unitMM>.01)return false;
    return true;
   }();
   std::ostringstream chain;
   chain<<std::setprecision(17)<<"{\"panel\":"<<role.panel+1<<",\"side\":"<<role.side<<",\"row\":"<<role.row
        <<",\"object\":\""<<role.handle<<"\",\"stations\":"<<wholePoints(stations)<<",\"segmentCount\":"<<values.size()
        <<",\"native\":"<<(nativeType?"true":"false")<<",\"savedReopened\":true,\"report\":\"外围间距核验.json\",\"result\":\""
        <<(nativeType&&continuous?"verified-generated-continuous-chain":"pending")<<"\"}";
   for(const auto* rule:{"D01","D08","D10"})productRecordRuleEvidence(*evidence,rule,chain.str());
   if(role.row<3)axisPairs[{role.panel,role.side}][role.row-1]=
      ReadbackChain{role.handle,coordinate(stations.front()),coordinate(stations.back()),values};
   std::ostringstream row;
   row<<std::setprecision(17)<<"{\"panel\":"<<role.panel+1<<",\"side\":"<<role.side<<",\"row\":"<<role.row
      <<",\"object\":\""<<axisHandle(native)<<"\",\"datum\":"<<datum<<",\"baseline\":"<<baseline
      <<",\"visibleStart\":"<<expectedStart<<",\"actualVisibleStarts\":"<<wholePoints(origins)
      <<",\"savedReopened\":true,\"report\":\"外围间距核验.json\",\"result\":\"verified-perimeter-spacing\"}";
   productRecordRuleEvidence(*evidence,"D09",row.str());
   productRecordRuleEvidence(*evidence,"A14",row.str());
  }
  const auto expected=c.sourceOpeningStations.find(role.handle);
  if(expected!=c.sourceOpeningStations.end()){
   const double stationTolerance=.01/c.unitMM;
   const auto& sourceStations=expected->second;
   require(role.row==3&&stations.size()==sourceStations.size(),"opening chain source station count");
   for(std::size_t i=0;i<stations.size();++i){
    const double station=vertical?stations[i].y:stations[i].x;
    require(std::abs(station-sourceStations[i])<=stationTolerance,"opening chain station differs from source");
   }
   for(std::size_t i=0;i<values.size();++i){
    auto* dimension=AcDbAlignedDimension::cast(static_cast<AcRxObject*>(parts.values[static_cast<int>(i)]));
    const double expectedDistance=sourceStations[i+1]-sourceStations[i];
    double measured{};
    AcString overrideText,shown;
    require(std::abs(values[i]-expectedDistance)<=stationTolerance&&
       dimension->measurement(measured)==Acad::eOk&&std::abs(measured-expectedDistance)<=stationTolerance,
       "opening chain measure differs from source");
    require(dimension->dimensionText(overrideText)==Acad::eOk&&
       (overrideText.isEmpty()||std::wstring(overrideText.kACharPtr())==L"<>")&&
       dimension->formatMeasurement(shown,measured,overrideText.isEmpty()?nullptr:overrideText.kACharPtr())==Acad::eOk&&
       !shown.isEmpty(),"opening chain display override or unavailable measurement");
    require(std::abs(dimension->dimlfac()-c.unitMM)<=1e-7,"opening chain display measurement factor");
   }
   proof<<",\"sourceStationsChecked\":true";
   if(savedReadback&&evidence){
    std::ostringstream row;
    row<<std::setprecision(17)<<"{\"panel\":"<<role.panel+1<<",\"side\":"<<role.side
       <<",\"object\":\""<<axisHandle(native)<<"\",\"stations\":"<<wholePoints(stations)<<",\"values\":[";
    for(std::size_t i=0;i<values.size();++i){if(i)row<<',';row<<values[i];}
    row<<"],\"savedReopened\":true,\"report\":\"外围间距核验.json\",\"result\":\"verified-opening-source-stations\"}";
    productRecordRuleEvidence(*evidence,"D24",row.str());
    productRecordRuleEvidence(*evidence,"D05",row.str());
   }
  }
  proof<<",\"stations\":"<<wholePoints(stations)<<",\"values\":[";
  for(std::size_t i=0;i<values.size();++i){if(i)proof<< ',';proof<<values[i];}
  proof<<"],\"buildingFacingExtent\":";
  // The whole entity may also have text or leaders on the building side of the witness.
  AcDbExtents extents;
  if(entity->getGeomExtents(extents)==Acad::eOk)proof<<normal(sign>0?extents.minPoint():extents.maxPoint());else proof<<"null";
  proof<<'}';
 }
 if(savedReadback&&evidence)for(const auto& [side,pair]:axisPairs){
  const auto& total=pair[0];
  const auto& spans=pair[1];
  if(!total||!spans)continue;
  const double sum=std::accumulate(spans->values.begin(),spans->values.end(),0.);
  const double overall=std::abs(total->last-total->first);
  const bool endpoints=std::abs(total->first-spans->first)*c.unitMM<.01&&
                       std::abs(total->last-spans->last)*c.unitMM<.01;
  const bool closed=endpoints&&total->values.size()==1&&std::abs(overall-total->values.front())*c.unitMM<.01&&
                    std::abs(overall-sum)*c.unitMM<.01;
  for(const auto* rule:{"D02","D03","D11"}){
   std::ostringstream row;
   row<<std::setprecision(17)<<"{\"panel\":"<<side.first+1<<",\"side\":"<<side.second
      <<",\"object\":\""<<(std::string(rule)=="D02"?total->handle:spans->handle)
      <<"\",\"totalChain\":\""<<total->handle<<"\",\"axisChain\":\""<<spans->handle
      <<"\",\"firstStation\":"<<total->first<<",\"lastStation\":"<<total->last
      <<",\"overall\":"<<overall<<",\"segmentSum\":"<<sum
      <<",\"savedReopened\":true,\"report\":\"外围间距核验.json\",\"result\":\""
      <<(closed?"verified-axis-chain-closure":"pending")<<"\"}";
   productRecordRuleEvidence(*evidence,rule,row.str());
  }
 }
 proof<<"],\"axes\":[";
 std::map<std::pair<int,int>,std::vector<std::tuple<double,double,double,std::string>>> circlesBySide;
 first=true;
 for(const auto& role:c.axes){
  require(role.panel>=0&&role.panel<c.panels.size(),"final axis panel association");
  const auto failed=c.axisLayoutFailures.find(role.handle);
  if(failed!=c.axisLayoutFailures.end()){
   if(!first)proof<<',';first=false;
   proof<<"{\"handle\":\""<<role.handle<<"\",\"verified\":false,\"retainedPrevious\":true,\"reason\":"
        <<jsonUtf8(bcpproduct::decodeMessage(failed->second))<<'}';
   if(savedReadback&&evidence)productRecordRuleEvidence(*evidence,"A28",std::string("{\"object\":\"")+role.handle+
      "\",\"result\":\"pending-retained-after-native-candidate-failure\"}");
   continue;
  }
  const auto native=id(role.handle);
  const auto grips=chainGrips(native);
  require(grips.size()==4||grips.size()==8,"final native plan axis grips");
  const bool vertical=role.side!=0;
  const auto panel=productPlacementBounds(c,role.panel);
  AxisInk ink;
  {
   auto entity=open<AcDbEntity>(native);
   require(!entity->isAProxy()&&std::wstring(entity->isA()->name())==L"TDbAxisLabelSet","final native plan axis type");
   axisInkLeaves(*entity,ink);
  }
  require(ink.circles.size()==grips.size()/4,"final plan axis circle coverage");
  if(!first)proof<<',';
  first=false;
  proof<<"{\"handle\":\""<<axisHandle(native)<<"\",\"grips\":"<<wholePoints(grips)<<",\"ends\":[";
  for(std::size_t base=0;base<grips.size();base+=4){
   const auto& lead=grips[base];
   const auto& centre=grips[base+1];
   const auto end=productAxisEnd(panel,vertical,lead,centre);
   const double expectedLead=end.datum+end.sign*layout.start;
   const double expectedCentre=end.datum+end.sign*layout.circleCentre;
   const auto fold=productFoldPoints(db,c,role.handle,end.side);
   const auto anchor=fold.empty()?lead:fold.front();
   require(std::abs(productAxisNormal(anchor,vertical)-expectedLead)<tolerance,"final plan axis anchor differs from rules");
   if(fold.empty())require(std::abs(productAxisStation(lead,vertical)-productAxisStation(centre,vertical))<tolerance,
           "unshifted native straight tail must stay on the real axis");
   if(!fold.empty()){
    const auto sign=end.sign;
    require(fold.size()==2&&fold.back().distanceTo(lead)<tolerance&&
            std::abs(productAxisStation(lead,vertical)-productAxisStation(anchor,vertical))<tolerance&&
            sign*productAxisNormal(lead,vertical)>productAxisFoldBandStart(db,c,role.panel,end.side,layout,id)+tolerance&&
            sign*(productAxisNormal(lead,vertical)-productAxisNormal(anchor,vertical))>tolerance&&
            std::abs(std::abs(productAxisNormal(centre,vertical)-productAxisNormal(lead,vertical))-
                     std::abs(productAxisStation(centre,vertical)-productAxisStation(lead,vertical)))<tolerance,
            "A28 requires one outer bend and a 45-degree radial native tail");
   }
   require(std::abs(productAxisNormal(centre,vertical)-expectedCentre)<tolerance,"final plan axis circle differs from rules");
   const auto circle=std::find_if(ink.circles.begin(),ink.circles.end(),[&](const auto& drawn){return drawn.centre.distanceTo(centre)<tolerance;});
   require(circle!=ink.circles.end(),"final axis rendered circle differs from grip");
   require(std::abs(circle->radius-layout.circleDiameter/2)<tolerance,"final plan axis circle radius differs from rules");
   const auto junction=centre+(lead-centre).normal()*circle->radius;
   const auto at=[&](const auto& a,const auto& b){return a.distanceTo(b)<tolerance;};
   require(lead.distanceTo(centre)>circle->radius+tolerance&&std::any_of(ink.lines.begin(),ink.lines.end(),[&](const auto& line){
    return (at(line.first,lead)&&at(line.second,junction))||(at(line.second,lead)&&at(line.first,junction));
   }),"final plan axis rendered lead differs from rules");
   if(!fold.empty())require(productAxisExternalStrokeCount(ink,centre,circle->radius)==1,"A28 radial tail contains an extra straight segment before the circle");
   if(savedReadback&&evidence){
    std::ostringstream leadProof;
    leadProof<<std::setprecision(17)<<"{\"panel\":"<<role.panel+1<<",\"side\":"<<end.side
      <<",\"object\":\""<<axisHandle(native)<<"\",\"anchor\":"<<wholePoints({anchor})
      <<",\"circleEdge\":"<<wholePoints({junction})<<",\"outerDimensionToCircleNearPaperMM\":"
      <<(layout.circleCentre-layout.circleDiameter/2-layout.total)*c.unitMM/c.scale
      <<",\"nativeLeadPaperMM\":"<<lead.distanceTo(junction)*c.unitMM/c.scale
      <<",\"fold\":"<<wholePoints(fold)
      <<",\"savedReopened\":true,\"report\":\"外围间距核验.json\",\"result\":\"verified-paper-spacing-and-actual-lead\"}";
    productRecordRuleEvidence(*evidence,"A13",leadProof.str());
   }
   const auto station=productAxisStation(anchor,vertical);
   const auto circleStation=productAxisStation(centre,vertical);
   require(std::abs(circleStation-station)<=c.scale/c.unitMM*bcpproduct::PlanPaperPolicy::circleMaxShift+tolerance,
           "axis circle lateral shift exceeds the paper limit");
   circlesBySide[{role.panel,end.side}].push_back({station,circleStation,circle->radius,role.handle});
   if(base)proof<<',';
   proof<<"{\"side\":"<<end.side<<",\"datum\":"<<end.datum<<",\"lead\":"<<productAxisNormal(anchor,vertical)
        <<",\"circleCentre\":"<<productAxisNormal(centre,vertical)<<",\"circleRadius\":"<<circle->radius
        <<",\"station\":"<<station<<",\"circleStation\":"<<circleStation
        <<",\"lateralShift\":"<<circleStation-station<<",\"realAnchor\":"<<wholePoints({anchor})
        <<",\"nativeLead\":"<<wholePoints({lead})<<",\"fold\":"<<wholePoints(fold)<<'}';
  }
  proof<<"]}";
 }
 for(const auto& role:c.frames){
  require(role.panel>=0&&role.panel<c.panels.size()&&role.row>=0&&role.row<4,"frame role");
  const auto frame=open<AcDbPolyline>(id(role.handle));
  require(frame->isClosed()&&frame->numVerts()==4,"final plan projection frame shape");
  const auto corners=productPlanFrameCorners(productPlacementBounds(c,role.panel),offsets[role.row]);
  for(unsigned int k=0;k<corners.size();++k){
   AcGePoint2d actual;
   productStatus(frame->getPointAt(k,actual),"read final plan projection frame");
   require(actual.distanceTo(corners[k])<tolerance,"final plan projection frame differs from rules");
  }
  if(savedReadback&&evidence){
   const auto layer=open<AcDbLayerTableRecord>(frame->layerId());
   const auto type=open<AcDbLinetypeTableRecord>(frame->linetypeId());
   const bool onDefpoints=symbolName(frame->layerId())==L"Defpoints";
   const bool nonplot=onDefpoints&&!layer->isPlottable();
   const double factor=frame->linetypeScale()*db.ltscale()*c.unitMM/c.scale;
   const bool twoParts=type->numDashes()==2;
   const double dash=twoParts?type->dashLengthAt(0)*factor:0.;
   const double space=twoParts?-type->dashLengthAt(1)*factor:0.;
   const bool appearance=onDefpoints&&frame->colorIndex()==256&&layer->color().colorIndex()==8&&
                         std::abs(dash-6.)<tolerance&&std::abs(space-3.)<tolerance;
   std::ostringstream row;
   row<<std::setprecision(17)<<"{\"panel\":"<<role.panel+1<<",\"row\":"<<role.row+1<<",\"object\":\""<<role.handle
      <<"\",\"layer\":"<<jsonUtf8(symbolName(frame->layerId()))<<",\"layerACI\":"<<layer->color().colorIndex()
      <<",\"entityACI\":"<<frame->colorIndex()<<",\"plotEnabled\":"<<(layer->isPlottable()?"true":"false")
      <<",\"dashPaperMM\":"<<dash<<",\"gapPaperMM\":"<<space<<",\"savedReopened\":true,";
   const auto prefix=row.str();
   productRecordRuleEvidence(*evidence,"Y07",prefix+"\"result\":\""+(appearance?"verified-frame-appearance":"pending")+"\"}");
   productRecordRuleEvidence(*evidence,"Y08",prefix+"\"result\":\""+(nonplot?"verified-layer-nonplot":"pending")+"\"}");
   productRecordRuleEvidence(*evidence,"A14",prefix+"\"result\":\"verified-frame-placement\"}");
  }
 }
 proof<<"],\"axisCircleGroups\":[";
 bool firstGroup=true;
 const double paper=c.unitMM/c.scale;
 for(auto& [key,circles]:circlesBySide){
  std::sort(circles.begin(),circles.end());
  const auto conflict=c.axisCircleConflicts.find(key);
  double minGap=std::numeric_limits<double>::infinity();
  for(std::size_t i=1;i<circles.size();++i){
   const auto separation=std::get<1>(circles[i])-std::get<1>(circles[i-1]);
   if(conflict==c.axisCircleConflicts.end())require(separation>0,"axis circle order follows the real axis stations");
   minGap=std::min(minGap,separation-std::get<2>(circles[i])-std::get<2>(circles[i-1]));
  }
  if(conflict==c.axisCircleConflicts.end())
   require(minGap*paper+tolerance>=bcpproduct::PlanPaperPolicy::circleClearance,"saved circle edge clearance");
  if(!firstGroup)proof<<',';
  firstGroup=false;
  proof<<"{\"view\":"<<key.first+1<<",\"side\":"<<key.second<<",\"circleCount\":"<<circles.size()
       <<",\"minimumEdgeGapPaperMM\":";
  if(std::isfinite(minGap))proof<<minGap*paper;else proof<<"null";
  proof<<",\"maximumAllowedTrialShiftPaperMM\":"<<bcpproduct::PlanPaperPolicy::circleMaxShift
       <<",\"pending\":"<<(conflict==c.axisCircleConflicts.end()?"null":jsonUtf8(bcpproduct::decodeMessage(conflict->second)))
       <<",\"axisHandles\":[";
  bool firstHandle=true;
  for(const auto& circle:circles){if(!firstHandle)proof<<',';firstHandle=false;proof<<'"'<<std::get<3>(circle)<<'"';}
  proof<<"]}";
  if(savedReadback&&evidence)for(const auto& [station,centre,radius,handle]:circles){
   std::ostringstream row;
   row<<std::setprecision(17)<<"{\"panel\":"<<key.first+1<<",\"side\":"<<key.second
      <<",\"object\":\""<<handle<<"\",\"station\":"<<station<<",\"circleStation\":"<<centre
      <<",\"shiftPaperMM\":"<<(centre-station)*paper<<",\"radiusPaperMM\":"<<radius*paper
      <<",\"minimumEdgeGapPaperMM\":";
   if(std::isfinite(minGap))row<<minGap*paper;else row<<"null";
   row<<",\"savedReopened\":true,\"report\":\"外围间距核验.json\",\"result\":\""
      <<(conflict==c.axisCircleConflicts.end()?"verified-axis-circle-placement":"pending")<<"\"}";
   productRecordRuleEvidence(*evidence,"A14",row.str());
  }
 }
 proof<<"],\"sidePlacement\":[";
 const auto& wallAxes=c.placementAxes.empty()?c.panels:c.placementAxes;
 bool firstSide=true;
 for(std::size_t p=0;p<c.sidePlacement.size();++p){
  for(int side=0;side<4;++side){
   if(!firstSide)proof<<',';
   firstSide=false;
   const auto& chosen=c.sidePlacement[p][side];
   proof<<"{\"view\":"<<p+1<<",\"side\":"<<side
        <<",\"axisCoordinate\":"<<productPlanDatum(wallAxes[p],side)
        <<",\"coordinate\":"<<chosen.coordinate
        <<",\"basis\":\""<<(chosen.eave.empty()?"wall-or-column-axis":"verified-eave")<<"\""
        <<",\"sourceEdge\":\""<<chosen.eave<<"\"}";
  }
 }
 proof<<"],\"frames\":"<<c.frames.size()<<",\"scope\":\"plan only; no elevations or sections module\"}";
 if(!report.empty())writeNew(report,proof.str());
 if(savedReadback&&evidence)for(const auto& issue:productAuditStoredAxisEnds(db,evidence->root/L"axis-expected-readback.json"))
  productRecordPlanAxisFailure(*evidence,0,{},issue.first,issue.second);
}
void productCommitExpectedPlanAxes(ProductJob& job,AcDbDatabase& db,ProductPlanLayoutConfig& c){
 const auto id=[&](const std::string& handle){return find(db,handle);};
 for(auto& expected:c.expectedAxes){
  if(expected.pendingSources.empty())continue;
  const auto sources=expected.pendingSources;
  try{
   require(expected.issue.empty()&&expected.outputs.size()==static_cast<std::size_t>(expected.negative+expected.positive),
           "complete staged source-axis replacement required");
   for(const bool positive:{false,true})if(positive?expected.positive:expected.negative){
    const int side=(expected.vertical?2:0)+(positive?1:0);
    const auto conflict=c.axisCircleConflicts.find({expected.panel,side});
    require(conflict==c.axisCircleConflicts.end(),conflict==c.axisCircleConflicts.end()?"":"joint A28 layout failed before source retirement");
   }
   auto check=c;check.axes.clear();check.dimensions.clear();check.frames.clear();check.expectedAxes.clear();
   for(const auto& handle:expected.outputs){
    require(!c.axisLayoutFailures.count(handle),"staged native axis layout failed");
    require(productAxisCaption(id(handle))==bcpproduct::decodeMessage(expected.number),"staged axis differs from approved number");
    const auto role=std::find_if(c.axes.begin(),c.axes.end(),[&](const auto& item){return item.handle==handle;});
    require(role!=c.axes.end(),"staged axis role missing");check.axes.push_back(*role);
   }
   productVerifyPlanLayout(db,check,id,{});
   std::vector<AcDbObjectId> previous;for(const auto& handle:sources)previous.push_back(id(handle));
   for(const auto& [side,handle]:expected.sourceFolds)previous.push_back(id(handle));
   productRetireAxisGroup(db,previous);
   for(auto it=c.axisFolds.begin();it!=c.axisFolds.end();){
    if(std::find(sources.begin(),sources.end(),it->first.first)!=sources.end())it=c.axisFolds.erase(it);else ++it;
   }
  }catch(const std::exception& error){
   expected.issue=std::string("source retained after complete layout/commit failure: ")+error.what();
   const std::set<std::string> candidates(expected.outputs.begin(),expected.outputs.end());
   std::vector<AcDbObjectId> discard;
   for(auto it=c.axisFolds.begin();it!=c.axisFolds.end();){
    if(candidates.count(it->first.first)){discard.push_back(id(it->second));it=c.axisFolds.erase(it);}else ++it;
   }
   for(const auto& handle:candidates){const auto native=id(handle);if(!native.isErased())discard.push_back(native);c.axisOriginalHandles.erase(handle);c.axisLayoutFailures.erase(handle);}
   productRetireAxisGroup(db,discard);
   c.axes.erase(std::remove_if(c.axes.begin(),c.axes.end(),[&](const auto& role){return candidates.count(role.handle)!=0;}),c.axes.end());
   for(const auto& handle:sources){
    require(!id(handle).isErased(),"failed candidate commit lost a frozen source axis");
    if(std::none_of(c.axes.begin(),c.axes.end(),[&](const auto& role){return role.handle==handle;}))c.axes.push_back({handle,expected.panel,expected.vertical?1:0,0});
    c.axisOriginalHandles[handle]=expected.original;c.axisLayoutFailures[handle]=expected.issue;
   }
   expected.outputs.clear();
   const int source=static_cast<std::size_t>(expected.panel)<job.layoutSourcePanels.size()?job.layoutSourcePanels[expected.panel]:expected.panel+1;
   productRecordPlanAxisFailure(job,source,expected.number,expected.original,expected.issue);
  }
  expected.pendingSources.clear();
 }
}

void productPlanLayout(AcDbDatabase& db,ProductPlanLayoutConfig& c,const std::function<AcDbObjectId(const std::string&)>& id,const fs::path& report={},ProductJob* job=nullptr){
 Working work(&db);const auto layout=bcpproduct::planAnnotationLayout(c.scale,c.unitMM,c.paper);const auto axisLayer=productPlanLayer(db,L"AXIS",3),pubLayer=productPlanLayer(db,L"PUB_DIM",25),defLayer=productPlanLayer(db,L"Defpoints",8,false);productPlanLayer(db,L"DOTE",24);productPlanLayer(db,L"A-房间名称",7);productPlanLayer(db,L"DIM_ELEV",3);productPlanLayer(db,L"DIM_SYMB",3);productPlanLayer(db,L"WINDOW_TEXT",7);
 productTraceAxisLayout(job,{},"dimension-locations-enter");
 for(const auto& r:c.dimensions){require(r.panel>=0&&r.panel<c.panels.size()&&r.row>=1&&r.row<=3,"dimension role range");const auto native=id(r.handle);const auto before=chainGrips(native);const auto values=chainValues(native);require(before.size()==2*values.size()+6,"native overall dimension chain");const bool vertical=r.side<2;const double sign=(r.side==0||r.side==2)?-1:1,wall=productPlacementDatum(c,r.panel,r.side),distance=r.row==1?layout.total:r.row==2?layout.second:layout.third,target=wall+sign*distance;const auto delta=vertical?AcGeVector3d(target-before[0].x,0,0):AcGeVector3d(0,target-before[0].y,0);
 {auto e=editObject<AcDbEntity>(native);require(std::wstring(e->isA()->name())==L"TDbDimension2","real plan dimension");productStatus(e->transformBy(AcGeMatrix3d::translation(delta)),"position complete plan dimension chain");productStatus(e->setLayer(r.row==3?pubLayer:axisLayer),"plan dimension layer");productStatus(e->setColorIndex(256),"plan dimension ByLayer");}
 double gap{};{auto e=open<AcDbEntity>(native);TemplateParts pieces(e.get());productStatus(e->explode(pieces.values),"actual dimension extension offset");require(pieces.values.length()>0,"actual dimension body");auto* dim=AcDbDimension::cast(static_cast<AcRxObject*>(pieces.values[0]));require(dim!=nullptr,"dimension display object");gap=dim->dimexo()*dim->dimscale();}
 const double definition=wall+sign*layout.start-sign*gap;auto grip=chainGrips(native);for(std::size_t station=2;station<=values.size()+2;++station){auto point=grip[station];if(vertical)point.x=definition;else point.y=definition;moveDimensionGrip(native,static_cast<int>(station),point);}moveDimensionGrip(native,0,grip[0]);for(std::size_t textIndex=values.size()+3;textIndex<=2*values.size()+2;++textIndex)moveDimensionGrip(native,static_cast<int>(textIndex),grip[textIndex]);
 const auto after=chainGrips(native);const auto measured=chainValues(native);require(measured.size()==values.size(),"dimension segment count preserved");for(std::size_t k=0;k<values.size();++k)require(std::abs(measured[k]-values[k])<1e-5,"layout changed actual dimension measurement");require(std::abs((vertical?after[0].x:after[0].y)-target)<1e-5,"saved dimension baseline position");
 }
 productTraceAxisLayout(job,{},"axis-labels-enter");
 const auto fitAxis=[&](const ProductPlanRole& role){
  productTraceAxisLayout(job,role.handle,"axis-label-enter");
  require(role.panel>=0&&role.panel<c.panels.size(),"axis panel association");
  const auto native=id(role.handle);
  const auto original=chainGrips(native);
  require(original.size()==4||original.size()==8,"native axis-label geometry");
  const bool vertical=role.side!=0;
  const auto panel=productPlacementBounds(c,role.panel);
  const auto expectedEnd=[&](std::size_t base){
   if(job&&original.size()==8&&
      std::abs(productAxisNormal(original[base+1],vertical)-productAxisNormal(original[base],vertical))<=1e-5){
    const double first=productAxisNormal(original[1],vertical),last=productAxisNormal(original[5],vertical);
    const int lower=vertical?2:0;
    // Two circles beyond opposite ends prove orientation even when a native
    // trial grip has collapsed onto its circle.
    if(std::min(first,last)<productPlanDatum(panel,lower)&&std::max(first,last)>productPlanDatum(panel,lower+1)){
     const bool positive=(base==0)==(first>last);
     const int side=lower+(positive?1:0);
     return ProductAxisEnd{side,productPlanDatum(panel,side),positive?1.:-1.};
    }
   }
   return productAxisEnd(panel,vertical,original[base],original[base+1]);
  };
  // Place both circles before fixing the leads. The native endpoint operation
  // preserves both circle positions and all other grips and stretch points.
  for(std::size_t base=0;base<original.size();base+=4){
   const auto end=expectedEnd(base);
   const auto grips=chainGrips(native);
   const auto& centre=grips[base+1];
   const auto target=productAxisAtNormal(centre,vertical,end.datum+end.sign*layout.circleCentre);
   if(centre.distanceTo(target)>1e-6){
    auto entity=editObject<AcDbEntity>(native);
    AcDbIntArray indices;
    indices.append(static_cast<int>(base+2));
    productStatus(entity->moveGripPointsAt(indices,target-centre),"move native axis circle with public grip");
   }
  }
  for(std::size_t base=0;base<original.size();base+=4){
   const auto end=expectedEnd(base);
   const auto grips=chainGrips(native);
   const auto& lead=grips[base];
   const auto target=productAxisAtNormal(lead,vertical,end.datum+end.sign*layout.start);
   if(lead.distanceTo(target)>1e-6)approvedgrips::endpoint(db,native,lead,target);
  }
  {
   auto entity=editObject<AcDbEntity>(native);
   productStatus(entity->setLayer(axisLayer),"plan axis layer");
   productStatus(entity->setColorIndex(256),"plan axis ByLayer");
  }
  const auto single=c.keepNegativeAxisEnd.find(role.handle);
  if(single!=c.keepNegativeAxisEnd.end()){
   const auto grips=chainGrips(native);
   require(grips.size()==8,"single-side edit starts from both fitted ends");
   const bool firstNegative=productAxisNormal(grips[1],vertical)<productAxisNormal(grips[5],vertical);
   productKeepAxisLabelEnd(db,native,vertical,single->second==firstNegative?grips[1]:grips[5],false,job);
  }
  const auto actual=chainGrips(native);
  require(actual.size()==4||actual.size()==8,"fitted axis has no readable ends");
  for(std::size_t base=0;base<actual.size();base+=4)
   productAxisEnd(panel,vertical,actual[base],actual[base+1]);
  productTraceAxisLayout(job,role.handle,"axis-label-returned");
 };
 if(job&&job->currentPlanRules&&!c.expectedAxes.empty()){productFreezeExpectedAxisEnds(db,c,layout);productBuildExpectedPlanAxes(*job,db,c,axisLayer,layout);}
 else for(auto& role:c.axes){
  const auto previous=role.handle;const auto native=id(previous);const auto original=chainGrips(native);
  const auto trial=productCloneAxisLabel(db,native);auto candidate=role;candidate.handle=axisHandle(trial);
  if(c.keepNegativeAxisEnd.count(previous))c.keepNegativeAxisEnd[candidate.handle]=c.keepNegativeAxisEnd.at(previous);
  try{
   fitAxis(candidate);
   productStatus(editObject<AcDbEntity>(native)->erase(),"replace old axis only after complete candidate succeeds");
   c.axisOriginalGrips[previous]=original;c.axisOriginalHandles[candidate.handle]=previous;role=candidate;
  }catch(const std::exception& error){
   if(!trial.isErased())productStatus(editObject<AcDbEntity>(trial)->erase(),"discard failed axis candidate only");
   c.axisLayoutFailures[previous]=error.what();
   if(job){const auto source=static_cast<std::size_t>(role.panel)<job->layoutSourcePanels.size()?job->layoutSourcePanels[role.panel]:static_cast<std::size_t>(role.panel)+1;
    productRecordPlanAxisFailure(*job,source,{},previous,std::string("kept previous axis after candidate failure: ")+error.what());}
  }
 }
 productTraceAxisLayout(job,{},"circle-arrangement-enter");
 productArrangeAxisCircles(db,c,axisLayer,layout,id,job);
 if(job&&!c.expectedAxes.empty())productCommitExpectedPlanAxes(*job,db,c);
 if(job&&!c.expectedAxes.empty()){productWriteExpectedPlanAxes(*job,c);productFreezeExpectedAxisEnds(db,c,layout);}
 productTraceAxisLayout(job,{},"circle-arrangement-returned");
 const std::array<double,4> offsets{layout.start,layout.third,layout.second,layout.total};
 for(const auto& r:c.frames){require(r.panel>=0&&r.panel<c.panels.size()&&r.row>=0&&r.row<4,"frame role");auto e=editObject<AcDbPolyline>(id(r.handle));require(e->isClosed()&&e->numVerts()==4,"four closed plan projection frames");const auto corners=productPlanFrameCorners(productPlacementBounds(c,r.panel),offsets[r.row]);for(unsigned k=0;k<corners.size();++k)productStatus(e->setPointAt(k,corners[k]),"plan frame location");productStatus(e->setLayer(defLayer),"nonplot grey frame");productStatus(e->setColorIndex(256),"frame ByLayer");e->setPlinegen(Adesk::kTrue);}
 if(!report.empty())productVerifyPlanLayout(db,c,id,report);
}
