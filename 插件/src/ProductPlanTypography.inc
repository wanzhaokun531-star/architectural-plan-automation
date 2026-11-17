// All finalizers read actual entities again; a previous pass or a style name is
// never evidence that the final drawing uses the required font definition.
struct ProductTypographyFonts {ProductDimensionFont dimension,general,opening;AcDbObjectId dim,text,caption;};
struct ProductTypographyIssue {std::string object,role,reason;};

ProductDimensionFont productTypographyOpeningFont(AcDbDatabase& reference){
 AcDbObjectId id;const auto table=open<AcDbTextStyleTable>(reference.textStyleTableId());
 if(table->getAt(L"TCH_WINDOW",id)!=Acad::eOk)productStatus(table->getAt(L"_TCH_WINDOW",id),"approved opening font resource");
 auto font=productReadDimensionFont(id);font.name=L"TCH_WINDOW";
 font.primary=fs::path(font.primary).filename().wstring();font.big=fs::path(font.big).filename().wstring();
 if(fs::path(font.primary).extension().empty())font.primary+=L".shx";
 if(!font.big.empty()&&fs::path(font.big).extension().empty())font.big+=L".shx";
 return font;
}

bool productTypographyFontMatches(AcDbObjectId id,const ProductDimensionFont& font,double fixed){
 if(id.isNull())return false;
 auto actual=productReadDimensionFont(id);auto expected=font;
 const auto normalize=[](std::wstring value){value=fs::path(value).filename().wstring();if(!value.empty()&&fs::path(value).extension().empty())value+=L".shx";return value;};
 actual.primary=normalize(actual.primary);actual.big=normalize(actual.big);
 expected.primary=normalize(expected.primary);expected.big=normalize(expected.big);
 const auto record=open<AcDbTextStyleTableRecord>(id);
 if(font.name==L"FY_FS"){
  AcString face;bool bold{},italic{};Charset charset{};Autodesk::AutoCAD::PAL::FontUtils::FontPitch pitch{};Autodesk::AutoCAD::PAL::FontUtils::FontFamily family{};
  if(record->font(face,bold,italic,charset,pitch,family)!=Acad::eOk||bold||italic||
     (_wcsicmp(face.kACharPtr(),L"新宋体")!=0&&_wcsicmp(face.kACharPtr(),L"NSimSun")!=0))return false;
  actual.primary=expected.primary; // Windows collection face is authoritative, not its resolved file alias.
 }
 auto* annotation=AcDbAnnotativeObjectPE::cast(record->queryX(AcDbAnnotativeObjectPE::desc()));
 return actual.name==font.name&&productSameDimensionFont(actual,expected)&&productDimensionEqual(record->textSize(),fixed)&&
  !record->isVertical()&&(!annotation||!annotation->annotative(record.get()));
}

AcDbObjectId productRestoreTypographyFont(AcDbDatabase& db,const ProductDimensionFont& font,double fixed){
 auto resolved=font;
 for(auto* file:{&resolved.primary,&resolved.big})if(!productDimensionFontAvailable(db,*file)){
  const auto local=productRoomFontFile(db,AcString(file->c_str()));require(local.has_value(),"required font files are unavailable");*file=*local;
 }
 const auto id=productExactPlanDimensionFont(db,resolved);
 {auto record=editObject<AcDbTextStyleTableRecord>(id);productStatus(record->setTextSize(fixed),"set typography style fixed height");
  if(font.name==L"FY_FS"){
   AcString face;bool bold{},italic{};Charset charset{};Autodesk::AutoCAD::PAL::FontUtils::FontPitch pitch{};Autodesk::AutoCAD::PAL::FontUtils::FontFamily family{};
   productStatus(record->font(face,bold,italic,charset,pitch,family),"read Windows font face");
   productStatus(record->setFont(L"新宋体",false,false,static_cast<Charset>(134),pitch,family),"set exact template Windows font face");
  }}
 require(productTypographyFontMatches(id,font,fixed),"restored typography style differs from its approved resource");return id;
}

