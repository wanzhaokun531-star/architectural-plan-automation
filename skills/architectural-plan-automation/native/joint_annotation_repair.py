"""Run and verify a source-bound local annotation repair on a delivered plan."""

import argparse
import csv
import json
from pathlib import Path
import time
import sys

from owned_plan_host import RetryableHostError, file_hash, require, run_owned_plan, write_json
from unattended_plan import workspace_root

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
from check_plan_print_ink import Ink, inflate, main as analyse_ink, pdf, projected


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def read_results(path, digest=None):
    rows = path.read_text(encoding="utf-8-sig").splitlines()
    require(rows and rows[0].startswith("PLANINKRESULT1 ") and rows[-1] == "END", "实际尺寸打印记录不完整")
    if digest is not None:
        require(rows[0].split()[1] == digest, "尺寸打印核验结果与 DWG 指纹不同")
    return {
        (int(panel), handle, int(segment), tag)
        for row in rows[1:-1]
        if (parts := row.split()) and parts[0] in {"COLLISION", "MISSING"}
        for tag, panel, handle, segment in [parts]
    }


def prior_evidence(report, source):
    result = read(report / "product-result.json")
    digest = file_hash(source)
    joint = result.get("dwgReopened") is True and (report / "joint-delivery-verification.json").is_file()
    require(result.get("completed") and (result.get("finalDocumentOpened") or joint) and result.get("outputSHA256") == digest,
            "原正式版与独立重开回执指纹不一致")
    layout = read(report / "formal-layout-print-review.json")
    require(layout["outputSHA256"] == digest, "原正式版打印记录指纹不一致")
    root = report / "过程" / "printed-ink"
    final = root / "final" / "paper-ink-input.json"
    candidates = [report / "joint-paper-input.json"] if joint else (
        [final] if final.is_file() else root.glob("*/paper-ink-input.json"))
    request = next((p for p in candidates if read(p)["drawingSHA256"] == digest and
                    [page["panel"] for page in read(p)["frames"]] ==
                    [page["panel"] for page in layout["pages"]]), None)
    require(request is not None, "缺少原正式版尺寸实打定位数据")
    frames = read(request)
    for page in frames["frames"]:
        require(file_hash(report / page["file"]) == page["sha256"], "原正式图幅 PDF 不一致")
    issues = read_results(request.parent / ("joint-paper-result.tsv" if joint else "paper-ink-result.tsv"), digest)
    if joint:
        delivery = read(report / "joint-delivery-verification.json")
        require(delivery["outputSHA256"] == digest and {tuple(item) for item in delivery["remaining"]} == issues,
                "局部交付回执与同版实打未闭合项不一致")
        # Older local runs only recorded the sidecar digest. That sidecar can
        # be identified by its name and checked against the drawing rows below.
        result = {**result, "blockingChecklist": str(source.with_name(source.stem + "_遗留清单.csv")),
                  "checklistSHA256": delivery["checklistSHA256"], "paperIssues": sorted(issues)}
    return result, frames, issues


def note_paper_bounds(note, frame, page):
    a, b = (projected(p,frame,page) for p in note['bounds'])
    return pdf.Rect(a[0],b[1],b[0],a[1])


def note_paper_vectors(page, note, expected):
    paths = []
    vicinity = inflate(expected,1.1)
    for path in page.get_drawings():
        if path['layer'] != note['layer'] or not vicinity.contains(path['rect']):
            continue
        commands = []
        for operation, *points in path['items']:
            if operation == 're':
                points = [points[0].tl,points[0].br]
            elif operation == 'qu':
                points = [points[0].ul,points[0].ur,points[0].lr,points[0].ll]
            if operation not in {'l','c','re','qu'}:
                break
            commands.append((operation,tuple((p.x-expected.x0,p.y-expected.y0) for p in points)))
        else:
            if commands:
                paths.append((path['seqno'],path['rect'],commands))
    return paths


