"""Shared, DWG-only wall-height reconciliation for every formal drawing route."""
from pathlib import Path
import json
import math

from owned_plan_host import (JobError, RetryableHostError, file_hash, read_json,
                             require, run_owned_plan, resume_owned_plan, write_json,
                             verify_owned_host_closure)

CONTRACT = 'plan.final-wall-heights.v1'
MAX_REPAIRS = 2


class WallStageFailure(JobError):
    """A recorded native execution failure, eligible for P06 exception delivery."""


def rebind(path, source_hash, target, destination):
    """Rebind an unchanged scope after a height-only edit, never another project."""
    lines = Path(path).read_text('utf-8-sig').splitlines()
    require(lines and lines[0].split()[-1] == source_hash, '墙高关联文件与当前DWG不一致')
    lines[0] = lines[0].rsplit(' ', 1)[0] + ' ' + file_hash(target)
    destination.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return destination


def frozen_text(expected, digest):
    lines = ['PLANWALLTARGETS1 ' + digest]
    number = lambda value: 'null' if value is None else format(value, '.17g')
    for floor in expected['floors']:
        lines.append('FLOOR {} {} {} {} {} {}'.format(
            floor['component'], floor['floor'], number(floor['targetMM']), floor['basis'],
            number(floor.get('finishedMetres')), floor.get('datumBasis') or 'none'))
        lines.extend(f"WALL {floor['component']} {wall['handle']}" for wall in floor['walls'])
    lines.extend('OMIT {} {} {}'.format(row['handle'], row['role'], json.dumps(row['reason'], ensure_ascii=False))
                 for row in expected.get('excludedWalls', []))
    return '\n'.join(lines) + '\nEND\n'


def compare_walls(expected, actual):
    """Verify identity, ownership, native height, base and thickness, including skips."""
    def index(data):
        rows = {}
        for floor in data['floors']:
            for wall in floor['walls']:
                require(wall['handle'] not in rows, '同一面墙出现多个楼层归属')
                rows[wall['handle']] = (floor, wall)
        return rows
    before, after = index(expected), index(actual)
    issues, walls = [], []
    for handle in sorted(before.keys() | after.keys()):
        reason = ''
        target = None
        if handle not in before or handle not in after:
            reason = '最终逐墙覆盖与原应有清单不一致'
        else:
            floor, wall = before[handle]
            current_floor, current = after[handle]
            target = wall['height'] if wall['nativeLowWall'] else floor['targetMM']
            if (floor['component'], floor['floor']) != (current_floor['component'], current_floor['floor']):
                reason = '最终墙体楼层归属发生变化'
            elif wall['nativeLowWall'] != current['nativeLowWall']:
                reason = '普通墙与矮墙用途发生变化'
            elif target is None and not wall['nativeLowWall']:
                reason = '层高来源冲突，尚未取得该楼层可采用的目标高度'
            elif target is not None and (not isinstance(current['height'], (int, float)) or
                                        not math.isfinite(current['height']) or abs(current['height']-target) > .01):
                reason = '独立重开实际墙高与目标不符'
            elif any(current.get(key) != wall.get(key) for key in ('base', 'left', 'right', 'start', 'end')):
                reason = '独立重开墙体底标高、厚度或平面定位发生变化'
        row = {'object': handle, 'targetHeight': target, 'issue': reason}
        if handle in after:
            f, w = after[handle]
            row.update(panel=f['component'], floor=f['floor'], actualHeight=w['height'],
                       actualBase=w.get('base'), nativeLowWall=w['nativeLowWall'])
        walls.append(row)
        if reason:
            issues.append({'object': handle, 'role': 'wall-height', 'reason': reason})
    old_excluded = {row['handle']: row for row in expected.get('excludedWalls', [])}
    new_excluded = {row['handle']: row for row in actual.get('excludedWalls', [])}
    for handle in sorted(old_excluded.keys() | new_excluded.keys()):
        row = new_excluded.get(handle) or old_excluded[handle]
        if row.get('role') not in ('roof', 'confirmed-non-floor') or old_excluded.get(handle) != new_excluded.get(handle):
            issues.append({'object': handle, 'role': 'wall-height-coverage',
                           'reason': row.get('reason', '墙体未进入可靠的楼层逐墙清单')})
    if not before:
        issues.append({'object': '楼层', 'role': 'wall-height-coverage', 'reason': '未取得主体楼层逐墙清单'})
    return walls, issues