ProductTypographyFonts productRestorePlanTypography(AcDbDatabase& db,AcDbDatabase& reference,bool templateMode=false){
 ProductTypographyFonts fonts;
 fonts.dimension={L"_TCH_DIM",L"simplex.shx",L"gbcbig.shx",1.,0.,0};
 fonts.general={L"FY_HZ",L"tssdeng.shx",L"hztxt.shx",.7,0.,0};
 fonts.opening=templateMode?ProductDimensionFont{L"_TCH_WINDOW",L"tssdeng.shx",L"hztxt.shx",.65,0.,0}:productTypographyOpeningFont(reference);
 if(!templateMode)fonts.dim=productRestoreTypographyFont(db,fonts.dimension,0);
 // FY_HZ may require the trusted local font search paths resolved by its importer.
 fonts.text=productRoomStyle(reference,db);require(!fonts.text.isNull(),"cannot restore the approved FY_HZ resource");
 {auto style=editObject<AcDbTextStyleTableRecord>(fonts.text);style->setTextSize(0);style->setObliquingAngle(0);style->setFlagBits(0);style->setIsVertical(false);
  if(auto* annotation=AcDbAnnotativeObjectPE::cast(style->queryX(AcDbAnnotativeObjectPE::desc())))
   productStatus(annotation->setAnnotative(style.get(),false),"restore non-annotative FY_HZ");}
 fonts.caption=productRestoreTypographyFont(db,fonts.opening,templateMode?0.:3.);
 return fonts;
}

bool productTypographyOpeningCode(const std::wstring& text){
 return std::regex_match(text,std::wregex(L"(?:FMD|M|C|LC|TC|LM|MLC|MDC|DK|D|MC)[0-9]{3,6}(?:[a-zA-Z])?"));
}

std::wstring productTypographyText(const ProductSourceAnnotation& row){std::wstring result;for(const auto& g:row.glyphs)result+=g.text;return result;}

