bool productPrintedProofRetainsClearance(const ProductPrintedInkProof& before,const ProductPrintedInkProof& after){
 return std::all_of(before.targets.begin(),before.targets.end(),[&](const auto& target){
  return after.targets.count(target.first)!=0;
 })&&std::includes(before.colliding.begin(),before.colliding.end(),after.colliding.begin(),after.colliding.end())&&
    std::includes(before.missing.begin(),before.missing.end(),after.missing.begin(),after.missing.end());
}

std::shared_ptr<const ProductPlanPostcheckTextEdit> productSavePostcheckSourceText(
 const ProductJob& job,const fs::path& candidate,const fs::path& edited,std::size_t panel,const std::string& handle){
 std::shared_ptr<const ProductPlanPostcheckTextEdit> proposal;
 {
  AcDbDatabase db(false,true);
  productStatus(db.readDwgFile(candidate.c_str(),AcDbDatabase::kForReadAndAllShare,false),"open isolated source text repair");
  productStatus(db.closeInput(true),"complete isolated source text input");
  Working working(&db);
  proposal=job.planPostcheckTextRepair(db,job,panel,handle);
  if(!proposal)return {};
  require(!fs::exists(edited),"new private source text candidate");
  productStatus(db.saveAs(edited.c_str(),false,AcDb::kDHL_CURRENT),"save local source text repair");
 }
 AcDbDatabase saved(false,true);
 productStatus(saved.readDwgFile(edited.c_str(),AcDbDatabase::kForReadAndAllShare,false),"reopen local source text repair");
 productStatus(saved.closeInput(true),"complete source text repair readback");
 require(proposal->verifyReopened(saved,job),"source text or protected geometry differs upon reopen");
 return proposal;
}

struct ProductAcceptedSourceText {
 std::size_t panel;
 std::string handle;
 AcGePoint3d before,after;
 std::vector<std::string> cleared;
};

template<class Measure>
std::vector<ProductAcceptedSourceText> productRepairSourceTextInk(ProductJob& job,AcApDocument*& final,
 fs::path& candidate,ProductPrintedInkProof& best,const fs::path& directory,const Measure& measure,
 const std::size_t& passes,std::size_t passLimit,std::size_t& examined,std::string& trialError){
 std::vector<ProductAcceptedSourceText> accepted;
 if(!job.planPostcheckTextRepair)return accepted;
 std::vector<bcpproduct::PlanDrawingChecks::Key> keys;
 for(const auto& [key,check]:best.drawingChecks.items){
  if(std::get<1>(key)=="source-text-clearance"&&
    (check.state==bcpproduct::PlanCheck::pending||check.state==bcpproduct::PlanCheck::failed))keys.push_back(key);
 }
 std::size_t printed=0;
 for(const auto& key:keys){
  if(passes>=passLimit||printed>=2)break;
  ++examined;
  const auto& [panel,scope,handle]=key;
  const auto token=std::to_wstring(examined);
  const auto edited=directory/(L"source-text-"+token+L".dwg");
  AcApDocument* trial{};
  try{
   const auto edit=productSavePostcheckSourceText(job,candidate,edited,panel,handle);
   if(!edit)continue;
   trial=productOpenPrintedInkCandidate(edited);
   ++printed;
   auto proof=measure(*trial,edited,directory/(L"attempt-text-"+token),{handle});
   if(!productPrintedProofRetainsClearance(best,proof)){
    productStatus(acDocManager->appContextCloseDocument(trial),"discard source text trial changing printed coverage or clearance");
    trial=nullptr;
    continue;
   }
   // Source-wording is checked in the reopened candidate. Paper clearance is
   // refreshed for every intersecting view before using it in the trial.
   for(const auto& item:accepted)proof.drawingChecks.items.insert_or_assign(
     {item.panel,"source-text-clearance",item.handle},best.drawingChecks.items.at({item.panel,"source-text-clearance",item.handle}));
   auto& check=proof.drawingChecks.items.at(key);
   check.state=bcpproduct::PlanCheck::verified;
   check.basis=u8"同版保存重开核验源文字内容、样式、落位与净空，并实际打印复验尺寸";
   check.reason.clear();
   productStatus(acDocManager->appContextCloseDocument(final),"close plan before accepted source text repair");
   final=trial;
   trial=nullptr;
   best=std::move(proof);
   candidate=edited;
   job.planVerifiedCandidateHash=best.digest;
   accepted.push_back({panel,handle,edit->before,edit->after,edit->cleared});
  }catch(const std::exception& error){
   if(trial)acDocManager->appContextCloseDocument(trial);
   trialError=error.what();
  }
 }
 return accepted;
}
