struct PlanLayoutInk {
    std::string handle;
    std::vector<AcDbExtents> boxes;
    std::vector<InkSegment> lines;
    AcDbExtents bounds;
    ProductPlanBounds xy;
    bool openingCaption{};

    void captureXY() {
        const auto low=bounds.minPoint(),high=bounds.maxPoint();
        xy={low.x,low.y,high.x,high.y};
    }
};

struct PlanChainLayout {
    AcGePoint3d baseline;
    std::vector<AcGePoint3d> textGrips;
    GenericDimensionBoxes boxes;
    std::vector<InkSegment> lines, captionStrokes;
    AcDbExtents coverage;
    int fixedHits{};
    double displacement{};
    bool defaultText{};
};

void boundPlanPlacement(PlanChainLayout& placement) {
    placement.coverage=AcDbExtents(placement.baseline,placement.baseline);
    for(const auto& box:placement.boxes)if(box)placement.coverage.addExt(*box);
    for(const auto& [a,b]:placement.lines){placement.coverage.addPoint(a);placement.coverage.addPoint(b);}
    for(const auto& [a,b]:placement.captionStrokes){placement.coverage.addPoint(a);placement.coverage.addPoint(b);}
}

struct PlanLayoutChain {
    AcDbObjectId id;
    AcGeVector3d along, above;
    std::vector<AcGePoint3d> stations;
    AcDbExtents reach;
    std::set<int> rooms;
    const ConfirmedPlanRoom* confirmed{};
    std::vector<const PlanLayoutInk*> fixed;
    bool stock{}, vertical{}, added{}, writable{true}, lockBaseline{};
    double modelPerPaperMm{};
    PlanChainLayout original;
    std::vector<PlanChainLayout> options;
};

struct PlanLayoutGroup {
    int panel{};
    std::vector<std::size_t> members;
    std::set<int> rooms;
    std::map<std::size_t,std::string> confirmedRooms;
    int before{}, written{}, after{}, overlaps{}, unreadable{}, beforeSide{}, sideIssues{};
    int segmentAssociationIssues{};
    std::map<std::string,AcGePoint3d> originalBaselines;
};

constexpr int planCaptionAvoidanceWeight=4;

double planDot(const AcGePoint3d& p,const AcGeVector3d& direction) {
    return (p-AcGePoint3d::kOrigin).dotProduct(direction);
}

std::array<AcGePoint3d,4> planBoxCorners(const AcDbExtents& box) {
    const auto& a=box.minPoint();
    const auto& b=box.maxPoint();
    return {a,AcGePoint3d(a.x,b.y,a.z),b,AcGePoint3d(b.x,a.y,b.z)};
}

bool planWholeBoxAbove(const AcDbExtents& box,const AcGeVector3d& above,double baseline,double tolerance=1e-5) {
    const auto corners=planBoxCorners(box);
    return std::all_of(corners.begin(),corners.end(),[&](const auto& p){return planDot(p,above)>baseline+tolerance;});
}

bool planWithinRequestedSide(const PlanLayoutChain& chain,const AcGePoint3d& point) {
    if(!chain.added)return true;
    const auto station=planDot(chain.stations.front(),chain.above);
    const double original=planDot(chain.original.baseline,chain.above)-station;
    return std::abs(original)<1e-5||(planDot(point,chain.above)-station)*original>0;
}

bool planBoxInLayoutRoom(const PlanLayoutChain& chain,const AcDbExtents& box) {
    if(!chain.added&&!chain.confirmed)return true;
    const auto corners=planBoxCorners(box);
    return std::all_of(corners.begin(),corners.end(),[&](const auto& p){
        return planWithinRequestedSide(chain,p)&&
            (!chain.confirmed||genericInside(chain.confirmed->vertices,{p.x,p.y}));
    });
}

ProductPlanBounds planDimensionPlacementBounds(const PlanLayoutChain& chain,ProductPlanBounds panel,double paper) {
    if(!chain.added||chain.confirmed)return panel;
    // The wall-axis frame can exclude native local details on the outer wall
    // face. Give an existing exterior-side baseline room to reflow locally.
    const auto middle=chain.stations.front()+(chain.stations.back()-chain.stations.front())*.5;
    const auto line=middle+chain.above*((chain.original.baseline-middle).dotProduct(chain.above));
    const double local=8*paper;
    if(line.x<panel.xmin)panel.xmin=line.x-local;
    if(line.x>panel.xmax)panel.xmax=line.x+local;
    if(line.y<panel.ymin)panel.ymin=line.y-local;
    if(line.y>panel.ymax)panel.ymax=line.y+local;
    return panel;
}

std::pair<double,double> planProjectedSpan(const AcDbExtents& box,const AcGeVector3d& direction) {
    if(direction.z==0.){
        const auto low=box.minPoint(),high=box.maxPoint();
        const AcGePoint3d first(direction.x<0?high.x:low.x,direction.y<0?high.y:low.y,0.);
        const AcGePoint3d last(direction.x<0?low.x:high.x,direction.y<0?low.y:high.y,0.);
        return {planDot(first,direction),planDot(last,direction)};
    }
    const auto corners=planBoxCorners(box);
    auto limits=std::minmax_element(corners.begin(),corners.end(),[&](const auto& a,const auto& b){
        return planDot(a,direction)<planDot(b,direction);
    });
    return {planDot(*limits.first,direction),planDot(*limits.second,direction)};
}

bool planTextNearMeasuredSegment(const PlanLayoutChain& chain,const AcDbExtents& box,
    const AcGePoint3d& baseline,std::size_t segment) {
    if(segment+1>=chain.stations.size())return false;
    const auto span=planProjectedSpan(box,chain.along);
    const auto side=planProjectedSpan(box,chain.above);
    const double center=(span.first+span.second)*0.5;
    const double first=planDot(chain.stations[segment],chain.along);
    const double second=planDot(chain.stations[segment+1],chain.along);
    // Allow a short value to straddle its endpoint by up to a glyph half-width,
    // and allow two lines of same-side staggering near the dimension line.
    const double nearEnd=(span.second-span.first)*0.5;
    const double nearLine=2*(side.second-side.first)+0.35*chain.modelPerPaperMm;
    return center>=std::min(first,second)-nearEnd&&center<=std::max(first,second)+nearEnd&&
        side.first-planDot(baseline,chain.above)<=nearLine;
}

int planSegmentAssociationIssueCount(const PlanLayoutChain& chain,const PlanChainLayout& layout) {
    int issues=0;
    for(std::size_t i=0;i<layout.boxes.size();++i)
        if(layout.boxes[i]&&!planTextNearMeasuredSegment(chain,*layout.boxes[i],layout.baseline,i))++issues;
    return issues;
}

std::vector<PlanLayoutInk> readPlanLayoutInk(AcDbDatabase& db,const std::set<std::string>& movable) {
    std::vector<PlanLayoutInk> result;
    for(const auto& [handle,type]:modelTypes(db)){
        const auto key=ascii(handle);
        if(movable.count(key))continue;
        const auto entity=open<AcDbEntity>(find(db,key));
        if(!productPlanDimensionVisible(*entity))continue;
        const auto layer=symbolName(entity->layerId());
        if(layer==L"Defpoints"||layer==L"DEFPOINTS"||layer==L"DEFPOINT")continue;
        const bool text=type==L"TDbText"||type==L"TDbMText"||type==L"AcDbText"||type==L"AcDbMText"||
            type==L"TDbOpening"||type==L"TDbGroupOpening"||type==L"TDbSymbElevation"||
            type==L"TDbAxisLabelSet"||type==L"TDbDimension2"||AcDbDimension::cast(entity.get())||
            type==L"AcDbBlockReference";
        const bool line=type==L"TDbWall"||type==L"TDbColumn"||type==L"TDbRectStair"||
            type==L"TDbLineStair"||type==L"TDbMultiStair"||type==L"TDbSymbArrow"||type==L"TDbSymbRupture"||
            type==L"TDbOpening"||type==L"TDbGroupOpening"||type==L"TDbDimension2"||
            type==L"AcDbBlockReference"||type==L"AcDbLeader"||layer==L"DOTE"||
            layer==L"STAIR"||layer==L"A-STAIR"||layer==L"家具"||layer==L"平-家具"||
            layer==L"P平-固定家具（落地到顶）"||AcDbDimension::cast(entity.get());
        if(!text&&!line)continue;
        PlanLayoutInk item;
        item.handle=key;
        item.openingCaption=type==L"TDbOpening"||type==L"TDbGroupOpening";
        if(entity->getGeomExtents(item.bounds)!=Acad::eOk)continue;
        if(text)productGenericTextLeaves(*entity,item.boxes);
        if(type==L"TDbWall"||type==L"TDbColumn")productCollectPlanSolidInk(*entity,item.boxes);
        if(line)appendInk(*entity,item.lines);
        if(const auto* leader=AcDbLeader::cast(entity.get()))
            for(int i=1;i<leader->numVertices();++i)item.lines.emplace_back(leader->vertexAt(i-1),leader->vertexAt(i));
        for(const auto& box:item.boxes)item.bounds.addExt(box);
        for(const auto& [a,b]:item.lines){item.bounds.addPoint(a);item.bounds.addPoint(b);}
        if(!item.boxes.empty()||!item.lines.empty()){
            item.captureXY();
            result.push_back(std::move(item));
        }
    }
    return result;
}

GenericDimensionBoxes planReadLayoutBoxes(AcDbDatabase& db,const PlanLayoutChain& chain) {
    if(!chain.stock)return productGenericChainText(db,chain.id,chain.vertical);
    return readPlanStockDimensionText(db,chain.id);
}

PlanChainLayout readPlanChainPlacement(AcDbDatabase& db,const PlanLayoutChain& chain) {
    PlanChainLayout result;
    if(chain.stock){
        const auto entity=open<AcDbEntity>(chain.id);
        const auto* ordinary=AcDbDimension::cast(entity.get());
        result.baseline=AcDbRotatedDimension::cast(entity.get())?
            AcDbRotatedDimension::cast(entity.get())->dimLinePoint():AcDbAlignedDimension::cast(entity.get())->dimLinePoint();
        result.textGrips.push_back(ordinary->textPosition());
        result.defaultText=ordinary->isUsingDefaultTextPosition();
    }else{
        const auto grips=chainGrips(chain.id);
        result.baseline=grips[0];
        const auto n=chain.stations.size()-1;
        result.textGrips.assign(grips.begin()+n+3,grips.begin()+2*n+3);
    }
    result.boxes=planReadLayoutBoxes(db,chain);
    {
        const auto entity=open<AcDbEntity>(chain.id);
        appendInk(*entity,result.lines);
        // Use saved-audit geometry for L05 without changing the scoring for
        // ordinary dimension typography and shared endpoints.
        if(!chain.stock)productPlanDimensionStrokes(*entity,result.captionStrokes);
    }
    if(result.lines.empty()){
        const auto project=[&](const AcGePoint3d& p){
            return p+chain.above*((result.baseline-p).dotProduct(chain.above));
        };
        result.lines.emplace_back(project(chain.stations.front()),project(chain.stations.back()));
        for(const auto& station:chain.stations)result.lines.emplace_back(station,project(station));
    }
    boundPlanPlacement(result);
    return result;
}

void planMoveLayoutBaseline(const PlanLayoutChain& chain,const AcGePoint3d& position) {
    if(!chain.stock){
        const auto current=chainGrips(chain.id).front();
        const auto shift=chain.above*((position-current).dotProduct(chain.above));
        if(shift.length()>1e-5)moveDimensionGrip(chain.id,0,current+shift);
        return;
    }
    const auto entity=editObject<AcDbEntity>(chain.id);
    if(auto* rotated=AcDbRotatedDimension::cast(entity.get()))status(rotated->setDimLinePoint(position),"rotate dimension baseline offset");
    else status(AcDbAlignedDimension::cast(entity.get())->setDimLinePoint(position),"aligned dimension baseline offset");
    status(AcDbDimension::cast(entity.get())->recomputeDimBlock(true),"refresh ordinary baseline display");
}

