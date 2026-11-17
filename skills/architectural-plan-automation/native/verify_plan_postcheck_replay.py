"""Compare two completed CAD jobs for the same source. Run with --help for usage.

Each job is verified against its own candidate DWG, formal PDFs and source
inventory. Independently saved DWGs and PDFs can differ at the byte level.
Audit equality then applies to the recorded semantic geometry and properties.
Source and generated object handles remain part of that comparison.
"""

import argparse
from collections import Counter
import csv
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import sys


ACCEPTANCE_RANK = {"omitted-with-evidence": 1, "verified": 2}
GATES = ("mainPlan", "reopened", "files")
SEMANTIC_REPORTS = (
    "平面构件数据.json", "通用输入核验.json", "plan-panel-ownership.json",
    "plan-source-annotations.json", "plan-annotation-reconciliation.json",
    "axis-project-readback.json", "外围间距核验.json", "perimeter-source-reconciliation.json",
    "source-dimension-disposition.json", "interior-joint-readback.json",
    "尺寸样式字高核验.json", "尺寸同侧无引线核验.json", "外围尺寸文字重开核验.json",
    "storey-wall-height-readback.json", "room-area-readback.json", "平面名称文字终核.json",
    "plan-furniture-readback.json", "opening-caption-public-fields.json",
    "门窗编号显示层核验.json", "interior-door-measurement-readback.json",
    "exterior-entrance-steps.json", "indoor-stairs.json", "bathroom-door-readback.json",
    "roof-plan-readback.json", "plan-print-geometry.json", "source-text-clearance.json",
)
REPAIR_PROTECTED_REPORTS = {
    "平面构件数据.json", "通用输入核验.json", "plan-panel-ownership.json",
    "plan-source-annotations.json", "storey-wall-height-readback.json",
    "room-area-readback.json", "opening-caption-public-fields.json", "plan-furniture-readback.json",
    "exterior-entrance-steps.json", "indoor-stairs.json", "bathroom-door-readback.json",
    "roof-plan-readback.json",
}
REQUIRED_SEMANTIC_REPORTS = {
    "平面构件数据.json", "plan-panel-ownership.json",
    "plan-annotation-reconciliation.json", "source-dimension-disposition.json",
}
LOCAL_KEYS = ("resources", "projectParameters", "plotInkAnalyzer", "plotInkPython")
AUDIT_RESULT_KEYS = ("verified", "checked", "expected", "unresolved", "acceptedRepairs")


class VerificationError(Exception):
    pass


def require(condition, message):
    if not condition:
        raise VerificationError(message)


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest().upper()


def local_file(directory, name, suffix):
    path = (directory / name).resolve(strict=True)
    require(path.parent == directory and path.is_file() and path.suffix.lower() == suffix,
            f"Expected {suffix} in {directory}: {name}")
    return path


def index(rows, fields, name):
    result = {}
    for row in rows:
        key = tuple(row[field] for field in fields)
        require(key not in result, f"{name}: duplicate {key}")
        result[key] = row
    return result


def settings_identity(checkpoint, review, host, ledger):
    entry = checkpoint["identity"]["settings"]
    config = dict(entry["nativeConfiguration"])
    mode = config.pop("postcheckMode", "off")
    require(mode in {"off", "audit", "repair"}, f"Unknown postcheckMode: {mode}")
    require(config.get("acceptance") == "architectural-plan" and config.get("delivery") == "formal"
            and config.get("printReview") == "1", "Settings did not request formal architectural-plan review")
    references = {key: Path(config.pop(key)) for key in LOCAL_KEYS if key in config}
    files = {Path(name): digest for name, digest in entry["inputFilesSHA256"].items()}
    module_matches = [path for path, digest in files.items()
                      if path.suffix.casefold() == ".arx" and digest == host["moduleSHA256"]]
    require(len(module_matches) == 1 and host["moduleSHA256"] == ledger["engineSHA256"],
            "Configured, loaded and reporting native module hashes differ")
    files.pop(module_matches[0])
    if "projectParameters" in references:
        require(files.get(references["projectParameters"]) == review["projectParametersSHA256"],
                "Project parameter hash differs from formal review")

    scoped_files = {}
    resources = references.get("resources")
    project = references.get("projectParameters")
    for path, digest in files.items():
        if resources and path.is_relative_to(resources):
            key = ("resources", path.relative_to(resources).as_posix())
        elif path == references.get("plotInkAnalyzer"):
            key = ("analyzer", path.name)
        elif project and path == project:
            key = ("project", "parameters")
        elif project and path.is_relative_to(project.parent):
            key = ("project", path.relative_to(project.parent).as_posix())
        else:
            key = ("explicit-input", path.as_posix())
        require(key not in scoped_files, f"Ambiguous settings input: {key}")
        scoped_files[key] = digest
    if "plotInkPython" in references:
        config["plotInkPython"] = references["plotInkPython"].as_posix()
    return mode, config, scoped_files