std::string productTypographyRole(AcDbDatabase& db,const ProductSourceAnnotation& row){
 if(row.kind=="dimension"||row.kind=="unreadable-dimension")return "dimension";
 if(row.kind=="unreadable-container"||row.kind=="unreadable-text")return "unreadable";
 if(row.path!=row.root){
  const auto root=open<AcDbEntity>(find(db,row.root));
  if(const auto* block=AcDbBlockReference::cast(root.get())){
   const auto name=symbolName(block->blockTableRecord());
   if(name.find(L"图框")!=std::wstring::npos||name.find(L"图签")!=std::wstring::npos||
      name.find(L"TITLE")!=std::wstring::npos||name.find(L"A2")!=std::wstring::npos||name.find(L"A3")!=std::wstring::npos)
    return "special-title-block";
   std::vector<ProductRoomLeaf> members;productRoomLeaves(*block,members);std::wstring content;
   for(const auto& member:members)content+=member.text;
   int evidence=0;for(const auto* word:{L"建设单位",L"设计阶段",L"项目负责",L"专业负责",L"图别图号"})
    evidence+=content.find(word)!=std::wstring::npos;
   if(evidence>=3)return "special-title-block";
  }
 }
 const auto body=productTypographyText(row);
 if(body==L"2"&&row.position){
  const auto owner=open<AcDbEntity>(find(db,row.handle))->ownerId();
  const auto block=open<AcDbBlockTableRecord>(owner);AcDbBlockTableRecordIterator* raw{};
  if(block->newIterator(raw)==Acad::eOk){std::unique_ptr<AcDbBlockTableRecordIterator> it(raw);
   for(;!it->done();it->step()){AcDbObjectId id;it->getEntityId(id);if(id==find(db,row.handle))continue;
    const auto entity=open<AcDbEntity>(id);const auto* text=AcDbText::cast(entity.get());if(!text)continue;
    if(!std::regex_match(std::wstring(text->textString()),std::wregex(L"[0-9.]+[mM]")))continue;
    AcDbExtents box;if(text->getGeomExtents(box)!=Acad::eOk)continue;
    if(row.position->distanceTo(box.maxPoint())<std::max(400.,text->height()*2.))return "special-area-superscript";
   }
  }
 }
 if(row.type==L"TDbDrawingName"||bcpproduct::explicitPlanFloor(body)||productRoofTitle(body))return "special-drawing-title";
 if(productTypographyOpeningCode(body))return "opening-label";
 if(body.find(L"面积")!=std::wstring::npos||body.find(L"㎡")!=std::wstring::npos||
    std::regex_match(body,std::wregex(L"[0-9.]+[mM]²?"))||body==L"²"||
    (body==L"2"&&row.type==L"TDbText"&&row.layer.find(L"AREA")!=std::wstring::npos))return "special-area";
 if(body==L"北"||std::regex_match(body,std::wregex(L"[NSWE北南东西]")))return "special-direction";
 if(std::regex_match(body,std::wregex(L"1[:：][0-9]+")))return "special-scale-or-slope";
 if(std::regex_match(body,std::wregex(L"[+-]?[0-9]+(?:\\.[0-9]+)?"))){
  if(row.position&&std::abs(row.position->z)>.01&&row.layer.find(L"高程")!=std::wstring::npos)return "special-survey-elevation";
  if(row.layer.find(L"高程")!=std::wstring::npos){
   unsigned total=0,spatial=0;for(const auto& [h,type]:modelTypes(db))if(type==L"AcDbText"){
    const auto text=open<AcDbText>(find(db,ascii(h)));if(symbolName(text->layerId())!=row.layer)continue;
    if(!std::regex_match(std::wstring(text->textString()),std::wregex(L"[+-]?[0-9]+(?:\\.[0-9]+)?")))continue;
    ++total;if(std::abs(text->position().z)>.01)++spatial;
   }
   if(total>=8&&spatial*2>=total)return "special-survey-elevation";
  }
  return "unclassified-numeric";
 }
 if(row.kind=="linked-text")return "linked-note";
 return "general";
}

std::vector<ProductSourceAnnotation> productTypographyInventory(AcDbDatabase& db){
 auto result=productReadOldAnnotations(db);
 const auto dictionary=open<AcDbDictionary>(db.layoutDictionaryId());
 std::unique_ptr<AcDbDictionaryIterator> it(dictionary->newIterator());
 for(;!it->done();it->next()){
  const auto layout=open<AcDbLayout>(it->objectId());if(layout->modelType())continue;
  auto rows=productReadOldAnnotations(db,nullptr,layout->getBlockTableRecordId());
  result.insert(result.end(),std::make_move_iterator(rows.begin()),std::make_move_iterator(rows.end()));
 }
 return result;
}

double productTypographyInstanceScale(AcDbDatabase& db,const ProductSourceAnnotation& row){
 AcGeMatrix3d world;std::size_t start=0;
 for(auto slash=row.path.find('/');slash!=std::string::npos;slash=row.path.find('/',start)){
  const auto handle=row.path.substr(start,slash-start);start=slash+1;
  const auto entity=open<AcDbEntity>(find(db,handle));
  if(const auto* ref=AcDbBlockReference::cast(entity.get()))world=world*ref->blockTransform();
 }
 auto x=AcGeVector3d::kXAxis,y=AcGeVector3d::kYAxis;x.transformBy(world);y.transformBy(world);
 require(std::abs(x.length()-y.length())<1e-6&&x.length()>1e-9,"non-uniform nested text scale requires an isolated instance");
 // Attributes are already expressed in the owning insert's parent space.
 if(row.type==L"AcDbAttribute"){
  const auto slash=row.path.rfind('/');const auto preceding=slash?row.path.rfind('/',slash-1):std::string::npos;
  if(slash!=std::string::npos){const auto parent=row.path.substr(preceding==std::string::npos?0:preceding+1,slash-(preceding==std::string::npos?0:preceding+1));
   const auto ref=open<AcDbBlockReference>(find(db,parent));const auto scale=ref->scaleFactors();
   require(std::abs(std::abs(scale.sx)-std::abs(scale.sy))<1e-6,"non-uniform attribute parent");return y.length()/std::abs(scale.sy);}
 }
 return y.length();
}