void planMoveLayoutText(const PlanLayoutChain& chain,std::size_t index,const AcGePoint3d& position) {
    if(!chain.stock){
        const auto grip=chain.stations.size()+2+index;
        if(chainGrips(chain.id)[grip].distanceTo(position)>1e-5)moveDimensionGrip(chain.id,static_cast<int>(grip),position);
        return;
    }
    const auto entity=editObject<AcDbEntity>(chain.id);
    auto* ordinary=AcDbDimension::cast(entity.get());
    status(ordinary->setTextPosition(position),"ordinary dimension text offset");
    status(ordinary->useSetTextPosition(),"retain explicit ordinary dimension text position");
    status(ordinary->recomputeDimBlock(true),"refresh ordinary dimension display");
}

void applyPlanChainPlacement(const PlanLayoutChain& chain,const PlanChainLayout& placement) {
    planMoveLayoutBaseline(chain,placement.baseline);
    if(chain.stock&&placement.defaultText){
        const auto dimension=editObject<AcDbDimension>(chain.id);
        status(dimension->useDefaultTextPosition(),"restore ordinary automatic text position");
        status(dimension->recomputeDimBlock(true),"refresh ordinary automatic text display");
        return;
    }
    for(std::size_t i=0;i<placement.textGrips.size();++i)planMoveLayoutText(chain,i,placement.textGrips[i]);
}

PlanChainLayout realizePlanPlacement(AcDbDatabase& db,const PlanLayoutChain& chain,const PlanChainLayout& placement) {
    applyPlanChainPlacement(chain,placement);
    auto actual=readPlanChainPlacement(db,chain);
    for(int pass=0;pass<2&&actual.boxes.size()==placement.boxes.size();++pass){
        bool adjusted=false;
        for(std::size_t i=0;i<actual.boxes.size();++i){
            if(!actual.boxes[i]||!placement.boxes[i])continue;
            const auto& box=*actual.boxes[i];
            const auto& target=*placement.boxes[i];
            const auto correction=((target.minPoint()-box.minPoint())+(target.maxPoint()-box.maxPoint()))*0.5;
            if(correction.length()<=1e-3)continue;
            planMoveLayoutText(chain,i,actual.textGrips[i]+correction);
            adjusted=true;
        }
        if(!adjusted)break;
        actual=readPlanChainPlacement(db,chain);
    }
    return actual;
}

bool planLayoutVisibleInkHits(const AcDbExtents& box,const PlanLayoutInk& item,double gap) {
    return std::any_of(item.boxes.begin(),item.boxes.end(),[&](const auto& obstacle){
            return productGenericBoxesOverlap(box,obstacle,gap);
        })||std::any_of(item.lines.begin(),item.lines.end(),[&](const auto& line){
            return productGenericSegmentHitsBox(line.first,line.second,box,gap);
        });
}

bool planLayoutInkHits(const AcDbExtents& box,const PlanLayoutInk& item,double gap) {
    return productGenericBoxesOverlap(box,item.bounds,gap)&&planLayoutVisibleInkHits(box,item,gap);
}

int planFixedLayoutHits(const std::vector<const PlanLayoutInk*>& fixed,const AcDbExtents& box,double gap) {
    const auto a=box.minPoint(),b=box.maxPoint();
    return static_cast<int>(std::count_if(fixed.begin(),fixed.end(),[&](const auto* obstacle){
        const auto& bounds=obstacle->xy;
        return a.x<bounds.xmax+gap&&b.x>bounds.xmin-gap&&a.y<bounds.ymax+gap&&b.y>bounds.ymin-gap&&
               planLayoutVisibleInkHits(box,*obstacle,gap);
    }));
}

int planFixedLayoutHits(const PlanLayoutChain& chain,const AcDbExtents& box,double gap) {
    return planFixedLayoutHits(chain.fixed,box,gap);
}

int planSelfLayoutHits(const PlanChainLayout& placement,double gap) {
    int hits=0;
    for(std::size_t i=0;i<placement.boxes.size();++i)if(placement.boxes[i]){
        const auto& a=*placement.boxes[i];
        for(std::size_t j=0;j<i;++j)
            if(placement.boxes[j]&&productGenericBoxesOverlap(a,*placement.boxes[j],gap))++hits;
        if(std::any_of(placement.lines.begin(),placement.lines.end(),[&](const auto& line){
            return productGenericSegmentHitsBox(line.first,line.second,a,gap*0.25);
        }))++hits;
    }
    return hits;
}

int planPairLayoutHits(const PlanChainLayout& a,const PlanChainLayout& b,double gap) {
    if(!productGenericBoxesOverlap(a.coverage,b.coverage,gap))return 0;
    int hits=0;
    const auto lineHits=[&](const auto& boxes,const auto& lines){
        for(const auto& box:boxes)if(box&&std::any_of(lines.begin(),lines.end(),[&](const auto& line){
            return productGenericSegmentHitsBox(line.first,line.second,*box,gap);
        }))++hits;
    };
    for(const auto& first:a.boxes)if(first)for(const auto& second:b.boxes)
        if(second&&productGenericBoxesOverlap(*first,*second,gap))++hits;
    lineHits(a.boxes,b.lines);
    lineHits(b.boxes,a.lines);
    return hits;
}

bool planLayoutLess(int hits,double move,int otherHits,double otherMove) {
    return hits<otherHits||(hits==otherHits&&move<otherMove);
}

int planLayoutRoom(const std::optional<ProductRoomGrid>& grid,const AcGePoint3d& point) {
    return grid?grid->at(point):ProductRoomGrid::outside;
}

void preparePlanLayoutRoom(PlanLayoutChain& chain,const std::optional<ProductRoomGrid>& grid,
    const std::vector<ConfirmedPlanRoom>& confirmed,int panel,double paper) {
    const auto noteRoom=[&](const auto& point) {
        const auto room=planLayoutRoom(grid,point);
        if(room>=0)chain.rooms.insert(room);
    };
    const auto middle=chain.stations.front()+(chain.stations.back()-chain.stations.front())*0.5;
    const auto baseline=middle+chain.above*((chain.original.baseline-middle).dotProduct(chain.above));
    noteRoom(baseline);
    noteRoom(baseline+chain.above*paper);
    noteRoom(baseline-chain.above*paper);
    for(const auto& box:chain.original.boxes)if(box)noteRoom(box->minPoint()+(box->maxPoint()-box->minPoint())*0.5);
    if(chain.added)for(const auto& room:confirmed)
        if(room.panel==panel&&strictlyInRoom(room.vertices,{baseline.x,baseline.y})&&
           std::all_of(chain.stations.begin(),chain.stations.end(),[&](const auto& station){
            const auto projected=station+chain.above*((baseline-station).dotProduct(chain.above));
            return genericInside(room.vertices,{projected.x,projected.y});
           })){
            chain.confirmed=&room;
            break;
        }
}

int planCaptionStrokeHits(const PlanLayoutChain& chain,const PlanChainLayout& placement,double gap) {
    int hits=0;
    const auto& strokes=placement.captionStrokes.empty()?placement.lines:placement.captionStrokes;
    for(const auto* obstacle:chain.fixed){
        if(!obstacle->openingCaption||!productGenericBoxesOverlap(placement.coverage,obstacle->bounds,gap))continue;
        for(const auto& label:obstacle->boxes)
            if(std::any_of(strokes.begin(),strokes.end(),[&](const auto& line){
                return productGenericSegmentHitsBox(line.first,line.second,label,gap);
            }))++hits;
    }
    return hits;
}

int planLayoutLocalConflicts(const PlanLayoutChain& chain,const PlanChainLayout& placement,double gap,
    int captionWeight=planCaptionAvoidanceWeight) {
    int hits=planSelfLayoutHits(placement,gap)+planSegmentAssociationIssueCount(chain,placement)+
        captionWeight*planCaptionStrokeHits(chain,placement,gap);
    for(const auto& box:placement.boxes)if(box)hits+=planFixedLayoutHits(chain,*box,gap);
    return hits;
}

