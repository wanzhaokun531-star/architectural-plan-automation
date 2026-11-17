double roomPolygonArea(const std::vector<AcGePoint2d>& points) {
    double cross = 0;
    for (std::size_t i=0;i<points.size();++i) {
        const auto a=points[i]-points.front(), b=points[(i+1)%points.size()]-points.front();
        require(points[i].distanceTo(points[(i+1)%points.size()])>1e-5, "duplicate room polygon vertex");
        cross+=a.x*b.y-a.y*b.x;
    }
    return std::abs(cross)/2;
}

bool roomBoundariesCross(const std::vector<AcGePoint2d>& a,const std::vector<AcGePoint2d>& b,bool same) {
    const auto cross=[](const AcGePoint2d& x,const AcGePoint2d& y,const AcGePoint2d& z) {
        const auto p=y-x,q=z-x;
        return p.x*q.y-p.y*q.x;
    };
    for(std::size_t i=0;i<a.size();++i)for(std::size_t j=0;j<b.size();++j){
        const auto& p=a[i]; const auto& q=a[(i+1)%a.size()];
        const auto& s=b[j]; const auto& t=b[(j+1)%b.size()];
        if(same&&(j==i||j==(i+1)%a.size()||i==(j+1)%a.size()))continue;
        if(cross(p,q,s)*cross(p,q,t)<-1e-8&&cross(s,t,p)*cross(s,t,q)<-1e-8)return true;
    }
    return false;
}

bool strictlyInRoom(const std::vector<AcGePoint2d>& ring,const AcGePoint2d& at) {
    if(!genericInside(ring,at))return false;
    for(std::size_t i=0;i<ring.size();++i){
        const auto& p=ring[i]; const auto& q=ring[(i+1)%ring.size()];
        const auto edge=q-p, probe=at-p;
        const double dot=edge.dotProduct(probe);
        if(std::abs(edge.x*probe.y-edge.y*probe.x)<1e-5&&dot>=0&&dot<=edge.dotProduct(edge))return false;
    }
    return true;
}

std::vector<ConfirmedPlanRoom> readConfirmedPlanRooms(
    const fs::path& request,const std::string& sourceHash,double unit) {
    if(request.empty())return {};
    std::ifstream in(request);
    std::string tag,hash;
    require(bool(in>>tag>>hash)&&tag=="PLANROOMS1"&&hash==sourceHash,"confirmed rooms do not match input DWG");
    std::vector<ConfirmedPlanRoom> rooms;
    std::set<std::string> handles;
    ConfirmedPlanRoom room{};
    std::string prefix;
    int n;
    while(in>>room.panel>>room.source>>room.area>>n>>std::quoted(prefix)){
        require(room.panel>0&&n>=3&&handles.insert(room.source).second,"invalid or duplicate confirmed room");
        room.prefix=bcpproduct::decodeMessage(prefix);
        room.vertices.resize(static_cast<std::size_t>(n));
        for(auto& p:room.vertices)
            require(bool(in>>p.x>>p.y)&&std::isfinite(p.x)&&std::isfinite(p.y),"invalid confirmed room vertex");
        const auto measured=roomPolygonArea(room.vertices)*unit*unit/1e6;
        require(std::isfinite(room.area)&&measured>0&&std::abs(measured-room.area)<1e-5&&
            !roomBoundariesCross(room.vertices,room.vertices,true),"confirmed polygon area or topology differs");
        for(const auto& other:rooms)if(other.panel==room.panel){
            const auto inside=[](const auto& vertices,const auto& ring){
                for(std::size_t i=0;i<vertices.size();++i)
                    if(strictlyInRoom(ring,vertices[i])||
                       strictlyInRoom(ring,vertices[i]+(vertices[(i+1)%vertices.size()]-vertices[i])*0.5))return true;
                return false;
            };
            require(!roomBoundariesCross(room.vertices,other.vertices,false)&&
                !inside(room.vertices,other.vertices)&&!inside(other.vertices,room.vertices),
                "confirmed room polygons overlap");
        }
        rooms.push_back(room);
    }
    require(in.eof()&&!rooms.empty(),"invalid or empty confirmed room job");
    return rooms;
}

