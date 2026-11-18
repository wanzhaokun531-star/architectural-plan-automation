"""Generate local plan-rule references or stage verified release resources."""
from pathlib import Path
import hashlib
import json
import math
import re
import shutil

ROOT = Path(__file__).resolve().parents[2]
INSTALLED_SKILLS = Path.home() / '.codex/skills'
SKILL = INSTALLED_SKILLS / 'architectural-annotation-rules'
PLAN_SKILL = ROOT / 'skills/architectural-plan-automation'
PLAN_FILES = ('SKILL.md', 'references/rules.md', 'references/project-parameters.md',
              'references/acceptance.md', 'references/rule-index.md', 'references/native-tools.md',
              'references/template-standard.md')
REVISION = '2026-11-18'
VERSION = '0.2.2'
MASTER = ROOT / 'docs/规则汇编_20261105/建筑施工图平面自动化通用规则_20261105.json'
SPACING = ROOT / 'docs/平面外围尺寸与轴号间距规则_20261105.json'
ENTRY = ROOT / 'docs/平面标注Skill执行规则_20261106.json'
PERIMETER_CONTENT = ROOT / 'docs/平面外围尺寸内容规则_20261105.json'
INTERIOR_DIMENSIONS = ROOT / 'docs/平面文字与室内定位尺寸规则_20261105.json'
ANNOTATION_RULES = {'PD01': 'D01', 'PD09': 'TL11', 'PD11': 'D28', 'PD13': 'D23', 'PD14': 'C01',
                    'PD12': 'Y04', 'PD16': 'D27', 'PD17': 'C20', 'PD18': 'Y10', 'PD19': 'D30',
                    'PA07': 'A12', 'PA11': 'A18', 'PA18': 'A25', 'PA19': 'A26', 'PA20': 'A27', 'PA21': 'A28',
                    'PA22': 'A29', 'PA23': 'A30', 'PA24': 'A14',
                    'PA25': 'A31', 'PA26': 'A32', 'PE01': 'B04', 'PB03': 'D29',
                    'PT02': 'L06', 'PT03': 'N06', 'PT04': 'L08'}
ANNOTATION_INSERT_AFTER = {'PD18': 'PD17', 'PD19': 'PD18', 'PA25': 'PA24', 'PA26': 'PA25',
                           'PT02': 'PT01', 'PT03': 'PT02', 'PT04': 'PT03'}
PLAN_RULE_BINDINGS = {'ruleSource': 'G09', 'sourceAnnotationDispositionRule': 'G06',
                      'axisLeadRule': 'A28', 'areaPartitionRule': 'S03',
                      'perimeterRowsRule': 'D01', 'roofThirdRowRule': 'D27',
                      'perimeterAcceptanceRule': 'QA10', 'standardDoorJambRule': 'TL11',
                      'outerOpeningJambRule': 'D05', 'interiorOpeningJambRule': 'D13',
                      'interiorDimensionPlacementRule': 'D23', 'interiorJointPlacementRule': 'D25',
                      'interiorAcceptanceRule': 'QA09',
                      'floorAndWallHeightRule': 'B04', 'sourceStoreyHeightRule': 'SE02',
                      'columnDimensionsRule': 'D28', 'dimensionAcceptanceRule': 'Q03',
                      'dimensionTextStyleRule': 'Y10', 'sourceDimensionReplacementRule': 'D30',
                      'legacyProjectionDimensionRule': 'D26', 'legacyDimensionDetailRule': 'TL08',
                      'borrowedColumnAxisDimensionRule': 'A15', 'totalAxisDimensionRule': 'D02',
                      'intermediateAxisDimensionRule': 'D03', 'perimeterEndLegRule': 'D04',
                      'railingUpstandPlanRule': 'D29', 'constructionAcceptanceRule': 'Q07',
                      'exteriorStepInventoryRule': 'C01', 'exteriorStepRiseRule': 'C02',
                      'exteriorStepDefaultsRule': 'C20', 'sourceTextPlacementPriorityRule': 'L06',
                      'planTextTypographyRule': 'N06',
                      'openingLabelTypographyRule': 'L08',
                      'architecturalPlanAcceptanceRule': 'P04', 'formalDeliveryRule': 'P06'}
EXECUTION_RULES = tuple(dict.fromkeys((*ANNOTATION_RULES.values(), *PLAN_RULE_BINDINGS.values())))
PRECEDENCE = ['用户最新明确确认', '现行平面主汇编', '历史项目与版本记录按原范围查阅']
ENTRY_AXIS_RULES = {'crossFloorWallAxis': 'A30', 'previouslyUnnumberedAxisReview': 'A31',
                   'crossFloorPreissueVerification': 'A32'}
REVIEWED_AXIS_RULES = ('A18', 'A25', 'A26', 'A27', 'A28')
INTERIOR_PLACEMENT_RULES = ('D13', 'D23', 'D25', 'QA09')
PLACEMENT = {
    'normal': 'WALL_OR_OUTER_COLUMN',
    'confirmedRoof': 'EAVE_IF_INTERSECTS',
    'steps': 'KEEP_AXIS_FOR_STEPS',
}
RETIRED_PLACEMENT_FIELDS = ('datum', 'axisDatumSelection', 'automaticOutwardShiftForProjectionEnvelope',
                            'relocateGroupWithDatumChange', 'roofOutlinePolicyReference')

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest().upper()


def read_json(path):
    return json.loads(path.read_text('utf-8-sig'))


def master_rules(master):
    rules = {row['id']: row['rule'] for row in master['rules']}
    section_rules = [block['args'] for section in master['sections']
                     for block in section['blocks'] if block['kind'] == 'rule']
    if (len(rules) != master['ruleCount'] or len(master['rules']) != len(rules) or
            len(section_rules) != len(rules) or dict(section_rules) != rules):
        raise ValueError('Inconsistent rule IDs or text in the master compilation')
    return rules


