#include "../../../插件/src/PlanDimensionWitness.h"

std::vector<PlanVisibleWitness> readPlanVisibleWitnesses(AcDbObjectId id,bool vertical) {
    const auto entity=open<AcDbEntity>(id);
    const auto read=[vertical](const AcDbDimension& part,const AcGePoint3d& baseline,
                               const AcGePoint3d& first,const AcGePoint3d& second,
                               std::vector<PlanVisibleWitness>& result){
        const auto normal=[vertical](const auto& p){return vertical?p.x:p.y;};
        const auto offset=part.dimexo()*part.dimscale();
        result.push_back({normal(baseline),normal(first),offset,part.dimse1()});
        result.push_back({normal(baseline),normal(second),offset,part.dimse2()});
    };
    std::vector<PlanVisibleWitness> result;
    if(const auto* aligned=AcDbAlignedDimension::cast(entity.get())){
        read(*aligned,aligned->dimLinePoint(),aligned->xLine1Point(),aligned->xLine2Point(),result);
    }else if(const auto* rotated=AcDbRotatedDimension::cast(entity.get())){
        read(*rotated,rotated->dimLinePoint(),rotated->xLine1Point(),rotated->xLine2Point(),result);
    }else{
        TemplateParts parts(entity.get());
        if(entity->explode(parts.values)!=Acad::eOk)return {};
        for(int i=0;i<parts.values.length();++i){
            const auto* part=AcDbAlignedDimension::cast(static_cast<AcRxObject*>(parts.values[i]));
            if(!part)return {};
            const auto delta=part->xLine2Point()-part->xLine1Point();
            if(std::abs(vertical?delta.x:delta.y)>1e-5)return {};
            read(*part,part->dimLinePoint(),part->xLine1Point(),part->xLine2Point(),result);
        }
    }
    return result;
}

bool planWitnessLengthMatches(const std::vector<PlanVisibleWitness>& lines,std::size_t segments,
                              double expected,double unit) {
    return lines.size()==2*segments&&!lines.empty()&&std::all_of(lines.begin(),lines.end(),[&](const auto& line){
        return !line.suppressed&&std::abs(line.visibleLength()*unit-expected)<.01;
    });
}

bool setFullPlanWitnessLength(AcDbDatabase& db,PlanNativeDimension& dim,double expected,double unit,
                             const ProductPlanBounds& panel) {
    // D23 sets the initial visible length. D25 may move the baseline after an
    // actual collision; saved readback must prove the exception and clear glyphs.
    dim.lockBaseline=false;
    const auto id=find(db,dim.handle);
    const auto lines=readPlanVisibleWitnesses(id,dim.vertical);
    if(planWitnessLengthMatches(lines,dim.values.size(),expected,unit))return true;
    if(dim.stock||lines.size()!=dim.values.size()*2)return false;
    const auto& first=lines.front();
    const double centre=dim.vertical?(panel.xmin+panel.xmax)/2:(panel.ymin+panel.ymax)/2;
    const double side=std::abs(first.baseline-first.station)<.01/unit?centre-first.station:first.baseline-first.station;
    const double target=first.station+std::copysign(expected/unit+first.startOffset,side);
    if(std::any_of(lines.begin(),lines.end(),[&](const auto& line){
        return line.suppressed||std::abs((std::abs(target-line.station)-line.startOffset)*unit-expected)>=.01;
    }))return false;
    const auto start=chainGrips(id).front();
    auto next=start;
    if(dim.vertical)next.x+=target-first.baseline; else next.y+=target-first.baseline;
    moveDimensionGrip(id,0,next);
    const bool verified=planWitnessLengthMatches(readPlanVisibleWitnesses(id,dim.vertical),dim.values.size(),expected,unit);
    if(!verified)
        moveDimensionGrip(id,0,start);
    verifyPlanLayoutChain(dim,id);
    return verified;
}

bool planNumericProductOverride(const std::wstring& content,double measurementMM) {
    const auto separator=content.find_first_of(L"*×xX");
    if(separator==std::wstring::npos)return false;
    const auto number=[](const std::wstring& part)->std::optional<double>{
        const auto first=part.find_first_not_of(L" \t");
        const auto last=part.find_last_not_of(L" \t");
        if(first==std::wstring::npos)return {};
        const auto value=part.substr(first,last-first+1);
        if(value.find_first_not_of(L"0123456789.")!=std::wstring::npos)return {};
        wchar_t* end=nullptr;
        const double parsed=std::wcstod(value.c_str(),&end);
        if(end!=value.c_str()+value.size()||!std::isfinite(parsed)||parsed<=0.)return {};
        return parsed;
    };
    const auto width=number(content.substr(0,separator));
    const auto height=number(content.substr(separator+1));
    return width&&height&&std::abs(*width-measurementMM)<.01;
}

std::optional<std::array<AcGePoint3d,4>> readPlanRectangularColumn(AcDbObjectId id,double tolerance) {
    try { return productColumnCorners(id); }
    catch(const std::exception&) {}
    const auto entity=open<AcDbEntity>(id);
    if(entity->isAProxy()||std::wstring(entity->isA()->name())!=L"TDbColumn")return {};
    GenericOpeningInk ink;
    productGenericOpeningInk(*entity,ink);
    const auto grips=chainGrips(id);
    if(grips.size()!=9||ink.straight.empty()||!ink.arcs.empty())return {};
    std::array<AcGePoint3d,4> corners{grips[1],grips[2],grips[3],grips[4]};
    const auto u=corners[1]-corners[0],v=corners[3]-corners[0];
    if(u.length()<=tolerance||v.length()<=tolerance||std::abs(u.normal().dotProduct(v.normal()))>1e-8||
       (corners[2]-(corners[1]+v)).length()>tolerance)return {};
    for(unsigned edge=0;edge<4;++edge){
        const auto a=corners[edge],b=corners[(edge+1)%4];
        if((a+(b-a)*.5).distanceTo(grips[5+edge])>tolerance)return {};
    }
    std::array<bool,2> visibleDirections{};
    const auto boundaryContains=[&](const auto& line,unsigned edge){
        const AcGeLineSeg3d side(corners[edge],corners[(edge+1)%4]);
        return side.distanceTo(line.first)<=tolerance&&side.distanceTo(line.second)<=tolerance;
    };
    for(const auto& line:ink.straight){
        if(line.first.distanceTo(line.second)<=tolerance)continue;
        for(unsigned edge=0;edge<4;++edge)if(boundaryContains(line,edge))visibleDirections[edge%2]=true;
    }
    // Wall masking may hide some native sides. Public corner and midpoint grips
    // can be used only when a visible boundary edge corroborates each direction.
    return visibleDirections[0]&&visibleDirections[1]?std::optional{corners}:std::nullopt;
}

struct PlanRoomNetSpan {
    AcGePoint3d first,last;
    std::string firstWall,lastWall;
    bool vertical;
};

