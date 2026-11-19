// Missing dependencies are never repaired by replacing an unknown font.
// Only styles explicitly declared purgeable by AutoCAD may be retired.
std::optional<fs::path> productResolveExactPlanFont(AcDbDatabase& db,const std::wstring& file){
 if(file.empty())return {};
 const fs::path requested(file);
 const auto expected=[](fs::path path){if(path.extension().empty())path+=L".shx";return path.filename().wstring();}(requested);
 const auto accept=[&](const fs::path& path)->std::optional<fs::path>{
  std::error_code error;
  if(!path.is_absolute()||_wcsicmp(path.filename().c_str(),expected.c_str())||!fs::is_regular_file(path,error))return {};
  return path;
 };
 if(const auto direct=accept(requested))return direct;
 AcString resolved;
 if(acdbHostApplicationServices()->findFile(resolved,file.c_str(),&db,AcDbHostApplicationServices::kFontFile)==Acad::eOk)
  if(const auto found=accept(fs::path(resolved.kACharPtr())))return found;
 if(const auto local=productRoomFontFile(db,AcString(file.c_str()),false))
  if(const auto found=accept(fs::path(*local)))return found;
 const auto extension=requested.extension().wstring();
 if(requested==requested.filename()&&(!_wcsicmp(extension.c_str(),L".ttf")||!_wcsicmp(extension.c_str(),L".ttc")||!_wcsicmp(extension.c_str(),L".otf"))){
  wchar_t directory[MAX_PATH];const auto count=GetWindowsDirectoryW(directory,MAX_PATH);
  if(count>0&&count<MAX_PATH)return accept(fs::path(directory)/L"Fonts"/requested);
 }
 return {};
}

struct ProductMissingFontStyle {std::string handle;std::wstring name,primary,big;};
struct ProductFontDependencyCleanup {
 std::vector<ProductMissingFontStyle> retired;
 std::vector<ProductTypographyIssue> issues;
};

std::vector<ProductMissingFontStyle> productMissingPlanFonts(AcDbDatabase& db){
 std::vector<ProductMissingFontStyle> result;
 const auto table=open<AcDbTextStyleTable>(db.textStyleTableId());
 AcDbTextStyleTableIterator* raw{};status(table->newIterator(raw),"read font dependency styles");
 std::unique_ptr<AcDbTextStyleTableIterator> it(raw);
 for(;!it->done();it->step()){
  AcDbObjectId id;status(it->getRecordId(id),"read font dependency style id");
  const auto font=productReadDimensionFont(id);
  const bool primary=font.primary.empty()||productResolveExactPlanFont(db,font.primary).has_value();
  const bool big=font.big.empty()||productResolveExactPlanFont(db,font.big).has_value();
  if(!primary||!big)result.push_back({axisHandle(id),font.name,font.primary,font.big});
 }
 return result;
}

ProductFontDependencyCleanup productCleanUnusedMissingPlanFonts(AcDbDatabase& db){
 Working context(&db);ProductFontDependencyCleanup result;
 const auto missing=productMissingPlanFonts(db);AcDbObjectIdArray unused;
 if(missing.empty())return result;
 for(const auto& row:missing)unused.append(find(db,row.handle));
 status(db.purge(unused),"identify purgeable missing-font styles");
 for(int k=0;k<unused.length();++k){
  const auto key=axisHandle(unused[k]);
  const auto original=std::find_if(missing.begin(),missing.end(),[&](const auto& row){return row.handle==key;});
  if(original==missing.end())continue;
  try{
   status(editObject<AcDbTextStyleTableRecord>(unused[k])->erase(),"retire purgeable missing-font style");
   result.retired.push_back(*original);
  }catch(const std::exception& e){result.issues.push_back({key,"font-dependency",e.what()});}
 }
 return result;
}

std::vector<ProductTypographyIssue> productAuditPlanFontDependencies(AcDbDatabase& saved,
 const ProductFontDependencyCleanup& cleanup,const fs::path& drawing,const fs::path& report){
 Working context(&saved);auto issues=cleanup.issues;const auto missing=productMissingPlanFonts(saved);
 std::ostringstream out;out<<"{\"contract\":\"plan.font-dependencies.v1\",\"savedReopened\":true,\"outputSHA256\":\""<<sha(drawing)
  <<"\",\"allStylesEnumerated\":true,\"retired\":[";
 bool first=true;
 const auto write=[&](const auto& row){if(!first)out<<',';first=false;
  out<<"{\"handle\":\""<<row.handle<<"\",\"name\":"<<jsonUtf8(row.name)<<",\"primary\":"<<jsonUtf8(row.primary)<<",\"big\":"<<jsonUtf8(row.big)<<'}';};
 for(const auto& row:cleanup.retired){
  AcDbObjectId id;const bool absent=saved.getAcDbObjectId(id,false,AcDbHandle(bcpproduct::decodeMessage(row.handle).c_str()))!=Acad::eOk||id.isErased();
  if(!absent)issues.push_back({row.handle,"font-dependency",u8"已清理的未引用缺字体样式在独立重开后仍存在"});
  if(!first)out<<',';first=false;
  out<<"{\"handle\":\""<<row.handle<<"\",\"name\":"<<jsonUtf8(row.name)<<",\"primary\":"<<jsonUtf8(row.primary)
   <<",\"big\":"<<jsonUtf8(row.big)<<",\"absent\":"<<(absent?"true":"false")<<'}';
 }
 out<<"],\"unresolved\":[";first=true;
 for(const auto& row:missing){
  write(row);issues.push_back({row.handle,"font-dependency",u8"实际样式仍引用不可解析的字体资源，保留原定义；未用替代字体冒充修复"});
 }
 out<<"],\"issues\":[";first=true;
 for(const auto& issue:issues){if(!first)out<<',';first=false;
  out<<"{\"object\":"<<jsonUtf8(bcpproduct::decodeMessage(issue.object))<<",\"role\":"<<jsonUtf8(bcpproduct::decodeMessage(issue.role))
   <<",\"reason\":"<<jsonUtf8(bcpproduct::decodeMessage(issue.reason))<<'}';}
 out<<"]}";writeNew(report/L"plan-font-dependencies-readback.json",out.str());return issues;
}
