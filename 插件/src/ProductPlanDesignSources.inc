// O09 / D31: source-bound adopted dimensions and per-object wall purposes.
// Edits use the owned document's public Properties controls, never private fields.
struct ProductDesignOpening {
 std::string handle,anchor,prefix,file,locator,basis,issue,number;
 double widthMM{},heightMM{},originalWidth{},originalHeight{},sill{};
 AcGePoint3d beforeA,beforeB,expectedA,expectedB;
 bool changed{},verified{};
};
struct ProductDesignSources {
 std::string hash;std::map<std::string,std::pair<fs::path,std::string>> files;
 std::vector<ProductDesignOpening> openings;
 std::map<std::string,std::pair<std::string,std::string>> walls;
 std::vector<std::pair<std::string,std::string>> pending;
 std::map<std::string,std::string> boundaryDimensions;
};

ProductDesignSources productReadDesignSources(ProductJob& job){
 ProductDesignSources out;
 const auto path=job.unifiedDesignSources;
 if(path.empty())return out;
 std::ifstream stream(path);std::string tag,hash,line;
 require(bool(stream>>tag>>hash)&&tag=="PLANDESIGN1"&&hash==job.sourceHash,
   "design adoption must bind this exact source DWG");
 out.hash=sha(path);std::getline(stream,line);std::set<std::string> openings;
 bool ended=false;
 while(std::getline(stream,line)){
  std::istringstream row(line);require(bool(row>>tag),"nonempty source adoption row");
  if(tag=="END"){row>>std::ws;stream>>std::ws;require(row.eof()&&stream.eof(),"source adoption trailing fields");ended=true;break;}
  if(tag=="FILE"){
   std::string key,digest,file;require(bool(row>>key>>digest>>std::quoted(file)),"design file reference");
   const auto actual=fs::u8path(file);require(fs::is_regular_file(actual)&&sha(actual)==digest&&
    out.files.emplace(key,std::make_pair(actual,digest)).second,"design evidence file changed or duplicated");
  }else if(tag=="OPENING"){
   ProductDesignOpening item;
   require(bool(row>>item.handle>>item.widthMM>>item.heightMM>>item.anchor>>item.prefix>>item.file>>
    std::quoted(item.locator)>>std::quoted(item.basis)),"complete adopted opening row");
   require(std::isfinite(item.widthMM)&&item.widthMM>0&&std::isfinite(item.heightMM)&&item.heightMM>0&&
    (item.anchor=="CENTER"||item.anchor=="START"||item.anchor=="END")&&!item.basis.empty()&&!item.locator.empty()&&
    std::set<std::string>{"M","LM","LC","FMD","DK"}.count(item.prefix)&&openings.insert(item.handle).second,
    "valid unique adopted opening and explicit locating basis");out.openings.push_back(std::move(item));
  }else if(tag=="WALL"){
   std::string handle,role,basis;require(bool(row>>handle>>role>>std::quoted(basis))&&
    (role=="MAIN"||role=="BOUNDARY")&&!basis.empty()&&out.walls.emplace(handle,std::make_pair(role,basis)).second,
    "unique source wall purpose and basis");
  }else if(tag=="PENDING"){
   std::string object,reason;require(bool(row>>std::quoted(object)>>std::quoted(reason))&&!reason.empty(),"explicit unresolved source item");
   out.pending.emplace_back(object,reason);
  }else require(false,"unknown design adoption row");
  row>>std::ws;require(row.eof(),"unexpected adoption field");
 }
 require(ended,"design adoption END missing");
 for(const auto& item:out.openings)require(out.files.count(item.file),"adopted opening lacks supplied file evidence");
 return out;
}

struct ProductNamedOpeningField:ProductHeightField {std::wstring name;};
BOOL CALLBACK productFindOpeningField(HWND h,LPARAM data){
 auto& p=*reinterpret_cast<ProductNamedOpeningField*>(data);
 if(productControlClass(h)!=L"ListBox"||!(GetWindowLongPtrW(h,GWL_STYLE)&LBS_HASSTRINGS))return TRUE;
 const auto count=SendMessageW(h,LB_GETCOUNT,0,0);if(count<1||count>500)return TRUE;
 for(int k=0;k<count;++k){const auto n=SendMessageW(h,LB_GETTEXTLEN,k,0);if(n<0||n>2000)continue;
  std::wstring text(static_cast<std::size_t>(n)+1,0);SendMessageW(h,LB_GETTEXT,k,reinterpret_cast<LPARAM>(text.data()));
  text.resize(static_cast<std::size_t>(n));if(text==p.name){p.list=h;p.index=k;return FALSE;}}
 return TRUE;
}

