bool productPlanTextOnRequiredSide(const AcDbExtents& box,bool vertical,double baseline) {
 return vertical ? box.maxPoint().x < baseline : box.minPoint().y > baseline;
}

// The Tianzheng root draws from a model-scale DIMSTYLE record (for example
// _TCH_ARCH&&100). The exploded AcDbDimension is only a readback view.
struct ProductDimensionText {
 AcDbObjectId style, font;
 double height{}, scale{}, fixed{};
 double gap{}, lineExtension{};
 double measurementFactor{};
};

bool productDimensionEqual(double a,double b) {
 return std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=1e-6;
}

bool productDimensionFamily(const std::wstring& name,const std::wstring& base) {
 if(_wcsicmp(name.c_str(),base.c_str())==0)return true;
 const auto prefix=base+L"&&";
 if(name.size()<=prefix.size()||_wcsnicmp(name.c_str(),prefix.c_str(),prefix.size())!=0)return false;
 return std::all_of(name.begin()+prefix.size(),name.end(),[](wchar_t c){return c>=L'0'&&c<=L'9';});
}

bool productDimensionRatioMatches(const std::wstring& name,const std::wstring& base,double scale) {
 return name.size()==base.size()||
        productDimensionEqual(std::wcstod(name.c_str()+base.size()+2,nullptr),scale);
}

ProductDimensionText productDimensionText(const AcDbDimension& dimension) {
 const auto font=dimension.dimtxsty();
 return {dimension.dimensionStyle(),font,dimension.dimtxt(),dimension.dimscale(),
         font.isNull()?-1:open<AcDbTextStyleTableRecord>(font)->textSize(),
         dimension.dimgap(),dimension.dimdle(),dimension.dimlfac()};
}

std::vector<ProductDimensionText> productDimensionTexts(AcDbEntity& entity) {
 if(const auto* dimension=AcDbDimension::cast(&entity))return {productDimensionText(*dimension)};
 TemplateParts parts(&entity);
 if(entity.explode(parts.values)!=Acad::eOk)return {};
 std::vector<ProductDimensionText> out;
 for(int i=0;i<parts.values.length();++i){
  const auto* dimension=AcDbDimension::cast(static_cast<AcRxObject*>(parts.values[i]));
  if(!dimension)return {};
  out.push_back(productDimensionText(*dimension));
 }
 return out;
}

std::vector<double> productDimensionMeasurements(AcDbEntity& entity,bool native) {
 if(native)return chainValues(entity.objectId());
 double value{};
 productStatus(AcDbDimension::cast(&entity)->measurement(value),"read plan dimension measurement");
 return {value};
}

bool productDimensionMatches(const ProductDimensionText& text,const std::wstring& base,double scale,double modelHeight) {
 if(text.style.isNull())return false;
 const auto name=symbolName(text.style);
 return productDimensionFamily(name,base)&&productDimensionRatioMatches(name,base,scale)&&
        productDimensionEqual(text.fixed,0)&&productDimensionEqual(text.height*text.scale,modelHeight);
}

struct ProductDimensionFont {
 std::wstring name, primary, big;
 double width{}, oblique{};
 unsigned flags{};
};

ProductDimensionFont productReadDimensionFont(AcDbObjectId id) {
 const auto record=open<AcDbTextStyleTableRecord>(id);
 AcString name, primary, big;
 productStatus(record->getName(name),"read dimension text style name");
 productStatus(record->fileName(primary),"read dimension primary font");
 productStatus(record->bigFontFileName(big),"read dimension big font");
 return {name.kACharPtr(),primary.kACharPtr(),big.kACharPtr(),record->xScale(),
         record->obliquingAngle(),record->flagBits()};
}

bool productSameDimensionFont(const ProductDimensionFont& a,const ProductDimensionFont& b) {
 return _wcsicmp(a.primary.c_str(),b.primary.c_str())==0&&_wcsicmp(a.big.c_str(),b.big.c_str())==0&&
        productDimensionEqual(a.width,b.width)&&productDimensionEqual(a.oblique,b.oblique)&&a.flags==b.flags;
}

bool productDimensionFontAvailable(AcDbDatabase& db,const std::wstring& file) {
 if(file.empty())return true;
 AcString resolved;
 std::error_code error;
 if(acdbHostApplicationServices()->findFile(resolved,file.c_str(),&db,
    AcDbHostApplicationServices::kFontFile)==Acad::eOk&&fs::is_regular_file(resolved.kACharPtr(),error))return true;
 // TrueType/OpenType faces may resolve through the Windows Fonts directory
 // even when the AutoCAD support-path query cannot provide their filename.
 const fs::path font(file);
 const auto extension=font.extension().wstring();
 if(font!=font.filename()||(_wcsicmp(extension.c_str(),L".ttf")&&_wcsicmp(extension.c_str(),L".ttc")&&
    _wcsicmp(extension.c_str(),L".otf")))return false;
 wchar_t systemDirectory[MAX_PATH];
 const auto length=GetWindowsDirectoryW(systemDirectory,MAX_PATH);
 return length>0&&length<MAX_PATH&&fs::is_regular_file(fs::path(systemDirectory)/L"Fonts"/font,error);
}

