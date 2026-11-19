import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from design_sources import adoption_bindings, build_manifest, digest, final_audit_request, final_boundary_replacements, skp_instance_size, verify_final_sources


class DesignSourcesTests(unittest.TestCase):
    def test_rotated_scaled_mirrored_instance_measures_local_axes(self):
        # Local X maps to world -Y with scale 2, local Z maps to +Z scale 3.
        geometry={'instances':[{'path':[10], 'visible':True,
            'world_transform_inches':[0,-2,0,0,1,0,0,0,0,0,3,0,0,0,0,1]}],
            'edges':[{'path':[10,20], 'visible':True,
                      'points_mm':[[0,0,0],[0,-2000,6000]]}]}
        self.assertEqual(skp_instance_size(geometry,[10],0,2),[2000,6000])

    def test_sheared_instance_not_given_false_width(self):
        geometry={'instances':[{'path':[10], 'visible':True,
            'world_transform_inches':[1,0,0,0,1,1,0,0,0,0,1,0,0,0,0,1]}],'edges':[]}
        with self.assertRaisesRegex(ValueError,'sheared'):
            skp_instance_size(geometry,[10],0,2)

    def test_embedded_plan_symbol_cannot_supply_door_height(self):
        geometry={'instances':[{'path':[10], 'visible':True,
            'world_transform_inches':[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]}],
            'edges':[{'path':[10], 'visible':True,'points_mm':[[0,0,0],[1800,240,0]]}]}
        with self.assertRaisesRegex(ValueError,'vertical 3D'):
            skp_instance_size(geometry,[10],0,1)

    def fixture(self, directory):
        root=Path(directory);source=root/'plan.dwg';source.write_bytes(b'independent source fixture')
        schedule=root/'openings.csv'
        schedule.write_text('id,width,height,unit,dimensionKind\nW1,1.5,1.8,m,rough-opening\n',encoding='utf-8')
        config={'schema':'plan.design-adoptions.v1','sourceSHA256':digest(source),
            'sources':{'table':{'path':schedule.name,'sha256':digest(schedule)}},
            'openings':[{'handle':'abc','decision':'adopt','basis':'provided table W1',
              'dimensionKind':'rough-opening','source':'table','record':'W1','anchor':'CENTER','prefix':'LC'}],
            'walls':[{'handle':'123','role':'BOUNDARY','basis':'provided wall purpose'}]}
        return source,schedule,root/'source.json',config,root/'native.tsv'

    def test_schedule_has_source_fingerprint_and_real_unit_conversion(self):
        with tempfile.TemporaryDirectory() as directory:
            source,schedule,adoption,config,out=self.fixture(directory)
            adoption.write_text(json.dumps(config),encoding='utf-8')
            receipt=build_manifest(source,adoption,out)
            self.assertEqual(receipt['pending'],[])
            self.assertIn('OPENING ABC 1500 1800 CENTER LC table',out.read_text(encoding='utf-8'))
            self.assertIn('WALL 123 BOUNDARY',out.read_text(encoding='utf-8'))
            self.assertEqual(receipt['sourceSHA256'],digest(source))

    def test_ambiguous_schedule_record_is_visible_pending_and_not_written(self):
        with tempfile.TemporaryDirectory() as directory:
            source,schedule,adoption,config,out=self.fixture(directory)
            with schedule.open('a',encoding='utf-8') as stream:stream.write('W1,2,2,m,rough-opening\n')
            config['sources']['table']['sha256']=digest(schedule)
            adoption.write_text(json.dumps(config),encoding='utf-8')
            result=build_manifest(source,adoption,out)
            self.assertEqual(result['adopted'],[])
            self.assertIn('ambiguous',result['pending'][0]['reason'])
            self.assertNotIn('OPENING ABC',out.read_text(encoding='utf-8'))

    def test_two_sources_for_same_opening_neither_is_silently_adopted(self):
        with tempfile.TemporaryDirectory() as directory:
            source,_,adoption,config,out=self.fixture(directory)
            config['openings'].append(dict(config['openings'][0], basis='a competing source'))
            adoption.write_text(json.dumps(config),encoding='utf-8')
            result=build_manifest(source,adoption,out)
            self.assertEqual(result['adopted'],[])
            self.assertNotIn('OPENING ABC',out.read_text('utf-8'))
            self.assertTrue(any('multiple adopted source' in x['reason'] for x in result['pending']))

    def test_changed_source_file_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            source,schedule,adoption,config,out=self.fixture(directory)
            adoption.write_text(json.dumps(config),encoding='utf-8')
            schedule.write_text('changed',encoding='utf-8')
            with self.assertRaisesRegex(ValueError,'fingerprint'):
                build_manifest(source,adoption,out)

    def test_native_manifest_locks_every_nested_design_file(self):
        with tempfile.TemporaryDirectory() as directory:
            source,schedule,adoption,config,out=self.fixture(directory)
            adoption.write_text(json.dumps(config),encoding='utf-8')
            build_manifest(source,adoption,out)
            self.assertEqual(set(adoption_bindings(out,source)), {str(p.resolve()) for p in (out,adoption,schedule)})
            schedule.write_text('altered',encoding='utf-8')
            with self.assertRaisesRegex(ValueError,'evidence changed'):
                adoption_bindings(out,source)

    def test_final_audit_does_not_turn_failed_adoption_into_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);drawing=root/'final.dwg';drawing.write_bytes(b'final')
            prior={'schema':'plan.native-design-readback.v1','savedReopened':True,
                'sourceSHA256':'A'*64,'adoptionSHA256':'B'*64,'files':[],
                'openings':[{'object':'ABC','widthMM':1500,'heightMM':1800,
                    'expectedPoints':[[0,0,0],[1500,0,0]],'nativeNumber':'LC1518',
                    'source':'table','locator':'record:W1','basis':'provided file','anchor':'START',
                    'prefix':'LC','passed':False,'reason':'native property was not writable'}]}
            target=root/'audit.tsv';final_audit_request(prior,drawing,target)
            self.assertIn('1500 1800 0 0 1500 0 0 "LC1518"',target.read_text('utf-8'))
            self.assertIn(digest(drawing),target.read_text('utf-8'))

    def test_final_readback_exhaustion_is_p06_and_does_not_block_other_items(self):
        from owned_plan_host import JobError, RetryableHostError
        with tempfile.TemporaryDirectory() as directory:
            source,_,adoption,config,manifest=self.fixture(directory)
            adoption.write_text(json.dumps(config),encoding='utf-8');build_manifest(source,adoption,manifest)
            root=Path(directory);drawing=root/'final.dwg';drawing.write_bytes(b'final');module=root/'native.arx';module.write_bytes(b'module')
            prior={'schema':'plan.native-design-readback.v1','savedReopened':True,
                'sourceSHA256':digest(source),'adoptionSHA256':digest(manifest),'files':[],
                'openings':[{'object':'ABC','widthMM':1500,'heightMM':1800,
                    'expectedPoints':[[0,0,0],[1500,0,0]],'nativeNumber':'LC1518',
                    'source':'table','locator':'record:W1','basis':'provided file','anchor':'START',
                    'prefix':'LC','passed':True,'reason':''}]}
            (root/'design-source-readback.json').write_text(json.dumps(prior),encoding='utf-8')
            result={'drawing':str(drawing),'sha256':digest(drawing),'report':str(root),'unresolved':[],'formal':True}
            with patch('owned_plan_host.run_owned_plan',side_effect=RetryableHostError('native read timed out')) as run, patch('owned_plan_host.resume_owned_plan'):
                self.assertIsNone(verify_final_sources(root,result,root/'work',root,root,module,manifest))
            self.assertEqual(run.call_count,3)
            self.assertFalse(result['formal'])
            self.assertEqual(result['unresolved'][0]['role'],'design-source-execution')
            self.assertEqual(drawing.read_bytes(),b'final')
            result['unresolved']=[];result['formal']=True
            with patch('owned_plan_host.run_owned_plan',side_effect=JobError('unsupported native field')) as run, patch('owned_plan_host.resume_owned_plan'):
                self.assertIsNone(verify_final_sources(root,result,root/'deterministic',root,root,module,manifest))
            self.assertEqual(run.call_count,1)
            self.assertFalse(result['formal'])

    def test_boundary_length_tracks_verified_native_replacement_to_final_dwg(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);first=root/'first';last=root/'last';first.mkdir();last.mkdir()
            for folder,source,target,hash_ in ((first,'A','B','middle'),(last,'B','C','final')):
                (folder/'template-native-readback.json').write_text(json.dumps({'savedReopened':True,
                    'outputSHA256':hash_,'objects':[{'source':source,'kind':'dimension','targets':[target],'verified':True}]}),encoding='utf-8')
            prior={'boundaryWalls':['10'],'boundaryDimensions':[{'wall':'10','dimension':'A','passed':True}]}
            final={'history':[str(first),str(last)],'report':str(last),'sha256':'final'}
            rows,issues=final_boundary_replacements(prior,final)
            self.assertFalse(issues);self.assertEqual(rows[0]['finalDimension'],'C')
            (last/'template-native-readback.json').write_text(json.dumps({'savedReopened':True,'outputSHA256':'final','objects':[]}),encoding='utf-8')
            self.assertTrue(final_boundary_replacements(prior,final)[1])

    def test_clear_size_cannot_overwrite_rough_opening(self):
        with tempfile.TemporaryDirectory() as directory:
            source,_,adoption,config,out=self.fixture(directory)
            config['openings'][0]['dimensionKind']='clear-opening'
            adoption.write_text(json.dumps(config),encoding='utf-8')
            result=build_manifest(source,adoption,out)
            self.assertEqual(result['adopted'],[])
            self.assertIn('cannot be silently',result['pending'][0]['reason'])

    def test_missing_provided_skp_reader_is_not_silent_dwg_fallback(self):
        with tempfile.TemporaryDirectory() as directory:
            source,_,adoption,config,out=self.fixture(directory)
            model=Path(directory)/'model.skp';model.write_bytes(b'unsupported test fixture')
            config['sources']['table']={'path':model.name,'sha256':digest(model)}
            adoption.write_text(json.dumps(config),encoding='utf-8')
            result=build_manifest(source,adoption,out)
            self.assertEqual(len(result['pending']),2)
            self.assertEqual(result['adopted'],[])


if __name__=='__main__':unittest.main()
