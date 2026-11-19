struct ProductLooseDimensionStroke {
 std::string path;
 AcGePoint3d start,end;
 bool standalone{};
};

std::string productAnnotationParent(const std::string& path){
 const auto slash=path.rfind('/');
 return slash==std::string::npos?std::string{}:path.substr(0,slash);
}

struct ProductLooseDimensionValue {double measuredMM,toleranceMM;};

std::optional<ProductLooseDimensionValue> productLooseDimensionValue(const ProductSourceAnnotation& annotation){
 if(annotation.kind!="text"||annotation.glyphs.empty())return {};
 std::wstring label;
 for(const auto& glyph:annotation.glyphs)label+=glyph.text;
 const auto first=label.find_first_not_of(L" \t\u3000");
 if(first==std::wstring::npos)return {};
 label=label.substr(first,label.find_last_not_of(L" \t\u3000")-first+1);
 for(auto& c:label){
  if(c>=L'０'&&c<=L'９')c=L'0'+(c-L'０');
  if(c==L'．')c=L'.';
 }
 if(label.size()>=2&&_wcsicmp(label.c_str()+label.size()-2,L"mm")==0){
  label.resize(label.size()-2);
  while(!label.empty()&&label.back()==L' ')label.pop_back();
 }
 if(label.empty()||label.front()<L'0'||label.front()>L'9'||label.back()==L'.'||
    label.find_first_not_of(L"0123456789.")!=std::wstring::npos||
    std::count(label.begin(),label.end(),L'.')>1)return {};
 const auto value=std::wcstod(label.c_str(),nullptr);
 if(value<=0||!std::isfinite(value))return {};
 const auto point=label.find(L'.');
 const auto decimals=point==std::wstring::npos?0:label.size()-point-1;
 return ProductLooseDimensionValue{value,.01+.5*std::pow(10.,-static_cast<double>(decimals))};
}

bool productLooseDimensionLayer(const std::wstring& layer){
 return _wcsicmp(layer.c_str(),L"DIM")==0||layer.rfind(L"PUB_DIM",0)==0||
  layer.find(L"尺寸")!=std::wstring::npos;
}