struct ProductDimensionReference {
 ProductDimensionFont font;
 double gap{}, lineExtension{};
};

std::optional<ProductDimensionReference> productDimensionReference(AcDbDatabase& reference,const std::wstring& style) {
 Working working(&reference);
 AcDbObjectId id;
 const auto table=open<AcDbDimStyleTable>(reference.dimStyleTableId());
 if(table->getAt(style.c_str(),id)!=Acad::eOk)return {};
 const auto record=open<AcDbDimStyleTableRecord>(id);
 const auto font=record->dimtxsty();
 if(font.isNull())return {};
 return ProductDimensionReference{productReadDimensionFont(font),record->dimgap(),record->dimdle()};
}

AcDbObjectId productPlanDimensionFont(AcDbDatabase& db,const ProductDimensionFont& expected) {
 AcDbObjectId id;
 auto name=expected.name;
 for(unsigned suffix=0;;++suffix){
  const auto table=open<AcDbTextStyleTable>(db.textStyleTableId());
  if(table->getAt(name.c_str(),id)!=Acad::eOk)break;
  if(productSameDimensionFont(productReadDimensionFont(id),expected)&&
     productDimensionEqual(open<AcDbTextStyleTableRecord>(id)->textSize(),0))return id;
  name=expected.name+L"_PLAN_"+std::to_wstring(suffix+1);
 }
 auto record=std::make_unique<AcDbTextStyleTableRecord>();
 productStatus(record->setName(name.c_str()),"name verified dimension text style");
 productStatus(record->setFileName(expected.primary.c_str()),"set verified dimension primary font");
 if(!expected.big.empty())
  productStatus(record->setBigFontFileName(expected.big.c_str()),"set verified dimension big font");
 productStatus(record->setXScale(expected.width),"set verified dimension text width");
 productStatus(record->setObliquingAngle(expected.oblique),"set verified dimension text obliquing");
 record->setFlagBits(static_cast<Adesk::UInt8>(expected.flags));
 auto table=editObject<AcDbTextStyleTable>(db.textStyleTableId());
 productStatus(table->add(id,record.get()),"add verified dimension text style");
 record.release()->close();
 return id;
}

bool productDimensionAppearanceMatches(const ProductDimensionText& text,const ProductDimensionReference& expected) {
 return !text.font.isNull()&&productSameDimensionFont(productReadDimensionFont(text.font),expected.font)&&
        productDimensionEqual(text.gap,expected.gap)&&productDimensionEqual(text.lineExtension,expected.lineExtension);
}

bool productNativeDimensionStyleMatches(const ProductDimensionText& text,AcDbDatabase& reference,
 const bcpproduct::PlanDimensionStylePolicy& policy,double scale,double unit) {
 if(!productDimensionMatches(text,policy.name,scale,policy.nominalPaperHeightMM*scale/unit))return false;
 const auto expected=productDimensionReference(reference,symbolName(text.style));
 return expected&&productDimensionAppearanceMatches(text,*expected);
}

bool productPlanDimensionVisible(const AcDbEntity& entity) {
 const auto layer=open<AcDbLayerTableRecord>(entity.layerId());
 return entity.visibility()==AcDb::kVisible&&!layer->isOff()&&!layer->isFrozen();
}

AcDbObjectId productPlanDimensionBase(AcDbDatabase& db,AcDbDatabase& reference,const std::wstring& name) {
 AcDbObjectId id;
 {auto table=open<AcDbDimStyleTable>(db.dimStyleTableId());
  if(table->getAt(name.c_str(),id)==Acad::eOk)return id;}
 AcDbObjectId source;
 {auto table=open<AcDbDimStyleTable>(reference.dimStyleTableId());
  if(table->getAt(name.c_str(),source)!=Acad::eOk)return {};}
 AcDbObjectIdArray objects;
 objects.append(source);
 AcDbIdMapping mapping;
 if(reference.wblockCloneObjects(objects,db.dimStyleTableId(),mapping,AcDb::kDrcIgnore,false)!=Acad::eOk)return {};
 const auto table=open<AcDbDimStyleTable>(db.dimStyleTableId());
 return table->getAt(name.c_str(),id)==Acad::eOk?id:AcDbObjectId::kNull;
}

