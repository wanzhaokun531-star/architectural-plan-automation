"""Reopen and verify first-floor outdoor step runs in the private AutoCAD host."""

import argparse
import math
from pathlib import Path

from verify_examples import ROOT, assert_no_bcp, read, run_examples, sha, write


YUMUSHAN_DELIVERY = ROOT / "阶段2/项目/衡阳雨母山末轮修订_20261111/交付验收.json"
YUMUSHAN_SOURCE = ROOT / "阶段2/项目/衡阳雨母山_20261110/work/source-read/input.dwg"
STEP_RECEIPT = "exterior-entrance-steps.json"
VALID_STATUSES = {"existing", "no-step", "candidate", "added", "pending"}
PROJECTS = {
    "陈府": {
        "sourceSHA256": "721C6947FA85DFEB6C10328FF6BF942CBAA867F16F117730B983C527A3738CB6",
        # The south porch steps are in the original. The north door opens directly outside.
        "rows": (
            "ENTRY 13649 MAIN EXISTING S -506854.432982493 -503054.432982493 150696.591557123 - - LINES 13636 13637 13638",
            "ENTRY 13653 OTHER CANDIDATE N -508294.432982493 -507394.432982492 - - -",
        ),
        "expected": {"13649": "existing", "13653": "candidate"},
        "candidate": {"door": "13653", "outwardLeaves": 0, "width": 900, "platform": 1000},
        "preserved": {"13649": {"13636": (3800, 150696.591557123), "13637": (3800, 150396.591557123),
                                 "13638": (3800, 150096.591557123)}},
    },
    "衡阳雨母山": {
        "sourceSHA256": "90B61574476D7F362B2520FD8C5185A18AF3E85C848F45BB66A605B0FBCD72D6",
        # Original south steps 1510–1514, and the east exterior door (verified also in the delivered revision).
        "rows": (
            "ENTRY 2E4 MAIN EXISTING S 78633.505519066 83373.505519066 3592.103123680 - - LINES 1510 1511 1512 1513 1514",
            "ENTRY 486 OTHER CANDIDATE E 10692.103123680 12092.103123680 - - -",
        ),
        "expected": {"2E4": "existing", "486": "candidate"},
        "candidate": {"door": "486", "outwardLeaves": 1, "width": 1400, "platform": 1000},
        "preserved": {"2E4": {"1510": (4260, 3592.103123680), "1511": (4740, 3332.103123680),
                               "1512": (4740, 3072.103123680), "1513": (520, None), "1514": (520, None)}},
    },
    "衡阳雨母山既有台阶": {
        "sourceSHA256": "0689420A653AD42F08EE4A564A24993DFB6BFE5E06371E9030003EECA2AD1FD5",
        # The later delivered drawing has the built east platform and steps.
        "rows": (
            "ENTRY 2E4 MAIN EXISTING S 78633.505519066 83373.505519066 3592.103123680 - -",
            "ENTRY 486 OTHER EXISTING E 10692.103123680 12092.103123680 88813.505519066 - -",
        ),
        "expected": {"2E4": "existing", "486": "existing"},
    },
}


def verify_case(folder, case_summary, project, allow_style_pending=False):
    audit = read(folder / "plan-native-result.json")
    drawing = folder / "审阅候选.dwg"
    assert audit["completed"] and audit["reopened"]
    assert audit["bcpLoadedBefore"] is False and audit["bcpLoadedAfter"] is False
    assert sha(Path(case_summary["source"])) == case_summary["SHA256"] == audit["sourceSHA256"]
    assert sha(drawing) == audit["outputSHA256"]
    if project and "priorModelRoots" in project:
        assert audit["modelRoots"] == project["priorModelRoots"], audit["modelRoots"]

    receipt = read(folder / STEP_RECEIPT)
    style_audit = read(folder / "尺寸样式字高核验.json")
    assert style_audit["savedReopened"] and style_audit["nominalStyleMatches"]
    styles = {dimension["handle"]: dimension for dimension in style_audit["dimensions"]}
    assert receipt["savedReopened"] is True
    entries = receipt["entries"]
    assert entries, f"No exterior entrance was verified in {folder}"
    doors = [row["door"] for row in entries]
    assert all(doors) and len(doors) == len(set(doors)), f"Duplicate or empty doors: {doors}"
    results = {}
    for row in entries:
        status = row["status"]
        assert status in VALID_STATUSES, row
        missing = row["missing"]
        if status in {"candidate", "pending"}:
            assert missing, f"No entrance-specific missing evidence: {row}"
        elif status == "added":
            assert not missing, row
        if project and (row.get("upperM") is None or row.get("lowerM") is None) and status != "no-step":
            assert missing and ("标高" in missing or "高差" in missing), row
        entities = row.get("entities", [])
        existing_lines = row.get("existingLines", [])
        pending_styles = []
        if status in {"candidate", "added"}:
            verify_plan(row, entities)
            verify_dimensions(row)
            for dimension in row["dimensions"]:
                observed = styles[dimension["handle"]]
                try:
                    verify_style(dimension, observed, style_audit["modelHeight"])
                except AssertionError:
                    if not allow_style_pending:
                        raise
                    pending_styles.append({**observed, "expectedModelHeight": style_audit["modelHeight"]})
        if status in {"no-step", "existing"}:
            assert not entities and not row.get("dimensions"), f"An entrance with this status has new geometry: {row}"
        if project and row["door"] in project.get("preserved", {}):
            verify_existing(existing_lines, project["preserved"][row["door"]])
        results[row["door"]] = {"status": status, "number": row["number"], "kind": row["kind"],
                                "missing": missing, "entities": [entity["handle"] for entity in entities],
                                "planMM": row.get("planMM"), "upperM": row.get("upperM"),
                                "lowerM": row.get("lowerM"), "side": row.get("side"),
                                "dimensions": row.get("dimensions", []), "outwardLeaves": row.get("outwardLeaves", 0),
                                "existingLines": existing_lines, "stylePending": pending_styles}
    if project:
        statuses = {handle: result["status"] for handle, result in results.items()}
        assert statuses == project["expected"], (folder, project["expected"], statuses)
        if "candidate" in project:
            expected = project["candidate"]
            actual = results[expected["door"]]
            assert actual["outwardLeaves"] == expected["outwardLeaves"], actual
            assert all(close(actual["planMM"][field], expected[field]) for field in ("width", "platform")), actual
    return results


