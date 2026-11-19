"""Run the template-native final pass, retaining retries and hash-bound evidence."""
from pathlib import Path
import argparse
import csv
import json
import math
import re
import sys

from owned_plan_host import (JobError, RetryableHostError, file_hash, read_json,
                             require, run_owned_plan, resume_owned_plan, write_json, verify_owned_host_closure)


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


def paired_area_superscript(row, index):
    """Recognize an actual native square-metre exponent from readback geometry."""
    glyphs = row.get('glyphs', [])
    if (row.get('role') != 'special-area' or row.get('nativeType') != 'TDbText' or
            '/' in row.get('object', '') or not 0 < index < len(glyphs) or
            '面积' not in ''.join(g.get('text', '') for g in glyphs)):
        return False
    body, exponent = glyphs[index - 1:index + 1]
    if (exponent.get('text') != '2' or not re.fullmatch(r'[0-9]+(?:\.[0-9]+)?[mM]', body.get('text', '')) or
            body.get('style') != exponent.get('style')):
        return False
    for glyph in (body, exponent):
        if any(not isinstance(glyph.get(k), (int, float)) or not math.isfinite(glyph[k])
               for k in ('height', 'rotation')):
            return False
        for key, length in (('anchor', 3), ('bounds', 6)):
            values = glyph.get(key)
            if not isinstance(values, list) or len(values) != length or any(
                    not isinstance(v, (int, float)) or not math.isfinite(v) for v in values):
                return False
        if any(glyph['bounds'][i] > glyph['bounds'][i + 3] for i in range(3)):
            return False
    height = body['height']
    if (height <= 0 or abs(exponent['height'] - height * .5) > .001 or
            abs(math.remainder(exponent['rotation'] - body['rotation'], math.tau)) > 1e-8):
        return False
    along = (math.cos(body['rotation']), math.sin(body['rotation']))
    delta = [exponent['anchor'][i] - body['anchor'][i] for i in range(2)]
    rise = -delta[0] * along[1] + delta[1] * along[0]
    if sum(delta[i] * along[i] for i in range(2)) <= 0 or not .1 * height <= rise <= 1.1 * height:
        return False
    def projected(glyph):
        box = glyph['bounds']
        values = [(x - body['anchor'][0]) * along[0] + (y - body['anchor'][1]) * along[1]
                  for x in (box[0], box[3]) for y in (box[1], box[4])]
        return min(values), max(values)
    gap = projected(exponent)[0] - projected(body)[1]
    return -.1 * height <= gap <= .6 * height


def typography_evidence_issues(audit, policy=None):
    issues = []
    styles = {style['name']: style for style in (policy or {}).get('styles', [])}
    for row in audit['objects']:
        # Native metadata can still contain the previous rule's expectation.
        # Recompute against the locked current policy, including failed rows.
        if row.get('verified') is not True:
            issues.append({'object': row['object'], 'role': row['role'],
                           'reason': row.get('reason') or '原生文字读回未通过，不能忽略该对象'})
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
                          'native-opening-label': '_TCH_WINDOW', 'special-area': 'FY_HZ',
                          'special-area-superscript': 'FY_HZ'}.get(row['role'])
        factor = row.get('unitsPerPaperMM')
        if styles and (not isinstance(factor, (int, float)) or not math.isfinite(factor) or factor <= 0):
            issues.append({'object': row['object'], 'role': row['role'],
                           'reason': '缺少实际图幅单位与比例换算依据，不能用生成器期望字高代替复核'})
            continue
        for index, glyph in enumerate(glyphs):
            spec = styles.get(expected_style or glyph['style'])
            if spec is None:
                continue
            font = Path(glyph.get('font', '').replace('\\', '/')).name.lower()
            big = Path(glyph.get('bigFont', '').replace('\\', '/')).name.lower()
            paper_height = spec['heightMM'] * (.5 if paired_area_superscript(row, index) else 1)
            if (glyph['style'] != spec['name'] or font != spec['font'].lower() or
                    big != spec['bigFont'].lower() or abs(glyph['width'] - spec['width']) > .01 or
                    not isinstance(glyph.get('styleWidth'), (int, float)) or not math.isfinite(glyph['styleWidth']) or
                    abs(glyph['styleWidth'] - spec['width']) > .01 or
                    not isinstance(glyph.get('fixedHeight'), (int, float)) or not math.isfinite(glyph['fixedHeight']) or
                    abs(glyph['fixedHeight'] - spec.get('fixedHeight', 0)) > .001 or
                    glyph.get('oblique') != 0 or glyph.get('flags') != 0 or
                    glyph.get('annotative') is not False or glyph.get('vertical') is not False or
                    abs(glyph['height'] / factor - paper_height) > .001):
                issues.append({'object': row['object'], 'role': row['role'],
                               'reason': '实际文字引用、字体、有效字高或宽度与当前三套强制参数不符'})
                break
    return issues


