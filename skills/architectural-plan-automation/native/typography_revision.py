"""Restore and independently audit final plan typography in an owned CAD host."""
import argparse
from pathlib import Path
import re
import sys

from owned_plan_host import RetryableHostError, file_hash, read_json, require, run_owned_plan, write_json


def verify_dimension_style_table(audit):
    table = audit.get('dimensionStyleTable')
    require(isinstance(table, dict) and isinstance(table.get('styles'), list),
            '缺少最终独立重开的尺寸样式表核验')
    styles = table['styles']
    names = set()
    failures = []
    for row in styles:
        require(isinstance(row, dict), '尺寸样式表核验记录格式不正确')
        name, text_style, definition, verified, reason = (row.get(key) for key in
            ('name', 'textStyle', 'definitionVerified', 'verified', 'reason'))
        require(isinstance(name, str) and re.fullmatch(r'_tch_arch(?:&&[0-9]+)?', name.casefold()) and
                name.casefold() not in names and isinstance(text_style, str) and isinstance(reason, str) and
                type(definition) is bool and type(verified) is bool, '尺寸样式表记录不完整、样式不适用或名称重复')
        names.add(name.casefold())
        if verified:
            require(text_style == '_TCH_DIM' and definition and not reason,
                    '尺寸样式表通过标志与实际 _TCH_DIM 引用或定义不一致')
        else:
            require(reason, '未解决尺寸样式缺少原因')
            failures.append(row)
    checked, unresolved = table.get('checked'), table.get('unresolved')
    require('_tch_arch' in names and type(checked) is int and checked == len(styles) and
            type(unresolved) is int and unresolved == len(failures) and
            table.get('allApplicableVerified') is (not failures), '尺寸样式表缺少基础 _TCH_ARCH 或复核汇总不一致')
    return failures


def verify_typography(report, digest, *, formal=False):
    audit = read_json(report / 'final-typography-readback.json')
    require(audit.get('outputSHA256') == digest and audit.get('savedReopened') is True and
            audit.get('independentInventory') is True, '最终字体复核未绑定独立重开的同版 DWG')
    style_failures = verify_dimension_style_table(audit)
    rows = audit.get('objects', [])
    checked = [row for row in rows if 'verified' in row]
    failed = [row for row in checked if row['verified'] is not True]
    require(checked and audit.get('checked') == len(checked) and audit.get('unresolved') == len(failed) and
            audit.get('allApplicableVerified') is (not failed), '逐对象字体复核覆盖或汇总不一致')
    for row in checked:
        if not row['verified']:
            require(isinstance(row.get('object'), str) and isinstance(row.get('reason'), str) and row['reason'],
                    '未解决字体项缺少对象或原因')
            continue
        require(row.get('glyphs') and all(g.get('definitionVerified') is True and
                abs(g['height'] - row['expectedHeight']) < .01 for g in row['glyphs']),
                '字体通过标志与实际定义、字高不一致')
        if row.get('role') == 'dimension':
            require(all(g.get('style') == '_TCH_DIM' for g in row['glyphs']),
                    '可见尺寸实际引用的文字样式并非 _TCH_DIM')
    style_rows = [row for row in checked if row.get('role') == 'dimension-style-table']
    require(len(style_rows) == len(style_failures) and all(row['verified'] is False for row in style_rows) and
            {row['object']: row['reason'] for row in style_rows} ==
            {row['name']: row['reason'] for row in style_failures}, '未解决尺寸样式与全图逐项验收记录不一致')
    require(not formal or not failed, '仍有字体未解决项，不得声称全项通过')
    return failed