def verify_plan(row, entities):
    plan = row["planMM"]
    assert row["kind"] in {"main", "other"} and row["side"] in {"W", "E", "S", "N"}, row
    assert row["swingChecked"] is True, row
    assert len(entities) in {2, 3} and len({entity["handle"] for entity in entities}) == len(entities), row
    assert all(math.isfinite(plan[field]) and plan[field] > 0 for field in ("width", "platform", "tread", "total")), row
    assert close(plan["platform"] + 2 * plan["tread"], plan["total"]), row
    assert close(plan["tread"], 300 if row["kind"] == "main" else 260), row

    # Run settings use one millimetre per drawing unit. Compare the reported dimensions
    # independently with the actual polyline points read from the saved DWG.
    normal = 0 if row["side"] in {"W", "E"} else 1
    sign = -1 if row["side"] in {"W", "S"} else 1
    tread_box, first_nosing = (item["points"] for item in entities[-2:])
    assert len(tread_box) == 4 and len(first_nosing) == 2, row
    for index in (0, 1):
        assert close(sign * (first_nosing[index][normal] - tread_box[3 * index][normal]), plan["tread"]), row
        assert close(sign * (tread_box[index + 1][normal] - first_nosing[index][normal]), plan["tread"]), row
        assert close(tread_box[index + 1][1 - normal], first_nosing[index][1 - normal]), row
    assert close(abs(first_nosing[1][1 - normal] - first_nosing[0][1 - normal]), plan["width"]), row
    if len(entities) == 3:
        platform = entities[0]["points"]
        assert len(platform) == 4, row
        for index in (0, 1):
            assert close(sign * (platform[index + 1][normal] - platform[3 * index][normal]), plan["platform"]), row
            assert close(platform[index + 1][normal], tread_box[3 * index][normal]), row
    if row["status"] == "added":
        assert row["upperM"] > row["lowerM"] and row["riseCount"] > 0, row


def verify_dimensions(row):
    plan = row["planMM"]
    dimensions = row["dimensions"]
    assert len({dimension["handle"] for dimension in dimensions}) == len(dimensions), row
    depth = [plan["platform"], plan["tread"], plan["tread"]]
    width = [plan["width"]]
    actual = [dimension["measuredMM"] for dimension in dimensions]
    assert any(match_dimensions(values, width) for values in actual), row
    assert any(match_dimensions(values, depth) or match_dimensions(values, depth[::-1]) for values in actual), row


def verify_existing(lines, expected):
    assert {line["handle"] for line in lines} == expected.keys(), (expected, lines)
    for line in lines:
        points = line["points"]
        evidence = expected[line["handle"]]
        if isinstance(evidence, dict):
            assert all(any(math.dist(vertex, point) <= .01 for point in points) for vertex in evidence["vertices"]), line
            continue
        length, depth = evidence
        actual_length = max(math.dist(a, b) for a in points for b in points)
        assert close(actual_length, length), line
        if depth is not None:
            assert all(close(point[1], depth) for point in points), line


def verify_style(dimension, style, model_height):
    assert style["layer"] == "PUB_DIM" and style["native"] and style["passed"], style
    assert len(style["parts"]) == len(dimension["measuredMM"]), style
    assert all(part["style"].startswith("_TCH_ARCH") and close(part["height"], model_height)
               and close(part["scale"], 1) and close(part["fontFixedHeight"], 0) for part in style["parts"]), style


def match_dimensions(actual, expected):
    return len(actual) == len(expected) and all(close(a, b) for a, b in zip(actual, expected))


