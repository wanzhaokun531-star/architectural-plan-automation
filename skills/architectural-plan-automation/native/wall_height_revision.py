"""Automatically reconcile native wall heights; optionally finalize and deliver a DWG."""
import argparse
import json
from pathlib import Path
from owned_plan_host import file_hash
from unattended_plan import workspace_root, formal_output
from wall_height_pipeline import reconcile_wall_heights, rebind


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('source', 'work'):
        parser.add_argument('--' + key, type=Path, required=True)
    parser.add_argument('--floor-levels', type=Path)
    parser.add_argument('--workspace-root', type=Path)
    parser.add_argument('--module', type=Path)
    parser.add_argument('--inspect', action='store_true', help='independent read only; no wall writes')
    parser.add_argument('--sheets', type=Path, help='source-bound sheets for final printing and delivery')
    parser.add_argument('--output', type=Path, help='new formal DWG path; requires --sheets')
    args = parser.parse_args()
    if args.output and (args.inspect or not args.sheets):
        parser.error('--output requires --sheets and automatic revision mode')
    source, work = args.source.resolve(strict=True), args.work.resolve()
    skill = Path(__file__).resolve().parents[1]
    workspace = workspace_root(args.workspace_root, Path.cwd(), skill)
    module = (args.module or skill / 'tools/PlanNative.arx').resolve(strict=True)
    import hashlib
    identity = {'source': file_hash(source), 'module': file_hash(module),
                'levels': file_hash(args.floor_levels) if args.floor_levels else None,
                'entry': file_hash(Path(__file__).with_name('wall_height_pipeline.py'))}
    version = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()[:24]
    work = work / ('revision-' + version)
    work.mkdir(parents=True, exist_ok=True)
    result = reconcile_wall_heights(source, work / 'walls', workspace, skill, module,
                                    floor_levels=args.floor_levels, repair=not args.inspect)
    if args.output:
        from template_revision import execute_template_revision, publish_template_result
        drawing = Path(result['drawing'])
        sheets = args.sheets.resolve(strict=True)
        if result['sha256'] != file_hash(source):
            sheets = rebind(sheets, file_hash(source), drawing, work / 'corrected-sheets.tsv')
        result = execute_template_revision(drawing, sheets, work / 'finalize', workspace, skill,
                                           module=module, wall_expected=result['expected'])
        publish_template_result(result, formal_output(args.output).resolve())
    print(json.dumps(result, ensure_ascii=False))


if __name__ == '__main__':
    main()
