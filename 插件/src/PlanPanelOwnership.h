#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>

enum class ProductPanelRole { floor, unnamed, roof, detail, attached, unassigned, outside };

struct ProductPanelOwnership {
 ProductPanelRole role{ProductPanelRole::unassigned};
 std::optional<int> floor;
 std::vector<std::string> titles;
 std::string reason;
 std::optional<std::size_t> parentPanel;

 bool eligible() const {return role==ProductPanelRole::floor||role==ProductPanelRole::unnamed;}
};

inline std::map<int,std::vector<std::size_t>> productMainFloorPanels(
 const std::vector<ProductPanelOwnership>& panels) {
 std::map<int,std::vector<std::size_t>> floors;
 for(std::size_t i=0;i<panels.size();++i)
  if(panels[i].role==ProductPanelRole::floor&&panels[i].floor)
   floors[*panels[i].floor].push_back(i);
 return floors;
}
