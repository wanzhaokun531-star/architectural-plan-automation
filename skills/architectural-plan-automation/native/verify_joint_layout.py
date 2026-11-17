"""Verify saved-DWG joint-layout evidence and write a per-floor acceptance report."""

import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import struct


ROOT = Path(__file__).resolve().parents[3]
BASELINE = ROOT / "outputs/平面Skill无BCP逐层验收_20261111"
CASES = {
    "陈府": ("陈府", ("一层", "二层", "隔热层"), True),
    "赵总原方案": ("赵总", ("一层", "二层"), True),
    "罗朝辉原DWG": ("罗朝辉", ("F0", "一层 F1", "二层 F2", "F3"), True),
    "陈府既有尺寸": ("陈府", ("一层", "二层", "隔热层"), False),
    "赵总": ("赵总", ("一层", "二层"), False),
    "罗朝辉": ("罗朝辉", ("F0", "一层 F1", "二层 F2", "F3"), False),
}
DIMENSION_PENDING = {
    "室内尺寸避让", "室内尺寸字框未验证", "新建室内尺寸位置待核", "普通投影尺寸待转",
    "室内尺寸", "室内尺寸联合排位", "室内尺寸移动模式待核", "尺寸同侧无引线", "普通CAD尺寸排版", "尺寸字高",
}
PROJECTION_REASONS = {
    "native projection did not retain the two source stations": "天正候选无法保留原两个测点",
    "native projection changed the measuring direction": "天正候选改变了原投影方向",
    "native projection changed the measured value": "天正候选改变了原实测值",
    "native projection could not reproduce the original baseline": "天正候选未能复现原基线",
}


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def audit_path(folder, case):
    return folder / case / "plan-native-result.json"


def verify_host(folder, summary):
    require(summary.get("nativeQuit") is True, "isolated host has no native quit confirmation")
    session = Path(summary["session"])
    terminal = read(session / "isolation-final.json")
    required = {"workerExited": True, "nativeQuitConfirmed": True, "workerExitCode": 0,
                "forcedTermination": False, "userCadTouched": False,
                "workerForegroundEvents": 0, "workerUserDesktopShowEvents": 0}
    require(all(terminal.get(key) == value for key, value in required.items()), "unexpected isolated host closure")
    archive = session / "staging-final-archive"
    modules = list(archive.glob("PlanNative_*.arx"))
    require(len(modules) == 1 and sha(modules[0]) == summary["moduleSHA256"], "staged ARX digest mismatch")
    for filename in ("worker-modules.txt", "worker-modules-after.txt"):
        data = (folder / filename).read_bytes().replace(b"\0", b"").lower()
        require(modules[0].name.lower().encode() in data and b"tch_kernal.arx" in data
                and b"buildingplanplugin2020.arx" not in data, f"unexpected module list in {filename}")


def verify_plot(path):
    with path.open("rb") as handle:
        header = handle.read(24)
    require(len(header) == 24 and header[:8] == b"\x89PNG\r\n\x1a\n"
            and header[12:16] == b"IHDR", f"invalid plot {path}")
    require(min(struct.unpack(">II", header[16:24])) >= 1000, f"plot too small {path}")


def equal_values(a, b):
    return len(a) == len(b) and all(math.isclose(x, y, rel_tol=0, abs_tol=1e-5) for x, y in zip(a, b))


def verify_dimension_geometry(key, row):
    label = f"{key} {row['handle']}"
    values = row["values"]
    stations = row["stationCoordinates"]
    require(values and len(stations) == len(values) + 1 and all(math.isfinite(value) for value in values),
            f"{label} station count differs from measured segments")
    require(all(len(point) == 3 and all(math.isfinite(component) for component in point) for point in stations),
            f"{label} invalid saved station coordinates")
    if row["stock"] or row.get("converted"):
        direction = row["projectionDirection"]
        require(len(direction) == 3 and math.isclose(math.hypot(*direction), 1.0, abs_tol=1e-8),
                f"{label} invalid saved projection direction")
        start, end = stations
        projected = abs(sum((b - a) * axis for a, b, axis in zip(start, end, direction)))
        require(equal_values(values, [projected]), f"{label} saved projection does not match its stations")
        for source_key, final_key in (("sourceBaselinePoint", "baselinePoint"), ("sourceTextPosition", "textPosition")):
            if source_key in row or final_key in row:
                require(all(k in row and len(row[k]) == 3 and all(math.isfinite(v) for v in row[k])
                            for k in (source_key, final_key)), f"{label} invalid projection placement trace")