void productSetOpeningPublicField(AcDbDatabase& db,AcDbObjectId id,const wchar_t* fieldName,double value){
 require(curDoc()&&curDoc()->database()==&db&&productJob&&curDoc()==productJob->ownedDocument,
   "opening edit requires the current owned native document");
 HWND panel{};EnumDesktopWindows(GetThreadDesktop(GetCurrentThreadId()),productFindProperties,reinterpret_cast<LPARAM>(&panel));
 const bool visible=panel!=nullptr;resbuf before{},pick{};require(acedGetVar(L"PICKFIRST",&before)==RTNORM&&before.restype==RTSHORT,"read opening pickfirst");
 struct Restore {bool visible;resbuf before;~Restore(){acedSSSetFirst(nullptr,nullptr);if(!visible)acedCommandS(RTSTR,L"_.PROPERTIESCLOSE",RTNONE);acedSetVar(L"PICKFIRST",&before);}} restore{visible,before};
 pick.restype=RTSHORT;pick.resval.rint=1;require(acedSetVar(L"PICKFIRST",&pick)==RTNORM,"opening exact selection enabled");
 WholeSelection selection(id);require(acedSSSetFirst(selection.value,nullptr)==RTNORM,"select adopted opening only");
 require(acedCommandS(RTSTR,L"_.PROPERTIES",RTNONE)==RTNORM,"open native properties");productOwnControlMessages();
 panel=nullptr;EnumDesktopWindows(GetThreadDesktop(GetCurrentThreadId()),productFindProperties,reinterpret_cast<LPARAM>(&panel));require(panel,"owned properties panel unavailable");
 ProductNamedOpeningField named;named.name=fieldName;EnumChildWindows(panel,productFindOpeningField,reinterpret_cast<LPARAM>(&named));
 require(named.list&&named.index>=0,"public native width/height field unavailable");
 ProductHeightField& field=named;RECT client{};GetClientRect(field.list,&client);SendMessageW(field.list,LB_GETITEMRECT,field.index,reinterpret_cast<LPARAM>(&field.row));
 if(field.row.top<0||field.row.bottom>client.bottom){SendMessageW(field.list,LB_SETTOPINDEX,std::max(0,field.index-4),0);SendMessageW(field.list,LB_GETITEMRECT,field.index,reinterpret_cast<LPARAM>(&field.row));}
 EnumChildWindows(field.list,productFindHeightCell,reinterpret_cast<LPARAM>(&field));require(field.cell,"named opening value cell unavailable");
 RECT box{};GetClientRect(field.cell,&box);const auto point=MAKELPARAM((box.right-box.left)/2,(box.bottom-box.top)/2);
 SetFocus(field.cell);SendMessageW(field.cell,WM_LBUTTONDOWN,MK_LBUTTON,point);SendMessageW(field.cell,WM_LBUTTONUP,0,point);
 EnumChildWindows(panel,productFindHeightEdit,reinterpret_cast<LPARAM>(&field));
 if(field.edits!=1){SendMessageW(field.cell,WM_LBUTTONDBLCLK,MK_LBUTTON,point);SendMessageW(field.cell,WM_LBUTTONUP,0,point);field.edits=0;field.edit=nullptr;EnumChildWindows(panel,productFindHeightEdit,reinterpret_cast<LPARAM>(&field));}
 require(field.edits==1&&field.edit,"unique native opening numeric editor unavailable");const auto text=std::to_wstring(value);
 require(SendMessageW(field.edit,WM_SETTEXT,0,reinterpret_cast<LPARAM>(text.c_str()))!=0,"native opening value rejected");
 SendMessageW(field.edit,WM_KEYDOWN,VK_RETURN,0);SendMessageW(field.edit,WM_KEYUP,VK_RETURN,0);SetFocus(field.list);productOwnControlMessages();
}