def prepared_wall_basis(source, report):
    """Carry the exact generated floor/wall association through moved titles and merging."""
    report = Path(report)
    receipt = read_json(report / 'product-result.json')
    require(receipt['outputSHA256'] == file_hash(source), '生成阶段逐墙依据与准备图指纹不一致')
    readback = read_json(report / 'storey-wall-height-readback.json')
    owners = read_json(report / 'plan-panel-ownership.json')
    require(readback['sourceSHA256'] == owners['sourceSHA256'] == receipt['sourceSHA256'], '生成阶段墙高和楼层来源不同')
    by_component = {r['component']: r for r in owners['components']}
    design_path = report / 'design-source-readback.json'
    boundary = set()
    if design_path.is_file():
        design = read_json(design_path)
        require(design.get('sourceSHA256') == receipt['sourceSHA256'] and design.get('savedReopened') is True,
                '围墙用途回执与生成源图不一致')
        boundary = set(design.get('boundaryWalls', []))
    expected = {'floors': [], 'excludedWalls': []}
    covered = set()
    for level in readback['levels']:
        panel = level['panel']
        owner = by_component[panel]
        require(owner['role'] == 'floor' and owner['floor'] is not None, '生成阶段目标缺楼层身份')
        walls = [{'handle': row['object'], 'height': row['actualHeight'], 'base': row['actualBase'],
                  'nativeLowWall': False} for row in readback['walls'] if row['panel'] == panel and row['object'] not in boundary]
        covered.update(w['handle'] for w in walls)
        expected['floors'].append({'component': panel, 'floor': owner['floor'], 'targetMM': level['heightMM'],
                                    'basis': level['heightBasis'], 'finishedMetres': level['finishedMetres'],
                                    'datumBasis': level['datumBasis'], 'walls': walls})
    for owner in owners['components']:
        for handle in owner['walls']:
            if handle in covered:
                continue
            expected['excludedWalls'].append({'handle': handle,
                'role': 'confirmed-non-floor' if handle in boundary else 'roof' if owner['role'] == 'roof' else
                        'confirmed-non-floor' if owner['role'] in ('detail', 'outside-confirmed-scope') and
                            owner.get('result') == 'omitted-with-evidence' else 'unassigned',
                'reason': 'D31逐墙确认围墙，保留原高度，不套主体层高' if handle in boundary else owner.get('reason') or '生成阶段未取得该墙高度目标'})
    return expected


def floor_levels_from_project(project, source):
    if not project:
        return None
    import shlex
    lines = Path(project).read_text('utf-8-sig').splitlines()
    require(lines and lines[0].split() == ['PLANPROJECT1', file_hash(source)], '项目参数与墙高源图不一致')
    matches = [shlex.split(line)[2] for line in lines[1:] if line.startswith('FILE floorLevels ')]
    require(len(matches) <= 1, '项目层高文件重复')
    return (Path(project).parent / matches[0]).resolve(strict=True) if matches else None


def run_stage(workspace, module, source, work, skill, *, inspect, expected=None, floor_levels=None):
    """Resume a native read/write stage; successful reads are reused by exact hash."""
    workspace, module, source, work, skill = (Path(p).resolve() for p in (workspace, module, source, work, skill))
    work.mkdir(parents=True, exist_ok=True)
    digest = file_hash(source)
    inputs = {'source': digest, 'module': file_hash(module), 'inspect': inspect,
              'expected': expected, 'floorLevels': file_hash(floor_levels) if floor_levels else None,
              'policy': file_hash(skill / 'assets/plan-policy/rule-catalog.tsv'),
              'entry': file_hash(Path(__file__)),
              'host': file_hash(Path(__file__).with_name('owned_plan_host.py'))}
    checkpoint = work / 'identity.json'
    if checkpoint.exists():
        require(read_json(checkpoint) == inputs, '墙高来源或程序变化，旧阶段不可复用')
    else:
        write_json(checkpoint, inputs)
    sheets = work / 'sheets.tsv'
    sheets.write_text('PLANJOINT1 ' + digest + '\nEND\n', encoding='utf-8')
    config = {'mode': 'wall-height', 'resources': str(skill / 'assets'),
              'wallSheets': str(sheets), 'wallScope': '', 'wallInspect': '1' if inspect else '0',
              'wallSkipPrint': '1'}
    if expected is not None:
        targets = work / 'targets.tsv'
        targets.write_text(frozen_text(expected, digest), encoding='utf-8')
        config['wallTargets'] = str(targets)
    if floor_levels:
        require(Path(floor_levels).read_text('utf-8-sig').splitlines()[0].split() == ['PLANDATUM1', digest],
                '项目层高文件与当前源DWG指纹不一致')
        config['floorLevels'] = str(floor_levels)
    for number in range(1, 4):
        attempt = work / f'attempt-{number:02}'
        receipt_path = attempt / 'job/product-result.json'
        try:
            if not attempt.exists():
                run_owned_plan(workspace, module, source, attempt, config)
            elif (attempt / 'host.json').is_file():
                session = Path(read_json(attempt / 'host.json')['session'])
                if not (session / 'isolation-final.json').exists():
                    resume_owned_plan(workspace, module, attempt, skill / 'assets')
            verify_owned_host_closure(attempt, module=module, workspace=workspace)
        except RetryableHostError:
            continue
        except JobError as error:
            # Binding/identity checks below remain hard failures. A native
            # inability to read/write walls is listed without hiding it.
            raise WallStageFailure(str(error)) from error
        if not receipt_path.is_file():
            continue
        receipt = read_json(receipt_path)
        if not receipt.get('completed'):
            continue
        require(receipt['sourceSHA256'] == digest == file_hash(source), '墙高作业改动了来源或来源已变化')
        if inspect:
            require(receipt.get('readOnly') is True, '墙高读回阶段不是只读')
        else:
            require(receipt.get('dwgReopened') is True and receipt['outputSHA256'] ==
                    file_hash(attempt / 'job/candidate.dwg'), '墙高写入缺少同版重开证明')
        return attempt / 'job'
    raise WallStageFailure('墙高原生阶段重试后仍未完成，原始失败记录已保留')