std::vector<PlanChainLayout> planLayoutOptions(AcDbDatabase& db,PlanLayoutChain& chain,const ProductPlanBounds& panel,
    const std::optional<ProductRoomGrid>& grid,double paper,const PlanChainLayout* around=nullptr) {
    constexpr std::array<double,25> baselines{0,1,-1,2,-2,3,-3,4.5,-4.5,6,-6,8,-8,10,-10,12,-12,15,-15,18,-18,21,-21,24,-24};
    // Short wall/offset segments can have less than 1 paper mm of free glyph
    // space. Coarse whole-mm samples skip that space even with a clear baseline.
    constexpr std::array<double,31> tangential{0,.25,-.25,.5,-.5,.75,-.75,1,-1,
        1.25,-1.25,1.5,-1.5,1.75,-1.75,2,-2,2.5,-2.5,3,-3,3.5,-3.5,5,-5,7.5,-7.5,10,-10,13,-13};
    constexpr std::array<double,16> normal{0,.25,.5,.75,1,1.25,1.5,1.75,2,2.5,3,3.5,5,7.5,10,13};
    constexpr std::array<double,29> localBaselines{0,0.5,-0.5,1,-1,1.5,-1.5,2,-2,3,-3,4,-4,
        5,-5,6,-6,7.5,-7.5,9,-9,11,-11,13,-13,16,-16,19,-19};
    const double gap=0.35*paper;
    const auto less=[](const PlanChainLayout& a,const PlanChainLayout& b){
        return planLayoutLess(a.fixedHits,a.displacement,b.fixedHits,b.displacement);
    };
    std::vector<PlanChainLayout> result;
    if(chain.original.boxes.size()!=chain.original.textGrips.size()||
        std::any_of(chain.original.boxes.begin(),chain.original.boxes.end(),[](const auto& box){return !box;}))return result;
    const auto& starting=around?*around:chain.original;
    const auto allowed=planDimensionPlacementBounds(chain,panel,paper);
    auto steps=around?std::vector<double>(localBaselines.begin(),localBaselines.end()):
        std::vector<double>(baselines.begin(),baselines.end());
    if(around){
        const auto initial=planDot(starting.baseline,chain.above);
        steps.push_back((planDot(chain.original.baseline,chain.above)-initial)/paper);
        for(const auto* opening:chain.fixed){
            if(!opening->openingCaption||!productGenericBoxesOverlap(starting.coverage,opening->bounds,gap))continue;
            for(const auto& box:opening->boxes){
                const auto hits=[&](const auto& stroke){return productGenericSegmentHitsBox(stroke.first,stroke.second,box,gap);};
                if(std::none_of(starting.captionStrokes.begin(),starting.captionStrokes.end(),hits))continue;
                const auto span=planProjectedSpan(box,chain.above);
                // Try the visible clearance on each edge of the caption, with
                // room for the witness tail. Actual strokes are read again on trial.
                for(const double tail:{.65,1.15}){
                    steps.push_back((span.first-initial-gap)/paper-tail);
                    steps.push_back((span.second-initial+gap)/paper+tail);
                }
            }
        }
        std::stable_sort(steps.begin(),steps.end(),[](double a,double b){return std::abs(a)<std::abs(b);});
        steps.erase(std::unique(steps.begin(),steps.end()),steps.end());
    }
    for(const double offset:steps){
        if(chain.lockBaseline&&offset!=0)break;
        const auto target=starting.baseline+chain.above*(offset*paper);
        const double totalOffset=(target-chain.original.baseline).dotProduct(chain.above)/paper;
        if(!planWithinRequestedSide(chain,target)||std::abs(totalOffset)>28)continue;
        if(around)applyPlanChainPlacement(chain,starting);
        planMoveLayoutBaseline(chain,target);
        if(!planWitnessesVisible(readPlanVisibleWitnesses(chain.id,chain.vertical),
                                chain.stations.size()-1,1./paper))continue;
        auto base=readPlanChainPlacement(db,chain);
        if(base.boxes.size()!=chain.original.boxes.size())continue;
        const double b=planDot(base.baseline,chain.above);
        std::vector<PlanChainLayout> beam{base};
        beam.front().displacement=std::abs(totalOffset)*1.5;
        beam.front().fixedHits=planCaptionAvoidanceWeight*planCaptionStrokeHits(chain,base,gap);
        for(std::size_t i=0;i<base.boxes.size();++i){
            if(!base.boxes[i]){beam.clear();break;}
            struct TextOption {AcDbExtents box;AcGePoint3d grip;int hits;double move;};
            std::vector<TextOption> choices;
            const auto& initial=*base.boxes[i];
            const auto centre=initial.minPoint()+(initial.maxPoint()-initial.minPoint())*0.5;
            const auto corners=planBoxCorners(initial);
            const auto low=*std::min_element(corners.begin(),corners.end(),[&](const auto& a,const auto& b){
                return planDot(a,chain.above)<planDot(b,chain.above);
            });
            const double first=planDot(chain.stations[i],chain.along), second=planDot(chain.stations[i+1],chain.along);
            const auto originalCentre=chain.original.boxes[i]->minPoint()+
                (chain.original.boxes[i]->maxPoint()-chain.original.boxes[i]->minPoint())*0.5;
            const auto placementMove=[&](double along,double rise){
                return chain.along*(along-planDot(centre,chain.along))+
                       chain.above*(b+(0.65+rise)*paper-planDot(low,chain.above));
            };
            auto envelope=initial;
            const auto [minRise,maxRise]=std::minmax_element(normal.begin(),normal.end());
            const auto [minRun,maxRun]=std::minmax_element(tangential.begin(),tangential.end());
            for(const auto x:{*minRun,*maxRun})for(const auto y:{*minRise,*maxRise}){
                const auto delta=placementMove((first+second)*0.5+x*paper,y);
                envelope.addPoint(initial.minPoint()+delta);
                envelope.addPoint(initial.maxPoint()+delta);
            }
            std::vector<const PlanLayoutInk*> nearby;
            const double boundTolerance=1e-6;
            const auto lo=envelope.minPoint(),hi=envelope.maxPoint();
            for(const auto* item:chain.fixed){
                const auto& xy=item->xy;
                if(lo.x<xy.xmax+gap+boundTolerance&&hi.x>xy.xmin-gap-boundTolerance&&
                   lo.y<xy.ymax+gap+boundTolerance&&hi.y>xy.ymin-gap-boundTolerance)nearby.push_back(item);
            }
            const auto sample=[&](const AcGeVector3d& move){
                const auto point=centre+move;
                const AcDbExtents box(initial.minPoint()+move,initial.maxPoint()+move);
                if(!inPanel(point,allowed)||!planWholeBoxAbove(box,chain.above,b)||
                    !planBoxInLayoutRoom(chain,box)||!planTextNearMeasuredSegment(chain,box,base.baseline,i))return;
                const auto ownRoom=planLayoutRoom(grid,point);
                const bool otherRoom=!chain.rooms.empty()&&ownRoom>=0&&!chain.rooms.count(ownRoom);
                int hits=planFixedLayoutHits(nearby,box,gap);
                if(std::any_of(base.lines.begin(),base.lines.end(),[&](const auto& line){
                    return productGenericSegmentHitsBox(line.first,line.second,box,gap*0.25);
                }))++hits;
                const double cost=point.distanceTo(originalCentre)/paper*0.22+(otherRoom?15:0)+
                    std::abs(planDot(point,chain.along)-(first+second)*0.5)/paper;
                choices.push_back({box,base.textGrips[i]+move,hits,cost});
            };
            sample(AcGeVector3d(0,0,0));
            for(const double y:normal)for(const double x:tangential){
                const double along=(first+second)*0.5+x*paper;
                if(along<std::min(first,second)-8*paper||along>std::max(first,second)+8*paper)continue;
                sample(placementMove(along,y));
            }
            std::stable_sort(choices.begin(),choices.end(),[](const auto& a,const auto& b){
                return planLayoutLess(a.hits,a.move,b.hits,b.move);
            });
            if(choices.size()>12){
                // Keep clear choices on nearby rows and toward each end of a
                // segment so similar positions do not crowd out staggering.
                std::vector<TextOption> spread(choices.begin(),choices.begin()+12);
                std::set<std::pair<int,int>> regions;
                const auto middle=(first+second)*.5;
                const auto region=[&](const auto& choice){
                    const auto across=planProjectedSpan(choice.box,chain.above);
                    const auto along=planProjectedSpan(choice.box,chain.along);
                    const auto x=((along.first+along.second)*.5-middle)/paper;
                    const auto y=(across.first-b)/paper;
                    return std::make_pair(static_cast<int>(std::floor(y)),x<-.5?-1:x>.5?1:0);
                };
                for(const auto& choice:spread)regions.insert(region(choice));
                for(auto candidate=choices.begin()+12;candidate!=choices.end();++candidate)
                    if(candidate->hits==choices.front().hits&&regions.insert(region(*candidate)).second)
                        spread.push_back(std::move(*candidate));
                choices=std::move(spread);
            }
            struct RankedText {const PlanChainLayout* partial;const TextOption* choice;int hits;double move;};
            std::vector<RankedText> ranked;
            ranked.reserve(beam.size()*choices.size());
            for(const auto& partial:beam)for(const auto& choice:choices){
                auto hits=partial.fixedHits+choice.hits;
                for(std::size_t j=0;j<i;++j)
                    if(productGenericBoxesOverlap(choice.box,*partial.boxes[j],gap))++hits;
                ranked.push_back({&partial,&choice,hits,partial.displacement+choice.move});
            }
            std::stable_sort(ranked.begin(),ranked.end(),[](const auto& a,const auto& b){
                return planLayoutLess(a.hits,a.move,b.hits,b.move);
            });
            if(ranked.size()>6)ranked.resize(6);
            std::vector<PlanChainLayout> next;
            next.reserve(ranked.size());
            for(const auto& rank:ranked){
                auto trial=*rank.partial;
                trial.boxes[i]=rank.choice->box;
                trial.textGrips[i]=rank.choice->grip;
                if(rank.choice->grip.distanceTo(base.textGrips[i])>1e-5)trial.defaultText=false;
                trial.fixedHits=rank.hits;
                trial.displacement=rank.move;
                next.push_back(std::move(trial));
            }
            beam=std::move(next);
        }
        for(auto& choice:beam){
            boundPlanPlacement(choice);
            result.push_back(std::move(choice));
        }
    }
    applyPlanChainPlacement(chain,starting);
    std::stable_sort(result.begin(),result.end(),less);
    if(result.size()>75){
        // Keep the best joint text layout for every viable baseline before
        // filling the rest of the shortlist with lower-displacement alternatives.
        std::vector<PlanChainLayout> spread;
        spread.reserve(75);
        std::vector<bool> selected(result.size());
        std::set<double> baselinesKept;
        for(std::size_t i=0;i<result.size();++i){
            if(!baselinesKept.insert(planDot(result[i].baseline,chain.above)).second)continue;
            selected[i]=true;
            spread.push_back(std::move(result[i]));
        }
        for(std::size_t i=0;i<result.size()&&spread.size()<75;++i)
            if(!selected[i])spread.push_back(std::move(result[i]));
        std::stable_sort(spread.begin(),spread.end(),less);
        result=std::move(spread);
    }
    return result;
}

std::vector<std::size_t> selectPlanLayout(const PlanLayoutGroup& group,const std::vector<PlanLayoutChain>& chains,double gap) {
    struct Choice {std::vector<std::size_t> options;int hits{};double move{};};
    std::vector<Choice> beam(1);
    for(std::size_t i=0;i<group.members.size();++i){
        const auto& chain=chains[group.members[i]];
        std::vector<Choice> next;
        for(const auto& partial:beam)for(std::size_t c=0;c<chain.options.size();++c){
            const auto& layout=chain.options[c];
            auto trial=partial;
            trial.options.push_back(c);
            trial.hits+=layout.fixedHits;
            trial.move+=layout.displacement;
            for(std::size_t j=0;j<i;++j)
                trial.hits+=planPairLayoutHits(layout,chains[group.members[j]].options[partial.options[j]],gap);
            next.push_back(std::move(trial));
        }
        std::stable_sort(next.begin(),next.end(),[](const auto& a,const auto& b){
            return planLayoutLess(a.hits,a.move,b.hits,b.move);
        });
        if(next.size()>40)next.resize(40);
        beam=std::move(next);
    }
    return beam.front().options;
}

std::vector<PlanLayoutChain> preparePlanLayout(AcDbDatabase& db,const std::vector<PlanNativeDimension>& dimensions,
    const std::vector<std::optional<ProductRoomGrid>>& grids,const std::vector<ConfirmedPlanRoom>& confirmed,
    const std::vector<PlanLayoutInk>& fixed,double paper) {
    std::vector<PlanLayoutChain> chains;
    chains.reserve(dimensions.size());
    for(const auto& dim:dimensions){
        PlanLayoutChain chain;
        chain.id=find(db,dim.handle);
        chain.stock=dim.stock;
        chain.vertical=dim.vertical;
        chain.added=dim.added;
        chain.lockBaseline=dim.lockBaseline;
        chain.modelPerPaperMm=paper;
        if(dim.stock){
            const auto& projection=*dim.projection;
            chain.along=projection.direction;
            chain.stations={projection.a,projection.b};
            chain.writable=!projection.associated&&(projection.normal-AcGeVector3d::kZAxis).length()<1e-8;
        }else{
            chain.along=(dim.grips[1]-dim.grips[0]).normal();
            chain.stations.assign(dim.grips.begin()+2,dim.grips.begin()+dim.values.size()+3);
        }
        if(chain.along.x < -1e-8||(std::abs(chain.along.x)<=1e-8&&chain.along.y<0))
            chain.along=-chain.along;
        chain.above=AcGeVector3d(-chain.along.y,chain.along.x,0);
        chain.original=readPlanChainPlacement(db,chain);
        chain.reach=AcDbExtents(chain.stations.front(),chain.stations.front());
        for(const auto& p:chain.stations)chain.reach.addPoint(p);
        for(const auto& box:chain.original.boxes)if(box)chain.reach.addExt(*box);
        preparePlanLayoutRoom(chain,grids[dim.panel],confirmed,dim.panel+1,paper);
        // Keep the full search envelope, including the post-placement glyph adjustments.
        for(const auto& ink:fixed)if(productGenericBoxesOverlap(chain.reach,ink.bounds,80*paper))
            chain.fixed.push_back(&ink);
        chain.original.fixedHits=planLayoutLocalConflicts(chain,chain.original,0.35*paper);
        chains.push_back(std::move(chain));
    }
    return chains;
}

std::vector<PlanLayoutGroup> planRoomGroups(const std::vector<PlanNativeDimension>& dimensions,
    const std::vector<PlanLayoutChain>& chains,double paper) {
    std::vector<PlanLayoutGroup> result;
    std::vector<bool> assigned(chains.size(),false);
    const auto linked=[&](std::size_t a,std::size_t b){
        if(dimensions[a].panel!=dimensions[b].panel)return false;
        const auto& first=chains[a];
        const auto& second=chains[b];
        return (first.confirmed&&first.confirmed==second.confirmed)||
            std::any_of(first.rooms.begin(),first.rooms.end(),[&](int room){return second.rooms.count(room)!=0;})||
            productGenericBoxesOverlap(first.reach,second.reach,160*paper);
    };
    for(std::size_t i=0;i<chains.size();++i){
        if(assigned[i])continue;
        PlanLayoutGroup group;
        group.panel=dimensions[i].panel;
        group.members.push_back(i);
        assigned[i]=true;
        for(std::size_t k=0;k<group.members.size();++k){
            const auto member=group.members[k];
            const auto& chain=chains[member];
            group.rooms.insert(chain.rooms.begin(),chain.rooms.end());
            if(chain.confirmed)group.confirmedRooms.emplace(member,chain.confirmed->source);
            for(std::size_t j=0;j<chains.size();++j)if(!assigned[j]&&linked(member,j)){
                assigned[j]=true;
                group.members.push_back(j);
            }
        }
        result.push_back(std::move(group));
    }
    return result;
}