def omission_coverage_issues(audit):
    """Enumeration and zero recognized residuals do not prove decoded coverage."""
    unknown = {'D23': set(), 'S01': set()}
    exclusions = audit.get('exclusions')
    enumerated, evaluated = audit.get('enumeratedAnnotationRows'), audit.get('evaluatedAnnotationRows')
    valid = (isinstance(exclusions, list) and type(enumerated) is int and type(evaluated) is int and
             0 <= evaluated <= enumerated and audit.get('scannedObjects') == evaluated)
    if not valid:
        for roots in unknown.values():
            roots.add('missing-or-invalid-coverage-ledger')
        exclusions = []
    for row in exclusions:
        structured = (isinstance(row, dict) and all(isinstance(row.get(key), str) and row[key]
                      for key in ('object', 'root', 'category', 'reason')) and
                      isinstance(row.get('rules'), list) and row['rules'] and
                      all(rule in unknown for rule in row['rules']) and type(row.get('coverageKnown')) is bool and
                      row.get('writeProtected') is True and
                      all(isinstance(row.get(key), list) and all(isinstance(path, str) and path for path in row[key])
                          for key in ('readAnnotationPaths', 'unreadPaths')))
        if not structured:
            for roots in unknown.values():
                roots.add('malformed-excluded-domain')
            continue
        if row['coverageKnown'] is not True or row['unreadPaths']:
            for rule in row['rules']:
                unknown[rule].add(row['root'])
    declared = audit.get('ruleCoverage', {})
    for rule, roots in unknown.items():
        if not isinstance(declared, dict) or not isinstance(declared.get(rule), dict) or declared[rule].get('coverageKnown') is not True:
            if not roots:
                roots.add('native-coverage-not-established')
    return [{'object': 'coverage/' + rule, 'role': 'omission-coverage-' + rule,
             'reason': f'本规则有 {len(roots)} 个未完整读取或未证明覆盖的实例域；家具原对象保留，已读标注仍核对，未计为全清；详见 exclusions'}
            for rule, roots in unknown.items() if roots]


def title_paper_evidence_issues(audit, digest, pages):
    """Paper title leaves are separate from model-space table coverage."""
    def issue(obj, reason, role='title-paper-coverage'):
        return {'object': obj, 'role': role, 'reason': reason}
    if (not isinstance(audit, dict) or audit.get('contract') != 'plan.title-paper-bounds.v1' or
            audit.get('outputSHA256') != digest or audit.get('savedReopened') is not True or
            not isinstance(audit.get('layouts'), list) or not isinstance(audit.get('objects'), list)):
        return [issue('TITLE-PAPER', '缺少同版独立重开的纸空间图签文字纸边凭据，模型表格或字体通过不能代替')]
    issues = []
    expected = {(p.get('panel'), p.get('layout')) for p in pages}
    actual = {(p.get('panel'), p.get('layout')) for p in audit['layouts']}
    if not expected or expected != actual or len(audit['layouts']) != len(actual):
        issues.append(issue('TITLE-PAPER', '纸空间图签纸边缺幅或布局重复'))
    layout_reasons = {}
    layouts = {(p.get('panel'), p.get('layout')): p for p in audit['layouts']}
    printed = {(p.get('panel'), p.get('layout')): p for p in pages}
    for key, layout in layouts.items():
        if layout.get('reason'):
            layout_reasons[key] = layout['reason']
        saved = layout.get('paperCoordinates'); effective = printed.get(key, {}).get('paperCoordinates')
        # Actual saved and effective print coordinates must both be present.
        if saved is None or effective is None:
            layout_reasons[key] = '缺少保存布局或本次实打的实际纸面坐标凭据'
        if saved is not None or effective is not None:
            try:
                def signature(settings):
                    custom = settings['customScale']
                    scale = settings['standardScale'] if settings['useStandardScale'] else custom[0] / custom[1]
                    return [settings['units'], settings['rotation'], settings['plotType'], settings['centered'], scale,
                            *settings['paperSize'], *settings['window'], *settings['margins'],
                            *([] if settings['centered'] else settings['origin'])]
                a, b = signature(saved), signature(effective)
                if len(a) != len(b) or any(not isinstance(x, (int, float)) or not math.isfinite(x) or
                        not isinstance(y, (int, float)) or not math.isfinite(y) or abs(x-y) > 1e-6 for x, y in zip(a, b)):
                    raise ValueError('coordinate mismatch')
            except (TypeError, KeyError, ValueError, ZeroDivisionError, IndexError):
                layout_reasons[key] = '保存布局与本次实打的有效比例、纸型、旋转、原点或窗口证据不一致'
    counts, empty_counts = {}, {}
    for row in audit['objects']:
        key = row.get('panel'), row.get('layout'); counts[key] = counts.get(key, 0) + 1
        if key not in expected:
            issues.append(issue(row.get('object', 'TITLE-PAPER'), '图签叶片不属于已实打的布局'))
            continue
        if row.get('noInk') is True and row.get('applicable') is False and row.get('text') == '':
            empty_counts[key] = empty_counts.get(key, 0) + 1
            continue
        if key in layout_reasons:
            continue
        boxes = [row.get('paperBounds'), row.get('bounds')]
        valid = all(isinstance(box, list) and len(box) == 4 and all(
            isinstance(v, (int, float)) and math.isfinite(v) for v in box) and
            box[0] <= box[2] and box[1] <= box[3] for box in boxes)
        if not valid:
            layout_reasons[key] = '非空图签文字或实际纸边包络不可读；对象见原生逐叶记录'
            continue
        paper, box = boxes
        excess = [max(0., paper[0] - box[0]), max(0., box[2] - paper[2]),
                  max(0., paper[1] - box[1]), max(0., box[3] - paper[3])]
        if max(excess) > .01:
            issues.append(issue(row['object'], f"图签文字超纸边；布局={row['layout']}；左/右/下/上(mm)=" +
                                '/'.join(f'{v:.3f}' for v in excess), 'title-paper-bounds'))
        elif row.get('verified') is not True:
            issues.append(issue(row['object'], row.get('reason') or '原生图签纸边读回未通过', 'title-paper-bounds'))
    for layout in audit['layouts']:
        key = layout.get('panel'), layout.get('layout')
        if type(layout.get('textLeafCount')) is not int or layout['textLeafCount'] <= 0 or counts.get(key, 0) != layout['textLeafCount']:
            layout_reasons[key] = '实际图签文字叶片数量不完整，未计为纸边覆盖通过'
        if 'emptyLeafCount' in layout and (layout['emptyLeafCount'] != empty_counts.get(key, 0) or
                layout.get('inkLeafCount') != counts.get(key, 0)-empty_counts.get(key, 0)):
            layout_reasons[key] = '空属性与实际有墨迹文字数量不一致'
        if key in layout_reasons:
            issues.append(issue(f"layout:{layout.get('panel')}", layout_reasons[key]))
    if audit.get('expectedCoverageKnown') is not True and not layout_reasons:
        issues.append(issue('TITLE-PAPER', '原生纸空间图签覆盖仍未知，未计为通过'))
    return issues