double productTypographySpaceFactor(AcDbDatabase& db,const ProductSourceAnnotation& row){
 const auto root=open<AcDbEntity>(find(db,row.root));const auto owner=root->ownerId();
 return owner==modelId(db)?1.:.01;
}

std::wstring productTypographyStripOverrides(const std::wstring& raw){
 std::wstring out;
 for(std::size_t i=0;i<raw.size();){
  if(raw[i]==L'\\'&&i+1<raw.size()){
   if(raw[i+1]==L'\\'||raw[i+1]==L'{'||raw[i+1]==L'}'){out+=raw.substr(i,2);i+=2;continue;}
   if(std::wstring(L"fFHWQT").find(raw[i+1])!=std::wstring::npos){
    const auto end=raw.find(L';',i+2);require(end!=std::wstring::npos,"unterminated inline typography override");i=end+1;continue;
   }
  }
  out+=raw[i++];
 }
 return out;
}

std::string productTypographyRestyleText(AcDbDatabase& db,const ProductSourceAnnotation& row,AcDbObjectId style,double height){
 const auto id=find(db,row.handle);auto entity=open<AcDbEntity>(id);
 if(entity->hasFields()||entity->isAProxy()||row.external||open<AcDbLayerTableRecord>(entity->layerId())->isLocked())
  return u8"字段、代理、外参或锁层文字，未强改其关联";
 const auto contents=productPlanNameContents(*entity);const auto anchor=productPlanNameAnchor(*entity);
 if(!anchor)return u8"未能读取文字的可靠锚点";
 if(AcDbText::cast(entity.get())){
  entity.reset();auto text=editObject<AcDbText>(id);productStatus(text->setTextStyle(style),"restore ordinary text style");
  productStatus(text->setHeight(height),"restore ordinary text height");productStatus(text->setWidthFactor(open<AcDbTextStyleTableRecord>(style)->xScale()),"restore ordinary text width");
  productStatus(text->setOblique(0),"restore ordinary text oblique");
  if(!text->isDefaultAlignment())productStatus(text->adjustAlignment(&db),"retain ordinary text alignment");
 }else if(const auto* multiline=AcDbMText::cast(entity.get())){
  const auto raw=productPlanMTextOriginal(*multiline);if(!raw)return u8"多行文字原文不可读";
  const auto clean=productTypographyStripOverrides(*raw);entity.reset();auto text=editObject<AcDbMText>(id);
  productStatus(text->setTextStyle(style),"restore multiline base font");text->setContents(clean.c_str());
  productStatus(text->setTextHeight(height),"restore multiline text height");
 }else{
  entity.reset();auto problem=productRestyleStandalonePlanText(db,id,style,[](const auto&){return true;},true);
  if(!problem.empty())return problem;
  auto current=open<AcDbEntity>(id);std::vector<ProductRoomLeaf> leaves;productRoomLeaves(*current,leaves);
  if(leaves.empty()||leaves.front().height<=0)return u8"原生文字字高不可读";
  const double oldHeight=std::max_element(leaves.begin(),leaves.end(),[](const auto& a,const auto& b){return a.height<b.height;})->height;
  if(row.kind!="template-mixed-text"&&!std::all_of(leaves.begin(),leaves.end(),[&](const auto& leaf){return std::abs(leaf.height-oldHeight)<1e-5;}))
   return u8"原生文字存在混合字高，保留正文并单列";
  current.reset();if(std::abs(oldHeight-height)>1e-5)
   productStatus(editObject<AcDbEntity>(id)->transformBy(AcGeMatrix3d::scaling(height/oldHeight,*anchor)),"restore native text height about its source anchor");
 }
 const auto actual=open<AcDbEntity>(id);const auto at=productPlanNameAnchor(*actual);
 require(contents==productPlanNameContents(*actual)&&at&&at->distanceTo(*anchor)<.01,"typography edit changed original text or anchor");
 return {};
}