def unified_catalog(master, digest):
    rows = {row['id']: row for row in master['rules']}
    if len(rows) != len(master['rules']):
        raise ValueError('Duplicate rule ID in unified catalog')
    replaced = {}
    for key, row in rows.items():
        for previous in row.get('execution', {}).get('replaces', []):
            if previous not in rows or previous == key or previous in replaced:
                raise ValueError(f'Ambiguous rule replacement: {previous}')
            replaced[previous] = key
    if any(key in replaced and replaced[key] in replaced for key in replaced):
        raise ValueError('Use the final active rule for each replacement')
    lines = [f'PLANRULES1 {digest.upper()}']
    for key, row in rows.items():
        if not re.fullmatch(r'[A-Z]+[0-9]+', key):
            raise ValueError(f'Invalid rule ID: {key}')
        metadata = row.get('execution', {})
        default_state = 'paused' if row.get('section') == '12 散水与明沟规则保留但暂停' else 'active'
        state = 'retired' if key in replaced else metadata.get('state', default_state)
        action = metadata.get('action', 'unclassified')
        if state not in {'active', 'retired', 'paused'} or not re.fullmatch(r'[a-z][a-z0-9-]*', action):
            raise ValueError(f'Invalid execution metadata: {key}')
        rule = row['rule']
        if state != 'active':
            action = 'none'
        signature = hashlib.sha256(rule.encode('utf-8')).hexdigest().upper()
        if state == 'active' and metadata.get('reviewedTextSHA256', signature) != signature:
            action = 'changed'
        if key == 'B04' and action == 'floor-datum-readback':
            metres = wall_height_policy(rule)['defaultStoreyHeightM']
            lines.append(f'PARAM B04 storey-height-mm {metres * 1000:g}')
        lines.append(f'RULE {key} {state} {action} {signature}')
    return '\n'.join((*lines, 'END', ''))


def sync_unified_catalog(policy_dir, master):
    policy_dir.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(MASTER, policy_dir / 'all-plan-rules.json')
    catalog = unified_catalog(master, sha(MASTER))
    (policy_dir / 'rule-catalog.tsv').write_text(catalog, encoding='ascii', newline='\n')


def annotation_line(alias, rule_id, rules):
    return f'{alias}  {rules[rule_id]}〔P14 {rule_id}〕'


def replace_rule_line(text, prefix, line):
    updated, count = re.subn(rf'^{re.escape(prefix)}[^\r\n]*$', lambda _: line, text, flags=re.M)
    if count != 1:
        raise ValueError('Missing or duplicate generated rule: ' + prefix.strip())
    return updated


def render_annotation_rules(text, rules):
    for alias, rule_id in ANNOTATION_RULES.items():
        line = annotation_line(alias, rule_id, rules)
        if alias in ANNOTATION_INSERT_AFTER and not re.search(rf'^{alias}[ \t]+', text, re.M):
            previous = ANNOTATION_INSERT_AFTER[alias]
            anchor, = re.findall(rf'^{previous}  [^\r\n]*$', text, re.M)
            text = replace_rule_line(text, previous + '  ', anchor + '\n\n' + line)
        else:
            text = replace_rule_line(text, alias + '  ', line)
    for rule_id in ('S06', 'S03', 'N05'):
        text, count = re.subn(rf'^.*〔P14 {rule_id}〕$', lambda _: rules[rule_id] + f'〔P14 {rule_id}〕',
                              text, flags=re.M)
        if count != 1:
            raise ValueError('Missing or duplicate annotation rule ' + rule_id)
    return text


def render_plan_index(master):
    lines = ['# 平面规则索引',
             f"现行主汇编共 {master['ruleCount']} 条，按规则编号查阅。图纸执行顺序及适用口径见[详细规则](rules.md)。历史样张、旧版本和案例值按其原范围理解。",
             f'仓库来源为 `{MASTER.relative_to(ROOT).as_posix()}`。']
    for section in master['sections']:
        rows = [block['args'] for block in section['blocks'] if block['kind'] == 'rule']
        if rows:
            lines.append('## ' + section['title'])
            lines.extend(f'**{rule_id}**　{rule}' for rule_id, rule in rows)
    return '\n\n'.join(lines) + '\n'


def verify_rule_content(master, annotation, plan_directory):
    rules = master_rules(master)
    for alias, rule_id in ANNOTATION_RULES.items():
        matches = re.findall(rf'^{alias}[ \t]+[^\r\n]*$', annotation, re.M)
        if matches != [annotation_line(alias, rule_id, rules)]:
            raise ValueError(f'Stale, missing or duplicate annotation rule {alias} / {rule_id}')
    index = (plan_directory / 'references/rule-index.md').read_text('utf-8-sig')
    entries = re.findall(r'^\*\*([A-Z]+\d+)\*\*　([^\r\n]+)$', index, re.M)
    if len(entries) != len(rules) or dict(entries) != rules:
        raise ValueError('Plan automation rule index differs from the master compilation')
    details = (plan_directory / 'references/rules.md').read_text('utf-8-sig')
    if rules['S03'] + '〔P14 S03〕' not in annotation.splitlines():
        raise ValueError('Stale annotation rule S03')
    for rule_id in EXECUTION_RULES:
        expected = f'- **{rule_id}**　{rules[rule_id]}'
        if re.findall(rf'^- \*\*{rule_id}\*\*　[^\r\n]*$', details, re.M) != [expected]:
            raise ValueError('Stale detailed plan automation rule ' + rule_id)


def write_text(path, text):
    original = path.read_bytes()
    newline = '\r\n' if b'\r\n' in original else '\n'
    encoding = 'utf-8-sig' if original.startswith(b'\xef\xbb\xbf') else 'utf-8'
    payload = text.replace('\n', newline).encode(encoding)
    if payload != original:
        path.write_bytes(payload)


def axis_tolerances(rule):
    patterns = {'registrationMaxResidualMM': r'不超过\s*(\d+(?:\.\d+)?)\s*mm',
                'drawingAxisPositionMaxDeviationMM': r'允许值为\s*(\d+(?:\.\d+)?)\s*mm'}
    tolerances = {}
    for field, pattern in patterns.items():
        numbers = re.findall(pattern, rule)
        if len(numbers) != 1 or float(numbers[0]) <= 0:
            raise ValueError('A32 tolerance is missing or ambiguous: ' + field)
        tolerances[field] = float(numbers[0])
    return tolerances


