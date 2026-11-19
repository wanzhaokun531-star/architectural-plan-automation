// Limited SHX glyph evidence. This does not claim OCR or a complete font engine.
// SDK headers textengine.h and acgiutil.h must be included at global scope.
struct ProductGlyphContour {
 std::vector<std::vector<AcGePoint3d>> lines;
 bool malformed{};
 static void receive(int count,const int* sizes,const AcGePoint3d* points,void* context) noexcept {
  auto& result=*static_cast<ProductGlyphContour*>(context);
  if(count<0||count>10000||(count&&(!sizes||!points))){result.malformed=true;return;}
  try{
   std::size_t offset=0;
   for(int k=0;k<count;++k){
    if(sizes[k]<0||sizes[k]>100000||offset+sizes[k]>100000){result.malformed=true;return;}
    result.lines.emplace_back(points+offset,points+offset+sizes[k]);offset+=sizes[k];
   }
  }catch(...){result.malformed=true;}
 }
 std::string signature() const {
  if(malformed)return {};
  std::ostringstream out;out<<std::fixed<<std::setprecision(7);bool ink=false;
  for(const auto& line:lines){
   if(line.size()<2)continue;
   for(std::size_t k=1;k<line.size();++k)ink|=line[k].distanceTo(line[k-1])>1e-8;
   out<<'[';for(const auto& point:line)out<<point.x<<','<<point.y<<','<<point.z<<';';out<<']';
  }
  return ink?out.str():std::string{};
 }
};

struct ProductRiskGlyphResult {
 std::string shape,reason,primarySHA256,bigSHA256;
 std::vector<std::string> placeholders;
 std::size_t contours{};
};

ProductRiskGlyphResult productReadRiskGlyph(AcDbDatabase& db,AcDbObjectId styleId,wchar_t character){
 ProductRiskGlyphResult result;
 try{
  const auto definition=productReadDimensionFont(styleId);
  const auto primary=productResolveExactPlanFont(db,definition.primary);
  const auto big=definition.big.empty()?std::optional<fs::path>{}:productResolveExactPlanFont(db,definition.big);
  require(primary.has_value()&&(definition.big.empty()||big.has_value()),"actual glyph font dependency is unavailable");
  require(_wcsicmp(fs::path(*primary).extension().c_str(),L".shx")==0,"risk glyph contour reader supports SHX only");
  result.primarySHA256=sha(fs::path(*primary));if(big)result.bigSHA256=sha(fs::path(*big));
  AcGiTextStyle style(&db);status(fromAcDbTextStyle(style,styleId),"read actual glyph style");
  // Unit contours retain this exact style's width, slant and font resources.
  style.setTextSize(1.);style.loadStyleRec(&db);
  std::unique_ptr<AcGiTextEngine> engine(AcGiTextEngine::create());require(bool(engine),"SHX tessellation engine unavailable");
  const auto read=[&](wchar_t value){ProductGlyphContour contour;const wchar_t text[]{value,0};
   engine->tessellate(style,text,1,true,1e-5,&contour,&ProductGlyphContour::receive);return contour;};
  const auto actual=read(character);result.shape=actual.signature();result.contours=actual.lines.size();
  require(!result.shape.empty(),"risk character has no readable SHX stroke contour");
  for(const wchar_t placeholder:{L'?',L'\uFFFD',L'\uFFFF'}){
   const auto fallback=read(placeholder).signature();
   result.placeholders.push_back(fallback);
  }
  require(std::none_of(result.placeholders.begin(),result.placeholders.end(),[&](const auto& shape){return !shape.empty()&&shape==result.shape;}),
   "risk character renders as the font's missing-character placeholder");
 }catch(const std::exception& e){result.reason=e.what();}
 return result;
}

// This repairs the equivalent unit spelling only. It is not a font substitution
// and must run after typography/position edits, before the final save and plot.
struct ProductPlanUnitGlyphRepair {
 std::string object,invariant,primarySHA256,bigSHA256,missingContour,mContour,squaredContour;
 std::wstring before,after,style,primary,big;
};