ProductRoomGrid confirmedRoomPlacementGrid(const std::vector<AcGePoint2d>& vertices,double step) {
    ProductRoomGrid grid;
    const auto cuts=[&](bool x,std::vector<double>& axis){
        for(const auto& p:vertices)axis.push_back(x?p.x:p.y);
        std::sort(axis.begin(),axis.end());
        axis.erase(std::unique(axis.begin(),axis.end()),axis.end());
        const double start=axis.front(),end=axis.back();
        for(double v=start+step;v<end;v+=step)axis.push_back(v);
        std::sort(axis.begin(),axis.end());
    };
    cuts(true,grid.xs);cuts(false,grid.ys);
    grid.cells.assign(grid.width()*grid.height(),0);
    grid.areas.push_back((grid.xs.back()-grid.xs.front())*(grid.ys.back()-grid.ys.front()));
    return grid;
}

void applyConfirmedPlanRooms(ProductJob& job,AcDbDatabase& db,const std::vector<ConfirmedPlanRoom>& rooms,
    const std::vector<ProductPlanBounds>& panels,std::vector<ProductPlanNameCheck>& names,
    double scale,double unit,std::vector<ProductRoomWriteReceipt>& written) {
    if(rooms.empty())return;
    AcDbDatabase donor(false,true);
    readDrawing(donor,job.profile/L"room-material.dwg");
    const auto pair=productRoomAreaPair(donor);
    require(pair.has_value(),"missing confirmed area material");
    for(const auto id:{pair->body,pair->exponent})
        require(!productRoomImportStyle(donor,open<AcDbText>(id)->textStyle(),db).isNull(),"area font missing");
    const auto layer=productPlanLayer(db,L"A-房间名称",7);
    std::vector<AcDbExtents> obstacles;
    productRoomGraphicObstacles(db,obstacles);
    struct Existing { AcGePoint3d centre; std::string handle; std::wstring value; };
    std::vector<Existing> existing;
    std::vector<Existing> qualifiers;
    std::vector<std::string> exponents;
    for(const auto& [handle,type]:modelTypes(db)){
        if(type!=L"TDbText"&&type!=L"AcDbText"&&type!=L"AcDbMText"&&type!=L"TDbOpening"&&type!=L"TDbGroupOpening")continue;
        const auto entity=open<AcDbEntity>(find(db,ascii(handle)));
        if(!productPlanDimensionVisible(*entity))continue;
        productGenericTextLeaves(*entity,obstacles);
        const auto* text=AcDbText::cast(entity.get());
        AcDbExtents box;
        if(text&&std::wstring(text->textString())==L"2")exponents.push_back(ascii(handle));
        if(text&&text->getGeomExtents(box)==Acad::eOk&&std::any_of(rooms.begin(),rooms.end(),[&](const auto& room){
            return !room.prefix.empty()&&std::wstring(text->textString())==room.prefix;
        }))qualifiers.push_back({box.minPoint()+(box.maxPoint()-box.minPoint())*.5,ascii(handle),text->textString()});
        const bool confirmedCaption=text&&std::any_of(rooms.begin(),rooms.end(),[&](const auto& room){
            return std::wstring(text->textString())==room.prefix+bcpproduct::decodeMessage(bcpproduct::formatArea(room.area));
        });
        if(text&&(productRoomAreaText(text->textString())||confirmedCaption)&&text->getGeomExtents(box)==Acad::eOk)
            existing.push_back({box.minPoint()+(box.maxPoint()-box.minPoint())*0.5,ascii(handle),text->textString()});
    }
    for(const auto& room:rooms){
        require(static_cast<std::size_t>(room.panel)<=panels.size(),"unknown confirmed room panel");
        const auto name=std::find_if(names.begin(),names.end(),[&](const auto& item){return item.source==room.source;});
        if(name==names.end()||name->view+1!=room.panel||!name->pendingReason.empty()||!name->conversionError.empty()){
            job.detailPending.push_back({u8"已确认净域",room.source,u8"名称或图幅关系未通过，保留边界记录"});
            continue;
        }
        auto nameEntity=open<AcDbEntity>(find(db,name->output));
        AcDbExtents nameBounds;
        status(nameEntity->getGeomExtents(nameBounds),"confirmed room name bounds");
        const auto nameCentre=nameBounds.minPoint()+(nameBounds.maxPoint()-nameBounds.minPoint())*.5;
        require(strictlyInRoom(room.vertices,{nameCentre.x,nameCentre.y}),"confirmed room name outside its polygon");
        nameEntity.reset();
        const auto caption=room.prefix+bcpproduct::decodeMessage(bcpproduct::formatArea(room.area));
        const auto prior=std::find_if(existing.begin(),existing.end(),[&](const auto& entry){
            return strictlyInRoom(room.vertices,{entry.centre.x,entry.centre.y});
        });
        if(prior!=existing.end()){
            const auto count=std::count_if(existing.begin(),existing.end(),[&](const auto& entry){
                return strictlyInRoom(room.vertices,{entry.centre.x,entry.centre.y});
            });
            const auto body=open<AcDbText>(find(db,prior->handle));
            AcDbExtents bounds;
            status(body->getGeomExtents(bounds),"existing confirmed area bounds");
            std::vector<std::string> matched;
            for(const auto& handle:exponents){
                const auto exponent=open<AcDbText>(find(db,handle));
                AcDbExtents ext;
                if(exponent->getGeomExtents(ext)!=Acad::eOk)continue;
                const double gap=ext.minPoint().x-bounds.maxPoint().x;
                const double rise=ext.minPoint().y-bounds.minPoint().y;
                if(exponent->layerId()==body->layerId()&&exponent->textStyle()==body->textStyle()&&
                   std::abs(body->height()-2*exponent->height())<1e-5&&std::abs(gap)<body->height()/2&&
                   rise>0&&rise<body->height())matched.push_back(handle);
            }
            std::vector<std::string> matchingQualifiers;
            const auto numeric=bcpproduct::decodeMessage(bcpproduct::formatArea(room.area));
            if(!room.prefix.empty()&&prior->value==numeric)for(const auto& item:qualifiers){
                if(item.value!=room.prefix||!strictlyInRoom(room.vertices,{item.centre.x,item.centre.y}))continue;
                const auto qualifier=open<AcDbText>(find(db,item.handle));AcDbExtents ext;
                if(qualifier->layerId()!=layer||qualifier->getGeomExtents(ext)!=Acad::eOk)continue;
                const double bodyCentre=(bounds.minPoint().x+bounds.maxPoint().x)*.5;
                if(std::abs(item.centre.x-bodyCentre)<=body->height()*.5&&
                   ext.minPoint().y>=bounds.maxPoint().y&&ext.minPoint().y-bounds.maxPoint().y<body->height()&&
                   ext.maxPoint().y<=nameBounds.minPoint().y)matchingQualifiers.push_back(item.handle);
            }
            const bool combined=prior->value==caption;
            const bool split=prior->value==numeric&&matchingQualifiers.size()==1;
            if(count==1&&(combined||split)&&matched.size()==1&&body->layerId()==layer){
                written.push_back({room.panel,name->output,prior->handle,matched.front(),room.area,caption});
                if(split)written.back().qualifier=matchingQualifiers.front();
                continue;
            }
            job.detailPending.push_back({u8"已确认净域",room.source,
                u8"域内已有面积 "+prior->handle+u8"，期望 "+productUtf8(caption)+u8"，实际 "+productUtf8(prior->value)});
            continue;
        }
        ProductRoomName anchor{name->output,name->text,ProductRoomRole::indoor,{}};
        auto entity=open<AcDbEntity>(find(db,anchor.handle));
        status(entity->getGeomExtents(anchor.bounds),"confirmed room name bounds");
        const auto centre=anchor.bounds.minPoint()+(anchor.bounds.maxPoint()-anchor.bounds.minPoint())*0.5;
        require(inPanel(centre,panels[room.panel-1]),"confirmed room name outside its panel");
        const auto box=anchor.bounds;
        entity.reset();
        const double drawingScale=scale/unit/100;
        const auto grid=confirmedRoomPlacementGrid(room.vertices,160*drawingScale);
        const auto placed=productRoomPlaceArea(db,donor,*pair,layer,grid,0,&anchor,room.area,drawingScale,
            obstacles,&room.vertices,caption);
        if(!placed){
            job.detailPending.push_back({u8"已确认净域",room.source,u8"净面积已复算，域内未找到可放置的位置"});
            continue;
        }
        if(name->expectedAnchor)name->expectedAnchor=*name->expectedAnchor+placed->nameShift;
        written.push_back({room.panel,name->output,placed->body,placed->exponent,room.area,caption});
        written.back().qualifier=placed->qualifier;
        for(const auto& handle:{placed->body,placed->exponent,placed->qualifier}){
            if(handle.empty())continue;
            AcDbExtents bounds;
            status(open<AcDbEntity>(find(db,handle))->getGeomExtents(bounds),"confirmed area rendered extent");
            obstacles.push_back(bounds);
        }
        if(placed->nameShift.length()>1e-5)
            obstacles.emplace_back(box.minPoint()+placed->nameShift,box.maxPoint()+placed->nameShift);
    }
}