std::map<std::string,ProductJob::LooseDimension> productRecognizeLooseDimensions(
 const std::vector<ProductSourceAnnotation>& annotations,const std::vector<ProductLooseDimensionStroke>& strokes,
 const std::vector<ProductPerimeterSourceWitness>& anchors,double paper,double unit){
 using Observation=ProductJob::LooseDimension;
 std::map<std::string,Observation> result;
 std::map<std::string,std::vector<const ProductLooseDimensionStroke*>> byParent;
 for(const auto& stroke:strokes)byParent[productAnnotationParent(stroke.path)].push_back(&stroke);
 const double snap=std::max(.02/unit,paper*.08);
 for(const auto& annotation:annotations){
  const auto value=productLooseDimensionValue(annotation);
  if(!value||!annotation.visible)continue;
  const auto parent=productAnnotationParent(annotation.path);
  const bool layerHint=productLooseDimensionLayer(annotation.layer);
  Observation row;
  row.value=value->measuredMM;row.valueToleranceMM=value->toleranceMM;row.dimensionLayerHint=layerHint;
  const auto peers=byParent.find(parent);
  if(annotation.position&&annotation.textDirection&&peers!=byParent.end()){
   const auto& direction=*annotation.textDirection;
   const auto& position=*annotation.position;
   const bool vertical=std::abs(direction.y)>std::abs(direction.x);
   const auto along=[vertical](const auto& p){return vertical?p.y:p.x;};
   const auto normal=[vertical](const auto& p){return vertical?p.x:p.y;};
   if(std::abs(normal(direction))<.04){
    const auto& nearby=peers->second;
    const double font=std::max(paper*2.,annotation.glyphs.front().height);
    struct Witness {
     const ProductLooseDimensionStroke* line;
     AcGePoint3d tip;
     std::optional<ProductPerimeterSourceWitness> support;
    };
    std::vector<Observation> candidates;
   for(const auto* base:nearby){
     const auto& [a,b]=std::minmax(base->start,base->end,[&](const auto& p,const auto& q){return along(p)<along(q);});
     const double axis=normal(a);
     if(std::abs(normal(a)-normal(b))>snap||along(b)-along(a)<=2*snap||
        std::abs(position.z-a.z)>snap||std::abs(position.z-b.z)>snap||
        std::abs(normal(position)-axis)>font*2.2)continue;
     const ProductLooseDimensionStroke* continuation=nullptr;
     double high=along(b);
     if(along(position)>along(b)+snap&&along(position)-along(b)<=font*3){
      for(const auto* other:nearby){
       if(other==base)continue;
       const auto first=along(other->start),second=along(other->end);
       const auto lo=std::min(first,second),hi=std::max(first,second);
       if(lo<along(position)-snap||hi<=lo+snap||lo-high>font*6||
          std::abs(normal(other->start)-axis)>snap||std::abs(normal(other->end)-axis)>snap||
          std::abs(other->start.z-a.z)>snap||std::abs(other->end.z-a.z)>snap)continue;
       if(continuation){continuation=nullptr;break;}
       continuation=other;
      }
      if(!continuation)continue;
      high=std::max(along(continuation->start),along(continuation->end));
     }
     if(along(position)<along(a)-snap||along(position)>high+snap)continue;
     std::vector<Witness> witnesses;
     for(const auto* line:nearby){
      if(line==base||line==continuation)continue;
      const auto t=along(line->start);
      const auto first=normal(line->start),second=normal(line->end);
      const auto lo=std::min(first,second),hi=std::max(first,second);
      // Extension lines can have a short tail across the baseline. A long
      // crossing in both directions is typically unrelated architectural ink.
      if(t<along(a)-snap||t>high+snap||std::abs(t-along(line->end))>snap||
         std::abs(line->start.z-a.z)>snap||std::abs(line->end.z-a.z)>snap||
         axis<lo-snap||axis>hi+snap||hi-lo<font*.8||
         std::min(std::abs(lo-axis),std::abs(hi-axis))>paper*3.)continue;
      const auto& tip=std::abs(hi-axis)>std::abs(lo-axis)?
        (normal(line->start)>normal(line->end)?line->start:line->end):
        (normal(line->start)<normal(line->end)?line->start:line->end);
      std::optional<ProductPerimeterSourceWitness> support;
      double closest=6*paper;
      for(const auto& witness:anchors){
       const auto gap=std::abs(normal(witness.point)-normal(tip));
       if(gap>closest||std::abs(along(witness.point)-t)>.02/unit||
          std::abs(witness.point.z-tip.z)>snap||
          (normal(witness.point)-axis)*(normal(tip)-axis)<=0)continue;
       if(gap<closest){closest=gap;support=witness;}
      }
      witnesses.push_back({line,tip,support});
     }
     for(const auto& left:witnesses)for(const auto& right:witnesses){
      const double lo=along(left.tip),hi=along(right.tip);
      if(hi-lo<=2*snap||along(position)<lo-snap||along(position)>hi+snap||
         (normal(left.tip)-axis)*(normal(right.tip)-axis)<=0||
         std::any_of(witnesses.begin(),witnesses.end(),[&](const auto& interior){
          const auto t=along(interior.tip);return t>lo+snap&&t<hi-snap;
         }))continue;
      const bool agrees=std::abs((hi-lo)*unit-value->measuredMM)<=value->toleranceMM;
      const bool supported=left.support&&right.support;
      const bool closedStrokePair=std::abs(lo-along(a))<=snap&&std::abs(hi-high)<=snap&&
       std::abs(normal(left.tip)-normal(right.tip))<=paper&&
       std::abs((hi-lo)*unit-value->measuredMM)<=value->measuredMM*.05;
      if(!agrees&&((!supported&&!closedStrokePair)||std::abs(along(position)-(lo+hi)/2)>font*.65))continue;
      Observation hit;
      hit.baseline=axis;hit.vertical=vertical;hit.value=value->measuredMM;
      hit.valueToleranceMM=value->toleranceMM;hit.dimensionLayerHint=layerHint;
      hit.strokes={base->path,left.line->path,right.line->path};
      for(const auto* piece:{base,left.line,right.line,continuation})if(piece)
       hit.strokeGeometry.emplace_back(piece->start,piece->end);
      if(continuation)hit.strokes.push_back(continuation->path);
      hit.stations={left.support?left.support->point:left.tip,right.support?right.support->point:right.tip};
      if(left.support)hit.anchors.push_back(left.support->handle);
      if(right.support)hit.anchors.push_back(right.support->handle);
      // End ticks and shared continuous baselines may be independent CAD
      // entities. Only retire a complete isolated stroke group.
      const auto touches=[&](const AcGePoint3d& point,double station){
       return std::abs(along(point)-station)<paper*1.5&&std::abs(normal(point)-axis)<paper*1.5&&
         std::abs(point.z-a.z)<=snap;
      };
      const bool otherInk=std::any_of(nearby.begin(),nearby.end(),[&](const auto* stroke){
       if(std::find(hit.strokes.begin(),hit.strokes.end(),stroke->path)!=hit.strokes.end())return false;
       return touches(stroke->start,lo)||touches(stroke->end,lo)||
        touches(stroke->start,hi)||touches(stroke->end,hi);
      });
      hit.standalone=agrees&&supported&&parent.empty()&&!annotation.external&&!otherInk&&
        base->standalone&&left.line->standalone&&right.line->standalone&&
        (!continuation||continuation->standalone)&&std::abs(lo-along(a))<font&&std::abs(hi-high)<font;
      hit.reason=!agrees?(supported?u8"尺寸线和两条界线已识别，原数值与实测构件测站不符":
       u8"尺寸线和两条界线已识别，原数值与界线间距不符，尚缺两个可证的构件测点"):
       !supported?u8"尺寸线和界线与原数值一致，尚缺两个可证的构件测点":
       !hit.standalone?u8"构件测点和测值已核，旧线含块成员、相邻笔画或共用线，需按实例核退出现状":
       u8"尺寸线、两条界线、数值和两个源构件测点一致";
      candidates.push_back(std::move(hit));
     }
    }
    if(candidates.size()==1)row=std::move(candidates.front());
    else if(!candidates.empty())row.reason=u8"数值附近有多组可能的尺寸线或界线，须按实际测站消除歧义";
   }
  }
  if(row.reason.empty()&&layerHint)row.reason=u8"尺寸图层上的数值缺可靠的尺寸线或两端界线";
  if(!row.reason.empty())result.emplace(annotation.path,std::move(row));
 }
 return result;
}