std::wstring productUnitGlyphText(const AcDbText& text){
 AcString body;productStatus(text.textString(body),"read unit text without field evaluation");return body.kACharPtr();
}

std::string productUnitGlyphInvariant(const AcDbText& text){
 const auto position=text.position(),alignment=text.alignmentPoint();const auto normal=text.normal();
 const auto font=productReadDimensionFont(text.textStyle());
 std::ostringstream out;out<<std::setprecision(17)
  <<position.x<<','<<position.y<<','<<position.z<<';'<<alignment.x<<','<<alignment.y<<','<<alignment.z<<';'
  <<normal.x<<','<<normal.y<<','<<normal.z<<';'<<text.height()<<';'<<text.widthFactor()<<';'
  <<text.rotation()<<';'<<text.oblique()<<';'<<text.thickness()<<';'<<int(text.horizontalMode())<<';'
  <<int(text.verticalMode())<<';'<<bool(text.isMirroredInX())<<';'<<bool(text.isMirroredInY())<<';'
  <<jsonUtf8(font.name)<<';'<<jsonUtf8(font.primary)<<';'<<jsonUtf8(font.big)<<';'
  <<font.width<<';'<<font.oblique<<';'<<font.flags<<';'
  <<open<AcDbTextStyleTableRecord>(text.textStyle())->textSize()<<';'<<jsonUtf8(symbolName(text.layerId()));
 return out.str();
}

std::vector<ProductPlanUnitGlyphRepair> productRepairPlanUnitGlyphs(ProductJob& job,AcDbDatabase& db,
 std::vector<ProductTypographyIssue>& issues){
 Working context(&db);productProtectFurnitureMembers(job,db);
 std::vector<ProductPlanUnitGlyphRepair> repaired;
 std::map<std::pair<AcDbObjectId,wchar_t>,ProductRiskGlyphResult> glyphs;
 const auto readGlyph=[&](AcDbObjectId style,wchar_t character)->const ProductRiskGlyphResult&{
  const auto key=std::make_pair(style,character);
  if(!glyphs.count(key))glyphs.emplace(key,productReadRiskGlyph(db,style,character));return glyphs.at(key);
 };
 for(const auto& row:productTypographyInventory(db)){
  if(!row.visible||row.external||row.root!=row.handle||job.protectedFurnitureMembers.count(row.handle))continue;
  try{
   const auto id=find(db,row.handle);auto original=open<AcDbEntity>(id);
   if(original->isA()!=AcDbText::desc()||!productPlanStandaloneTextWritable(db,*original)||
      !original->extensionDictionary().isNull())continue;
   const auto* text=AcDbText::cast(original.get());const auto before=productUnitGlyphText(*text);
   if(before.find(L'\u33A1')==std::wstring::npos)continue;
   // Remaining room-area text must be removed by the omission stage, not kept
   // alive through this repair. The current repair is scoped to building area.
   if(before.find(L"建筑面积")==std::wstring::npos)continue;
   const auto style=text->textStyle();const auto& missing=readGlyph(style,L'\u33A1');
   const bool placeholder=!missing.shape.empty()&&std::any_of(missing.placeholders.begin(),missing.placeholders.end(),
    [&](const auto& shape){return !shape.empty()&&shape==missing.shape;});
   if(!placeholder)continue; // A font resolution/read error is not proof of a missing glyph.
   const auto& m=readGlyph(style,L'm');const auto& squared=readGlyph(style,L'\u00B2');
   if(!m.reason.empty()||!squared.reason.empty()||m.shape.empty()||squared.shape.empty()){
    issues.push_back({row.path,"unit-glyph-repair",u8"平方米缺字已证实，但当前实际字体的 m/² 轮廓未通过，未替换正文"});continue;
   }
   ProductPlanUnitGlyphRepair item;item.object=row.handle;item.before=before;item.invariant=productUnitGlyphInvariant(*text);
   const auto definition=productReadDimensionFont(style);item.style=definition.name;item.primary=definition.primary;item.big=definition.big;
   item.primarySHA256=missing.primarySHA256;item.bigSHA256=missing.bigSHA256;
   item.missingContour=missing.shape;item.mContour=m.shape;item.squaredContour=squared.shape;
   for(const auto character:before){if(character==L'\u33A1')item.after+=L"m\u00B2";else item.after+=character;}
   // Reject unusual aligned-text side effects in a transient clone first.
   auto trial=std::unique_ptr<AcDbText>(AcDbText::cast(text->clone()));require(bool(trial),"clone ordinary unit text");
   productStatus(trial->setTextString(item.after.c_str()),"try equivalent square-metre unit spelling");
   require(productUnitGlyphInvariant(*trial)==item.invariant,"unit spelling trial changed protected text parameters");
   original.reset();auto writable=editObject<AcDbText>(id);
   productStatus(writable->setTextString(item.after.c_str()),"replace missing square-metre unit glyph");
   if(productUnitGlyphInvariant(*writable)!=item.invariant||productUnitGlyphText(*writable)!=item.after){
    productStatus(writable->setTextString(item.before.c_str()),"restore rejected unit spelling edit");
    throw std::runtime_error("unit spelling edit changed protected text parameters; original body restored");
   }
   repaired.push_back(std::move(item));
  }catch(const std::exception& e){issues.push_back({row.path,"unit-glyph-repair",e.what()});}
 }
 return repaired;
}