AcDbObjectId productPlanDimensionBase(ProductJob& job,AcDbDatabase& db) {
 const auto& name=job.dimensionStylePolicy.name;
 AcDbObjectId id;
 {auto table=open<AcDbDimStyleTable>(db.dimStyleTableId());
  if(table->getAt(name.c_str(),id)==Acad::eOk)return id;}
 AcDbDatabase reference(false,true);
 if(reference.readDwgFile((job.profile/L"reference.dwg").c_str(),AcDbDatabase::kForReadAndAllShare,false)!=Acad::eOk||
    reference.closeInput(true)!=Acad::eOk)return {};
 return productPlanDimensionBase(db,reference,name);
}

std::vector<AcDbObjectId> productProtectedDimensions(AcDbDatabase& db,const std::set<std::string>& excluded) {
 std::vector<AcDbObjectId> dimensions;
 for(const auto& handle:excluded){
  AcDbObjectId id;
  const auto status=db.getAcDbObjectId(id,false,AcDbHandle(bcpproduct::decodeMessage(handle).c_str()));
  if(status==Acad::eKeyNotFound||status==Acad::eWasErased||(status==Acad::eOk&&id.isErased()))continue;
  productStatus(status,"resolve excluded dimension style reference");
  dimensions.push_back(id);
 }
 const auto table=open<AcDbBlockTable>(db.blockTableId());
 AcDbBlockTableIterator* raw{};
 productStatus(table->newIterator(raw),"list protected dimension containers");
 std::unique_ptr<AcDbBlockTableIterator> blocks(raw);
 for(;!blocks->done();blocks->step()){
  AcDbObjectId id;
  productStatus(blocks->getRecordId(id),"read dimension container");
  if(id==modelId(db))continue;
  const auto block=open<AcDbBlockTableRecord>(id);
  if(block->isFromExternalReference())continue;
  AcDbBlockTableRecordIterator* entities{};
  productStatus(block->newIterator(entities),"list protected dimension entities");
  std::unique_ptr<AcDbBlockTableRecordIterator> it(entities);
  for(;!it->done();it->step()){
   AcDbObjectId entityId;
   productStatus(it->getEntityId(entityId),"read protected dimension entity");
   auto entity=open<AcDbEntity>(entityId);
   if(!AcDbDimension::cast(entity.get())&&std::wstring(entity->isA()->name())!=L"TDbDimension2")continue;
   dimensions.push_back(entityId);
  }
 }
 return dimensions;
}

// A shared record in another layout or block must not be edited in place.
std::set<AcDbObjectId> productProtectedDimensionStyles(AcDbDatabase& db,const std::set<std::string>& excluded) {
 std::set<AcDbObjectId> styles;
 for(const auto id:productProtectedDimensions(db,excluded))
  for(const auto& text:productDimensionTexts(*open<AcDbEntity>(id)))
   if(!text.style.isNull())styles.insert(text.style);
 return styles;
}

bool productEqualDimensionAppearance(const ProductDimensionText& a,const ProductDimensionText& b){
 return a.style==b.style&&a.font==b.font&&productDimensionEqual(a.height,b.height)&&
  productDimensionEqual(a.scale,b.scale)&&productDimensionEqual(a.fixed,b.fixed)&&
  productDimensionEqual(a.gap,b.gap)&&productDimensionEqual(a.lineExtension,b.lineExtension)&&
  productDimensionEqual(a.measurementFactor,b.measurementFactor);
}