AcDbExtents productTypographyNoteEnvelope(const AcDbEntity& entity,AcDbExtents box){
 if(!AcDbMText::cast(&entity))return box;
 std::vector<ProductRoomLeaf> leaves;productRoomLeaves(entity,leaves);double width=0,height=0;
 for(const auto& leaf:leaves){width=std::max(width,leaf.text.size()*leaf.height*.75);height+=leaf.height*1.7;}
 const auto size=box.maxPoint()-box.minPoint();
 const AcGeVector3d margin(std::max(0.,width-size.x)*.5+150,std::max(0.,height-size.y)*.5+150,0);
 return AcDbExtents(box.minPoint()-margin,box.maxPoint()+margin);
}

std::vector<ProductTypographyIssue> productNormalizeWholePlanTypography(ProductJob& job,AcDbDatabase& db,AcDbDatabase& reference){
 Working working(&db);
 std::map<std::pair<std::string,std::wstring>,AcGePoint3d> captionCentres;
 for(const auto& [h,type]:modelTypes(db))if(type==L"TDbOpening"||type==L"TDbGroupOpening"){
  const auto entity=open<AcDbEntity>(find(db,ascii(h)));std::vector<ProductRoomLeaf> leaves;productRoomLeaves(*entity,leaves);
  for(const auto& leaf:leaves)if(productTypographyOpeningCode(leaf.text))if(const auto glyph=productGenericCaptionGlyph(*entity,leaf.text))
   captionCentres[{ascii(h),leaf.text}]=productGenericGlyphCentre(*glyph);
 }
 const bool templateMode=job.planMode=="template-finalize";
 const auto fonts=productRestorePlanTypography(db,reference,templateMode);
 if(!templateMode)productSynchronizePlanDimensionTable(job,db,reference,fonts.dim);
 std::vector<ProductTypographyIssue> issues;
 const auto rows=productTypographyInventory(db);
 std::map<std::string,std::pair<std::string,double>> requirements;std::set<std::string> conflicting,changed;
 for(const auto& row:rows)if(row.visible){
  const auto role=productTypographyRole(db,row);if(role!="general"&&role!="opening-label")continue;
  try{
   const auto target=(role=="general"?400.:row.type==L"TDbText"?(templateMode?238.:255.):(templateMode?280.:300.))*productTypographySpaceFactor(db,row)/job.unifiedUnitMM/productTypographyInstanceScale(db,row);
   const auto match=requirements.emplace(row.handle,std::make_pair(role,target));
   if(!match.second&&(match.first->second.first!=role||std::abs(match.first->second.second-target)>.01))conflicting.insert(row.handle);
  }catch(const std::exception& error){conflicting.insert(row.handle);issues.push_back({row.path,role,error.what()});}
 }
 for(const auto& row:rows)if(row.visible){
  const auto role=productTypographyRole(db,row);
  if(role!="general"&&role!="opening-label")continue;
  if(!changed.insert(row.handle).second)continue;
  try{
   require(!conflicting.count(row.handle),"shared text instances require different effective heights or roles");
   const auto problem=productTypographyRestyleText(db,row,role=="general"?fonts.text:fonts.caption,requirements.at(row.handle).second);
   if(!problem.empty())issues.push_back({row.path,role,problem});
  }catch(const std::exception& error){issues.push_back({row.path,role,error.what()});}
 }
 // Existing multi-line step notes were often laid out at 2.5 mm. At the
 // required 4 mm their boxes must be placed again, without shrinking the font.
 for(const auto& row:rows)if(row.visible&&row.type==L"AcDbMText"&&row.path==row.root&&productTypographySpaceFactor(db,row)==1.){
  const auto body=productTypographyText(row);
  if(body.size()<12&&body.find(L"级")==std::wstring::npos&&body.find(L"台阶")==std::wstring::npos&&body.find(L"详见")==std::wstring::npos)continue;
  if(productTypographyRole(db,row)!="general")continue;
  const auto id=find(db,row.handle);AcDbExtents originalBox;
  {const auto note=open<AcDbEntity>(id);if(note->getGeomExtents(originalBox)!=Acad::eOk)continue;
   originalBox=productTypographyNoteEnvelope(*note,originalBox);}
  std::vector<AcDbExtents> obstacles;
  std::optional<AcDbExtents> frame;
  const auto middle=originalBox.minPoint()+(originalBox.maxPoint()-originalBox.minPoint())*.5;
  for(const auto& [panel,sheet]:job.planSheetLayouts){const auto view=open<AcDbViewport>(find(db,sheet.viewport));
   if(std::abs(view->twistAngle())>1e-9||view->height()<=0)continue;
   const double height=view->viewHeight(),width=height*view->width()/view->height();
   const auto centre=view->viewTarget()+AcGeVector3d(view->viewCenter().x,view->viewCenter().y,0);
   if(std::abs(middle.x-centre.x)<=width*.5&&std::abs(middle.y-centre.y)<=height*.5)
    frame=AcDbExtents(centre-AcGeVector3d(width*.5,height*.5,0),centre+AcGeVector3d(width*.5,height*.5,0));
  }
  for(const auto& other:productTypographyInventory(db))if(other.visible&&other.root!=row.root&&other.bounds){
   const auto entity=open<AcDbEntity>(find(db,other.handle));
   obstacles.push_back(other.path==other.root?productTypographyNoteEnvelope(*entity,*other.bounds):*other.bounds);
  }
  for(const auto& [h,type]:modelTypes(db))if(type==L"TDbWall"||type==L"TDbColumn"||type==L"TDbStair"||
     type==L"TDbAxisLabelSet"||type==L"TDbSymbElevation"){
   const auto entity=open<AcDbEntity>(find(db,ascii(h)));AcDbExtents box;
   if(productPlanDimensionVisible(*entity)&&entity->getGeomExtents(box)==Acad::eOk)obstacles.push_back(box);
  }
  const auto clear=[&](const AcGeVector3d& delta){auto lo=originalBox.minPoint()+delta,hi=originalBox.maxPoint()+delta;
   if(frame&&(lo.x<frame->minPoint().x||lo.y<frame->minPoint().y||hi.x>frame->maxPoint().x||hi.y>frame->maxPoint().y))return false;
   for(const auto& box:obstacles)if(hi.x+100>box.minPoint().x&&lo.x-100<box.maxPoint().x&&hi.y+100>box.minPoint().y&&lo.y-100<box.maxPoint().y)return false;
   return true;
  };
  if(clear({}))continue;
  std::vector<AcGeVector3d> candidates;
  for(int x=-20;x<=20;++x)for(int y=-20;y<=20;++y)if(x||y)candidates.emplace_back(x*400.,y*400.,0.);
  std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.lengthSqrd()<b.lengthSqrd();});
  const auto found=std::find_if(candidates.begin(),candidates.end(),clear);
  if(found!=candidates.end())productStatus(editObject<AcDbEntity>(id)->transformBy(AcGeMatrix3d::translation(*found)),"place enlarged step note clear of actual annotations");
  else issues.push_back({row.path,"note-clearance",u8"按4.0字高放大后，同图幅邻近8米范围内未找到不压文字、尺寸、轴号和墙体的位置"});
 }
 for(const auto& [handle,type]:modelTypes(db))if(type==L"TDbOpening"||type==L"TDbGroupOpening"){
  const auto id=find(db,ascii(handle));auto entity=open<AcDbEntity>(id);if(!productPlanDimensionVisible(*entity))continue;
  std::vector<ProductRoomLeaf> leaves;productRoomLeaves(*entity,leaves);
  for(const auto& leaf:leaves)if(productTypographyOpeningCode(leaf.text)){
   try{
    const auto glyph=productGenericCaptionGlyph(*entity,leaf.text);require(glyph.has_value(),"native opening label glyph unavailable");
    const auto captionLayer=productPlanLayer(db,templateMode?L"A-WINDOW-TEXT":L"A-WINDOW",7);
    if(glyph->style==fonts.caption&&glyph->layer==captionLayer){
     entity.reset();productGenericMoveCaption(id,leaf.text,captionCentres.at({ascii(handle),leaf.text}));entity=open<AcDbEntity>(id);continue;
    }
    GenericOpeningCaption caption;caption.id=id;caption.number=leaf.text;caption.desired=productGenericGlyphCentre(*glyph);
    caption.vertical=std::abs(std::sin(glyph->angle))>.5;
    entity.reset();
    require(productRemapCaption(db,caption,*glyph,captionLayer,fonts.caption,false,0.,false),"native opening label mapping did not preserve its geometry");
    productGenericMoveCaption(id,leaf.text,captionCentres.at({ascii(handle),leaf.text}));
    entity=open<AcDbEntity>(id);
   }catch(const std::exception& error){issues.push_back({ascii(handle),"native-opening-label",error.what()});}
  }
 }
 return issues;
}