int planLayoutConflictCount(const PlanLayoutGroup& group,const std::vector<PlanLayoutChain>& chains,
    const std::vector<PlanChainLayout>& states,double gap,int captionWeight=planCaptionAvoidanceWeight) {
    int hits=0;
    for(std::size_t i=0;i<states.size();++i){
        const auto& a=states[i];
        hits+=planLayoutLocalConflicts(chains[group.members[i]],a,gap,captionWeight);
        for(std::size_t j=0;j<i;++j)hits+=planPairLayoutHits(a,states[j],gap);
    }
    return hits;
}

int planLayoutSideIssueCount(const PlanLayoutChain& chain,const PlanChainLayout& layout) {
    const auto baseline=planDot(layout.baseline,chain.above);
    return static_cast<int>(std::count_if(layout.boxes.begin(),layout.boxes.end(),[&](const auto& box){
        return box&&(!planWholeBoxAbove(*box,chain.above,baseline)||!planBoxInLayoutRoom(chain,*box));
    }));
}

void verifyPlanLayoutChain(const PlanNativeDimension& dim,AcDbObjectId id);

int planMemberConflicts(const PlanLayoutGroup& group,const std::vector<PlanLayoutChain>& chains,
    const std::vector<PlanChainLayout>& states,std::size_t index,const PlanChainLayout& candidate,double gap) {
    int result=planLayoutLocalConflicts(chains[group.members[index]],candidate,gap);
    for(std::size_t j=0;j<states.size();++j)
        if(j!=index)result+=planPairLayoutHits(candidate,states[j],gap);
    return result;
}

void addPlanEdgeOptions(std::vector<std::pair<double,double>>& offsets,const PlanLayoutChain& chain,
    const AcDbExtents& box,const AcDbExtents& obstacle,double paper) {
    const auto x=planProjectedSpan(box,chain.along),y=planProjectedSpan(box,chain.above);
    const auto ox=planProjectedSpan(obstacle,chain.along),oy=planProjectedSpan(obstacle,chain.above);
    const double gap=0.42*paper;
    const auto x1=(ox.first-x.second-gap)/paper,x2=(ox.second-x.first+gap)/paper;
    const auto y1=(oy.first-y.second-gap)/paper,y2=(oy.second-y.first+gap)/paper;
    for(const double a:{0.,x1,x2})for(const double b:{0.,y1,y2})
        if((a||b)&&std::abs(a)<=14&&std::abs(b)<=15)offsets.emplace_back(a,b);
}

bool planLabelHasConflict(const PlanLayoutChain& chain,const std::vector<PlanChainLayout>& states,
    std::size_t index,std::size_t segment,double gap) {
    const auto& current=states[index];
    const auto& box=*current.boxes[segment];
    if(planFixedLayoutHits(chain,box,gap)||!planBoxInLayoutRoom(chain,box)||
        !planTextNearMeasuredSegment(chain,box,current.baseline,segment)||
        !planWholeBoxAbove(box,chain.above,planDot(current.baseline,chain.above)))return true;
    for(std::size_t j=0;j<states.size();++j){
        const auto& other=states[j];
        for(std::size_t k=0;k<other.boxes.size();++k)
            if((index!=j||segment!=k)&&other.boxes[k]&&productGenericBoxesOverlap(box,*other.boxes[k],gap))return true;
        if(std::any_of(other.lines.begin(),other.lines.end(),[&](const auto& line){
            return productGenericSegmentHitsBox(line.first,line.second,box,index==j?gap*0.25:gap);
        }))return true;
    }
    return false;
}

std::vector<std::pair<double,double>> planFreeGlyphSlots(const PlanLayoutChain& chain,
    const std::vector<PlanChainLayout>& states,std::size_t index,std::size_t segment,double paper) {
    const auto& current=states[index];
    const auto& box=*current.boxes[segment];
    const auto x=planProjectedSpan(box,chain.along),y=planProjectedSpan(box,chain.above);
    const double cx=(x.first+x.second)/2,cy=(y.first+y.second)/2;
    const double hx=(x.second-x.first)/2,hy=(y.second-y.first)/2;
    const auto from=planDot(chain.stations[segment],chain.along);
    const auto to=planDot(chain.stations[segment+1],chain.along);
    const double lowX=std::min(from,to)-hx,highX=std::max(from,to)+hx;
    const double baseline=planDot(current.baseline,chain.above);
    const double lowY=baseline+hy+0.04*paper;
    const double highY=baseline+5*hy+0.35*paper;
    struct Block {double left,right,bottom,top;};
    std::vector<Block> blocks;
    std::vector<double> rows{std::max(cy,lowY),lowY,highY};
    const auto include=[&](double a,double b,double c,double d,double extra){
        const Block item{std::min(a,b)-hx-extra,std::max(a,b)+hx+extra,
            std::min(c,d)-hy-extra,std::max(c,d)+hy+extra};
        if(item.left>=highX||item.right<=lowX||item.bottom>=highY||item.top<=lowY)return;
        blocks.push_back(item);
        if(item.bottom>lowY)rows.push_back(item.bottom-0.08*paper);
        if(item.top<highY)rows.push_back(item.top+0.08*paper);
    };
    const auto text=[&](const AcDbExtents& obstacle){
        const auto horizontal=planProjectedSpan(obstacle,chain.along);
        const auto vertical=planProjectedSpan(obstacle,chain.above);
        include(horizontal.first,horizontal.second,vertical.first,vertical.second,0.37*paper);
    };
    const auto lines=[&](const auto& strokes,double extra){
        for(const auto& [a,b]:strokes)
            include(planDot(a,chain.along),planDot(b,chain.along),planDot(a,chain.above),planDot(b,chain.above),extra);
    };
    for(const auto* ink:chain.fixed){
        for(const auto& label:ink->boxes)text(label);
        lines(ink->lines,0.37*paper);
    }
    for(std::size_t j=0;j<states.size();++j){
        const auto& other=states[j];
        for(std::size_t k=0;k<other.boxes.size();++k)
            if((j!=index||k!=segment)&&other.boxes[k])text(*other.boxes[k]);
        lines(other.lines,(j==index?0.10:0.37)*paper);
    }
    std::sort(rows.begin(),rows.end());
    rows.erase(std::unique(rows.begin(),rows.end(),[&](double a,double b){return std::abs(a-b)<0.01*paper;}),rows.end());
    std::vector<std::pair<double,double>> slots;
    for(const double row:rows){
        if(row<lowY||row>highY)continue;
        std::vector<std::pair<double,double>> occupied;
        for(const auto& b:blocks)if(row>=b.bottom&&row<=b.top)
            occupied.emplace_back(std::max(b.left,lowX),std::min(b.right,highX));
        std::sort(occupied.begin(),occupied.end());
        double left=lowX;
        const auto collect=[&](double right){
            if(right-left<0.16*paper)return;
            const double station=std::clamp(cx,left+0.08*paper,right-0.08*paper);
            slots.emplace_back((station-cx)/paper,(row-cy)/paper);
            if(chain.lockBaseline&&right-left>0.7*paper)
                slots.emplace_back(((left+right)*0.5-cx)/paper,(row-cy)/paper);
        };
        for(const auto& [start,end]:occupied){
            collect(start);
            left=std::max(left,end);
        }
        collect(highX);
    }
    std::stable_sort(slots.begin(),slots.end(),[](const auto& a,const auto& b){
        return a.first*a.first+a.second*a.second<b.first*b.first+b.second*b.second;
    });
    const std::size_t limit=chain.lockBaseline?80:35;
    if(slots.size()>limit)slots.resize(limit);
    return slots;
}

void tryOtherPlanLayouts(AcDbDatabase& db,const PlanLayoutGroup& group,const std::vector<PlanLayoutChain>& chains,
    const std::vector<PlanNativeDimension>& dimensions,double gap,std::vector<PlanChainLayout>& states) {
    for(int pass=0;pass<2;++pass){
        bool improved=false;
        for(std::size_t i=0;i<states.size();++i){
            const auto index=group.members[i];
            const auto& chain=chains[index];
            auto& current=states[i];
            int bestHits=planMemberConflicts(group,chains,states,i,current,gap);
            int bestSide=planLayoutSideIssueCount(chain,current);
            if(!chain.writable||(!bestHits&&!bestSide))continue;
            const auto original=current;
            struct Alternative {std::size_t index;int hits,side;double move;};
            std::vector<Alternative> choices;
            for(std::size_t j=0;j+1<chain.options.size();++j){
                const auto& option=chain.options[j];
                choices.push_back({j,planMemberConflicts(group,chains,states,i,option,gap),
                    planLayoutSideIssueCount(chain,option),option.displacement});
            }
            std::stable_sort(choices.begin(),choices.end(),[](const auto& a,const auto& b){
                return std::tie(a.side,a.hits,a.move)<std::tie(b.side,b.hits,b.move);
            });
            for(const auto& choice:choices){
                auto actual=realizePlanPlacement(db,chain,chain.options[choice.index]);
                const int hits=planMemberConflicts(group,chains,states,i,actual,gap);
                const int side=planLayoutSideIssueCount(chain,actual);
                if(planLayoutLess(side,hits,bestSide,bestHits)&&actual.boxes.size()==original.boxes.size()&&
                    std::all_of(actual.boxes.begin(),actual.boxes.end(),[](const auto& box){return bool(box);})){
                    verifyPlanLayoutChain(dimensions[index],chain.id);
                    current=std::move(actual);
                    bestHits=hits;
                    bestSide=side;
                    improved=true;
                    if(!bestHits&&!bestSide)break;
                }
            }
            current=realizePlanPlacement(db,chain,current);
            verifyPlanLayoutChain(dimensions[index],chain.id);
        }
        if(!improved)break;
    }
}

void replanBlockedRoomChains(AcDbDatabase& db,const PlanLayoutGroup& group,const std::vector<PlanLayoutChain>& chains,
    const std::vector<PlanNativeDimension>& dimensions,const ProductPlanBounds& panel,
    const std::optional<ProductRoomGrid>& grid,double paper,std::vector<PlanChainLayout>& states) {
    const double gap=0.35*paper;
    for(int pass=0;pass<2;++pass){
        bool improved=false;
        for(std::size_t i=0;i<states.size();++i){
            const auto index=group.members[i];
            const auto& chain=chains[index];
            const auto original=states[i];
            int best=planMemberConflicts(group,chains,states,i,original,gap);
            int bestSide=planLayoutSideIssueCount(chain,original);
            if(!chain.writable||(!best&&!bestSide))continue;
            PlanLayoutChain local;
            local.id=chain.id; local.along=chain.along; local.above=chain.above;
            local.stations=chain.stations; local.reach=chain.reach; local.rooms=chain.rooms;
            local.confirmed=chain.confirmed; local.fixed=chain.fixed;
            local.stock=chain.stock; local.vertical=chain.vertical; local.added=chain.added;
            local.lockBaseline=chain.lockBaseline;
            local.modelPerPaperMm=chain.modelPerPaperMm;
            local.original=chain.original;
            std::vector<PlanLayoutInk> neighbors;
            neighbors.reserve(states.size()-1);
            for(std::size_t j=0;j<states.size();++j)if(j!=i){
                PlanLayoutInk ink;
                ink.bounds=states[j].coverage;
                ink.captureXY();
                ink.lines=states[j].lines;
                for(const auto& box:states[j].boxes)if(box)ink.boxes.push_back(*box);
                neighbors.push_back(std::move(ink));
            }
            for(const auto& neighbor:neighbors)local.fixed.push_back(&neighbor);
            const auto options=planLayoutOptions(db,local,panel,grid,paper,&original);
            auto bestState=original;
            for(const auto& option:options){
                auto actual=realizePlanPlacement(db,chain,option);
                const auto hits=planMemberConflicts(group,chains,states,i,actual,gap);
                const auto side=planLayoutSideIssueCount(chain,actual);
                if(planLayoutLess(side,hits,bestSide,best)&&actual.boxes.size()==original.boxes.size()&&
                    std::all_of(actual.boxes.begin(),actual.boxes.end(),[](const auto& box){return bool(box);})){
                    verifyPlanLayoutChain(dimensions[index],chain.id);
                    bestState=std::move(actual);
                    best=hits;bestSide=side;
                    improved=true;
                    if(!best&&!bestSide)break;
                }
            }
            states[i]=realizePlanPlacement(db,chain,bestState);
            verifyPlanLayoutChain(dimensions[index],chain.id);
        }
        if(!improved)break;
    }
}

