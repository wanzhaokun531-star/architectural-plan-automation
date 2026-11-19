"""Read source-bound opening schedules/SKP instances into the native adoption contract.

Only explicitly adopted, unambiguous rough-opening dimensions are writable.  All
other supplied rows remain visible pending items; no filename or nearest-instance
heuristic silently supplies architectural decisions.
"""
from __future__ import annotations

import argparse
from collections import Counter
import csv
import hashlib
import importlib.util
import json
import math
import re
import shlex
from pathlib import Path


def adoption_bindings(manifest, source=None):
    """Return every immutable evidence input referenced by the native manifest."""
    manifest = Path(manifest).resolve(strict=True)
    lines = manifest.read_text(encoding='utf-8-sig').splitlines()
    header = lines[0].split() if lines else []
    if len(header) != 2 or header[0] != 'PLANDESIGN1' or (source and header[1] != digest(source)):
        raise ValueError('design source table does not bind the current DWG')
    result = {str(manifest): digest(manifest)}
    for line in lines[1:]:
        fields = shlex.split(line, posix=True)
        if fields and fields[0] == 'FILE':
            if len(fields) != 4:
                raise ValueError('invalid design evidence FILE row')
            path = Path(fields[3]).resolve(strict=True)
            if digest(path) != fields[2]:
                raise ValueError('provided design evidence changed: ' + fields[1])
            result[str(path)] = fields[2]
    return result


def final_audit_request(prepared, final_drawing, output):
    """Freeze geometric expectations, preserving failed adoption rows as failures."""
    if prepared.get('schema') != 'plan.native-design-readback.v1' or not prepared.get('savedReopened'):
        raise ValueError('missing independently reopened adopted-opening evidence')
    q = lambda text: json.dumps(str(text), ensure_ascii=False)
    rows = [f'PLANDESIGNAUDIT1 {digest(final_drawing)} {prepared["sourceSHA256"]} {prepared["adoptionSHA256"]} 1']
    for file in prepared['files']:
        if digest(file['path']) != file['sha256']:
            raise ValueError('provided design evidence changed before final readback')
        rows.append(f'FILE {file["key"]} {file["sha256"]} {q(Path(file["path"]).as_posix())}')
    for item in prepared['openings']:
        points = item['expectedPoints']
        if len(points) != 2 or any(len(point) < 2 for point in points):
            raise ValueError('adopted opening lacks its two actual expected locating points')
        numbers = [item['widthMM'], item['heightMM'], *points[0][:2], *points[1][:2]]
        if not all(math.isfinite(float(x)) for x in numbers):
            raise ValueError('nonfinite expected native opening dimensions')
        tokens = [item['nativeNumber'], item['source'], item['locator'], item['basis'],
                  item['anchor'], item['prefix'], item.get('reason', '')]
        rows.append('EXPECT ' + item['object'] + ' ' + ' '.join(format(x, '.17g') for x in numbers) +
                    ' ' + str(int(item['passed'] is True)) + ' ' + ' '.join(map(q, tokens)))
    rows.append('END')
    Path(output).write_text('\n'.join(rows)+'\n', encoding='utf-8')


def final_boundary_replacements(prior, finalized):
    """Follow the already reopened template chain records for independent lengths."""
    histories = []
    for directory in finalized.get('history', [finalized['report']]):
        path = Path(directory) / 'template-native-readback.json'
        if path.is_file():
            histories.append(json.loads(path.read_text('utf-8-sig')))
    issues, records = [], []
    lengths = {row['wall']: row for row in prior.get('boundaryDimensions', [])}
    for wall in prior.get('boundaryWalls', []):
        original = lengths.get(wall, {})
        handle = original.get('dimension', '')
        passed = original.get('passed') is True and bool(handle) and bool(histories)
        for history in histories:
            found = [row for row in history['objects'] if row['source'] == handle and row['kind'] == 'dimension']
            valid = history.get('savedReopened') is True and len(found) == 1 and found[0]['verified'] is True and len(found[0]['targets']) == 1
            if not valid:
                passed = False;break
            handle = found[0]['targets'][0]
        passed = passed and histories[-1].get('outputSHA256') == finalized['sha256']
        records.append({'wall':wall, 'sourceDimension':original.get('dimension'), 'finalDimension':handle,
                        'passed':passed, 'basis':'independent wall-length readback plus every template-native dimension replacement'})
        if not passed:
            issues.append({'object':wall, 'role':'围墙独立尺寸最终复核',
                           'reason':'独立长度缺少生成读回或模板替换到最终同版尺寸的完整对应，未计为通过'})
    return records, issues


