#pragma once
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

namespace bcpproduct {
inline bool planJoinRoomDomain(std::vector<std::size_t>& parents,const std::vector<double>& areasM2,
 const std::vector<bool>& excluded,std::size_t named,std::size_t extra,double expectedM2) {
 if(parents.size()!=areasM2.size()||parents.size()!=excluded.size()||named>=parents.size()||
    extra>=parents.size()||named==extra||excluded[named]||excluded[extra]||
    !std::isfinite(expectedM2)||expectedM2<=0||!std::isfinite(areasM2[extra])||
    std::abs(areasM2[extra]-expectedM2)>1e-5)return false;
 const auto root=[&](std::size_t index)->std::optional<std::size_t>{
  for(std::size_t n=0;n<parents.size();++n){
   if(index>=parents.size())return {};
   if(parents[index]==index)return index;
   index=parents[index];
  }
  return {};
 };
 const auto a=root(named),b=root(extra);
 if(!a||!b)return false;
 parents[*b]=*a;
 return true;
}
}