def plan_text_typography(rule):
    match = re.search(
        r'文字比例为 1:(\d+)，文字样式为 ([A-Za-z0-9_]+)，名义纸面字高为 (\d+(?:\.\d+)?) mm', rule
    )
    if not match or int(match[1]) <= 0 or float(match[3]) <= 0:
        raise ValueError('Missing or invalid N06 plan text typography')
    return {'ruleID': 'N06', 'mandatory': True, 'scaleDenominator': int(match[1]),
            'styleName': match[2], 'nominalPaperHeightMM': float(match[3])}


def opening_label_typography(rule):
    match = re.search(r'编号文字样式统一为 ([A-Za-z0-9_]+)，纸面名义字高统一为 (\d+(?:\.\d+)?) mm', rule)
    if not match or float(match[2]) <= 0:
        raise ValueError('Missing or invalid L08 opening label typography')
    result = {'typographyRule': 'L08', 'mandatoryTypography': True,
              'textStyle': match[1], 'paperHeightMM': float(match[2])}
    if match[1] == '_TCH_WINDOW':
        result.update(fontFile='simplex.shx', bigFontFile='gbcbig.shx',
                      widthFactor=0.65, fixedHeight=4.0, layer='A-WINDOW-TEXT',
                      source='user-confirmed-2026-11-18')
    return result


def project_execution_rules(execution, rules, dimension_policy, template_policy=None, cleanup_policy=None):
    execution['planRuleBindings'] = {field: rules[rule_id] for field, rule_id in PLAN_RULE_BINDINGS.items()}
    execution['planTextTypography'] = plan_text_typography(rules['N06'])
    execution.setdefault('roomTextAndArea', {})['typographyRule'] = 'N06'
    if cleanup_policy:
        execution['annotationCleanupPolicy'] = cleanup_policy
        room = execution['roomTextAndArea']
        for field in ('areaPlacement', 'areaDisplay', 'areaStyleNotForcedToFY_HZ'):
            room.pop(field, None)
        room.update({
            'publishRoomArea': False,
            'removeSourceRoomArea': True,
            'roomLayer': 'A-房间名称',
            'calculationBasis': '房间净域按需用于内部定位，建筑面积按 S06 单独处理。',
            'connectedSpacePartition': rules['S03'],
            'buildingAreaCalculation': rules['S06'],
            'outdoorFeatureNames': rules['N05'],
        })
    execution.setdefault('openingLabels', {}).update(opening_label_typography(rules['L08']))
    dimensions = execution.setdefault('dimensions', {})
    dimensions['stylePolicy'] = dict(dimension_policy, skillRuleID='PD12', textStyleSkillRuleID='PD18')
    dimensions['sourceDimensionPolicy'] = {
        'ruleID': 'D30', 'outputActions': ['create', 'replace'],
        'allowSourceObjectReuse': False, 'allowSourceStyleReuse': False,
    }
    if dimension_policy.get('mode') == 'native-template-clone-stretch-only':
        dimensions['sourceDimensionPolicy'].update(
            creationMethod='clone-native-template-then-stretch', allowSourceStyleOverwrite=False,
            defaultStyleInitialization='before-template-import-only')
    if template_policy:
        execution['annotationTemplatePolicy'] = template_policy
        execution['layers'] = {row['role']: {'name': row['name'], 'ACI': row['aci']}
                               for row in template_policy['layers']}
        execution['controlFrames'] = template_policy['controlFrames']
        execution['printing'] = template_policy['plot']
        execution['textRoleProfiles'] = template_policy['textProfiles']
        execution['axes']['templateSource'] = {
            'sha256': template_policy['sha256'], 'operations': ['clone-native-object', 'stretch-native-grips'],
            'allowSourceStyleOverwrite': False, 'readback': template_policy['axisReadback']}
    dimensions.pop('interiorHingedDoorJambMM', None)
    dimensions['doorJambByWallThicknessMM'] = door_jamb_by_wall_thickness(rules['TL11'])
    if cleanup_policy and cleanup_policy.get('publishInteriorDimensions') is False:
        dimensions['publishInteriorDimensions'] = False
        dimensions['removeSourceInteriorDimensions'] = True
        for previous in ('netDimensions', 'interiorOpeningChainEndpoints',
                         'includeExistingStairLiftPlatformStepDimensions', 'interiorJointPlacement'):
            dimensions.pop(previous, None)
        execution['furnitureEdit'] = cleanup_policy['furnitureEdit']
    else:
        dimensions['interiorJointPlacement'] = rules['D25']
    execution.setdefault('floorElevations', {})['wallHeightPolicy'] = wall_height_policy(rules['B04'])
    floor_details = execution.setdefault('floorDetailVerification', {})
    floor_details['railingUpstandPlanSpacingMM'] = railing_upstand_plan_spacing(rules['D29'])
    floor_details['entrance'] = rules['C01']
    floor_details['exteriorStepDefaultsMM'] = exterior_step_defaults(rules['C20'])
    floor_details['rules'] = list(dict.fromkeys((*floor_details.get('rules', []), 'D29', 'C01', 'C02', 'C20')))
    execution['precedence'] = execution['rulePrecedence'] = PRECEDENCE
    execution['axes'].setdefault('reviewedPlacementRules', {}).update(
        {rule_id: rules[rule_id] for rule_id in REVIEWED_AXIS_RULES})
    roof = execution.setdefault('roofOutlinePolicy', {})
    for rule_id in ('A29', 'D27', 'QA10'):
        roof[rule_id] = rules[rule_id]
    for field, rule_id in ENTRY_AXIS_RULES.items():
        execution['axes'].setdefault(field, {}).update(rule=rule_id, text=rules[rule_id])
    execution['axes']['crossFloorPreissueVerification'].update(axis_tolerances(rules['A32']))
    execution['perimeter'].setdefault('axisCircleCrowding', {}).update(
        rule='A14', layout=rules['A14'], **axis_circle_spacing(rules['A14']))


