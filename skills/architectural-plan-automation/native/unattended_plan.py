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
                             resume_owned_plan, run_owned_plan, write_json, verify_owned_host_closure)


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
    verify_owned_host_closure(attempt)
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
    from runtime_preflight import discover_workspace
    try:
        return discover_workspace(explicit, cwd, skill)
    except (OSError, ValueError) as error:
        raise JobError(str(error)) from error


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
        if fields[1] == 'designSources':
            from design_sources import adoption_bindings
            bindings.update(adoption_bindings(included))
    return bindings


def project_design_sources(profile, source):
    if profile is None:
        return None
    lines = Path(profile).read_text(encoding='utf-8-sig').splitlines()
    require(lines and lines[0].split() == ['PLANPROJECT1', file_hash(source)],
            '项目资料必须绑定本轮源 DWG')
    found = []
    for line in lines[1:]:
        fields = shlex.split(line, posix=True)
        if len(fields) == 3 and fields[:2] == ['FILE', 'designSources']:
            found.append((Path(profile).parent / fields[2]).resolve(strict=True))
    require(len(found) <= 1, '项目逐樘资料采用表重复')
    if found:
        from design_sources import adoption_bindings
        adoption_bindings(found[0], source)
        return found[0]
    return None


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


def verify_final_plan_scope(prepared_report, finalized):
    """Follow the same original objects through preparation and every saved template pass."""
    final_report = Path(finalized['report'])
    original = read_json(prepared_report / 'plan-furniture-readback.json')
    omission = read_json(prepared_report / 'plan-omissions-readback.json')
    require(original.get('savedReopened') and omission.get('savedReopened'), '缺少准备图独立重开的对象回执')
    expected = {item['object']: item['targetLayer'] for item in original['items']}
    require(len(expected) == len(original['items']) and
            all(item['geometryVerified'] and item['layerVerified'] for item in original['items']),
            '准备图的家具身份、结构或归层复核失败')
    removed = {item['handle'] for item in omission['removed'] if item['result'] == 'verified-absent'}
    for location in finalized.get('history', [final_report]):
        report = Path(location)
        saved = read_json(report / 'plan-furniture-readback.json')
        actual = {item['object']: item for item in saved['items']}
        require(saved.get('savedReopened') and all(handle in actual and
                actual[handle]['geometryVerified'] and actual[handle]['layerVerified'] and
                actual[handle]['targetLayer'] == target for handle, target in expected.items()),
                '模板重开后不能逐件对上原家具结构、句柄或目标图层')
        transferred = read_json(report / 'template-native-readback.json')
        live = {handle for row in transferred['objects'] for handle in (row['source'], *row['targets'])}
        require(removed.isdisjoint(live), '已清除的旧标注再次出现在模板交接回执中')
    digest = file_hash(Path(finalized['drawing']))
    require(digest == finalized['sha256'] == read_json(final_report / 'formal-layout-print-review.json')['outputSHA256'],
            '对象复核与同版试印指纹不一致')
    # The catalog describes common preparation actions across module versions.
    # Record the current module's object evidence separately against the final plotted DWG.
    cases = {}
    for key in ('D23', 'S01'):
        handles = sorted(row['handle'] for row in omission['removed']
                         if row.get('rule') == key and row['result'] == 'verified-absent')
        cases[key] = {'action': 'remove-interior-dimensions' if key == 'D23' else 'remove-room-area-labels',
                      'scope': '本次源图明确识别并退场的对象及模板交接回执',
                      'removedSourceHandles': handles, 'savedReopened': True}
    cases['Y09'] = {'action': 'assign-original-furniture-layer',
                    'verifiedOriginalLayers': dict(sorted(expected.items()))}
    cases['NC17'] = {'action': 'verify-original-furniture-structure', 'verifiedOriginalHandles': sorted(expected),
                     'scope': '每次保存重开核原句柄、定义、成员、属性、变换及原几何，外层图层按用途验收'}
    write_json(final_report / 'plan-current-rules-final.json', {
        'schema': 'plan.final-rule-evidence.v1', 'outputSHA256': digest,
        'furnitureVerified': len(expected), 'removedSourceHandles': sorted(removed), 'ruleEvidence': cases,
        'scope': '本版实证仅覆盖列明对象。各图幅和其他事项按最终模板、实打及 P06 同版清单复核。'})