bool productLooseDimensionMatches(AcDbDatabase& db,const std::string& handle,
 const ProductJob::LooseDimension& row,double unit){
 const auto actual=productReadSourceDimension(db,handle,.01/unit);
 if(!actual.readable||!actual.automaticText||row.stations.size()!=2)return false;
 const auto close=[=](const auto& a,const auto& b){return a.distanceTo(b)*unit<=.02;};
 for(std::size_t i=0;i<actual.values.size();++i){
  if(std::abs(actual.values[i]*unit-row.value)>row.valueToleranceMM)continue;
  const auto& a=actual.points[i];const auto& b=actual.points[i+1];
  if((close(a,row.stations[0])&&close(b,row.stations[1]))||
     (close(b,row.stations[0])&&close(a,row.stations[1])))return true;
 }
 return false;
}

std::optional<AcDbObjectId> productLooseDimensionEntity(AcDbDatabase& db,const std::string& handle){
 AcDbObjectId id;
 if(db.getAcDbObjectId(id,false,AcDbHandle(bcpproduct::decodeMessage(handle).c_str()))!=Acad::eOk||id.isErased())return {};
 return id;
}

bool productVerifyLooseDimensionTarget(ProductJob& job,AcDbDatabase& db,
 const ProductJob::LooseDimension& row,AcDbDatabase& reference){
 const auto id=productLooseDimensionEntity(db,row.output);
 if(!id)return false;
 auto entity=open<AcDbEntity>(*id);
 if(std::wstring(entity->isA()->name())!=L"TDbDimension2"||!productPlanDimensionVisible(*entity)||
    symbolName(entity->layerId())!=L"PUB_DIM")return false;
 const auto styles=productDimensionTexts(*entity);
 entity.reset();
 return !styles.empty()&&std::all_of(styles.begin(),styles.end(),[&](const auto& part){
  return productNativeDimensionStyleMatches(part,reference,job.dimensionStylePolicy,job.unifiedScale,job.unifiedUnitMM)&&
   productDimensionEqual(part.measurementFactor,job.unifiedUnitMM);
 })&&productLooseDimensionMatches(db,row.output,row,job.unifiedUnitMM);
}

