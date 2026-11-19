import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('policy_sync', ROOT / '插件/tools/sync_plan_policy.py')
policy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(policy)

class PolicySyncTests(unittest.TestCase):
    def test_opening_information_and_three_mm_typography_cannot_drift(self):
        master = policy.read_json(policy.MASTER)
        data = policy.read_json(self.resources / 'plan-policy/rules.json')
        self.assertEqual(data['openingLabels']['paperHeightMM'], 3.0)
        self.assertEqual(data['openingLabels']['fixedHeight'], 3.0)
        self.assertEqual(data['openingLabels']['widthFactor'], .65)
        self.assertEqual(data['openingDesignSourcePolicy']['crossSourcePriority'], 'project-confirmed-basis')
        data['openingDesignSourcePolicy']['sourcesWhenProvided'] = []
        with self.assertRaisesRegex(ValueError, 'openingDesignSourcePolicy'):
            policy.verify_machine_rules(data, policy.master_rules(master), master['dimensionStylePolicy'])
        data['openingDesignSourcePolicy'] = master['openingDesignSourcePolicy']
        data['openingLabels']['paperHeightMM'] = 4.0
        with self.assertRaisesRegex(ValueError, 'L08'):
            policy.verify_machine_rules(data, policy.master_rules(master), master['dimensionStylePolicy'])

    def test_boundary_walls_are_separate_and_gates_have_numbers(self):
        master = policy.read_json(policy.MASTER)
        data = policy.read_json(self.resources / 'plan-policy/rules.json')
        scope = data['buildingPerimeterScopePolicy']
        self.assertIn('porch-steps', scope['subjectComponents'])
        self.assertIn('eave', scope['subjectComponents'])
        self.assertIn('boundary-wall', scope['excludeComponents'])
        self.assertIs(scope['useWholeDrawingBounds'], False)
        self.assertIs(data['boundaryWallPlanPolicy']['individualWallLengths'], True)
        self.assertIs(data['boundaryWallPlanPolicy']['numberEveryGate'], True)
        scope['useWholeDrawingBounds'] = True
        with self.assertRaisesRegex(ValueError, 'buildingPerimeterScopePolicy'):
            policy.verify_machine_rules(data, policy.master_rules(master), master['dimensionStylePolicy'])

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.resources = Path(self.temp.name)
        policy_dir = self.resources / 'plan-policy'
        policy_dir.mkdir()
        (policy_dir / 'rules.json').write_text(json.dumps({
            'skill': {},
            'perimeter': {'datum': '旧墙柱轴基准', 'automaticOutwardShiftForProjectionEnvelope': False},
            'axes': {},
        }), 'utf-8')
        policy.sync(self.resources)

    def tearDown(self):
        self.temp.cleanup()

    def test_current_master_and_skill_pass(self):
        policy.verify(self.resources)
        master = policy.read_json(policy.MASTER)
        staged = policy.read_json(self.resources / 'plan-policy/rules.json')
        self.assertEqual(staged['annotationCleanupPolicy'], master['annotationCleanupPolicy'])
        self.assertIs(staged['roomTextAndArea']['publishRoomArea'], False)
        staged['roomTextAndArea']['publishRoomArea'] = True
        with self.assertRaisesRegex(ValueError, 'room area display policy'):
            policy.verify_machine_rules(staged, policy.master_rules(master), master['dimensionStylePolicy'])

    def test_scope_disables_old_generation_and_retains_perimeter(self):
        master = policy.read_json(policy.MASTER)
        data = policy.read_json(self.resources / 'plan-policy/rules.json')
        self.assertIs(data['dimensions']['publishInteriorDimensions'], False)
        self.assertIs(data['dimensions']['removeSourceInteriorDimensions'], True)
        self.assertNotIn('interiorJointPlacement', data['dimensions'])
        self.assertEqual(data['furnitureEdit'], 'set-layer-only')
        self.assertEqual(data['perimeter']['sidePlacement'], master['perimeterSidePlacement'])
        self.assertTrue(data['annotationCleanupPolicy']['preserveFurnitureBlockDefinitions'])
        for key in ('allowFurnitureExplode', 'allowFurnitureRegroup'):
            data['annotationCleanupPolicy'][key] = True
            with self.assertRaisesRegex(ValueError, 'cleanup'):
                policy.verify_machine_rules(data, policy.master_rules(master), master['dimensionStylePolicy'])
            data['annotationCleanupPolicy'][key] = False

    def test_unified_catalog_discovers_new_and_replaced_rules(self):
        master = policy.read_json(policy.MASTER)
        master['rules'].extend([
            {'id': 'ZZ01', 'rule': '新的施工做法', 'execution': {'replaces': ['G01']}},
            {'id': 'ZZ02', 'rule': '待接入的新绘图动作', 'execution': {'action': 'arc-stair'}},
        ])
        catalog = policy.unified_catalog(master, 'A' * 64)
        rows = {line.split()[1]: line.split()[2:] for line in catalog.splitlines() if line.startswith('RULE ')}
        self.assertEqual(len(rows), master['ruleCount'] + 2)
        self.assertEqual(rows['G01'][:2], ['retired', 'none'])
        self.assertEqual(rows['ZZ01'][:2], ['active', 'unclassified'])
        self.assertEqual(rows['ZZ02'][:2], ['active', 'arc-stair'])
        self.assertNotIn('PARAM D23 stair-extension', catalog)

    def test_unified_catalog_cannot_leave_an_ambiguous_replacement(self):
        master = policy.read_json(policy.MASTER)
        master['rules'].extend([{'id': name, 'rule': name, 'execution': {'replaces': ['G01']}}
                                for name in ('ZZ01', 'ZZ02')])
        with self.assertRaisesRegex(ValueError, 'replacement'):
            policy.unified_catalog(master, 'A' * 64)

    def test_reopened_drawing_checks_have_versioned_mappings(self):
        master = policy.read_json(policy.MASTER)
        expected = {'D17': 'scope-policy-pending-runtime', 'D18': 'dimension-clearance',
                    'Y07': 'projection-frame', 'Y08': 'projection-frame',
                    'A02': 'axis-label-readback', 'A26': 'axis-occupancy-readback',
                    'A27': 'floor-axis-registration',
                    'A11': 'axis-line-readback', 'D11': 'perimeter-chain-readback',
                    'O06': 'opening-caption-readback'}
        expected.update({key: 'scope-policy-pending-runtime' for key in ('QA09', 'P04')})
        expected.update({key: 'current-plan-final-readback' for key in
                         ('G09', 'N06', 'L08', 'Y10', 'D23', 'S01', 'A28', 'O09', 'D31', 'QA11')})
        catalog = policy.unified_catalog(master, 'A' * 64)
        for key in ('A14', 'D09', 'D24', 'P06', 'TL04'):
            self.assertIn(f'RULE {key} active changed ', catalog)
        for key, action in expected.items():
            self.assertIn(f'RULE {key} active {action} ', catalog)
            row = next(row for row in master['rules'] if row['id'] == key)
            if action == 'current-plan-final-readback':
                self.assertTrue(row['execution']['runtimeConnected'])
                self.assertTrue(row['execution']['projectEvidenceRequired'])
                self.assertFalse(row['execution']['nativeImplementationVerified'])
            row['rule'] += ' 增加待接入条件。'
        catalog = policy.unified_catalog(master, 'A' * 64)
        for key in expected:
            self.assertIn(f'RULE {key} active changed ', catalog)

    def test_interior_scope_has_no_layout_parameter_and_detects_rewording(self):
        master = policy.read_json(policy.MASTER)
        row = next(row for row in master['rules'] if row['id'] == 'D23')
        catalog = policy.unified_catalog(master, 'B' * 64)
        self.assertNotIn('PARAM D23 stair-extension', catalog)
        self.assertIn('RULE D23 active current-plan-final-readback ', catalog)
        row['rule'] += ' 增加未经接入的适用条件。'
        self.assertIn('RULE D23 active changed ', policy.unified_catalog(master, 'B' * 64))

    def test_dimension_text_style_requires_exact_name(self):
        master = policy.read_json(policy.MASTER)
        expected = master['dimensionStylePolicy']
        self.assertEqual(expected['styleName'], '_TCH_ARCH')
        self.assertEqual(expected['nominalTextHeightPaperMM'], 2.8)
        self.assertEqual(expected['actualDimensionTextHeightAt100'], 280)
        self.assertEqual(expected['textStyleName'], '_TCH_DIM')
        self.assertEqual(expected['textStyleMatch'], 'exact')
        self.assertEqual(expected['textStyleDefinition'], {
            'fontFile': 'simplex.shx', 'bigFontEnabled': True, 'bigFontFile': 'gbcbig.shx',
            'fixedHeight': 0.0, 'widthFactor': 1.0, 'obliqueDegrees': 0.0,
        })
        self.assertIs(expected['allowSourceStyleOverwrite'], False)
        self.assertIs(expected['verifyActualSegmentTextStyle'], True)
        references = expected['textStyleReferencePolicy']
        self.assertEqual(references['dimensionStyleVariable'], 'DIMTXSTY')
        self.assertEqual(references['baseDimensionStyle'], '_TCH_ARCH')
        self.assertEqual(references['scaledDimensionStylePattern'], '_TCH_ARCH&&<比例分母>')
        self.assertIs(references['verifyStyleTableAfterReopen'], True)
        self.assertIs(references['verifyEffectiveSegmentsAfterReopen'], True)
        self.assertEqual(references['nativeTemplate'], {
            'baseTextStyle': '_TCH_DIM', 'scaledTextStyle': '_TCH_DIM',
            'effectiveTextStyle': '_TCH_DIM',
        })
        self.assertEqual(references['explicitTchDim'], {
            'baseTextStyle': '_TCH_DIM', 'scaledTextStyle': '_TCH_DIM',
            'effectiveTextStyle': '_TCH_DIM',
        })
        rules = policy.master_rules(master)
        entry = policy.read_json(policy.ENTRY)
        effective = entry['dimensions']['stylePolicy']
        effective['textStyleName'] = '_TCH_DIM_T3_PLAN_1'
        with self.assertRaisesRegex(ValueError, 'Y10'):
            policy.verify_machine_rules(entry, rules, expected)
        effective['textStyleName'] = '_TCH_DIM'
        effective['textStyleDefinition']['widthFactor'] = 0.8
        with self.assertRaisesRegex(ValueError, 'Y10'):
            policy.verify_machine_rules(entry, rules, expected)
        effective['textStyleDefinition']['widthFactor'] = 1.0
        effective['textStyleReferencePolicy']['explicitTchDim']['baseTextStyle'] = '_TCH_DIM_PLAN_1'
        with self.assertRaisesRegex(ValueError, 'Y10'):
            policy.verify_machine_rules(entry, rules, expected)
        effective['textStyleReferencePolicy']['explicitTchDim']['baseTextStyle'] = '_TCH_DIM'
        effective['textStyleReferencePolicy']['verifyStyleTableAfterReopen'] = False
        with self.assertRaisesRegex(ValueError, 'Y10'):
            policy.verify_machine_rules(entry, rules, expected)
        row = next(row for row in master['rules'] if row['id'] == 'Y10')
        self.assertIn('RULE Y10 active current-plan-final-readback ', policy.unified_catalog(master, 'A' * 64))
        self.assertIs(row['execution']['nativeImplementationVerified'], False)

    def test_source_dimension_policy_rejects_reuse_and_old_rule_evidence(self):
        master = policy.read_json(policy.MASTER)
        entry = policy.read_json(policy.ENTRY)
        self.assertEqual(entry['dimensions']['sourceDimensionPolicy']['outputActions'], ['create', 'replace'])
        for field in ('allowSourceObjectReuse', 'allowSourceStyleReuse'):
            entry['dimensions']['sourceDimensionPolicy'][field] = True
            with self.assertRaisesRegex(ValueError, 'D30'):
                policy.verify_machine_rules(entry, policy.master_rules(master), master['dimensionStylePolicy'])
            entry['dimensions']['sourceDimensionPolicy'][field] = False
        catalog = policy.unified_catalog(master, 'A' * 64)
        self.assertIn('RULE G06 active changed ', catalog)
        self.assertIn('RULE D22 active template-rule-pending-runtime ', catalog)
        for rule_id in ('D26', 'D30', 'Q03', 'TL08', 'Y04', 'TL09'):
            self.assertIn(f'RULE {rule_id} active scope-policy-pending-runtime ', catalog)

    def test_template_fingerprint_rejects_replaced_dwg(self):
        template = self.resources / 'annotation-template/平面标注.dwg'
        template.write_bytes(template.read_bytes() + b'changed')
        with self.assertRaisesRegex(ValueError, 'Template DWG fingerprint'):
            policy.verify(self.resources)

    def test_source_style_override_and_legacy_frame_policy_rejected(self):
        entry = policy.read_json(policy.ENTRY)
        master = policy.read_json(policy.MASTER)
        entry['annotationTemplatePolicy']['operations']['allowSourceStyleOverwrite'] = True
        with self.assertRaisesRegex(ValueError, 'template policy'):
            policy.verify_machine_rules(entry, policy.master_rules(master), master['dimensionStylePolicy'])
        entry = policy.read_json(policy.ENTRY)
        entry['controlFrames']['countPerPlan'] = 0
        with self.assertRaisesRegex(ValueError, 'frame'):
            policy.verify_machine_rules(entry, policy.master_rules(master), master['dimensionStylePolicy'])

    def test_includes_current_plan_workflow(self):
        manifest = policy.verify(self.resources)
        self.assertIn('plan-automation-skill/references/project-parameters.md', manifest['files'])
        rules = json.loads((self.resources / 'plan-policy/rules.json').read_text('utf-8'))
        self.assertEqual(rules['currentRuleRevision'], policy.REVISION)

    def test_storey_default_only_from_reviewed_b04(self):
        master = policy.read_json(policy.MASTER)
        self.assertIn('PARAM B04 storey-height-mm 3300', policy.unified_catalog(master, 'B' * 64))
        row = next(row for row in master['rules'] if row['id'] == 'B04')
        row['rule'] += ' 增加未经接入的适用条件。'
        catalog = policy.unified_catalog(master, 'B' * 64)
        self.assertIn('RULE B04 active changed ', catalog)
        self.assertNotIn('PARAM B04 storey-height-mm', catalog)

    def test_stale_master_cannot_pass(self):
        path = self.resources / 'plan-policy/all-plan-rules.json'
        path.write_text('{}', 'utf-8')
        with self.assertRaisesRegex(ValueError, 'Stale policy'):
            policy.verify(self.resources)

    def test_stale_executable_parameters_cannot_pass(self):
        path = self.resources / 'plan-policy/execution.tsv'
        path.write_text('BCPPLANPOLICY1 18 11 8 8 8 8 END', 'ascii')
        with self.assertRaisesRegex(ValueError, 'Stale policy'):
            policy.verify(self.resources)

    def test_executable_perimeter_selection_replaces_legacy_fields(self):
        directory = self.resources / 'plan-policy'
        perimeter = json.loads((directory / 'rules.json').read_text('utf-8'))['perimeter']
        self.assertNotIn('datum', perimeter)
        self.assertNotIn('automaticOutwardShiftForProjectionEnvelope', perimeter)
        self.assertEqual(perimeter['sidePlacement']['confirmedRoof'], 'EAVE_IF_INTERSECTS')
        words = (directory / 'execution.tsv').read_text('ascii').split()
        self.assertEqual(words[0], 'BCPPLANPOLICY3')
        self.assertEqual(words[-5:], ['PERIMETER', 'WALL_OR_OUTER_COLUMN',
                                      'EAVE_IF_INTERSECTS', 'KEEP_AXIS_FOR_STEPS', 'END'])

    def test_verifier_rejects_a_reintroduced_legacy_datum(self):
        path = self.resources / 'plan-policy/rules.json'
        data = json.loads(path.read_text('utf-8'))
        data['perimeter']['datum'] = '旧墙柱轴基准'
        path.write_text(json.dumps(data), 'utf-8')
        with self.assertRaisesRegex(ValueError, 'Retired or stale perimeter'):
            policy.verify(self.resources)

    def test_stale_skill_hash_cannot_pass(self):
        path = self.resources / 'plan-policy/rules.json'
        data = json.loads(path.read_text('utf-8'))
        data['skill']['sourceHashes']['references/plan.md'] = '0' * 64
        path.write_text(json.dumps(data), 'utf-8')
        with self.assertRaisesRegex(ValueError, 'Skill source hash stale'):
            policy.verify(self.resources)

    def test_installed_annotation_covers_a31_and_a32(self):
        master = json.loads(policy.MASTER.read_text('utf-8-sig'))
        text = (self.resources / 'plan-policy/current-plan-rules.md').read_text('utf-8')
        for alias, source in (('PD16', 'D27'), ('PA22', 'A29'), ('PA23', 'A30'),
                              ('PA24', 'A14'), ('PA25', 'A31'), ('PA26', 'A32')):
            rule = next(row['rule'] for row in master['rules'] if row['id'] == source)
            self.assertIn(f'{alias}  {rule}〔P14 {source}〕', text)

    def test_stale_clause_fails_even_with_updated_file_fingerprint(self):
        directory = self.resources / 'plan-policy'
        path = directory / 'current-plan-rules.md'
        text = path.read_text('utf-8')
        self.assertIn('PA26  ', text)
        changed = '\n'.join(line.replace('0.01 mm', '0.1 mm') if line.startswith('PA26  ')
                            else line for line in text.split('\n'))
        self.assertNotEqual(text, changed)
        path.write_text(changed, 'utf-8')
        manifest_path = directory / 'policy-sources.json'
        manifest = json.loads(manifest_path.read_text('utf-8'))
        manifest['files'][path.name] = policy.sha(path)
        manifest_path.write_text(json.dumps(manifest), 'utf-8')
        with self.assertRaisesRegex(ValueError, 'PA26.*A32'):
            policy.verify(self.resources)

    def test_staged_machine_tolerances_come_from_master(self):
        path = self.resources / 'plan-policy/rules.json'
        data = json.loads(path.read_text('utf-8'))
        rule = data['axes']['crossFloorPreissueVerification']
        self.assertEqual(rule['registrationMaxResidualMM'], 1)
        self.assertEqual(rule['drawingAxisPositionMaxDeviationMM'], .01)
        rule['drawingAxisPositionMaxDeviationMM'] = .1
        path.write_text(json.dumps(data), 'utf-8')
        with self.assertRaisesRegex(ValueError, 'Stale A32 execution tolerance'):
            policy.verify(self.resources)

    def test_staged_axis_circle_spacing_comes_from_a14(self):
        path = self.resources / 'plan-policy/rules.json'
        data = json.loads(path.read_text('utf-8'))
        spacing = data['perimeter']['axisCircleCrowding']
        self.assertEqual(spacing['minimumEdgeClearancePaperMM'], 1)
        self.assertEqual(spacing['maximumLateralOffsetPaperMM'], 8)
        self.assertEqual(spacing['centerSpacingAtReferenceDiameterPaperMM'], 9)
        spacing['minimumEdgeClearancePaperMM'] = 2
        path.write_text(json.dumps(data), 'utf-8')
        with self.assertRaisesRegex(ValueError, 'Stale A14 execution spacing'):
            policy.verify(self.resources)

    def test_staged_projection_and_perimeter_rules_reject_stale_text(self):
        path = self.resources / 'plan-policy/rules.json'
        original = json.loads(path.read_text('utf-8'))
        for field, rule_id in [('axisLeadRule', 'A28'), ('perimeterRowsRule', 'D01'),
                               ('roofThirdRowRule', 'D27'), ('perimeterAcceptanceRule', 'QA10')]:
            with self.subTest(rule=rule_id):
                data = json.loads(json.dumps(original))
                data['planRuleBindings'][field] = '旧规则'
                path.write_text(json.dumps(data), 'utf-8')
                with self.assertRaisesRegex(ValueError, rule_id):
                    policy.verify(self.resources)

    def test_staged_standard_door_jamb_comes_from_tl11(self):
        path = self.resources / 'plan-policy/rules.json'
        data = json.loads(path.read_text('utf-8'))
        master = policy.master_rules(policy.read_json(policy.MASTER))
        rule = data['planRuleBindings']['standardDoorJambRule']
        self.assertEqual(rule, master['TL11'])
        for phrase in ('200 mm', '240 mm', '100 mm', '120 mm', '图上不补室内尺寸'):
            self.assertIn(phrase, rule)
        jambs = data['dimensions']['doorJambByWallThicknessMM']
        self.assertEqual(jambs, {
            '240': {'standardClearWidthMM': 120, 'preferredAdjustmentMM': 120},
            '200': {'standardClearWidthMM': 100, 'preferredAdjustmentMM': 100},
        })
        data['planRuleBindings']['standardDoorJambRule'] = rule.replace('图上不补室内尺寸', '图上补室内尺寸')
        path.write_text(json.dumps(data), 'utf-8')
        with self.assertRaisesRegex(ValueError, 'TL11'):
            policy.verify(self.resources)
        data['planRuleBindings']['standardDoorJambRule'] = rule
        for field in ('standardClearWidthMM', 'preferredAdjustmentMM'):
            with self.subTest(field=field):
                jambs['200'][field] = 120
                path.write_text(json.dumps(data), 'utf-8')
                with self.assertRaisesRegex(ValueError, 'TL11'):
                    policy.verify(self.resources)
                jambs['200'][field] = 100

    def test_staged_wall_height_uses_floor_difference_with_default(self):
        path = self.resources / 'plan-policy/rules.json'
        data = json.loads(path.read_text('utf-8'))
        self.assertEqual(data['planRuleBindings']['floorAndWallHeightRule'],
                         policy.master_rules(policy.read_json(policy.MASTER))['B04'])
        height = data['floorElevations']['wallHeightPolicy']
        self.assertEqual(height['defaultStoreyHeightM'], 3.3)
        self.assertEqual(height['basis'], 'upperFloorElevationMinusCurrentFloorElevation')
        height['defaultStoreyHeightM'] = 3.6
        path.write_text(json.dumps(data), 'utf-8')
        with self.assertRaisesRegex(ValueError, 'B04'):
            policy.verify(self.resources)

    def test_staged_column_dimension_rule_preserves_source_edges(self):
        path = self.resources / 'plan-policy/rules.json'
        data = json.loads(path.read_text('utf-8'))
        rule = data['planRuleBindings']['columnDimensionsRule']
        self.assertEqual(rule, policy.master_rules(policy.read_json(policy.MASTER))['D28'])
        for phrase in ('实际柱边', '两个真实测点', '测量用途'):
            self.assertIn(phrase, rule)
        data['planRuleBindings']['columnDimensionsRule'] = rule.replace('实际柱边', '柱的首个夹点')
        path.write_text(json.dumps(data), 'utf-8')
        with self.assertRaisesRegex(ValueError, 'D28'):
            policy.verify(self.resources)

    def test_staged_railing_upstand_spacing_matches_d29(self):
        path = self.resources / 'plan-policy/rules.json'
        data = json.loads(path.read_text('utf-8'))
        self.assertEqual(data['planRuleBindings']['railingUpstandPlanRule'],
                         policy.master_rules(policy.read_json(policy.MASTER))['D29'])
        spacing = data['floorDetailVerification']['railingUpstandPlanSpacingMM']
        self.assertEqual(spacing, {'240': [80, 80, 80], '200': [60, 80, 60]})
        spacing['200'] = [80, 80, 40]
        path.write_text(json.dumps(data), 'utf-8')
        with self.assertRaisesRegex(ValueError, 'D29'):
            policy.verify(self.resources)

    def test_staged_exterior_step_defaults_match_c20(self):
        path = self.resources / 'plan-policy/rules.json'
        data = json.loads(path.read_text('utf-8'))
        self.assertEqual(data['planRuleBindings']['exteriorStepDefaultsRule'],
                         policy.master_rules(policy.read_json(policy.MASTER))['C20'])
        defaults = data['floorDetailVerification']['exteriorStepDefaultsMM']
        self.assertEqual(defaults, {'main': {'exposedTreadCount': 2, 'treadDepth': 300},
                                    'other': {'platformDepth': 1000, 'exposedTreadCount': 2,
                                              'treadDepth': 260}})
        defaults['other']['platformDepth'] = 900
        path.write_text(json.dumps(data), 'utf-8')
        with self.assertRaisesRegex(ValueError, 'C20'):
            policy.verify(self.resources)

if __name__ == '__main__':
    unittest.main()
