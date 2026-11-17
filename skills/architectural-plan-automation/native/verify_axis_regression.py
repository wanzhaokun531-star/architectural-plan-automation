"""Verify that axis policy changes do not regress the three original-plan refinements."""

import argparse
import json
from pathlib import Path

from verify_joint_layout import BASELINE, CASES, read, verify_case, verify_host, verify_names_and_areas


def verify(folder, baseline_folder, historical):
    summary = read(folder / "验证摘要.json")
    inputs = {key for key, (_, _, original) in CASES.items() if original != historical}
    assert set(summary["cases"]) == inputs
    verify_host(folder, summary)
    results = {}
    for key in sorted(inputs):
        floors = CASES[key][1]
        baseline = read(baseline_folder / key / "plan-native-result.json")
        source_baseline = read(BASELINE / key / "plan-native-result.json")
        audit, groups = verify_case(folder, key, summary["cases"][key], source_baseline, len(floors))
        fields = ("sourceHandle", "stock", "stationCoordinates", "values")
        assert [{k: row.get(k) for k in fields} for row in audit["interior"]] == [
            {k: row.get(k) for k in fields} for row in baseline["interior"]]
        verify_names_and_areas(folder, baseline_folder, key, audit, baseline)
        axes = read(folder / key / "axis-retention.json")
        assert axes["reopened"] and not axes["unlocated"]
        assert [p["panel"] for p in axes["panels"]] == list(range(1, len(floors)+1))
        assert sum(p["lines"] + p["labels"] for p in axes["panels"]) == len(axes["objects"])
        results[key] = {
            "axesByFloor": dict(zip(floors, axes["panels"])),
            "netAreaLabels": len(audit["areas"]),
            "insideDimensions": len(audit["interior"]),
            "convertedProjectionDimensions": sum(bool(d.get("converted")) for d in audit["interior"]),
            "retainedStockDimensions": sum(d["stock"] for d in audit["interior"]),
            "remainingConflicts": sum(g["afterConflicts"] for g, _ in groups),
        }
    return results


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("--historical", action="store_true")
    options = parser.parse_args()
    print(json.dumps(verify(options.run, options.baseline, options.historical), indent=2, ensure_ascii=False))
