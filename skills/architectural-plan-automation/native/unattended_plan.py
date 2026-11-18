"""Checkpoint one PLAN_DRAW job and deliver only a hash-bound formal DWG."""

import argparse
import contextlib
import csv
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import shlex
import sys
import tempfile

from owned_plan_host import (JobError, RetryableHostError, file_hash, packaged_host_inputs, read_json, require,
                             resume_owned_plan, run_owned_plan, write_json)


@contextlib.contextmanager
def job_lock(path):
    with path.open('a+b') as stream:
        stream.seek(0)
        if os.name == 'nt':
            import msvcrt
            try:
                msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
            except OSError as error:
                raise JobError(f'作业仍在运行：{path.parent}') from error
        else:
            import fcntl
            try:
                fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            except OSError as error:
                raise JobError(f'作业仍在运行：{path.parent}') from error
        yield


def verified_candidate(attempt, source_hash, *, require_complete_plot=False):
    report = attempt / 'job'
    receipt_file = report / 'product-result.json'
    if not receipt_file.is_file():
        return None
    try:
        receipt = read_json(receipt_file)
    except json.JSONDecodeError:
        return None
    if not receipt.get('completed'):
        return None
    drawing = Path(receipt['output']).resolve(strict=True)
    require(drawing.parent == report and Path(receipt['records']).resolve() == report and drawing.suffix.lower() == '.dwg',
            '生成回执指向了作业目录以外的文件')
    require(receipt.get('contract') == 'bcp.product-result.v1' and
            all(receipt.get(key) is True for key in ('published', 'finalDocumentOpened', 'sourceUnchanged')),
            '生成回执缺少写盘、重开或原图保护证明')
    output_hash = file_hash(drawing)
    review = read_json(report / 'plan-delivery-review.json')
    ledger = read_json(report / 'plan-rule-ledger.json')
    for evidence in (receipt, review, ledger):
        require(evidence.get('sourceSHA256') == source_hash and evidence.get('outputSHA256') == output_hash,
                '生成回执、验收清单与实际 DWG 的指纹不一致')
    require(review.get('formalRequested') is True and review.get('acceptanceMode') == 'architectural-plan',
            '当前原生模块未执行建筑平面出图验收口径')
    formal = review.get('formalReady') is True
    from typography_revision import verify_typography
    typography_failures = verify_typography(report, output_hash, formal=formal)
    listed_typography = {item['source'] for item in review.get('blockingLocations', []) if item.get('findings')}
    require(all(item['object'] in listed_typography for item in typography_failures),
            '最终字体未解决项没有进入同版交付清单')
    require(ledger.get('formalReady') is formal and receipt.get('constructionDrawingComplete') is formal and
            receipt.get('formalDelivery') is True and review.get('formalDelivery') is True and
            review.get('dwgReopened') is True and
            receipt.get('deliveryStatus') == ('formal' if formal else 'formal-with-exceptions'), '正式交付及逐项验收状态不一致')
    result = {'formal': formal, 'drawing': str(drawing), 'sha256': output_hash, 'report': str(report)}
    if require_complete_plot:
        try:
            count = review.get('plottedPages')
            require(type(count) is int and count > 0, '正式验收未记录已打印图幅数量')
            verify_final_plot_receipt(report, output_hash, formal, expected_pages=count)
        except (JobError, OSError, ValueError, KeyError, TypeError) as error:
            raise RetryableHostError(f'同版正式打印尚未完整核验：{error}') from error
    if not formal:
        checklist = Path(receipt['blockingChecklist']).resolve(strict=True)
        digest = receipt['checklistSHA256']
        require(checklist.parent == report and checklist.suffix.lower() == '.csv' and file_hash(checklist) == digest,
                '遗留清单路径或指纹与生成回执不一致')
        open_items = {(str(item['panel']), item['source']) for item in review['blockingLocations'] if item['findings']}
        with checklist.open(encoding='utf-8-sig', newline='') as stream:
            rows = list(csv.DictReader(stream))
        require(open_items and len(rows) == len(open_items) and
                {(row['源图幅'], row['源对象']) for row in rows} == open_items and
                all(row['DWG SHA-256'] == output_hash and row['实际部位'] and row['适用规则'] and
                    row['实际楼层'] and row['问题'] and row['图面保留状态'] for row in rows),
                '遗留清单未逐项对应同版 DWG 的核验记录')
        return {**result, 'checklist': str(checklist), 'checklistSHA256': digest}
    gates = review.get('gates') or {}
    require(all(gates.get(key) is True for key in ('mainPlan', 'reopened', 'files')), '主平面、独立重开和交付验收未全部通过')
    if not require_complete_plot:
        verify_final_plot_receipt(report, output_hash, formal)
    require(not review.get('blockingLocations') and not receipt.get('blockingChecklist'), '全项通过与遗留清单状态不一致')
    return result