def reconcile_wall_heights(source, work, workspace, skill, module, *, expected=None, floor_levels=None,
                           repair=True):
    """Inspect final DWG, fix mismatches only, independently reopen after every fix."""
    source, work, workspace, skill, module = (Path(p).resolve() for p in (source, work, workspace, skill, module))
    work.mkdir(parents=True, exist_ok=True)
    current = source
    history = []
    for number in range(MAX_REPAIRS + 1):
        try:
            report = run_stage(workspace, module, current, work / f'read-{number}', skill,
                               inspect=True, expected=expected, floor_levels=floor_levels if number == 0 else None)
        except WallStageFailure as error:
            issues = [{'object': '墙高原生核验', 'role': 'wall-height-execution', 'reason': str(error)}]
            proof = {'contract': CONTRACT, 'savedReopened': False, 'verified': False,
                     'outputSHA256': file_hash(current), 'expected': expected, 'walls': [],
                     'unresolved': issues, 'repairs': history}
            write_json(work / 'storey-wall-height-readback.json', proof)
            return {'drawing': str(current), 'sha256': file_hash(current), 'expected': expected,
                    'proof': str(work / 'storey-wall-height-readback.json'), 'unresolved': issues}
        actual = read_json(report / 'wall-height-preflight.json')
        if expected is None:
            expected = actual
        elif number == 0:
            import copy
            expected = copy.deepcopy(expected)
            baseline = {w['handle']: w for f in actual['floors'] for w in f['walls']}
            for floor in expected['floors']:
                for wall in floor['walls']:
                    if wall['handle'] in baseline:
                        for key in ('left', 'right', 'start', 'end'):
                            if key not in wall and key in baseline[wall['handle']]:
                                wall[key] = baseline[wall['handle']][key]
        rows, issues = compare_walls(expected, actual)
        proof = {'contract': CONTRACT, 'savedReopened': True, 'sourceSHA256': file_hash(source),
                 'outputSHA256': file_hash(current), 'nativeReadback': str(report),
                 'expected': expected, 'walls': rows, 'unresolved': issues, 'repairs': history,
                 'verified': not issues}
        write_json(work / 'storey-wall-height-readback.json', proof)
        # Source conflicts, missing ownership and changed protected fields need
        # new evidence, not repetitions of an unchanged native write.
        fixable = [r for r in rows if r['issue'] == '独立重开实际墙高与目标不符']
        if not repair or not fixable or number == MAX_REPAIRS:
            return {'drawing': str(current), 'sha256': file_hash(current), 'expected': expected,
                    'proof': str(work / 'storey-wall-height-readback.json'), 'unresolved': issues}
        try:
            repaired = run_stage(workspace, module, current, work / f'write-{number+1}', skill,
                                 inspect=False, expected=expected)
        except WallStageFailure as error:
            issues.append({'object': '墙高自动返修', 'role': 'wall-height-execution', 'reason': str(error)})
            proof['unresolved'] = issues
            write_json(work / 'storey-wall-height-readback.json', proof)
            return {'drawing': str(current), 'sha256': file_hash(current), 'expected': expected,
                    'proof': str(work / 'storey-wall-height-readback.json'), 'unresolved': issues}
        candidate = repaired / 'candidate.dwg'
        history.append({'beforeSHA256': file_hash(current), 'afterSHA256': file_hash(candidate),
                        'objects': [row['object'] for row in fixable], 'report': str(repaired)})
        current = candidate
    raise AssertionError('unreachable')


def require_wall_proof(result):
    require(result.get('wallHeightProof') and Path(result['wallHeightProof']).is_file(), '最终交付缺少逐墙高度复核凭据')
    proof = read_json(Path(result['wallHeightProof']))
    require(proof.get('contract') == CONTRACT and
            (proof.get('savedReopened') is True or bool(proof.get('unresolved'))) and
            proof.get('outputSHA256') == result['sha256'] == file_hash(result['drawing']),
            '最终墙高逐墙凭据与交付DWG不一致')
    require(all(row in result['unresolved'] for row in proof['unresolved']), '最终墙高遗留项未进入同版清单')
    return proof