ProductDesignSources productApplyDesignSources(ProductJob& job,AcDbDatabase& db,GenericNativeInput& input){
 auto run=productReadDesignSources(job);job.planBoundaryWalls.clear();job.planAdoptedOpeningNumbers.clear();
 for(const auto& item:run.pending)job.detailPending.push_back({u8"门窗资料或构件用途",item.first,item.second});
 for(const auto& item:run.walls){
  const auto found=std::find_if(input.walls.begin(),input.walls.end(),[&](const auto& w){return w.wall.handle==item.first;});
  if(found==input.walls.end()){job.detailPending.push_back({u8"主体围墙用途",item.first,u8"绑定墙句柄不可读，未扩大到整组件"});continue;}
  if(item.second.first=="BOUNDARY")job.planBoundaryWalls.insert(item.first);
  productRecordRuleEvidence(job,"D31","{\"object\":\""+item.first+"\",\"role\":\""+item.second.first+"\",\"result\":\"source-bound-wall-purpose\"}");
 }
 // A pier touching a boundary is not automatically a building column. Keep
 // ambiguous shared supports, but never silently certify their main-grid use.
 for(const auto& column:input.columns){bool touches=false;
  for(const auto& wall:input.walls)if(job.planBoundaryWalls.count(wall.wall.handle)){
   const AcGePoint3d a(wall.wall.start.x,wall.wall.start.y,0),b(wall.wall.end.x,wall.wall.end.y,0);
   const auto direction=b-a;const double length2=direction.dotProduct(direction);if(length2<=1e-12)continue;
   const AcGePoint3d centre(column.centre.x,column.centre.y,0);
   const auto t=std::clamp((centre-a).dotProduct(direction)/length2,0.,1.);
   touches|=centre.distanceTo(a+direction*t)<=wall.total*.5+std::max(column.halfX,column.halfY)+.01/job.unifiedUnitMM;
  }
  if(touches)job.detailPending.push_back({u8"围墙关联柱用途",column.handle,
   u8"柱与已确认围墙相交，尚无逐构件用途依据；保留柱及已有主体定位，未计为围墙构件分离通过"});
 }
 if(run.openings.empty())return run;
 const double eps=.01/job.unifiedUnitMM;
 for(auto& adopted:run.openings){
  const auto source=std::find_if(input.openings.begin(),input.openings.end(),[&](const auto& o){return o.handle==adopted.handle;});
  bool transaction=false;std::map<std::string,std::vector<AcGePoint3d>> protectedGrips;std::set<AcDbObjectId> rootIds;
  try{
   require(source!=input.openings.end()&&!source->grouped,"adopted opening requires a readable single native opening; group leaves need a verified adapter");
   adopted.beforeA=source->a;adopted.beforeB=source->b;adopted.originalWidth=source->width;adopted.originalHeight=source->height;adopted.sill=source->sill;
   require(std::abs(source->a.distanceTo(source->b)-source->width)<eps,"source opening endpoints disagree with native width");
   const auto prefix=bcpproduct::decodeMessage(adopted.prefix);const auto number=productGenericOpeningCode(prefix.c_str(),adopted.widthMM,adopted.heightMM);
   require(!number.empty(),"adopted dimensions lack a supported numbering code");
   require((adopted.prefix=="LC")==source->window,"adopted category conflicts with native door/window type");
   const auto direction=(source->b-source->a).normal();const double width=adopted.widthMM/job.unifiedUnitMM,height=adopted.heightMM/job.unifiedUnitMM;
   adopted.expectedA=adopted.anchor=="START"?source->a:adopted.anchor=="END"?source->b-direction*width:source->a+(source->b-source->a)*.5-direction*(width*.5);
   adopted.expectedB=adopted.expectedA+direction*width;
   for(const auto& w:input.walls)protectedGrips.emplace(w.wall.handle,chainGrips(find(db,w.wall.handle)));
   for(const auto& o:input.openings)if(o.handle!=adopted.handle)protectedGrips.emplace(o.handle,chainGrips(find(db,o.handle)));
   rootIds=wholeModelIds(db);auto* manager=db.transactionManager();require(manager->numActiveTransactions()==0&&manager->startTransaction(),"start adopted opening transaction");transaction=true;
   const auto id=find(db,adopted.handle);
   if(std::abs(source->width-width)>eps)productSetOpeningPublicField(db,id,L"宽度",width);
   if(std::abs(source->height-height)>eps)productSetOpeningPublicField(db,id,L"高度",height);
   auto gp=chainGrips(id);require(gp.size()>=3,"changed opening endpoint topology");
   const AcGeVector3d shift(adopted.expectedA.x-gp[1].x,adopted.expectedA.y-gp[1].y,0);
   if(shift.length()>eps){auto e=editObject<AcDbEntity>(id);status(e->transformBy(AcGeMatrix3d::translation(shift)),"preserve adopted opening locating anchor");}
   gp=chainGrips(id);require(std::hypot(gp[1].x-adopted.expectedA.x,gp[1].y-adopted.expectedA.y)<eps&&
    std::hypot(gp[2].x-adopted.expectedB.x,gp[2].y-adopted.expectedB.y)<eps,"native opening plane geometry differs from adopted width/anchor");
   const auto commandModule=fs::path(productSetting(L"tangentDirectory",L"C:\\Tangent\\TArchT20V8\\SYS23x64"))/L"tch_kerncmd.arx";
   if(!acedRegCmds->lookupGlobalCmd(L"TChWinLab"))require(acrxDynamicLinker->loadModule(commandModule.c_str(),false,false),"load native opening numbering command");
   WholeSelection exact(id);require(acedCommandS(RTSTR,L"TChWinLab",RTPICKS,exact.value,RTSTR,L"",RTSTR,number.c_str(),RTNONE)==RTNORM,"write adopted native opening number");
   const std::set<std::string> selected{adopted.handle};const auto props=productReadProperties(db,job.root/L"design-source-fields"/(bcpproduct::decodeMessage(adopted.handle)+L"-"+std::to_wstring(GetTickCount64())),&selected).at(adopted.handle);
   require(std::abs(bcpproduct::propertyNumber(props,L"宽度")-width)<eps&&std::abs(bcpproduct::propertyNumber(props,L"高度")-height)<eps&&
    std::abs(bcpproduct::propertyNumber(props,L"离地高度")-source->sill)<eps&&props.at(L"门窗类型")==source->kind&&props.at(L"门窗编号")==number,"native opening adopted public fields differ");
   require(wholeModelIds(db)==rootIds,"opening edit changed model membership");
   for(const auto& p:protectedGrips)require(approvedgrips::same(chainGrips(find(db,p.first)),p.second),"opening edit moved another opening or wall baseline");
   status(manager->endTransaction(),"commit adopted opening dimensions");transaction=false;
   adopted.changed=std::abs(source->width-width)>eps||std::abs(source->height-height)>eps;
   source->a=adopted.expectedA;source->b=adopted.expectedB;source->width=width;source->height=height;source->existing=number;
   input.properties[adopted.handle]=props;job.planAdoptedOpeningNumbers[adopted.handle]=number;adopted.number=productUtf8(number);adopted.verified=true;
  }catch(const std::exception& error){
   if(transaction){status(db.transactionManager()->abortTransaction(),"rollback adopted opening");require(wholeModelIds(db)==rootIds,"opening rollback membership");
    for(const auto& p:protectedGrips)require(approvedgrips::same(chainGrips(find(db,p.first)),p.second),"opening rollback changed protected geometry");
    const auto points=chainGrips(find(db,adopted.handle));require(points.size()>=3&&
     std::hypot(points[1].x-adopted.beforeA.x,points[1].y-adopted.beforeA.y)<eps&&
     std::hypot(points[2].x-adopted.beforeB.x,points[2].y-adopted.beforeB.y)<eps,"opening rollback failed to restore source endpoints");
    const std::set<std::string> selected{adopted.handle};
    const auto restored=productReadProperties(db,job.root/L"design-source-rollback"/(bcpproduct::decodeMessage(adopted.handle)+L"-"+std::to_wstring(GetTickCount64())),&selected).at(adopted.handle);
    require(std::abs(bcpproduct::propertyNumber(restored,L"宽度")-adopted.originalWidth)<eps&&
     std::abs(bcpproduct::propertyNumber(restored,L"高度")-adopted.originalHeight)<eps&&
     std::abs(bcpproduct::propertyNumber(restored,L"离地高度")-adopted.sill)<eps&&
     restored.at(L"门窗类型")==source->kind&&restored.at(L"门窗编号")==source->existing,
     "opening rollback failed to restore source public fields");}
   adopted.issue=error.what();job.detailPending.push_back({u8"门窗资料采用未闭合",adopted.handle,adopted.issue});
  }
 }
 return run;
}

