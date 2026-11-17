"""Compare a source-derived perimeter inventory with visible chains from a reopened DWG."""

import argparse
from collections import defaultdict
import json
import math
from pathlib import Path


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def verify(expected, reopened):
    unit_mm = expected["unitMM"]
    if not math.isfinite(unit_mm) or unit_mm <= 0:
        raise ValueError("unitMM 必须为正值")
    tolerance = .01 / unit_mm

    def valid_stations(stations):
        return (len(stations) >= 2 and all(math.isfinite(v) for v in stations)
                and all(b-a > tolerance for a,b in zip(stations,stations[1:])))

    by_row = defaultdict(list)
    errors = []
    for chain in reopened["dimensions"]:
        view, side, row = (chain[key] for key in ("view", "side", "row"))
        if side not in range(4) or row not in (1, 2, 3):
            errors.append(f"无有效侧别或道次 {view}/{side}/{row}")
            continue
        coordinate = int(side < 2)
        stations = tuple(point[coordinate] for point in chain["stations"])
        values = chain["values"]
        if stations and stations[0] > stations[-1]:
            stations = stations[::-1]
            values = values[::-1]
        if (not valid_stations(stations) or len(values) != len(stations)-1 or
            any(not math.isfinite(v) or abs(v-(b-a)) > tolerance
                for a,b,v in zip(stations,stations[1:],values))):
            errors.append(f"{view}/{side}/{row} 尺寸链站点或实测分段无效")
        by_row[view,side,row].append(stations)

    views = expected["views"]
    if not views or len({view["view"] for view in views}) != len(views):
        raise ValueError("应有清单须明确列出所有图幅")
    for view in views:
        sides = view["sides"]
        if {item["side"] for item in sides} != set(range(4)) or len(sides) != 4:
            raise ValueError(f'图幅 {view["view"]} 应独立核对四侧')
        for side in sides:
            axes = side["axes"]
            if axes and not all(math.isfinite(v) for v in axes):
                raise ValueError("前两道须有源图核实的首末轴和严格递增的轴间测站")
            if len(axes) >= 2 and not valid_stations(axes):
                raise ValueError("前两道须有源图核实的首末轴和严格递增的轴间测站")
            if len(axes) < 2 and not side.get("axisOmittedReason", "").strip():
                raise ValueError("无有效首末轴时须登记前两道不适用的源图依据")
            if len(axes) >= 2 and side.get("axisOmittedReason", "").strip():
                raise ValueError("已有有效首末轴，前两道须标注")
            if not side["third"] and not side.get("thirdOmittedReason", "").strip():
                raise ValueError("第三道可省时须登记源图依据")
            if any(not valid_stations(chain) for chain in side["third"]):
                raise ValueError("第三道须有严格递增的有效测站")
            if side["third"] and side.get("thirdOmittedReason", "").strip():
                raise ValueError("第三道有应标测站，整道不可省略")
            for group in side.get("thirdAxisBounds", []):
                start, end = group["from"], group["to"]
                if not valid_stations([start, end]) or not all(
                    any(abs(station-axis) <= tolerance for axis in axes)
                    for station in (start, end)
                ):
                    raise ValueError("第三道开间边界须取本侧有效轴，或另列无法按轴定位的依据")
                chains = [chain for chain in side["third"]
                          if any(abs(x-start) <= tolerance for x in chain)
                          and any(abs(x-end) <= tolerance for x in chain)]
                if len(chains) != 1:
                    raise ValueError(f'图幅 {view["view"]} 侧 {side["side"]} 第三道缺少唯一轴间闭合段 {start}～{end}')
            total = ((axes[0],axes[-1]),) if len(axes) >= 2 else ()
            intermediate = (axes,) if len(axes) >= 2 else ()
            rows = total, intermediate, side["third"]
            for index, chains in enumerate(rows,1):
                key = view["view"], side["side"], index
                actual = sorted(by_row.pop(key, []))
                wanted = sorted(tuple(stations) for stations in chains)
                if len(actual) != len(wanted):
                    errors.append(f"{key} 实有 {len(actual)} 道，应有 {len(wanted)} 道")
                    continue
                for a,b in zip(actual,wanted):
                    if len(a)!=len(b) or any(abs(x-y)>tolerance for x,y in zip(a,b)):
                        errors.append(f"{key} 实测站点 {a}，应有 {b}")
    if by_row:
        errors.append(f"有额外图幅或外围链 {sorted(by_row)}")
    if errors:
        raise ValueError("\n".join(errors))
    return {"views": len(views), "sides": 4*len(views), "checked": True}


if __name__ == "__main__":
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("expected", type=Path)
    cli.add_argument("reopened", type=Path)
    args = cli.parse_args()
    print(json.dumps(verify(read(args.expected), read(args.reopened)), ensure_ascii=False))
