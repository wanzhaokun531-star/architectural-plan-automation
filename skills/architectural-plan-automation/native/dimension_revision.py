"""Replace dimensions in an existing independently confirmed plan and print the result."""

import argparse
from pathlib import Path

from joint_annotation_repair import read
from owned_plan_host import RetryableHostError, file_hash, require, run_owned_plan
from typography_revision import verify_dimension_style_table
from unattended_plan import workspace_root


def verify_dimension_revision(report):
    drawing = report / "candidate.dwg"
    digest = file_hash(drawing)
    native = read(report / "required-dimension-readback.json")
    paper = read(report / "formal-layout-print-review.json")
    receipt = read(report / "product-result.json")
    require(receipt["dwgReopened"] and native["savedReopened"] and
            all(row["outputSHA256"] == digest for row in (receipt, native, paper)),
            "尺寸成果缺少同版独立重开及实打证据")
    require(paper["pages"] and all(page["status"] == "plotted" and
            file_hash(report / page["file"]) == page["sha256"] for page in paper["pages"]),
            "正式图幅实打或文件指纹不完整")
    verify_dimension_style_table(native)
    for row in native["dimensions"]:
        if row["issue"]:
            continue
        require(row["source"] != row["target"] and not row["sourceVisible"] and
                row["parts"] and all(part["textStyle"] == "_TCH_DIM" and
                part.get("textStyleDefinitionVerified") is True and
                abs(part["effectiveHeight"] - 280) <= 1e-6 for part in row["parts"]),
                "重开尺寸仍采用旧对象或实际文字样式不合规")
    return drawing, native


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--sheets", required=True, type=Path)
    parser.add_argument("--work", required=True, type=Path)
    args = parser.parse_args()
    skill = Path(__file__).resolve().parents[1]
    root = workspace_root(None, Path.cwd(), skill)
    source, sheets = args.source.resolve(strict=True), args.sheets.resolve(strict=True)
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    for number in range(1, 4):
        attempt = work / f"attempt-{number:02}"
        if attempt.exists():
            continue
        try:
            run_owned_plan(root, skill / "tools/PlanNative.arx", source, attempt, {
                "mode": "dimension-rebuild", "resources": str(skill / "assets"),
                "dimensionSheets": str(sheets),
            })
        except RetryableHostError:
            continue
        report = attempt / "job"
        drawing, native = verify_dimension_revision(report)
        from wall_height_pipeline import rebind
        from template_revision import execute_template_revision
        final_sheets = rebind(sheets, file_hash(source), drawing, work / 'final-sheets.tsv')
        final = execute_template_revision(drawing, final_sheets, work / 'finalize', root, skill)
        print(final['report'])
        open_items = sum(bool(row['issue']) for row in native['dimensions']) + native['dimensionStyleTable']['unresolved']
        print(f"dimensions={len(native['dimensions'])} open={open_items}")
        return
    raise RuntimeError("尺寸修订的隔离宿主已用尽自动重试")


if __name__ == "__main__":
    main()
