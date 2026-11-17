void productPlanAuditRow(ProductJob& job,std::initializer_list<const char*> rules,std::size_t panel,
 const std::string& handle,const char* scope,const char* result,const std::string& detail,
 const std::string& measured={}){
 std::ostringstream row;
 row<<"{\"panel\":"<<panel<<",\"object\":\""<<handle<<"\",\"scope\":\""<<scope
    <<"\",\"savedReopened\":true,\"result\":\""<<result<<"\",\"detail\":"
    <<jsonUtf8(bcpproduct::decodeMessage(detail));
 if(!measured.empty())row<<','<<measured;
 row<<'}';
 for(const auto* rule:rules)productRecordRuleEvidence(job,rule,row.str());
}

struct ProductPlanSourceGeometry {
 std::string handle;
 std::wstring type;
 std::size_t panel{};
 std::vector<AcGePoint3d> geometry;
 double width{},height{};
 std::wstring openingStyle,openingNumber;
};

std::vector<ProductPlanSourceGeometry> productCapturePlanSourceGeometry(AcDbDatabase& db,const GenericNativeInput& input,
 const std::vector<bcpproduct::GenericView>& views){
 std::vector<ProductPlanSourceGeometry> result;
 const auto add=[&](const std::string& handle,const std::wstring& type,const AcGePoint3d& anchor,
                    const GenericNativeOpening* opening=nullptr){
  const auto id=find(db,handle);
  auto points=chainGrips(id);
  if(type==L"TDbOpening"||type==L"TDbGroupOpening"){
   if(points.size()<3)return;
   points.resize(3);
  }
  result.push_back({handle,type,bcpproduct::nearestGenericView(views,{anchor.x,anchor.y})+1,std::move(points),
   opening?opening->width:0.,opening?opening->height:0.,opening?opening->style:L"",opening?opening->existing:L""});
 };
 for(const auto& wall:input.walls)add(wall.wall.handle,L"TDbWall",{wall.wall.start.x,wall.wall.start.y,0});
 for(const auto& column:input.columns)add(column.handle,L"TDbColumn",column.centre);
 for(const auto& opening:input.openings)add(opening.handle,opening.grouped?L"TDbGroupOpening":L"TDbOpening",opening.a,&opening);
 return result;
}