// Free the canonical name for new chains by moving only the legacy record's
// name. The old record ID, its actual typography and its users remain together.
void productSeparateSharedPlanDimensionStyles(ProductJob& job,AcDbDatabase& db,AcDbDatabase& reference,
 const std::set<std::string>& excluded,double scale,double unit){
 struct ProtectedDimension {
  AcDbObjectId id;
  bool model{};
  std::vector<ProductDimensionText> parts;
  std::vector<double> measures;
 };
 const auto& policy=job.dimensionStylePolicy;
 std::vector<ProtectedDimension> snapshots;
 std::set<AcDbObjectId> shared;
 std::vector<std::pair<std::wstring,std::wstring>> separated;
 std::vector<std::wstring> standardizedModelStyles;
 std::vector<std::pair<std::wstring,std::string>> issues;
 for(const auto id:productProtectedDimensions(db,excluded)){
  auto entity=open<AcDbEntity>(id);
  auto parts=productDimensionTexts(*entity);
  if(parts.empty())continue;
  for(const auto& part:parts)
   if(!part.style.isNull())shared.insert(part.style);
  snapshots.push_back({id,entity->ownerId()==modelId(db),std::move(parts),
   productDimensionMeasurements(*entity,!AcDbDimension::cast(entity.get()))});
 }
 for(const auto id:shared){
  const auto name=symbolName(id);
  if(!productDimensionFamily(name,policy.name)||!productDimensionRatioMatches(name,policy.name,scale))continue;
  AcDbObjectId donor;
  {const auto table=open<AcDbDimStyleTable>(reference.dimStyleTableId());
   if(table->getAt(name.c_str(),donor)!=Acad::eOk){
    issues.emplace_back(name,"no matching reference dimension style");continue;
   }}
  const auto appearance=productDimensionReference(reference,name);
  if(!appearance)continue;
  const bool base=name.size()==policy.name.size();
  const auto height=[&](const AcDbDimStyleTableRecord& row){
   return base?policy.nominalPaperHeightMM:policy.nominalPaperHeightMM*scale/unit/row.dimscale();
  };
  {const auto old=open<AcDbDimStyleTableRecord>(id);
   const ProductDimensionText state{id,old->dimtxsty(),old->dimtxt(),old->dimscale(),0,old->dimgap(),old->dimdle()};
   if(old->dimscale()>0&&productDimensionEqual(old->dimtxt(),height(*old))&&
      productDimensionAppearanceMatches(state,*appearance))continue;}

  const auto legacyName=L"PLAN_LEGACY_DIM_"+bcpproduct::decodeMessage(axisHandle(id));
  const auto ratio=base?std::wstring{}:name.substr(policy.name.size());
  std::wstring legacy=legacyName+ratio;
  const auto availableName=[&]{
   auto table=open<AcDbDimStyleTable>(db.dimStyleTableId());AcDbObjectId used;
   return table->getAt(legacy.c_str(),used)!=Acad::eOk;
  };
  for(unsigned suffix=1;!availableName();++suffix)legacy=legacyName+L"_"+std::to_wstring(suffix)+ratio;
  const auto font=productPlanDimensionFont(db,appearance->font);
  AcDbObjectId canonical;
  bool renamed=false;
  try{
   productStatus(editObject<AcDbDimStyleTableRecord>(id)->setName(legacy.c_str()),"reserve plan dimension style name");
   renamed=true;
   AcDbIdMapping map;
   AcDbObjectIdArray ids;ids.append(donor);
   productStatus(reference.wblockCloneObjects(ids,db.dimStyleTableId(),map,AcDb::kDrcIgnore,false),
    "import the standard dimension style");
   {auto table=open<AcDbDimStyleTable>(db.dimStyleTableId());
    productStatus(table->getAt(name.c_str(),canonical),"find imported standard dimension style");}
   require(canonical!=id,"standard and legacy dimension styles need separate records");
   {
    auto row=editObject<AcDbDimStyleTableRecord>(canonical);
    require(row->dimscale()>0,"standard dimension scaling is invalid");
    productStatus(row->setDimtxt(height(*row)),"set the plan's canonical dimension text height");
    productStatus(row->setDimtxsty(font),"bind the verified dimension font");
    productStatus(row->setDimtmove(2),"set independent plan dimension text");
   }
   for(const auto& before:snapshots){
    auto entity=open<AcDbEntity>(before.id);
    const auto present=productDimensionTexts(*entity);
    const auto measures=productDimensionMeasurements(*entity,!AcDbDimension::cast(entity.get()));
    require(present.size()==before.parts.size()&&std::equal(present.begin(),present.end(),before.parts.begin(),
     productEqualDimensionAppearance)&&measures.size()==before.measures.size()&&
     std::equal(measures.begin(),measures.end(),before.measures.begin(),productDimensionEqual),
     "shared legacy dimension changed during canonical style separation");
   }
   separated.emplace_back(name,legacy);
  }catch(const std::exception& error){
   if(!canonical.isNull()&&canonical!=id)
    productStatus(editObject<AcDbDimStyleTableRecord>(canonical)->erase(),"roll back new standard dimension style");
   if(renamed)productStatus(editObject<AcDbDimStyleTableRecord>(id)->setName(name.c_str()),"restore protected dimension style name");
   issues.emplace_back(name,error.what());
  }
  if(!separated.empty()&&separated.back().first==name)continue;
  const auto usesStyle=[&](const ProtectedDimension& row){
   return std::any_of(row.parts.begin(),row.parts.end(),[&](const auto& part){return part.style==id;});
  };
  if(std::any_of(snapshots.begin(),snapshots.end(),[&](const auto& row){return usesStyle(row)&&!row.model;}))continue;

  // Tianzheng can resolve the style by name again when exploding a native chain.
  // A model-only shared style can use the canonical appearance throughout this
  // output copy. Keep its original owner references and check every measured value.
  auto old=open<AcDbDimStyleTableRecord>(id);
  const auto previousFont=old->dimtxsty();
  const double previousHeight=old->dimtxt(),previousGap=old->dimgap(),previousTail=old->dimdle();
  const auto previousMove=old->dimtmove();
  const double desiredHeight=height(*old);
  old.reset();
  const auto setAppearance=[&](AcDbObjectId text,double size,double gap,double tail,int move){
   auto row=editObject<AcDbDimStyleTableRecord>(id);
   productStatus(row->setDimtxsty(text),"set shared model dimension font");
   productStatus(row->setDimtxt(size),"set shared model dimension text height");
   productStatus(row->setDimgap(gap),"set shared model dimension text gap");
   productStatus(row->setDimdle(tail),"set shared model dimension line extension");
   productStatus(row->setDimtmove(move),"set shared model dimension text placement");
  };
  try{
   require(std::isfinite(desiredHeight)&&desiredHeight>0,"positive shared model dimension text height");
   setAppearance(font,desiredHeight,appearance->gap,appearance->lineExtension,2);
   for(const auto& before:snapshots)if(usesStyle(before)){
    auto entity=open<AcDbEntity>(before.id);
    const auto measured=productDimensionMeasurements(*entity,!AcDbDimension::cast(entity.get()));
    require(!measured.empty()&&measured.size()==before.measures.size()&&
      std::equal(measured.begin(),measured.end(),before.measures.begin(),productDimensionEqual),
      "canonical model style altered a protected dimension measurement");
   }
   standardizedModelStyles.push_back(name);
  }catch(const std::exception& error){
   setAppearance(previousFont,previousHeight,previousGap,previousTail,previousMove);
   issues.emplace_back(name,error.what());
  }
 }
 std::ostringstream proof;
 proof<<"{\"preservedUsers\":"<<snapshots.size()<<",\"separatedStyles\":[";
 for(std::size_t i=0;i<separated.size();++i){
  if(i)proof<<',';
  proof<<"{\"canonical\":"<<jsonUtf8(separated[i].first)<<",\"legacy\":"<<jsonUtf8(separated[i].second)<<'}';
 }
 proof<<"],\"standardizedModelStyles\":[";
 for(std::size_t i=0;i<standardizedModelStyles.size();++i){
  if(i)proof<<',';
  proof<<jsonUtf8(standardizedModelStyles[i]);
 }
 proof<<"],\"isolationTrials\":[";
 for(std::size_t i=0;i<issues.size();++i){
  if(i)proof<<',';
  proof<<"{\"canonical\":"<<jsonUtf8(issues[i].first)<<",\"reason\":"
      <<jsonUtf8(bcpproduct::decodeMessage(issues[i].second))<<'}';
 }
 proof<<"]}";
 writeNew(job.root/L"plan-shared-dimension-styles.json",proof.str());
}