void productResolveLooseDimensions(ProductJob& job,AcDbDatabase& db,AcDbDatabase& reference,
 const std::vector<bcpproduct::GenericView>& views,const std::vector<bool>& active,
 std::vector<std::pair<AcDbObjectId,bool>>& interior){
 if((!job.architecturalPlanOnly&&!job.currentPlanRules)||job.planLooseDimensions.empty())return;
 std::map<std::string,std::size_t> uses;
 for(const auto& [path,row]:job.planLooseDimensions)for(const auto& stroke:row.strokes)++uses[stroke];
 std::vector<std::string> native;
 for(const auto& [wide,type]:modelTypes(db))
  if(type==L"TDbDimension2"&&!job.planDimensionExclusions.count(ascii(wide)))native.push_back(ascii(wide));
 const auto unit=job.unifiedUnitMM;
 for(const auto& annotation:job.planSourceAnnotations){
  auto match=job.planLooseDimensions.find(annotation.path);
  if(match==job.planLooseDimensions.end())continue;
  auto& row=match->second;
  row.inSelectedPanel=annotation.panel>0&&annotation.panel<=active.size()&&active[annotation.panel-1];
  if(!row.inSelectedPanel||!row.standalone)continue;
  const auto label=productLooseDimensionEntity(db,annotation.path);
  if(!label)continue;
  const auto canRetire=[&](AcDbObjectId id){
   const auto entity=open<AcDbEntity>(id);
   return entity->ownerId()==modelId(db)&&productPlanDimensionVisible(*entity)&&
    entity->extensionDictionary().isNull()&&!productSourceNativeDimensionProtected(*entity);
  };
  std::vector<AcDbObjectId> old{*label};
  for(std::size_t i=0;i<row.strokes.size();++i){
   const auto id=productLooseDimensionEntity(db,row.strokes[i]);
   if(!id||uses[row.strokes[i]]!=1)break;
   const auto entity=open<AcDbEntity>(*id);
   const auto* line=AcDbLine::cast(entity.get());
   const auto& [start,end]=row.strokeGeometry[i];
   if(!line||line->startPoint().distanceTo(start)*unit>.01||line->endPoint().distanceTo(end)*unit>.01)break;
   old.push_back(*id);
  }
  if(old.size()!=row.strokes.size()+1||!std::all_of(old.begin(),old.end(),canRetire)){
   row.reason=u8"源线共用、受保护或已改动，保留原尺寸表达供重开对账";
   continue;
  }
  if(job.currentPlanRules&&job.interiorLooseLabels.count(annotation.path)){
   for(auto id:old){
    productStatus(editObject<AcDbEntity>(id)->erase(),"remove confirmed interior dimension stroke");
    job.planOmissions.emplace(axisHandle(id),"D23");
   }
   row.reason=u8"测点对应室内构件，已按 D23 清除整组独立线文。";
   continue;
  }
  const auto retire=[&]{
   std::vector<AcDbObjectId> hidden;
   try{
    for(const auto id:old){
     productStatus(editObject<AcDbEntity>(id)->setVisibility(AcDb::kInvisible),"retire identified source dimension");
     hidden.push_back(id);
    }
    return true;
   }catch(const std::exception& error){
    for(const auto id:hidden)productStatus(editObject<AcDbEntity>(id)->setVisibility(AcDb::kVisible),"restore legacy dimension stroke");
    row.reason=error.what();
    return false;
   }
  };
  std::vector<std::string> equivalents;
  for(const auto& handle:native){
   const auto id=productLooseDimensionEntity(db,handle);
   if(id&&productPlanDimensionVisible(*open<AcDbEntity>(*id))&&productLooseDimensionMatches(db,handle,row,unit))
    equivalents.push_back(handle);
  }
  if(equivalents.size()>1){
   row.reason=u8"有多条原生尺寸对应两端测站，须先排除重复链";
   retire();
   continue;
  }
  std::optional<AcDbObjectId> created;
  if(equivalents.size()==1)row.output=equivalents.front();
  else{
   const auto& bounds=views[annotation.panel-1].bounds;
   const auto& first=row.stations[0];const auto& last=row.stations[1];
   if(job.currentPlanRules){row.reason=u8"须按实际外围或室外细部测点核对原表达。";continue;}
   const auto step=last-first;
   const auto middle=first+step*.5;
   const AcGePoint3d position(row.vertical?row.baseline:middle.x,row.vertical?middle.y:row.baseline,middle.z);
   if(position.x<bounds.xmin||position.x>bounds.xmax||position.y<bounds.ymin||position.y>bounds.ymax||
      std::abs(row.vertical?step.x:step.y)*unit>.02){
    row.reason=u8"旧尺寸测点已核，须按外围道次或原投影方向对应原生链";
    retire();
    continue;
   }
   const auto prototype=cloneOne(reference,find(reference,"19A0"),db);
   const auto offset=row.vertical?first.x-row.baseline:row.baseline-first.y;
   created=productTryPlanChain(job,db,prototype,row.stations,offset,annotation.panel,annotation.path,
     u8"旧线文尺寸原生化","TL09");
   productStatus(editObject<AcDbEntity>(prototype)->erase(),"retire temporary legacy dimension prototype");
   if(created)row.output=axisHandle(*created);
  }
  if(row.output.empty()||!productVerifyLooseDimensionTarget(job,db,row,reference)){
   if(created)productStatus(editObject<AcDbEntity>(*created)->erase(),"discard unverified legacy dimension");
   row.output.clear();
   row.reason=u8"旧线文尺寸已确认，原生替换的测点、测值、样式或图层待处理";
   retire();
   continue;
  }
  if(!retire()){
   if(created)productStatus(editObject<AcDbEntity>(*created)->erase(),"discard incomplete legacy dimension replacement");
   row.output.clear();continue;
  }
  if(created)interior.emplace_back(*created,row.vertical);
  row.reason=u8"原数值与测点对应，旧文字及线条退出并转入原生尺寸复核";
 }
}