def same_note_vectors(before, after):
    if len(before) != len(after) or not before:
        return False
    unmatched = [item[2] for item in after]
    for _, _, source in before:
        positions = []
        for index, candidate in enumerate(unmatched):
            if len(source) != len(candidate):
                continue
            if all(a[0]==b[0] and len(a[1])==len(b[1]) and
                   all(abs(x-y)<=.38 for u,v in zip(a[1],b[1]) for x,y in zip(u,v))
                   for a,b in zip(source,candidate)):
                positions.append(index)
        if len(positions) != 1:
            return False
        unmatched.pop(positions[0])
    return True


def verify_candidate(report, old_frames, old_issues, *, allow_unchanged=False, prior_report=None):
    candidate = report / "candidate.dwg"
    before = read(report / "joint-before.json")
    after = read(report / "joint-after.json")
    receipt = read(report / "product-result.json")
    digest = file_hash(candidate)
    require(receipt["completed"] and receipt["dwgReopened"] and receipt["outputSHA256"] == after["drawingSHA256"] == digest,
            "局部返修候选缺少独立重开证据")
    require(before["drawingSHA256"] == old_frames["drawingSHA256"], "返修初始图和原有打印记录不一致")
    printed = read(report / "formal-layout-print-review.json")
    require(printed["outputSHA256"] == digest and len(printed["pages"]) == len(old_frames["frames"]),
            "局部返修图与实际全幅打印不一致")
    edited = {row["handle"]: row for row in after["objects"] if "grips" in row}
    named = {row["handle"]: row for row in after["objects"] if "content" in row and "type" not in row}
    before_objects = {row["handle"]: row for row in before["objects"]}
    request = {"contract": "plan.printed-ink-input.v1", "drawingSHA256": digest,
               "proposeRepairs": False, "extendedInteriorSearch": old_frames.get("extendedInteriorSearch", False),
               "frames": []}
    accepted_notes = []
    for page, old in zip(printed["pages"], old_frames["frames"]):
        require(page["status"] == "plotted" and page["panel"] == old["panel"] and
                page["paperScale"] == 1 and page["viewportScale"] == old["ratio"] and
                file_hash(report / page["file"]) == page["sha256"], "重开全幅正式打印、比例或 SHA-256 不一致")
        changed_sheet = any(row["handle"] in edited for row in old["targets"])
        frame = {**old, "sha256": page["sha256"], "file": page["file"],
                 "extendedInteriorSearch": changed_sheet or old.get("extendedInteriorSearch", False), "targets": []}
        for original in old["targets"]:
            row = dict(original)
            change = edited.get(row["handle"])
            if change:
                n, i = len(change["values"]), row["segment"]
                grips = change["grips"]
                row["baseline"] = grips[0][:2]
                row["anchor"] = grips[n+3+i][:2]
            frame["targets"].append(row)
        request["frames"].append(frame)
        for handle, note in named.items():
            if note["panel"] != old["panel"] or note["anchor"] == before_objects[handle]["anchor"]:
                continue
            with pdf.open(report / page["file"]) as rendered:
                sheet = rendered[0]
                expected = note_paper_bounds(note,frame,sheet)
                matched = [box for box in sheet.search_for(note["content"]) if inflate(expected, 2).contains(box)]
                excluded = set()
                if not matched and prior_report is not None:
                    # AutoCAD can plot a native SHX note as geometry. The text
                    # and style have already been checked in the reopened DWG.
                    former_note = before_objects[handle]
                    with pdf.open(prior_report/old['file']) as before_sheet:
                        old_bounds = note_paper_bounds(former_note,old,before_sheet[0])
                        vectors = note_paper_vectors(before_sheet[0],former_note,old_bounds)
                    current = note_paper_vectors(sheet,note,expected)
                    if same_note_vectors(vectors,current):
                        excluded = {row[0] for row in current}
                        total = pdf.Rect(current[0][1])
                        for _, box, _ in current[1:]:
                            total |= box
                        matched = [total]
                require(len(matched) == 1, f"原文字 {handle} 未在移动后的纸面位置唯一读回")
                artwork = Ink(inflate(expected, 4))
                for item in sheet.get_drawings():
                    if item['seqno'] not in excluded:
                        artwork.add_path(item)
                require(not artwork.has_ink(inflate(matched[0], .12)), f"原文字 {handle} 新位置仍与实打线迹相交")
                accepted_notes.append(handle)
    request_path = report / "joint-paper-input.json"
    write_json(request_path, request)
    reply = report / "joint-paper-result.tsv"
    analyse_ink(request_path, reply)
    remaining = read_results(reply)
    require(remaining <= old_issues, "局部方案造成原已通过尺寸新增打印冲突")
    cleared = old_issues - remaining
    require(cleared or accepted_notes or (allow_unchanged and not edited and not named),
            "局部方案未关闭原有尺寸或文字冲突")
    return digest, cleared, remaining, accepted_notes