void refinePlanLayout(AcDbDatabase& db,const PlanLayoutGroup& group,const std::vector<PlanLayoutChain>& chains,
    const std::vector<PlanNativeDimension>& dimensions,const ProductPlanBounds& panel,double paper,
    std::vector<PlanChainLayout>& states) {
    constexpr std::array<double,17> along{0,0.6,-0.6,1.25,-1.25,2.5,-2.5,4,-4,6,-6,8,-8,10,-10,13,-13};
    constexpr std::array<double,15> normal{0,0.6,-0.6,1.25,-1.25,2.5,-2.5,4.5,-4.5,7,-7,10,-10,13,-13};
    std::vector<std::pair<double,double>> offsets;
    for(const auto x:along)for(const auto y:normal)if(x||y)offsets.emplace_back(x,y);
    std::stable_sort(offsets.begin(),offsets.end(),[](const auto& a,const auto& b){
        return a.first*a.first+a.second*a.second<b.first*b.first+b.second*b.second;
    });
    const double gap=0.35*paper;
    for(int pass=0;pass<2;++pass){
        bool improved=false;
        for(std::size_t i=0;i<states.size();++i){
            const auto index=group.members[i];
            const auto& chain=chains[index];
            auto& state=states[i];
            if(!chain.writable||(!planMemberConflicts(group,chains,states,i,state,gap)&&!planLayoutSideIssueCount(chain,state)))continue;
            const auto allowed=planDimensionPlacementBounds(chain,panel,paper);
            for(std::size_t k=0;k<state.boxes.size();++k){
                if(!state.boxes[k]||!planLabelHasConflict(chain,states,i,k,gap))continue;
                const auto original=state;
                const int originalHits=planMemberConflicts(group,chains,states,i,original,gap);
                const int originalSide=planLayoutSideIssueCount(chain,original);
                if(!originalHits&&!originalSide)break;
                const auto baseline=planDot(original.baseline,chain.above);
                const auto& oldBox=*original.boxes[k];
                auto positions=offsets;
                if(planWholeBoxAbove(oldBox,chain.above,baseline)){
                    const auto slots=planFreeGlyphSlots(chain,states,i,k,paper);
                    positions.insert(positions.end(),slots.begin(),slots.end());
                }
                if(chain.lockBaseline){
                    const auto normalSpan=planProjectedSpan(oldBox,chain.above);
                    const double nearBaseline=(baseline+0.12*paper-normalSpan.first)/paper;
                    // Search next to the original baseline before trying deeper rows.
                    for(double offset:{0.,0.3,0.65,1.,1.6,2.4,3.5,5.})
                        for(int x=-12;x<=12;++x)positions.emplace_back(x*0.6,nearBaseline+offset);
                }
                for(const auto* ink:chain.fixed){
                    if(!productGenericBoxesOverlap(oldBox,ink->bounds,gap))continue;
                    for(const auto& box:ink->boxes)if(productGenericBoxesOverlap(oldBox,box,gap))
                        addPlanEdgeOptions(positions,chain,oldBox,box,paper);
                    for(const auto& line:ink->lines)if(productGenericSegmentHitsBox(line.first,line.second,oldBox,gap)){
                        AcDbExtents edge(line.first,line.first);
                        edge.addPoint(line.second);
                        addPlanEdgeOptions(positions,chain,oldBox,edge,paper);
                    }
                }
                for(std::size_t j=0;j<states.size();++j){
                    for(std::size_t l=0;l<states[j].boxes.size();++l){
                        const auto& neighbor=states[j].boxes[l];
                        if((i!=j||k!=l)&&neighbor&&productGenericBoxesOverlap(oldBox,*neighbor,gap))
                            addPlanEdgeOptions(positions,chain,oldBox,*neighbor,paper);
                    }
                    for(const auto& line:states[j].lines){
                        if(!productGenericSegmentHitsBox(line.first,line.second,oldBox,gap))continue;
                        AcDbExtents edge(line.first,line.first);
                        edge.addPoint(line.second);
                        addPlanEdgeOptions(positions,chain,oldBox,edge,paper);
                    }
                }
                std::stable_sort(positions.begin(),positions.end(),[](const auto& a,const auto& b){
                    return a.first*a.first+a.second*a.second<b.first*b.first+b.second*b.second;
                });
                for(const auto& [x,y]:positions){
                    double lift=0;
                    if(!planWholeBoxAbove(oldBox,chain.above,baseline)){
                        const auto corners=planBoxCorners(oldBox);
                        const auto low=*std::min_element(corners.begin(),corners.end(),[&](const auto& a,const auto& b){
                            return planDot(a,chain.above)<planDot(b,chain.above);
                        });
                        lift=baseline-planDot(low,chain.above)+0.65*paper;
                    }
                    const auto shift=chain.along*(x*paper)+chain.above*(y*paper+lift);
                    const auto trialGrip=original.textGrips[k]+shift;
                    const AcDbExtents anticipated(oldBox.minPoint()+shift,oldBox.maxPoint()+shift);
                    const auto at=anticipated.minPoint()+(anticipated.maxPoint()-anticipated.minPoint())*0.5;
                    if(!inPanel(at,allowed)||!planWholeBoxAbove(anticipated,chain.above,baseline)||
                        !planBoxInLayoutRoom(chain,anticipated)||
                        !planTextNearMeasuredSegment(chain,anticipated,original.baseline,k))continue;
                    if(chain.lockBaseline){
                        auto predicted=original;
                        predicted.boxes[k]=anticipated;
                        boundPlanPlacement(predicted);
                        if(planMemberConflicts(group,chains,states,i,predicted,gap)>originalHits)continue;
                    }
                    planMoveLayoutText(chain,k,trialGrip);
                    auto trial=readPlanChainPlacement(db,chain);
                    if(k<trial.boxes.size()&&trial.boxes[k]){
                        const auto& box=*trial.boxes[k];
                        const auto correction=((anticipated.minPoint()-box.minPoint())+
                            (anticipated.maxPoint()-box.maxPoint()))*0.5;
                        if(correction.length()>1e-3){
                            planMoveLayoutText(chain,k,trial.textGrips[k]+correction);
                            trial=readPlanChainPlacement(db,chain);
                        }
                    }
                    const auto trialSide=planLayoutSideIssueCount(chain,trial);
                    const auto trialHits=planMemberConflicts(group,chains,states,i,trial,gap);
                    if(planWithinRequestedSide(chain,trial.baseline)&&k<trial.boxes.size()&&trial.boxes[k]&&
                        planTextNearMeasuredSegment(chain,*trial.boxes[k],trial.baseline,k)&&
                        planLayoutLess(trialSide,trialHits,originalSide,originalHits)){
                        verifyPlanLayoutChain(dimensions[index],chain.id);
                        state=std::move(trial);
                        improved=true;
                        break;
                    }
                    applyPlanChainPlacement(chain,original);
                }
            }
            const int priorHits=planMemberConflicts(group,chains,states,i,state,gap);
            if(!priorHits||chain.lockBaseline)continue;
            const auto original=state;
            const int priorSide=planLayoutSideIssueCount(chain,state);
            for(double offset:{0.5,-0.5,0.75,-0.75,1.25,-1.25,1.75,-1.75,2.5,-2.5,3.5,-3.5,5.,-5.}){
                auto trial=original;
                const auto shift=chain.above*(offset*paper);
                trial.baseline+=shift;
                if(!planWithinRequestedSide(chain,trial.baseline))continue;
                for(auto& point:trial.textGrips)point+=shift;
                for(auto& box:trial.boxes)if(box)box=AcDbExtents(box->minPoint()+shift,box->maxPoint()+shift);
                if(std::any_of(trial.boxes.begin(),trial.boxes.end(),[&](const auto& box){
                    return box&&!planBoxInLayoutRoom(chain,*box);
                }))continue;
                auto actual=realizePlanPlacement(db,chain,trial);
                if(planWitnessesVisible(readPlanVisibleWitnesses(chain.id,chain.vertical),chain.stations.size()-1,1./paper)&&
                    planLayoutSideIssueCount(chain,actual)<=priorSide&&
                    planMemberConflicts(group,chains,states,i,actual,gap)<priorHits){
                    verifyPlanLayoutChain(dimensions[index],chain.id);
                    state=std::move(actual);
                    improved=true;
                    break;
                }
                applyPlanChainPlacement(chain,original);
            }
        }
        if(!improved)break;
    }
}

void verifyPlanLayoutChain(const PlanNativeDimension& dim,AcDbObjectId id) {
    if(dim.projection){
        if(dim.stock)verifyStockPlanProjection(id,*dim.projection);
        else verifyNativePlanProjection(id,*dim.projection);
    }
    if(dim.stock)return;
    const auto grips=chainGrips(id);
    const auto values=chainValues(id);
    require(grips.size()==dim.grips.size()&&values.size()==dim.values.size()&&
        std::equal(values.begin(),values.end(),dim.values.begin(),[](double a,double b){return std::abs(a-b)<1e-5;})&&
        std::equal(grips.begin()+2,grips.begin()+values.size()+3,dim.grips.begin()+2,
            [](const auto& a,const auto& b){return a.distanceTo(b)<1e-5;}),
        "joint dimension layout changed physical stations or measurements");
    const auto along=(grips[1]-grips[0]).normal();
    require(along.crossProduct((dim.grips[1]-dim.grips[0]).normal()).length()<1e-8,
        "joint dimension layout changed the measuring direction");
}