def omission_evidence_issues(audit, digest):
    require(audit.get('contract') == 'plan.final-omissions.v1' and
            audit.get('outputSHA256') == digest and audit.get('savedReopened') is True and
            audit.get('independentInventory') is True and audit.get('modelEnumerated') is True and
            type(audit.get('layoutsEnumerated')) is int and audit['layoutsEnumerated'] >= 0 and
            type(audit.get('scannedObjects')) is int and audit['scannedObjects'] >= 0,
            '缺少同版独立重开后的全图室内尺寸／房间面积扫描，不能用已删除句柄清单代替')
    require(isinstance(audit.get('sourceCandidates'), list) and isinstance(audit.get('residuals'), list),
            '禁用标注复核缺少源应有清单或最终残留清单')
    for row in audit['sourceCandidates'] + audit['residuals']:
        require(isinstance(row, dict) and all(isinstance(row.get(k), str) and row[k]
                for k in ('object', 'role', 'reason')), '禁用标注复核存在无对象或无原因的结果')
    issues = list(audit['residuals'])
    for row in omission_coverage_issues(audit):
        if not any(old['object'] == row['object'] and old['role'] == row['role'] for old in issues):
            issues.append(row)
    return issues


def risk_glyph_evidence_issues(audit, typography, digest):
    require(audit.get('contract') == 'plan.risk-glyph-contours.v1' and
            audit.get('outputSHA256') == digest and audit.get('savedReopened') is True and
            isinstance(audit.get('objects'), list), '风险字符没有同版独立重开的实际轮廓凭据')
    expected = {(row['object'], character, glyph.get('style'))
                for row in typography['objects'] for glyph in row.get('glyphs', [])
                for character in glyph.get('text', '')
                if character in '²³' or 0x3300 <= ord(character) <= 0x33ff}
    records = {(row.get('object'), row.get('character'), row.get('style')): row for row in audit['objects']}
    issues = []
    for key in expected:
        row = records.get(key)
        shape = row.get('unitContour') if row else None
        placeholders = row.get('placeholderContours') if row else None
        valid = (row and not row.get('reason') and isinstance(shape, str) and shape and
                 type(row.get('contours')) is int and row['contours'] > 0 and
                 isinstance(placeholders, list) and len(placeholders) == 3 and
                 all(isinstance(value, str) for value in placeholders) and shape not in placeholders and
                 isinstance(row.get('primarySHA256'), str) and len(row['primarySHA256']) == 64)
        if not valid:
            issues.append({'object': key[0], 'role': 'risk-glyph',
                           'reason': f'字符 {key[1]} 缺少可核轮廓、显示为占位符或缺少实际字体资源；不能仅凭字体名称判通过'})
    return issues