def verify_final_sources(prepared_report, finalized, work, workspace, skill, module, native_manifest):
    """Read final native widths/heights/numbers after all template and wall passes."""
    from owned_plan_host import run_owned_plan, resume_owned_plan, read_json, write_json, require, JobError, RetryableHostError
    prior = read_json(Path(prepared_report) / 'design-source-readback.json')
    bindings = adoption_bindings(native_manifest)
    original = [shlex.split(line)[1] for line in Path(native_manifest).read_text('utf-8-sig').splitlines()
                if line.startswith('OPENING ')]
    require(digest(native_manifest) == prior['adoptionSHA256'] and len(original) == len(set(original)) and
            len(prior['openings']) == len(original) and {r['object'] for r in prior['openings']} == set(original),
            '准备图门窗采用回执未覆盖原始应有 OPENING 集合')
    drawing = Path(finalized['drawing'])
    require(digest(drawing) == finalized['sha256'], 'final design-source audit drawing changed')
    boundary_records, boundary_issues = final_boundary_replacements(prior, finalized)
    finalized['unresolved'].extend(row for row in boundary_issues if row not in finalized['unresolved'])
    finalized['formal'] = finalized['formal'] and not boundary_issues
    write_json(Path(finalized['report']) / 'design-boundary-final.json',
               {'outputSHA256':finalized['sha256'], 'boundaries':boundary_records})
    root = Path(work) / ('design-final-' + digest(module)[:12] + '-' + finalized['sha256'][:12])
    root.mkdir(parents=True, exist_ok=True)
    request = root / 'expected.tsv'
    final_audit_request(prior, drawing, request)
    proof_file = None
    failures = []
    for number in range(1, 4):
        require(digest(drawing) == finalized['sha256'] and all(digest(p) == h for p,h in bindings.items()),
                '最终 DWG 或门窗来源资料在复核期间变化')
        attempt = root / f'attempt-{number:02d}'
        try:
            if attempt.exists():
                resume_owned_plan(workspace, module, attempt, Path(skill) / 'assets')
            else:
                run_owned_plan(workspace, module, drawing, attempt,
                               {'mode': 'design-source-audit', 'resources': str(Path(skill) / 'assets'),
                                'designSourceAudit': str(request)})
            receipt = read_json(attempt / 'job/product-result.json')
            require(receipt.get('completed') is True and receipt.get('readOnly') is True and
                    receipt.get('sourceSHA256') == finalized['sha256'], '来源复核执行未正常完成')
            proof_file = attempt / 'job/design-source-readback.json'
            require(proof_file.is_file(), '来源复核缺少实际原生读回')
            break
        except (JobError, OSError, ValueError, KeyError) as error:
            failures.append({'attempt': number, 'reason': str(error)})
            # Resume validates the owned host identity before waiting/closing it.
            # Identity failures are deliberately outside this retry handler.
            host_file = attempt / 'host.json'
            if host_file.is_file():
                try:
                    resume_owned_plan(workspace, module, attempt, Path(skill) / 'assets')
                except RetryableHostError:
                    pass  # A recorded crash stays retryable only after safe closure below.
                session = Path(read_json(host_file)['session'])
                closure = session / 'isolation-final.json'
                require(closure.is_file() and read_json(closure).get('stillRunningOwnedWorker') is False,
                        '来源复核自有宿主未确认安全结束，不能启动重试')
            proof_file = None
            if not isinstance(error, (RetryableHostError, OSError)):
                break
    if proof_file is None:
        issue = {'object': '/'.join(original) or '门窗资料', 'role': 'design-source-execution',
                 'reason': f'最终原生来源复核经 {len(failures)} 次执行未完成，未计为通过：' + failures[-1]['reason']}
        finalized['unresolved'].append(issue);finalized['formal'] = False
        write_json(root / 'execution-failures.json', {'outputSHA256': finalized['sha256'], 'attempts': failures})
        write_json(Path(finalized['report']) / 'template-delivery-verification.json', finalized)
        return None
    proof = read_json(proof_file)
    require(proof.get('outputSHA256') == finalized['sha256'] and proof.get('savedReopened') is True and
            proof.get('adoptionSHA256') == prior['adoptionSHA256'] and
            proof.get('sourceSHA256') == prior['sourceSHA256'] and len(proof['openings']) == len(original) and
            {row['object'] for row in proof['openings']} == {row['object'] for row in prior['openings']},
            'final design-source readback lacks same-version object coverage')
    issues = [{'object': row['object'], 'role': '门窗来源最终复核',
               'reason': row.get('reason') or '最终原生宽高、定位或自身号未与采用资料闭合'}
              for row in proof['openings'] if row['passed'] is not True]
    finalized['unresolved'].extend(row for row in issues if row not in finalized['unresolved'])
    finalized['formal'] = finalized['formal'] and not issues
    finalized['designSourceProof'] = str(proof_file)
    write_json(Path(finalized['report']) / 'template-delivery-verification.json', finalized)
    return proof


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest().upper()


