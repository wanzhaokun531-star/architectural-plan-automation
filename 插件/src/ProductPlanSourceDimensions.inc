ProductJob::SourceDimension productReadSourceDimension(AcDbDatabase& db,const std::string& handle,double eps){
 ProductJob::SourceDimension result;
 AcDbObjectId id;
 if(db.getAcDbObjectId(id,false,AcDbHandle(bcpproduct::decodeMessage(handle).c_str()))!=Acad::eOk||id.isErased())return result;
 const auto entity=open<AcDbEntity>(id);
 if(entity->isAProxy())return result;
 if(std::wstring(entity->isA()->name())==L"TDbDimension2"){
  AcGePoint3dArray grips;
  AcDbIntArray modes, ids;
  const bool gripsRead=entity->getGripPoints(grips,modes,ids)==Acad::eOk;
  TemplateParts parts(entity.get());
  if(entity->explode(parts.values)!=Acad::eOk||parts.values.isEmpty())return result;
  std::vector<std::pair<AcGePoint3d,AcGePoint3d>> segments;
  result.automaticText=true;
  for(int i=0;i<parts.values.length();++i){
   auto* segment=AcDbAlignedDimension::cast(static_cast<AcRxObject*>(parts.values[i]));
   double value{};
   AcString overrideText;
   if(!segment||segment->measurement(value)!=Acad::eOk||
      segment->dimensionText(overrideText)!=Acad::eOk)return result;
   if(i==0)result.baseline=segment->dimLinePoint();
   result.values.push_back(value);
   segments.emplace_back(segment->xLine1Point(),segment->xLine2Point());
   if(!overrideText.isEmpty()&&overrideText!=L"<>"){
    result.displayOverride+=overrideText.kACharPtr();
    result.displayOverride+=L"\n";
   }
  }
  result.automaticText&=result.displayOverride.empty();
  if(gripsRead&&(grips.length()==2*result.values.size()+6||grips.length()==2*result.values.size()+3)){
   for(std::size_t i=0;i<=result.values.size();++i)result.points.push_back(grips[static_cast<int>(i+2)]);
   result.baseline=grips[0];
  }else{
   // Alternate native grip layouts still expose the complete aligned subdimensions.
   // Require an uninterrupted chain in the public segment order, preserving measurement order.
   for(const bool reverseFirst:{false,true}){
    const auto& first=segments.front();
    std::vector<AcGePoint3d> points{reverseFirst?first.second:first.first,reverseFirst?first.first:first.second};
    for(std::size_t i=1;i<segments.size();++i){
     const auto& [a,b]=segments[i];
     const bool atFirst=points.back().distanceTo(a)<=eps;
     const bool atSecond=points.back().distanceTo(b)<=eps;
     if(atFirst==atSecond)break;
     points.push_back(atFirst?b:a);
    }
    if(points.size()!=segments.size()+1)continue;
    result.points=std::move(points);
    result.pointsFromSegments=true;
    break;
   }
   if(result.points.empty())return result;
  }
 }else{
  auto* dimension=AcDbDimension::cast(entity.get());
  if(!dimension)return result;
  if(const auto* aligned=AcDbAlignedDimension::cast(entity.get())){
   result.points={aligned->xLine1Point(),aligned->xLine2Point()};
   result.baseline=aligned->dimLinePoint();
  }else if(const auto* rotated=AcDbRotatedDimension::cast(entity.get())){
   result.points={rotated->xLine1Point(),rotated->xLine2Point()};
   result.baseline=rotated->dimLinePoint();
  }else return result;
  double value{};
  AcString overrideText;
  if(dimension->measurement(value)!=Acad::eOk||dimension->dimensionText(overrideText)!=Acad::eOk)return result;
  result.values={value};
  result.displayOverride=overrideText.kACharPtr();
  result.automaticText=overrideText.isEmpty()||overrideText==L"<>";
 }
 const auto finite=[](const auto& p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);};
 result.readable=finite(result.baseline)&&std::all_of(result.points.begin(),result.points.end(),finite)&&
  std::all_of(result.values.begin(),result.values.end(),[](double value){return std::isfinite(value);});
 return result;
}