def axis_circle_spacing(rule):
    fields = {'minimumEdgeClearancePaperMM': '相邻圈外缘净空默认至少纸面',
              'maximumLateralOffsetPaperMM': '单圈沿排布线的最大侧移默认纸面',
              'referenceCircleDiameterPaperMM': '圈径为纸面',
              'centerSpacingAtReferenceDiameterPaperMM': '圈心距至少纸面'}
    spacing = {}
    for field, phrase in fields.items():
        values = re.findall(phrase + r' (\d+(?:\.\d+)?) mm', rule)
        if len(values) != 1 or float(values[0]) <= 0:
            raise ValueError('A14 spacing missing or ambiguous: ' + field)
        spacing[field] = json.loads(values[0])
    if not math.isclose(spacing['referenceCircleDiameterPaperMM'] + spacing['minimumEdgeClearancePaperMM'],
                        spacing['centerSpacingAtReferenceDiameterPaperMM']):
        raise ValueError('A14 circle centre spacing does not match diameter and clearance')
    return spacing


def door_jamb_by_wall_thickness(rule):
    standard = re.findall(r'墙厚 (\d+) mm 时，标准门垛净宽为 (\d+) mm', rule)
    if len(standard) != 2 or len(dict(standard)) != 2 or any(int(width) <= 0 for _, width in standard):
        raise ValueError('Missing or inconsistent TL11 door jamb dimensions')
    return {wall: {'standardClearWidthMM': int(width), 'preferredAdjustmentMM': int(width)}
            for wall, width in standard}


def wall_height_policy(rule):
    defaults = re.findall(r'默认层高 (\d+(?:\.\d+)?) m', rule)
    if len(defaults) != 1 or float(defaults[0]) <= 0 or '上层楼面标高减本层楼面标高' not in rule:
        raise ValueError('Missing or inconsistent B04 wall height policy')
    return {'basis': 'upperFloorElevationMinusCurrentFloorElevation',
            'defaultStoreyHeightM': float(defaults[0])}


def railing_upstand_plan_spacing(rule):
    profiles = re.findall(r'墙厚 (\d+) mm，反坎总宽 \1 mm，平面间距 (\d+)＋(\d+)＋(\d+) mm', rule)
    if len(profiles) != 2 or len({width for width, *_ in profiles}) != 2:
        raise ValueError('Missing or ambiguous D29 railing and upstand profiles')
    spacing = {width: list(map(int, (outer, middle, inner))) for width, outer, middle, inner in profiles}
    for width, (outer, middle, inner) in spacing.items():
        if outer <= 0 or outer != inner or middle != 80 or outer + middle + inner != int(width):
            raise ValueError('Invalid D29 railing and upstand spacing')
    return spacing


def exterior_step_defaults(rule):
    main = re.findall(r'主入口设 (\d+) 段平台外踏面，各 (\d+) mm', rule)
    other = re.findall(r'其他出口上平台默认进深 (\d+) mm，设 (\d+) 段平台外踏面，各 (\d+) mm', rule)
    if len(main) != 1 or len(other) != 1:
        raise ValueError('Missing or ambiguous C20 exterior step defaults')
    main_count, main_depth = map(int, main[0])
    platform, other_count, other_depth = map(int, other[0])
    if min(main_count, main_depth, platform, other_count, other_depth) <= 0:
        raise ValueError('Invalid C20 exterior step defaults')
    return {'main': {'exposedTreadCount': main_count, 'treadDepth': main_depth},
            'other': {'platformDepth': platform, 'exposedTreadCount': other_count, 'treadDepth': other_depth}}