void productVerifyPlanSourceGeometry(ProductJob& job,AcDbDatabase& saved,
 const std::vector<ProductPlanSourceGeometry>& source){
 Working work(&saved);
 for(const auto& row:source){
  const auto change=job.doorJambChanges.find(row.handle);
  const bool moved=change!=job.doorJambChanges.end()&&change->second.offset.length()>0.;
  auto offset=moved?change->second.offset:AcGeVector3d::kIdentity;
  const auto bases=productPlanDetailConfirmations(job).elevationBases;
  const auto base=bases.find(row.handle);
  const bool baseChanged=base!=bases.end();
  if(baseChanged){
   require(!row.geometry.empty()&&std::abs(row.geometry.front().z-base->second[0])*job.unifiedUnitMM<=.01,
    "source geometry matches confirmed original base");
   offset.z+=base->second[1]-base->second[0];
  }
  std::vector<AcGePoint3d> actual;
  AcDbObjectId id;
  const auto handle=bcpproduct::decodeMessage(row.handle);
  bool same=saved.getAcDbObjectId(id,false,AcDbHandle(handle.c_str()))==Acad::eOk&&!id.isErased()&&!row.geometry.empty();
  if(same){
   const auto entity=open<AcDbEntity>(id);
   same=std::wstring(entity->isA()->name())==row.type&&!entity->isAProxy();
   if(same){
    actual=chainGrips(id);
    const bool opening=row.type==L"TDbOpening"||row.type==L"TDbGroupOpening";
    same=(opening?actual.size()>=row.geometry.size():actual.size()==row.geometry.size())&&
      std::equal(row.geometry.begin(),row.geometry.end(),actual.begin(),[&](const auto& a,const auto& b){
       return (a+offset).distanceTo(b)*job.unifiedUnitMM<=.01;
      });
   }
  }
  productPlanAuditRow(job,{"G05","Q02"},row.panel,row.handle,"source-native-geometry",
   same?(baseChanged?"verified-confirmed-native-base-translation":moved?"verified-native-jamb-translation":"verified-native-type-and-grips"):"pending-native-type-or-grips",
   same?(baseChanged?u8"源绑定的基标高修正：逐点核原XY和目标Z；构件属性另按修正前后读回核验":moved?u8"原生门三个洞口夹点均按 TL11 沿宿主刚性移动":u8"原生类型和所核实体夹点与源图一致；门窗比较实际洞口前三点"):
      u8"重开后实体类型或所核几何夹点偏离预期，需复核",
   "\"sourceType\":"+jsonUtf8(row.type)+",\"sourcePoints\":"+wholePoints(row.geometry));
  const bool wall=row.type==L"TDbWall",column=row.type==L"TDbColumn";
  const bool opening=row.type==L"TDbOpening"||row.type==L"TDbGroupOpening";
  if(wall||column||opening){
   std::ostringstream measured;
   measured<<std::setprecision(17)<<"\"sourceType\":"<<jsonUtf8(row.type)<<",\"sourcePoints\":"<<wholePoints(row.geometry);
   if(opening)measured<<",\"sourceWidthMM\":"<<row.width*job.unifiedUnitMM<<",\"sourceHeightMM\":"<<row.height*job.unifiedUnitMM
     <<",\"sourceStyle\":"<<jsonUtf8(row.openingStyle)<<",\"sourceOwnNumber\":"<<jsonUtf8(row.openingNumber);
   productPlanAuditRow(job,{"R01","R02","R04"},row.panel,row.handle,"source-geometry-fixed-constraints",
    same?(moved?"verified-door-jamb-translation":"verified-source-fixed-geometry"):"pending-source-geometry-change",
    same?(moved?u8"按 TL11 沿宿主整体调整门洞，重开核原测点的刚性位移及洞口几何":
     u8"源墙、柱或洞口定位公开测点保持，未对各段单独改数；实际约束及整百诉求须结合项目确认"):
         u8"源几何与最终不等，需复核共享节点、关联尺寸及固定构件约束",
    measured.str());
  }
  if(moved){
   const auto& move=change->second;
   if(actual.size()>3)actual.resize(3);
   std::ostringstream geometry;
   geometry<<std::setprecision(17)<<"\"hostWall\":\""<<move.host<<"\",\"adjacentWall\":\""<<move.adjacent
     <<"\",\"priorNetJambMM\":"<<move.priorMM<<",\"standardJambMM\":"<<move.standardMM
     <<",\"sourcePoints\":"<<wholePoints(row.geometry)<<",\"finalPoints\":"<<wholePoints(actual);
   productPlanAuditRow(job,{"D13","TL11"},row.panel,row.handle,"native-door-jamb-rigid-translation",
    same?"verified-door-jamb-rigid-translation":"pending-door-jamb-rigid-translation",
    u8"重开比较实际洞口三点和源图，验证沿宿主移动及保留洞口宽度",geometry.str());
  }
  productPlanAuditRow(job,{"TL07"},row.panel,row.handle,"source-bound-model-object",
   same?(moved?"verified-native-jamb-translation":"verified-source-bound-native-geometry"):"pending-source-bound-native-geometry",
   u8"按当前源图实际原生对象逐件比对；字体、完整编辑能力及局部图面见相应专项",
   "\"sourceType\":"+jsonUtf8(row.type)+",\"sourcePoints\":"+wholePoints(row.geometry));
 }
 const auto count=std::count_if(source.begin(),source.end(),[](const auto& row){return row.type==L"TDbOpening";});
 productPlanAuditRow(job,{"R03"},0,"","no-confirmed-project-exception",
  "not-applicable-no-project-200-600-exception",u8"本作业未提交 200／600 例外确认，未套用历史特例，现行门垛条件按 TL11 逐门检查",
  "\"nativeOpeningRoots\":"+std::to_string(count));
}