std::vector<ProductTypographyIssue> productAuditPlanUnitGlyphRepairs(AcDbDatabase& saved,
 const std::vector<ProductPlanUnitGlyphRepair>& repaired,const fs::path& drawing,const fs::path& report){
 Working context(&saved);std::vector<ProductTypographyIssue> issues;std::ostringstream out;
 out<<"{\"contract\":\"plan.unit-glyph-repairs.v1\",\"outputSHA256\":\""<<sha(drawing)
    <<"\",\"savedReopened\":true,\"scope\":\"ordinary model-space building-area text; proven missing U+33A1 replaced by contour-verified m+U+00B2\",\"objects\":[";
 bool first=true;
 for(const auto& item:repaired){
  std::string reason,actualInvariant;std::wstring actualBody;ProductRiskGlyphResult m,squared;
  try{
   const auto entity=open<AcDbEntity>(find(saved,item.object));
   require(entity->isA()==AcDbText::desc()&&entity->ownerId()==modelId(saved)&&!entity->hasFields(),"reopened unit text identity/type differs");
   const auto* text=AcDbText::cast(entity.get());actualBody=productUnitGlyphText(*text);actualInvariant=productUnitGlyphInvariant(*text);
   require(actualBody==item.after&&actualInvariant==item.invariant,"reopened unit body or protected text parameters differ");
   m=productReadRiskGlyph(saved,text->textStyle(),L'm');squared=productReadRiskGlyph(saved,text->textStyle(),L'\u00B2');
   require(m.reason.empty()&&squared.reason.empty()&&m.shape==item.mContour&&squared.shape==item.squaredContour&&
    m.primarySHA256==item.primarySHA256&&m.bigSHA256==item.bigSHA256&&
    squared.primarySHA256==item.primarySHA256&&squared.bigSHA256==item.bigSHA256,
    "reopened equivalent unit glyph contours or exact font resources differ");
  }catch(const std::exception& e){reason=e.what();issues.push_back({item.object,"unit-glyph-repair-readback",reason});}
  if(!first)out<<',';first=false;
  out<<"{\"object\":"<<jsonUtf8(bcpproduct::decodeMessage(item.object))<<",\"before\":"<<jsonUtf8(item.before)
     <<",\"after\":"<<jsonUtf8(item.after)<<",\"actual\":"<<jsonUtf8(actualBody)<<",\"style\":"<<jsonUtf8(item.style)
     <<",\"primary\":"<<jsonUtf8(item.primary)<<",\"big\":"<<jsonUtf8(item.big)
     <<",\"primarySHA256\":\""<<item.primarySHA256<<"\",\"bigSHA256\":\""<<item.bigSHA256
     <<"\",\"expectedProtectedParameters\":"<<jsonUtf8(bcpproduct::decodeMessage(item.invariant))
     <<",\"actualProtectedParameters\":"<<jsonUtf8(bcpproduct::decodeMessage(actualInvariant))
     <<",\"originalPlaceholderContour\":"<<jsonUtf8(bcpproduct::decodeMessage(item.missingContour))
     <<",\"replacementMContour\":"<<jsonUtf8(bcpproduct::decodeMessage(item.mContour))
     <<",\"replacementSquaredContour\":"<<jsonUtf8(bcpproduct::decodeMessage(item.squaredContour))
     <<",\"verified\":"<<(reason.empty()?"true":"false")<<",\"reason\":"<<jsonUtf8(bcpproduct::decodeMessage(reason))<<'}';
 }
 out<<"]}";writeNew(report/L"plan-unit-glyph-repair-readback.json",out.str());return issues;
}