def verify_case(folder, key, recorded, baseline, floor_count):
    audit = read(audit_path(folder, key))
    require(audit["completed"] and audit["reopened"], f"{key} DWG not reopened")
    require(audit["bcpLoadedBefore"] is False and audit["bcpLoadedAfter"] is False, f"{key} BCP was loaded")
    require(audit["sourceSHA256"] == recorded["SHA256"] == baseline["sourceSHA256"] == sha(Path(recorded["source"])),
            f"{key} source version mismatch")
    require(audit["outputSHA256"] == sha(folder / key / "审阅候选.dwg"), f"{key} DWG digest mismatch")
    expected = set(range(1, floor_count + 1))
    require({row["panel"] for row in audit["panels"] if row["processed"]} == expected,
            f"{key} missing a floor in saved DWG evidence")
    require(len(recorded["plots"]) == floor_count
            and {plot["panel"] for plot in recorded["plots"] if "image" in plot} == expected,
            f"{key} missing a floor plot")
    for plot in recorded["plots"]:
        verify_plot(Path(plot["image"]))

    require("layout" in audit, f"{key} no joint layout evidence")
    prior_by_source = {row["handle"]: row for row in baseline["interior"] if not row["added"]}
    by_source = {}
    by_handle = {}
    for row in audit["interior"]:
        source = row.get("sourceHandle") or row["handle"]
        require(row["handle"] not in by_handle, f"{key} duplicate result handle {row['handle']}")
        by_handle[row["handle"]] = row
        require(row["panel"] in expected and row["measurementsPreserved"] is True,
                f"{key} dimension {row['handle']} measurement readback failed")
        require(row.get("stationsPreserved") is True, f"{key} dimension {row['handle']} station readback absent")
        verify_dimension_geometry(key, row)
        if row["added"]:
            require(isinstance(row.get("textOnRequestedStationSide"), bool),
                    f"{key} new dimension {row['handle']} has no requested-side readback")
            continue
        require(source not in by_source, f"{key} duplicate source dimension {source}")
        by_source[source] = row
        if source in prior_by_source:
            old = prior_by_source[source]
            require(old["panel"] == row["panel"] and equal_values(old["values"], row["values"]),
                    f"{key} {source} changed panel or measured value")
            if old["stock"]:
                require(row.get("projectionPreserved") is True, f"{key} {source} projection readback absent")
                require(bool(row.get("converted")) != bool(row["stock"]), f"{key} {source} inconsistent conversion")
    require(prior_by_source.keys() <= by_source.keys(),
            f"{key} missing prior source dimensions {sorted(prior_by_source.keys() - by_source.keys())}")

    layout_handles = set()
    groups = []
    for group in audit["layout"]:
        handles = set(group["chains"])
        require(handles and not (handles & layout_handles) and handles <= by_handle.keys(),
                f"{key} invalid group membership {group['group']}")
        require(all(by_handle[h]["panel"] == group["panel"] for h in handles),
                f"{key} group spans distinct floors {group['group']}")
        before, after = group["beforeConflicts"], group["afterConflicts"]
        unreadable = group["unreadable"]
        require(isinstance(before, int) and isinstance(after, int) and min(before, after) >= 0,
                f"{key} invalid conflict counts in {group['group']}")
        if "overlaps" in group:
            require(isinstance(group["overlaps"], int) and 0 <= group["overlaps"] <= after,
                    f"{key} invalid visible-overlap count in {group['group']}")
        for field in ("writtenConflicts", "beforeSideIssues", "sideIssues"):
            if field in group:
                require(isinstance(group[field], int) and group[field] >= 0,
                        f"{key} invalid {field} count in {group['group']}")
        unknown_count = len(unreadable) if isinstance(unreadable, list) else unreadable
        require(isinstance(unknown_count, int) and unknown_count >= 0,
                f"{key} invalid unreadable count in {group['group']}")
        groups.append((group, unknown_count))
        layout_handles.update(handles)
    require(by_handle.keys() == layout_handles, f"{key} some chains have no joint layout evidence")
    return audit, groups