bool planRoomNetFaceVisible(AcDbDatabase& db,const std::string& handle,const AcGePoint3d& point,double tolerance) {
    const auto entity=open<AcDbEntity>(find(db,handle));
    if(!productPlanDimensionVisible(*entity)||std::wstring(entity->isA()->name())!=L"TDbWall")return false;
    GenericOpeningInk ink;
    productGenericOpeningInk(*entity,ink);
    return std::any_of(ink.straight.begin(),ink.straight.end(),[&](const auto& line){
        return line.first.distanceTo(line.second)>tolerance&&AcGeLineSeg3d(line.first,line.second).distanceTo(point)<=tolerance;
    });
}

std::optional<PlanRoomNetSpan> readPlanRoomNetSpan(const ProductRoomGrid& grid,const GenericNativeInput& input,
    const bcpproduct::GenericView& view,const AcGePoint3d& anchor,bool vertical,double tolerance) {
    const auto room=grid.at(anchor);
    if(room<0)return {};
    const auto& stations=vertical?grid.ys:grid.xs;
    const double across=vertical?anchor.x:anchor.y;
    const double along=vertical?anchor.y:anchor.x;
    const auto point=[&](double value){return vertical?AcGePoint3d(across,value,0):AcGePoint3d(value,across,0);};
    const auto inRoom=[&](std::size_t cell){return grid.at(point((stations[cell]+stations[cell+1])*.5))==room;};
    const auto cell=static_cast<std::size_t>(std::upper_bound(stations.begin(),stations.end(),along)-stations.begin()-1);
    std::size_t first=cell,last=cell+1;
    while(first>0&&inRoom(first-1))--first;
    while(last+1<stations.size()&&inRoom(last))++last;
    if(first==0||last+1==stations.size()||stations[last]-stations[first]<=tolerance)return {};
    if(grid.at(point((stations[first-1]+stations[first])*.5))!=ProductRoomGrid::blocked||
       grid.at(point((stations[last]+stations[last+1])*.5))!=ProductRoomGrid::blocked)return {};
    const auto face=[&](double coordinate,bool positive){
        std::vector<std::string> matches;
        for(const auto index:view.walls){
            const auto& wall=input.walls[index];
            if(wall.low||!wall.orthogonal)continue;
            const bool wallVertical=std::abs(wall.wall.start.x-wall.wall.end.x)<=tolerance;
            if(wallVertical==vertical)continue;
            const auto rect=approvedRect(wall.wall);
            const auto axis=vertical?1:0,other=1-axis;
            if(std::abs(rect[axis+(positive?2:0)]-coordinate)<=tolerance&&
               across>rect[other]+tolerance&&across<rect[other+2]-tolerance)
                matches.push_back(wall.wall.handle);
        }
        return matches.size()==1?matches.front():std::string();
    };
    const auto a=face(stations[first],true),b=face(stations[last],false);
    if(a.empty()||b.empty()||a==b)return {};
    const auto crossesOpening=[&](const auto& opening){
        const auto station=[&](const auto& p){return vertical?p.x:p.y;};
        const auto depth=[&](const auto& p){return vertical?p.y:p.x;};
        const auto [lo,hi]=std::minmax(station(opening.a),station(opening.b));
        if(across<lo-tolerance||across>hi+tolerance||std::abs(depth(opening.a)-depth(opening.b))>tolerance)return false;
        const auto host=productGenericHost(opening,input,view);
        if(!host)return false;
        const auto& handle=input.walls[host->wall].wall.handle;
        return handle==a||handle==b;
    };
    if(std::any_of(input.openings.begin(),input.openings.end(),crossesOpening))return {};
    return PlanRoomNetSpan{point(stations[first]),point(stations[last]),a,b,vertical};
}