def unit_glyph_repair_evidence_issues(audit, risk, typography, digest):
    """Independently constrain the native edit to the exact equivalent unit."""
    def issue(obj, reason):
        return {'object': obj, 'role': 'unit-glyph-repair-readback', 'reason': reason}
    if (not isinstance(audit, dict) or audit.get('contract') != 'plan.unit-glyph-repairs.v1' or
            audit.get('outputSHA256') != digest or audit.get('savedReopened') is not True or
            not isinstance(audit.get('objects'), list)):
        return [issue('单位字形替换', '缺少同版独立重开的单位等价替换凭据，不能计为通过')]
    issues, seen = [], set()
    def sha(value):
        return isinstance(value, str) and len(value) == 64 and all(c in '0123456789abcdefABCDEF' for c in value)
    def font(value):
        return Path(value.replace('\\', '/')).name.lower() if isinstance(value, str) else None
    for row in audit['objects']:
        if not isinstance(row, dict):
            issues.append(issue('单位字形替换', '单位替换记录不可读，不能计为通过'))
            continue
        obj = row.get('object')
        before, after, actual = (row.get(key) for key in ('before', 'after', 'actual'))
        text_rows = [text for text in typography.get('objects', []) if text.get('object') == obj]
        glyphs = text_rows[0].get('glyphs', []) if len(text_rows) == 1 else []
        expected_state = row.get('expectedProtectedParameters')
        body_ok = (isinstance(obj, str) and obj and obj not in seen and
                   isinstance(before, str) and '建筑面积' in before and '㎡' in before and
                   before.replace('㎡', 'm²') == after == actual and
                   glyphs and ''.join(glyph.get('text', '') for glyph in glyphs) == actual)
        if isinstance(obj, str):
            seen.add(obj)
        state_ok = (row.get('verified') is True and not row.get('reason') and
                    isinstance(expected_state, str) and expected_state and
                    row.get('actualProtectedParameters') == expected_state)
        fonts_ok = (isinstance(row.get('style'), str) and row['style'] and
                    isinstance(row.get('primary'), str) and row['primary'] and sha(row.get('primarySHA256')) and
                    isinstance(row.get('big'), str) and (not row['big'] or sha(row.get('bigSHA256'))) and
                    all(glyph.get('style') == row['style'] and font(glyph.get('font')) == font(row['primary']) and
                        font(glyph.get('bigFont', '')) == font(row['big']) for glyph in glyphs))
        shapes = [row.get(key) for key in ('originalPlaceholderContour', 'replacementMContour', 'replacementSquaredContour')]
        shapes_ok = (all(isinstance(shape, str) and shape for shape in shapes) and
                     shapes[0] not in shapes[1:])
        squared = [value for value in risk.get('objects', []) if value.get('object') == obj and
                   value.get('character') == '²' and value.get('style') == row.get('style')]
        risk_ok = (risk.get('contract') == 'plan.risk-glyph-contours.v1' and risk.get('outputSHA256') == digest and
                   risk.get('savedReopened') is True and len(squared) == 1 and
                   not risk_glyph_evidence_issues(risk, {'objects': text_rows}, digest) and
                   squared[0].get('unitContour') == row.get('replacementSquaredContour') and
                   squared[0].get('primarySHA256') == row.get('primarySHA256') and
                   squared[0].get('bigSHA256') == row.get('bigSHA256'))
        if not (body_ok and state_ok and fonts_ok and shapes_ok and risk_ok):
            issues.append(issue(obj if isinstance(obj, str) and obj else '单位字形替换',
                                '单位替换未独立核实仅㎡→m²、原数值正文及锚点样式不变、实际字体来源和最终²轮廓；未计为通过'))
    return issues


def font_dependency_evidence_issues(audit, digest):
    require(audit.get('contract') == 'plan.font-dependencies.v1' and
            audit.get('outputSHA256') == digest and audit.get('savedReopened') is True and
            audit.get('allStylesEnumerated') is True and
            all(isinstance(audit.get(key), list) for key in ('retired', 'unresolved', 'issues')),
            '缺少同版独立重开的全样式字体依赖检查')
    issues = list(audit['issues'])
    for row in audit['retired']:
        if row.get('absent') is not True:
            issues.append({'object': row['handle'], 'role': 'font-dependency',
                           'reason': '缺字体旧样式清理后仍存在，不能计为已清理'})
    for row in audit['unresolved']:
        issues.append({'object': row['handle'], 'role': 'font-dependency',
                       'reason': f"样式 {row['name']} 的实际字体资源仍缺失：{row.get('primary', '')} / {row.get('big', '')}"})
    for row in issues:
        require(isinstance(row, dict) and all(isinstance(row.get(key), str) and row[key]
                for key in ('object', 'role', 'reason')), '缺字体问题必须保留对象、类别和原因')
    return issues


