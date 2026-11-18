"""Run the template-native final pass, retaining retries and hash-bound evidence."""
from pathlib import Path
import argparse
import csv
import json
import math
import sys

from owned_plan_host import (JobError, RetryableHostError, file_hash, read_json,
                             require, run_owned_plan, resume_owned_plan, write_json)


def missing_paper_segments(audit, ink):
    missing = []
    for frame in ink['frames']:
        cx, cy = frame['modelCenter']; width, height = frame['modelSize']
        checked = {(t['handle'], t['segment']) for t in frame['targets']}
        for row in audit['objects']:
            if row['kind'] != 'dimension' or not row['verified']:
                continue
            for segment, (a, b) in enumerate(zip(row['stations'], row['stations'][1:])):
                length = math.hypot(a[0]-b[0], a[1]-b[1])
                x, y = (a[0]+b[0])/2, (a[1]+b[1])/2
                if not (0 < length <= 450.0001 and abs(x-cx) < width/2 and abs(y-cy) < height/2):
                    continue
                if len(row['targets']) != 1 or (row['targets'][0], segment) not in checked:
                    missing.append({'object': '/'.join(row['targets']) + '/' + str(segment+1),
                                    'role': 'printed-segment-coverage',
                                    'reason': f"图幅{frame['panel']}第{segment+1}段短尺寸未进入实打字形核验；斜向或未支持表达需单独处理，不能计为已通过"})
    return missing


def verify_final_panel_coverage(sheets, source_digest, pages, ink_frames):
    lines = Path(sheets).read_text('utf-8-sig').splitlines()
    require(lines and lines[0].split() == ['PLANJOINT1', source_digest],
            '图幅应有清单与本轮源DWG不一致')
    expected = []
    for line in lines[1:]:
        if line.startswith('SHEET '):
            _, panel, layout = line.split(maxsplit=2)
            expected.append((int(panel), json.loads(layout)))
    require(expected and len({panel for panel, _ in expected}) == len(expected) and
            len({layout for _, layout in expected}) == len(expected), '输入图幅重复或缺少应有清单')
    actual = [(page['panel'], page['layout']) for page in pages]
    require(len(actual) == len(expected) and set(actual) == set(expected),
            '最终布局有漏幅、重复或图幅归属与输入不符')
    ink_panels = [frame['panel'] for frame in ink_frames]
    require(len(ink_panels) == len(expected) and set(ink_panels) == {panel for panel, _ in expected},
            '实打复核未覆盖全部应有图幅或出现重复')
    return [{'panel': panel, 'layout': layout} for panel, layout in expected]


def typography_evidence_issues(audit, policy=None):
    issues = []
    styles = {style['name']: style for style in (policy or {}).get('styles', [])}
    for row in audit['objects']:
        if not row['verified']:
            continue
        glyphs = row.get('glyphs', [])
        complete = bool(glyphs) and all(
            glyph.get('style') and (glyph.get('font') or glyph.get('fontFamily')) and
            all(isinstance(glyph.get(key), (int, float)) and math.isfinite(glyph[key]) and glyph[key] > 0
                for key in ('width', 'height'))
            for glyph in glyphs)
        if not complete:
            issues.append({'object': row['object'], 'role': row['role'],
                           'reason': '文字通过记录缺少有效实际字形、字体、宽度或字高证据，未计为通过'})
            continue
        expected_style = {'general': 'FY_HZ', 'opening-label': '_TCH_WINDOW',
                          'native-opening-label': '_TCH_WINDOW'}.get(row['role'])
        for glyph in glyphs:
            spec = styles.get(expected_style or glyph['style'])
            if spec is None:
                continue
            font = Path(glyph.get('font', '').replace('\\', '/')).name.lower()
            big = Path(glyph.get('bigFont', '').replace('\\', '/')).name.lower()
            if (glyph['style'] != spec['name'] or font != spec['font'].lower() or
                    big != spec['bigFont'].lower() or abs(glyph['width'] - spec['width']) > .01 or
                    min(abs(glyph['height'] - spec['heightMM']),
                        abs(glyph['height'] - spec['heightMM'] * 100)) > .01):
                issues.append({'object': row['object'], 'role': row['role'],
                               'reason': '实际文字引用、字体、有效字高或宽度与当前三套强制参数不符'})
                break
    return issues