def verify_machine_rules(entry, rules, dimension_policy):
    master = read_json(MASTER)
    cleanup = master.get('annotationCleanupPolicy')
    if cleanup:
        room = entry.get('roomTextAndArea', {})
        if (entry.get('annotationCleanupPolicy') != cleanup or
                room.get('publishRoomArea') is not False or room.get('removeSourceRoomArea') is not True or
                room.get('roomLayer') != 'A-房间名称' or room.get('connectedSpacePartition') != rules['S03'] or
                room.get('buildingAreaCalculation') != rules['S06'] or
                room.get('outdoorFeatureNames') != rules['N05'] or
                entry.get('axes', {}).get('mainAxisContinuityPolicy') != master['mainAxisContinuityPolicy'] or
                room.keys() & {'areaPlacement', 'areaDisplay', 'areaStyleNotForcedToFY_HZ'}):
            raise ValueError('Stale plan annotation cleanup or room area display policy')
    if dimension_policy.get('mode') == 'native-template-clone-stretch-only':
        template = entry.get('annotationTemplatePolicy', {})
        if template != master.get('annotationTemplatePolicy'):
            raise ValueError('Stale native template policy')
        if (entry.get('controlFrames') != template['controlFrames'] or
                entry.get('printing') != template['plot'] or
                entry.get('textRoleProfiles') != template['textProfiles']):
            raise ValueError('Stale template typography, frame or printing policy')
        expected_layers = {row['role']: {'name': row['name'], 'ACI': row['aci']}
                           for row in template['layers']}
        if entry.get('layers') != expected_layers:
            raise ValueError('Stale template layers')
    labels = entry.get('openingLabels', {})
    if any(labels.get(key) != value for key, value in opening_label_typography(rules['L08']).items()):
        raise ValueError('Stale L08 opening label typography')
    if (entry.get('planTextTypography') != plan_text_typography(rules['N06']) or
            entry.get('roomTextAndArea', {}).get('typographyRule') != 'N06'):
        raise ValueError('Stale N06 plan text typography')
    for field, rule_id in PLAN_RULE_BINDINGS.items():
        if entry.get('planRuleBindings', {}).get(field) != rules[rule_id]:
            raise ValueError('Stale execution rule: ' + rule_id)
    if entry.get('precedence') != PRECEDENCE or entry.get('rulePrecedence') != PRECEDENCE:
        raise ValueError('Stale precedence for G09')
    if entry.get('floorElevations', {}).get('wallHeightPolicy') != wall_height_policy(rules['B04']):
        raise ValueError('Stale B04 wall height policy')
    details = entry.get('floorDetailVerification', {})
    if ('D29' not in details.get('rules', []) or
            details.get('railingUpstandPlanSpacingMM') != railing_upstand_plan_spacing(rules['D29'])):
        raise ValueError('Stale D29 railing and upstand spacing')
    if (not {'C01', 'C02', 'C20'} <= set(details.get('rules', [])) or
            details.get('entrance') != rules['C01'] or
            details.get('exteriorStepDefaultsMM') != exterior_step_defaults(rules['C20'])):
        raise ValueError('Stale C01, C02 or C20 exterior step rules')
    dimensions = entry.get('dimensions', {})
    expected_source = {
            'ruleID': 'D30', 'outputActions': ['create', 'replace'],
            'allowSourceObjectReuse': False, 'allowSourceStyleReuse': False,
    }
    template_mode = dimension_policy.get('mode') == 'native-template-clone-stretch-only'
    if template_mode:
        expected_source.update(creationMethod='clone-native-template-then-stretch',
                               allowSourceStyleOverwrite=False,
                               defaultStyleInitialization='before-template-import-only')
    if dimensions.get('sourceDimensionPolicy') != expected_source:
        raise ValueError('Stale D30 source dimension replacement policy')
    expected_style = dict(dimension_policy, skillRuleID='PD12', textStyleSkillRuleID='PD18')
    expected_references = {
        'dimensionStyleVariable': 'DIMTXSTY',
        'baseDimensionStyle': '_TCH_ARCH',
        'scaledDimensionStylePattern': '_TCH_ARCH&&<比例分母>',
        'verifyStyleTableAfterReopen': True,
        'verifyEffectiveSegmentsAfterReopen': True,
        'nativeTemplate': {
            'baseTextStyle': '_TCH_DIM',
            'scaledTextStyle': '_TCH_DIM',
            'effectiveTextStyle': '_TCH_DIM',
        },
        'explicitTchDim': {
            'baseTextStyle': '_TCH_DIM',
            'scaledTextStyle': '_TCH_DIM',
            'effectiveTextStyle': '_TCH_DIM',
        },
    }
    if (dimension_policy.get('textStyleName') != '_TCH_DIM' or
            dimension_policy.get('textStyleMatch') != 'exact' or
            dimension_policy.get('textStyleReferencePolicy') != expected_references or
            'Y10' not in dimension_policy.get('ruleIDs', []) or
            dimensions.get('stylePolicy') != expected_style):
        raise ValueError('Stale Y10 dimension text style policy')
    if ('interiorHingedDoorJambMM' in dimensions or
            dimensions.get('doorJambByWallThicknessMM') != door_jamb_by_wall_thickness(rules['TL11'])):
        raise ValueError('Stale TL11 door jamb dimensions')
    cleanup = entry.get('annotationCleanupPolicy', {})
    if cleanup.get('publishInteriorDimensions') is False:
        if (dimensions.get('publishInteriorDimensions') is not False or
                dimensions.get('removeSourceInteriorDimensions') is not True or
                'interiorJointPlacement' in dimensions or
                entry.get('furnitureEdit') != 'set-layer-only' or
                cleanup.get('allowFurnitureExplode') is not False or
                cleanup.get('allowFurnitureRegroup') is not False or
                entry['roomTextAndArea'].get('publishRoomArea') is not False):
            raise ValueError('Stale plan annotation or furniture scope')
    elif dimensions.get('interiorJointPlacement') != rules['D25']:
        raise ValueError('Stale D25 interior joint placement')
    for rule_id in REVIEWED_AXIS_RULES:
        if entry['axes'].get('reviewedPlacementRules', {}).get(rule_id) != rules[rule_id]:
            raise ValueError('Stale axis connection rule: ' + rule_id)
    crowding = entry['perimeter'].get('axisCircleCrowding', {})
    if crowding.get('rule') != 'A14' or crowding.get('layout') != rules['A14']:
        raise ValueError('Stale execution rule: A14')
    for field, value in axis_circle_spacing(rules['A14']).items():
        if crowding.get(field) != value:
            raise ValueError('Stale A14 execution spacing: ' + field)
    for rule_id in ('A29', 'D27', 'QA10'):
        if entry.get('roofOutlinePolicy', {}).get(rule_id) != rules[rule_id]:
            raise ValueError('Stale execution rule: ' + rule_id)
    for field, rule_id in ENTRY_AXIS_RULES.items():
        item = entry['axes'].get(field, {})
        if item.get('rule') != rule_id or item.get('text') != rules[rule_id]:
            raise ValueError('Stale execution rule: ' + rule_id)
    for field, value in axis_tolerances(rules['A32']).items():
        if entry['axes']['crossFloorPreissueVerification'].get(field) != value:
            raise ValueError('Stale A32 execution tolerance: ' + field)


def verify_execution_rules(master):
    rules = master_rules(master)
    entry = read_json(ENTRY)
    markdown = ENTRY.with_suffix('.md').read_text('utf-8-sig')
    master_markdown = MASTER.with_suffix('.md').read_text('utf-8-sig')
    for rule_id in EXECUTION_RULES:
        line = f'**{rule_id}**　{rules[rule_id]}'
        if master_markdown.splitlines().count(line) != 1 or markdown.splitlines().count('- ' + line) != 1:
            raise ValueError('Stale plan rule in the master or execution Markdown: ' + rule_id)
    verify_machine_rules(entry, rules, master['dimensionStylePolicy'])
    if master['precedence'] != PRECEDENCE:
        raise ValueError('Stale master precedence for G09')
    perimeter = read_json(PERIMETER_CONTENT)
    if perimeter.get('applicabilityRules') != {rule_id: rules[rule_id] for rule_id in ('D01', 'D27', 'QA10')}:
        raise ValueError('Stale perimeter applicability rules: D01, D27 or QA10')
    interior = read_json(INTERIOR_DIMENSIONS)
    if interior.get('dimensionStylePolicy') != master['dimensionStylePolicy']:
        raise ValueError('Stale interior dimension style policy')
    if interior.get('planTextTypography') != plan_text_typography(rules['N06']):
        raise ValueError('Stale N06 interior plan text typography')
    if interior.get('openingLabelTypography') != opening_label_typography(rules['L08']):
        raise ValueError('Stale L08 interior opening label typography')
    jamb, = [row for row in interior['rules'] if row['id'] == 'TL11']
    if (jamb['rule'] != rules['TL11'] or 'modelLengthMM' in jamb or
            jamb.get('doorJambByWallThicknessMM') != door_jamb_by_wall_thickness(rules['TL11'])):
        raise ValueError('Stale interior dimension rule: TL11')
    for rule_id in INTERIOR_PLACEMENT_RULES:
        if interior.get('supplementalPlacementRules', {}).get(rule_id) != rules[rule_id]:
            raise ValueError('Stale interior dimension rule: ' + rule_id)
    for name, expected in entry['skill']['sourceHashes'].items():
        if sha(SKILL / name).lower() != expected:
            raise ValueError('Stale annotation source fingerprint: ' + name)