def execute_ordinary_revision(source, output, work, workspace, skill, module, config):
    from template_revision import execute_template_revision, publish_template_result
    from wall_height_pipeline import reconcile_wall_heights, floor_levels_from_project
    source, work, output = Path(source).resolve(strict=True), Path(work).resolve(), Path(output).resolve()
    work.mkdir(parents=True, exist_ok=True)
    files = {str(p): file_hash(p) for p in (module, *plan_resource_files(skill / 'assets'),
                                             *packaged_host_inputs(workspace))}
    if config.get('projectParameters'):
        files.update(project_bindings(Path(config['projectParameters'])))
    source_hash = file_hash(source)
    identity = {'sourceSHA256': source_hash, 'inputFilesSHA256': files, 'configuration': config}
    with job_lock(work / 'revision.lock'):
        state_file = work / 'revision-job.json'
        state = read_json(state_file) if state_file.is_file() else dict(identity=identity, attempts=[])
        require(state['identity'] == identity, '修订暂存目录已绑定其他版本或任务')
        for number in range(1, 4):
            attempt = work / f'revision-{number:02}'
            report = attempt / 'job'
            if attempt.exists():
                if (attempt / 'host.json').is_file() and not (report / 'product-result.json').is_file():
                    try:
                        resume_owned_plan(workspace, module, attempt, skill / 'assets')
                    except RetryableHostError:
                        continue
            else:
                state['attempts'].append(attempt.name)
                write_json(state_file, state)
                try:
                    run_owned_plan(workspace, module, source, attempt, config)
                except RetryableHostError:
                    continue
            if not (report / 'product-result.json').is_file():
                continue
            try:
                verify_owned_host_closure(attempt, module=module, workspace=workspace)
            except RetryableHostError:
                continue
            receipt = read_json(report / 'product-result.json')
            require(receipt.get('completed') and receipt.get('reopened'), receipt.get('error') or '通用修订未完成保存重开')
            drawing = report / 'candidate.dwg'
            digest = file_hash(drawing)
            require(source_hash == file_hash(source) == receipt['sourceSHA256'] and digest == receipt['outputSHA256']
                    and all(file_hash(p) == h for p, h in files.items()), '修订源图、成图或输入资源指纹已变化')
            furniture = read_json(report / 'plan-furniture-readback.json')
            omissions = read_json(report / 'plan-omissions-readback.json')
            require(furniture.get('savedReopened') and omissions.get('savedReopened') and
                    all(x['geometryVerified'] and x['layerVerified'] for x in furniture['items']),
                    '修订缺少家具和旧标注的独立重开凭据')
            layouts = receipt['layouts']
            require(layouts and len(layouts) == len(set(layouts)), '原图没有可核实的出图布局或布局冲突')
            sheets = work / 'revision-sheets.tsv'
            sheets.write_text('PLANJOINT1 ' + digest + '\n' + ''.join(
                f'SHEET {i} {json.dumps(name, ensure_ascii=False)}\n' for i, name in enumerate(layouts, 1)) +
                'END\n', encoding='utf-8')
            prior = []
            carried = checklist_path(source)
            if not carried.is_file():
                adjacent = source.parent / '待确认清单.csv'
                if adjacent.is_file():
                    carried = adjacent
            if carried.is_file():
                with carried.open(encoding='utf-8-sig', newline='') as stream:
                    rows = list(csv.DictReader(stream))
                if carried == checklist_path(source):
                    require(all(row.get('DWG SHA-256', '').upper() == source_hash for row in rows),
                            '修订源图与其同名遗留清单指纹不同')
                prior = [row for row in rows if row.get('DWG SHA-256','').upper() == source_hash]
            remaining = [(row.get('源对象') or row.get('对象',''), row.get('类别') or row.get('实际部位',''),
                          row.get('问题与原因') or row.get('原因') or row.get('问题','')) for row in prior]
            remaining.extend((handle, category, reason) for category, handle, reason in receipt['pending'])
            staging = report / 'revision-unresolved.csv'
            with staging.open('w', encoding='utf-8-sig', newline='') as stream:
                writer = csv.writer(stream)
                writer.writerow(('对象','类别','原因','DWG SHA-256'))
                writer.writerows((*row, digest) for row in dict.fromkeys(remaining))
            levels_file = floor_levels_from_project(config.get('projectParameters'), source)
            wall_expected = None
            if levels_file:
                wall_expected = reconcile_wall_heights(source, work / 'project-wall-basis', workspace, skill, module,
                                                        floor_levels=levels_file, repair=False)['expected']
            result = execute_template_revision(drawing, sheets, work / 'template-finalize', workspace, skill,
                                               module=module, wall_expected=wall_expected)
            require(file_hash(source) == source_hash, '修订过程中源图发生变化')
            verify_final_plan_scope(report, result)
            publish_template_result(result, output, prior_checklist=staging)
            return output
        raise JobError('通用修订未取得可验证的原生回执，保留暂存目录以便续验')