// Search adjacent free slots in two stages so a clear glyph can be moved
// out of the way of a neighboring blocked one without changing either baseline.
void resolvePlanTextNeighbors(AcDbDatabase& db,const PlanLayoutGroup& group,
    const std::vector<PlanLayoutChain>& chains,const std::vector<PlanNativeDimension>& dimensions,
    const ProductPlanBounds& panel,double paper,std::vector<PlanChainLayout>& states) {
    const double gap=.35*paper;
    struct Target {std::size_t chain,segment;};
    const auto localChain=[&](Target target)->const PlanLayoutChain&{return chains[group.members[target.chain]];};
    const auto positions=[&](Target target){
        const auto& chain=localChain(target);
        const auto& state=states[target.chain];
        const auto& box=*state.boxes[target.segment];
        const auto allowed=planDimensionPlacementBounds(chain,panel,paper);
        const auto x=planProjectedSpan(box,chain.along),y=planProjectedSpan(box,chain.above);
        const double centerX=(x.first+x.second)/2;
        const double halfWidth=(x.second-x.first)/2;
        const double baseline=planDot(state.baseline,chain.above);
        const auto from=planDot(chain.stations[target.segment],chain.along);
        const auto to=planDot(chain.stations[target.segment+1],chain.along);
        const double begin=std::min(from,to)-halfWidth;
        const double end=std::max(from,to)+halfWidth;
        std::vector<std::pair<double,AcDbExtents>> candidates;
        const auto collect=[&](double along,double normal){
            const auto shift=chain.along*(along-centerX)+chain.above*normal;
            const AcDbExtents trial(box.minPoint()+shift,box.maxPoint()+shift);
            const auto center=trial.minPoint()+(trial.maxPoint()-trial.minPoint())*.5;
            if(!inPanel(center,allowed)||!planBoxInLayoutRoom(chain,trial)||
               !planWholeBoxAbove(trial,chain.above,baseline)||
               !planTextNearMeasuredSegment(chain,trial,state.baseline,target.segment)||
               planFixedLayoutHits(chain,trial,gap))return;
            for(std::size_t j=0;j<states.size();++j)
                if(std::any_of(states[j].lines.begin(),states[j].lines.end(),[&](const auto& line){
                    return productGenericSegmentHitsBox(line.first,line.second,trial,j==target.chain?gap*.25:gap);
                }))return;
            const double segmentCenter=(from+to)*.5;
            const double cost=shift.length()+.25*std::abs(along-segmentCenter)+
                std::max(0.,std::abs(along-segmentCenter)-halfWidth-std::abs(to-from)*.5);
            candidates.emplace_back(cost,trial);
        };
        const double maxRise=2*(y.second-y.first)+gap;
        for(double rise=.04*paper;rise<=maxRise;rise+=.12*paper){
            const double shift=baseline+rise-y.first;
            collect(end,shift);
            for(double along=begin;along<=end;along+=.20*paper)collect(along,shift);
        }
        for(const auto& [along,normal]:planFreeGlyphSlots(chain,states,target.chain,target.segment,paper))
            collect(centerX+along*paper,normal*paper);
        std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.first<b.first;});
        return candidates;
    };
    const auto blockingTexts=[&](Target target,const AcDbExtents& box){
        std::vector<Target> blockers;
        for(std::size_t j=0;j<states.size();++j)for(std::size_t k=0;k<states[j].boxes.size();++k)
            if((target.chain!=j||target.segment!=k)&&states[j].boxes[k]&&
                productGenericBoxesOverlap(box,*states[j].boxes[k],gap))blockers.push_back({j,k});
        return blockers;
    };
    const auto place=[&](Target target,const AcDbExtents& box){
        const auto& chain=localChain(target);
        auto expected=states[target.chain];
        const auto& old=*expected.boxes[target.segment];
        expected.textGrips[target.segment]+=((box.minPoint()-old.minPoint())+(box.maxPoint()-old.maxPoint()))*.5;
        expected.boxes[target.segment]=box;
        states[target.chain]=realizePlanPlacement(db,chain,expected);
        verifyPlanLayoutChain(dimensions[group.members[target.chain]],chain.id);
    };
    for(int pass=0;pass<2;++pass){
        bool improved=false;
        for(std::size_t j=0;j<states.size();++j)for(std::size_t k=0;k<states[j].boxes.size();++k){
            const Target target{j,k};
            const auto& chain=localChain(target);
            if(!chain.writable||!states[j].boxes[k]||
               !planLabelHasConflict(chain,states,j,k,gap))continue;
            const auto initial=planLayoutConflictCount(group,chains,states,gap);
            const auto candidates=positions(target);
            std::map<std::pair<std::size_t,std::size_t>,std::vector<std::pair<double,AcDbExtents>>> cachedSlots;
            for(const auto& [cost,box]:candidates){
                const auto blockers=blockingTexts(target,box);
                if(blockers.size()>2)continue;
                const auto original=states[j];
                if(blockers.empty()){
                    place(target,box);
                    if(!planLabelHasConflict(chain,states,j,k,gap)&&
                       planLayoutConflictCount(group,chains,states,gap)<initial){improved=true;break;}
                    applyPlanChainPlacement(chain,original);
                    states[j]=original;
                    continue;
                }
                if(blockers.size()==2){
                    const auto snapshot=states;
                    const auto initialSide=planLayoutSideIssueCount(chain,states[j]);
                    auto trial=states;
                    trial[j].boxes[k]=box;
                    std::vector<AcDbExtents> staged;
                    for(const auto& neighborTarget:blockers){
                        const auto& neighbor=localChain(neighborTarget);
                        if(!neighbor.writable)break;
                        const auto key=std::make_pair(neighborTarget.chain,neighborTarget.segment);
                        auto found=cachedSlots.find(key);
                        if(found==cachedSlots.end())found=cachedSlots.emplace(key,positions(neighborTarget)).first;
                        const auto free=std::find_if(found->second.begin(),found->second.end(),[&](const auto& slot){
                            for(std::size_t m=0;m<trial.size();++m)for(std::size_t n=0;n<trial[m].boxes.size();++n)
                                if((m!=neighborTarget.chain||n!=neighborTarget.segment)&&trial[m].boxes[n]&&
                                   productGenericBoxesOverlap(slot.second,*trial[m].boxes[n],gap))return false;
                            return true;
                        });
                        if(free==found->second.end())break;
                        trial[neighborTarget.chain].boxes[neighborTarget.segment]=free->second;
                        staged.push_back(free->second);
                    }
                    if(staged.size()!=blockers.size())continue;
                    for(std::size_t m=0;m<staged.size();++m)place(blockers[m],staged[m]);
                    place(target,box);
                    if(planLayoutSideIssueCount(chain,states[j])<=initialSide&&
                       !planLabelHasConflict(chain,states,j,k,gap)&&
                       planLayoutConflictCount(group,chains,states,gap)<initial){improved=true;break;}
                    std::set<std::size_t> restore{j,blockers[0].chain,blockers[1].chain};
                    for(const auto m:restore)applyPlanChainPlacement(chains[group.members[m]],snapshot[m]);
                    states=snapshot;
                    continue;
                }
                const auto blocker=blockers.front();
                const auto& neighbor=localChain(blocker);
                if(!neighbor.writable)continue;
                const auto oldNeighbor=states[blocker.chain];
                const auto slotKey=std::make_pair(blocker.chain,blocker.segment);
                auto found=cachedSlots.find(slotKey);
                if(found==cachedSlots.end())found=cachedSlots.emplace(slotKey,positions(blocker)).first;
                const auto& neighborSlots=found->second;
                bool accepted=false;
                int attempts=0;
                for(const auto& slot:neighborSlots){
                    if(productGenericBoxesOverlap(slot.second,box,gap))continue;
                    const auto others=blockingTexts(blocker,slot.second);
                    if(std::any_of(others.begin(),others.end(),[&](const auto& other){
                        return other.chain!=target.chain||other.segment!=target.segment;
                    }))continue;
                    place(blocker,slot.second);
                    place(target,box);
                    if(!planLabelHasConflict(chain,states,j,k,gap)&&
                       !planLabelHasConflict(neighbor,states,blocker.chain,blocker.segment,gap)&&
                       planLayoutConflictCount(group,chains,states,gap)<initial){accepted=true;break;}
                    applyPlanChainPlacement(chain,original);
                    states[j]=original;
                    if(blocker.chain!=j){
                        applyPlanChainPlacement(neighbor,oldNeighbor);
                        states[blocker.chain]=oldNeighbor;
                    }
                    if(++attempts==8)break;
                }
                if(accepted){improved=true;break;}
            }
        }
        if(!improved)break;
    }
}

void tryOtherAutomaticDimensionSide(AcDbDatabase& db,const PlanLayoutGroup& group,
    std::vector<PlanLayoutChain>& chains,const std::vector<PlanNativeDimension>& dimensions,
    const ProductPlanBounds& panel,const std::optional<ProductRoomGrid>& grid,double paper,
    std::vector<PlanChainLayout>& states) {
    const double gap=.35*paper;
    for(std::size_t i=0;i<states.size();++i){
        const auto index=group.members[i];
        auto& chain=chains[index];
        if(!dimensions[index].alternateStationSide||!chain.writable||
           !planMemberConflicts(group,chains,states,i,states[i],gap))continue;
        const auto previous=states[i];
        const auto initial=planLayoutConflictCount(group,chains,states,gap);
        const double station=planDot(chain.stations.front(),chain.above);
        const double offset=planDot(previous.baseline,chain.above)-station;
        if(std::abs(offset)<1e-5||std::any_of(chain.stations.begin(),chain.stations.end(),[&](const auto& point){
            return std::abs(planDot(point,chain.above)-station)>1e-5;
        }))continue;
        auto trial=previous;
        const auto shift=chain.above*(-2.*offset);
        trial.baseline+=shift;
        for(auto& point:trial.textGrips)point+=shift;
        for(auto& box:trial.boxes)if(box)box=AcDbExtents(box->minPoint()+shift,box->maxPoint()+shift);
        const auto source=chain.original;
        chain.original=realizePlanPlacement(db,chain,trial);
        const auto options=planLayoutOptions(db,chain,panel,grid,paper);
        bool accepted=false;
        for(const auto& option:options){
            if(planLayoutSideIssueCount(chain,option))continue;
            auto actual=realizePlanPlacement(db,chain,option);
            if(planLayoutSideIssueCount(chain,actual))continue;
            states[i]=actual;
            if(planLayoutConflictCount(group,chains,states,gap)>=initial){states[i]=previous;continue;}
            verifyPlanLayoutChain(dimensions[index],chain.id);
            accepted=true;
            break;
        }
        if(!accepted&&!options.empty()){
            const auto flipped=realizePlanPlacement(db,chain,options.front());
            auto alternate=states;
            alternate[i]=flipped;
            resolvePlanTextNeighbors(db,group,chains,dimensions,panel,paper,alternate);
            if(!planLayoutSideIssueCount(chain,alternate[i])&&
               planLayoutConflictCount(group,chains,alternate,gap)<initial){
                states=std::move(alternate);
                accepted=true;
            }else for(std::size_t j=0;j<states.size();++j)
                applyPlanChainPlacement(chains[group.members[j]],states[j]);
        }
        if(!accepted){
            applyPlanChainPlacement(chain,previous);
            chain.original=source;
            states[i]=previous;
        }
    }
}