bool productSameDimensionGeometry(const ProductJob::SourceDimension& source,
 const ProductJob::SourceDimension& target,double eps,bool reverse){
 if(!source.readable||!target.readable||source.points.size()!=target.points.size()||
    source.values.size()!=target.values.size())return false;
 for(std::size_t i=0;i<source.points.size();++i)
  if(source.points[i].distanceTo(target.points[reverse?target.points.size()-1-i:i])>eps)return false;
 for(std::size_t i=0;i<source.values.size();++i)
  if(std::abs(source.values[i]-target.values[reverse?target.values.size()-1-i:i])>eps)return false;
 return true;
}

bool productEquivalentSourceDimension(const ProductJob::SourceDimension& source,
 const ProductJob::SourceDimension& target,double eps){
 return source.automaticText&&target.automaticText&&
  (productSameDimensionGeometry(source,target,eps,false)||productSameDimensionGeometry(source,target,eps,true));
}

bool productSourceNativeDimensionProtected(const AcDbEntity& entity){
 const auto layer=open<AcDbLayerTableRecord>(entity.layerId());
 if(layer->isLocked()||!layer->isPlottable()||entity.hasFields())return true;
 const auto* reactors=entity.reactors();
 if(reactors)for(int i=0;i<reactors->length();++i)if(acdbIsPersistentReactor(reactors->at(i)))return true;
 return false;
}