void productSeparateBoundaryWalls(const ProductJob& job,const GenericNativeInput& input,std::vector<bcpproduct::GenericView>& views){
 for(auto& view:views){
  const auto before=view.walls.size();view.walls.erase(std::remove_if(view.walls.begin(),view.walls.end(),[&](auto i){return job.planBoundaryWalls.count(input.walls[i].wall.handle);}),view.walls.end());
  if(view.walls.size()==before||view.walls.empty())continue;
  bcpproduct::GenericBounds bounds{1e100,1e100,-1e100,-1e100};
  for(const auto i:view.walls){const auto& w=input.walls[i].wall;bounds.xmin=std::min({bounds.xmin,w.start.x,w.end.x});bounds.ymin=std::min({bounds.ymin,w.start.y,w.end.y});bounds.xmax=std::max({bounds.xmax,w.start.x,w.end.x});bounds.ymax=std::max({bounds.ymax,w.start.y,w.end.y});}
  view.bounds=bounds;
 }
}

void productWriteBoundaryDimensions(ProductJob& job,AcDbDatabase& db,const GenericNativeInput& input,AcDbObjectId prototype,ProductDesignSources& run){
 for(const auto& wall:input.walls)if(job.planBoundaryWalls.count(wall.wall.handle)){
  const auto handle=wall.wall.handle;
  try{
   require(wall.orthogonal,"curved/oblique boundary length needs a verified native dimension adapter");
   const AcGePoint3d a(wall.wall.start.x,wall.wall.start.y,0),b(wall.wall.end.x,wall.wall.end.y,0);
   const auto id=productTryPlanChain(job,db,prototype,{a,b},800./job.unifiedUnitMM,0,handle,u8"围墙独立尺寸","D31");
   if(!id)continue;
   auto e=editObject<AcDbEntity>(*id);status(e->setLayer(productPlanLayer(db,L"PUB_DIM",25)),"boundary length layer");e->setColorIndex(256);
   run.boundaryDimensions.emplace(handle,axisHandle(*id));
  }catch(const std::exception& error){job.detailPending.push_back({u8"围墙独立尺寸",handle,error.what()});}
 }
}