def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--mode', choices=('generate','revise'), default='generate')
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
    source, output = args.source.resolve(), formal_output(args.output).resolve()
    resources = skill / 'assets'
    module = (args.module or skill / 'tools/PlanNative.arx').resolve(strict=True)
    from runtime_preflight import module_compatibility
    project = (args.project_parameters.resolve(strict=True) if args.project_parameters else source_project_parameters(source))
    design_sources = project_design_sources(project, source)
    effective_mode = 'generate' if design_sources else args.mode
    template_only = bool(args.template_sheets) and not design_sources
    capability = module_compatibility(skill, module, 'template-finalize' if template_only else effective_mode)
    require(capability['compatible'], capability['reason'] + ' 保留原图，继续只读查漏或已验证的副本终核；禁止用旧破坏性路径生成后再修补。')
    workspace = workspace_root(args.workspace_root, Path.cwd(), skill)
    project = (args.project_parameters.resolve(strict=True) if args.project_parameters else source_project_parameters(source))
    from wall_height_pipeline import floor_levels_from_project
    floor_levels = floor_levels_from_project(project, source)
    if template_only:
        from template_revision import execute_template_revision, publish_template_result
        work = args.work_directory or workspace / 'outputs/.plan-template-jobs' / file_hash(source)[:24]
        result = execute_template_revision(source, args.template_sheets.resolve(strict=True), work,
                                           workspace, skill, module=module, floor_levels=floor_levels)
        publish_template_result(result, output, prior_checklist=checklist_path(source))
        print(output)
        return
    ink_analyzer = (skill / 'tools/check_plan_print_ink.py').resolve(strict=True)
    project = (args.project_parameters.resolve(strict=True) if args.project_parameters else
               source_project_parameters(source))
    config = {'acceptance': 'architectural-plan', 'delivery': 'formal', 'mode': effective_mode, 'printReview': '1',
              'scale': '100', 'unitMM': '1', 'plotStyle': args.plot_style, 'resources': str(resources),
              'plotInkAnalyzer': str(ink_analyzer), 'plotInkPython': sys.executable, 'roomTextConversion': '1',
              'postcheckMode': args.postcheck_mode, 'templatePreparation': '1'}
    if project:
        config['projectParameters'] = str(project)
    if args.mode == 'revise' and not design_sources:
        key = hashlib.sha256((str(output) + file_hash(source) + file_hash(module)).encode('utf-8')).hexdigest()[:24]
        work = args.work_directory or workspace / 'outputs/.plan-revise-jobs' / key
        print(execute_ordinary_revision(source, output, work, workspace, skill, module, config))
        return
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
        roof_file = layout_file.with_name('roof-plan-readback.json')
        if roof_file.is_file():
            roof = read_json(roof_file)
            for side in roof.get('existingAxisDimensions', []):
                if side.get('savedReopened') and side.get('result') == 'verified-two-native-axis-distance-chains' and 'datum' in side:
                    sides.setdefault((side['panel'], side['side']), side['datum'])
        rows = []
        for index, page in enumerate(pages, 1):
            panel = page['panel']
            present = [(panel, side) in sides for side in range(4)]
            if not any(present):
                continue
            require(all(present), '图幅四侧辅助框基准不完整')
            bounds = ' '.join(str(sides[panel, side]) for side in (0, 2, 1, 3))
            rows.append(f'PANEL {index} {bounds}')
        if rows:
            frame_datums = work / 'template-frame-datums.tsv'
            frame_datums.write_text('PLANFRAMES1 ' + file_hash(result) + '\n' + '\n'.join(rows) + '\nEND\n', encoding='ascii')
    from wall_height_pipeline import prepared_wall_basis
    wall_expected = prepared_wall_basis(result, Path(prepared['report']))
    finalized = execute_template_revision(result, sheets, work / 'template-finalize', workspace, skill, module=module,
                                           frame_datums=frame_datums, wall_expected=wall_expected)
    verify_final_plan_scope(Path(prepared['report']), finalized)
    if design_sources:
        from design_sources import verify_final_sources
        guard_inputs()
        verify_final_sources(Path(prepared['report']), finalized, work, workspace, skill, module, design_sources)
        guard_inputs()
    publish_template_result(finalized, output, prior_checklist=checklist_path(result))
    print(output)
    if checklist_path(output).is_file():
        print(checklist_path(output))


if __name__ == '__main__':
    try:
        main()
    except (JobError, OSError, ValueError, KeyError) as error:
        sys.exit(str(error))