def verify_template_revision(attempt, source, resources, *, sheets=None):
    report = attempt / 'job'
    drawing = report / 'candidate.dwg'
    digest = file_hash(drawing)
    receipt = read_json(report / 'product-result.json')
    audit = read_json(report / 'template-native-readback.json')
    for value in (receipt, audit):
        require(value.get('sourceSHA256') == file_hash(source) and value.get('outputSHA256') == digest,
                '模板终核与源图、最终DWG指纹不一致')
    require(receipt.get('completed') is True and receipt.get('dwgReopened') is True and audit.get('savedReopened') is True,
            '模板终核没有完成独立重开')
    require(receipt.get('nonAnnotationGeometryPreserved') is True, '缺少非标注几何保护和重开证据')
    standard = read_json(resources / 'annotation-template/standard.json')
    require(audit.get('templateSHA256') == file_hash(resources / 'annotation-template' / standard['dwg']) == standard['sha256'].upper(),
            '原生对象来源不是随包核准模板')
    donors = {(row['handle'], row['type']) for row in standard['nativeDonors']}
    unresolved = receipt.get('unresolved')
    require(isinstance(unresolved, list), '缺少模板未解决项清单')
    for row in audit['objects']:
        if row['verified']:
            require(row['targets'] and not row['reason'] and
                    (row['donor'], 'TDbDimension2' if row['kind'] == 'dimension' else 'TDbAxisLabelSet') in donors,
                    '通过对象缺少实际模板样件来源')
        else:
            require(row['reason'] and any(x['object'] == row['source'] and x['reason'] == row['reason'] for x in unresolved),
                    '未通过对象没有进入同版清单')
    text_audit = read_json(report / 'template-typography-readback.json')
    require(text_audit.get('outputSHA256') == digest and text_audit.get('savedReopened') is True,
            '字体复核没有绑定最终重开DWG')
    for row in text_audit['objects']:
        if not row['verified']:
            require(any(x['object'] == row['object'] and x['role'] == row['role'] for x in unresolved),
                    '字体未通过对象没有进入同版清单')
    typography_issues = typography_evidence_issues(text_audit, standard.get('finalTypographyPolicy'))
    unresolved.extend(typography_issues)
    host = read_json(attempt / 'host.json')
    closure = read_json(Path(host['session']) / 'isolation-final.json')
    require(closure.get('userCadTouched') is False, '缺少用户 CAD 隔离证据')
    if (closure.get('nativeQuitConfirmed') is not True or closure.get('workerExitCode') != 0 or
            closure.get('forcedTermination') is not False):
        raise RetryableHostError('自有宿主未正常退出，不能计为验证通过')
    ink = read_json(report / 'typography-paper-input.json')
    require(ink.get('drawingSHA256') == digest, '短尺寸实打输入不是最终DWG')
    coverage_issues = missing_paper_segments(audit, ink)
    unresolved.extend(coverage_issues)
    write_json(report / 'template-paper-coverage.json', {'outputSHA256': digest, 'unresolved': coverage_issues})
    plots = read_json(report / 'formal-layout-print-review.json')
    require(plots.get('outputSHA256') == digest and plots.get('pages'), '缺少最终同版整套实打')
    if sheets is not None:
        panels = verify_final_panel_coverage(sheets, file_hash(source), plots['pages'], ink['frames'])
        write_json(report / 'template-final-checks.json', {
            'outputSHA256': digest,
            'ruleCatalogSHA256': file_hash(resources / 'plan-policy/rule-catalog.tsv'),
            'expectedPanels': panels,
            'typographyEvidenceIssues': typography_issues,
        })
    frame_check = read_json(report / 'defpoints-frame-readback.json')
    frames = frame_check['frames']
    expected = {(page['panel'], row) for page in plots['pages'] for row in range(1, 5)}
    require(frame_check['outputSHA256'] == digest and len(frames) == len(expected) and
            len({f['handle'] for f in frames}) == len(frames) and
            {(f['panel'], f['row']) for f in frames} == expected, '重开后未能逐幅核实四道 Defpoints 实体框')
    for page in plots['pages']:
        require(page.get('status') == 'plotted' and page['paperScale'] == 1 and page['viewportScale'] == .01 and
                file_hash(report / page['file']) == page['sha256'], '最终实打缺页、比例错误或文件被替换')
        require(Path(page.get('device', '')).name == 'FY-XS-2026.9-PDF.pc3' and
                Path(page.get('plotStyle', '')).name == ('FY-XS-2026.9-A1.ctb' if '_A1_' in page['paper'] else 'FY-XS-2026.9-A3 A4.ctb'), '打印未实际采用模板PC3/CTB')
        import pymupdf
        with pymupdf.open(report / page['file']) as pdf:
            require(len(pdf) == 1, '单幅打印文件页数错误')
            # AutoCAD keeps invisible searchable text; only visible text must be outlines.
            require(not any(span['type'] != 3 for span in pdf[0].get_texttrace()),
                    'PDF仍有可见字体文字，所有字体转几何选项没有实际生效')
    return {'drawing': str(drawing), 'sha256': digest, 'report': str(report), 'unresolved': unresolved,
            'formal': not unresolved}


