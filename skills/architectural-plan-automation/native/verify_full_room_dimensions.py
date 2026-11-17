"""Check the saved PLAN_DRAW result against the Liu source and old full run."""

import argparse
from math import dist
from pathlib import Path
import shutil
import subprocess

from verify_examples import ROOT, dispatch, read, run_examples, sha, write
from verify_indoor_stairs import LIU_FLIGHTS


def verify(result, baseline, original):
    def west_pair(report):
        return [row for row in report["labels"] if row["side"] == 0 and row["row"] == 3
                and row["panel"] == 1 and round(row["value"]) in {180, 220}]

    old_report = read(baseline / "外围尺寸文字核验.json")
    for row in old_report["labels"]:
        row.setdefault("panel", 1 if row["handle"] == "4512" else None)
    before = west_pair(old_report)
    assert len(before) == 2 and all(row["conflict"] for row in before), before
    after = west_pair(read(result / "外围尺寸文字重开核验.json"))
    assert len(after) == 2 and all(not row["conflict"] for row in after), after
    assert all(row["leader"] is None for row in after), after
    prior_perimeter = read(baseline / "外围间距核验.json")["dimensions"]
    final_perimeter = read(result / "外围间距核验.json")["dimensions"]
    fields = ("side", "row", "baseline", "stations", "values")
    assert len(prior_perimeter) == len(final_perimeter)
    for a, b in zip(prior_perimeter, final_perimeter):
        assert all(a[field] == b[field] for field in fields), (a, b)

    saved = read(result / "interior-joint-readback.json")
    assert saved["reopened"] and not saved["bcpLoaded"], saved
    original_rows = {row["handle"]: row for row in read(original)["interior"] if not row["added"]}
    retained = {row["source"]: row for row in saved["chains"] if row["source"]}
    assert original_rows.keys() <= retained.keys(), original_rows.keys() - retained.keys()
    for handle, before_chain in original_rows.items():
        after_chain = retained[handle]
        assert len(after_chain["stationCoordinates"]) == len(before_chain["stationCoordinates"])
        assert all(dist(a, b) < 1e-5 for a, b in
                   zip(after_chain["stationCoordinates"], before_chain["stationCoordinates"])), handle
        assert len(after_chain["values"]) == len(before_chain["values"])
        assert all(abs(a-b) < 1e-5 for a, b in zip(after_chain["values"], before_chain["values"])), handle
    for row in saved["chains"]:
        assert row["measurementVerified"], row
        assert row["witnessVerified"], row
    assert {row["panel"] for row in saved["chains"]} == {1, 2}
    assert any(row["added"] for row in saved["chains"])
    assert all(row["unreadable"] == 0 for row in saved["groups"])
    return {"interior": saved, "westBefore": before, "westAfter": after}


def preview_generated(session, ready, work, final, result, roots):
    bounds = [view["bounds"] for view in read(final / "通用输入核验.json")["views"]]
    x0, y0, _, _ = bounds[0]
    windows = {"西侧短尺寸": (x0-4800, y0+4300, x0+1600, y0+7200)}
    for i, (x0, y0, x1, y1) in enumerate(bounds, 1):
        windows[f"图幅{i}"] = (x0-5500, y0-6100, x1+5500, y1+5500)
    for label, bounds in windows.items():
        preview = work / f"plot-{label}"
        preview.mkdir()
        control = preview / "control.dwg"
        shutil.copyfile(result, control)
        window = " ".join(format(value, ".17g") for value in bounds)
        (preview / "annotation-index.tsv").write_text(
            f"S2DIMPREVIEW3 0 {roots} {window} 594 420\n", encoding="ascii")
        dispatch(session, ready, "S1OPENPROBECONTROL", preview, "native-open.done", 45)
        dispatch(session, ready, "S1PHASE2PREVIEW", preview, "stage2-preview-native.json", 70)
        plot = read(preview / "stage2-preview-native.json")
        assert plot["error"] is None and plot["originalBefore"] == plot["originalAfter"], plot["error"]
        assert sha(control) == sha(result)
        subprocess.run(["pdftoppm", "-scale-to", "2400", "-png", "-singlefile",
                        str(preview / "stage2-A2-1-100.pdf"), str(final / label)], check=True,
                       creationflags=subprocess.CREATE_NO_WINDOW)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--module", type=Path)
    parser.add_argument("--resources", type=Path,
                        default=Path.home() / ".codex/skills/architectural-plan-automation/assets")
    parser.add_argument("--plots", action="store_true")
    args = parser.parse_args()
    baseline = ROOT / "outputs/刘府_PLAN_DRAW共用房间资源复验_20261113"
    if args.module:
        source = Path("D:/资料/方案/茶陵严塘镇玳溪村7组刘府-建筑平面方案.dwg")
        assert sha(source) == read(baseline / "原图深化对照/plan-native-result.json")["sourceSHA256"]

        def configure(name, source, digest, work, settings):
            stairs = work / "indoor-stairs.tsv"
            stairs.write_text("\n".join((f"PLANSTAIRS1 {digest}", *LIU_FLIGHTS, "END")) + "\n", encoding="ascii")
            settings.update(mode="generate", indoorStairs=stairs, resources=args.resources.resolve())

        summary = run_examples(args.output, ("整图生成",), module=args.module,
                               extra_samples={"整图生成": source}, plots=False, configure=configure,
                               preview_generated=preview_generated if args.plots else None)
        assert summary["nativeQuit"] is True
    report = verify(args.output / "整图生成", baseline / "整图生成", baseline / "原图深化对照/plan-native-result.json")
    write(args.output / "逐链复验.json", report)


if __name__ == "__main__":
    main()