std::vector<ProductTypographyIssue> productAuditRiskGlyphs(ProductJob& job,AcDbDatabase& saved,
 const fs::path& drawing,const fs::path& report){
 Working context(&saved);
 std::vector<ProductTypographyIssue> issues;
 std::map<std::pair<AcDbObjectId,wchar_t>,ProductRiskGlyphResult> cache;
 std::ostringstream out;
 out<<"{\"contract\":\"plan.risk-glyph-contours.v1\",\"outputSHA256\":\""<<sha(drawing)
    <<"\",\"savedReopened\":true,\"scope\":\"SHX superscript 2/3 and U+3300-U+33FF unit characters; nonempty and not placeholder\",\"objects\":[";
 bool first=true;
 for(const auto& row:productTypographyInventory(saved)){
  if(!row.visible||job.protectedFurnitureMembers.count(row.handle)||job.protectedFurnitureMembers.count(row.root))continue;
  try{
   std::vector<ProductRoomLeaf> leaves;productRoomLeaves(*open<AcDbEntity>(find(saved,row.handle)),leaves);
   for(const auto& leaf:leaves)for(const auto character:leaf.text){
    if(character!=L'²'&&character!=L'³'&&(character<L'\u3300'||character>L'\u33FF'))continue;
    const auto key=std::make_pair(leaf.style,character);
    if(!cache.count(key))cache.emplace(key,productReadRiskGlyph(saved,leaf.style,character));
    const auto& actual=cache.at(key);
    if(!actual.reason.empty())issues.push_back({row.path,"risk-glyph",u8"风险字符实际轮廓未通过；"+actual.reason});
    if(!first)out<<',';first=false;
    out<<"{\"object\":"<<jsonUtf8(bcpproduct::decodeMessage(row.path))<<",\"character\":"<<jsonUtf8(std::wstring(1,character))
       <<",\"style\":"<<jsonUtf8(symbolName(leaf.style))<<",\"primarySHA256\":\""<<actual.primarySHA256
       <<"\",\"bigSHA256\":\""<<actual.bigSHA256<<"\",\"contours\":"<<actual.contours
       <<",\"reason\":"<<jsonUtf8(bcpproduct::decodeMessage(actual.reason))<<",\"unitContour\":"
       <<jsonUtf8(bcpproduct::decodeMessage(actual.shape))<<",\"placeholderContours\":[";
    for(std::size_t k=0;k<actual.placeholders.size();++k){if(k)out<<',';out<<jsonUtf8(bcpproduct::decodeMessage(actual.placeholders[k]));}out<<"]}";
   }
  }catch(const std::exception& e){issues.push_back({row.path,"risk-glyph-readback",e.what()});}
 }
 out<<"],\"cachedStyleCharacters\":"<<cache.size()<<'}';writeNew(report/L"plan-risk-glyph-readback.json",out.str());
 return issues;
}