def read_plots(path, config, drawing_hash=None, *, allow_failed=False, allowed_panels=None):
    report = read(path)
    if drawing_hash is not None:
        require(report["outputSHA256"] == drawing_hash, f"{path}: plot report refers to a different DWG")
    rows = index(report["pages"], ("panel",), str(path))
    expected = {(i,) for i in range(1, len(rows) + 1)} if allowed_panels is None else allowed_panels
    require(rows and (set(rows) == expected if allowed_panels is None else set(rows) <= expected),
            f"{path}: missing or nonconsecutive plot panels")
    viewport_scale = float(config["unitMM"]) / float(config["scale"])
    names = set()
    for row in rows.values():
        if allow_failed and row.get("status") == "failed":
            continue
        require(row.get("status") == "plotted", f"{path}: plot did not succeed on panel {row['panel']}")
        pdf = local_file(path.parent, row["file"], ".pdf")
        with pdf.open("rb") as stream:
            require(stream.read(5) == b"%PDF-", f"Invalid PDF signature: {pdf}")
        require(row["file"] not in names and row["sha256"] == sha(pdf), f"{path}: duplicate or mismatched PDF: {pdf.name}")
        require(row["paperScale"] == 1 and math.isclose(row["viewportScale"], viewport_scale, rel_tol=1e-5),
                f"{path}: plot scale differs from the configured drawing scale")
        names.add(row["file"])
    return report, {key: row for key, row in rows.items() if row["status"] == "plotted"}


def page_hashes(rows):
    return {(row["file"], row["sha256"]) for row in rows}


def panel_pages(rows):
    return {(row["panel"], row["file"], row["sha256"]) for row in rows}


def read_plot_proof(directory, drawing_hash, pages):
    request = read(directory / "paper-ink-input.json")
    require(request["contract"] == "plan.printed-ink-input.v1" and request["drawingSHA256"] == drawing_hash,
            f"{directory}: paper analyzer input is for a different DWG")
    frames = index(request["frames"], ("panel",), f"{directory}: paper analyzer input")
    require(panel_pages(frames.values()) == panel_pages(pages.values()),
            f"{directory}: analyzer input PDFs differ from actual plotted pages")
    targets = {}
    for (panel,), frame in frames.items():
        for key, row in index(frame["targets"], ("handle", "segment"), f"{directory}: panel {panel}").items():
            targets[(panel, *key)] = row
    raw = (directory / "paper-ink-result.tsv").read_text(encoding="utf-8-sig")
    lines = [line.strip() for line in raw.splitlines() if line.strip()]
    require(len(lines) >= 2 and lines[-1] == "END", f"{directory}: incomplete raw print analysis")
    tag, digest, *counts = lines[0].split()
    plotted, expected, checked, missing, collision, moves = map(int, counts)
    require(tag == "PLANINKRESULT1" and digest == drawing_hash and plotted == len(pages) and
            expected == len(targets) and 0 <= checked <= expected and missing == expected - checked,
            f"{directory}: raw print analysis hash/target count mismatch")
    findings = {"MISSING": set(), "COLLISION": set(), "MOVE": set()}
    for line in lines[1:-1]:
        kind, panel, handle, segment, *args = line.split()
        key = int(panel), handle, int(segment)
        require(kind in findings and key in targets and key not in findings[kind],
                f"{directory}: invalid or duplicate plotted segment {line}")
        require(len(args) == (2 if kind == "MOVE" else 0) and all(math.isfinite(float(a)) for a in args),
                f"{directory}: malformed analyzer row {line}")
        findings[kind].add(key)
    require((len(findings["MISSING"]), len(findings["COLLISION"]), len(findings["MOVE"])) ==
            (missing, collision, moves) and not findings["MISSING"] & findings["COLLISION"],
            f"{directory}: raw print analysis findings disagree with summary")
    return {"targets": targets, "frames": frames, "checked": checked,
            "missing": findings["MISSING"], "collisions": findings["COLLISION"]}


