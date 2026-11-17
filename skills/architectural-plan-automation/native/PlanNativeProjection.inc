struct PlanProjection {
    AcGePoint3d a, b, baseline, textPosition;
    AcGeVector3d direction, normal;
    double measurement{}, oblique{};
    std::wstring caption;
    bool rotated{}, associated{};
};

std::optional<PlanProjection> readPlanProjection(AcDbObjectId id) {
    const auto entity = open<AcDbEntity>(id);
    const auto* rotated = AcDbRotatedDimension::cast(entity.get());
    const auto* aligned = AcDbAlignedDimension::cast(entity.get());
    if (!rotated && !aligned) return std::nullopt;

    auto& dimension = *AcDbDimension::cast(entity.get());
    PlanProjection projection;
    projection.rotated = rotated != nullptr;
    projection.normal = dimension.normal();
    projection.a = rotated ? rotated->xLine1Point() : aligned->xLine1Point();
    projection.b = rotated ? rotated->xLine2Point() : aligned->xLine2Point();
    projection.baseline = rotated ? rotated->dimLinePoint() : aligned->dimLinePoint();
    projection.oblique = rotated ? rotated->oblique() : aligned->oblique();
    projection.textPosition = dimension.textPosition();
    if (rotated) {
        const auto angle = rotated->rotation();
        projection.direction = AcGeVector3d(std::cos(angle), std::sin(angle), 0);
        projection.direction.transformBy(AcGeMatrix3d::planeToWorld(projection.normal));
    } else {
        const auto delta = projection.b - projection.a;
        if (delta.length() <= 1e-5) return std::nullopt;
        projection.direction = delta.normal();
    }
    AcString caption;
    if (dimension.dimensionText(caption) != Acad::eOk ||
        dimension.measurement(projection.measurement) != Acad::eOk ||
        !std::isfinite(projection.measurement) || projection.measurement <= 1e-5) return std::nullopt;
    projection.caption = caption.kACharPtr();

    // A replaced stock dimension must not lose a live geometry association.
    const auto dictionary = dimension.extensionDictionary();
    if (!dictionary.isNull()) projection.associated = open<AcDbDictionary>(dictionary)->has(L"ACAD_DIMASSOC");
    return projection;
}

void verifyNativePlanProjection(AcDbObjectId id, const PlanProjection& source) {
    require(std::wstring(open<AcDbEntity>(id)->isA()->name()) == L"TDbDimension2", "projection candidate is not a native Tianzheng chain");
    const auto points = chainGrips(id);
    const auto values = chainValues(id);
    require(points.size() == 8 && values.size() == 1, "projection candidate is not a single native segment");
    require((points[2].distanceTo(source.a) < 1e-5 && points[3].distanceTo(source.b) < 1e-5) ||
            (points[2].distanceTo(source.b) < 1e-5 && points[3].distanceTo(source.a) < 1e-5),
            "native projection did not retain the two source stations");
    const auto along = points[1] - points[0];
    require(along.length() > 1e-5 && along.normal().crossProduct(source.direction).length() < 1e-8,
            "native projection changed the measuring direction");
    require(std::abs(values.front() - source.measurement) < 1e-5,
            "native projection changed the measured value");
}

void verifyStockPlanProjection(AcDbObjectId id, const PlanProjection& source) {
    const auto actual = readPlanProjection(id);
    require(actual && actual->rotated == source.rotated && actual->a.distanceTo(source.a) < 1e-5 &&
            actual->b.distanceTo(source.b) < 1e-5 && (actual->direction-source.direction).length() < 1e-8 &&
            (actual->normal-source.normal).length() < 1e-8 && std::abs(actual->oblique-source.oblique) < 1e-8 &&
            std::abs(actual->measurement-source.measurement) < 1e-5 && actual->caption == source.caption &&
            actual->associated == source.associated, "retained CAD dimension changed its defining geometry or text");
}

void styleRetainedPlanProjection(ProductJob& job,AcDbDatabase& db,AcDbObjectId id,const PlanProjection& source,
                                 double scale,double unit) {
    const auto base=productPlanDimensionBase(job,db);
    if(base.isNull())return;
    const auto font=open<AcDbDimStyleTableRecord>(base)->dimtxsty();
    if(font.isNull()||!productDimensionEqual(open<AcDbTextStyleTableRecord>(font)->textSize(),0))return;
    auto current=open<AcDbDimension>(id);
    const auto oldStyle=current->dimensionStyle();
    const double height=job.dimensionStylePolicy.nominalPaperHeightMM*scale/unit;
    if(productDimensionMatches(productDimensionText(*current),job.dimensionStylePolicy.name,scale,height)&&
        current->dimtmove()==2)return;
    const auto oldFont=current->dimtxsty();
    const double oldScale=current->dimscale(),oldHeight=current->dimtxt(),oldFactor=current->dimlfac();
    const auto oldMove=current->dimtmove();
    const double scaleFactor=oldScale>0?oldScale:scale/unit;
    current.reset();
    const auto apply=[&](AcDbObjectId style,AcDbObjectId font,double factor,double textHeight,int move){
        auto dimension=editObject<AcDbDimension>(id);
        status(dimension->setDimensionStyle(style),"set retained projection dimension style");
        status(dimension->setDimtxsty(font),"set retained projection font");
        status(dimension->setDimscale(factor),"set retained projection drawing scale");
        status(dimension->setDimtxt(textHeight),"set retained projection paper height");
        status(dimension->setDimlfac(oldFactor),"retain projection display measurement factor");
        status(dimension->setDimtmove(move),"set retained projection movement mode");
        status(dimension->recomputeDimBlock(true),"refresh retained projection style");
    };
    try{
        apply(base,font,scaleFactor,height/scaleFactor,2);
        verifyStockPlanProjection(id,source);
    }catch(const std::exception& error){
        apply(oldStyle,oldFont,oldScale,oldHeight,oldMove);
        job.detailPending.push_back({u8"普通投影尺寸样式",axisHandle(id),error.what()});
    }
}

