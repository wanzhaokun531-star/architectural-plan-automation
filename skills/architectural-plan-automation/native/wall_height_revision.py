"""Inspect or revise wall heights, then verify the newly reopened native plan."""

import argparse
from pathlib import Path

from owned_plan_host import RetryableHostError, file_hash, require, run_owned_plan
from unattended_plan import workspace_root
from joint_annotation_repair import read


def verify_revision(report, expected):
    actual = read(report / "storey-wall-height-readback.json")
    current = read(report / "wall-height-preflight.json")
    require(current == expected, "预检与写入阶段的楼层、墙体或取值不一致")
    all_expected = {
        wall["handle"]: (floor["component"], floor["targetMM"])
        for floor in expected["floors"] for wall in floor["walls"] if not wall["nativeLowWall"]
    }
    all_actual = {row["object"]: row for row in actual["walls"]}
    require(actual["savedReopened"] and len(all_actual) == len(actual["walls"]) and
            all_expected.keys() == all_actual.keys(), "重开逐墙验收覆盖不足")
    for handle, (panel, target) in all_expected.items():
        row = all_actual[handle]
        require(row["panel"] == panel and row["targetHeight"] == target, "墙体取值与预检不符")
        if not row["issue"]:
            require(target is not None and abs(row["actualHeight"] - target) <= .01,
                    "重开实际墙高与目标不符")
    receipt = read(report / "product-result.json")
    drawing = report / "candidate.dwg"
    digest = file_hash(drawing)
    sheets = read(report / "formal-layout-print-review.json")
    require(receipt["dwgReopened"] and digest == receipt["outputSHA256"] == sheets["outputSHA256"],
            "墙高成果与同版实打回执不一致")
    require(sheets["pages"] and all(row["status"] == "plotted" and
            file_hash(report / row["file"]) == row["sha256"] for row in sheets["pages"]),
            "墙高成果有未完成的实打图幅")
    return drawing


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--scope", type=Path, required=True)
    parser.add_argument("--sheets", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--floor-levels", type=Path, help="source-bound project elevation evidence")
    parser.add_argument("--preflight", type=Path, help="read-only report directory required before writing")
    args = parser.parse_args()
    source, work = args.source.resolve(strict=True), args.work.resolve()
    skill = Path(__file__).resolve().parents[1]
    root = workspace_root(None, Path.cwd(), skill)
    expected = None
    if args.preflight:
        evidence = read(args.preflight / "product-result.json")
        require(evidence.get("readOnly") and evidence["sourceSHA256"] == file_hash(source),
                "写入前需要当前 DWG 的原生只读预检回执")
        expected = read(args.preflight / "wall-height-preflight.json")
    work.mkdir(parents=True, exist_ok=True)
    for number in range(1, 4):
        attempt = work / f"attempt-{number:02}"
        if attempt.exists():
            continue
        try:
            run_owned_plan(root, skill / "tools/PlanNative.arx", source, attempt, {
                "mode": "wall-height", "resources": str(skill / "assets"),
                "wallScope": str(args.scope.resolve(strict=True)), "wallSheets": str(args.sheets.resolve(strict=True)),
                "floorLevels": str(args.floor_levels.resolve(strict=True)) if args.floor_levels else "",
                "wallInspect": "1" if expected is None else "0",
            })
        except RetryableHostError:
            continue
        report = attempt / "job"
        if expected is not None:
            verify_revision(report, expected)
        print(report)
        return
    raise RuntimeError("墙高隔离宿主已用尽自动重试")


if __name__ == "__main__":
    main()