std::function<void(AcDbDatabase&)> arrangeFullPlanRoomDimensions(
    ProductJob& job,AcDbDatabase& db,AcDbDatabase& reference,
    const std::map<std::wstring,std::wstring>& original,const GenericNativeInput& input,const std::vector<bcpproduct::GenericView>& views,
    const std::vector<bool>& active,const std::vector<ProductPlanBounds>& panels,
    std::vector<std::optional<ProductRoomGrid>> grids,
    const std::vector<std::pair<AcDbObjectId,bool>>& added,const std::vector<ProductStairRow>& stairs,
    double scale,double unit,const std::function<void()>& placeSourceText) {
    PlanJointTiming timing;
    auto tick=GetTickCount64();
    const auto stamp=[&](const char* step){timing.add(step,tick);tick=GetTickCount64();};
    const auto requiredPaper=job.ruleCatalog?job.ruleCatalog->stairExtensionPaperMM:std::optional<double>{5.5};
    const double expected=requiredPaper.value_or(5.5)*scale;
    std::vector<PlanNativeDimension> dimensions;
    struct DuplicateDimension { std::string hidden,retained; std::size_t panel; std::vector<AcGePoint3d> stations; };
    std::vector<DuplicateDimension> duplicates;
    struct ColumnDimension {
        std::string source,dimension;
        std::size_t panel;
        unsigned direction;
        std::array<AcGePoint3d,2> stations;
        bool generated;
    };
    std::vector<ColumnDimension> columnDimensions;
    struct RoomNetDimension { std::string name,dimension; std::size_t panel; PlanRoomNetSpan span; };
    std::vector<RoomNetDimension> netDimensions;
    std::vector<std::string> stockRoots;
    const auto addNative=[&](AcDbObjectId id,std::size_t panel,bool isNew,const std::string& source=std::string{}){
        const auto key=axisHandle(id);
        const auto grips=chainGrips(id);
        const auto values=chainValues(id);
        if(values.empty()||grips.size()!=2*values.size()+6||grips[0].distanceTo(grips[1])<1e-5){
            job.detailPending.push_back({u8"室内尺寸字框未验证",key,u8"原生尺寸公开测点、测值或方向无法读取"});
            return;
        }
        dimensions.push_back({key,static_cast<int>(panel),std::abs(grips[0].x-grips[1].x)<1e-5,
            false,isNew,grips,values,source.empty()&&!isNew?key:source,{}});
    };
    for(const auto& [handle,type]:original){
        const bool native=type==L"TDbDimension2";
        const bool stock=type==L"AcDbRotatedDimension"||type==L"AcDbAlignedDimension";
        if(!native&&!stock)continue;
        const auto key=ascii(handle);
        const auto perimeter=job.oldPerimeterDimensions.find(key);
        if(perimeter!=job.oldPerimeterDimensions.end()&&!perimeter->second.replacement.empty())continue;
        if(stock)stockRoots.push_back(key);
        const auto normalized=job.planDimensionRatioReplacements.find(key);
        if(normalized==job.planDimensionRatioReplacements.end()&&job.planDimensionExclusions.count(key))continue;
        const auto id=find(db,normalized==job.planDimensionRatioReplacements.end()?key:normalized->second);
        const auto entity=open<AcDbEntity>(id);
        if(!productPlanDimensionVisible(*entity))continue;
        const auto grips=native?chainGrips(id):std::vector<AcGePoint3d>{};
        const auto* ordinary=AcDbDimension::cast(entity.get());
        if(native&&grips.empty()){
            job.detailPending.push_back({u8"室内尺寸范围待核",key,u8"原链基线位置不可读"});
            continue;
        }
        const auto anchor=native?grips.front():ordinary->textPosition();
        const auto panel=bcpproduct::nearestGenericView(views,{anchor.x,anchor.y});
        if(!active[panel]||!inPanel(anchor,panels[panel]))continue;
        if(native){addNative(id,panel,false,key);continue;}
        const auto projection=readPlanProjection(id);
        if(!projection){
            job.detailPending.push_back({u8"普通投影尺寸待转",key,u8"原尺寸公开测点或投影方向不可读"});
            continue;
        }
        const auto values=productDimensionMeasurements(*entity,false);
        dimensions.push_back({key,static_cast<int>(panel),std::abs(projection->direction.x)<1e-5,
            true,false,{},values,key,projection});
    }
    applyConfirmedPlanDimensions(job,db,reference,job.unifiedNewDimensions,panels,active,unit,dimensions);
    for(const auto& [id,vertical]:added){
        const auto grips=chainGrips(id);
        if(grips.size()<3)continue;
        const auto panel=bcpproduct::nearestGenericView(views,{grips[2].x,grips[2].y});
        const auto count=chainValues(id).size();
        const auto duplicate=std::find_if(dimensions.begin(),dimensions.end(),[&](const auto& existing){
            if(existing.stock||existing.panel!=static_cast<int>(panel)||!count||existing.values.size()!=count||
               grips.size()!=2*count+6)return false;
            const auto close=[unit](const auto& a,const auto& b){return a.distanceTo(b)*unit<.01;};
            return std::equal(grips.begin()+2,grips.begin()+count+3,existing.grips.begin()+2,close)||
                   std::equal(grips.begin()+2,grips.begin()+count+3,
                       std::make_reverse_iterator(existing.grips.begin()+count+3),close);
        });
        if(duplicate!=dimensions.end()){
            const auto first=grips.begin()+2;
            duplicates.push_back({axisHandle(id),duplicate->handle,panel,
                std::vector<AcGePoint3d>(first,first+count+1)});
            status(editObject<AcDbEntity>(id)->setVisibility(AcDb::kInvisible),"hide exactly duplicated generated interior measurement");
        }else {
            const auto size=dimensions.size();
            addNative(id,panel,true);
            if(dimensions.size()>size)dimensions.back().alternateStationSide=true;
        }
    }
    for(const auto& stair:stairs)for(const auto& dimension:stair.dimensions)
        if(stair.panel>0&&stair.panel<=active.size()&&active[stair.panel-1])
            addNative(find(db,dimension.handle),stair.panel-1,true);

    stamp("collect-source-generated-and-stair-dimensions");
    AcDbObjectId columnPrototype;
    for(const auto& [handle,type]:original){
        if(type!=L"TDbColumn")continue;
        const auto key=ascii(handle);
        const auto id=find(db,key);
        const auto entity=open<AcDbEntity>(id);
        const auto layer=open<AcDbLayerTableRecord>(entity->layerId());
        AcDbExtents extents;
        if(entity->visibility()!=AcDb::kVisible||layer->isOff()||layer->isFrozen()||
           entity->getGeomExtents(extents)!=Acad::eOk)continue;
        const auto anchor=extents.minPoint()+(extents.maxPoint()-extents.minPoint())*.5;
        const auto panel=bcpproduct::nearestGenericView(views,{anchor.x,anchor.y});
        const auto& panelBounds=panels[panel];
        const double viewAllowance=12*scale/unit;
        if(!active[panel]||!inPanel(anchor,{panelBounds.xmin-viewAllowance,panelBounds.ymin-viewAllowance,
                                          panelBounds.xmax+viewAllowance,panelBounds.ymax+viewAllowance}))continue;
        const auto sourceSection=readPlanRectangularColumn(id,.01/unit);
        if(!sourceSection){
            job.detailPending.push_back({u8"柱截面尺寸 D28",key,u8"未取得四条相接的实际矩形柱边；需按异形柱真实轮廓逐肢确认测点"});
            productRecordRuleEvidence(job,"D28","{\"panel\":"+std::to_string(panel+1)+
                ",\"object\":\""+key+"\",\"scope\":\"nonrectangular-column-section\",\"result\":\"pending\"}");
            continue;
        }
        const auto& points=*sourceSection;
        const auto close=[unit](const auto& a,const auto& b){return a.distanceTo(b)*unit<.01;};
        const double outward=(points[1]-points[0]).crossProduct(points[2]-points[1]).z>0?-1.:1.;
        for(unsigned direction=0;direction<2;++direction){
            const auto sameStations=[&](const auto& dim,unsigned edge){
                const auto& a=points[edge];
                const auto& b=points[(edge+1)%4];
                return dim.panel==static_cast<int>(panel)&&!dim.stock&&dim.values.size()==1&&
                    ((close(dim.grips[2],a)&&close(dim.grips[3],b))||
                     (close(dim.grips[3],a)&&close(dim.grips[2],b)));
            };
            const auto existing=std::find_if(dimensions.begin(),dimensions.end(),[&](const auto& dim){
                return sameStations(dim,direction)||sameStations(dim,direction+2);
            });
            if(existing!=dimensions.end()){
                columnDimensions.push_back({key,existing->handle,panel,direction,{existing->grips[2],existing->grips[3]},false});
                continue;
            }
            if(columnPrototype.isNull()){
                productPlanLayer(db,L"PUB_DIM",25);
                columnPrototype=cloneOne(reference,find(reference,"19A0"),db);
            }
            const std::array<AcGePoint3d,2> stations{points[direction],points[direction+1]};
            const auto result=productTryPlanChain(job,db,columnPrototype,{stations[0],stations[1]},outward*expected/unit,
                panel+1,key,u8"柱截面定位尺寸","D28");
            if(!result)continue;
            const auto chain=*result;
            const auto before=dimensions.size();
            addNative(chain,panel,true);
            if(dimensions.size()>before)dimensions.back().alternateStationSide=true;
            columnDimensions.push_back({key,axisHandle(chain),panel,direction,stations,true});
        }
    }
    if(!columnPrototype.isNull())status(editObject<AcDbEntity>(columnPrototype)->erase(),"retire column section prototype");

    stamp("column-section-dimensions");
    AcDbObjectId netPrototype;
    for(std::size_t panel=0;panel<views.size();++panel){
        if(!active[panel]||!grids[panel])continue;
        const auto& grid=*grids[panel];
        std::map<int,std::vector<std::pair<const GenericNativeTitle*,AcGePoint3d>>> names;
        for(const auto& note:input.notes){
            if(!productRoomHasArea(productRoomRole(note.text))||!inPanel(note.anchor,panels[panel]))continue;
            const auto room=grid.at(note.anchor);
            if(room>=0)names[room].push_back({&note,note.anchor});
        }
        for(const auto& [room,roomNames]:names){
            if(roomNames.size()!=1){
                for(const auto& [name,anchor]:roomNames)productRecordRuleEvidence(job,"D12","{\"panel\":"+
                    std::to_string(panel+1)+",\"object\":\""+name->handle+"\",\"position\":"+wholePoints({anchor})+
                    ",\"scope\":\"room-net-domain-multiple-names\",\"result\":\"pending\"}");
                continue;
            }
            const auto& [name,anchor]=roomNames.front();
            for(const bool vertical:{false,true}){
                const auto span=readPlanRoomNetSpan(grid,input,views[panel],anchor,vertical,.01/unit);
                if(!span||!planRoomNetFaceVisible(db,span->firstWall,span->first,.01/unit)||
                          !planRoomNetFaceVisible(db,span->lastWall,span->last,.01/unit)){
                    productRecordRuleEvidence(job,"D12","{\"panel\":"+std::to_string(panel+1)+
                        ",\"object\":\""+name->handle+"\",\"position\":"+wholePoints({anchor})+
                        ",\"vertical\":"+(vertical?"true":"false")+",\"scope\":\"room-net-opposing-wall-faces\",\"result\":\"pending\"}");
                    continue;
                }
                const auto found=std::find_if(dimensions.begin(),dimensions.end(),[&](const auto& dim){
                    if(dim.panel!=static_cast<int>(panel)||dim.stock||dim.values.empty())return false;
                    for(std::size_t i=0;i<dim.values.size();++i){
                        const auto& a=dim.grips[i+2];const auto& b=dim.grips[i+3];
                        const auto same=[&](const auto& a,const auto& b){return a.distanceTo(b)*unit<.01;};
                        if((same(a,span->first)&&same(b,span->last))||(same(b,span->first)&&same(a,span->last)))return true;
                    }
                    return false;
                });
                if(found!=dimensions.end()){
                    netDimensions.push_back({name->handle,found->handle,panel,*span});
                    continue;
                }
                if(netPrototype.isNull())netPrototype=cloneOne(reference,find(reference,"19A0"),db);
                const auto offset=expected/unit;
                const auto result=productTryPlanChain(job,db,netPrototype,{span->first,span->last},offset,
                    panel+1,name->handle,u8"房间净尺寸","D12");
                if(!result)continue;
                const auto id=*result;
                const auto before=dimensions.size();
                addNative(id,panel,true);
                if(dimensions.size()>before)dimensions.back().alternateStationSide=true;
                netDimensions.push_back({name->handle,axisHandle(id),panel,*span});
            }
        }
    }
    if(!netPrototype.isNull())status(editObject<AcDbEntity>(netPrototype)->erase(),"retire room net dimension prototype");

    stamp("room-net-dimensions");
    const bool ordinaryDimensions=std::any_of(dimensions.begin(),dimensions.end(),[](const auto& dimension){return dimension.stock;});
    if(ordinaryDimensions){
        const auto prototype=cloneOne(reference,find(reference,"19A0"),db);
        for(auto& dim:dimensions)if(dim.stock){
            const auto id=find(db,dim.handle);
            std::string reason;
            const auto candidate=tryConvertPlanProjection(db,id,prototype,*dim.projection,reason);
            if(candidate.isNull()){
                job.detailPending.push_back({u8"普通投影尺寸待转",dim.handle,reason});
                styleRetainedPlanProjection(job,db,id,*dim.projection,scale,unit);
                continue;
            }
            status(editObject<AcDbEntity>(id)->setVisibility(AcDb::kInvisible),"retain source reference for verified ordinary dimension conversion");
            dim.handle=axisHandle(candidate);
            dim.stock=false;
            dim.grips=chainGrips(candidate);
            dim.vertical=std::abs(dim.grips[0].x-dim.grips[1].x)<1e-5;
        }
        status(editObject<AcDbEntity>(prototype)->erase(),"retire full plan projection prototype");
    }
    if(ordinaryDimensions||!columnDimensions.empty()||!netDimensions.empty())productApplyPlanDimensionStyle(job,db,scale,unit);
    stamp("projected-dimension-conversion-and-style");
    AcDbObjectId witnessPrototype;
    std::map<std::string,std::wstring> retiredMeasurementOverrides;
    for(auto& dim:dimensions){
        const bool witnessReady=setFullPlanWitnessLength(db,dim,expected,unit,panels[dim.panel]);
        if(dim.stock||dim.projection||dim.added)continue;
        const auto originalId=find(db,dim.handle);
        const auto originalWitnesses=readPlanVisibleWitnesses(originalId,dim.vertical);
        const bool coincident=!witnessReady&&originalWitnesses.size()==2&&std::all_of(
            originalWitnesses.begin(),originalWitnesses.end(),[unit](const auto& line){
                return !line.suppressed&&std::abs(line.baseline-line.station)*unit<=.01;
            });
        if(!coincident&&dim.values.size()!=1)continue;
        auto original=open<AcDbEntity>(originalId);
        if(productSourceNativeDimensionProtected(*original))continue;
        const auto& policy=job.dimensionStylePolicy;
        TemplateParts originalParts(original.get());
        if(original->explode(originalParts.values)!=Acad::eOk)continue;
        if(originalParts.values.length()!=static_cast<int>(dim.values.size()))continue;
        bool numbersVerified=true;
        std::wstring sourceOverride;
        for(int i=0;i<originalParts.values.length()&&numbersVerified;++i){
            const auto* piece=AcDbDimension::cast(static_cast<AcRxObject*>(originalParts.values[i]));
            AcString customText;
            if(!piece||piece->dimensionText(customText)!=Acad::eOk||!productDimensionEqual(piece->dimlfac(),unit)){
                numbersVerified=false;
                break;
            }
            if(customText.isEmpty()||customText==L"<>")continue;
            // The one projected distance corroborates the first operand. Keep the
            // full source expression on the original entity, apart from the live measurement.
            sourceOverride=customText.kACharPtr();
            numbersVerified=dim.values.size()==1&&planNumericProductOverride(sourceOverride,dim.values.front()*unit);
        }
        if(!numbersVerified||(!coincident&&sourceOverride.empty()))continue;
        if(witnessPrototype.isNull())witnessPrototype=cloneOne(reference,find(reference,"19A0"),db);
        const auto& source=dim.grips;
        const std::vector<AcGePoint3d> stations(source.begin()+2,source.begin()+dim.values.size()+3);
        const auto direction=(stations.back()-stations.front()).normal();
        const AcGeVector3d normal(-direction.y,direction.x,0.);
        if(std::any_of(stations.begin(),stations.end(),[&](const auto& point){
            return std::abs((point-stations.front()).dotProduct(normal))>1e-5;
        }))continue;
        const auto& panel=panels[dim.panel];
        const AcGePoint3d centre((panel.xmin+panel.xmax)/2,(panel.ymin+panel.ymax)/2,source[2].z);
        const auto donor=open<AcDbEntity>(witnessPrototype);
        TemplateParts parts(donor.get());
        require(donor->explode(parts.values)==Acad::eOk&&parts.values.length()==1,"interior witness prototype");
        const auto* part=AcDbAlignedDimension::cast(static_cast<AcRxObject*>(parts.values[0]));
        require(part,"interior witness prototype dimension");
        const auto sourceSide=(chainGrips(originalId).front()-stations.front()).dotProduct(normal);
        const auto side=std::abs(sourceSide)>.01/unit?sourceSide:(centre-stations.front()).dotProduct(normal);
        const auto offset=std::copysign(expected/unit+part->dimexo()*part->dimscale(),side);
        const auto replacement=productTryPlanChain(job,db,witnessPrototype,stations,offset,dim.panel+1,
            dim.handle,sourceOverride.empty()?u8"室内界线尺寸":u8"旧室内尺寸读数",sourceOverride.empty()?"D23":"Q03");
        if(!replacement)continue;
        const auto rebuilt=*replacement;
        bool verified=false;
        try{
            verifyPlanLayoutChain(dim,rebuilt);
            auto entity=open<AcDbEntity>(rebuilt);
            const auto actual=productDimensionTexts(*entity);
            const auto measured=productReadSourceDimension(db,axisHandle(rebuilt),.01/unit);
            verified=measured.readable&&measured.automaticText&&
                planWitnessLengthMatches(readPlanVisibleWitnesses(rebuilt,dim.vertical),dim.values.size(),expected,unit)&&
                actual.size()==dim.values.size()&&std::all_of(actual.begin(),actual.end(),[&](const auto& style){
                    return productNativeDimensionStyleMatches(style,reference,policy,scale,unit)&&
                           productDimensionEqual(style.measurementFactor,unit);
                });
        }catch(const std::exception&){
            verified=false;
        }
        if(!verified){
            status(editObject<AcDbEntity>(rebuilt)->erase(),"retire unverified interior dimension replacement");
            continue;
        }
        original.reset();
        // Keep the original native dimension and its handle as an invisible source reference.
        status(editObject<AcDbEntity>(originalId)->setVisibility(AcDb::kInvisible),"hide verified superseded interior chain");
        job.planDimensionMeasurements.erase(dim.handle);
        dim.handle=axisHandle(rebuilt);
        job.planDimensionMeasurements[dim.handle]=dim.values;
        if(!sourceOverride.empty())retiredMeasurementOverrides.emplace(dim.sourceHandle,std::move(sourceOverride));
        const auto normalized=job.planDimensionRatioReplacements.find(dim.sourceHandle);
        if(normalized!=job.planDimensionRatioReplacements.end())normalized->second=dim.handle;
    }
    if(!witnessPrototype.isNull())status(editObject<AcDbEntity>(witnessPrototype)->erase(),"retire witness prototype");
    stamp("dimension-witnesses-and-replacements");
    productSeedPlanDimensionText(job,db,scale/unit);
    stamp("seed-dimension-text");
    placeSourceText();
    stamp("place-source-text");
    auto groups=layoutPlanRooms(job,db,dimensions,panels,grids,job.confirmedRooms,scale/unit,timing);
    if(job.validation){
        std::ostringstream report;
        report<<"step\tcalls\tmilliseconds\n";
        for(const auto& [step,row]:timing.rows)report<<step<<'\t'<<row.first<<'\t'<<row.second<<'\n';
        writeNew(job.root/L"plan-joint-layout-timing.tsv",report.str());
    }
    return [&job,dimensions=std::move(dimensions),grids=std::move(grids),groups=std::move(groups),duplicates=std::move(duplicates),
            columnDimensions=std::move(columnDimensions),netDimensions=std::move(netDimensions),stockRoots=std::move(stockRoots),
            retiredMeasurementOverrides=std::move(retiredMeasurementOverrides),scale,unit,expected](AcDbDatabase& saved) mutable {
        auditPlanLayout(job,saved,dimensions,grids,job.confirmedRooms,scale/unit,groups);
        if(stockRoots.empty())productRecordRuleEvidence(job,"D26",
            "{\"scope\":\"source-model-space-ordinary-aligned-rotated-dimensions\",\"count\":0,\"result\":\"not-applicable-no-source-root-projection\"}");
        for(const auto& source:stockRoots)if(std::none_of(dimensions.begin(),dimensions.end(),[&](const auto& row){
            return row.sourceHandle==source&&row.projection.has_value();
        }))productRecordRuleEvidence(job,"D26","{\"object\":\""+source+
            "\",\"scope\":\"unprocessed-source-model-space-projection\",\"result\":\"pending\"}");
        if(duplicates.empty())productRecordRuleEvidence(job,"D15","{\"scope\":\"generated-interior-exact-station-chain\",\"count\":0,\"result\":\"not-applicable-no-exact-generated-duplicate\"}");
        for(const auto& room:netDimensions){
            const auto actual=std::find_if(dimensions.begin(),dimensions.end(),[&](const auto& dim){
                return dim.handle==room.dimension||dim.sourceHandle==room.dimension;
            });
            bool verified=false;
            if(actual!=dimensions.end()){
                const auto id=find(saved,actual->handle);
                const auto grips=chainGrips(id);
                const auto values=chainValues(id);
                const auto same=[&](const auto& a,const auto& b){return a.distanceTo(b)*unit<.01;};
                for(std::size_t k=0;k<values.size()&&grips.size()==2*values.size()+6;++k){
                    const auto& a=grips[k+2];const auto& b=grips[k+3];
                    const auto& span=room.span;
                    if(((same(a,span.first)&&same(b,span.last))||(same(a,span.last)&&same(b,span.first)))&&
                       std::abs(values[k]-span.first.distanceTo(span.last))*unit<.01)verified=true;
                }
                verified=verified&&productPlanDimensionVisible(*open<AcDbEntity>(id))&&
                    planRoomNetFaceVisible(saved,room.span.firstWall,room.span.first,.01/unit)&&
                    planRoomNetFaceVisible(saved,room.span.lastWall,room.span.last,.01/unit);
            }
            if(!verified)job.detailPending.push_back({u8"房间净尺寸 D12",room.name,u8"独立重开未证实墙面到墙面的实测净尺寸"});
            std::ostringstream evidence;
            evidence<<std::setprecision(17)<<"{\"panel\":"<<room.panel+1<<",\"object\":\""
                <<(actual==dimensions.end()?room.dimension:actual->handle)<<"\",\"sourceName\":\""<<room.name
                <<"\",\"wallFaces\":[\""<<room.span.firstWall<<"\",\""<<room.span.lastWall<<"\"]"
                <<",\"stations\":"<<wholePoints({room.span.first,room.span.last})
                <<",\"valueMM\":"<<room.span.first.distanceTo(room.span.last)*unit
                <<",\"savedReopened\":true,\"scope\":\"room-net-opposing-wall-faces\",\"result\":\""
                <<(verified?"verified-room-net-width":"pending")<<"\"}";
            productRecordRuleEvidence(job,"D12",evidence.str());
        }
        for(const auto& column:columnDimensions){
            const auto dim=std::find_if(dimensions.begin(),dimensions.end(),[&](const auto& row){
                return row.sourceHandle==column.dimension||row.handle==column.dimension;
            });
            bool passed=false;
            if(dim!=dimensions.end()){
                const auto id=find(saved,dim->handle);
                const auto entity=open<AcDbEntity>(id);
                const auto layer=open<AcDbLayerTableRecord>(entity->layerId());
                const auto style=productDimensionTexts(*entity);
                const auto grips=chainGrips(id);
                const auto points=readPlanRectangularColumn(find(saved,column.source),.01/unit);
                const auto same=[&](const auto& a,const auto& b){return a.distanceTo(b)*unit<.01;};
                const auto sameEdge=[&](auto edge){
                    if(!points)return false;
                    const auto& a=(*points)[edge];
                    const auto& b=(*points)[(edge+1)%4];
                    return (same(a,column.stations[0])&&same(b,column.stations[1]))||
                           (same(a,column.stations[1])&&same(b,column.stations[0]));
                };
                const auto& policy=job.dimensionStylePolicy;
                const auto color=entity->colorIndex()==256?layer->color().colorIndex():entity->colorIndex();
                passed=productPlanDimensionVisible(*entity)&&symbolName(entity->layerId())==L"PUB_DIM"&&color==25&&
                    grips.size()==8&&same(grips[2],column.stations[0])&&same(grips[3],column.stations[1])&&
                    (sameEdge(column.direction)||sameEdge(column.direction+2))&&style.size()==1&&
                    productDimensionMatches(style.front(),policy.name,scale,policy.nominalPaperHeightMM*scale/unit);
            }
            if(!passed)job.detailPending.push_back({u8"柱截面尺寸 D28",column.source,u8"柱边、截面尺寸测点或实际尺寸样式在重开后未能核实"});
            std::ostringstream record;
            record<<std::setprecision(17)<<"{\"panel\":"<<column.panel+1<<",\"source\":\""<<column.source
                <<"\",\"object\":\""<<(dim==dimensions.end()?column.dimension:dim->handle)<<"\",\"direction\":"<<column.direction+1
                <<",\"stations\":"<<wholePoints({column.stations[0],column.stations[1]})
                <<",\"valueMM\":"<<column.stations[0].distanceTo(column.stations[1])*unit
                <<",\"generated\":"<<(column.generated?"true":"false")<<",\"scope\":\"rectangular-section-not-location\",\"savedReopened\":true"
                <<",\"result\":\""<<(passed?"verified-rectangular-section":"pending")<<"\"}";
            productRecordRuleEvidence(job,"D28",record.str());
            if(column.direction==0){
                job.detailPending.push_back({u8"柱局部定位 D28",column.source,u8"已按柱边处理截面尺寸；局部定位需逐方向核其实际轴线、相邻墙面及重复标注"});
                productRecordRuleEvidence(job,"D28","{\"panel\":"+std::to_string(column.panel+1)+
                    ",\"object\":\""+column.source+"\",\"scope\":\"column-positioning-need\",\"result\":\"pending\"}");
            }
        }
        for(const auto& duplicate:duplicates){
            const auto hidden=find(saved,duplicate.hidden);
            const auto replacement=std::find_if(dimensions.begin(),dimensions.end(),[&](const auto& dimension){
                return dimension.sourceHandle==duplicate.retained||dimension.handle==duplicate.retained;
            });
            bool retained=false;
            if(replacement!=dimensions.end()){
                const auto id=find(saved,replacement->handle);
                const auto grips=chainGrips(id);
                const auto count=duplicate.stations.size();
                const auto close=[unit](const auto& a,const auto& b){return a.distanceTo(b)*unit<.01;};
                retained=productPlanDimensionVisible(*open<AcDbEntity>(id))&&grips.size()==2*(count-1)+6&&
                    (std::equal(duplicate.stations.begin(),duplicate.stations.end(),grips.begin()+2,close)||
                     std::equal(duplicate.stations.rbegin(),duplicate.stations.rend(),grips.begin()+2,close));
            }
            const bool omitted=!productPlanDimensionVisible(*open<AcDbEntity>(hidden))&&retained;
            if(!omitted)job.detailPending.push_back({u8"室内重复尺寸",duplicate.hidden,u8"未能重开验证等价链和原测站"});
            productRecordRuleEvidence(job,"D15","{\"panel\":"+std::to_string(duplicate.panel+1)+
                ",\"object\":\""+duplicate.hidden+"\",\"retained\":\""+(retained?replacement->handle:duplicate.retained)+
                "\",\"stations\":"+wholePoints(duplicate.stations)+",\"scope\":\"same-interior-measurement\",\"savedReopened\":true,\"result\":\""+
                (omitted?"verified-exact-duplicate-omission":"pending")+"\"}");
        }
        std::ostringstream out;
        out<<std::setprecision(17)<<"{\"reopened\":true,\"bcpLoaded\":"<<(bcpLoaded()?"true":"false")
           <<",\"requiredVisibleMM\":"<<expected<<",\"chains\":[";
        for(std::size_t i=0;i<dimensions.size();++i){
            if(i)out<<',';
            const auto& dim=dimensions[i];
            const auto id=find(saved,dim.handle);
            verifyPlanLayoutChain(dim,id);
            const auto grips=dim.stock?std::vector<AcGePoint3d>{}:chainGrips(id);
            const auto values=productDimensionMeasurements(*open<AcDbEntity>(id),!dim.stock);
            require(values.size()==dim.values.size()&&std::equal(values.begin(),values.end(),dim.values.begin(),
                [](double a,double b){return std::abs(a-b)<1e-5;}),"full plan saved measurement changed");
            if(!dim.stock)job.planVerifiedInteriorMeasurementPanels.emplace(dim.handle,dim.panel+1);
            const auto stations=dim.projection?std::vector<AcGePoint3d>{dim.projection->a,dim.projection->b}:
                std::vector<AcGePoint3d>(grips.begin()+2,grips.begin()+values.size()+3);
            const auto witness=readPlanVisibleWitnesses(id,dim.vertical);
            const bool lengthVerified=planWitnessLengthMatches(witness,values.size(),expected,unit);
            const auto group=std::find_if(groups.begin(),groups.end(),[i](const auto& candidate){
                return std::find(candidate.members.begin(),candidate.members.end(),i)!=candidate.members.end();
            });
            const auto savedProjection=dim.stock?readPlanProjection(id):std::optional<PlanProjection>{};
            const auto baseline=savedProjection?savedProjection->baseline:grips.front();
            bool shiftedVisibleBaseline=false;
            if(planWitnessesVisible(witness,values.size(),unit)&&group!=groups.end()&&group->before>0){
                const auto initial=group->originalBaselines.find(dim.handle);
                shiftedVisibleBaseline=initial!=group->originalBaselines.end()&&initial->second.distanceTo(baseline)*unit>.01;
            }
            bool collisionAdjusted=shiftedVisibleBaseline;
            for(std::size_t k=0;k<values.size()&&collisionAdjusted;++k){
                const auto check=job.drawingChecks.items.find({static_cast<std::size_t>(dim.panel+1),
                    "interior-clearance",dim.handle+"/"+std::to_string(k+1)});
                collisionAdjusted=check!=job.drawingChecks.items.end()&&check->second.state==bcpproduct::PlanCheck::verified;
            }
            const bool placementVerified=lengthVerified||collisionAdjusted;
            if(!placementVerified)
                job.detailPending.push_back({u8"室内尺寸界线",dim.handle,u8"默认可见长度未满足，且无通过净空复验的冲突避让依据"});
            job.drawingChecks.record(dim.panel+1,"interior-baseline",dim.handle,{"D23","QA09"},
                bcpproduct::PlanReleaseGate::mainPlan,placementVerified?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::failed,
                collisionAdjusted?"saved-reopened-collision-adjusted":shiftedVisibleBaseline?
                    "saved-reopened-visible-adjustment-awaiting-text":"saved-reopened",lengthVerified?u8"默认可见界线长度通过":collisionAdjusted?
                    u8"初始实测冲突后移动基线，所有测段净空复验通过":u8"可见界线长度及冲突避让依据未通过");
            out<<"{\"handle\":\""<<dim.handle<<"\",\"source\":\""<<dim.sourceHandle<<"\",\"panel\":"<<dim.panel+1
               <<",\"group\":"<<std::distance(groups.begin(),group)+1
               <<",\"added\":"<<(dim.added?"true":"false")<<",\"measurementVerified\":true,\"stationCoordinates\":"
               <<wholePoints(stations)<<",\"values\":[";
            for(std::size_t k=0;k<values.size();++k){if(k)out<<',';out<<values[k];}
            out<<"],\"witnessVerified\":"<<(lengthVerified?"true":"false")
               <<",\"collisionAdjusted\":"<<(collisionAdjusted?"true":"false")<<",\"visibleWitnessesMM\":[";
            for(std::size_t k=0;k<witness.size();++k){
                if(k)out<<',';
                if(witness[k].suppressed)out<<"null"; else out<<witness[k].visibleLength()*unit;
            }
            out<<"]}";
            std::ostringstream evidence;
            evidence<<"{\"panel\":"<<dim.panel+1<<",\"object\":\""<<dim.handle<<"\",\"requiredMM\":"<<expected
                    <<",\"scope\":\"interior-chain\",\"source\":\""<<dim.sourceHandle
                    <<"\",\"savedReopened\":true,\"measurementVerified\":true,\"report\":\"interior-joint-readback.json\""
                    <<",\"result\":\""<<(lengthVerified?"verified-visible-extension":collisionAdjusted?"verified-collision-adjusted-baseline":"pending")<<"\"}";
            productRecordRuleEvidence(job,"D23",evidence.str());
            productRecordRuleEvidence(job,"QA09",evidence.str());
            const auto entity=open<AcDbEntity>(id);
            const auto layer=open<AcDbLayerTableRecord>(entity->layerId());
            TemplateParts parts(entity.get());
            std::vector<AcDbDimension*> pieces;
            if(auto* stock=AcDbDimension::cast(entity.get()))pieces.push_back(stock);
            else if(entity->explode(parts.values)==Acad::eOk){
                for(int k=0;k<parts.values.length();++k)
                    pieces.push_back(AcDbDimension::cast(static_cast<AcRxObject*>(parts.values[k])));
            }
            std::ostringstream checks;
            const auto native=std::wstring(entity->isA()->name())==L"TDbDimension2"&&!entity->isAProxy();
            const auto color=entity->colorIndex()==256?layer->color().colorIndex():entity->colorIndex();
            checks<<std::setprecision(17)<<"{\"panel\":"<<dim.panel+1<<",\"object\":\""<<dim.handle
                <<"\",\"source\":\""<<dim.sourceHandle<<"\",\"added\":"<<(dim.added?"true":"false")
                <<",\"type\":"<<jsonUtf8(entity->isA()->name())<<",\"layer\":"<<jsonUtf8(symbolName(entity->layerId()))
                <<",\"effectiveACI\":"<<color<<",\"stations\":"<<wholePoints(stations)
                <<",\"scope\":\"room-layout-chain-stations-and-display\",\"savedReopened\":true,\"segments\":[";
            bool displayRead=pieces.size()==values.size();
            for(std::size_t k=0;k<pieces.size();++k){
                if(k)checks<<',';
                auto* piece=pieces[k];
                if(!piece){displayRead=false;checks<<"null";continue;}
                double value{};
                AcString overrideText,shown;
                const bool read=piece->measurement(value)==Acad::eOk&&piece->dimensionText(overrideText)==Acad::eOk&&
                    piece->formatMeasurement(shown,value,overrideText.isEmpty()?nullptr:overrideText.kACharPtr())==Acad::eOk&&!shown.isEmpty();
                const bool plain=overrideText.isEmpty()||std::wstring(overrideText.kACharPtr())==L"<>";
                displayRead=displayRead&&read&&plain&&k<values.size()&&std::abs(value-values[k])*unit<.01&&
                    std::abs(piece->dimlfac()-unit)<1e-7;
                checks<<"{\"measured\":"<<value<<",\"factor\":"<<piece->dimlfac()<<",\"precision\":"<<piece->dimdec()
                    <<",\"override\":"<<jsonUtf8(overrideText.kACharPtr())<<",\"display\":"<<jsonUtf8(shown.kACharPtr())<<'}';
            }
            checks<<"],\"nativeTopology\":"<<(native?"true":"false")<<",\"result\":\""
                <<(displayRead&&native?"verified-in-scope-chain-measurement":"pending")<<"\"}";
            if(!displayRead)job.detailPending.push_back({u8"室内尺寸读数 Q03",dim.handle,u8"尺寸显示覆盖、显示比例或实测读数未通过"});
            for(const auto* rule:{"Q03","TL08","TL10"})productRecordRuleEvidence(job,rule,checks.str());
            const auto retired=retiredMeasurementOverrides.find(dim.sourceHandle);
            if(retired!=retiredMeasurementOverrides.end()){
                const auto original=productReadSourceDimension(saved,dim.sourceHandle,.01/unit);
                const auto current=productReadSourceDimension(saved,dim.handle,.01/unit);
                const bool verified=displayRead&&native&&current.automaticText&&
                    original.displayOverride==retired->second+L"\n"&&
                    !productPlanDimensionVisible(*open<AcDbEntity>(find(saved,dim.sourceHandle)))&&
                    productSameDimensionGeometry(original,current,.01/unit,false);
                displayRead=displayRead&&verified;
                productRecordRuleEvidence(job,"Q03","{\"panel\":"+std::to_string(dim.panel+1)+
                    ",\"object\":\""+dim.handle+"\",\"source\":\""+dim.sourceHandle+
                    "\",\"retiredOverride\":"+jsonUtf8(retired->second.c_str())+
                    ",\"savedReopened\":true,\"result\":\""+
                    (verified?"verified-automatic-measurement-replacement":"pending")+"\"}");
            }
            const auto styles=productDimensionTexts(*entity);
            const auto& policy=job.dimensionStylePolicy;
            const bool effectiveStyle=styles.size()==values.size()&&std::all_of(styles.begin(),styles.end(),[&](const auto& style){
                return productDimensionMatches(style,policy.name,scale,policy.nominalPaperHeightMM*scale/unit);
            });
            std::ostringstream styleEvidence;
            styleEvidence<<std::setprecision(17)<<"{\"panel\":"<<dim.panel+1<<",\"object\":\""<<dim.handle
                <<"\",\"source\":\""<<dim.sourceHandle<<"\",\"native\":"<<(native?"true":"false")
                <<",\"measurementDisplayVerified\":"<<(displayRead?"true":"false")<<",\"segments\":[";
            for(std::size_t k=0;k<styles.size();++k){
                if(k)styleEvidence<<',';
                const auto& style=styles[k];
                styleEvidence<<"{\"style\":"<<jsonUtf8(style.style.isNull()?L"":symbolName(style.style))
                    <<",\"scale\":"<<style.scale<<",\"height\":"<<style.height<<",\"fixed\":"<<style.fixed;
                if(!style.font.isNull()){
                    const auto font=productReadDimensionFont(style.font);
                    styleEvidence<<",\"font\":"<<jsonUtf8(font.primary)<<",\"bigFont\":"<<jsonUtf8(font.big)
                        <<",\"widthFactor\":"<<font.width;
                }
                styleEvidence<<'}';
            }
            styleEvidence<<"],\"scope\":\"interior-native-value-display-style\",\"savedReopened\":true,\"result\":\""
                <<(displayRead&&native&&effectiveStyle?"verified-in-scope-value-and-style":"pending")<<"\"}";
            productRecordRuleEvidence(job,"D22",styleEvidence.str());
            if(!dim.sourceHandle.empty()){
                const bool checked=displayRead&&native&&effectiveStyle&&placementVerified;
                // The source dimension's panel is taken from its original extents.
                for(const auto& [key,item]:job.sourceInventory.items){
                    const auto& [panel,scope,source]=key;
                    if(scope=="source-dimension"&&source==dim.sourceHandle){
                        productCheckPlanSource(job,panel,scope,source,
                            checked?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::pending,{dim.handle},
                            checked?u8"原测点、测值、样式和界线经独立重开；避让见 D25":u8"室内测值显示、原生类型、样式或界线待核");
                        break;
                    }
                }
            }
            if(i==0){
                const auto unverified=std::string("{\"scope\":\"unclassified-nested-and-line-text-dimensions\",\"result\":\"pending\",\"detail\":")+
                    jsonUtf8(L"独立块成员和源线文尺寸需另建覆盖清单")+"}";
                productRecordRuleEvidence(job,"Q03",unverified);
                productRecordRuleEvidence(job,"TL08",unverified);
                productRecordRuleEvidence(job,"TL10",std::string("{\"scope\":\"whole-chain-and-shared-node-edit-restore\",\"result\":\"pending\",\"detail\":")+
                    jsonUtf8(L"室内链已读回，整体及共享节点编辑恢复需另测")+"}");
            }
            if(dim.projection){
                const auto& source=*dim.projection;
                const bool retainedReference=dim.stock||!productPlanDimensionVisible(*open<AcDbEntity>(find(saved,dim.sourceHandle)));
                std::ostringstream projection;
                projection<<std::setprecision(17)<<"{\"panel\":"<<dim.panel+1<<",\"source\":\""<<dim.sourceHandle
                    <<"\",\"object\":\""<<dim.handle<<"\",\"stations\":"<<wholePoints({source.a,source.b})
                    <<",\"direction\":["<<source.direction.x<<','<<source.direction.y<<','<<source.direction.z<<']'
                    <<",\"sourceBaseline\":"<<wholePoints({source.baseline})<<",\"sourceMeasurement\":"<<source.measurement
                    <<",\"sourceCaption\":"<<jsonUtf8(source.caption)<<",\"associated\":"<<(source.associated?"true":"false")
                    <<",\"originalReferenceRetained\":"<<(retainedReference?"true":"false")
                    <<",\"savedReopened\":true,\"result\":\""<<(dim.stock?"pending-conversion-stock-preserved":
                        retainedReference?"converted-and-verified":"pending-original-reference-visibility")<<"\"}";
                productRecordRuleEvidence(job,"D26",projection.str());
            }
        }
        out<<"],\"groups\":[";
        for(std::size_t i=0;i<groups.size();++i){
            if(i)out<<',';
            const auto& group=groups[i];
            std::ostringstream record;
            record<<"{\"panel\":"<<group.panel+1<<",\"group\":"<<i+1<<",\"before\":"<<group.before
                  <<",\"after\":"<<group.after<<",\"overlaps\":"<<group.overlaps<<",\"unreadable\":"<<group.unreadable
                  <<",\"sideIssues\":"<<group.sideIssues
                  <<",\"segmentAssociationIssues\":"<<group.segmentAssociationIssues
                  <<",\"scope\":\"interior-group\",\"savedReopened\":true,\"report\":\"interior-joint-readback.json\",\"result\":\""
                  <<(group.after||group.unreadable||group.sideIssues||group.segmentAssociationIssues?
                      "pending":"layout-verified")<<"\"}";
            productRecordRuleEvidence(job,"D25",record.str());
            productRecordRuleEvidence(job,"QA09",record.str());
            out<<record.str();
        }
        out<<"]}";
        writeNew(job.root/L"interior-joint-readback.json",out.str());
    };
}