AcDbObjectId tryConvertPlanProjection(AcDbDatabase& db, AcDbObjectId sourceId, AcDbObjectId prototype,
                                     const PlanProjection& source, std::string& reason) {
    const auto fail = [&](const std::string& text) { reason = text; return AcDbObjectId{}; };
    if (source.associated) return fail(u8"原 CAD 尺寸有关联几何，已保留原对象");
    if (!source.caption.empty() && source.caption != L"<>") return fail(u8"原 CAD 尺寸有文字覆盖，已保留原对象");
    if ((source.normal-AcGeVector3d::kZAxis).length() > 1e-8 ||
        std::abs(source.a.z-source.b.z) > 1e-5 || std::abs(source.oblique) > 1e-8)
        return fail(u8"原 CAD 尺寸有非平面或倾斜界线参数，已保留原对象");

    const auto direction = (source.b-source.a).dotProduct(source.direction) < 0 ? -source.direction : source.direction;
    const auto normal = AcGeVector3d(-direction.y,direction.x,0);
    const double projection = (source.b-source.a).dotProduct(direction);
    if (std::abs(projection-source.measurement) > 1e-5) return fail(u8"原尺寸实测与公开投影几何不一致");

    AcDbObjectId candidate;
    try {
        const auto sample = chainGrips(prototype);
        require(sample.size() == 8 && chainValues(prototype).size() == 1, "single native projection prototype required");
        const auto sampleDirection = (sample[3]-sample[2]).normal();
        const auto angle = std::atan2(direction.y,direction.x)-std::atan2(sampleDirection.y,sampleDirection.x);
        const double textOffset = std::abs((sample[4]-sample[0]).dotProduct(
            AcGeVector3d(-sampleDirection.y,sampleDirection.x,0)));
        candidate = productCloneDetail(db,prototype);
        {
            const auto sourceEntity = open<AcDbEntity>(sourceId);
            auto entity = editObject<AcDbEntity>(candidate);
            status(entity->transformBy(AcGeMatrix3d::rotation(angle,AcGeVector3d::kZAxis,sample[2])), "orient native projection");
            status(entity->transformBy(AcGeMatrix3d::translation(source.a-sample[2])), "position native projection");
            status(entity->setLayer(sourceEntity->layerId()), "retain projection dimension layer");
        }
        // Keep the prototype's measuring direction while moving the second
        // station, including any offset normal to the measuring direction.
        moveDimensionGrip(candidate,3,source.b);
        const double baseline = (source.baseline-source.a).dotProduct(normal);
        moveDimensionGrip(candidate,0,source.a+normal*baseline);
        const auto textNormal = normal.y < -1e-8 || (std::abs(normal.y) <= 1e-8 && normal.x > 0) ? -normal : normal;
        moveDimensionGrip(candidate,4,source.a+direction*(projection/2)+normal*baseline+textNormal*textOffset);
        verifyNativePlanProjection(candidate,source);
        require(std::abs((chainGrips(candidate)[0]-source.baseline).dotProduct(normal)) < 1e-5,
                "native projection could not reproduce the original baseline");
        return candidate;
    } catch (const std::exception& error) {
        if (!candidate.isNull()) status(editObject<AcDbEntity>(candidate)->erase(), "retire failed native projection trial");
        return fail(error.what());
    }
}

GenericDimensionBoxes readPlanStockDimensionText(AcDbDatabase& db, AcDbObjectId id) {
    const auto entity = open<AcDbEntity>(id);
    auto* dimension = AcDbDimension::cast(entity.get());
    if (!dimension) return {};
    std::vector<AcDbExtents> displayed;
    productGenericTextLeaves(*dimension,displayed);
    if (!displayed.empty()) {
        auto box = displayed.front();
        for (const auto& part:displayed) box.addExt(part);
        return {box};
    }
    const auto projection = readPlanProjection(id);
    AcString caption, overrideText;
    if (!projection || dimension->dimensionText(overrideText) != Acad::eOk ||
        dimension->formatMeasurement(caption,projection->measurement,overrideText.isEmpty()?nullptr:overrideText.kACharPtr()) != Acad::eOk ||
        caption.isEmpty()) return {std::nullopt};
    const double height = dimension->dimtxt()*dimension->dimscale();
    double angle = dimension->textRotation();
    if (std::abs(angle) < 1e-8) {
        angle = std::atan2(projection->direction.y,projection->direction.x);
        if (angle > std::acos(0.)+1e-8) angle -= std::acos(-1.);
        if (angle < -std::acos(0.)-1e-8) angle += std::acos(-1.);
    }
    AcDbMText sample;
    sample.setDatabaseDefaults(&db);
    if (height<=0 || sample.setTextStyle(dimension->dimtxsty())!=Acad::eOk ||
        sample.setTextHeight(height)!=Acad::eOk || sample.setAttachment(AcDbMText::kMiddleCenter)!=Acad::eOk ||
        sample.setRotation(angle)!=Acad::eOk || sample.setLocation(dimension->textPosition())!=Acad::eOk)
        return {std::nullopt};
    sample.setContents(caption.kACharPtr());
    AcDbExtents box;
    return {sample.getGeomExtents(box)==Acad::eOk ? std::optional<AcDbExtents>(box) : std::nullopt};
}