void productVerifyDesignSources(ProductJob& job,AcDbDatabase& db,const ProductDesignSources& run,const fs::path& drawing={}){
 const double eps=.01/job.unifiedUnitMM;std::set<std::string> handles;for(const auto& o:run.openings)handles.insert(o.handle);
 const auto properties=handles.empty()?std::map<std::string,bcpproduct::PublicProperties>{}:productReadProperties(db,job.root/L"design-source-reopened"/std::to_wstring(GetTickCount64()),&handles);
 std::ostringstream out;out<<std::setprecision(17)<<"{\"schema\":\"plan.native-design-readback.v1\",\"sourceSHA256\":\""<<job.sourceHash<<"\",\"adoptionSHA256\":\""<<run.hash<<"\",\"outputSHA256\":\""<<(drawing.empty()?std::string{}:sha(drawing))<<"\",\"savedReopened\":true,\"files\":[";bool first=true;
 for(const auto& file:run.files){if(!first)out<<',';first=false;
  out<<"{\"key\":"<<jsonUtf8(bcpproduct::decodeMessage(file.first))<<",\"path\":"<<jsonUtf8(file.second.first.wstring())<<",\"sha256\":\""<<file.second.second<<"\"}";
 }
 out<<"],\"openings\":[";first=true;
 for(const auto& item:run.openings){bool passed=false;std::string issue=item.issue;
  try{const auto& p=properties.at(item.handle);const auto gp=chainGrips(find(db,item.handle));
   passed=item.verified&&gp.size()>=3&&std::hypot(gp[1].x-item.expectedA.x,gp[1].y-item.expectedA.y)<eps&&
    std::hypot(gp[2].x-item.expectedB.x,gp[2].y-item.expectedB.y)<eps&&
    std::abs(bcpproduct::propertyNumber(p,L"宽度")*job.unifiedUnitMM-item.widthMM)<.01&&std::abs(bcpproduct::propertyNumber(p,L"高度")*job.unifiedUnitMM-item.heightMM)<.01&&
    p.count(L"门窗编号")&&productUtf8(p.at(L"门窗编号"))==item.number;
   if(!passed&&issue.empty())issue=u8"保存重开后采用宽高、定位或原生自身号未闭合";
  }catch(const std::exception& e){issue=e.what();}
  if(!passed)job.detailPending.push_back({u8"门窗来源最终复核",item.handle,issue});
  std::ostringstream row;row<<std::setprecision(17)<<"{\"object\":\""<<item.handle<<"\",\"widthMM\":"<<item.widthMM<<",\"heightMM\":"<<item.heightMM<<",\"source\":\""<<item.file<<"\",\"locator\":"<<jsonUtf8(bcpproduct::decodeMessage(item.locator))
   <<",\"basis\":"<<jsonUtf8(bcpproduct::decodeMessage(item.basis))<<",\"anchor\":\""<<item.anchor<<"\",\"prefix\":\""<<item.prefix<<"\",\"nativeNumber\":"<<jsonUtf8(bcpproduct::decodeMessage(item.number))
   <<",\"expectedPoints\":"<<wholePoints({item.expectedA,item.expectedB})<<",\"passed\":"<<(passed?"true":"false")<<",\"reason\":"<<jsonUtf8(bcpproduct::decodeMessage(issue))<<'}';
  if(!first)out<<',';first=false;out<<row.str();
  productRecordRuleEvidence(job,"O09",row.str());productRecordRuleEvidence(job,"QA11",row.str());
 }
 out<<"],\"boundaryDimensions\":[";first=true;
 for(const auto& item:run.boundaryDimensions){bool passed=false;std::string issue;
  try{const auto wall=open<AcDbEntity>(find(db,item.first));const auto curve=AcDbCurve::cast(wall.get());AcGePoint3d a,b;require(curve&&curve->getStartPoint(a)==Acad::eOk&&curve->getEndPoint(b)==Acad::eOk,"boundary wall native endpoints");
   const auto values=chainValues(find(db,item.second));passed=values.size()==1&&std::abs(values[0]-a.distanceTo(b))<eps;
  }catch(const std::exception& e){issue=e.what();}
  if(!passed)job.detailPending.push_back({u8"围墙独立尺寸复核",item.first,issue.empty()?u8"实际长度未闭合":issue});
  const auto row=std::string("{\"wall\":\"")+item.first+"\",\"dimension\":\""+item.second+"\",\"passed\":"+(passed?"true":"false")+'}';
  if(!first)out<<',';first=false;out<<row;productRecordRuleEvidence(job,"D31",row);
 }
 out<<"],\"boundaryWalls\":[";first=true;for(const auto& h:job.planBoundaryWalls){if(!first)out<<',';first=false;out<<'"'<<h<<'"';}out<<"]}";
 writeNew(job.root/L"design-source-readback.json",out.str());
}