void productApplyPlanDimensionStyle(ProductJob& job,AcDbDatabase& db,double scale,double unitMM) {
 Working work(&db);
 const auto& policy=job.dimensionStylePolicy;
 const double target=policy.nominalPaperHeightMM*scale/unitMM;
 require(std::isfinite(target)&&target>0,"positive model dimension text height");
 job.planDimensionMeasurements.clear();
 std::optional<std::set<AcDbObjectId>> protectedStyles;
 try{protectedStyles=productProtectedDimensionStyles(db,job.planDimensionExclusions);}
 catch(const std::exception& error){
  job.detailPending.push_back({u8"尺寸样式",u8"共享样式",u8"无法确认全部非模型空间引用，保留共享样式。"+std::string(error.what())});
 }
 const auto base=productPlanDimensionBase(job,db);
 AcDbDatabase reference(false,true);
 const bool hasReference=job.unifiedPlan&&
  reference.readDwgFile((job.profile/L"reference.dwg").c_str(),AcDbDatabase::kForReadAndAllShare,false)==Acad::eOk&&
  reference.closeInput(true)==Acad::eOk;
 const auto styleReference=hasReference?productDimensionReference(reference,policy.name):std::nullopt;
 std::map<AcDbObjectId,double> edits;
 std::set<AcDbObjectId> nativeStyles;
 std::set<AcDbObjectId> conflicts;
 for(const auto& [handle,type]:modelTypes(db)){
  const bool native=type==L"TDbDimension2";
  const auto key=ascii(handle);
  if(job.planDimensionExclusions.count(key))continue;
  try{
  const auto id=find(db,key);
  auto entity=open<AcDbEntity>(id);
  const auto* stock=AcDbDimension::cast(entity.get());
  if((!native&&!stock)||!productPlanDimensionVisible(*entity))continue;
  job.planDimensionMeasurements[key]=productDimensionMeasurements(*entity,native);
  const auto layer=open<AcDbLayerTableRecord>(entity->layerId());
  if(layer->isLocked()||!layer->isPlottable()){
   for(const auto& text:productDimensionTexts(*entity))
    if(!text.style.isNull())conflicts.insert(text.style);
   continue;
  }
  if(native){
   for(const auto& text:productDimensionTexts(*entity)){
    if(text.style.isNull())continue;
    const auto name=symbolName(text.style);
    if(!productDimensionFamily(name,policy.name))continue;
    const auto record=open<AcDbDimStyleTableRecord>(text.style);
    const bool supported=productDimensionRatioMatches(name,policy.name,scale)&&
     record->dimscale()>0&&productDimensionEqual(text.scale,record->dimscale())&&
     productDimensionEqual(text.height,record->dimtxt());
    if(!supported||text.style==base){conflicts.insert(text.style);continue;}
    nativeStyles.insert(text.style);
    if(!productDimensionMatches(text,policy.name,scale,target))edits[text.style]=target/text.scale;
   }
   continue;
  }
  const auto text=productDimensionText(*stock);
  if(base.isNull()||!std::isfinite(text.scale)||text.scale<=0||
     (!styleReference&&!productDimensionEqual(text.fixed,0)))continue;
  if(stock->dimtmove()==2&&text.style==base&&productDimensionMatches(text,policy.name,scale,target)&&
     (!job.unifiedPlan||(styleReference&&productDimensionAppearanceMatches(text,*styleReference)))&&
     productDimensionEqual(open<AcDbDimStyleTableRecord>(base)->dimtxt(),policy.nominalPaperHeightMM))continue;
  AcDbDimStyleTableRecord* old{};
  if(stock->getDimstyleData(old)!=Acad::eOk||!old)continue;
  std::unique_ptr<AcDbDimStyleTableRecord> before(old);
  auto settings=std::make_unique<AcDbDimStyleTableRecord>();
  productStatus(settings->copyFrom(before.get()),"copy effective dimension settings");
  productStatus(settings->setDimtxt(target/text.scale),"write plan dimension effective height");
  productStatus(settings->setDimtmove(2),"keep dimension text independent without leader");
  if(styleReference){
   productStatus(settings->setDimtxsty(productPlanDimensionFont(db,styleReference->font)),"apply verified plan font");
   productStatus(settings->setDimgap(styleReference->gap*scale/unitMM/text.scale),"apply verified plan dimension gap");
   productStatus(settings->setDimdle(styleReference->lineExtension*scale/unitMM/text.scale),"apply verified plan dimension line extension");
  }
  const auto oldStyle=stock->dimensionStyle();
  entity.reset();
  auto dimension=editObject<AcDbDimension>(id);
  if(dimension->setDimensionStyle(base)!=Acad::eOk||
     dimension->setDimstyleData(settings.get())!=Acad::eOk||
     dimension->recomputeDimBlock(true)!=Acad::eOk){
   productStatus(dimension->setDimensionStyle(oldStyle),"restore original dimension style");
   productStatus(dimension->setDimstyleData(before.get()),"restore original dimension settings");
   productStatus(dimension->recomputeDimBlock(true),"restore original dimension graphics");
  }
  }catch(const std::exception& error){
   job.detailPending.push_back({u8"尺寸字高",key,u8"尺寸样式或测值无法读取或应用。"+std::string(error.what())});
  }
 }
 for(const auto& style:nativeStyles){
  if(conflicts.count(style)||!protectedStyles||protectedStyles->count(style))continue;
  const auto expected=hasReference?productDimensionReference(reference,symbolName(style)):std::nullopt;
  const auto font=expected?productPlanDimensionFont(db,expected->font):AcDbObjectId{};
  auto record=editObject<AcDbDimStyleTableRecord>(style);
  if(record->dimtmove()!=2)productStatus(record->setDimtmove(2),"write native plan dimension text mode");
  if(expected){
   if(record->dimtxsty()!=font)productStatus(record->setDimtxsty(font),"apply verified native dimension font");
   if(!productDimensionEqual(record->dimgap(),expected->gap))
    productStatus(record->setDimgap(expected->gap),"apply verified native dimension gap");
   if(!productDimensionEqual(record->dimdle(),expected->lineExtension))
    productStatus(record->setDimdle(expected->lineExtension),"apply verified native dimension line extension");
  }
 }
 for(const auto& [style,height]:edits){
  if(conflicts.count(style)||!protectedStyles||protectedStyles->count(style))continue;
  auto record=editObject<AcDbDimStyleTableRecord>(style);
  productStatus(record->setDimtxt(height),"write effective Tianzheng plan dimension style height");
 }
 if(!base.isNull()&&!conflicts.count(base)&&protectedStyles&&!protectedStyles->count(base)){
  const auto expected=hasReference?productDimensionReference(reference,policy.name):std::nullopt;
  const auto font=expected?productPlanDimensionFont(db,expected->font):AcDbObjectId{};
  auto record=editObject<AcDbDimStyleTableRecord>(base);
  if(!productDimensionEqual(record->dimtxt(),policy.nominalPaperHeightMM))
   productStatus(record->setDimtxt(policy.nominalPaperHeightMM),"write nominal plan dimension style height");
  if(expected){
   if(record->dimtxsty()!=font)productStatus(record->setDimtxsty(font),"apply verified base dimension font");
   if(!productDimensionEqual(record->dimgap(),expected->gap))
    productStatus(record->setDimgap(expected->gap),"apply verified base dimension gap");
   if(!productDimensionEqual(record->dimdle(),expected->lineExtension))
    productStatus(record->setDimdle(expected->lineExtension),"apply verified base dimension line extension");
  }
 }
}