def final_rule_evidence(digest, native, text, omissions, axis, issues, *, policy_digest, printed_pages):
    """Summarize checked scope; catalog registration is never counted as proof."""
    def make(rule, count, verified, relevant, known, scope, files):
        distinct = {(x['object'], x['role'], x['reason']) for x in relevant}
        return {'rule': rule, 'coverage': {'objectCount': count, 'scope': scope},
                'verifiedCount': verified, 'issueCount': len(distinct),
                'expectedCoverageKnown': bool(known),
                'passed': bool(known and not distinct and count == verified), 'evidence': files}
    rows = []
    rows.append(make('G09', 1, int(text.get('policySHA256') == policy_digest), [],
                     text.get('savedReopened') is True and native.get('savedReopened') is True,
                     'locked current typography policy and saved final template execution; source/module lock is in template-job.json',
                     ['template-native-readback.json', 'template-typography-readback.json']))
    for rule, roles in [('N06', {'general'}), ('L08', {'opening-label', 'native-opening-label'})]:
        actual = [row for row in text['objects'] if row['role'] in roles]
        objects = {row['object'] for row in actual}
        relevant = [row for row in issues if row['object'] in objects]
        bad = {row['object'] for row in relevant}
        verified = sum(row.get('verified') is True and row['object'] not in bad for row in actual)
        rows.append(make(rule, len(actual), verified, relevant, bool(actual),
                         'final enumerated native objects and actual rendered leaves in this typography role',
                         ['template-typography-readback.json', 'plan-risk-glyph-readback.json']))
    dimensions = [row for row in native['objects'] if row['kind'] == 'dimension']
    dimension_objects = {row['source'] for row in dimensions} | {h for row in dimensions for h in row['targets']}
    relevant = [row for row in issues if row['object'] in dimension_objects or row['role'] == 'dimension']
    bad = {row['object'] for row in relevant}
    verified = sum(row.get('verified') is True and not ({row['source']} | set(row['targets'])) & bad for row in dimensions)
    rows.append(make('Y10', len(dimensions), verified, relevant, bool(dimensions),
                     'template-rebuilt native chains, each actual segment signature; unrecognized dimensions are not implied covered',
                     ['template-native-readback.json', 'template-typography-readback.json']))
    coverage = omission_coverage_issues(omissions)
    for rule in ('D23', 'S01'):
        unknown = [row for row in [*omissions['residuals'], *issues, *coverage]
                   if row['role'] in ('omission-coverage', 'omission-coverage-' + rule)]
        source = {row['object'] for row in omissions['sourceCandidates'] if row['role'] == rule}
        residual = [row for row in omissions['residuals'] if row['role'] == rule]
        residual_objects = {row['object'] for row in residual}
        row = make(rule, len(source), len(source - residual_objects), residual + unknown,
                   omissions.get('independentInventory') is True and omissions['scannedObjects'] > 0 and not unknown,
                   'source candidates plus independent full final inventory within adopted plan windows; unknown scope is unresolved',
                   ['plan-final-omissions-readback.json'])
        row['coverage']['finalInventoryObjects'] = omissions['scannedObjects']
        row['coverage']['enumeratedAnnotationRows'] = omissions.get('enumeratedAnnotationRows')
        row['coverage']['evaluatedAnnotationRows'] = omissions.get('evaluatedAnnotationRows')
        row['coverage']['excludedInstanceCount'] = len({item['root'] for item in (omissions.get('exclusions') or [])
            if isinstance(item, dict) and isinstance(item.get('root'), str) and rule in (item.get('rules') or [])})
        rows.append(row)
    ends = axis.get('ends', [])
    relevant = [row for row in issues if row['role'] in ('axis-expected', 'axis-fold', 'axis', 'axis-template-provenance')]
    rows.append(make('A28', len(ends), sum(row.get('verified') is True for row in ends), relevant,
                     axis.get('expectedCoverageKnown') is True,
                     'frozen expected axis ends, actual connectors and radial ink on saved drawing',
                     ['axis-expected-readback.json', 'template-native-readback.json']))
    p06 = make('P06', printed_pages + 1, printed_pages + 1, [], False,
               'saved final DWG and one plot per expected sheet; delivery/checklist publication has not run yet',
               ['product-result.json', 'formal-layout-print-review.json'])
    p06['pendingPublication'] = True
    p06['strictUnresolvedCount'] = len({(r['object'], r['role'], r['reason']) for r in issues})
    rows.append(p06)
    return {'contract': 'plan.final-rule-evidence.v1', 'outputSHA256': digest,
            'policySHA256': policy_digest, 'allCatalogRulesImplemented': False, 'rules': rows}