void resolvePlanWitnessNeighbors(AcDbDatabase& db,const PlanLayoutGroup& group,const std::vector<PlanLayoutChain>& chains,
    const std::vector<PlanNativeDimension>& dimensions,const ProductPlanBounds& panel,double paper,
    std::vector<PlanChainLayout>& states) {
    const double gap=.35*paper;
    const auto valid=[&](const PlanLayoutChain& chain,const PlanChainLayout& placement){
        const auto middle=chain.stations.front()+(chain.stations.back()-chain.stations.front())*.5;
        const auto onBaseline=middle+chain.above*((placement.baseline-middle).dotProduct(chain.above));
        return placement.boxes.size()+1==chain.stations.size()&&
            std::all_of(placement.boxes.begin(),placement.boxes.end(),[](const auto& box){return bool(box);})&&
            inPanel(onBaseline,planDimensionPlacementBounds(chain,panel,paper))&&
            planWithinRequestedSide(chain,placement.baseline)&&
            !planLayoutSideIssueCount(chain,placement)&&!planSegmentAssociationIssueCount(chain,placement);
    };
    const auto shiftBaseline=[&](const PlanLayoutChain& chain,const PlanChainLayout& initial,double offset){
        auto shifted=initial;
        const auto delta=chain.above*(offset*paper);
        shifted.baseline+=delta;
        for(auto& grip:shifted.textGrips)grip+=delta;
        for(auto& box:shifted.boxes)if(box)box=AcDbExtents(box->minPoint()+delta,box->maxPoint()+delta);
        return shifted;
    };
    const auto witnessesVisible=[&](const PlanLayoutChain& chain){
        return planWitnessesVisible(readPlanVisibleWitnesses(chain.id,chain.vertical),chain.stations.size()-1,1./paper);
    };
    const auto moveNeighborText=[&](const PlanLayoutChain& chain,std::size_t member){
        for(std::size_t segment=0;segment<states[member].boxes.size();++segment){
            if(!states[member].boxes[segment]||!planLabelHasConflict(chain,states,member,segment,gap))continue;
            const auto base=states[member];
            const auto slots=planFreeGlyphSlots(chain,states,member,segment,paper);
            const auto& box=*base.boxes[segment];
            for(const auto& [along,normal]:slots){
                const auto move=(chain.along*along+chain.above*normal)*paper;
                auto anticipated=base;
                anticipated.boxes[segment]=AcDbExtents(box.minPoint()+move,box.maxPoint()+move);
                boundPlanPlacement(anticipated);
                states[member]=anticipated;
                if(valid(chain,anticipated)&&!planLabelHasConflict(chain,states,member,segment,gap)){
                    anticipated.textGrips[segment]+=move;
                    states[member]=realizePlanPlacement(db,chain,anticipated);
                    if(valid(chain,states[member])&&!planLabelHasConflict(chain,states,member,segment,gap))break;
                    applyPlanChainPlacement(chain,base);
                }
                states[member]=base;
            }
        }
    };
    for(std::size_t i=0;i<states.size();++i){
        const auto index=group.members[i];
        const auto& labelChain=chains[index];
        if(!labelChain.writable)continue;
        for(std::size_t segment=0;segment<states[i].boxes.size();++segment){
            if(!states[i].boxes[segment])continue;
            const auto original=states[i];
            bool resolved=false;
            for(std::size_t j=0;j<states.size()&&!resolved;++j){
                if(i==j)continue;
                const auto neighborIndex=group.members[j];
                const auto& neighbor=chains[neighborIndex];
                if(!neighbor.writable||neighbor.lockBaseline)continue;
                const auto originalNeighbor=states[j];
                const auto& glyph=*original.boxes[segment];
                if(std::none_of(originalNeighbor.lines.begin(),originalNeighbor.lines.end(),[&](const auto& line){
                    return productGenericSegmentHitsBox(line.first,line.second,glyph,gap);
                }))continue;
                const auto originalHits=planLayoutConflictCount(group,chains,states,gap);
                std::vector<double> neighborOffsets{0.,.5,-.5,1.,-1.,1.5,-1.5,2.,-2.,3.,-3.,4.,-4.,
                    4.5,-4.5,5.,-5.,5.5,-5.5,6.,-6.,8.,-8.,10.,-10.,12.,-12.,15.,-15.,18.,-18.,21.,-21.,24.,-24.};
                const auto visible=readPlanVisibleWitnesses(neighbor.id,neighbor.vertical);
                if(!visible.empty()){
                    const auto& witness=visible.front();
                    const auto target=witness.station+std::copysign(5.5*paper+witness.startOffset,
                        witness.baseline-witness.station);
                    const double shift=(target-witness.baseline)*(neighbor.vertical?-1.:1.)/paper;
                    if(std::abs(shift)>1e-5)neighborOffsets.insert(neighborOffsets.begin()+1,shift);
                }
                for(const double offset:neighborOffsets){
                    auto candidate=shiftBaseline(neighbor,originalNeighbor,offset);
                    if(!planWithinRequestedSide(neighbor,candidate.baseline)||
                       std::abs((candidate.baseline-neighbor.original.baseline).dotProduct(neighbor.above))>28*paper)continue;
                    states[j]=realizePlanPlacement(db,neighbor,candidate);
                    if(valid(neighbor,states[j])&&witnessesVisible(neighbor)){
                        if(std::none_of(states[j].lines.begin(),states[j].lines.end(),[&](const auto& line){
                            return productGenericSegmentHitsBox(line.first,line.second,glyph,gap);
                        }))moveNeighborText(neighbor,j);
                        const auto improved=[&]{
                            return valid(labelChain,states[i])&&!planLabelHasConflict(labelChain,states,i,segment,gap)&&
                                planLayoutConflictCount(group,chains,states,gap)<originalHits;
                        };
                        for(const double labelOffset:{0.,3.,-3.,5.,-5.,8.,-8.,12.,-12.,16.,-16.,20.,-20.,24.,-24.}){
                            if(labelChain.lockBaseline&&labelOffset)break;
                            auto labelCandidate=shiftBaseline(labelChain,original,labelOffset);
                            if(!planWithinRequestedSide(labelChain,labelCandidate.baseline)||
                               std::abs((labelCandidate.baseline-labelChain.original.baseline).dotProduct(labelChain.above))>28*paper)
                                continue;
                            states[i]=realizePlanPlacement(db,labelChain,labelCandidate);
                            const auto base=states[i];
                            if(base.boxes.size()!=original.boxes.size()||!base.boxes[segment]||!witnessesVisible(labelChain))
                                continue;
                            resolved=improved();
                            const auto slots=resolved?std::vector<std::pair<double,double>>{}:
                                planFreeGlyphSlots(labelChain,states,i,segment,paper);
                            const auto& box=*base.boxes[segment];
                            for(const auto& [along,normal]:slots){
                                const auto move=(labelChain.along*along+labelChain.above*normal)*paper;
                                auto anticipated=base;
                                anticipated.boxes[segment]=AcDbExtents(box.minPoint()+move,box.maxPoint()+move);
                                boundPlanPlacement(anticipated);
                                states[i]=anticipated;
                                if(improved()){
                                    anticipated.textGrips[segment]+=move;
                                    states[i]=realizePlanPlacement(db,labelChain,anticipated);
                                    if(improved()){resolved=true;break;}
                                    applyPlanChainPlacement(labelChain,base);
                                }
                                states[i]=base;
                            }
                            if(resolved)break;
                        }
                    }
                    if(resolved){
                        verifyPlanLayoutChain(dimensions[index],labelChain.id);
                        verifyPlanLayoutChain(dimensions[neighborIndex],neighbor.id);
                        break;
                    }
                    applyPlanChainPlacement(labelChain,original);
                    states[i]=original;
                    applyPlanChainPlacement(neighbor,originalNeighbor);
                    states[j]=originalNeighbor;
                }
            }
        }
    }
}

void resolvePlanCaptionWitnesses(AcDbDatabase& db,const PlanLayoutGroup& group,std::vector<PlanLayoutChain>& chains,
    const std::vector<PlanNativeDimension>& dimensions,const ProductPlanBounds& panel,
    const std::optional<ProductRoomGrid>& grid,double paper,const std::vector<PlanChainLayout>& initial,
    std::vector<PlanChainLayout>& states) {
    const double gap=.35*paper;
    for(std::size_t i=0;i<states.size();++i){
        const auto index=group.members[i];
        auto& chain=chains[index];
        const auto prior=states[i];
        const int priorCaptionHits=planCaptionStrokeHits(chain,prior,gap);
        if(!chain.writable||!priorCaptionHits)continue;
        auto selected=prior;
        auto selectedOrigin=chain.original;
        const auto priorConflicts=planMemberConflicts(group,chains,states,i,prior,gap);
        auto best=std::make_pair(priorCaptionHits,priorConflicts);
        PlanLayoutChain trialChain=chain;
        trialChain.options.clear();
        std::vector<PlanLayoutInk> neighbors;
        neighbors.reserve(states.size()-1);
        for(std::size_t j=0;j<states.size();++j)if(j!=i){
            PlanLayoutInk ink;
            ink.bounds=states[j].coverage;
            ink.captureXY();
            ink.lines=states[j].lines;
            for(const auto& box:states[j].boxes)if(box)ink.boxes.push_back(*box);
            neighbors.push_back(std::move(ink));
        }
        for(const auto& ink:neighbors)trialChain.fixed.push_back(&ink);
        const std::array<const PlanChainLayout*,2> origins{&chain.original,&initial[i]};
        for(const auto* origin:origins){
            trialChain.original=*origin;
            // Restore both the initial side and its nearby clear rows after an
            // automatic side change. Never move the fixed door caption.
            const auto& around=planWithinRequestedSide(trialChain,prior.baseline)?prior:*origin;
            const auto options=planLayoutOptions(db,trialChain,panel,grid,paper,&around);
            for(const auto& option:options){
                if(planCaptionStrokeHits(trialChain,option,gap)>=priorCaptionHits)continue;
                auto actual=realizePlanPlacement(db,trialChain,option);
                if(planLayoutSideIssueCount(trialChain,actual)||planSegmentAssociationIssueCount(trialChain,actual)||
                   !planWitnessesVisible(readPlanVisibleWitnesses(chain.id,chain.vertical),chain.stations.size()-1,1./paper))continue;
                const auto score=std::make_pair(planCaptionStrokeHits(trialChain,actual,gap),
                    planMemberConflicts(group,chains,states,i,actual,gap));
                if(score.second>priorConflicts||score>=best)continue;
                best=score;
                selected=std::move(actual);
                selectedOrigin=*origin;
                if(best==std::make_pair(0,0))break;
            }
            if(best==std::make_pair(0,0))break;
        }
        chain.original=std::move(selectedOrigin);
        states[i]=realizePlanPlacement(db,chain,selected);
        verifyPlanLayoutChain(dimensions[index],chain.id);
    }
}

struct PlanJointTiming {
    std::map<std::string,std::pair<unsigned,unsigned long long>> rows;
    void add(const char* step,unsigned long long begin) {
        auto& row=rows[step];
        ++row.first;
        row.second+=GetTickCount64()-begin;
    }
    template<class Operation> void run(const char* step,Operation&& operation) {
        const auto begin=GetTickCount64();
        operation();
        add(step,begin);
    }
};

