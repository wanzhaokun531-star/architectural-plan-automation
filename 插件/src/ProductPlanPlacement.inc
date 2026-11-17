// The project file identifies verified source edges. Positions are always
// read from the owned DWG, including the actual closing segment when present.
void productResolvePlanPlacement(ProductJob& job, AcDbDatabase& db, ProductPlanLayoutConfig& config) {
 const auto spacing=bcpproduct::planAnnotationLayout(config.scale,config.unitMM,config.paper);
 const auto& wallAxes=config.placementAxes.empty()?config.panels:config.placementAxes;
 require(wallAxes.size()==config.panels.size(),"perimeter axis and layout panel counts");
 using SideEdges=std::array<std::vector<bcpproduct::PlanEaveSegment>,4>;
 std::vector<SideEdges> eaves(config.panels.size());
 std::ifstream source(job.profile/L"plan-policy/roof-eaves.tsv");
 if(source){
  std::string tag,digest;
  require(bool(source>>tag>>digest)&&tag=="PLANROOFEAVES1","roof eave source configuration");
  if(digest==job.sourceHash){
   bool complete=false;
   while(source>>tag){
    if(tag=="END"){complete=true;break;}
    int panel{},segment{};
    std::string handle;
    require(bool(source>>panel>>handle>>segment)&&(tag=="EAVE"||tag=="PENDING")&&
        panel>0&&static_cast<std::size_t>(panel)<=config.panels.size()&&segment>0&&
        handle.find_first_not_of("0123456789ABCDEFabcdef")==std::string::npos,"roof eave reference");
    const auto reference=handle+":"+std::to_string(segment);
    if(tag=="PENDING"){
     job.detailPending.push_back({u8"屋面排布",reference,u8"檐边或收边待确认，维持本侧有依据的基准"});
     continue;
    }
    try{
     const auto edge=open<AcDbEntity>(find(db,handle));
     require(edge->visibility()==AcDb::kVisible,"confirmed roof source must be visible");
     AcGePoint3d start,end;
     if(const auto* poly=AcDbPolyline::cast(edge.get())){
      const auto n=poly->numVerts();
      const auto index=static_cast<unsigned>(segment-1);
      require(n>=2&&index<n&&(index+1<n||poly->isClosed()),"actual roof polyline segment");
      double bulge{};
      status(poly->getBulgeAt(index,bulge),"read actual roof segment shape");
      require(std::abs(bulge)<1e-7,"confirmed straight roof segment required");
      status(poly->getPointAt(index,start),"read roof edge start");
      status(poly->getPointAt((index+1)%n,end),"read roof edge end");
     }else{
      const auto* line=AcDbLine::cast(edge.get());
      require(line&&segment==1,"confirmed roof line segment");
      start=line->startPoint();end=line->endPoint();
     }
     require(std::isfinite(start.x)&&std::isfinite(start.y)&&std::isfinite(end.x)&&std::isfinite(end.y)&&
             start.distanceTo(end)>1e-7,"finite nondegenerate roof edge");
     auto& sides=eaves[panel-1];
     for(int side=0;side<4;++side){
      sides[side].push_back(side<2?
       bcpproduct::PlanEaveSegment{start.x,start.y,end.x,end.y,reference}:
       bcpproduct::PlanEaveSegment{start.y,start.x,end.y,end.x,reference});
     }
    }catch(const std::exception&){
     job.detailPending.push_back({u8"屋面排布",reference,u8"该檐边不能从本图核实，保留有依据的原排布"});
    }
   }
   source>>std::ws;
   require(complete&&source.eof(),"complete roof eave source configuration");
  }
 }
 config.sidePlacement.clear();
 for(std::size_t panel=0;panel<wallAxes.size();++panel){
  std::array<bcpproduct::PlanSideDatum,4> sides{};
  const auto& bounds=wallAxes[panel];
  const auto& stations=config.panels[panel];
  for(int side=0;side<4;++side){
   const double low=side<2?stations.ymin:stations.xmin;
   const double high=side<2?stations.ymax:stations.xmax;
   // The first visible projection frame also occupies both corner returns.
   sides[side]=bcpproduct::resolvePerimeterDatum(productPlanDatum(bounds,side),side%2?1:-1,
      low-spacing.start,high+spacing.start,spacing.start,eaves[panel][side]);
  }
  config.sidePlacement.push_back(std::move(sides));
 }
}

double productPlacementDatum(const ProductPlanLayoutConfig& config,int panel,int side){
 return config.sidePlacement.at(panel).at(side).coordinate;
}

ProductPlanBounds productPlacementBounds(const ProductPlanLayoutConfig& config,int panel){
 return {productPlacementDatum(config,panel,0),productPlacementDatum(config,panel,2),
         productPlacementDatum(config,panel,1),productPlacementDatum(config,panel,3)};
}