def verify_installed_skills():
    master = read_json(MASTER)
    annotation = (SKILL / 'references/plan.md').read_text('utf-8-sig')
    installed = INSTALLED_SKILLS / PLAN_SKILL.name
    for directory in (PLAN_SKILL, installed):
        verify_rule_content(master, annotation, directory)
        verify_template_assets(directory / 'assets/annotation-template', master)
    verify_execution_rules(master)
    manifest = read_json(PLAN_SKILL / 'package-manifest.json')
    if manifest != read_json(installed / 'package-manifest.json') or any(
            sha(PLAN_SKILL / name).lower() != value or sha(installed / name).lower() != value
            for name, value in manifest.items()):
        raise ValueError('Workspace and installed plan automation package fingerprints differ')
    provenance = read_json(SKILL / 'references/provenance.json')
    source, = [item for item in provenance['sources'] if item['id'] == 'P14']
    template_reference = 'references/template-standard.md'
    if (source['sha256'] != sha(MASTER).lower() or source['bytes'] != MASTER.stat().st_size or
            provenance['reference_sha256']['plan.md'] != sha(SKILL / 'references/plan.md').lower() or
            provenance['reference_sha256']['template-standard.md'] != sha(SKILL / template_reference).lower() or
            (SKILL / template_reference).read_text('utf-8-sig') !=
            (PLAN_SKILL / template_reference).read_text('utf-8-sig')):
        raise ValueError('Installed annotation rule provenance is stale')


