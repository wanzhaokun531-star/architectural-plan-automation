"""Compare floor footprints, recognize exposed roofs, and check projected axes."""

import argparse
import json
from itertools import combinations
from math import dist, isfinite
from pathlib import Path
from statistics import median

from shapely import affinity
from shapely.geometry import GeometryCollection, LineString, Polygon, mapping
from shapely.ops import unary_union


def polygon(points):
    shape = Polygon(points)
    if shape.is_empty or not shape.is_valid or shape.area <= 0:
        raise ValueError("屋面与建筑空间需要有效的闭合轮廓")
    return shape


def footprint(polygons):
    return unary_union([polygon(points) for points in polygons])


def translation(lower, upper, tolerance):
    keys = sorted(lower.keys() & upper.keys())
    anchors = [lower[key] for key in keys]
    if not any(abs((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])) >
               tolerance**2 for a,b,c in combinations(anchors,3)):
        return None
    offsets = [(upper[key][0]-lower[key][0],upper[key][1]-lower[key][1]) for key in keys]
    offset = tuple(median(values) for values in zip(*offsets))
    error = max(dist(point,offset) for point in offsets)
    if error > tolerance:
        return None
    return {"offsetXY": offset, "maxResidual": error, "ties": keys}


def surfaces(geometry):
    if geometry.geom_type == "Polygon":
        if not geometry.is_empty:
            yield geometry
    elif hasattr(geometry, "geoms"):
        for item in geometry.geoms:
            yield from surfaces(item)


def intersects_axis(zone, vertical, station, tolerance):
    if zone.is_empty:
        return False
    xmin,ymin,xmax,ymax = zone.bounds
    if vertical:
        probe = LineString([(station,ymin-tolerance),(station,ymax+tolerance)])
    else:
        probe = LineString([(xmin-tolerance,station),(xmax+tolerance,station)])
    return zone.intersection(probe).length > tolerance


def classify(candidate, exposed, tolerance, unit_mm):
    shape = polygon(candidate["boundary"])
    overhang_mm = candidate.get("verifiedOverhangMM",0)
    if not isfinite(overhang_mm) or overhang_mm < 0:
        raise ValueError("檐口出挑须填写已核实的非负值")
    outside = shape.difference(exposed.buffer(tolerance+overhang_mm/unit_mm)).area
    evidence = candidate.get("evidence",{})
    basis = evidence.get("confirmedUse")
    if shape.intersection(exposed).area <= tolerance**2 or outside > tolerance**2:
        status = "待核对"
        reason = "轮廓与下层实有覆盖范围不吻合"
    elif basis in {"露台","阳台","挑空","雨棚"}:
        status = basis
        reason = "已核实的空间或构件用途"
    elif basis == "屋面" or evidence.get("nativeRoof") or (
         evidence.get("roofConstruction") and evidence.get("levelOrSlope") and not evidence.get("access")):
        status = "屋面"
        reason = "下层覆盖、上层退让和屋面依据相符"
    else:
        status = "待核对"
        reason = "叠合显示下层覆盖，仍需区分屋面、露台和其他空间"
    return {"source":candidate["source"],"status":status,"reason":reason,
            "bounds":shape.bounds,"areaM2":shape.area*unit_mm**2/1e6,
            "overlapM2":shape.intersection(exposed).area*unit_mm**2/1e6,
            "outsideM2":outside*unit_mm**2/1e6,"evidence":evidence,
            "geometry":mapping(shape)},shape