// An isolated owned-host probe exercises the same mutation/readback code as
// generation. Its synthetic schedule is test evidence, never a design adoption.
void productRunDesignSourcesProbe(const fs::path& source,const fs::path& output,
 const fs::path& report,const fs::path& resources,const fs::path& adoption){
 require(!productJob&&curDoc()&&fs::equivalent(source,curDoc()->fileName()),"design probe requires its exclusive owned input");
 require(!output.empty()&&!fs::exists(output)&&output!=source,"design probe output must be new");
 productJob=std::make_unique<ProductJob>();
 struct Reset {~Reset(){productJob.reset();}} reset;
 auto& job=*productJob;job.root=report;job.profile=resources;job.sourceHash=sha(source);job.ownedDocument=curDoc();
 job.unifiedDesignSources=adoption;job.unifiedPlan=true;job.architecturalPlanOnly=true;
 auto& db=*curDoc()->database();Working context(&db);
 auto input=productReadGenericInput(job,db);auto expected=productReadDesignSources(job);
 std::set<std::string> scope;for(const auto& item:expected.openings)scope.insert(item.handle);
 const auto protectedBefore=wholeProtected(db,scope,true);
 const auto result=productApplyDesignSources(job,db,input);
 status(db.saveAs(output.c_str(),false,AcDb::kDHL_CURRENT),"save adopted-opening probe");
 AcDbDatabase saved(false,true);productStatus(saved.readDwgFile(output.c_str(),AcDbDatabase::kForReadAndAllShare,false),"reopen adopted opening probe");
 productStatus(saved.closeInput(true),"finish adopted opening probe read");
 {Working reopened(&saved);
  require(bcpproduct::samePublicGeometry(protectedBefore,wholeProtected(saved,scope,true)),"adopted opening probe changed unrelated geometry");
  productVerifyDesignSources(job,saved,result,output);
 }
 writeNew(report/L"product-result.json","{\"completed\":true,\"savedReopened\":true,\"sourceSHA256\":\""+job.sourceHash+
  "\",\"outputSHA256\":\""+sha(output)+"\",\"unresolvedCount\":"+std::to_string(job.detailPending.size())+"}");
}