def sync_installed_skills():
    master = read_json(MASTER)
    rules = master_rules(master)
    annotation_path = SKILL / 'references/plan.md'
    annotation = render_annotation_rules(annotation_path.read_text('utf-8-sig'), rules)
    index = render_plan_index(master)
    details = (PLAN_SKILL / 'references/rules.md').read_text('utf-8-sig')
    execution_markdown = ENTRY.with_suffix('.md')
    execution_text = execution_markdown.read_text('utf-8-sig')
    master_markdown = MASTER.with_suffix('.md')
    master_text = master_markdown.read_text('utf-8-sig')
    execution = read_json(ENTRY)
    execution['axes']['mainAxisContinuityPolicy'] = master['mainAxisContinuityPolicy']
    for rule_id in EXECUTION_RULES:
        prefix = f'**{rule_id}**　'
        line = prefix + rules[rule_id]
        details = replace_rule_line(details, '- ' + prefix, '- ' + line)
        execution_text = replace_rule_line(execution_text, '- ' + prefix, '- ' + line)
        master_text = replace_rule_line(master_text, prefix, line)
    project_execution_rules(execution, rules, master['dimensionStylePolicy'], master.get('annotationTemplatePolicy'),
                            master.get('annotationCleanupPolicy'))
    execution['currentRuleRevision'] = REVISION
    execution['effectiveDate'] = REVISION
    execution['skill']['sourceVersion'] = REVISION + ' 平面主汇编同步'
    perimeter = read_json(PERIMETER_CONTENT)
    perimeter['applicabilityRules'] = {rule_id: rules[rule_id] for rule_id in ('D01', 'D27', 'QA10')}
    perimeter['effectiveDate'] = REVISION
    write_text(PERIMETER_CONTENT, json.dumps(perimeter, ensure_ascii=False, indent=2) + '\n')
    interior = read_json(INTERIOR_DIMENSIONS)
    interior['effectiveDate'] = REVISION
    interior['dimensionStylePolicy'] = master['dimensionStylePolicy']
    interior['planTextTypography'] = plan_text_typography(rules['N06'])
    interior['openingLabelTypography'] = opening_label_typography(rules['L08'])
    jamb, = [row for row in interior['rules'] if row['id'] == 'TL11']
    jamb['rule'] = rules['TL11']
    jamb.pop('modelLengthMM', None)
    jamb['doorJambByWallThicknessMM'] = door_jamb_by_wall_thickness(rules['TL11'])
    for row in interior['rules']:
        if row['id'] in {'TL05', 'TL08', 'TL09'}:
            row['rule'] = rules[row['id']]
    interior.setdefault('supplementalPlacementRules', {}).update(
        {rule_id: rules[rule_id] for rule_id in INTERIOR_PLACEMENT_RULES})
    write_text(INTERIOR_DIMENSIONS, json.dumps(interior, ensure_ascii=False, indent=2) + '\n')

    installed = INSTALLED_SKILLS / PLAN_SKILL.name
    manifest_path = PLAN_SKILL / 'package-manifest.json'
    manifest = read_json(manifest_path)
    template = master.get('annotationTemplatePolicy', {})
    if template.get('finalTypographyPolicy'):
        fonts_path = PLAN_SKILL / 'assets/fonts.json'
        fonts = read_json(fonts_path)
        fonts['roomName'].update(nominalPaperTextHeightMM=4.0, widthFactor=0.7)
        fonts['dimension'].update(textStyle=master['dimensionStylePolicy']['textStyleName'],
                                  nominalPaperTextHeightMM=2.8, widthFactor=1.0)
        fonts['openingNumber'] = {
            'style': '_TCH_WINDOW', 'font': 'simplex.shx', 'bigFont': 'gbcbig.shx',
            'widthFactor': 0.65, 'nominalPaperTextHeightMM': 4.0, 'fixedHeight': 4.0,
            'source': 'annotation-template/standard.json:finalTypographyPolicy',
        }
        fonts['roleProfiles'] = template['textProfiles']
        write_text(fonts_path, json.dumps(fonts, ensure_ascii=False, indent=2) + '\n')
    sync_unified_catalog(PLAN_SKILL / 'assets/plan-policy', master)
    (PLAN_SKILL / 'assets/plan-policy/execution.tsv').write_text(
        executable_policy(master, read_json(SPACING)), encoding='ascii')
    for name in ('assets/plan-policy/all-plan-rules.json', 'assets/plan-policy/rule-catalog.tsv',
                 'assets/axes.dwg', 'assets/axes.tsv', 'tools/Invoke-PlanJointRepair.ps1',
                 'tools/joint_annotation_search.py', 'tools/joint_annotation_slots.py', 'native/joint_annotation_repair.py',
                 'native/wall_height_revision.py', 'native/dimension_revision.py'):
        manifest.setdefault(name, '')
    provenance_path = SKILL / 'references/provenance.json'
    provenance = read_json(provenance_path)
    write_text(master_markdown, master_text)
    plan_sources = {'P1': MASTER.with_suffix('.md'), 'P2': SPACING, 'P3': PERIMETER_CONTENT,
                    'P4': INTERIOR_DIMENSIONS, 'P5': ROOT / 'docs/平面图层规则_20261105.json',
                    'P14': MASTER}
    for item in provenance['sources']:
        path = plan_sources.get(item['id'])
        if path:
            item.update(path=str(path), sha256=sha(path).lower(), bytes=path.stat().st_size)

    write_text(annotation_path, annotation)
    common_path = annotation_path.parent / 'common.md'
    write_text(common_path, replace_rule_line(common_path.read_text('utf-8-sig'), '| 平面尺寸标注 |',
               '| 平面尺寸标注 | 按模板出图遵循原生样件实际引用 | 明确统一为 `_TCH_DIM` 时按 PD18 检查独立重开后的样式表及图面尺寸，两处均实际引用 `_TCH_DIM` |'))
    write_text(annotation_path.parent / 'template-standard.md',
               (PLAN_SKILL / 'references/template-standard.md').read_text('utf-8-sig'))
    for name in ('plan.md', 'common.md', 'template-standard.md'):
        provenance['reference_sha256'][name] = sha(annotation_path.parent / name).lower()
    write_text(provenance_path, json.dumps(provenance, ensure_ascii=False, indent=2) + '\n')
    for name, text in (('references/rule-index.md', index), ('references/rules.md', details)):
        write_text(PLAN_SKILL / name, text)
    for name in manifest:
        origin = PLAN_SKILL / name
        manifest[name] = sha(origin).lower()
        target = installed / name
        if not target.exists() or sha(target).lower() != manifest[name]:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(origin, target)
    write_text(manifest_path, json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
    shutil.copyfile(manifest_path, installed / manifest_path.name)
    for name in execution['skill']['sourceHashes']:
        execution['skill']['sourceHashes'][name] = sha(SKILL / name).lower()
    write_text(execution_markdown, execution_text)
    write_text(ENTRY, json.dumps(execution, ensure_ascii=False, indent=2) + '\n')
    verify_installed_skills()

def executable_policy(master, spacing):
    placement = spacing['sidePlacement']
    if placement != master['perimeterSidePlacement'] or any(placement.get(key) != value for key, value in PLACEMENT.items()):
        raise ValueError('Unsupported perimeter side placement policy')
    measures = {x['relation']: x['paperMillimeters'] for x in spacing['rules'] if 'paperMillimeters' in x}
    distances = [measures[name] for name in ('本侧排布基准至界线可见起笔端',
        '洞口尺寸到投影辅助框', '轴间尺寸到洞口尺寸', '总尺寸到轴间尺寸', '轴圈近边到总尺寸', '轴圈直径')]
    dimension_style = master['dimensionStylePolicy']
    name = dimension_style['styleName']
    nominal_height = dimension_style['nominalTextHeightPaperMM']
    if not re.fullmatch(r'[A-Za-z0-9_$.-]+', name) or isinstance(nominal_height, bool) or not math.isfinite(nominal_height) or nominal_height <= 0:
        raise ValueError('Invalid plan dimension style or nominal paper height')
    execution = ' '.join(map(str, ('BCPPLANPOLICY3', *distances, 'DIMSTYLE', name, nominal_height,
                                  'PERIMETER', *(placement[key] for key in PLACEMENT), 'END')))
    return execution + '\n'


def verify_template_assets(folder, master):
    standard = master.get('annotationTemplatePolicy')
    if not standard:
        return
    if read_json(folder / 'standard.json') != standard:
        raise ValueError('Template resource policy differs from master')
    if sha(folder / standard['dwg']).lower() != standard['sha256'].lower():
        raise ValueError('Template DWG fingerprint differs')
    evidence = read_json(folder / 'native-readback.json')
    if evidence['sourceSHA256'] != standard['sha256']:
        raise ValueError('Template native readback source differs')
    ids = {(row['handle'], row['entity']['type']) for row in evidence['objects']}
    if ids != {(row['handle'], row['type']) for row in standard['nativeDonors']}:
        raise ValueError('Template donor mapping differs')
    if (standard['operations']['allowSourceStyleOverwrite'] or
            standard['operations']['allowExplodedReplacement']):
        raise ValueError('Template dependency protection disabled')


def sync(resources):
    policy = Path(resources) / 'plan-policy'
    master = read_json(MASTER)
    verify_rule_content(master, (SKILL / 'references/plan.md').read_text('utf-8-sig'), PLAN_SKILL)
    verify_execution_rules(master)
    sync_unified_catalog(policy, master)
    if master.get('annotationTemplatePolicy'):
        for folder in (Path(resources) / 'annotation-template',
                       policy / 'plan-automation-skill/assets/annotation-template'):
            folder.mkdir(parents=True, exist_ok=True)
            for origin in (PLAN_SKILL / 'assets/annotation-template').iterdir():
                if origin.is_file():
                    shutil.copyfile(origin, folder / origin.name)
    for name in PLAN_FILES:
        target = policy / 'plan-automation-skill' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(PLAN_SKILL / name, target)
    for source, target in [('references/plan.md', 'current-plan-rules.md'),
                           ('references/common.md', 'current-common-rules.md'),
                           ('references/provenance.json', 'current-plan-provenance.json')]:
        shutil.copyfile(SKILL / source, policy / target)
    data = json.loads((policy / 'rules.json').read_text('utf-8-sig'))
    spacing = json.loads(SPACING.read_text('utf-8-sig'))
    placement = spacing['sidePlacement']
    (policy / 'execution.tsv').write_text(executable_policy(master, spacing), 'ascii')
    data['skill']['sourceHashes'] = {name: sha(SKILL / name).lower() for name in
                                   ('SKILL.md', 'references/common.md', 'references/plan.md')}
    for key in RETIRED_PLACEMENT_FIELDS:
        data['perimeter'].pop(key, None)
    data['perimeter']['sidePlacement'] = placement
    data.get('controlFrames', {}).pop('datum', None)
    data['perimeter']['totalEndpoints'] = '本侧有效墙柱轴首末；第二道同边界，保留真实偏心段'
    data['axes']['representationPolicy'] = '逐根按服务构件与有效范围确认轴位；范围不明的可靠原表达留在源台账。整图深化按 A26 删除全部已识别的源轴实体并重建。'
    data['axes']['mainAxisContinuityPolicy'] = master.get('mainAxisContinuityPolicy')
    project_execution_rules(data, master_rules(master), master['dimensionStylePolicy'],
                            master.get('annotationTemplatePolicy'), master.get('annotationCleanupPolicy'))
    data['currentRuleRevision'] = REVISION
    data['runtimeImplementation'] = {
        'version': VERSION, 'commonFinalization': 'all supported generation routes before save',
        'entry': 'ProductCurrentPlanRules.inc', 'axisService': 'native walls/columns with reviewed per-axis overrides',
        'limitations': ['通用入口自动执行源标注清理与轴网重建尚未验证；建筑面积及构造模块实际覆盖以当前版本验收为准',
                        '栏杆反坎、踏面进深、详图库和CTB缺项继续待确认；散水暂停'],
        'allRulesUniversallyImplemented': False,
    }
    if master.get('annotationTemplatePolicy'):
        data['runtimeImplementation'].update(templateNativeAdaptationVerified=False,
            templateRuleStatus='rules-and-resources-only; do not claim existing ARX is template-compatible')
    (policy / 'rules.json').write_text(json.dumps(data, ensure_ascii=False, indent=2), 'utf-8')
    sources = {'all-plan-rules.json': sha(MASTER),
               'rule-catalog.tsv': sha(policy / 'rule-catalog.tsv'),
               'execution.tsv': sha(policy / 'execution.tsv'),
               'current-plan-rules.md': sha(SKILL / 'references/plan.md'),
               'current-common-rules.md': sha(SKILL / 'references/common.md'),
               'current-plan-provenance.json': sha(SKILL / 'references/provenance.json')}
    sources.update({f'plan-automation-skill/{name}': sha(PLAN_SKILL / name) for name in PLAN_FILES})
    if master.get('annotationTemplatePolicy'):
        sources.update({f'plan-automation-skill/assets/annotation-template/{p.name}': sha(p)
                        for p in (PLAN_SKILL / 'assets/annotation-template').iterdir() if p.is_file()})
    (policy / 'policy-sources.json').write_text(json.dumps({'schema': 'bcp.plan-policy-sources.v1',
        'version': VERSION, 'ruleAliases': ANNOTATION_RULES, 'files': sources}, ensure_ascii=False, indent=2), 'utf-8')
    verify(resources)

def verify(resources):
    policy = Path(resources) / 'plan-policy'
    manifest = json.loads((policy / 'policy-sources.json').read_text('utf-8'))
    for name, expected in manifest['files'].items():
        if sha(policy / name) != expected:
            raise ValueError('Stale policy resource: ' + name)
    for name in PLAN_FILES:
        if manifest['files'].get(f'plan-automation-skill/{name}') != sha(PLAN_SKILL / name):
            raise ValueError('Plan workflow skill not synchronized: ' + name)
    if sha(policy / 'all-plan-rules.json') != sha(MASTER):
        raise ValueError('Master policy not synchronized')
    master = read_json(MASTER)
    verify_template_assets(Path(resources) / 'annotation-template', master)
    if (policy / 'rule-catalog.tsv').read_text('utf-8-sig') != unified_catalog(master, sha(MASTER)):
        raise ValueError('Stale unified rule catalog')
    if manifest.get('ruleAliases') != ANNOTATION_RULES:
        raise ValueError('Packaged plan rule aliases are stale')
    verify_rule_content(master, (policy / 'current-plan-rules.md').read_text('utf-8-sig'),
                        policy / 'plan-automation-skill')
    for source, target in [('references/plan.md', 'current-plan-rules.md'),
                           ('references/common.md', 'current-common-rules.md'),
                           ('references/provenance.json', 'current-plan-provenance.json')]:
        if sha(SKILL / source) != sha(policy / target):
            raise ValueError('Skill resource not synchronized: ' + source)
    data = json.loads((policy / 'rules.json').read_text('utf-8'))
    for name, expected in data['skill']['sourceHashes'].items():
        if sha(SKILL / name).lower() != expected.lower():
            raise ValueError('Skill source hash stale: ' + name)
    verify_machine_rules(data, master_rules(master), master['dimensionStylePolicy'])
    spacing = json.loads(SPACING.read_text('utf-8-sig'))
    perimeter = data['perimeter']
    if (perimeter.get('sidePlacement') != spacing['sidePlacement'] or
            perimeter.keys() & set(RETIRED_PLACEMENT_FIELDS) or 'datum' in data.get('controlFrames', {})):
        raise ValueError('Retired or stale perimeter datum policy')
    if (policy / 'execution.tsv').read_text('ascii') != executable_policy(master, spacing):
        raise ValueError('Stale executable perimeter policy')
    return manifest

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('resources', type=Path, nargs='?')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--sync-skills', action='store_true')
    mode.add_argument('--check-skills', action='store_true')
    args = parser.parse_args()
    if args.sync_skills:
        sync_installed_skills()
    elif args.check_skills:
        verify_installed_skills()
    elif args.resources is None:
        parser.error('provide a resource directory, --sync-skills or --check-skills')
    if args.resources is not None:
        sync(args.resources)
    print('Current plan policy verified')