def verify_final_plot_receipt(report, output_hash, formal, *, expected_pages=None):
    plots = read_json(report / 'formal-layout-print-review.json')
    ink = read_json(report / 'plan-printed-ink.json')
    pages = plots.get('pages') or []
    require(plots.get('outputSHA256') == output_hash and pages and
            (expected_pages is None or len(pages) == expected_pages) and
            {page.get('panel') for page in pages} == set(range(1, len(pages)+1)) and
            len({page.get('file') for page in pages}) == len(pages), '缺少同版 DWG 的逐幅正式打印记录')
    for page in pages:
        pdf = (report / page['file']).resolve(strict=True)
        require(pdf.parent == report and pdf.suffix.lower() == '.pdf' and page.get('status') == 'plotted' and
                page.get('paperScale') == 1 and page.get('viewportScale') == .01 and page.get('sha256') == file_hash(pdf),
                '逐幅打印文件、比例或指纹校验失败')
    ink_pages = ink.get('pages')
    checked, expected, unresolved = (ink.get(key) for key in ('checked', 'expected', 'unresolved'))
    require(ink.get('contract') == 'plan.printed-ink.v1' and ink.get('drawingSHA256') == output_hash and
            all(type(value) is int for value in (checked, expected, unresolved)) and
            0 <= checked <= expected and expected - checked <= unresolved and 0 <= unresolved <= expected and
            ink.get('verified') is (unresolved == 0) and (not formal or unresolved == 0) and
            isinstance(ink_pages, list) and len(ink_pages) == len(pages) and
            {(page.get('file'), page.get('sha256')) for page in ink_pages} ==
            {(page['file'], page['sha256']) for page in pages},
            '同版逐幅实打字形记录不完整，或与严格验收状态不一致')


def publish_file(source, output, expected):
    if output.exists():
        require(file_hash(output) == expected, '交付位置已有不同文件')
        return False
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=output.parent, delete=False) as stream:
        temporary = Path(stream.name)
    try:
        shutil.copyfile(source, temporary)
        require(file_hash(temporary) == expected, '交付暂存副本指纹与验收记录不一致')
        if os.name == 'nt':
            temporary.rename(output)
        else:
            os.link(temporary, output)
    finally:
        temporary.unlink(missing_ok=True)
    require(file_hash(output) == expected, '交付文件指纹回读失败')
    return True


def checklist_path(output):
    return output.with_name(f'{output.stem}_遗留清单.csv')


def formal_output(path):
    stem = path.stem.removesuffix('_待复核').removesuffix('_待放行')
    if not stem.endswith('_正式版'):
        stem += '_正式版'
    return path.with_name(stem + path.suffix)


def publish(result, output):
    checklist = checklist_path(output)
    require(not output.exists() or file_hash(output) == result['sha256'], '交付位置已有不同 DWG 文件')
    wrote_checklist = False
    if result.get('checklist'):
        wrote_checklist = publish_file(result['checklist'], checklist, result['checklistSHA256'])
    else:
        require(not checklist.exists(), '全项通过的交付位置已有遗留清单')
    try:
        publish_file(result['drawing'], output, result['sha256'])
    except Exception:
        if wrote_checklist and not output.exists() and file_hash(checklist) == result['checklistSHA256']:
            checklist.unlink()
        raise