def verify(data):
    unit_mm = data["unitMM"]
    if not isfinite(unit_mm) or unit_mm <= 0:
        raise ValueError("unitMM 必须为正值")
    registration_tolerance = 1/unit_mm
    geometry_tolerance = .01/unit_mm
    plans = data["plans"]
    if len(plans)<2 or len({plan["id"] for plan in plans})!=len(plans):
        raise ValueError("须提供至少两幅已按楼层排序且标识唯一的平面")
    report = []
    for index, upper in enumerate(plans[1:],1):
        if upper.get("referenceOnly"):
            continue
        checked = {"plan":upper["id"],"registrations":[],"candidates":[],"axes":[],"pending":[]}
        report.append(checked)
        occupied = footprint(upper["interior"])
        registered = []
        for lower in plans[max(0,index-2):index]:
            aligned = translation(lower["ties"],upper["ties"],registration_tolerance)
            if aligned is None:
                checked["pending"].append({"plan":lower["id"],"reason":"共有基准不足或配准不唯一"})
                continue
            registered.append((lower,aligned))
            checked["registrations"].append({"plan":lower["id"],**aligned})
        candidates = upper.get("outlines",[])
        roof_zones = []
        if registered and registered[-1][0]["id"] == plans[index-1]["id"]:
            lower, aligned = registered[-1]
            dx,dy = aligned["offsetXY"]
            cover = affinity.translate(footprint(lower["covered"]),dx,dy)
            exposed = cover.difference(occupied)
            measured = []
            for candidate in candidates:
                item, shape = classify(candidate,exposed,geometry_tolerance,unit_mm)
                checked["candidates"].append(item)
                measured.append(shape)
                if item["status"] == "屋面":
                    roof_zones.append(shape)
            for shape in surfaces(exposed.difference(unary_union(measured)).buffer(-geometry_tolerance)):
                checked["pending"].append({"bounds":shape.bounds,"reason":"下层覆盖、上层退让，待核实轮廓及用途"})
        else:
            checked["pending"].append({"reason":"直接下层未完成配准，暂不判定屋面"})
        roof = unary_union(roof_zones) if roof_zones else GeometryCollection()

        for lower,aligned in reversed(registered):
            for source in lower.get("axes",[]):
                vertical = source["vertical"]
                station = source["station"] + aligned["offsetXY"][0 if vertical else 1]
                if not intersects_axis(roof,vertical,station,geometry_tolerance):
                    continue
                matches = [axis for axis in checked["axes"]
                           if axis["vertical"]==vertical and abs(axis["station"]-station)<=registration_tolerance]
                if matches:
                    matches[0]["references"].append({"plan":lower["id"],"label":source["label"]})
                else:
                    checked["axes"].append({"vertical":vertical,"station":station,"references":[
                        {"plan":lower["id"],"label":source["label"]}]})
        for axis in checked["axes"]:
            names = {ref["label"] for ref in axis["references"] if ref["label"]}
            if len(names) != 1:
                axis["status"] = "轴号待核对"
            else:
                label = next(iter(names))
                axis["label"] = label
                at_station = [current for current in upper.get("axes",[])
                              if current["vertical"]==axis["vertical"] and
                              abs(current["station"]-axis["station"])<=registration_tolerance]
                if any(current["label"]==label for current in at_station):
                    axis["status"] = "同轴同号，复核本侧测站"
                elif at_station:
                    axis["status"] = "轴位已识别，核对轴号及本侧测站"
                else:
                    axis["status"] = "缺对应轴位，按实有服务范围核对补设"

        checked["existingAxisReview"] = []
        if upper.get("roofOnly") or roof_zones:
            for axis in upper.get("axes",[]):
                applies = intersects_axis(occupied,axis["vertical"],axis["station"],geometry_tolerance) or \
                          intersects_axis(roof,axis["vertical"],axis["station"],geometry_tolerance)
                if not applies and not axis.get("requiredLabel") and (
                   upper.get("roofOnly") and roof_zones or axis.get("reviewOutsideRoof")):
                    checked["existingAxisReview"].append({**axis,"labelAction":"可隐去轴圈及专用引线",
                        "stationAction":"按是否承担实际定位单独核定"})
        checked["perimeterThirdRow"] = "纯檐口轮廓第三道可省，洞口尺寸和必要定位按实际用途核定"
    return {"unitMM":unit_mm,"plans":report}


if __name__ == "__main__":
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("source",type=Path)
    cli.add_argument("--output",type=Path)
    args = cli.parse_args()
    result = json.dumps(verify(json.loads(args.source.read_text("utf-8-sig"))),ensure_ascii=False,indent=2)+"\n"
    if args.output:
        args.output.write_text(result,encoding="utf-8")
    else:
        print(result,end="")