def publication_coverage(evidence, digest):
    """A delivered DWG is distinct from complete, passing rule coverage."""
    require(evidence.get('contract') == 'plan.final-rule-evidence.v1' and
            evidence.get('outputSHA256') == digest and isinstance(evidence.get('rules'), list),
            '发布缺少同版逐规则覆盖凭据')
    applicable = [row for row in evidence['rules'] if row.get('applicability') != 'not-applicable']
    unknown = [row for row in applicable if row.get('expectedCoverageKnown') is not True]
    checked = [row for row in applicable if row.get('expectedCoverageKnown') is True]
    checked_passed = bool(checked) and all(row.get('passed') is True for row in checked)
    complete = bool(applicable) and not unknown and evidence.get('allCatalogRulesImplemented') is True
    return {'checkedRulesPassed': checked_passed, 'coverageComplete': complete,
            'allRulesPassed': complete and checked_passed,
            'unknownCoverageRules': [row['rule'] for row in unknown]}


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
            require(row['targets'] and not row['reason'], '通过对象缺少目标或仍有未解决原因')
            if (row['donor'], 'TDbDimension2' if row['kind'] == 'dimension' else 'TDbAxisLabelSet') not in donors:
                unresolved.append({'object': row['source'], 'role': row['kind'] + '-template-provenance',
                                   'reason': '独立读回缺少签名模板中的具体原生样件句柄；泛来源标记不能作为复制来源通过依据'})
        else:
            require(row['reason'] and any(x['object'] == row['source'] and x['reason'] == row['reason'] for x in unresolved),
                    '未通过对象没有进入同版清单')
    text_audit = read_json(report / 'template-typography-readback.json')
    require(text_audit.get('outputSHA256') == digest and text_audit.get('savedReopened') is True,
            '字体复核没有绑定最终重开DWG')
    require(text_audit.get('policySHA256') == file_hash(resources / 'annotation-template/standard.json'),
            '最终原生字体读回与当前统一参数契约版本不符')
    for row in text_audit['objects']:
        if not row['verified']:
            require(any(x['object'] == row['object'] and x['role'] == row['role'] for x in unresolved),
                    '字体未通过对象没有进入同版清单')
    typography_issues = typography_evidence_issues(text_audit, standard.get('finalTypographyPolicy'))
    unresolved.extend(typography_issues)
    omission_audit = read_json(report / 'plan-final-omissions-readback.json')
    omission_issues = omission_evidence_issues(omission_audit, digest)
    unresolved.extend(row for row in omission_issues if row not in unresolved)
    risk_audit = read_json(report / 'plan-risk-glyph-readback.json')
    glyph_issues = risk_glyph_evidence_issues(risk_audit, text_audit, digest)
    unresolved.extend(row for row in glyph_issues if row not in unresolved)
    unit_path = report / 'plan-unit-glyph-repair-readback.json'
    unit_issues = unit_glyph_repair_evidence_issues(read_json(unit_path) if unit_path.is_file() else None,
                                                 risk_audit, text_audit, digest)
    unresolved.extend(row for row in unit_issues if row not in unresolved)
    dependency_issues = font_dependency_evidence_issues(read_json(report / 'plan-font-dependencies-readback.json'), digest)
    unresolved.extend(row for row in dependency_issues if row not in unresolved)
    verify_owned_host_closure(attempt)
    ink = read_json(report / 'typography-paper-input.json')
    require(ink.get('drawingSHA256') == digest, '短尺寸实打输入不是最终DWG')
    coverage_issues = missing_paper_segments(audit, ink)
    unresolved.extend(coverage_issues)
    write_json(report / 'template-paper-coverage.json', {'outputSHA256': digest, 'unresolved': coverage_issues})
    plots = read_json(report / 'formal-layout-print-review.json')
    require(plots.get('outputSHA256') == digest and plots.get('pages'), '缺少最终同版整套实打')
    title_path = report / 'plan-title-paper-readback.json'
    title_issues = title_paper_evidence_issues(read_json(title_path) if title_path.is_file() else None, digest, plots['pages'])
    unresolved.extend(row for row in title_issues if not any(
        old.get('object') == row['object'] and old.get('role') == row['role'] for old in unresolved))
    if sheets is not None:
        panels = verify_final_panel_coverage(sheets, file_hash(source), plots['pages'], ink['frames'])
        write_json(report / 'template-final-checks.json', {
            'outputSHA256': digest,
            'ruleCatalogSHA256': file_hash(resources / 'plan-policy/rule-catalog.tsv'),
            'expectedPanels': panels,
            'typographyEvidenceIssues': typography_issues,
            'omissionEvidenceIssues': omission_issues,
            'riskGlyphEvidenceIssues': glyph_issues,
            'unitGlyphRepairEvidenceIssues': unit_issues,
            'fontDependencyEvidenceIssues': dependency_issues,
            'titlePaperEvidenceIssues': title_issues,
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
    write_json(report / 'plan-final-rule-evidence.json', final_rule_evidence(
        digest, audit, text_audit, omission_audit, read_json(report / 'axis-expected-readback.json'), unresolved,
        policy_digest=file_hash(resources / 'annotation-template/standard.json'), printed_pages=len(plots['pages'])))
    return {'drawing': str(drawing), 'sha256': digest, 'report': str(report), 'unresolved': unresolved,
            'formal': not unresolved}


def _execute_template_revision(source, sheets, work, workspace, skill, *, module=None, auto_repair=True, frame_datums=None, wall_expected=None):
    source, sheets, work, workspace, skill = (Path(p).resolve() for p in (source, sheets, work, workspace, skill))
    module = module or skill / 'tools/PlanNative.arx'
    resources = skill / 'assets'
    config = {'mode': 'template-finalize', 'resources': str(resources), 'typographySheets': str(sheets)}
    identity = {'source': file_hash(source), 'sheets': file_hash(sheets), 'module': file_hash(module),
                'template': file_hash(resources / 'annotation-template/平面标注.dwg'),
                'plotters': file_hash(resources / 'plotters/manifest.json'),
                'standard': file_hash(resources / 'annotation-template/standard.json'),
                'typographyExecution': file_hash(resources / 'annotation-template/typography.tsv'),
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
                require(file_hash(resources / 'annotation-template/standard.json') == identity['standard'] and
                        file_hash(resources / 'annotation-template/typography.tsv') == identity['typographyExecution'] and
                        file_hash(resources / 'plan-policy/rule-catalog.tsv') == identity['ruleCatalog'] and
                        file_hash(resources / 'plan-policy/all-plan-rules.json') == identity['planRules'],
                        '作业期间规则或执行参数发生变化，不能沿用本轮终核结果')
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
                                                            workspace, skill, module=module, auto_repair=False, wall_expected=wall_expected)
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


def execute_template_revision(source, sheets, work, workspace, skill, *, module=None, auto_repair=True,
                              frame_datums=None, wall_expected=None, floor_levels=None):
    """All formal routes reconcile native wall properties before plotting and after finalization."""
    from wall_height_pipeline import reconcile_wall_heights, rebind, require_wall_proof
    source, sheets, work, workspace, skill = (Path(p).resolve() for p in (source, sheets, work, workspace, skill))
    module = Path(module or skill / 'tools/PlanNative.arx').resolve()
    import hashlib
    identity = {'source': file_hash(source), 'sheets': file_hash(sheets), 'module': file_hash(module),
                'expected': wall_expected, 'levels': file_hash(floor_levels) if floor_levels else None,
                'wallPipeline': file_hash(Path(__file__).with_name('wall_height_pipeline.py')),
                'rules': file_hash(skill / 'assets/plan-policy/rule-catalog.tsv')}
    version = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()[:24]
    work = work / ('walls-' + version)
    work.mkdir(parents=True, exist_ok=True)
    before = reconcile_wall_heights(source, work / 'before', workspace, skill, module,
                                    expected=wall_expected, floor_levels=floor_levels)
    current = Path(before['drawing'])
    expected = before['expected']
    active_sheets = sheets
    active_frames = frame_datums
    if file_hash(current) != file_hash(source):
        active_sheets = rebind(sheets, file_hash(source), current, work / 'sheets.tsv')
        if frame_datums:
            active_frames = rebind(frame_datums, file_hash(source), current, work / 'frames.tsv')
    for number in range(2):
        result = _execute_template_revision(current, active_sheets, work / f'template-{number}', workspace, skill,
                                            module=module, auto_repair=auto_repair,
                                            frame_datums=active_frames, wall_expected=expected)
        final = reconcile_wall_heights(Path(result['drawing']), work / f'final-{number}', workspace, skill, module,
                                       expected=expected, repair=number == 0)
        if final['sha256'] == result['sha256']:
            result['wallHeightProof'] = final['proof']
            result['unresolved'].extend(row for row in final['unresolved'] if row not in result['unresolved'])
            result['formal'] = result['formal'] and not final['unresolved']
            require_wall_proof(result)
            write_json(Path(result['report']) / 'template-delivery-verification.json', result)
            return result
        # A post-processing mismatch was repaired. Print the repaired version,
        # then read back all walls again; never reuse an earlier PDF hash.
        binding = file_hash(current)
        current = Path(final['drawing'])
        active_sheets = rebind(active_sheets, binding, current, work / 'repaired-sheets.tsv')
        if active_frames:
            active_frames = rebind(active_frames, binding, current, work / 'repaired-frames.tsv')
    raise JobError('墙高最终返修未形成同版成图')


def publish_template_result(result, output, *, prior_checklist=None):
    from unattended_plan import publish
    from wall_height_pipeline import require_wall_proof
    wall_proof = require_wall_proof(result)
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
    verified_text = set()
    text_path = report / 'template-typography-readback.json'
    standard_path = Path(__file__).resolve().parents[1] / 'assets/annotation-template/standard.json'
    if text_path.is_file() and standard_path.is_file():
        text_proof = read_json(text_path)
        if (text_proof.get('savedReopened') is True and text_proof.get('outputSHA256') == digest and
                text_proof.get('policySHA256') == file_hash(standard_path)):
            current_issues = typography_evidence_issues(text_proof, read_json(standard_path).get('finalTypographyPolicy'))
            bad_objects = {row['object'] for row in current_issues + result['unresolved']}
            verified_text = {row['object'] for row in text_proof['objects']
                             if row.get('verified') is True and row['object'] not in bad_objects}
    removed_prohibited = set()
    omission_path = report / 'plan-final-omissions-readback.json'
    if omission_path.is_file():
        omission_proof = read_json(omission_path)
        residuals = omission_evidence_issues(omission_proof, digest)
        blocked = {row['object'] for row in residuals + result['unresolved']}
        removed_prohibited = {(row['object'], row['role']) for row in omission_proof['sourceCandidates']
                              if row.get('presentAfterReopen') is False and row['object'] not in blocked}
    verified_walls = {row['object'] for row in wall_proof.get('walls', []) if not row['issue']}
    if prior_checklist and prior_checklist.is_file():
        with prior_checklist.open(encoding='utf-8-sig', newline='') as stream:
            for old in csv.DictReader(stream):
                handle = old.get('源对象', old.get('对象', ''))
                reason = old.get('问题与原因') or old.get('原因') or old.get('问题') or ''
                category = old.get('类别') or old.get('实际部位') or ''
                if '墙高' in category and handle in verified_walls:
                    closed_history.append({'priorObject': handle, 'basis': 'final independent native wall height readback'})
                    continue
                final_handle = handle.split('/')[0]
                visited = set()
                while final_handle in replacements and final_handle not in visited:
                    visited.add(final_handle)
                    final_handle = replacements[final_handle]
                font_issue = (any(term in category + reason for term in ('字体', '字高', '字宽', '文字样式', 'N06', 'L08', 'Y10')) and
                              not any(term in category + reason for term in
                                      ('碰撞', '压线', '净空', '遮挡', '重叠', '位置', '错号', '测值', '测点', '裁切', '面积', '轴线', '轴号')))
                if font_issue and (handle in verified_text or final_handle in verified_text):
                    closed_history.append({'priorObject': handle, 'finalObject': final_handle,
                                           'basis': 'same-object current-policy actual glyph readback; no final object issue'})
                    continue
                omission_rule = ('D23' if '室内尺寸' in category + reason or 'D23' in category else
                                 'S01' if '房间面积' in category + reason or 'S01' in category else None)
                if omission_rule and (handle, omission_rule) in removed_prohibited:
                    closed_history.append({'priorObject': handle, 'rule': omission_rule,
                                           'basis': 'same source object independently confirmed absent in final DWG; no final residual'})
                    continue
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
    evidence_path = report / 'plan-final-rule-evidence.json'
    evidence = (read_json(evidence_path) if evidence_path.is_file() else
                {'contract': 'plan.final-rule-evidence.v1', 'outputSHA256': digest,
                 'allCatalogRulesImplemented': False,
                 'rules': [{'rule': 'G09', 'expectedCoverageKnown': False, 'passed': False,
                            'coverage': {'scope': '本轮没有逐规则最终覆盖凭据'}}]})
    require(evidence.get('outputSHA256') == digest, '发布规则凭据不是同版最终DWG')
    # Publication is committed below. This provisional P06 transition is only
    # persisted after publish() returns successfully.
    for rule in evidence['rules']:
        if rule['rule'] == 'P06':
            rule.update(expectedCoverageKnown=True, passed=True, pendingPublication=False)
    quality = publication_coverage(evidence, digest)
    for rule in evidence['rules']:
        if rule.get('applicability') == 'not-applicable':
            continue
        if rule.get('expectedCoverageKnown') is not True:
            rows.append({'对象': rule['rule'], '类别': '规则覆盖未验证',
                         '原因': '应核范围未确定或凭据缺失：' + rule.get('coverage', {}).get('scope', ''),
                         'DWG SHA-256': digest})
        elif rule.get('passed') is not True and not rule.get('issueCount'):
            rows.append({'对象': rule['rule'], '类别': '规则复核未通过',
                         '原因': '已登记检查未全部通过，不能仅因未生成问题行而认定通过', 'DWG SHA-256': digest})
    if evidence.get('allCatalogRulesImplemented') is not True:
        rows.append({'对象': '通用规则执行覆盖', '类别': '全量规则未验证',
                     '原因': '本轮最终读回仅声明以下规则的已核范围：' + '、'.join(r['rule'] for r in evidence['rules']) +
                             '；其余现行规则没有在本凭据中逐项证明，不能宣称全部规则通过', 'DWG SHA-256': digest})
    delivery = {'drawing': drawing, 'sha256': digest}
    if rows:
        checklist = report / 'template-unresolved.csv'
        with checklist.open('w', encoding='utf-8-sig', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=['对象', '类别', '原因', 'DWG SHA-256'])
            writer.writeheader(); writer.writerows(rows)
        delivery.update(checklist=checklist, checklistSHA256=file_hash(checklist))
    publish(delivery, output)
    for rule in evidence['rules']:
        if rule['rule'] != 'P06':
            continue
        rule.update(strictUnresolvedCount=len(rows),
                    delivery={'dwgSHA256': digest, 'checklistSHA256': delivery.get('checklistSHA256')})
        rule['coverage']['scope'] = 'saved and printed final version delivered, with every unresolved item in the same-version checklist'
    write_json(evidence_path, evidence)
    write_json(report / 'publication-quality-status.json', {
        'contract': 'plan.publication-quality.v1', 'outputSHA256': digest,
        'output': str(output), 'processingCompleted': True, 'delivered': True,
        **quality, 'allRulesPassed': quality['allRulesPassed'] and not rows, 'unresolvedCount': len(rows),
        'status': 'delivered-with-exceptions' if rows else 'delivered-passed',
        'checklist': str(output.with_name(output.stem + '_遗留清单.csv')) if rows else None})


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'sheets', 'work', 'workspace-root'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--floor-levels', type=Path, help='与源图绑定的项目楼层高度依据')
    parser.add_argument('--frame-datums', type=Path, help='与输入 DWG 指纹匹配的逐层外围排布基准 TSV')
    args = parser.parse_args(argv)
    skill = Path(__file__).resolve().parents[1]
    print(json.dumps(execute_template_revision(args.source.resolve(strict=True), args.sheets.resolve(strict=True),
          args.work.resolve(), args.workspace_root.resolve(), skill, frame_datums=args.frame_datums, floor_levels=args.floor_levels), ensure_ascii=False))


if __name__ == '__main__':
    main()