def execute_template_revision(source, sheets, work, workspace, skill, *, module=None, auto_repair=True, frame_datums=None):
    source, sheets, work, workspace, skill = (Path(p).resolve() for p in (source, sheets, work, workspace, skill))
    module = module or skill / 'tools/PlanNative.arx'
    resources = skill / 'assets'
    config = {'mode': 'template-finalize', 'resources': str(resources), 'typographySheets': str(sheets)}
    identity = {'source': file_hash(source), 'sheets': file_hash(sheets), 'module': file_hash(module),
                'template': file_hash(resources / 'annotation-template/平面标注.dwg'),
                'plotters': file_hash(resources / 'plotters/manifest.json'),
                'standard': file_hash(resources / 'annotation-template/standard.json'),
                'layers': file_hash(resources / 'plotters/layers.tsv'),
                'ruleCatalog': file_hash(resources / 'plan-policy/rule-catalog.tsv'),
                'planRules': file_hash(resources / 'plan-policy/all-plan-rules.json'),
                'inkAnalyzer': file_hash(skill / 'tools/check_plan_print_ink.py'),
                'entry': file_hash(Path(__file__)), 'host': file_hash(Path(__file__).with_name('owned_plan_host.py'))}
    if frame_datums is not None:
        config['frameDatums'] = str(frame_datums.resolve(strict=True))
        identity['frameDatums'] = file_hash(frame_datums)
    work.mkdir(parents=True, exist_ok=True)
    checkpoint = work / 'template-job.json'
    if checkpoint.exists() and read_json(checkpoint)['identity'] != identity:
        import hashlib
        version = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()[:24]
        work = work / ('revision-' + version)
        work.mkdir(parents=True, exist_ok=True)
        checkpoint = work / 'template-job.json'
    if checkpoint.exists():
        require(read_json(checkpoint)['identity'] == identity, '模板作业输入发生变化，不能沿用旧断点')
        state = read_json(checkpoint)
    else:
        state = {'identity': identity, 'attempts': []}
        write_json(checkpoint, state)
    from unattended_plan import job_lock
    with job_lock(work / 'job.lock'):
        for number in range(1, 4):
            attempt = work / f'attempt-{number:02}'
            if attempt.exists() and (attempt / 'host.json').is_file():
                session = Path(read_json(attempt / 'host.json')['session'])
                if not (session / 'isolation-final.json').exists():
                    try:
                        resume_owned_plan(workspace, module, attempt, resources)
                    except RetryableHostError as error:
                        state.setdefault('resumeFailures', []).append({'attempt': attempt.name, 'error': str(error)})
                        write_json(checkpoint, state)
                        continue
            if not attempt.exists():
                record = {'attempt': attempt.name, 'status': 'running'}
                state['attempts'].append(record)
                write_json(checkpoint, state)
                try:
                    run_owned_plan(workspace, module, source, attempt, config)
                except RetryableHostError as error:
                    record.update(status='retryable-host-failure', error=str(error))
                    write_json(checkpoint, state)
                    continue
                except JobError as error:
                    record.update(status='native-failure', error=str(error))
                    write_json(checkpoint, state)
                    raise
            result_file = attempt / 'job/product-result.json'
            if not result_file.is_file() or not read_json(result_file).get('completed'):
                continue
            try:
                result = verify_template_revision(attempt, source, resources, sheets=sheets)
            except RetryableHostError as error:
                record = next((row for row in state['attempts'] if row['attempt'] == attempt.name), None)
                if record is not None:
                    record.update(status='retryable-host-closure', error=str(error))
                write_json(checkpoint, state)
                continue
            result['history'] = [str(attempt / 'job')]
            sys.path.insert(0, str(skill / 'tools'))
            from check_plan_print_ink import main as check_ink
            from joint_annotation_repair import read_results
            report = attempt / 'job'
            reply = report / 'template-paper-result.tsv'
            if not reply.exists():
                check_ink(report / 'typography-paper-input.json', reply)
            remaining = sorted(read_results(reply, result['sha256']))
            result['paperIssues'] = remaining
            result['formal'] = result['formal'] and not remaining
            if auto_repair and remaining:
                request_path = report / 'template-repair-proposal.json'
                request = read_json(report / 'typography-paper-input.json')
                request['proposeRepairs'] = True
                write_json(request_path, request)
                proposal = report / 'template-repair-proposal.tsv'
                if not proposal.exists():
                    check_ink(request_path, proposal)
                lookup = {(f['panel'], t['handle'], t['segment']): t for f in request['frames'] for t in f['targets']}
                moves = {}
                for line in proposal.read_text('ascii').splitlines():
                    parts = line.split()
                    if parts and parts[0] == 'MOVE':
                        panel, handle, segment = int(parts[1]), parts[2], int(parts[3])
                        dx, dy = float(parts[4]), float(parts[5])
                        if dx*dx + dy*dy <= 1800**2:
                            anchor = lookup[(panel, handle, segment)]['anchor']
                            moves.setdefault((panel, handle), []).append((segment, anchor[0]+dx, anchor[1]+dy))
                if moves:
                    layouts = read_json(report / 'formal-layout-print-review.json')['pages']
                    lines = ['PLANJOINT1 ' + result['sha256']]
                    lines += [f"SHEET {page['panel']} " + json.dumps(page['layout'], ensure_ascii=False) for page in layouts]
                    for (panel, handle), positions in sorted(moves.items()):
                        lines.append(f'CHAIN {panel} {handle} 0')
                        lines += [f'TEXT {panel} {handle} {part} {x:.17g} {y:.17g}' for part, x, y in positions]
                    lines.append('END')
                    repair_plan = report / 'template-text-repair.tsv'
                    repair_plan.write_text('\n'.join(lines)+'\n', 'utf-8')
                    try:
                        repaired = execute_template_revision(Path(result['drawing']), repair_plan, work / 'text-repair',
                                                            workspace, skill, module=module, auto_repair=False)
                    except (JobError, OSError, ValueError) as error:
                        state['repairAttempt'] = {'accepted': False, 'error': str(error)}
                        result['unresolved'].append({'object': '局部尺寸返修', 'role': 'automatic-repair',
                                                     'reason': '局部返修失败，保留已重开实打版本：' + str(error)})
                    else:
                        state['repairAttempt'] = {'before': len(remaining), 'after': len(repaired['paperIssues']),
                                                  'candidate': repaired['sha256'], 'accepted': len(repaired['paperIssues']) < len(remaining)}
                        if state['repairAttempt']['accepted']:
                            repaired['history'] = result['history'] + repaired.get('history', [repaired['report']])
                            result = repaired
            write_json(report / 'template-delivery-verification.json', result)
            state['result'] = result
            write_json(checkpoint, state)
            return result
    raise JobError('模板出图未完成，保留全部失败记录；未发布未验证文件')