void productVerifyPlanDimensionStyle(ProductJob& job,AcDbDatabase& db,double scale,double unitMM) {
 Working work(&db);
 const auto& policy=job.dimensionStylePolicy;
 const double target=policy.nominalPaperHeightMM*scale/unitMM;
 AcDbDatabase reference(false,true);
 const bool hasReference=job.unifiedPlan&&
  reference.readDwgFile((job.profile/L"reference.dwg").c_str(),AcDbDatabase::kForReadAndAllShare,false)==Acad::eOk&&
  reference.closeInput(true)==Acad::eOk;
 AcDbObjectId base;
 bool nominal=false;
 {auto table=open<AcDbDimStyleTable>(db.dimStyleTableId());
  if(table->getAt(policy.name.c_str(),base)==Acad::eOk)
   nominal=productDimensionEqual(open<AcDbDimStyleTableRecord>(base)->dimtxt(),policy.nominalPaperHeightMM);}
 if(job.unifiedPlan&&nominal){
  const auto expected=hasReference?productDimensionReference(reference,policy.name):std::nullopt;
  const auto record=open<AcDbDimStyleTableRecord>(base);
  nominal=expected&&productDimensionAppearanceMatches(
   {base,record->dimtxsty(),record->dimtxt(),record->dimscale(),0,record->dimgap(),record->dimdle()},*expected);
 }
 std::map<std::wstring,bool> fontFiles;
 const auto fileAvailable=[&](const std::wstring& file){
  const auto [entry,inserted]=fontFiles.try_emplace(file,false);
  if(inserted)entry->second=productDimensionFontAvailable(db,file);
  return entry->second;
 };
 std::set<std::string> observed;
 const auto panelOf=[&](const std::string& handle,const AcDbEntity* entity){
  for(const auto& [key,_]:job.sourceInventory.items)
   if(std::get<1>(key)=="source-dimension"&&std::get<2>(key)==handle)return std::get<0>(key);
  AcDbExtents extents;
  if(!entity||entity->getGeomExtents(extents)!=Acad::eOk)return std::size_t{};
  const auto center=extents.minPoint()+(extents.maxPoint()-extents.minPoint())*.5;
  for(std::size_t i=0;i<job.planPrintWindows.size();++i){
   const auto& box=job.planPrintWindows[i];
   if(center.x>=box[0]&&center.x<=box[2]&&center.y>=box[1]&&center.y<=box[3])
    return i<job.layoutSourcePanels.size()?job.layoutSourcePanels[i]:i+1;
  }
  return std::size_t{};
 };
 const auto styleFailure=[&](std::size_t panel,const std::string& handle,const std::string& reason){
  job.detailPending.push_back({u8"尺寸字高",handle,reason});
  if(job.architecturalPlanOnly)job.drawingChecks.record(panel,"dimension-style",handle,{"Y04","G04"},
    bcpproduct::PlanReleaseGate::reopened,bcpproduct::PlanCheck::failed,u8"独立重开实际尺寸样式",reason);
 };
 std::ostringstream proof;
 proof<<std::setprecision(17)<<"{\"style\":"<<jsonUtf8(policy.name)
      <<",\"savedReopened\":true,\"nominalStyleMatches\":"<<(nominal?"true":"false")
      <<",\"paperHeightMM\":"<<policy.nominalPaperHeightMM<<",\"modelHeight\":"<<target<<",\"dimensions\":[";
 bool first=true;
 int passed=0,failed=0;
 for(const auto& [handle,type]:modelTypes(db)){
  const bool native=type==L"TDbDimension2";
  const auto key=ascii(handle);
  if(job.planDimensionExclusions.count(key))continue;
  auto entity=open<AcDbEntity>(find(db,key));
  if((!native&&!AcDbDimension::cast(entity.get()))||!productPlanDimensionVisible(*entity))continue;
  observed.insert(key);
  std::vector<ProductDimensionText> text;
  bool measurement=false,correct=false;
  std::string issue;
  try{
   text=productDimensionTexts(*entity);
   const auto old=job.planDimensionMeasurements.find(key);
   const auto values=productDimensionMeasurements(*entity,native);
   measurement=!values.empty()&&old!=job.planDimensionMeasurements.end()&&old->second.size()==values.size()&&
    std::equal(values.begin(),values.end(),old->second.begin(),productDimensionEqual);
   correct=measurement&&text.size()==values.size()&&std::all_of(text.begin(),text.end(),[&](const auto& row){
    if(!productDimensionMatches(row,policy.name,scale,target))return false;
    if(!job.unifiedPlan)return true;
    if(native&&!productDimensionEqual(row.measurementFactor,unitMM)){
     issue=u8"尺寸有效测量因子与当前图纸单位不同，须核显示值";
     return false;
    }
    auto expected=hasReference?productDimensionReference(reference,symbolName(row.style)):std::nullopt;
    if(expected&&!native){
     expected->gap*=scale/unitMM/row.scale;
     expected->lineExtension*=scale/unitMM/row.scale;
    }
    if(!expected||!productDimensionAppearanceMatches(row,*expected))return false;
    if(!fileAvailable(expected->font.primary)||!fileAvailable(expected->font.big)){
     issue=u8"字样定义正确，但当前 AutoCAD 未解析到对应实际字体文件";
     return false;
    }
    return true;
   });
   for(const auto& row:text)if(!row.style.isNull()){
    const auto name=symbolName(row.style);
    if(productDimensionFamily(name,policy.name)&&!productDimensionRatioMatches(name,policy.name,scale))
     issue=u8"尺寸比例派生样式与本次成图比例不同，保留待确认";
   }
  }catch(const std::exception& error){issue=error.what();}
  if(correct)++passed;
  else{
   ++failed;
   const auto pending=job.planDimensionNormalizationPending.find(key);
   if(pending!=job.planDimensionNormalizationPending.end())issue=pending->second;
   styleFailure(panelOf(key,entity.get()),key,!issue.empty()?issue:measurement?
    u8"重开后尺寸有效字体、字宽、字高、样式或显示覆盖未达到规则":
    u8"重开后缺尺寸样式应用记录或实测值与应用前不一致");
  }
  if(!first)proof<<',';
  first=false;
  proof<<"{\"handle\":\""<<key<<"\",\"layer\":"<<jsonUtf8(symbolName(entity->layerId()))
       <<",\"native\":"<<(native?"true":"false")<<",\"passed\":"<<(correct?"true":"false")<<",\"parts\":[";
  bool firstPart=true;
  for(const auto& part:text){
   if(!firstPart)proof<<',';
   firstPart=false;
   proof<<"{\"style\":"<<jsonUtf8(part.style.isNull()?L"":symbolName(part.style))
        <<",\"height\":"<<part.height<<",\"scale\":"<<part.scale<<",\"fontFixedHeight\":"<<part.fixed
        <<",\"gap\":"<<part.gap<<",\"lineExtension\":"<<part.lineExtension<<",\"measurementFactor\":"<<part.measurementFactor;
   if(!part.font.isNull()){
    const auto font=productReadDimensionFont(part.font);
    proof<<",\"fontStyle\":"<<jsonUtf8(font.name)<<",\"primaryFont\":"<<jsonUtf8(font.primary)
         <<",\"bigFont\":"<<jsonUtf8(font.big)<<",\"fontWidth\":"<<font.width;
    if(job.unifiedPlan)proof<<",\"primaryFontResolved\":"<<(fileAvailable(font.primary)?"true":"false")
                            <<",\"bigFontResolved\":"<<(fileAvailable(font.big)?"true":"false");
   }
   proof<<"}";
  }
  proof<<"]}";
  if(job.unifiedPlan){
   std::ostringstream row;
   row<<std::setprecision(17)<<"{\"object\":\""<<key<<"\",\"native\":"<<(native?"true":"false")
      <<",\"nominalStyleMatches\":"<<(nominal?"true":"false")<<",\"requiredPaperHeightMM\":"<<policy.nominalPaperHeightMM
      <<",\"requiredModelHeight\":"<<target<<",\"savedReopened\":true,\"report\":\"尺寸样式字高核验.json\",\"result\":\""
      <<(correct&&nominal?"verified-dimension-style":"pending")<<"\"}";
   productRecordRuleEvidence(job,"Y04",row.str());
   productRecordRuleEvidence(job,"G04",row.str());
  }
 }
 for(const auto& [key,values]:job.planDimensionMeasurements)if(!observed.count(key)){
  ++failed;
  styleFailure(panelOf(key,nullptr),key,u8"重开后缺少应核验的可见尺寸");
  if(job.unifiedPlan)productRecordRuleEvidence(job,"Y04","{\"object\":\""+key+
      "\",\"savedReopened\":true,\"report\":\"尺寸样式字高核验.json\",\"result\":\"missing-visible-dimension\"}");
 }
 if(!nominal&&!observed.empty())
  styleFailure(0,u8"基础样式",u8"重开后基础尺寸样式的名义字高不符合纸面规则");
 proof<<"],\"verified\":"<<passed<<",\"failed\":"<<failed
      <<",\"excluded\":"<<job.planDimensionExclusions.size()
      <<",\"passed\":"<<(nominal&&passed>0&&failed==0?"true":"false")<<"}";
 writeNew(job.root/L"尺寸样式字高核验.json",proof.str());
}