def utc_timestamp(value):
    if isinstance(value, (int, float)):
        return value
    if isinstance(value, str):
        try:
            parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
        except ValueError:
            return None
        return parsed.timestamp() if parsed.tzinfo else None
    return None


def timings(job, host_path, host, attempt, postcheck, plot_paths):
    markers = sorted(job.glob("*.step"), key=lambda path: path.stat().st_mtime)
    start = next((instant for data in (attempt, host, postcheck or {})
                  if (instant := utc_timestamp(data.get("startedAt"))) is not None), None)
    basis = "recorded startedAt" if start is not None else "host.json write time"
    if start is None:
        dispatch_path = Path(host["session"]) / "native-dispatch.intent.json"
        if dispatch_path.is_file() and read(dispatch_path).get("nonce") == host["nonce"]:
            start = dispatch_path.stat().st_mtime
            basis = "bound native-dispatch.intent.json write time"
        else:
            start = host_path.stat().st_mtime

    def seconds(path):
        return round(path.stat().st_mtime - start, 3)

    return {
        "timeOrigin": basis,
        "originUtc": datetime.fromtimestamp(start, timezone.utc).isoformat(),
        "phaseMarkersSeconds": {path.name: seconds(path) for path in markers},
        "firstSavedModelSeconds": seconds(job / "过程" / "ready-to-publish.dwg"),
        "printedPassReportSeconds": {
            path.parent.name: seconds(path) for path in sorted(plot_paths, key=lambda path: path.stat().st_mtime)
        },
        "productReceiptSeconds": seconds(job / "product-result.json"),
        "postcheckReceiptSeconds": None if postcheck is None else seconds(job / "plan-postcheck.json"),
        "generationMilliseconds": None if postcheck is None else postcheck.get("generationMilliseconds"),
        "auditMilliseconds": None if postcheck is None else postcheck.get("auditMilliseconds"),
    }