std::vector<PlanLayoutGroup> layoutPlanRooms(ProductJob& job,AcDbDatabase& db,
    const std::vector<PlanNativeDimension>& dimensions,const std::vector<ProductPlanBounds>& panels,
    const std::vector<std::optional<ProductRoomGrid>>& grids,const std::vector<ConfirmedPlanRoom>& confirmed,
    double paper,PlanJointTiming& timing) {
    auto tick=GetTickCount64();
    std::set<std::string> movable;
    for(const auto& dim:dimensions)movable.insert(dim.handle);
    const auto fixed=readPlanLayoutInk(db,movable);
    timing.add("read-fixed-ink",tick);
    tick=GetTickCount64();
    auto chains=preparePlanLayout(db,dimensions,grids,confirmed,fixed,paper);
    auto groups=planRoomGroups(dimensions,chains,paper);
    timing.add("prepare-chains-and-groups",tick);
    const double gap=0.35*paper;
    for(auto& group:groups){
        std::vector<PlanChainLayout> originals,updated;
        int originalSide=0;
        int originalAssociations=0;
        for(const auto index:group.members){
            auto& chain=chains[index];
            originals.push_back(chain.original);
            group.originalBaselines.emplace(dimensions[index].handle,chain.original.baseline);
            originalSide+=planLayoutSideIssueCount(chain,chain.original);
            originalAssociations+=planSegmentAssociationIssueCount(chain,chain.original);
        }
        group.before=planLayoutConflictCount(group,chains,originals,gap,1);
        const auto originalScore=planLayoutConflictCount(group,chains,originals,gap);
        group.written=group.before;
        group.beforeSide=originalSide;
        if(!group.before&&!originalSide)continue;
        tick=GetTickCount64();
        for(const auto index:group.members){
            auto& chain=chains[index];
            const auto& dimension=dimensions[index];
            const auto panel=static_cast<std::size_t>(dimension.panel);
            if(chain.writable)try{
                chain.options=planLayoutOptions(db,chain,panels[panel],grids[panel],paper);
            }catch(const std::exception& error){
                applyPlanChainPlacement(chain,chain.original);
                verifyPlanLayoutChain(dimension,chain.id);
                job.detailPending.push_back({u8"室内尺寸联合排位",dimension.handle,error.what()});
            }
            auto fallback=chain.original;
            fallback.fixedHits+=25*planLayoutSideIssueCount(chain,fallback);
            chain.options.push_back(std::move(fallback));
        }
        timing.add("generate-baseline-and-text-options",tick);
        tick=GetTickCount64();
        const auto chosen=selectPlanLayout(group,chains,gap);
        timing.add("select-joint-options",tick);
        bool applied=true;
        try{
            tick=GetTickCount64();
            for(std::size_t i=0;i<chosen.size();++i){
                const auto index=group.members[i];
                auto& chain=chains[index];
                const auto& placement=chain.options[chosen[i]];
                if(placement.displacement>1e-6||chosen[i]+1<chain.options.size())
                    updated.push_back(realizePlanPlacement(db,chain,placement));
                else updated.push_back(readPlanChainPlacement(db,chain));
                verifyPlanLayoutChain(dimensions[index],chain.id);
            }
            timing.add("apply-and-read-initial-options",tick);
            int sideIssues=0;
            for(std::size_t i=0;i<updated.size();++i)
                sideIssues+=planLayoutSideIssueCount(chains[group.members[i]],updated[i]);
            if(sideIssues||planLayoutConflictCount(group,chains,updated,gap)){
                timing.run("try-other-options",[&]{tryOtherPlanLayouts(db,group,chains,dimensions,gap,updated);});
                timing.run("refine-glyph-positions",[&]{refinePlanLayout(db,group,chains,dimensions,panels[group.panel],paper,updated);});
                if(planLayoutConflictCount(group,chains,updated,gap))
                    timing.run("replan-blocked-chains-1",[&]{replanBlockedRoomChains(db,group,chains,dimensions,panels[group.panel],grids[group.panel],paper,updated);});
                if(planLayoutConflictCount(group,chains,updated,gap))
                    timing.run("resolve-text-neighbors-1",[&]{resolvePlanTextNeighbors(db,group,chains,dimensions,panels[group.panel],paper,updated);});
                // Text-neighbor moves and automatic side changes invalidate the
                // earlier baseline scores. Recompute against their actual ink.
                if(planLayoutConflictCount(group,chains,updated,gap))
                    timing.run("replan-blocked-chains-2",[&]{replanBlockedRoomChains(db,group,chains,dimensions,panels[group.panel],grids[group.panel],paper,updated);});
                if(planLayoutConflictCount(group,chains,updated,gap))
                    timing.run("try-other-automatic-sides",[&]{tryOtherAutomaticDimensionSide(db,group,chains,dimensions,panels[group.panel],grids[group.panel],paper,updated);});
                if(planLayoutConflictCount(group,chains,updated,gap))
                    timing.run("resolve-text-neighbors-2",[&]{resolvePlanTextNeighbors(db,group,chains,dimensions,panels[group.panel],paper,updated);});
                if(planLayoutConflictCount(group,chains,updated,gap))
                    timing.run("resolve-witness-neighbors",[&]{resolvePlanWitnessNeighbors(db,group,chains,dimensions,panels[group.panel],paper,updated);});
                if(planLayoutConflictCount(group,chains,updated,gap))
                    timing.run("resolve-caption-witnesses",[&]{resolvePlanCaptionWitnesses(db,group,chains,dimensions,panels[group.panel],grids[group.panel],paper,originals,updated);});
            }
        }catch(const std::exception& error){
            applied=false;
            job.detailPending.push_back({u8"室内尺寸联合排位",dimensions[group.members.front()].handle,error.what()});
        }
        int updatedSide=0;
        int updatedAssociations=0;
        for(std::size_t i=0;i<updated.size();++i){
            const auto& chain=chains[group.members[i]];
            updatedSide+=planLayoutSideIssueCount(chain,updated[i]);
            updatedAssociations+=planSegmentAssociationIssueCount(chain,updated[i]);
        }
        const auto updatedHits=planLayoutConflictCount(group,chains,updated,gap,1);
        const auto updatedScore=planLayoutConflictCount(group,chains,updated,gap);
        if(!applied||updatedSide>originalSide||updatedAssociations>originalAssociations||
           (updatedSide==originalSide&&updatedScore>originalScore)){
            for(std::size_t i=0;i<group.members.size();++i){
                const auto index=group.members[i];
                if(chains[index].writable)applyPlanChainPlacement(chains[index],originals[i]);
                verifyPlanLayoutChain(dimensions[index],chains[index].id);
            }
        }else group.written=updatedHits;
    }
    return groups;
}

void auditPlanLayout(ProductJob& job,AcDbDatabase& db,const std::vector<PlanNativeDimension>& dimensions,
    const std::vector<std::optional<ProductRoomGrid>>& grids,const std::vector<ConfirmedPlanRoom>& confirmed,
    double paper,std::vector<PlanLayoutGroup>& groups) {
    std::set<std::string> movable;
    for(const auto& dim:dimensions)movable.insert(dim.handle);
    const auto fixed=readPlanLayoutInk(db,movable);
    auto chains=preparePlanLayout(db,dimensions,grids,confirmed,fixed,paper);
    const double gap=0.35*paper;
    for(auto& group:groups){
        std::vector<PlanChainLayout> states;
        for(const auto index:group.members)states.push_back(chains[index].original);
        group.after=planLayoutConflictCount(group,chains,states,gap,1);
        group.overlaps=planLayoutConflictCount(group,chains,states,0.,1);
        for(std::size_t i=0;i<states.size();++i){
            const auto index=group.members[i];
            const auto& chain=chains[index];
            const auto& handle=dimensions[index].handle;
            const auto& state=states[i];
            group.sideIssues+=planLayoutSideIssueCount(chain,state);
            group.segmentAssociationIssues+=planSegmentAssociationIssueCount(chain,state);
            const auto before=group.originalBaselines.find(handle);
            if(before!=group.originalBaselines.end()&&group.before>0){
                const double shift=(state.baseline-before->second).dotProduct(chain.above);
                std::ostringstream placement;
                placement<<std::setprecision(17)<<"{\"panel\":"<<group.panel+1<<",\"object\":\""<<handle
                         <<"\",\"scope\":\"geometry-scored-room-group\",\"oldAndNewBaseline\":"
                         <<wholePoints({before->second,state.baseline})<<",\"normalShift\":"<<shift
                         <<",\"beforeConflicts\":"<<group.before<<",\"reopenedConflicts\":"<<group.after
                         <<",\"savedReopened\":true,\"result\":\""
                         <<(group.after==0?"verified-room-group-geometric-clearance":"pending-room-group-clearance")<<"\"}";
                productRecordRuleEvidence(job,"D16",placement.str());
            }
            const auto& grid=grids[group.panel];
            if(grid&&(std::abs(chain.along.x)<1e-8||std::abs(chain.along.y)<1e-8)){
                const auto values=dimensions[index].stock?dimensions[index].values:chainValues(chain.id);
                const double eps=paper*.0001;
                for(std::size_t segment=0;segment<values.size()&&segment+1<chain.stations.size();++segment){
                    const auto a=chain.stations[segment],b=chain.stations[segment+1];
                    const double span=a.distanceTo(b);
                    if(span<=2*eps)continue;
                    const auto direction=(b-a).normal();
                    const int region=grid->at(a+direction*eps);
                    if(region<0||grid->at(b-direction*eps)!=region||
                        grid->at(a-direction*eps)!=ProductRoomGrid::blocked||
                        grid->at(b+direction*eps)!=ProductRoomGrid::blocked)continue;
                    const bool vertical=std::abs(direction.x)<1e-8;
                    const auto& cuts=vertical?grid->ys:grid->xs;
                    const auto low=std::min(vertical?a.y:a.x,vertical?b.y:b.x);
                    const auto high=std::max(vertical?a.y:a.x,vertical?b.y:b.x);
                    bool interior=true;
                    for(std::size_t cut=1;cut<cuts.size()&&interior;++cut){
                        const double lo=std::max(low,cuts[cut-1]),hi=std::min(high,cuts[cut]);
                        if(hi-lo<=eps)continue;
                        interior=grid->at(vertical?a.x:(lo+hi)/2,vertical?(lo+hi)/2:a.y)==region;
                    }
                    if(!interior)continue;
                    std::ostringstream net;
                    net<<std::setprecision(17)<<"{\"panel\":"<<group.panel+1<<",\"object\":\""<<handle
                       <<"\",\"segment\":"<<segment+1<<",\"roomRegion\":"<<region<<",\"stations\":"<<wholePoints({a,b})
                       <<",\"roomNetDrawingUnits\":"<<span<<",\"reopenedMeasuredDrawingUnits\":"<<values[segment]
                       <<",\"savedReopened\":true,\"result\":\""
                       <<(std::abs(span-values[segment])<=eps?"verified-opposed-inner-faces":"pending-net-measurement")<<"\"}";
                    productRecordRuleEvidence(job,"D12",net.str());
                }
            }
            for(std::size_t k=0;k+1<chain.stations.size();++k){
                const auto prefix=u8"第"+std::to_string(k+1)+u8"段 ";
                std::ostringstream record;
                record<<std::setprecision(17)<<"{\"panel\":"<<group.panel+1<<",\"object\":\""<<handle
                      <<"\",\"segment\":"<<k+1<<",\"scope\":\"interior-segment\",\"savedReopened\":true";
                if(k>=state.boxes.size()||!state.boxes[k]){
                    job.drawingChecks.record(group.panel+1,"interior-clearance",handle+"/"+std::to_string(k+1),
                        {"D18","D25","QA09"},bcpproduct::PlanReleaseGate::mainPlan,bcpproduct::PlanCheck::pending,
                        "saved-reopened",prefix+u8"字框不可读");
                    ++group.unreadable;
                    job.detailPending.push_back({u8"室内尺寸字框未验证",handle,prefix+u8"字框不可读"});
                    record<<",\"result\":\"unreadable\"}";
                    productRecordRuleEvidence(job,"D18",record.str());
                    continue;
                }
                const auto& box=*state.boxes[k];
                const bool nearSegment=planTextNearMeasuredSegment(chain,box,state.baseline,k);
                const bool sameSide=planWholeBoxAbove(box,chain.above,planDot(state.baseline,chain.above))&&
                    planBoxInLayoutRoom(chain,box);
                std::set<std::string> against;
                for(const auto* ink:chain.fixed)if(planLayoutInkHits(box,*ink,gap))against.insert(ink->handle);
                for(std::size_t j=0;j<states.size();++j){
                    const auto& neighbor=states[j];
                    for(std::size_t l=0;l<neighbor.boxes.size();++l)
                        if((i!=j||l!=k)&&neighbor.boxes[l]&&productGenericBoxesOverlap(box,*neighbor.boxes[l],gap))
                            against.insert(dimensions[group.members[j]].handle+":"+std::to_string(l+1));
                    if(std::any_of(neighbor.lines.begin(),neighbor.lines.end(),[&](const auto& line){
                        return productGenericSegmentHitsBox(line.first,line.second,box,i==j?gap*0.25:gap);
                    }))against.insert(dimensions[group.members[j]].handle+u8"界线或符号");
                }
                const bool clear=against.empty()&&nearSegment&&sameSide;
                record<<",\"textBox\":"<<wholePoints({box.minPoint(),box.maxPoint()})
                      <<",\"nearMeasuredSegment\":"<<(nearSegment?"true":"false")
                      <<",\"sameSide\":"<<(sameSide?"true":"false")
                      <<",\"result\":\""<<(clear?"verified-interior-text":"pending")<<"\",\"obstacles\":[";
                std::string list;
                for(const auto& h:against){
                    if(!list.empty()){list+=", ";record<<',';}
                    list+=h;
                    record<<jsonUtf8(bcpproduct::decodeMessage(h));
                }
                record<<"]}";
                productRecordRuleEvidence(job,"D18",record.str());
                job.drawingChecks.record(group.panel+1,"interior-clearance",handle+"/"+std::to_string(k+1),
                    {"D18","D25","QA09"},bcpproduct::PlanReleaseGate::mainPlan,
                    clear?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::failed,"saved-reopened",
                    prefix+(clear?u8"字框净空、测段归属和同侧位置通过":
                        !nearSegment?u8"字框偏离所属测段":!sameSide?u8"字框未保持本侧及所属空间":u8"尺寸字框碰撞 "+list));
                if(!nearSegment)job.detailPending.push_back({u8"室内尺寸测段对应",handle,prefix+u8"重开字框偏离所属测段或基线，须就近同侧复排"});
                if(against.empty())continue;
                const auto at=box.minPoint()+(box.maxPoint()-box.minPoint())*0.5;
                job.detailPending.push_back({u8"室内尺寸避让",handle,prefix+u8"重开字框 "+std::to_string(at.x)+","+
                    std::to_string(at.y)+u8" 净空对象 "+list});
            }
        }
    }
}