def write_local_request(path, source, printed, plans, survey=()):
    with path.open('x', encoding='utf-8', newline='\n') as stream:
        stream.write(f'PLANJOINT1 {file_hash(source)}\n')
        for page in printed['pages']:
            stream.write(f'SHEET {page["panel"]} {json.dumps(page["layout"], ensure_ascii=False)}\n')
            if page.get('viewport'):
                stream.write(f'VIEWPORT {page["panel"]} {page["viewport"]}\n')
        for panel, handle in survey:
            stream.write(f'CHAIN {panel} {handle} 0\n')
        for panel, plan in plans:
            handle = plan['handle']
            stream.write(f'CHAIN {panel} {handle} {plan["normal"]}\n')
            for segment, (x,y) in enumerate(plan['text']):
                stream.write(f'TEXT {panel} {handle} {segment} {x:.15f} {y:.15f}\n')
        stream.write('END\n')


def local_paper_candidates(report, frames, issues, geometry):
    """Choose a bounded raster search separately for each originally blocked chain."""
    from joint_annotation_slots import paper_slot_plans

    result = []
    for frame in frames['frames']:
        panel = frame['panel']
        collisions = {(handle, part) for sheet,handle,part,kind in issues if sheet == panel and kind == 'COLLISION'}
        incomplete = {handle for sheet,handle,_,kind in issues if sheet == panel and kind == 'MISSING'}
        if not collisions:
            continue
        with pdf.open(report/frame['file']) as page:
            for obj in geometry['objects']:
                if obj['panel'] != panel or obj['handle'] in incomplete or not any(h == obj['handle'] for h,_ in collisions):
                    continue
                targets = [row for row in frame['targets'] if row['handle'] == obj['handle']]
                if not targets or not all(row['movable'] for row in targets) or not obj['ink']:
                    continue
                plans = paper_slot_plans(page[0], frame, obj, collisions, geometry['openingGlyphs'],
                                         deadline=time.monotonic()+24)
                if not plans and all(not row.get('interior') for row in targets):
                    # A verified exterior short chain can stagger beyond an
                    # end station. Bound that extra search by its actual text
                    # height; interior association and all paper checks stay
                    # unchanged. The native chain still owns each real value.
                    plans = paper_slot_plans(page[0], frame, obj, collisions, geometry['openingGlyphs'],
                        deadline=time.monotonic()+12,exterior_overhang=1.5*max(row['height'] for row in targets))
                if plans:
                    result.append((panel, plans))
    return result