def execute_plan(source, output, work, settings, run_attempt, *, resume_attempt=None):
    source = source.resolve(strict=True)
    output = output.resolve()
    work = work.resolve()
    require(source != output and output.suffix.lower() == '.dwg', '请选择独立的新 DWG 交付路径')
    require(work != output.parent and work not in output.parents, '作业档案与交付目录需分开')
    source_hash = file_hash(source)
    identity = {'source': str(source), 'sourceSHA256': source_hash, 'output': str(output), 'settings': settings}
    work.mkdir(parents=True, exist_ok=True)
    with job_lock(work / 'job.lock'):
        state = work / 'checkpoint.json'
        checkpoint = read_json(state) if state.exists() else {'identity': identity, 'attempts': []}
        require(checkpoint.get('identity') == identity, '作业来源、规则或输出路径已变化，旧断点无法复用')
        attempts = checkpoint['attempts']

        def check_attempt(record):
            attempt = work / record['directory']
            mode = (settings.get('nativeConfiguration') or {}).get('postcheckMode', 'off')
            try:
                result = verified_candidate(attempt, source_hash, require_complete_plot=mode == 'repair')
            except RetryableHostError as error:
                record.update(status='retryable-plot-review', error=str(error))
                write_json(state, checkpoint)
                return None
            if result is None:
                return None
            require(file_hash(source) == source_hash, '运行期间源 DWG 已变化')
            if mode != 'off':
                postcheck = read_json(attempt / 'job' / 'plan-postcheck.json')
                require(postcheck.get('contract') == 'plan.postcheck.v1' and postcheck.get('mode') == mode and
                        postcheck.get('readOnly') is True and postcheck.get('sourceSHA256') == source_hash and
                        postcheck.get('drawingSHA256') == result['sha256'],
                        '缺少同一版 DWG 的只读查漏记录')
            record.update(result, status='verified' if result['formal'] else 'verified-with-exceptions')
            write_json(state, checkpoint)
            publish(result, output)
            record.update(status='delivered')
            write_json(state, checkpoint)
            return output

        for record in reversed(attempts):
            if record['status'] == 'running' and resume_attempt is not None:
                try:
                    resume_attempt(work / record['directory'])
                except RetryableHostError as error:
                    record['error'] = str(error)
                record['status'] = 'interrupted'
                write_json(state, checkpoint)
            if (result := check_attempt(record)) is not None:
                return result
            if record['status'] == 'failed':
                raise JobError(record['error'])
        require(not output.exists(), '交付位置已有文件，且没有与本作业相符的验收记录')
        for number in range(len(attempts)+1, 4):
            record = {'directory': f'attempt-{number:02d}', 'status': 'running'}
            attempts.append(record)
            write_json(state, checkpoint)
            try:
                run_attempt(work / record['directory'])
            except RetryableHostError as error:
                record.update(status='retryable-host-failure', error=str(error))
                write_json(state, checkpoint)
                if (result := check_attempt(record)) is not None:
                    return result
                continue
            except JobError as error:
                record.update(status='failed', error=str(error))
                write_json(state, checkpoint)
                raise
            if (result := check_attempt(record)) is not None:
                return result
            if record['status'] == 'retryable-plot-review':
                continue
            record.update(status='failed', error='原生执行未留下可核验的生成回执')
            write_json(state, checkpoint)
            raise JobError(record['error'])
        raise JobError(f'自有 CAD 宿主的自动重试次数已用完，作业记录位于 {work}')


def workspace_root(explicit, cwd, skill):
    if not explicit and (skill / 'host/controller/run_quiet_job.py').is_file():
        return skill.resolve()
    roots = [Path(explicit)] if explicit else [cwd, *cwd.parents, *skill.parents, Path('D:/Codex/建筑施工图自动化')]
    for root in roots:
        if (root / 'host/controller/run_quiet_job.py').is_file():
            return root.resolve()
        if (root / '阶段1/原生接口探测/run_quiet_job.py').is_file():
            return root.resolve()
    raise JobError('没有找到平面 Skill 配套的 AutoCAD 隔离宿主工作区')


def project_bindings(profile):
    if profile is None:
        return {}
    bindings = {str(profile): file_hash(profile)}
    for line in profile.read_text(encoding='utf-8-sig').splitlines():
        if not line.lstrip().startswith('FILE '):
            continue
        fields = shlex.split(line, posix=True)
        require(len(fields) == 3, '项目参数行必须绑定一个明确文件')
        included = (profile.parent / fields[2]).resolve(strict=True)
        bindings[str(included)] = file_hash(included)
    return bindings


def source_project_parameters(source):
    profile = source.with_suffix('.plan-project.tsv')
    if not profile.is_file():
        return None
    try:
        with profile.open(encoding='utf-8-sig') as stream:
            header = stream.readline().split()
        if header != ['PLANPROJECT1', file_hash(source)]:
            return None
        project_bindings(profile)
    except (JobError, OSError, ValueError):
        return None
    return profile