void productAuditPlanInputAndDependencies(ProductJob& job,AcDbDatabase& saved,const GenericNativeInput& input,
 const std::vector<bcpproduct::GenericView>& views,const std::vector<bool>& generated){
 if(!job.unifiedPlan)return;
 Working work(&saved);
 const auto units=saved.insunits();
 std::set<std::size_t> named;
 auto titles=input.titles;
 for(const auto& note:input.notes)if(bcpproduct::explicitPlanFloor(note.text))titles.push_back(note);
 for(const auto& title:titles){
  const auto panel=bcpproduct::nearestGenericView(views,{title.anchor.x,title.anchor.y})+1;
  if(!generated[panel-1])continue;
  const auto id=find(saved,title.handle);
  const auto entity=open<AcDbEntity>(id);
  std::vector<ProductRoomLeaf> leaves;
  productRoomLeaves(*entity,leaves);
  const auto floor=bcpproduct::explicitPlanFloor(title.text);
  const bool font=std::any_of(leaves.begin(),leaves.end(),[&](const auto& leaf){
   return floor&&bcpproduct::explicitPlanFloor(leaf.text)==floor&&productPlanTitleTextStyle(saved,leaf.style);
  });
  const std::wstring type=entity->isA()->name();
  const bool valid=(type==L"TDbDrawingName"||type==L"TDbText")&&!entity->isAProxy()&&floor.has_value();
  if(valid)named.insert(panel);
  if(floor)job.drawingChecks.record(panel,"native-plan-title-font",title.handle,{"P01"},
   bcpproduct::PlanReleaseGate::mainPlan,valid&&font?bcpproduct::PlanCheck::verified:bcpproduct::PlanCheck::failed,
   u8"楼层图名正文及其实际宋体字体",valid&&font?"":u8"重开后楼层图名正文或实际宋体字体未通过核验");
  productPlanAuditRow(job,{"P01"},panel,title.handle,"native-plan-title",
   valid&&font?"verified-existing-native-title-font":"pending-title-font-or-floor",
   valid&&font?u8"已有楼层图名在成果重开，实际引用 -宋体；图框和正式排版另外核验":u8"楼层文字或实际宋体样式未证实",
   "\"title\":"+jsonUtf8(title.text));
 }
 for(std::size_t i=0;i<views.size();++i)if(generated[i]){
  std::vector<double> widths;
  for(const auto& wall:input.walls){
   const auto at=wall.wall.start;
   if(bcpproduct::nearestGenericView(views,{at.x,at.y})==i)widths.push_back(wall.total*job.unifiedUnitMM);
  }
  const bool unitMatches=units==AcDb::kUnitsMillimeters&&job.unifiedUnitMM==1.;
  std::ostringstream parameters;
  parameters<<"\"insunits\":"<<static_cast<int>(units)<<",\"unitMM\":"<<job.unifiedUnitMM
            <<",\"scale\":"<<job.unifiedScale<<",\"actualWallThicknessesMM\":[";
  std::sort(widths.begin(),widths.end());
  widths.erase(std::unique(widths.begin(),widths.end()),widths.end());
  for(std::size_t j=0;j<widths.size();++j){if(j)parameters<<',';parameters<<widths[j];}
  parameters<<']';
  productPlanAuditRow(job,{"G03","Y03"},i+1,"","plan-units-and-scale",
   unitMatches?"verified-configured-model-unit":"pending-source-unit-reconciliation",
   unitMatches?u8"重开图单位与作业毫米设置一致，实墙厚已记录":u8"源图单位缺明确毫米声明或与作业参数不同，列实墙厚和作业比例供复核",
   parameters.str());
  productPlanAuditRow(job,{"P02","T07"},i+1,"","generated-view-identity",
   named.count(i+1)?"verified-explicit-floor-panel":"pending-explicit-floor-panel",
   named.count(i+1)?u8"明确楼层图名与本源墙体聚类对应":u8"已从实际墙体分幅；明确楼层身份或单独局部图范围待核");
  productPlanAuditRow(job,{"P03","Y05"},i+1,"","formal-plot-setup","pending-formal-plot-setup",
   u8"现有 DWG 生成及重开已完成；正式图签字段、适用图幅、可绘范围和 CTB／STB 需按本项目核定");
 }
 std::size_t sourceConstructionNotes=0;
 for(const auto& note:input.notes){
  const auto panel=bcpproduct::nearestGenericView(views,{note.anchor.x,note.anchor.y})+1;
  if(!generated[panel-1])continue;
  if(note.text.find(L"厨房")!=std::wstring::npos||note.text.find(L"卫生间")!=std::wstring::npos||
     note.text==L"公卫"||note.text==L"主卫"||note.text.find(L"管井")!=std::wstring::npos){
   productPlanAuditRow(job,{"D21"},panel,note.handle,"facility-dimension-source-location","pending-facility-measurement-stations",
    u8"已定位源空间；逐件核设施中心、台面及管井边和定位基准，避免恢复公卫内部隔墙或隔门尺寸",
    "\"position\":"+wholePoints({note.anchor})+",\"sourceName\":"+jsonUtf8(note.text));
  }
  if(note.text==L"厨房"||note.text==L"公卫"||note.text==L"卫生间"||note.text==L"庭院"||note.text==L"门廊")
   productPlanAuditRow(job,{"B05"},panel,note.handle,"slope-drainage-input","pending-confirmed-slope-input",
    u8"有相关空间；需核本处坡率、排向及终点后出坡向", "\"position\":"+wholePoints({note.anchor}));
  if(note.text.find(L"管井")!=std::wstring::npos||note.text.find(L"空调")!=std::wstring::npos||note.text.find(L"电梯")!=std::wstring::npos)
   productPlanAuditRow(job,{"B11"},panel,note.handle,"service-location-input","pending-service-design-input",
    u8"实有部位文字；位置、尺寸、管线及专业条件按对应资料核定", "\"position\":"+wholePoints({note.anchor}));
  if(note.text.find(L"说明")==0||note.text.find(L"做法")==0||note.text.find(L"大门高度")!=std::wstring::npos||
     note.text.find(L"参见")!=std::wstring::npos||note.text.find(L"详见")!=std::wstring::npos){
   ++sourceConstructionNotes;
   AcDbObjectId id;
   const auto handle=bcpproduct::decodeMessage(note.handle);
   bool preserved=saved.getAcDbObjectId(id,false,AcDbHandle(handle.c_str()))==Acad::eOk&&!id.isErased();
   if(preserved){
    const auto entity=open<AcDbEntity>(id);
    preserved=productPlanNameContents(*entity)==note.text&&productPlanDimensionVisible(*entity);
   }
   productPlanAuditRow(job,{"B01"},panel,note.handle,"source-construction-note",
    preserved?"verified-source-note-retained":"pending-source-note-retention",
    u8"对照源说明和重开文字；构造参数按本项目确定值", "\"position\":"+wholePoints({note.anchor})+
     ",\"sourceText\":"+jsonUtf8(note.text));
  }
  if(note.text==L"厨房"||note.text==L"公卫"||note.text==L"卫生间"||note.text==L"庭院"||note.text==L"门廊"||
     note.text.find(L"管井")!=std::wstring::npos||note.text.find(L"空调")!=std::wstring::npos)
   productPlanAuditRow(job,{"B07","B08"},panel,note.handle,"detail-index-candidate","pending-detail-library-association",
    u8"已定位详图索引候选部位；需要项目详图库、图号、适用构造及代表位置才能建立索引",
    "\"position\":"+wholePoints({note.anchor}));
 }
 if(!sourceConstructionNotes)productPlanAuditRow(job,{"B01"},0,"","readable-source-construction-note-inventory",
  "not-applicable-no-recognized-source-construction-note",u8"未检出符合说明、做法或详图引用语义的可读源说明；原文字保留，确认模板后可接用项目说明");
 for(const auto& opening:input.openings)if(opening.window||opening.grouped){
  const auto panel=bcpproduct::nearestGenericView(views,{opening.a.x,opening.a.y})+1;
  if(!generated[panel-1])continue;
  std::ostringstream measured;
  measured<<"\"widthMM\":"<<opening.width*job.unifiedUnitMM<<",\"sillMM\":"<<opening.sill*job.unifiedUnitMM
          <<",\"position\":"<<wholePoints({opening.a,opening.b});
  productPlanAuditRow(job,{"C12","C13","C14","C15","C16","C17","C18"},panel,opening.handle,
   "sill-detail-input","pending-confirmed-sill-detail",u8"已登记实际窗洞端点、宽和离地；窗台内外边、饰面厚度、伸出、排水及节点需本项目确认",measured.str());
  if(opening.grouped)productPlanAuditRow(job,{"C09","C10"},panel,opening.handle,"combined-opening-threshold",
   "pending-combined-opening-construction",u8"已定位原生组合洞口；门槛限通行门段，需分别核门槛顶、窗台顶及节点交接",measured.str());
 }
 for(const auto& [wide,type]:modelTypes(saved)){
  if(type!=L"TDbDimension2"&&type!=L"TDbAxisLabelSet"&&type!=L"TDbSymbElevation")continue;
  const auto handle=ascii(wide);
  const auto entity=open<AcDbEntity>(find(saved,handle));
  if(!productPlanDimensionVisible(*entity))continue;
  const auto grips=chainGrips(entity->objectId());
  if(grips.empty())continue;
  const auto panel=bcpproduct::nearestGenericView(views,{grips.front().x,grips.front().y})+1;
  const bool byLayer=entity->color().isByLayer();
  productPlanAuditRow(job,{"Y02"},panel,handle,"model-space-annotation-color",
   byLayer?"verified-root-annotation-by-layer":"pending-annotation-color-override",
   byLayer?u8"已读回标注根对象随层颜色；天正内部显示仍按各专项检查":u8"标注根对象有显式颜色覆盖，需核本图对象的参考依据",
   "\"layer\":"+jsonUtf8(symbolName(entity->layerId()))+",\"nativeType\":"+jsonUtf8(type));
 }
 for(const auto* rule:{"B06","P08","P09","P10"}){
  const auto detail=std::string(rule)=="B06"?u8"该条为剖切方向和剖面编号，本作业只运行平面入口":u8"该条限定安装包或仓库发布，本作业输出为 DWG";
  productPlanAuditRow(job,{rule},0,"","entry-mode-scope","not-applicable-to-plan-generate",detail);
 }
 for(const auto* rule:{"A06","O08","S07","X02","X03","X04","QA08"})
  productPlanAuditRow(job,{rule},0,"","historical-project-rule","not-applicable-to-plan-generate",
   u8"该条限定既往具名项目或历史版本；本次通用整图按当前源几何、资源及通用条文运行");
 productPlanAuditRow(job,{"G07"},0,"","legacy-entry-scope","not-applicable-to-plan-generate",
  u8"本次命令是 PLAN_DRAW；另有旧产品的 BCP／BCP_BUILD 入口");
}

