"""Check source-derived cross-floor axis assignments against reopened DWG axis ends."""

import argparse
from collections import defaultdict
import json
from math import isfinite
from pathlib import Path

from check_floor_roofs import translation


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def verify(expected, reopened, layout=None):
    unit_mm = expected["unitMM"]
    if not isfinite(unit_mm) or unit_mm <= 0:
        raise ValueError("unitMM 必须为正值")
    tolerance = .01 / unit_mm
    folds = {}
    if layout is not None:
        if not layout.get("savedReadback"):
            raise ValueError("折引线须从独立重开后的图纸读回")
        for axis in layout["axes"]:
            for end in axis["ends"]:
                fold = end.get("fold", [])
                if not fold:
                    continue
                key = axis["handle"], end["side"]
                if (key in folds or len(fold) != 3 or end["realAnchor"] != [fold[0]] or
                        end["nativeLead"] != [fold[-1]]):
                    raise ValueError(f"折引线与真实轴位端、原生引线的关联无效 {key}")
                folds[key] = (fold[0], fold[-1])
    views = expected["views"]
    view_ids = {item["view"] for item in views}
    if not views or len(view_ids) != len(views):
        raise ValueError("应按楼层顺序列出每幅平面，图幅标识须唯一")
    offsets = {views[0]["view"]: (0.0, 0.0)}
    registrations = []
    for lower, upper in zip(views, views[1:]):
        aligned = translation(lower["ties"], upper["ties"], 1 / unit_mm)
        if aligned is None:
            raise ValueError(f'图幅 {lower["view"]} 与 {upper["view"]} 的共同基准不足或配准不一致')
        offsets[upper["view"]] = tuple(
            a + b for a, b in zip(offsets[lower["view"]], aligned["offsetXY"])
        )
        registrations.append({"lower": lower["view"], "upper": upper["view"], **aligned})

    requested = {}
    stations = defaultdict(list)
    covered_views = set()
    for axis in expected["axes"]:
        vertical, label, station = (axis[k] for k in ("vertical", "label", "station"))
        if not isinstance(vertical, bool) or not label or not isfinite(station):
            raise ValueError("应有轴须填写方向、编号和有效轴位")
        if (any(other == label or abs(station - at) <= tolerance for other, at in stations[vertical])
            or not axis["members"]):
            raise ValueError(f"全楼应有轴表存在同号分轴、重复轴位或缺少楼层归属 {label}")
        stations[vertical].append((label, station))
        members = set()
        for member in axis["members"]:
            view, sides = member["view"], member["sides"]
            allowed = {2, 3} if vertical else {0, 1}
            if (view not in view_ids or view in members or len(set(sides)) != len(sides)
                or not set(sides) <= allowed or (not sides and not member.get("omittedReason"))):
                raise ValueError(f"轴 {label} 的图幅、侧别或省略依据无效 {member}")
            members.add(view)
            covered_views.add(view)
            for side in sides:
                requested[view, side, label] = station
    if covered_views != view_ids:
        raise ValueError(f"应有轴表缺少图幅 {sorted(view_ids - covered_views)}")

    actual = defaultdict(list)
    centres = defaultdict(list)
    seen = {}
    for item in reopened["objects"]:
        if not item["visible"] or "grips" not in item:
            continue
        handle = item["handle"]
        signature = (item["view"], item["grips"], item["text"])
        if handle in seen:
            if seen[handle] != signature:
                raise ValueError(f"同一轴号对象的读回互相矛盾 {handle}")
            continue
        seen[handle] = signature
        view, grips, words = signature
        if view not in offsets or len(grips) not in (4, 8):
            raise ValueError(f"轴号对象的图幅或原生夹点无效 {handle}")
        count = len(grips) // 4
        if not words or len(words) % count:
            raise ValueError(f"轴号文字无法与各端对应 {handle}")
        width = len(words) // count
        for index in range(count):
            lead, centre = grips[4*index:4*index+2]
            if not all(isfinite(value) for value in (*lead[:2], *centre[:2])) or lead[:2]==centre[:2]:
                raise ValueError(f"轴号引线内端或圆心无效 {handle}")
            along = int(abs(centre[1]-lead[1]) > abs(centre[0]-lead[0]))
            side = 2*along + int(centre[along] > lead[along])
            label = "/".join(words[index*width:(index+1)*width])
            anchor, attachment = folds.pop((handle, side), (lead, lead))
            if any(abs(a-b) > tolerance for a,b in zip(attachment, lead)):
                raise ValueError(f"折引线未接到该轴号的原生引线 {handle}")
            station = anchor[1-along] - offsets[view][1-along]
            if not isfinite(station):
                raise ValueError(f"轴号对象的定位坐标无效 {handle}")
            actual[view, side, label].append((station, handle))
            centres[view, side].append((centre[along], handle))

    issues = []
    if folds:
        issues.append(f"折引线缺少对应的可见轴号端 {sorted(folds)}")
    for key, expected_station in requested.items():
        observed = actual.pop(key, [])
        if len(observed) != 1:
            issues.append(f"{key} 应有一个轴号端，实有 {len(observed)} 个 {observed}")
            continue
        position, handle = observed[0]
        if abs(position-expected_station) > tolerance:
            issues.append(f"{key} 轴位实测 {position:.6f}，应为 {expected_station:.6f}，对象 {handle}")
    for key, observed in actual.items():
        issues.append(f"{key} 有未列入应有清单的轴号端 {observed}")
    for key, group in centres.items():
        first, last = min(group), max(group)
        if last[0]-first[0] > tolerance:
            issues.append(f"{key} 同侧圆心偏离同一排布线 {(last[0]-first[0])*unit_mm:.3f} mm，"
                          f"对象 {first[1]} 与 {last[1]}")
    return {"passed": not issues, "views": len(views), "axes": len(expected["axes"]),
            "expectedEnds": len(requested), "registrations": registrations, "issues": issues}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("expected", type=Path)
    parser.add_argument("reopened", type=Path)
    parser.add_argument("--layout", type=Path, help="含重开后实际折引线的外围间距核验 JSON")
    args = parser.parse_args()
    report = verify(read(args.expected), read(args.reopened), read(args.layout) if args.layout else None)
    print(json.dumps(report, ensure_ascii=False, indent=2))
    raise SystemExit(0 if report["passed"] else 1)