def positive(value):
    value = float(value)
    if not math.isfinite(value) or value <= 0:
        raise ValueError('width/height must be finite positive dimensions')
    return value


def skp_instance_size(geometry, instance_path, width_axis, height_axis):
    """Measure actual transformed geometry on the instance's orthogonal axes."""
    path = list(instance_path)
    instances = [x for x in geometry['instances'] if x['path'] == path]
    if len(instances) != 1 or not instances[0].get('visible'):
        raise ValueError('selected SKP instance absent, duplicated or not visible')
    if width_axis not in (0, 1, 2) or height_axis not in (0, 1, 2) or width_axis == height_axis:
        raise ValueError('distinct explicit local width and height axes are required')
    transform = instances[0]['world_transform_inches']
    vectors = [[transform[4 * axis + j] for j in range(3)] for axis in range(3)]
    lengths = [positive(math.sqrt(sum(x*x for x in v))) for v in vectors]
    axes = [[x/length for x in v] for v, length in zip(vectors, lengths)]
    if any(abs(sum(a*b for a,b in zip(axes[i], axes[j]))) > 1e-7
           for i in range(3) for j in range(i)):
        raise ValueError('sheared SKP instance requires explicit dimensional interpretation')
    if abs(axes[height_axis][2]) < 1-1e-7 or abs(axes[width_axis][2]) > 1e-7:
        raise ValueError('SKP height must be a vertical 3D span; a plan symbol or sloped frame is not a supported opening envelope')
    points = []
    for edge in geometry['edges']:
        if edge.get('visible') and edge['path'][:len(path)] == path:
            points.extend(edge['points_mm'])
    if not points:
        raise ValueError('selected SKP instance has no readable visible edges')
    spans = []
    for axis in (width_axis, height_axis):
        values = [sum(p*q for p,q in zip(point, axes[axis])) for point in points]
        spans.append(positive(max(values)-min(values)))
    return spans