void productAuditPlanEntranceDetails(ProductJob& job,const std::vector<ProductEntranceReceipt>& entrances){
 if(!job.unifiedPlan)return;
 for(const auto& row:entrances){
  if(row.status=="no-step")continue;
  std::ostringstream measured;
  measured<<"\"position\":"<<wholePoints({row.anchor})<<",\"upperM\":";
  if(row.upper)measured<<*row.upper;else measured<<"null";
  measured<<",\"lowerM\":";
  if(row.lower)measured<<*row.lower;else measured<<"null";
  productPlanAuditRow(job,{"C05","C06","C07"},1,row.door,"threshold-design-input","pending-threshold-node",
   u8"已有实际入口定位；室内与门廊完成面、门槛顶和门槛节点需分别确认，再从实体量洞口两端及进深",
   measured.str());
 }
}

void productAuditDeferredProjectItems(ProductJob& job,const GenericNativeInput& input,
 const std::vector<bcpproduct::GenericView>& views){
 if(!job.unifiedPlan)return;
 for(const auto& opening:input.openings)if(opening.window){
  const auto panel=bcpproduct::nearestGenericView(views,{opening.a.x,opening.a.y})+1;
  productPlanAuditRow(job,{"C19"},panel,opening.handle,"deferred-sill-finish-interface",
   "pending-sill-finish-design",u8"该窗两端的收头、返包、外饰面完成面及节点尚无已核项目依据；保留源窗位",
   "\"position\":"+wholePoints({opening.a,opening.b}));
 }
 productPlanAuditRow(job,{"X05"},0,"","formal-project-deliverable-boundary","pending-formal-project-deliverables",
  u8"散水和明沟暂停；详图库、CTB 和正式图框待补。图面净空与构造待项按对象保留");
 productPlanAuditRow(job,{"X01"},0,"","input-drawing-selection","verified-selected-source-only",
  u8"本次所有楼层使用作业指定源文件中的实际构件；未把上一轮生成图幅或旧项目常数作为输入",
  "\"sourceSHA256\":\""+job.sourceHash+"\"");
}