void productRunDesignSourcesAudit(const fs::path& source,const fs::path& report,const fs::path& request){
 require(!productJob&&curDoc()&&fs::equivalent(source,curDoc()->fileName()),"design audit requires its exclusive owned input");
 productJob=std::make_unique<ProductJob>();struct Reset {~Reset(){productJob.reset();}} reset;
 auto& job=*productJob;job.root=report;job.ownedDocument=curDoc();
 std::ifstream stream(request);std::string tag,finalHash;ProductDesignSources expected;
 require(bool(stream>>tag>>finalHash>>job.sourceHash>>expected.hash>>job.unifiedUnitMM)&&
  tag=="PLANDESIGNAUDIT1"&&finalHash==sha(source)&&job.unifiedUnitMM>0,"final source audit is bound to this exact DWG");
 bool ended=false;std::string line;std::getline(stream,line);
 while(std::getline(stream,line)){
  std::istringstream row(line);require(bool(row>>tag),"nonempty final source audit record");
  if(tag=="END"){row>>std::ws;stream>>std::ws;require(row.eof()&&stream.eof(),"final source audit trailing data");ended=true;break;}
  if(tag=="FILE"){
   std::string key,hash,path;require(bool(row>>key>>hash>>std::quoted(path)),"final source evidence fields");const auto file=fs::u8path(path);
   require(fs::is_regular_file(file)&&sha(file)==hash&&expected.files.emplace(key,std::make_pair(file,hash)).second,"final design evidence changed");
  }else if(tag=="EXPECT"){
   ProductDesignOpening o;int verified{};
   require(bool(row>>o.handle>>o.widthMM>>o.heightMM>>o.expectedA.x>>o.expectedA.y>>o.expectedB.x>>o.expectedB.y>>verified>>
    std::quoted(o.number)>>std::quoted(o.file)>>std::quoted(o.locator)>>std::quoted(o.basis)>>std::quoted(o.anchor)>>std::quoted(o.prefix)>>std::quoted(o.issue)),"final expected opening row");
   o.verified=verified==1;expected.openings.push_back(std::move(o));
  }else require(false,"unknown final source audit record");
  row>>std::ws;require(row.eof(),"unexpected final audit fields");
 }
 require(ended,"final source audit END missing");
 AcDbDatabase db(false,true);productStatus(db.readDwgFile(source.c_str(),AcDbDatabase::kForReadAndAllShare,false),"independently read final source dimensions");
 productStatus(db.closeInput(true),"close final source audit input");
 {Working reopened(&db);productVerifyDesignSources(job,db,expected,source);}
 require(sha(source)==finalHash,"read-only source audit changed DWG");
 writeNew(report/L"product-result.json","{\"completed\":true,\"readOnly\":true,\"sourceSHA256\":\""+finalHash+
  "\",\"outputSHA256\":\""+finalHash+"\",\"unresolvedCount\":"+std::to_string(job.detailPending.size())+"}");
}