def close(first, second):
    return math.isclose(first, second, rel_tol=0, abs_tol=.01)


def verify_host(folder, summary):
    terminal = read(Path(summary["session"]) / "isolation-final.json")
    assert summary["nativeQuit"] and terminal["nativeQuitConfirmed"] and terminal["workerExited"]
    assert terminal["workerExitCode"] == 0 and not terminal["forcedTermination"]
    required = f"PlanNative_{terminal['nonce'][:8]}.arx"
    for inventory in ("worker-modules.txt", "worker-modules-after.txt"):
        assert_no_bcp((folder / inventory).read_bytes(), required)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=Path)
    parser.add_argument("--module", type=Path, help="Freshly built PlanNative.arx")
    parser.add_argument("--case", action="append", help="Project key, defaults to 陈府 and 衡阳雨母山")
    parser.add_argument("--plan-parameters", action="append", default=[], metavar="CASE=TSV",
                        help="Optional source-hash-bound entrance parameters")
    parser.add_argument("--auto", action="store_true", help="Run automatic detection without the verified project rows")
    parser.add_argument("--plot", action="store_true", help="Export native previews")
    parser.add_argument("--repeat-source", type=Path, help="Use outputs of a verified prior run and check for duplicate steps")
    parser.add_argument("--repeat-evidence", action="store_true", help="Additionally bind prior generated lines as source evidence")
    parser.add_argument("--allow-style-pending", action="store_true", help="Record style deviations while enforcing geometry")
    parser.add_argument("--check-only", action="store_true", help="Verify completed runs without starting AutoCAD")
    args = parser.parse_args()
    cases = args.case or ["陈府", "衡阳雨母山"]
    parameters = dict(value.split("=", 1) for value in args.plan_parameters)
    expected = {name: PROJECTS.get(name) for name in cases if name not in parameters and not args.auto}
    if args.repeat_source and (args.auto or parameters or any(project is None for project in expected.values())):
        parser.error("--repeat-source requires the built-in project rows")
    if args.repeat_evidence and not args.repeat_source:
        parser.error("--repeat-evidence requires --repeat-source")
    samples = {}
    if args.repeat_source:
        for name, project in expected.items():
            samples[name] = (args.repeat_source / name / "审阅候选.dwg").resolve()
            first_audit = read(args.repeat_source / name / "plan-native-result.json")
            assert sha(samples[name]) == first_audit["outputSHA256"]
            previous = read(args.repeat_source / name / STEP_RECEIPT)
            assert {row["door"]: row["status"] for row in previous["entries"]} == project["expected"]
            expected[name] = {**project, "sourceSHA256": first_audit["outputSHA256"],
                              "expected": dict.fromkeys(project["expected"], "existing"),
                              "priorModelRoots": first_audit["modelRoots"]}
            expected[name].pop("candidate", None)
            if args.repeat_evidence:
                prior_geometry = {row["door"]: {item["handle"]: {"vertices": item["points"]}
                                                for item in row["entities"]}
                                  for row in previous["entries"] if row["status"] == "candidate"}
                expected[name]["preserved"] = {**project.get("preserved", {}), **prior_geometry}
                expected[name]["rows"] = []
                for setting in project["rows"]:
                    fields = setting.split()
                    handle = fields[1]
                    if handle in prior_geometry:
                        fields[3] = "EXISTING"
                        fields.extend(["LINES", *prior_geometry[handle]])
                    expected[name]["rows"].append(" ".join(fields))
    if not args.check_only:

        def configure(name, _source, digest, work, settings):
            project = expected.get(name) or PROJECTS.get(name)
            if project:
                assert digest == project["sourceSHA256"]
            if name in parameters:
                path = Path(parameters[name]).resolve(strict=True)
                assert path.read_text(encoding="utf-8-sig").splitlines()[0] == f"PLANENTRANCE1 {digest}"
                settings["entranceSteps"] = path
            elif project and not args.auto:
                path = work / "entrance-steps.tsv"
                path.write_bytes(("\n".join((f"PLANENTRANCE1 {digest}", *project["rows"], "END")) + "\n").encode("ascii"))
                settings["entranceSteps"] = path

        yumushan = read(YUMUSHAN_DELIVERY)
        regression_source = Path(yumushan["delivered"])
        assert sha(regression_source) == yumushan["sha256"]
        summary = run_examples(args.results, cases, module=args.module, plots=args.plot,
                               extra_samples={"衡阳雨母山": YUMUSHAN_SOURCE, "衡阳雨母山既有台阶": regression_source, **samples},
                               configure=configure)
    else:
        summary = read(args.results / "验证摘要.json")
    verify_host(args.results, summary)
    summary["entranceVerification"] = {
        name: verify_case(args.results / name, summary["cases"][name], expected.get(name), args.allow_style_pending)
        for name in cases
    }
    write(args.results / "室外台阶回归验收.json", summary)
    print({name: {door: item["status"] for door, item in checked.items()}
           for name, checked in summary["entranceVerification"].items()}, flush=True)


if __name__ == "__main__":
    main()
