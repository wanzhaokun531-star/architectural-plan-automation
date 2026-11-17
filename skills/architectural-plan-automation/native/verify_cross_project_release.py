"""Check saved drawing checks against publication; a release suite needs a formal success."""
import argparse
import hashlib
import json
from pathlib import Path


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def verify(report, require_visual_review=False):
    ledger = read(report / "plan-rule-ledger.json")
    release = read(report / "plan-delivery-review.json")
    receipt = read(report / "product-result.json")
    drawing = Path(receipt["output"])
    digest = hashlib.sha256(drawing.read_bytes()).hexdigest().upper()
    assert digest == receipt["outputSHA256"] == release["outputSHA256"] == ledger["outputSHA256"]
    assert receipt["published"] and release["formalRequested"]
    assert release["formalReady"] == ledger["formalReady"] == receipt["constructionDrawingComplete"]
    checks = ledger["drawingChecks"]
    assert checks and all(row["rules"] and row["basis"] for row in checks)
    locations = {(row["panel"], row["source"]): row for row in release["blockingLocations"]}
    for check in checks:
        if check["status"] not in {"failed", "pending"}:
            continue
        key = check["panel"], check["source"] or check["scope"]
        assert key in locations, ("unreported check", check)
        assert any(row["check"] == check["scope"] and row["gate"] == check["gate"]
                   for row in locations[key]["findings"]), check
        assert not release["gates"][check["gate"]] and not release["formalReady"]
    if release["formalReady"]:
        assert all(release["gates"].values()) and not locations
        assert receipt["deliveryStatus"] == "formal"
        layout = read(report / "formal-layout-print-review.json")
        assert layout["outputSHA256"] == digest
        numbers = {row["panel"] for row in release["sheetChecks"]
                   if row["kind"] == "NUMBER" and row["verified"]}
        pages = layout["pages"]
        assert numbers and len(pages) == len(numbers) == release["plottedPages"]
        assert {page["panel"] for page in pages} == numbers
        for page in pages:
            assert page["status"] == "plotted" and page["paperScale"] == 1
            assert page["viewportScale"] > 0
            file = (report / page["file"]).resolve()
            assert file.parent == report.resolve() and file.suffix.lower() == ".pdf"
            assert hashlib.sha256(file.read_bytes()).hexdigest().upper() == page["sha256"]
        if require_visual_review:
            visual = read(report / "plan-visual-review.json")
            assert visual["outputSHA256"] == digest and visual["outcome"] == "accepted"
            visual_pages = visual["pages"]
            assert len(visual_pages) == len(pages)
            assert {page["panel"]: page["sha256"] for page in visual_pages} == {
                page["panel"]: page["sha256"] for page in pages
            }
            assert all(page["checked"] and not page["blockingFindings"] for page in visual_pages)
    else:
        assert locations and receipt["deliveryStatus"] == "pending-review"
        assert Path(receipt["blockingChecklist"]).is_file()
    return release["formalReady"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("reports", type=Path, nargs="+")
    parser.add_argument("--allow-pending-suite", action="store_true",
                        help="mechanism regression only; does not certify formal delivery")
    parser.add_argument("--require-visual-review", action="store_true",
                        help="also verify visual inspection of the exact plotted PDF pages")
    args = parser.parse_args()
    successes = sum(verify(report, args.require_visual_review) for report in args.reports)
    if not args.allow_pending_suite:
        assert successes, "No real formal success: this suite cannot certify formal delivery"
    print(json.dumps({"reports": len(args.reports), "formalSuccesses": successes,
                      "mechanismChecksPassed": True,
                      "formalReleaseSuitePassed": not args.allow_pending_suite and successes > 0}))


if __name__ == "__main__":
    main()