def verify_names_and_areas(folder, baseline_folder, key, audit, baseline):
    current = folder / key
    prior = baseline_folder / key
    require(audit["areas"] == baseline["areas"], f"{key} newly written area readback changed")
    require(read(current / "平面名称文字终核.json") == read(prior / "平面名称文字终核.json"),
            f"{key} room name readback changed")
    current_rooms = {path.name: read(path) for path in current.glob("通用房间核验_*.json")}
    prior_rooms = {path.name: read(path) for path in prior.glob("通用房间核验_*.json")}
    require(current_rooms == prior_rooms, f"{key} room area, existing area or source record changed")


def table_rows(folder, key, floor_labels, audit, groups, baseline):
    prior_panel = {row["handle"]: row["panel"] for row in baseline["interior"]}
    before_chains = Counter(prior_panel[handle] for handle in {
        handle for kind, handle, _ in baseline["pending"]
        if kind == "室内尺寸避让" and handle in prior_panel
    })
    for index, floor in enumerate(floor_labels, 1):
        dims = [row for row in audit["interior"] if row["panel"] == index]
        local_groups = [(g, n) for g, n in groups if g["panel"] == index]
        group_before = sum(g["beforeConflicts"] for g, _ in local_groups)
        group_after = sum(g["afterConflicts"] for g, _ in local_groups)
        overlap = sum(g["overlaps"] for g, _ in local_groups) if all(
            "overlaps" in g for g, _ in local_groups) else "未记录"
        written = sum(g["writtenConflicts"] for g, _ in local_groups) if all(
            "writtenConflicts" in g for g, _ in local_groups) else "未记录"
        before_side = sum(g["beforeSideIssues"] for g, _ in local_groups) if all(
            "beforeSideIssues" in g for g, _ in local_groups) else "未记录"
        after_side = sum(g.get("sideIssues", 0) for g, _ in local_groups)
        unreadable = sum(n for _, n in local_groups)
        remaining = sum(row["stock"] for row in dims)
        converted = sum(bool(row.get("converted")) for row in dims)
        new = sum(row["added"] for row in dims)
        old_native = len(dims) - remaining - converted - new
        figure = (folder / key / f"图幅{index}.png").relative_to(folder).as_posix()
        yield (f"| {CASES[key][0]} {floor} | {old_native}／{new} | {converted}／{remaining} | "
               f"{len(local_groups)} | {before_chains[index]} | {group_before} → {written} → {group_after} | "
               f"{overlap} | {before_side} → {after_side} | {unreadable} | "
               f"[原生图面]({figure}) |")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    parser.add_argument("--baseline", type=Path, default=BASELINE)
    parser.add_argument("--require-clearance", action="store_true",
                        help="require zero reopened conflicts, unreadable boxes and side issues")
    parser.add_argument("--require-resolved", action="store_true")
    args = parser.parse_args()
    folder = args.run.resolve()
    summary = read(folder / "验证摘要.json")
    require(set(summary["cases"]) == set(CASES), "expected all six original and historical DWG inputs")
    verify_host(folder, summary)
    lines = ["# 室内尺寸联合排版无 BCP 逐层复验", "",
             "使用隔离 AutoCAD 2020 和天正宿主。运行前后模块清单未见 BCP，六份输入指纹与上一轮相同。"
             "输出 DWG 已独立重开，逐幅保存原生图面。名称、新落面积和既有面积复算记录与上一轮一致。"
             "以下是程序读回和图面证据索引，"
             "可见净空和交互编辑仍以相应核图或试拖记录为准。", "",
             "上轮警报以涉及天正链的数量计。本轮按组比较试排前、写盘前与独立重开后的冲突。"
             "净空按纸面 0.35 mm 检查，直接交叠一列按零额外净空统计。", ""]
    all_results = {}
    unresolved = Counter()
    style_results = {}
    for key, (_, floors, _) in CASES.items():
        baseline = read(audit_path(args.baseline, key))
        audit, groups = verify_case(folder, key, summary["cases"][key], baseline, len(floors))
        verify_names_and_areas(folder, args.baseline, key, audit, baseline)
        style_rows = read(folder / key / "尺寸样式字高核验.json")["dimensions"]
        style_by_handle = {row["handle"]: row["passed"] for row in style_rows}
        style_ok = sum(style_by_handle.get(row["handle"]) is True for row in audit["interior"])
        style_results[key] = (style_ok, len(audit["interior"]))
        all_results[key] = audit, groups, baseline
        unresolved.update(普通尺寸待转换=sum(row["stock"] for row in audit["interior"]),
                          组内冲突=sum(g["afterConflicts"] for g, _ in groups),
                          无法判读=sum(n for _, n in groups),
                          同侧待核=sum(g.get("sideIssues", 0) for g, _ in groups),
                          新建侧向不符=sum(row["added"] and not row["textOnRequestedStationSide"]
                                          for row in audit["interior"]),
                          样式字高待核=len(audit["interior"]) - style_ok)
    for label, original in (("原始 DWG 副本", True), ("带既有标注的历史成图副本", False)):
        lines.extend([f"## {label}", "",
                      "| 项目及楼层 | 既有／新建天正链 | 普通已转／保留 | 排版组 | 上轮天正链警报 | 本轮冲突 | 直接交叠 | 同侧待核前后 | 无法判读 | 图面 |",
                      "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |"])
        for key, (_, floors, is_original) in CASES.items():
            if is_original == original:
                lines.extend(table_rows(folder, key, floors, *all_results[key]))
        lines.append("")
    lines.extend(["每幅普通尺寸与原测点、投影方向及测值逐条回读比对。原方案某幅链数为零时，"
                  "其结果只代表该输入现有尺寸覆盖。历史成图用于验证已有链的排位。", "",
                  "## 待处理与原生证据", ""])
    for key, (audit, _, _) in all_results.items():
        pending = Counter(kind for kind, _, _ in audit["pending"] if kind in DIMENSION_PENDING)
        lines.append(f"- {key} [候选 DWG]({key}/审阅候选.dwg) · [原生读回及对象定位]({key}/plan-native-result.json)")
        style_ok, style_total = style_results[key]
        if style_total:
            lines.append(f"  室内尺寸有效样式与纸面字高通过 {style_ok}／{style_total} 条。[读回记录]({key}/尺寸样式字高核验.json)")
        if pending:
            kinds = "、".join(f"{kind} {n} 项" for kind, n in sorted(pending.items()))
            lines.append(f"  {kinds}。逐条位置与原因见原生读回。")
        panels = {row["handle"]: row["panel"] for row in audit["interior"]}
        for kind, handle, detail in audit["pending"]:
            if kind == "室内尺寸避让" and handle in panels:
                floor = CASES[key][1][panels[handle] - 1]
                lines.append(f"  - {floor} `{handle}` {detail}")
        rejected = {}
        for kind, handle, reason in audit["pending"]:
            if kind == "普通投影尺寸待转":
                rejected.setdefault(PROJECTION_REASONS.get(reason, reason), []).append(handle)
        for reason, handles in rejected.items():
            lines.append(f"  {reason}，保留普通尺寸 {len(handles)} 条，源句柄 {'、'.join(handles)}。")
        traced = [row for row in audit["interior"] if row["stock"] and "sourceTextPosition" in row]
        if traced:
            text = sum(math.dist(row["sourceTextPosition"], row["textPosition"]) > 1e-5 for row in traced)
            baselines = sum(math.dist(row["sourceBaselinePoint"], row["baselinePoint"]) > 1e-5 for row in traced)
            lines.append(f"  保留的普通尺寸中有 {text} 条文字位置、{baselines} 条基线定义点位置调整，原测点与投影值通过重开核验。")
    lines.extend(["", "[运行摘要](验证摘要.json) · [启动模块清单](worker-modules.txt) · "
                  "[收尾模块清单](worker-modules-after.txt)", ""])
    report = folder / "室内尺寸联合排版逐层复验.md"
    report.write_text("\n".join(lines), encoding="utf-8")
    print(report)
    print("Six source DWGs and eighteen reopened plots verified. Unresolved:", dict(unresolved))
    clearance = sum(unresolved[key] for key in ("组内冲突", "无法判读", "同侧待核", "新建侧向不符"))
    if args.require_resolved and sum(unresolved.values()):
        return 1
    return 1 if args.require_clearance and clearance else 0


if __name__ == "__main__":
    raise SystemExit(main())
