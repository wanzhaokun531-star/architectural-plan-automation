// Owned regression only. The source is read-only; no drawing is plotted.
BOOL CALLBACK productWidthProbeFieldNames(HWND h,LPARAM data){
 auto& names=*reinterpret_cast<std::vector<std::wstring>*>(data);
 if(productControlClass(h)!=L"ListBox"||!(GetWindowLongPtrW(h,GWL_STYLE)&LBS_HASSTRINGS))return TRUE;
 const auto count=SendMessageW(h,LB_GETCOUNT,0,0);if(count<1||count>500)return TRUE;
 for(int k=0;k<count;++k){const auto n=SendMessageW(h,LB_GETTEXTLEN,k,0);if(n<0||n>2000)continue;
  std::wstring name(static_cast<std::size_t>(n)+1,0);SendMessageW(h,LB_GETTEXT,k,reinterpret_cast<LPARAM>(name.data()));name.resize(static_cast<std::size_t>(n));names.push_back(name);}
 return TRUE;
}
void productWidthProbePublicFields(AcDbDatabase& db,const fs::path& report,const std::set<std::string>& handles){
 const auto fields=productReadProperties(db,report/L"native-text-properties",&handles);
 std::ostringstream out;out<<"{\"objects\":[";bool first=true;
 for(const auto& handle:handles){if(!first)out<<',';first=false;
  WholeSelection selected(find(db,handle));require(acedSSSetFirst(selected.value,nullptr)==RTNORM,"width probe exact selection");
  require(acedCommandS(RTSTR,L"_.PROPERTIES",RTNONE)==RTNORM,"width probe own public properties");productOwnControlMessages();
  HWND panel{};EnumDesktopWindows(GetThreadDesktop(GetCurrentThreadId()),productFindProperties,reinterpret_cast<LPARAM>(&panel));
  std::vector<std::wstring> names;if(panel)EnumChildWindows(panel,productWidthProbeFieldNames,reinterpret_cast<LPARAM>(&names));
  out<<"{\"object\":\""<<handle<<"\",\"fields\":[";bool firstName=true;for(const auto& name:names){if(!firstName)out<<',';firstName=false;out<<jsonUtf8(name);}out<<"],\"listProperties\":{";
  firstName=true;if(fields.count(handle))for(const auto& value:fields.at(handle)){if(!firstName)out<<',';firstName=false;out<<jsonUtf8(value.first)<<':'<<jsonUtf8(value.second);}out<<"}}";
  acedSSSetFirst(nullptr,nullptr);
 }
 acedCommandS(RTSTR,L"_.PROPERTIESCLOSE",RTNONE);out<<"]}";writeNew(report/L"native-text-public-fields.json",out.str());
}
void productProbeTypographyWidth(const fs::path& source,const fs::path& output,const fs::path& report,const fs::path& resources){
 isolated(ini(iniPath(),L"nonce"));require(curDoc()&&fs::equivalent(source,curDoc()->fileName())&&!fs::exists(output)&&!bcpLoaded(),"owned typography width probe input");
 require(!productJob,"width probe owns exclusive job");productJob=std::make_unique<ProductJob>();struct Reset{~Reset(){productJob.reset();}}reset;
 auto& job=*productJob;job.profile=resources;job.root=report;job.ownedDocument=curDoc();job.currentPlanRules=true;job.architecturalPlanOnly=true;job.unifiedScale=100;job.unifiedUnitMM=1;
 const auto sourceHash=sha(source);auto& db=*curDoc()->database();Working working(&db);
 productWidthProbePublicFields(db,report,{"42EF","42EC"});
 struct Item {std::string handle,role,reason;std::wstring type;double before{},after{},target{},height{};bool good{};};std::vector<Item> items;
 for(const auto& row:productTypographyInventory(db))if(row.visible){
  const auto role=productTypographyRole(db,row);if(role!="general"&&role!="opening-label")continue;
  std::vector<ProductRoomLeaf> leaves;{const auto object=open<AcDbEntity>(find(db,row.handle));productRoomLeaves(*object,leaves);}
  if(leaves.size()!=1)continue;const auto font=productCurrentTypographyPolicy(job).at(role=="general"?"FY_HZ":"_TCH_WINDOW");
  if(std::abs(leaves[0].width-font.width)<.001)continue;
  Item item{row.handle,role,{},row.type,leaves[0].width,0,font.width,leaves[0].height,false};
  try{item.reason=productTypographyNativeWidth(db,find(db,row.handle),font.width);
   leaves.clear();productRoomLeaves(*open<AcDbEntity>(find(db,row.handle)),leaves);item.after=leaves.empty()?0:leaves[0].width;
   item.good=item.reason.empty()&&leaves.size()==1&&std::abs(item.after-item.target)<.001&&std::abs(leaves[0].height-item.height)<.001;
  }catch(const std::exception& e){item.reason=e.what();}items.push_back(std::move(item));
 }
 status(db.saveAs(output.c_str(),false,AcDb::kDHL_CURRENT),"save typography width regression copy");
 AcDbDatabase reopened(false,true);readDrawing(reopened,output);
 std::ostringstream out;out<<std::setprecision(12)<<"{\"completed\":true,\"sourceSHA256\":\""<<sourceHash<<"\",\"outputSHA256\":\""<<sha(output)<<"\",\"savedReopened\":true,\"objects\":[";bool first=true;
 for(auto& item:items){std::vector<ProductRoomLeaf> leaves;productRoomLeaves(*open<AcDbEntity>(find(reopened,item.handle)),leaves);
  const double actual=leaves.empty()?0:leaves[0].width;item.good=item.good&&leaves.size()==1&&std::abs(actual-item.target)<.001&&std::abs(leaves[0].height-item.height)<.001;
  if(!first)out<<',';first=false;out<<"{\"object\":\""<<item.handle<<"\",\"type\":"<<jsonUtf8(item.type)<<",\"role\":\""<<item.role<<"\",\"before\":"<<item.before<<",\"after\":"<<item.after<<",\"reopenedActual\":"<<actual<<",\"target\":"<<item.target<<",\"verified\":"<<(item.good?"true":"false")<<",\"reason\":"<<jsonUtf8(bcpproduct::decodeMessage(item.reason))<<'}';
 }
 require(sha(source)==sourceHash,"width probe source changed");out<<"],\"sourceUnchanged\":true,\"pdfPrinted\":false}";
 writeNew(report/L"typography-width-probe.json",out.str());writeNew(report/L"product-result.json",out.str());
}
