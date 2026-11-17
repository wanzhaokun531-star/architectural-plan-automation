struct ProductAxisServices {
 std::vector<bcpproduct::AxisServiceWall> walls;
 std::vector<bcpproduct::AxisServiceColumn> columns;
};
ProductAxisServices productAxisServices(AcDbDatabase& db,const ProductPlanBounds& panel){
 ProductAxisServices result;
 for(const auto& item:modelTypes(db)){
  auto e=open<AcDbEntity>(find(db,ascii(item.first)));if(e->isAProxy())continue;
  if(item.second==L"TDbWall"){
   AcGePoint3dArray p;if(e->getStretchPoints(p)!=Acad::eOk||p.length()!=2)continue;
   const auto mid=(p[0]+(p[1]-p[0])*0.5);if(mid.x<panel.xmin-1e-5||mid.x>panel.xmax+1e-5||mid.y<panel.ymin-1e-5||mid.y>panel.ymax+1e-5)continue;
   if(std::abs(p[0].x-p[1].x)<1e-5)result.walls.push_back({true,p[0].x,std::min(p[0].y,p[1].y),std::max(p[0].y,p[1].y)});
   else if(std::abs(p[0].y-p[1].y)<1e-5)result.walls.push_back({false,p[0].y,std::min(p[0].x,p[1].x),std::max(p[0].x,p[1].x)});
  }else if(item.second==L"TDbColumn"){
   TemplateParts parts(e.get());if(e->explode(parts.values)!=Acad::eOk||parts.values.length()!=4)continue;
   double x0=1e100,y0=1e100,x1=-1e100,y1=-1e100;bool rectangular=true;
   for(int k=0;k<4;++k){auto* line=AcDbLine::cast(static_cast<AcRxObject*>(parts.values[k]));if(!line){rectangular=false;break;}const auto a=line->startPoint(),b=line->endPoint();if(std::abs(a.x-b.x)>1e-5&&std::abs(a.y-b.y)>1e-5){rectangular=false;break;}x0=std::min(x0,a.x);x1=std::max(x1,a.x);y0=std::min(y0,a.y);y1=std::max(y1,a.y);}
   const double x=(x0+x1)/2,y=(y0+y1)/2;
   if(rectangular&&x>=panel.xmin-1e-5&&x<=panel.xmax+1e-5&&y>=panel.ymin-1e-5&&y<=panel.ymax+1e-5)result.columns.push_back({x,y,(x1-x0)/2,(y1-y0)/2});
  }
 }
 return result;
}