bool productNormalizeSourceNativeDimensionStyles(ProductJob& job,AcDbDatabase& db,AcDbDatabase& reference,
 const std::vector<bcpproduct::GenericView>& views,const std::vector<bool>& active,double scale,double unit){
 if(!job.architecturalPlanOnly)return false;
 const double eps=.01/unit;
 const auto& policy=job.dimensionStylePolicy;
 AcDbObjectId prototype;
 bool usablePrototype=false;
 bool replaced=false;
 for(const auto& [key,item]:job.sourceInventory.items){
  const auto& [panel,scope,handle]=key;
  if(scope!="source-dimension"||!panel||panel>active.size()||!active[panel-1]||
     job.planDimensionExclusions.count(handle))continue;
  const auto perimeter=job.oldPerimeterDimensions.find(handle);
  if(perimeter!=job.oldPerimeterDimensions.end()&&!perimeter->second.replacement.empty())continue;
  AcDbObjectId sourceId;
  if(db.getAcDbObjectId(sourceId,false,AcDbHandle(bcpproduct::decodeMessage(handle).c_str()))!=Acad::eOk||sourceId.isErased())continue;
  auto entity=open<AcDbEntity>(sourceId);
  if(entity->isAProxy()||std::wstring(entity->isA()->name())!=L"TDbDimension2"||
     !productPlanDimensionVisible(*entity)||entity->ownerId()!=modelId(db))continue;
  const auto pending=[&](const char* reason){job.planDimensionNormalizationPending[handle]=reason;};
  const auto styleMatches=[&](const auto& style){return productNativeDimensionStyleMatches(style,reference,policy,scale,unit);};
  std::vector<ProductDimensionText> styles;
  ProductJob::SourceDimension source;
  try{
   styles=productDimensionTexts(*entity);
   source=productReadSourceDimension(db,handle,eps);
   if(productSourceNativeDimensionProtected(*entity)){
    pending(u8"旧原生尺寸有锁定或非打印图层、字段或持久关联，需核原关联后改样式");
    continue;
   }
  }catch(const std::exception&){
   pending(u8"旧原生尺寸的公开分段、尺寸样式或测点未完整读出");
   continue;
  }
  const auto& original=job.sourceDimensions.at(handle);
  if(!job.unifiedPanelScope.empty()&&source.readable){
   const double search=70*scale/unit;
   bool reachesSelectedFloor=false;
   for(std::size_t i=0;i<views.size();++i)if(active.at(i)){
    const auto& b=views[i].bounds;
    const auto inRange=[&](const auto& point){
     return point.x>=b.xmin-search&&point.x<=b.xmax+search&&point.y>=b.ymin-search&&point.y<=b.ymax+search;
    };
    reachesSelectedFloor|=inRange(source.baseline)||std::any_of(source.points.begin(),source.points.end(),inRange);
   }
   if(!reachesSelectedFloor)continue;
  }
  if(!source.readable||source.values.empty()||styles.size()!=source.values.size()){
   pending(u8"旧原生尺寸的公开分段、尺寸样式或测点未完整读出");
   continue;
  }
  if(!source.automaticText||!original.automaticText){
   pending(u8"旧原生尺寸包含自定义数字、前后缀或其他显示覆盖，需核文字及测值后换样式");
   continue;
  }
  if(!productEquivalentSourceDimension(original,source,eps)||source.points.size()!=source.values.size()+1){
   pending(u8"旧原生尺寸与原图记录的测点、测值或分段不一致，无法证实可等价重建");
   continue;
  }
  if(std::any_of(styles.begin(),styles.end(),[&](const auto& style){return !productDimensionEqual(style.measurementFactor,unit);})){
   pending(u8"旧原生尺寸的有效测量因子与当前图纸单位不同，需核原显示值");
   continue;
  }
  const auto span=source.points.back()-source.points.front();
  if(span.length()<=eps||std::abs(span.z)>eps){
   pending(u8"旧原生尺寸含非平面或重合测站，需核实际投影方向后换样式");
   continue;
  }
  const auto direction=span.normal();
  const AcGeVector3d normal(-direction.y,direction.x,0.);
  bool chain=true;
  for(std::size_t i=0;i<source.values.size()&&chain;++i){
   const auto delta=source.points[i+1]-source.points[i];
   const auto along=delta.dotProduct(direction);
   chain=along>eps&&std::abs(delta.z)<=eps&&std::abs(delta.dotProduct(normal))<=eps&&
         std::abs(along-source.values[i])<=eps;
  }
  if(!chain){
   pending(u8"旧原生尺寸为错位投影、倒序或非等价连续测站，需核原测量方式");
   continue;
  }
  const auto compliant=[&](const auto& row){
   return styleMatches(row)&&productDimensionEqual(row.measurementFactor,unit);
  };
  const auto incompatibleResource=u8"无法取得符合本次比例、字体及有效字高的原生 _TCH_ARCH 资源，需核目标图共享样式";
  if(prototype.isNull()){
   prototype=cloneOne(reference,find(reference,"19A0"),db);
   const auto verifyPrototype=[&]{
    const auto parts=productDimensionTexts(*open<AcDbEntity>(prototype));
    return parts.size()==1&&compliant(parts.front());
   };
   usablePrototype=verifyPrototype();
   if(!usablePrototype){
    // The donor may have just imported a ratio record with reference height.
    // Normalize it only through the shared-resource protections used for every plan chain.
    productApplyPlanDimensionStyle(job,db,scale,unit);
    usablePrototype=verifyPrototype();
   }
  }
  if(!usablePrototype){
   pending(incompatibleResource);
   continue;
  }
  const auto offset=(source.baseline-source.points.front()).dotProduct(normal);
  const auto replacement=productTryPlanChain(job,db,prototype,source.points,offset,panel,handle,u8"旧原生尺寸样式","Y04");
  if(!replacement){
   pending(u8"旧原生尺寸的原生样式重建未成功，保留原链和测值");
   continue;
  }
  const auto target=axisHandle(*replacement);
  const char* rejection=nullptr;
  try{
   const auto geometry=productReadSourceDimension(db,target,eps);
   const auto actual=productDimensionTexts(*open<AcDbEntity>(*replacement));
   if(!productEquivalentSourceDimension(source,geometry,eps)||
      std::abs((source.baseline-geometry.baseline).dotProduct(normal))>eps)
    rejection=u8"旧原生尺寸的试建链未保持原测点、测值或基线";
   else if(actual.size()!=source.values.size()||!std::all_of(actual.begin(),actual.end(),compliant))
    rejection=incompatibleResource;
  }catch(const std::exception&){
   rejection=u8"旧原生尺寸的试建链无法读回实际测点、测值或尺寸样式";
  }
  if(rejection){
   productStatus(editObject<AcDbEntity>(*replacement)->erase(),"remove unverified plan dimension style replacement");
   pending(rejection);
   continue;
  }
  if(symbolName(entity->layerId())==L"AXIS")
   productStatus(editObject<AcDbEntity>(*replacement)->setLayer(entity->layerId()),"retain valid native axis dimension layer");
  entity.reset();
  productStatus(editObject<AcDbEntity>(sourceId)->setVisibility(AcDb::kInvisible),"retire replaced source plan dimension style");
  job.planDimensionExclusions.insert(handle);
  job.planDimensionRatioReplacements.emplace(handle,target);
  replaced=true;
 }
 if(!prototype.isNull()){
  job.planDimensionMeasurements.erase(axisHandle(prototype));
  productStatus(editObject<AcDbEntity>(prototype)->erase(),"retire source plan dimension style prototype");
 }
 return replaced;
}