def publish(source_report, old_receipt, report, output, result):
    digest, cleared, remaining, notes = result
    old_checklist = Path(old_receipt["blockingChecklist"])
    closed = {f"{handle}/{part+1}" for _, handle, part, _ in cleared} | set(notes)
    if not remaining:
        closed.add("pdf")
    with old_checklist.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        fields = reader.fieldnames
        prior_rows = list(reader)
    listed_ink = {row['源对象'] for row in prior_rows if row['实际部位'] == 'printed-dimension-ink'
                  or '实打短尺寸' in row['问题']}
    prior_ink = {f'{handle}/{part+1}' for _,handle,part,kind in old_receipt.get('paperIssues',())
                 if kind == 'COLLISION'}
    require(file_hash(old_checklist) == old_receipt["checklistSHA256"] or
            (old_receipt.get("dwgReopened") is True and prior_rows and listed_ink == prior_ink and
             all(row["DWG SHA-256"] == old_receipt["outputSHA256"] for row in prior_rows)),
            "原正式版遗留清单与 DWG 不一致")
    rows = [row for row in prior_rows if row["源对象"] not in closed]
    output.parent.mkdir(parents=True, exist_ok=True)
    checklist = output.with_name(output.stem+"_遗留清单.csv")
    require(not output.exists() and not checklist.exists(), "局部正式交付路径须为新文件")
    if rows:
        with checklist.open("x", encoding="utf-8-sig", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=fields)
            writer.writeheader()
            writer.writerows({**row, "DWG SHA-256": digest} for row in rows)
    with output.open("xb") as stream:
        stream.write((report / "candidate.dwg").read_bytes())
    require(file_hash(output) == digest, "局部正式版发布指纹变化")
    write_json(report / "joint-delivery-verification.json", {
        "priorJob": str(source_report), "sourceSHA256": old_receipt["outputSHA256"], "outputSHA256": digest,
        "cleared": sorted(cleared), "remaining": sorted(remaining), "notes": notes, "residuals": len(rows),
        "checklistSHA256": file_hash(checklist) if rows else "",
    })


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for field in ("source", "prior-report", "output", "work"):
        parser.add_argument("--"+field, type=Path, required=True)
    parser.add_argument('--request', type=Path)
    parser.add_argument('--workspace-root', type=Path)
    args = parser.parse_args()
    skill = Path(__file__).resolve().parents[1]
    workspace = workspace_root(args.workspace_root, Path.cwd(), skill)
    source, previous, work = args.source.resolve(), args.prior_report.resolve(), args.work.resolve()
    old_receipt, frames, issues = prior_evidence(previous, source)
    work.mkdir(parents=True, exist_ok=True)
    module = skill/'tools/PlanNative.arx'
    config = {'mode':'joint-annotations', 'resources':str(skill/'assets')}
    request = args.request.resolve() if args.request else None
    if request is None:
        printed = read(previous/'formal-layout-print-review.json')
        allowed = {(frame['panel'],target['handle']) for frame in frames['frames'] for target in frame['targets']
                   if target.get('movable')}
        survey = sorted({(panel,handle) for panel,handle,_,kind in issues
                         if kind == 'COLLISION' and (panel,handle) in allowed})[:32]
        candidates = []
        if survey:
            request = work/'paper-slot-survey.tsv'
            write_local_request(request,source,printed,(),survey)
            run_owned_plan(workspace,module,source,work/'survey',
                           {**config,'jointRequest':str(request),'jointInspect':'1'})
            geometry = read(work/'survey/job/joint-before.json')
            require(geometry['drawingSHA256'] == frames['drawingSHA256'], '实读候选几何属于其他 DWG')
            candidates = local_paper_candidates(previous,frames,issues,geometry)
        write_json(work/'paper-slot-candidates.json', {'drawingSHA256':frames['drawingSHA256'], 'groups':candidates})
        trials = [[(panel, plans[0]) for panel,plans in candidates]] if candidates else []
        trials.extend([(panel,plan)] for panel,plans in candidates for plan in plans[:2])
        unique = {json.dumps(plan,sort_keys=True):plan for plan in trials}
        failures = []
        for number, plan in enumerate(list(unique.values())[:4] + [[]], 1):
            request = work/f'paper-slot-candidate-{number:02}.tsv'
            write_local_request(request,source,printed,plan)
            attempt = work/f'attempt-{number:02}'
            try:
                run_owned_plan(workspace,module,source,attempt,{**config,'jointRequest':str(request)})
                result = verify_candidate(attempt/'job',frames,issues,allow_unchanged=not plan,prior_report=previous)
            except RuntimeError as exc:
                if not plan:
                    raise
                failures.append({'attempt':number, 'error':str(exc)})
                continue
            write_json(work/'paper-slot-attempts.json', failures)
            publish(previous,old_receipt,attempt/'job',args.output.resolve(),result)
            print(args.output.resolve())
            return
    for number in range(1, 4):
        attempt = work / f"attempt-{number:02}"
        try:
            run_owned_plan(workspace,module,source,attempt,{**config,'jointRequest':str(request)})
        except RetryableHostError:
            if number == 3:
                raise
            continue
        break
    report = attempt / "job"
    result = verify_candidate(report, frames, issues, prior_report=previous)
    publish(previous, old_receipt, report, args.output.resolve(), result)
    print(args.output.resolve())


if __name__ == "__main__":
    main()
