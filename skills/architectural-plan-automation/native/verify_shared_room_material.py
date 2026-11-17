"""Compare full PLAN_DRAW room output to the independently reopened refinement baseline."""

import argparse
from decimal import Decimal, ROUND_HALF_UP
from pathlib import Path

from verify_examples import read, sha


def verify(generated, baseline):
    output = read(generated / "product-result.json")
    assert output["completed"] and output["finalDocumentOpened"]
    assert sha(generated / "审阅候选.dwg") == output["outputSHA256"]
    expected_names = read(baseline / "平面名称文字终核.json")
    actual_names = read(generated / "平面名称文字终核.json")
    assert expected_names["savedReadback"] and actual_names["savedReadback"]
    expected = {r["source"]: r for r in expected_names["names"] if r["status"] == "passed"}
    actual = {r["source"]: r for r in actual_names["names"] if r["status"] == "passed"}
    assert len(expected) == 12 and set(actual) == set(expected), (sorted(expected), sorted(actual))
    for handle, row in actual.items():
        assert row["actualType"] == "TDbText" and row["actualStyle"] == "FY_HZ"
        assert row["actualText"] == expected[handle]["actualText"] and row["panel"] == expected[handle]["panel"]

    def areas(report, names):
        source = {r["output"]: r["source"] for r in names["names"] if r["output"]}
        return {(a["panel"], source.get(a["room"], a["room"])): a for a in report}

    before = areas(read(baseline / "plan-native-result.json")["areas"], expected_names)
    receipt = read(generated / "room-area-readback.json")
    after = areas(receipt["areas"], actual_names)
    assert receipt["savedReadback"] and len(before) == 10 and set(after) == set(before), (sorted(before), sorted(after))
    for key, row in after.items():
        assert abs(row["m2"] - before[key]["m2"]) < 1e-6 and row["body"] and row["superscript"], key
        rounded = Decimal(str(row["m2"])).quantize(Decimal(".01"), rounding=ROUND_HALF_UP)
        assert row["actualText"] == f"{rounded}m" and row["actualSuperscript"] == "2", key
        assert len(row["bodyAnchor"]) == len(row["superscriptAnchor"]) == 3, key
    return before, after


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("generated", type=Path)
    parser.add_argument("baseline", type=Path)
    args = parser.parse_args()
    verify(args.generated, args.baseline)
    print("12 source names and 10 measured room area labels verified")