def verify_job(job):
    checkpoint = read(job.parent.parent / "checkpoint.json")
    completed_attempts = [row for row in checkpoint["attempts"] if row["directory"] == job.parent.name]
    require(len(completed_attempts) == 1 and completed_attempts[0]["status"] in
            {"delivered", "verified", "verified-with-exceptions"}, f"{job}: no completed checkpoint attempt")
    attempt = completed_attempts[0]
    host_path = job.parent / "host.json"
    host = read(host_path)
    receipt = read(job / "product-result.json")
    review = read(job / "plan-delivery-review.json")
    ledger = read(job / "plan-rule-ledger.json")
    inv_path = job / "plan-source-inventory.json"
    inventory = read(inv_path)
    reconciliation = read(job / "plan-delivery-reconciliation.json")
    require(receipt["contract"] == "bcp.product-result.v1" and
            all(receipt[key] is True for key in ("completed", "published", "sourceUnchanged", "finalDocumentOpened")),
            f"{job}: missing completed, published, protected and reopened receipt")
    source_hash = sha(Path(checkpoint["identity"]["source"]))
    require(checkpoint["identity"]["sourceSHA256"] == source_hash and
            all(doc["sourceSHA256"] == source_hash for doc in (receipt, review, ledger, inventory, reconciliation)),
            f"{job}: source hash mismatch")
    output = local_file(job, Path(receipt["output"]).name, ".dwg")
    output_hash = sha(output)
    require(attempt["sha256"] == output_hash and
            all(doc["outputSHA256"] == output_hash for doc in (receipt, review, ledger, reconciliation)),
            f"{job}: actual DWG, receipt, P04 review, ledger and reconciliation hashes differ")
    inventory_hash = sha(inv_path)
    require(reconciliation["savedReopened"] is True and
            reconciliation["sourceInventorySHA256"] == ledger["sourceInventorySHA256"] == inventory_hash and
            inventory["masterSHA256"] == reconciliation["masterSHA256"] == ledger["masterSHA256"],
            f"{job}: inventory or rule fingerprint mismatch")
    item_key = ("panel", "scope", "source")
    expected = index(inventory["items"], item_key, "source inventory")
    source_checks = index(reconciliation["items"], item_key, "source reconciliation")
    require(expected.keys() == source_checks.keys() and all(
        (item["basis"], item["rules"]) == (source_checks[key]["basis"], source_checks[key]["rules"])
        for key, item in expected.items()
    ), f"{job}: source coverage or item basis differs between inventory and reconciliation")
    draw_checks = index(ledger["drawingChecks"], item_key, "native drawing checks")
    rules = index(ledger["rules"], ("id",), "rule ledger")
    require(len(rules) == ledger["ruleCount"] and review["formalRequested"] is True
            and review["acceptanceMode"] == "architectural-plan" and review["dwgReopened"] is True,
            f"{job}: incomplete P04 or rule review")
    gates = review["gates"]
    require(set(gates) == set(GATES) and all(type(value) is bool for value in gates.values()),
            f"{job}: malformed P04 gates")
    formal_ready = review["formalReady"]
    require(formal_ready is all(gates.values()) and formal_ready is ledger["formalReady"]
            and formal_ready is receipt["constructionDrawingComplete"] and formal_ready is attempt["formal"]
            and review["formalDelivery"] is receipt["formalDelivery"] is True,
            f"{job}: formal delivery, strict acceptance and checkpoint disagree")
    if formal_ready:
        require(not review["blockingLocations"] and receipt["deliveryStatus"] == "formal",
                f"{job}: passing job reports blocking issues")
    else:
        require(review["blockingLocations"] and receipt["deliveryStatus"] == "formal-with-exceptions",
                f"{job}: failed P04 gate without documented exceptions")
        checklist = local_file(job, Path(receipt["blockingChecklist"]).name, ".csv")
        require(sha(checklist) == receipt["checklistSHA256"] == attempt["checklistSHA256"],
                f"{job}: associated exception checklist hash mismatch")
        with checklist.open(encoding="utf-8-sig", newline="") as stream:
            checklist_rows = list(csv.DictReader(stream))
        blocking = {(str(row["panel"]), row["source"]) for row in review["blockingLocations"] if row["findings"]}
        require(blocking == {(row["源图幅"], row["源对象"]) for row in checklist_rows}
                and len(checklist_rows) == len(blocking)
                and all(row["DWG SHA-256"] == output_hash for row in checklist_rows),
                f"{job}: exception rows do not match same-version P04 blockers")

    mode, config, inputs = settings_identity(checkpoint, review, host, ledger)
    formal_plots, final_pages = read_plots(job / "formal-layout-print-review.json", config, output_hash)
    plots = read(job / "plan-print-review.json")
    ink = read(job / "plan-printed-ink.json")
    expected_pages = page_hashes(final_pages.values())
    require(plots["outputSHA256"] == ink["drawingSHA256"] == output_hash and
            panel_pages(final_pages.values()) == panel_pages(plots["pages"]) and
            expected_pages == page_hashes(ink["pages"]) and
            len(final_pages) == len(plots["pages"]) == len(ink["pages"]) == review["plottedPages"] and
            plots["plotStyle"] == config["plotStyle"] and float(plots["scale"]) == float(config["scale"]),
            f"{job}: final printed PDF or style evidence differs between reports")
    require(ink["contract"] == "plan.printed-ink.v1" and ink["verified"] is (ink["unresolved"] == 0) and
            0 <= ink["checked"] <= ink["expected"], f"{job}: inconsistent paper ink counts")
    plot_paths = sorted((job / "过程" / "printed-ink").rglob("formal-layout-print-review.json"))
    plot_attempts = {}
    for path in plot_paths:
        require(path.parent.name not in plot_attempts, f"{job}: duplicate plotted attempt label")
        plotted, rows = read_plots(path, config, allow_failed=True, allowed_panels=set(final_pages))
        plot_attempts[path.parent.name] = (plotted, rows, path)
    published_plots = [item for item in plot_attempts.values()
                       if item[0]["outputSHA256"] == output_hash and
                       panel_pages(item[1].values()) == panel_pages(final_pages.values())]
    scoped = "finalPrintPasses" in ink
    require(published_plots and (scoped or "attempt-0" in plot_attempts),
            f"{job}: no recorded initial or full final print evidence")
    final_plot, final_proof_pages, final_path = plot_attempts["final"] if scoped else published_plots[0]
    require(final_plot["outputSHA256"] == output_hash and
            panel_pages(final_proof_pages.values()) == panel_pages(final_pages.values()),
            f"{job}: final full print does not match the published drawing")
    if "attempt-0" in plot_attempts:
        initial_plot, initial_pages, initial_path = plot_attempts["attempt-0"]
        initial_hash = initial_plot["outputSHA256"]
        initial_proof = read_plot_proof(initial_path.parent, initial_hash, initial_pages)
    else:
        require(scoped and len(plot_attempts) == 1, f"{job}: missing initial trial evidence")
        initial_hash = output_hash
        initial_proof = {"missing": set(), "collisions": set(), "targets": {}, "checked": 0}
    final_proof = read_plot_proof(final_path.parent, final_plot["outputSHA256"], final_proof_pages)
    trial_count = len(plot_paths) - int(scoped)
    if scoped:
        trial_sheets = sum(len(rows) for key, (_, rows, _) in plot_attempts.items() if key != "final")
        require(ink["finalPrintPasses"] == 1 and ink["finalSheetPrints"] == len(final_pages) and
                ink["trialSheetPrints"] == trial_sheets and
                read(final_path.parent / "paper-ink-input.json").get("proposeRepairs") is False,
                f"{job}: trial or final full-print accounting does not match the individual PDFs")
    if mode == "repair":
        initial_open = len(initial_proof["missing"]) + len(initial_proof["collisions"])
        expected_limit = 8 + 4 * min(initial_open // 12, 2)
        require(ink["initialUnresolved"] == initial_open and ink["printPassLimit"] == expected_limit
                and type(ink["printPasses"]) is int and 0 <= trial_count <= ink["printPasses"] <= expected_limit,
                f"{job}: recorded trial prints do not agree with the bounded native path")
    require(ink["expected"] == len(final_proof["targets"]) and ink["checked"] == final_proof["checked"] and
            ink["unresolved"] == len(final_proof["missing"]) + len(final_proof["collisions"]),
            f"{job}: final summary differs from raw plot analyzer results")
    postcheck_path = job / "plan-postcheck.json"
    postcheck = read(postcheck_path) if postcheck_path.is_file() else None
    if mode != "off":
        require(postcheck is not None and postcheck["contract"] == "plan.postcheck.v1"
                and postcheck["mode"] == mode and postcheck["readOnly"] is True
                and postcheck["sourceSHA256"] == source_hash and postcheck["drawingSHA256"] == output_hash
                and not postcheck["error"],
                f"{job}: missing or mismatched independent postcheck receipt")
        require(postcheck["prePrintCandidateSHA256"] == initial_hash,
                f"{job}: postcheck pre-print candidate hash differs from actual initial plot")
        require(all(type(postcheck[key]) is int and postcheck[key] >= 0
                    for key in ("generationMilliseconds", "auditMilliseconds", "annotationsRead")) and
                all(type(finding["panel"]) is int and isinstance(finding["source"], str) and finding["issueCode"]
                    for finding in postcheck["findings"]),
                f"{job}: malformed postcheck metrics or findings")

    return {
        "job": job, "mode": mode, "config": config, "inputs": inputs, "source": source_hash,
        "output": output_hash, "inventory": inventory, "sourceChecks": source_checks,
        "drawChecks": draw_checks, "ledger": ledger, "rules": rules, "review": review, "ink": ink,
        "formalPages": formal_plots["pages"], "receipt": receipt,
        "initialProof": initial_proof, "finalProof": final_proof,
        "metrics": {
            "outputSHA256": output_hash, "mode": mode, "gates": gates,
            "prePrintCandidateSHA256": initial_hash,
            "engineSHA256": ledger["engineSHA256"], "blockingLocations": len(review["blockingLocations"]),
            "sourceResults": dict(Counter(row["status"] for row in source_checks.values())),
            "drawingChecks": dict(Counter(row["status"] for row in draw_checks.values())),
            "plottedPasses": len(plot_paths),
            "actualPlottedPages": sum(len(rows) for _, rows, _ in plot_attempts.values()),
            "plotsPerPass": {key: len(rows) for key, (_, rows, _) in plot_attempts.items()},
            "actualFinalPDFs": formal_plots["pages"], "ink": ink,
            "postcheckFindings": None if postcheck is None else dict(
                Counter(row["issueCode"] for row in postcheck["findings"])),
            "timings": timings(job, host_path, host, attempt, postcheck, plot_paths),
        },
    }


def differences(before, after, location="$"):
    """Yield keyed differences without reordering report arrays or dropping handles."""
    if isinstance(before, dict) and isinstance(after, dict):
        for key in sorted(before.keys() | after.keys(), key=repr):
            branch = f"{location}.{key}"
            if key not in before or key not in after:
                yield {"at": branch, "baselinePresent": key in before, "comparisonPresent": key in after}
            else:
                yield from differences(before[key], after[key], branch)
    elif isinstance(before, list) and isinstance(after, list):
        if len(before) != len(after):
            yield {"at": location, "baselineCount": len(before), "comparisonCount": len(after)}
        for position, (old, new) in enumerate(zip(before, after)):
            yield from differences(old, new, f"{location}[{position}]")
    elif before != after:
        yield {"at": location, "baseline": before, "comparison": after}


def semantic_report(path):
    doc = read(path)
    # The actual candidate hashes are checked separately. Some specialist reports
    # intentionally bind the pre-print snapshot, whose container digest also varies.
    doc = {key: value for key, value in doc.items() if key not in {"outputSHA256", "sourceInventorySHA256"}}
    if path.name == "perimeter-source-reconciliation.json":
        doc["verifiedInteriorDimensions"] = sorted(doc["verifiedInteriorDimensions"])
    return doc


def canonical_furniture(before, after):
    """Allow reassigned nested output handles only under a bijective path map."""
    evidence_aliases = {}
    old_items = {row["object"]: row for row in before["items"]}
    for row in after["items"]:
        old = old_items.get(row["object"])
        if old is None or "memberCorrespondence" not in old or "memberCorrespondence" not in row:
            continue
        pairs_before, pairs_after = old["memberCorrespondence"], row["memberCorrespondence"]
        if len(pairs_before) != len(pairs_after):
            continue
        forward, reverse = {}, {}
        for first, second in zip(pairs_before, pairs_after):
            if first[0] != second[0]:
                break
            a, b = first[1].split("/"), second[1].split("/")
            if len(a) != len(b) or a[0] != b[0]:
                break
            for depth in range(1, len(a) + 1):
                x, y = tuple(a[:depth]), tuple(b[:depth])
                if forward.setdefault(x, y) != y or reverse.setdefault(y, x) != x:
                    break
            else:
                continue
            break
        else:
            first, second = old.get("firstNonconformingMember"), row.get("firstNonconformingMember")
            if first != second:
                scoped_aliases = {target[-1] for source, target in forward.items() if source[-1] == first}
                if scoped_aliases == {second}:
                    evidence_aliases[(row["object"], second)] = first
                    row["firstNonconformingMember"] = first
            row["memberCorrespondence"] = pairs_before
    return evidence_aliases


def audit_raster_digests(job):
    import pymupdf

    result = {}
    for page in job["formalPages"]:
        with pymupdf.open(job["job"] / page["file"]) as pdf:
            require(len(pdf) == 1, f"Unexpected multi-page native plot: {page['file']}")
            raster = pdf[0].get_pixmap(dpi=120, alpha=False)
            result[page["panel"]] = (raster.width, raster.height, hashlib.sha256(raster.samples).hexdigest())
    return result


def rule_evidence_keys(rules):
    keys = Counter()
    identity = ("panel", "sourcePanel", "scope", "object", "source", "segment", "side", "row", "end")
    for (rule,), row in rules.items():
        for proof in row["evidence"]:
            result = proof.get("result", "")
            if proof.get("savedReopened") and (proof.get("object") or proof.get("source")) and (
                    result.startswith("verified") or result == "omitted-with-evidence"):
                key = (rule, tuple((field, json.dumps(proof[field], ensure_ascii=False, sort_keys=True))
                                   for field in identity if field in proof))
                keys[key] += 1
    return keys


def blocker_keys(review):
    return {(location["panel"], location["source"], finding["gate"], finding["check"])
            for location in review["blockingLocations"] for finding in location["findings"]}


def compare(before, after, mode):
    regressions = []
    watched_reports = []
    evidence_aliases = {}

    def exact(name, old, new):
        changes = list(differences(old, new))
        if changes:
            regressions.append({"check": name, "differences": len(changes), "examples": changes[:15]})

    exact("source DWG", before["source"], after["source"])
    exact("native settings excluding postcheck mode and fingerprinted location changes", before["config"], after["config"])
    exact("fingerprinted settings inputs excluding verified native module upgrade", before["inputs"], after["inputs"])
    exact("source inventory scope and evidence", before["inventory"], after["inventory"])
    exact("rule catalog/active rules", [before["ledger"][key] for key in ("masterSHA256", "catalogSHA256", "ruleCount")],
          [after["ledger"][key] for key in ("masterSHA256", "catalogSHA256", "ruleCount")])
    exact("selected model panels", before["ledger"]["panels"], after["ledger"]["panels"])
    exact("effective postcheck mode", ["off", "audit" if mode == "audit-only" else "repair"], [before["mode"], after["mode"]])
    formal_geometry = ("panel", "layout", "status", "paper", "paperScale", "viewportScale")
    exact("formal PDF page scope and scales",
          [tuple(row[k] for k in formal_geometry) for row in before["formalPages"]],
          [tuple(row[k] for k in formal_geometry) for row in after["formalPages"]])
    for name in ("initialProof", "finalProof") if mode == "audit-only" else ("initialProof",):
        old, new = before[name], after[name]
        frame_fields = ("ratio", "modelCenter", "modelSize", "paperOffset")
        exact(f"{name} viewport frame geometry", {k: [row[field] for field in frame_fields] for k, row in old["frames"].items()},
              {k: [row[field] for field in frame_fields] for k, row in new["frames"].items()})
        exact(f"{name} plotted model and glyph targets", old["targets"], new["targets"])
        for key in ("missing", "collisions"):
            exact(f"{name} {key} segment identities", sorted(old[key]), sorted(new[key]))
    for name in SEMANTIC_REPORTS:
        if mode == "repair" and name not in REPAIR_PROTECTED_REPORTS:
            continue
        old, new = (job["job"] / name for job in (before, after))
        if old.is_file() or new.is_file() or name in REQUIRED_SEMANTIC_REPORTS:
            watched_reports.append(name)
            if old.is_file() and new.is_file():
                old_report, new_report = semantic_report(old), semantic_report(new)
                if name == "plan-furniture-readback.json":
                    evidence_aliases = canonical_furniture(old_report, new_report)
                exact(name, old_report, new_report)
            else:
                regressions.append({"check": name, "baselinePresent": old.is_file(), "comparisonPresent": new.is_file()})
    if mode == "audit-only":
        exact("full formal PDF raster on each accepted panel", audit_raster_digests(before), audit_raster_digests(after))
        exact("P04 finalized gates and issues", {k: v for k, v in before["review"].items() if k != "outputSHA256"},
              {k: v for k, v in after["review"].items() if k != "outputSHA256"})
        exact("native plot passes and actual PDF count",
              before["metrics"]["plotsPerPass"], after["metrics"]["plotsPerPass"])
        exact("source object reconciliation", before["sourceChecks"], after["sourceChecks"])
        exact("drawing object checks", before["drawChecks"], after["drawChecks"])
        # Proofs for a printed or reopened object are independently DWG-bound.
        # Their output digest need not be identical across independent saves.
        def normalized_rules(job, aliases):
            def normalize(proof):
                item = {k: v for k, v in proof.items() if k not in {"drawingSHA256", "outputSHA256"}}
                alias = aliases.get((item.get("object"), item.get("firstNonconformingMember")))
                if alias is not None:
                    item["firstNonconformingMember"] = alias
                return item
            return {key: {**row, "evidence": [normalize(proof) for proof in row["evidence"]]}
                    for key, row in job["rules"].items()}

        old_rules, new_rules = normalized_rules(before, {}), normalized_rules(after, evidence_aliases)
        exact("rule statuses and saved model evidence", old_rules, new_rules)
        exact("paper ink coverage and result", [before["ink"][key] for key in AUDIT_RESULT_KEYS],
              [after["ink"][key] for key in AUDIT_RESULT_KEYS])
        for key in ("finalRoots", "dimensionChains", "dimensionSegments", "openingCaptions"):
            exact(f"model receipt {key}", before["receipt"][key], after["receipt"][key])
    else:
        old_review, new_review = before["review"], after["review"]
        for gate in GATES:
            if old_review["gates"][gate] and not new_review["gates"][gate]:
                regressions.append({"check": "previously passing P04 gate", "gate": gate})
        new_blockers = blocker_keys(new_review) - blocker_keys(old_review)
        if new_blockers:
            regressions.append({"check": "new P04 blockers", "examples": sorted(new_blockers)[:20],
                                "count": len(new_blockers)})
        for name in ("sourceChecks", "drawChecks"):
            old, new = before[name], after[name]
            rank = ACCEPTANCE_RANK.get
            worsened = [{"key": key, "baseline": row["status"], "comparison": new.get(key, {}).get("status")}
                        for key, row in old.items()
                        if rank(row["status"], 0) > rank(new.get(key, {}).get("status"), 0)]
            if worsened:
                regressions.append({"check": name, "worsened": len(worsened), "examples": worsened[:20]})
        for key, row in before["sourceChecks"].items():
            if row["status"] == "verified" and after["sourceChecks"].get(key, {}).get("status") == "verified":
                exact(f"source-to-output association {key}", row["objects"], after["sourceChecks"][key]["objects"])
        lost_proofs = rule_evidence_keys(before["rules"]) - rule_evidence_keys(after["rules"])
        if lost_proofs:
            regressions.append({"check": "previously accepted rule object evidence",
                                "lost": lost_proofs.total(), "examples": list(lost_proofs.items())[:20]})
        if (after["ink"]["checked"] < before["ink"]["checked"]
                or after["ink"]["expected"] < before["ink"]["expected"]
                or after["ink"]["unresolved"] > before["ink"]["unresolved"]):
            regressions.append({"check": "paper ink coverage or clearance worsened",
                                "baseline": {k: before["ink"][k] for k in AUDIT_RESULT_KEYS},
                                "comparison": {k: after["ink"][k] for k in AUDIT_RESULT_KEYS}})
        old, new = before["finalProof"], after["finalProof"]
        previously_clear = old["targets"].keys() - old["missing"] - old["collisions"]
        newly_open = previously_clear & (new["missing"] | new["collisions"])
        lost = previously_clear - new["targets"].keys()
        if newly_open or lost:
            regressions.append({"check": "previously clear final printed segments",
                                "newIssues": sorted(newly_open), "missingTargets": sorted(lost)})
        immutable_fields = ("handle", "segment", "layer", "vertical", "height", "digits", "number", "start", "end")
        for key in sorted(previously_clear & new["targets"].keys()):
            exact(f"preserved measurements/style for printed segment {key}",
                  [old["targets"][key][field] for field in immutable_fields],
                  [new["targets"][key][field] for field in immutable_fields])

    old_metrics, new_metrics = before["metrics"], after["metrics"]
    new_page_hashes = {page["panel"]: page["sha256"] for page in after["formalPages"]}
    return {
        "passed": not regressions, "mode": mode, "sourceSHA256": before["source"],
        "dwgByteIdentity": before["output"] == after["output"],
        "prePrintDWGByteIdentity": old_metrics["prePrintCandidateSHA256"] == new_metrics["prePrintCandidateSHA256"],
        "finalPDFByteIdentityByPanel": {
            row["panel"]: row["sha256"] == new_page_hashes.get(row["panel"])
            for row in before["formalPages"]
        },
        "semanticReportsCompared": watched_reports,
        "baseline": {"job": str(before["job"]), **old_metrics},
        "comparison": {"job": str(after["job"]), **new_metrics},
        "actualPlottedPageDelta": new_metrics["actualPlottedPages"] - old_metrics["actualPlottedPages"],
        "regressions": regressions,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline_job", type=Path, help="Completed off-mode job directory")
    parser.add_argument("comparison_job", type=Path, help="Completed audit or repair job directory")
    parser.add_argument("--mode", required=True, choices=("audit-only", "repair"))
    options = parser.parse_args()
    jobs = []
    for role, path in (("baseline", options.baseline_job), ("comparison", options.comparison_job)):
        try:
            jobs.append(verify_job(path.resolve(strict=True)))
        except (VerificationError, OSError, ValueError, KeyError, TypeError) as error:
            print(json.dumps({"passed": False, "job": str(path), "role": role, "error": str(error)},
                             ensure_ascii=False, indent=2))
            return 1
    try:
        result = compare(*jobs, options.mode)
    except (VerificationError, OSError, ValueError, KeyError, TypeError) as error:
        result = {"passed": False, "mode": options.mode, "error": str(error)}
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