std::vector<ProductTypographyIssue> productAuditWholePlanTypography(ProductJob& job,AcDbDatabase& db,AcDbDatabase& reference,
 const fs::path& drawing,const fs::path& report){
 Working working(&db);ProductTypographyFonts fonts;
 fonts.dimension={L"_TCH_DIM",L"simplex.shx",L"gbcbig.shx",1.,0.,0};fonts.general={L"FY_HZ",L"tssdeng.shx",L"hztxt.shx",.7,0.,0};
 fonts.opening=productTypographyOpeningFont(reference);
 const auto styleTable=productReadPlanDimensionTable(db,job.dimensionStylePolicy.name,fonts.dimension);
 std::vector<ProductTypographyIssue> issues;std::ostringstream out;out<<std::setprecision(17)<<"{\"outputSHA256\":\""<<sha(drawing)
  <<"\",\"savedReopened\":true,\"independentInventory\":true,\"dimensionStyleTable\":";
 productWritePlanDimensionTable(out,styleTable);
 out<<",\"objects\":[";bool first=true;std::size_t checked=0,special=0;
 const auto record=[&](const std::string& path,const std::string& role,const std::vector<ProductRoomLeaf>& leaves,
  const ProductDimensionFont* expected,double height,double fixed,const std::string& reason){
  bool good=reason.empty()&&!leaves.empty();
  if(!first)out<<',';first=false;out<<"{\"object\":\""<<path<<"\",\"role\":\""<<role<<"\",\"expectedHeight\":"<<height<<",\"glyphs\":[";
  for(std::size_t i=0;i<leaves.size();++i){if(i)out<<',';const auto& leaf=leaves[i];
   const auto actual=leaf.style.isNull()?ProductDimensionFont{}:productReadDimensionFont(leaf.style);
   const bool font=expected&&productTypographyFontMatches(leaf.style,*expected,fixed);
   good=good&&font&&std::abs(leaf.height-height)<.01;
   out<<"{\"text\":"<<jsonUtf8(leaf.text)<<",\"style\":"<<jsonUtf8(actual.name)<<",\"font\":"<<jsonUtf8(actual.primary)
    <<",\"bigFont\":"<<jsonUtf8(actual.big)<<",\"height\":"<<leaf.height<<",\"definitionVerified\":"<<(font?"true":"false")<<'}';
  }
  auto problem=reason;if(!good&&problem.empty())problem=u8"实际字体定义、引用或有效字高与适用规则不符";
  out<<"],\"verified\":"<<(good?"true":"false")<<",\"reason\":"<<jsonUtf8(bcpproduct::decodeMessage(problem))<<'}';
  ++checked;if(!good)issues.push_back({path,role,problem});
 };
 for(const auto& row:styleTable)if(!row.reason.empty())
  record(ascii(row.name),"dimension-style-table",{},nullptr,0,0,row.reason);
 for(const auto& row:productTypographyInventory(db))if(row.visible){
  const auto role=productTypographyRole(db,row);
  if(role.rfind("special-",0)==0){++special;if(!first)out<<',';first=false;
   out<<"{\"object\":\""<<row.path<<"\",\"role\":\""<<role<<"\",\"applicability\":\"separate-specialist-rule\"}";continue;}
  const auto entity=open<AcDbEntity>(find(db,row.handle));
  try{
   if(role=="dimension"){
    const auto parts=productDimensionTexts(*entity);std::vector<ProductRoomLeaf> leaves;
    for(const auto& part:parts)leaves.push_back({L"dimension",part.font,0,part.height*part.scale,{}});
    const bool family=!parts.empty()&&std::all_of(parts.begin(),parts.end(),[&](const auto& part){return productRequiredDimensionTypography(part,job,fonts.dimension);});
    record(row.path,role,leaves,&fonts.dimension,280./job.unifiedUnitMM,0,family?"":u8"实际尺寸样式、比例、测量系数或文字定义不符");
   }else if(role=="general"||role=="opening-label"){
    std::vector<ProductRoomLeaf> leaves;productRoomLeaves(*entity,leaves);const auto scale=productTypographyInstanceScale(db,row);
    for(auto& leaf:leaves)leaf.height*=scale;
    std::string issue;
    if(const auto* text=AcDbMText::cast(entity.get()))if(!productPlanMTextUsesStyle(*text))issue=u8"多行文字仍存在行内字体覆盖";
    record(row.path,role,leaves,role=="general"?&fonts.general:&fonts.opening,(role=="general"?400.:row.type==L"TDbText"?255.:300.)*productTypographySpaceFactor(db,row)/job.unifiedUnitMM,role=="general"?0.:3.,issue);
   }else if(role=="linked-note"){
    std::vector<ProductRoomLeaf> leaves;productRoomLeaves(*entity,leaves);
    record(row.path,role,leaves,&fonts.general,400./job.unifiedUnitMM,0,
     u8"当前公开修订入口不支持独立改写关联引注内部字高；保留原引线、锚点和文字，未计为字体通过");
   }else record(row.path,role,{},nullptr,0,0,u8"该标注的用途或原生内部字形尚未完成可靠归类及改写");
  }catch(const std::exception& error){record(row.path,role,{},nullptr,0,0,error.what());}
 }
 for(const auto& [handle,type]:modelTypes(db))if(type==L"TDbOpening"||type==L"TDbGroupOpening"){
  const auto entity=open<AcDbEntity>(find(db,ascii(handle)));if(!productPlanDimensionVisible(*entity))continue;
  std::vector<ProductRoomLeaf> leaves;productRoomLeaves(*entity,leaves);
  for(const auto& leaf:leaves)if(!leaf.text.empty())record(ascii(handle),"native-opening-label",{leaf},&fonts.opening,255./job.unifiedUnitMM,3.,
   productTypographyOpeningCode(leaf.text)?std::string{}:u8"洞口内部可见文字未与标准门窗编码匹配，未计为编号通过");
 }
 out<<"],\"checked\":"<<checked<<",\"specialistObjects\":"<<special<<",\"unresolved\":"<<issues.size()<<",\"allApplicableVerified\":"<<(issues.empty()?"true":"false")<<'}';
 writeNew(report,out.str());
 for(const auto& issue:issues)job.drawingChecks.record(0,"final-whole-drawing-typography",issue.object,{"Y10","N06","L08","G06"},
  bcpproduct::PlanReleaseGate::reopened,bcpproduct::PlanCheck::failed,u8"最终全图独立逐对象字体与字高复核",issue.reason);
 return issues;
}