def publish_template_result(result, output, *, prior_checklist=None):
    from unattended_plan import publish
    drawing = Path(result['drawing'])
    digest = file_hash(drawing)
    require(digest == result['sha256'], '最终DWG在核验后被改动')
    report = Path(result['report'])
    rows = []
    replacements = {}
    for prior in result.get('history', [result['report']]):
        audit = read_json(Path(prior) / 'template-native-readback.json')
        for item in audit['objects']:
            if item['kind'] == 'dimension' and item['verified'] and len(item['targets']) == 1:
                replacements[item['source']] = item['targets'][0]
    ink = read_json(report / 'typography-paper-input.json')
    checked = {t['handle'] for f in ink['frames'] for t in f['targets']}
    failed = {x[1] for x in result.get('paperIssues', [])}
    closed_history = []
    if prior_checklist and prior_checklist.is_file():
        with prior_checklist.open(encoding='utf-8-sig', newline='') as stream:
            for old in csv.DictReader(stream):
                handle = old.get('源对象', old.get('对象', ''))
                reason = old.get('问题与原因') or old.get('原因') or old.get('问题') or ''
                final_handle = handle.split('/')[0]
                visited = set()
                while final_handle in replacements and final_handle not in visited:
                    visited.add(final_handle)
                    final_handle = replacements[final_handle]
                collision = any(term in reason for term in ('尺寸字框碰撞', '实际纸面尺寸字', '实际文字边界相交', '实打短尺寸'))
                if collision and visited and final_handle in checked and final_handle not in failed:
                    closed_history.append({'priorObject': handle, 'finalDimension': final_handle,
                                           'basis': 'transitive template replacement and final actual paper clearance'})
                    continue

                location = ' / '.join(old.get(key, '') for key in ('实际楼层', '图幅', '源图幅', '实际部位', '文字或测值', '依据') if old.get(key))
                rows.append({'对象': handle, '类别': '此前未关闭事项',
                             '原因': location + '；' + reason + '；本轮仅模板适配，不据此追认该历史事项已关闭',
                             'DWG SHA-256': digest})
    write_json(report / 'template-prior-issues-reconciliation.json', {'outputSHA256': digest, 'closed': closed_history})
    for issue in result['unresolved']:
        rows.append({'对象': issue['object'], '类别': issue['role'], '原因': issue['reason'], 'DWG SHA-256': digest})
    for issue in result.get('paperIssues', []):
        rows.append({'对象': str(issue), '类别': '实打净空', '原因': '同版实打净空未达到既定规则', 'DWG SHA-256': digest})
    delivery = {'drawing': drawing, 'sha256': digest}
    if rows:
        checklist = report / 'template-unresolved.csv'
        with checklist.open('w', encoding='utf-8-sig', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=['对象', '类别', '原因', 'DWG SHA-256'])
            writer.writeheader(); writer.writerows(rows)
        delivery.update(checklist=checklist, checklistSHA256=file_hash(checklist))
    publish(delivery, output)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'sheets', 'work', 'workspace-root'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--frame-datums', type=Path, help='与输入 DWG 指纹匹配的逐层外围排布基准 TSV')
    args = parser.parse_args(argv)
    skill = Path(__file__).resolve().parents[1]
    print(json.dumps(execute_template_revision(args.source.resolve(strict=True), args.sheets.resolve(strict=True),
          args.work.resolve(), args.workspace_root.resolve(), skill, frame_datums=args.frame_datums), ensure_ascii=False))


if __name__ == '__main__':
    main()
