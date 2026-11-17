"""Check source coverage and final-DWG binding of a PLAN_DRAW generation report."""

from collections import Counter
from pathlib import Path
import argparse
import hashlib
import json


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def verify_scope_reports(report, source, reconciliation):
    paths = [report / name for name in (
        "plan-panel-ownership.json",
        "perimeter-source-reconciliation.json",
        "source-dimension-disposition.json",
    )]
    if not any(path.exists() for path in paths[:1] + paths[2:]):
        return {}
    ownership, perimeter, dimensions = map(read, paths)
    for evidence in (ownership, perimeter, dimensions):
        assert evidence["savedReopened"] and evidence["sourceSHA256"] == source["sourceSHA256"]
    assert dimensions["outputSHA256"] == reconciliation["outputSHA256"]
    items = {(item["panel"], item["scope"], item["source"]): item for item in reconciliation["items"]}
    components = {row["component"]: row for row in ownership["components"]}
    assert len(components) == len(ownership["components"])
    for component, row in components.items():
        assert row["reason"] and row["result"] == items[component, "panel", ""]["status"]
        if row["role"] in {"roof", "detail", "unassigned", "outside-confirmed-scope"}:
            assert not row["generated"]
            assert not any(panel == component and scope in {"floor-datum", "story-height", "perimeter", "wall-height"}
                           for panel, scope, _ in items)
    for side in perimeter["sides"]:
        assert len(side["rowChecks"]) == 3
        if side["result"] == "verified-source-station-and-visible-chain-reconciliation":
            assert all(side["rowChecks"][:2])
            assert side["rowChecks"][2] or side["thirdOmittedWithEvidence"]
            assert not side["missingAxisStations"] and not side["reasons"]
        else:
            assert side["reasons"]
    keys = {(row["panel"], row["source"]) for row in dimensions["dimensions"]}
    assert len(keys) == len(dimensions["dimensions"])
    assert keys == {(panel, handle) for panel, scope, handle in items if scope == "source-dimension-disposition"}
    for row in dimensions["dimensions"]:
        item = items[row["panel"], "source-dimension-disposition", row["source"]]
        assert row["result"] == item["status"] and row["targets"] == item["objects"] and row["reason"]
        if row["disposition"] == "replaced":
            assert not row["visibleOriginal"] and row["targets"] and row["sourceReadable"]
        if row["disposition"] == "rebuilt-perimeter":
            assert not row["originalExists"] and not row["visibleOriginal"]
            assert row["sourceReadable"] and len(row["targets"]) in (2, 3)
            side = next(s for s in perimeter["sides"] if row["source"] in s.get("removedSourceMainChains", []))
            assert row["targets"] == [handle for handle in side["selectedChains"] if handle]
        if row["disposition"] == "retained":
            assert row["visibleOriginal"] and row["source"] in row["targets"]
    return {
        "panelRoles": Counter(row["role"] for row in components.values()),
        "perimeterSides": Counter(row["result"] for row in perimeter["sides"]),
        "sourceDimensions": Counter(row["disposition"] for row in dimensions["dimensions"]),
    }


def verify(report):
    source_path = report / "plan-source-inventory.json"
    source = read(source_path)
    reconciliation = read(report / "plan-delivery-reconciliation.json")
    ledger = read(report / "plan-rule-ledger.json")
    receipt = read(report / "product-result.json")
    assert receipt["completed"] and receipt["finalDocumentOpened"]
    assert not source["savedReopened"] and reconciliation["savedReopened"]
    assert digest(source_path) == reconciliation["sourceInventorySHA256"] == ledger["sourceInventorySHA256"]
    assert digest(Path(receipt["source"])) == source["sourceSHA256"] == reconciliation["sourceSHA256"] == ledger["sourceSHA256"]
    output = Path(receipt["output"]) if receipt["output"] else None
    if not receipt.get("published", True):
        assert output is None and not ledger["formalReady"] and not receipt["constructionDrawingComplete"]
        output = report / "过程" / "ready-to-publish.dwg"
    assert output and digest(output) == receipt["outputSHA256"] == reconciliation["outputSHA256"] == ledger["outputSHA256"]
    assert source["masterSHA256"] == reconciliation["masterSHA256"] == ledger["masterSHA256"]
    assert receipt["output"] == reconciliation["output"]
    assert len(source["items"]) == len(reconciliation["items"]) > 0
    seen = set()
    expected, unresolved, failed = Counter(), Counter(), Counter()
    for start, end in zip(source["items"], reconciliation["items"]):
        for field in ("panel", "scope", "source", "basis", "rules"):
            assert start[field] == end[field], (start, end)
        key = (end["panel"], end["scope"], end["source"])
        assert key not in seen, key
        seen.add(key)
        expected.update(end["rules"])
        if end["status"] in {"pending", "failed"}:
            assert end["reason"], end
            unresolved.update(end["rules"])
        if end["status"] == "failed":
            failed.update(end["rules"])
    assert len({row["id"] for row in ledger["rules"]}) == ledger["ruleCount"]
    for row in ledger["rules"]:
        key = row["id"]
        assert row["reason"], row
        assert row["sourceExpected"] == expected[key], row
        assert row["sourceUnresolved"] == unresolved[key], row
        assert row["sourceFailed"] == failed[key], row
    return {
        "rules": ledger["ruleStatusCounts"],
        "items": Counter(item["status"] for item in reconciliation["items"]),
        "scopes": {scope: Counter(item["status"] for item in reconciliation["items"] if item["scope"] == scope)
                   for scope in sorted({item["scope"] for item in reconciliation["items"]})},
        **verify_scope_reports(report, source, reconciliation),
        "outputSHA256": ledger["outputSHA256"],
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports", nargs="+", type=Path)
    args = parser.parse_args()
    print(json.dumps({str(report): verify(report) for report in args.reports}, ensure_ascii=False, indent=2))