std::size_t productDimensionSourcePanel(const ProductJob::SourceDimension& dimension,
 const std::vector<bcpproduct::GenericView>& views,double eps){
 if(!dimension.readable)return 0;
 std::optional<std::size_t> match;
 double best=std::numeric_limits<double>::infinity();
 for(std::size_t i=0;i<views.size();++i){
  double distance=0;
  for(const auto& point:dimension.points)
   distance=std::max(distance,std::sqrt(bcpproduct::genericDistanceSquared(views[i].bounds,{point.x,point.y})));
  if(distance<best-eps){best=distance;match=i;}
  else if(std::abs(distance-best)<=eps)match.reset();
 }
 return match?*match+1:0;
}

void productStageOldPerimeterDimensions(ProductJob& job,AcDbDatabase& db,
 const ProductPlanLayoutConfig& layout,int panel,std::size_t component,const ProductPlanBounds* originalFacade=nullptr){
 const double eps=.01/layout.unitMM;
 const auto& bounds=layout.panels.at(panel);
 struct Candidate {std::string handle;int side;double normal;};
 std::vector<Candidate> candidates;
 for(const auto& [key,item]:job.sourceInventory.items){
  const auto& [owner,scope,handle]=key;
  if(owner!=component||scope!="source-dimension")continue;
  const auto& source=job.sourceDimensions.at(handle);
  if(!source.readable||!source.automaticText||source.points.size()!=source.values.size()+1||source.values.empty())continue;
  const auto id=find(db,handle);
  const auto entity=open<AcDbEntity>(id);
  if(!productPlanDimensionVisible(*entity)||entity->ownerId()!=modelId(db)||
     open<AcDbLayerTableRecord>(entity->layerId())->isLocked())continue;
  const auto delta=source.points.back()-source.points.front();
  const bool vertical=std::abs(delta.x)<=eps&&std::abs(delta.y)>eps;
  if(!vertical&&(std::abs(delta.y)>eps||std::abs(delta.x)<=eps))continue;
  const auto along=[&](const auto& p){return vertical?p.y:p.x;};
  const auto normal=[&](const auto& p){return vertical?p.x:p.y;};
  if(!std::all_of(source.points.begin(),source.points.end(),[&](const auto& p){
   return std::abs(normal(p)-normal(source.points.front()))<=eps;
  }))continue;
  const auto first=std::min(along(source.points.front()),along(source.points.back()));
  const auto last=std::max(along(source.points.front()),along(source.points.back()));
  // Allow for source chains ending at the wall face or centre at either end.
  const double wallAllowance=240./layout.unitMM;
  bool classified=false;
  for(const auto* facade:{&bounds,originalFacade}){
   if(!facade||classified)continue;
   const double low=vertical?facade->ymin:facade->xmin,high=vertical?facade->ymax:facade->xmax;
   if(first>low+wallAllowance||last<high-wallAllowance||last-first<(high-low)*.9)continue;
   for(int side=vertical?0:2;side<(vertical?2:4);++side){
   const int sign=side%2?1:-1;
   const auto datum=productPlanDatum(*facade,side);
   const auto baseline=sign*(normal(source.baseline)-datum);
   const auto witness=sign*(normal(source.points.front())-datum);
   if(baseline>eps&&witness>=-eps&&baseline>witness+eps&&baseline<=80*layout.scale/layout.unitMM){
    candidates.push_back({handle,side,normal(source.baseline)});
    classified=true;break;
   }
   }
  }
 }
 for(const auto& candidate:candidates){
  const auto peers=std::count_if(candidates.begin(),candidates.end(),[&](const auto& other){
   return other.side==candidate.side&&std::abs(other.normal-candidate.normal)>eps;
  });
  const auto& source=job.sourceDimensions.at(candidate.handle);
  // Use the whole facade span and its parallel rows to separate main chains
  // from a single local total or a short construction dimension.
  if(!peers&&source.values.size()==1)continue;
  const auto id=find(db,candidate.handle);
  productStatus(editObject<AcDbEntity>(id)->setVisibility(AcDb::kInvisible),"clear confirmed source perimeter chain before rebuilding");
  job.oldPerimeterDimensions.emplace(candidate.handle,ProductJob::OldPerimeterDimension{panel,candidate.side});
 }
}