def plan_resource_files(resources):
    # AutoCAD owns these locks and may leave them behind after an interruption.
    return sorted(path for path in resources.rglob('*')
                  if path.suffix.lower() not in {'.dwl', '.dwl2'} and path.is_file())


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--workspace-root', type=Path)
    parser.add_argument('--work-directory', type=Path)
    parser.add_argument('--project-parameters', type=Path)
    parser.add_argument('--plot-style', default='monochrome.ctb')
    parser.add_argument('--module', type=Path)
    parser.add_argument('--template-sheets', type=Path,
                        help='对已成图DWG执行模板终核；未指定时先成图，再自动执行模板终核')
    parser.add_argument('--postcheck-mode', choices=('off', 'audit', 'repair'), default='repair')
    args = parser.parse_args(argv)
    required_modules = ('pymupdf', 'PIL', 'fontTools')
    missing_modules = [name for name in required_modules if importlib.util.find_spec(name) is None]
    require(not missing_modules, '本机 Python 缺少实际打印分析依赖：' + ', '.join(missing_modules))

    skill = Path(__file__).resolve().parents[1]
    workspace = workspace_root(args.workspace_root, Path.cwd(), skill)
    source, output = args.source.resolve(), formal_output(args.output).resolve()
    resources = skill / 'assets'
    module = (args.module or skill / 'tools/PlanNative.arx').resolve(strict=True)
    if args.template_sheets:
        from template_revision import execute_template_revision, publish_template_result
        work = args.work_directory or workspace / 'outputs/.plan-template-jobs' / file_hash(source)[:24]
        result = execute_template_revision(source, args.template_sheets.resolve(strict=True), work,
                                           workspace, skill, module=module)
        publish_template_result(result, output, prior_checklist=checklist_path(source))
        print(output)
        return
    ink_analyzer = (skill / 'tools/check_plan_print_ink.py').resolve(strict=True)
    project = (args.project_parameters.resolve(strict=True) if args.project_parameters else
               source_project_parameters(source))
    config = {'acceptance': 'architectural-plan', 'delivery': 'formal', 'mode': 'generate', 'printReview': '1',
              'scale': '100', 'unitMM': '1', 'plotStyle': args.plot_style, 'resources': str(resources),
              'plotInkAnalyzer': str(ink_analyzer), 'plotInkPython': sys.executable, 'roomTextConversion': '1',
              'postcheckMode': args.postcheck_mode, 'templatePreparation': '1'}
    if project:
        config['projectParameters'] = str(project)
    joint_search = ink_analyzer.with_name('joint_annotation_search.py').resolve(strict=True)
    files = {str(path): file_hash(path) for path in (module, ink_analyzer, joint_search, *plan_resource_files(resources))}
    files.update({str(path): file_hash(path) for path in packaged_host_inputs(workspace)})
    files.update(project_bindings(project))
    bindings = {'nativeConfiguration': config, 'inputFilesSHA256': files}
    job_version = json.dumps(bindings, ensure_ascii=False, sort_keys=True)
    work_key = hashlib.sha256(os.fsencode(os.path.normcase(output)) + job_version.encode('utf-8')).hexdigest()[:24]
    work = args.work_directory or workspace / 'outputs' / '.plan-draw-jobs' / work_key
    def guard_inputs():
        require(all(file_hash(path) == digest for path, digest in files.items()), '原生模块、规则或项目参数在运行期间发生了变化')

    def native_attempt(attempt):
        guard_inputs()
        run_owned_plan(workspace, module, source, attempt, config)
        guard_inputs()

    def resume_attempt(attempt):
        guard_inputs()
        resume_owned_plan(workspace, module, attempt, resources)
        guard_inputs()

    # The old geometry route produces an internal preparation only. User-visible
    # publication occurs after the template-native pass and independent plotting.
    prepared_output = work.parent / (work.name + '-prepared') / 'prepared.dwg'
    result = execute_plan(source, prepared_output, work, bindings, native_attempt,
                          resume_attempt=resume_attempt)
    state = read_json(work / 'checkpoint.json')
    prepared = next(row for row in reversed(state['attempts']) if row['status'] == 'delivered')
    pages = read_json(Path(prepared['report']) / 'formal-layout-print-review.json')['pages']
    sheets = work / 'template-sheets.tsv'
    sheets.write_text('PLANJOINT1 ' + file_hash(result) + '\n' + ''.join(
        f'SHEET {index} {json.dumps(page["layout"], ensure_ascii=False)}\n'
        for index, page in enumerate(pages, 1)) + 'END\n', encoding='utf-8')
    from template_revision import execute_template_revision, publish_template_result
    frame_datums = None
    layout_file = Path(prepared['report']) / '外围间距核验.json'
    if layout_file.is_file():
        layout = read_json(layout_file)
        sides = {}
        for side in layout['sidePlacement']:
            key = (side['view'], side['side'])
            require(key not in sides, '辅助框排布基准重复')
            sides[key] = side['coordinate']
        rows = []
        for index, page in enumerate(pages, 1):
            panel = page['panel']
            require(all((panel, side) in sides for side in range(4)), '图幅缺少已核实的辅助框基准')
            bounds = ' '.join(str(sides[panel, side]) for side in (0, 2, 1, 3))
            rows.append(f'PANEL {index} {bounds}')
        frame_datums = work / 'template-frame-datums.tsv'
        frame_datums.write_text('PLANFRAMES1 ' + file_hash(result) + '\n' + '\n'.join(rows) + '\nEND\n', encoding='ascii')
    finalized = execute_template_revision(result, sheets, work / 'template-finalize', workspace, skill, module=module, frame_datums=frame_datums)
    publish_template_result(finalized, output, prior_checklist=checklist_path(result))
    print(output)
    if checklist_path(output).is_file():
        print(checklist_path(output))


if __name__ == '__main__':
    try:
        main()
    except (JobError, OSError, ValueError, KeyError) as error:
        sys.exit(str(error))