def read_schedule(path):
    if path.suffix.lower() == '.csv':
        with path.open(encoding='utf-8-sig', newline='') as stream:
            return list(csv.DictReader(stream))
    if path.suffix.lower() == '.json':
        data = json.loads(path.read_text(encoding='utf-8-sig'))
        if data.get('schema') != 'plan.opening-schedule.v1':
            raise ValueError('unrecognized opening schedule JSON schema')
        return data['openings']
    raise ValueError('unsupported schedule format; use source-traceable CSV or plan.opening-schedule.v1 JSON')


def build_manifest(source, adoption, output, *, skp_api=None, skp_reader=None):
    source, adoption, output = map(lambda p: Path(p).resolve(), (source, adoption, output))
    config = json.loads(adoption.read_text(encoding='utf-8-sig'))
    if config.get('schema') != 'plan.design-adoptions.v1' or config.get('sourceSHA256', '').upper() != digest(source):
        raise ValueError('adoption manifest does not identify this saved DWG')
    q = lambda value: json.dumps(str(value), ensure_ascii=False)
    rows = ['PLANDESIGN1 ' + digest(source)]
    rows.append(f'FILE manifest {digest(adoption)} {q(adoption.as_posix())}')
    evidence, pending, adopted = {}, [], []
    for key, spec in config.get('sources', {}).items():
        if not key.isascii() or not key.replace('_','').isalnum() or key == 'manifest':
            raise ValueError('unique ASCII source keys are required')
        path = (adoption.parent / spec['path']).resolve(strict=True)
        sha = digest(path)
        if spec.get('sha256', '').upper() != sha:
            raise ValueError(f'{key}: supplied design source fingerprint differs')
        rows.append(f'FILE {key} {sha} {q(path.as_posix())}')
        try:
            if path.suffix.lower() == '.skp':
                if not skp_api or not skp_reader:
                    raise ValueError('SketchUp C API or packaged source reader is unavailable')
                module_spec = importlib.util.spec_from_file_location('plan_skp_source', skp_reader)
                reader = importlib.util.module_from_spec(module_spec)
                module_spec.loader.exec_module(reader)
                evidence[key] = ('skp', reader.read_skp(path, skp_api))
            else:
                evidence[key] = ('schedule', read_schedule(path))
        except (ValueError, OSError, KeyError) as error:
            pending.append((key, f'提供的门窗资料未能读取：{error}'))
    seen, consumed = set(), set()
    opening_counts = Counter(str(item.get('handle', '')).upper() for item in config.get('openings', []))
    for item in config.get('openings', []):
        handle = str(item.get('handle', '')).upper()
        try:
            if opening_counts[handle] != 1:
                raise ValueError('multiple adopted source rows target this opening; resolve the source priority before any mutation')
            if not handle or any(c not in '0123456789ABCDEF' for c in handle) or handle in seen:
                raise ValueError('unique DWG opening handle required')
            seen.add(handle)
            if item.get('decision') != 'adopt' or not item.get('basis'):
                raise ValueError('source adoption or dimensional basis is unresolved')
            if item.get('dimensionKind') != 'rough-opening':
                raise ValueError('frame/clear dimensions cannot be silently used as wall-opening dimensions')
            kind, data = evidence[item['source']]
            if kind == 'skp':
                if item.get('geometryRole') != 'opening-envelope':
                    raise ValueError('SKP frame/trim geometry cannot stand for the rough-opening envelope')
                width, height = skp_instance_size(data, item['instancePath'], item['widthAxis'], item['heightAxis'])
                locator = 'instance:' + '/'.join(map(str,item['instancePath']))
            else:
                matched = [r for r in data if str(r.get('id')) == str(item['record'])]
                if len(matched) != 1:
                    raise ValueError('schedule ID is absent or ambiguous')
                record = matched[0]
                if record.get('dimensionKind') != 'rough-opening':
                    raise ValueError('source schedule does not explicitly give rough-opening dimensions')
                unit = {'mm': 1., 'cm': 10., 'm': 1000.}.get(record.get('unit'))
                if unit is None:
                    raise ValueError('schedule dimension unit is missing')
                width, height = positive(record['width'])*unit, positive(record['height'])*unit
                locator = 'record:' + str(item['record'])
            anchor, prefix = item.get('anchor'), item.get('prefix')
            if anchor not in ('CENTER','START','END') or prefix not in ('M','LM','LC','FMD','DK'):
                raise ValueError('explicit native anchor and semantic opening prefix required')
            rows.append(f'OPENING {handle} {width:.12g} {height:.12g} {anchor} {prefix} {item["source"]} {q(locator)} {q(item["basis"])}')
            adopted.append({'object': handle, 'widthMM': width, 'heightMM': height, 'source': item['source'], 'locator': locator})
            consumed.add((item['source'], locator))
        except (ValueError, KeyError, TypeError) as error:
            pending.append((handle or 'unmatched-opening', str(error)))
    # Supplied rows that were not mapped must not disappear merely because one
    # selected opening was successfully adopted.
    for key, (kind, data) in evidence.items():
        if kind == 'schedule':
            for index, record in enumerate(data):
                locator = 'record:' + str(record.get('id', ''))
                if (key, locator) not in consumed:
                    pending.append((f'{key}/{record.get("id", index+1)}', '提供的门窗表行尚未唯一采用到 DWG 洞口'))
        else:
            inventory = config['sources'][key].get('openingInventory')
            if inventory is None:
                candidates = [row for row in data['instances'] if row.get('visible') and
                    re.search(r'(门|窗|door|window)', ' '.join(str(row.get(n,'')) for n in ('name','definition_name','tag')), re.I)]
                inventory = [row['path'] for row in candidates]
                pending.append((key, 'SU 已读实际几何；尚无完整逐樘语义清单，不能把已匹配实例当作全模型门窗已全部核对'))
            elif not config['sources'][key].get('inventoryBasis'):
                pending.append((key, 'SU 逐樘清单缺少完整性核定依据'))
            for path in inventory:
                locator = 'instance:' + '/'.join(map(str,path))
                if (key, locator) not in consumed:
                    pending.append((key+'/'+locator, 'SU 门窗实例尚未唯一采用到 DWG 洞口'))
    walls = set()
    wall_counts = Counter(str(item.get('handle', '')).upper() for item in config.get('walls', []))
    for item in config.get('walls', []):
        handle = str(item.get('handle','')).upper()
        if wall_counts[handle] != 1:
            pending.append((handle or 'unmatched-wall', '同一墙存在多个用途采用项，未选择其中任何一项'))
            continue
        if not handle or any(c not in '0123456789ABCDEF' for c in handle) or handle in walls:
            raise ValueError('unique source wall handle required')
        walls.add(handle)
        if item.get('role') not in ('MAIN','BOUNDARY') or not item.get('basis'):
            pending.append((handle, '主体/围墙用途或采用依据未确定'))
        else:
            rows.append(f'WALL {handle} {item["role"]} {q(item["basis"])}')
    pending.extend((str(x.get('object','unmatched')),str(x['reason'])) for x in config.get('pending',[]))
    rows.extend(f'PENDING {q(key)} {q(reason)}' for key, reason in pending)
    rows.append('END')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('\n'.join(rows)+'\n', encoding='utf-8')
    receipt = {'schema':'plan.design-source-readback.v1','sourceSHA256':digest(source),
               'manifestSHA256':digest(output),'adopted':adopted,'pending':[{'object':k,'reason':r} for k,r in pending]}
    output.with_suffix('.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2),encoding='utf-8')
    return receipt


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path); parser.add_argument('adoption',type=Path); parser.add_argument('output',type=Path)
    parser.add_argument('--skp-api',type=Path); parser.add_argument('--skp-reader',type=Path)
    args=parser.parse_args()
    print(json.dumps(build_manifest(args.source,args.adoption,args.output,skp_api=args.skp_api,skp_reader=args.skp_reader),ensure_ascii=False))