void productFinishOldPerimeterDimensions(ProductJob& job,AcDbDatabase& db,const ProductPlanLayoutConfig& layout,int panel){
 for(int side=0;side<4;++side){
  const auto& expected=layout.expectedPerimeter.at({panel,side});
  std::array<std::string,3> chains;
  for(const auto& row:layout.dimensions)if(row.panel==panel&&row.side==side&&row.row>=1&&row.row<=3)
   chains[row.row-1]=row.handle;
  const bool complete=expected.axisScopeKnown&&!chains[0].empty()&&!chains[1].empty()&&
   ((expected.openingScopeKnown&&!chains[2].empty())||(!expected.openingFreeWalls.empty()&&chains[2].empty()));
  for(auto& [handle,old]:job.oldPerimeterDimensions){
   if(old.panel!=panel||old.side!=side)continue;
   const auto id=find(db,handle);
   auto entity=editObject<AcDbEntity>(id);
   if(!complete){
    productStatus(entity->setVisibility(AcDb::kVisible),"restore source perimeter chain on an incomplete side");
    continue;
   }
   productStatus(entity->erase(),"delete replaced source perimeter chain");
   for(const auto& chain:chains)if(!chain.empty())old.replacement.push_back(chain);
  }
 }
}

void productAuditSourceDimensionDisposition(ProductJob& job,AcDbDatabase& saved,const fs::path& candidate,double unit){
 if(!job.unifiedPlan)return;
 Working work(&saved);
 const double eps=.01/unit;
 std::map<std::string,std::string> styleIssues;
 for(const auto& [key,check]:job.drawingChecks.items){
  const auto& [_,scope,handle]=key;
  if(scope=="dimension-style"&&(check.state==bcpproduct::PlanCheck::failed||check.state==bcpproduct::PlanCheck::pending))
   styleIssues.emplace(handle,check.reason);
 }
 std::ostringstream report;
 report<<std::setprecision(17)<<"{\"savedReopened\":true,\"sourceSHA256\":\""<<job.sourceHash
       <<"\",\"outputSHA256\":\""<<sha(candidate)<<"\",\"dimensions\":[";
 bool first=true;
 for(const auto& [key,item]:job.sourceInventory.items){
  const auto& [panel,scope,source]=key;
  if(scope!="source-dimension")continue;
  const auto& old=job.sourceDimensions.at(source);
  const auto actual=productReadSourceDimension(saved,source,eps);
  AcDbObjectId id;
  const bool exists=saved.getAcDbObjectId(id,false,AcDbHandle(bcpproduct::decodeMessage(source).c_str()))==Acad::eOk&&!id.isErased();
  const bool shown=exists&&productPlanDimensionVisible(*open<AcDbEntity>(id));
  const bool same=productSameDimensionGeometry(old,actual,eps,false)&&
    old.displayOverride==actual.displayOverride&&old.automaticText==actual.automaticText;
  std::vector<std::string> targets=item.output;
  const auto styleReplacement=job.planDimensionRatioReplacements.find(source);
  if(styleReplacement!=job.planDimensionRatioReplacements.end())targets={styleReplacement->second};
  if(const auto strict=job.planRequiredDimensionReplacements.find(source);strict!=job.planRequiredDimensionReplacements.end())
   targets={strict->second};
  const auto perimeter=job.oldPerimeterDimensions.find(source);
  const bool rebuilt=perimeter!=job.oldPerimeterDimensions.end()&&!perimeter->second.replacement.empty();
  if(rebuilt)targets=perimeter->second.replacement;
  std::string disposition="pending";
  std::string reason=u8"原尺寸测点、测值或显示不可读，需人工定位核对";
  auto status=bcpproduct::PlanCheck::pending;
  if(!rebuilt&&styleReplacement==job.planDimensionRatioReplacements.end()&&shown&&same&&
     productOutsideSelectedPlanWindows(job,saved,source)){
   disposition="retained-outside-print-scope";
   status=bcpproduct::PlanCheck::omitted;
   targets={source};
   reason=u8"同一 DWG 重开确认旧尺寸仍保留，实际范围完全位于已确认打印图窗之外";
  }else if(rebuilt){
   if(!exists&&perimeter->second.verified){
    disposition="rebuilt-perimeter";
    status=bcpproduct::PlanCheck::verified;
    reason=u8"旧外围主链已删除；同侧现行各道按源墙柱轴、洞口及必要墙段重新取点，独立重开已核";
   }else reason=u8"旧外围链删除或同侧重建未通过独立重开核验";
  }else if(shown&&same&&!job.architecturalPlanOnly){
   disposition="retained";
   if(std::find(targets.begin(),targets.end(),source)==targets.end())targets.insert(targets.begin(),source);
   status=bcpproduct::PlanCheck::verified;
   reason=u8"源句柄可见，原定义点、测值和文字覆盖重开一致。闭合与排版见各专项";
  }else if(!shown&&!targets.empty()){
   const auto targetExists=[&](const std::string& handle){
    AcDbObjectId target;
    return saved.getAcDbObjectId(target,false,AcDbHandle(bcpproduct::decodeMessage(handle).c_str()))==Acad::eOk&&!target.isErased()&&
      productPlanDimensionVisible(*open<AcDbEntity>(target));
   };
   const bool equivalent=std::any_of(targets.begin(),targets.end(),[&](const auto& target){
    return targetExists(target)&&productEquivalentSourceDimension(old,productReadSourceDimension(saved,target,eps),eps);
   });
   // Specialty audits can establish a replacement using the original object and
   // independently read drawing stations, even when the definition points differ.
   const bool replacementVerified=item.check==bcpproduct::PlanCheck::verified&&
    std::all_of(targets.begin(),targets.end(),targetExists);
   if(equivalent||replacementVerified){
    disposition="replaced";
    status=bcpproduct::PlanCheck::verified;
    reason=equivalent?u8"源链退出，重开找到定义点和测值等价且可见的链":
       item.reason.empty()?u8"专项已独立重开核对源图与对应替换链":item.reason;
   }else reason=u8"原链不可见，现有替换关系或测点、测值尚未证实";
  }else if(old.readable)reason=shown?u8"原句柄可见，但定义点、测值或覆盖有变化":
                                   u8"原链不可见，未取得可核的替换或省标关系";
  for(const auto& target:targets){
   const auto issue=styleIssues.find(target);
   if(issue==styleIssues.end())continue;
   status=bcpproduct::PlanCheck::pending;
   reason=issue->second;
   break;
  }
  if(const auto issue=job.planRequiredDimensionIssues.find(source);issue!=job.planRequiredDimensionIssues.end()){
   status=bcpproduct::PlanCheck::pending;
   reason=issue->second;
  }
  if(styleReplacement!=job.planDimensionRatioReplacements.end()&&status==bcpproduct::PlanCheck::verified&&
     item.check==bcpproduct::PlanCheck::pending&&item.output==targets){
   status=item.check;
   reason=item.reason;
  }
  productCheckPlanSource(job,panel,"source-dimension-disposition",source,status,targets,reason);
  if(item.output.empty()||status!=bcpproduct::PlanCheck::verified||styleReplacement!=job.planDimensionRatioReplacements.end())
   productCheckPlanSource(job,panel,scope,source,status,targets,reason);
  std::ostringstream row;
  row<<std::setprecision(17)<<"{\"panel\":"<<panel<<",\"source\":\""<<source<<"\",\"disposition\":\""
     <<disposition<<"\",\"visibleOriginal\":"<<(shown?"true":"false")<<",\"originalExists\":"<<(exists?"true":"false")
     <<",\"sourceReadable\":"<<(old.readable?"true":"false")
     <<",\"sourcePointRead\":\""<<(old.pointsFromSegments?"native-segment-definition-points":old.readable?"public-definition-points":"pending")
     <<"\",\"sourcePoints\":"<<wholePoints(old.points)<<",\"sourceValues\":";
  productWriteStationArray(row,old.values);
  row<<",\"targets\":";
  productWriteHandleArray(row,targets);
  row<<",\"sourceBaseline\":"<<wholePoints({old.baseline})
     <<",\"reopenedOriginalPoints\":"<<wholePoints(actual.points)<<",\"reopenedOriginalValues\":";
  productWriteStationArray(row,actual.values);
  row<<",\"reason\":"<<jsonUtf8(bcpproduct::decodeMessage(reason))
     <<",\"savedReopened\":true,\"result\":\""<<productPlanCheckName(status)<<"\"}";
  if(!first)report<<',';
  first=false;
  report<<row.str();
  for(const auto* rule:{"D22","Q03","G06","Y06"})productRecordRuleEvidence(job,rule,row.str());
 }
 report<<"]}";
 writeNew(job.root/L"source-dimension-disposition.json",report.str());
}