def verify_revision(report, source):
    drawing = report / 'candidate.dwg'
    digest = file_hash(drawing)
    receipt = read_json(report / 'product-result.json')
    require(receipt.get('completed') and receipt.get('dwgReopened') and receipt.get('nonAnnotationGeometryPreserved') and
            receipt.get('sourceSHA256') == file_hash(source) and receipt.get('outputSHA256') == digest,
            '字体修订缺少同版重开及原几何保护证据')
    failures = verify_typography(report, digest)
    unresolved = receipt.get('unresolved')
    require(isinstance(unresolved, list) and all(isinstance(row, dict) and
            all(isinstance(row.get(key), str) for key in ('object', 'role', 'reason')) for row in unresolved),
            '字体修订回执缺少未解决项清单')
    listed = {(row['object'], row['role'], row['reason']) for row in unresolved}
    require(all((row['object'], row['role'], row['reason']) in listed for row in failures),
            '字体修订回执缺少同版字体未解决项')
    host = read_json(report.parent / 'host.json')
    closure = read_json(Path(host['session']) / 'isolation-final.json')
    require(closure.get('nativeQuitConfirmed') is True and closure.get('workerExitCode') == 0 and
            closure.get('forcedTermination') is False and closure.get('userCadTouched') is False,
            '隔离宿主缺少正常退出及用户 CAD 保护证据')
    plots = read_json(report / 'formal-layout-print-review.json')
    require(plots['outputSHA256'] == digest and plots['pages'], '缺少同版整套实打')
    for page in plots['pages']:
        require(page['status'] == 'plotted' and page['paperScale'] == 1 and page['viewportScale'] == .01 and
                file_hash(report / page['file']) == page['sha256'], '实际打印图幅或比例不一致')
    return drawing, receipt


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'sheets', 'work'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--workspace-root', type=Path)
    parser.add_argument('--font-directory', type=Path)
    args = parser.parse_args(argv)
    from unattended_plan import workspace_root
    skill = Path(__file__).resolve().parents[1]
    root = workspace_root(args.workspace_root, Path.cwd(), skill)
    source, sheets = args.source.resolve(strict=True), args.sheets.resolve(strict=True)
    work = args.work.resolve(); work.mkdir(parents=True, exist_ok=True)
    configuration = {'mode': 'typography-finalize', 'resources': str(skill / 'assets'), 'typographySheets': str(sheets)}
    if args.font_directory:
        configuration['roomFontDirectory'] = str(args.font_directory.resolve(strict=True))
    binding = {'sourceSHA256': file_hash(source), 'sheetsSHA256': file_hash(sheets),
               'moduleSHA256': file_hash(skill / 'tools/PlanNative.arx'),
               'referenceSHA256': file_hash(skill / 'assets/reference.dwg'), 'configuration': configuration}
    checkpoint = work / 'typography-job.json'
    if checkpoint.exists() and read_json(checkpoint) != binding:
        work = work / ('revision-' + ''.join(binding[key][:8] for key in
                       ('moduleSHA256', 'sourceSHA256', 'sheetsSHA256')))
        work.mkdir(parents=True, exist_ok=True)
        checkpoint = work / 'typography-job.json'
    if checkpoint.exists():
        require(read_json(checkpoint) == binding, '版本过程目录的续跑记录与当前输入不一致')
    else:
        write_json(checkpoint, binding)
    for number in range(1, 4):
        attempt = work / f'attempt-{number:02}'
        if not attempt.exists():
            try:
                run_owned_plan(root, skill / 'tools/PlanNative.arx', source, attempt, configuration)
            except RetryableHostError:
                continue
        report = attempt / 'job'
        result = report / 'product-result.json'
        if not result.is_file() or not read_json(result).get('completed'):
            continue
        require(read_json(attempt / 'host.json')['moduleSHA256'] == binding['moduleSHA256'],
                '旧模块结果不能用作当前规则的续跑成果')
        drawing, receipt = verify_revision(report, source)
        sys.path.insert(0, str(skill / 'tools'))
        from check_plan_print_ink import main as check_ink
        request, reply = report / 'typography-paper-input.json', report / 'typography-paper-result.tsv'
        if not reply.exists():
            check_ink(request, reply)
        from joint_annotation_repair import read_results
        remaining = sorted(read_results(reply, file_hash(drawing)))
        write_json(report / 'typography-delivery-verification.json', {
            'outputSHA256': file_hash(drawing), 'dwgReopened': True, 'wholeDrawingTypography': True,
            'unresolved': receipt['unresolved'], 'paperIssues': remaining,
            'allApplicableVerified': not receipt['unresolved'] and not remaining})
        from wall_height_pipeline import rebind
        from template_revision import execute_template_revision
        final_sheets = rebind(sheets, file_hash(source), drawing, work / 'final-sheets.tsv')
        finalized = execute_template_revision(drawing, final_sheets, work / 'finalize', root, skill)
        print(finalized['report'])
        return
    raise RuntimeError('字体修订未完成；保留各次诊断，不发布未验证文件')


if __name__ == '__main__':
    main()
