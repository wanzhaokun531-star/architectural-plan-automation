"""Regressions for real TCH filled ticks and staggered short labels."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from check_plan_print_ink import pdf
from joint_annotation_slots import chain_paper_paths, shifted_chain_paths, pack_slots


class PaperGeometryTests(unittest.TestCase):
    def test_template_shx_cap_outline_matches_without_changing_clearance(self):
        from check_plan_print_ink import match_number, READ_GAP
        # Four actual 600-dpi simplex glyph contours from the template-native 2780.
        box = pdf.Rect(1037.280029296875, 603.239990234375, 1044.1199951171875, 620.0400390625)
        frame = {'ratio': .01, 'modelCenter': [54000,145700], 'paperOffset': [-27,0]}
        target = {'vertical':True,'digits':4,'layer':'PUB_DIM','height':297.5,
                  'anchor':[63716.65351936643,145142.35870729788], 'number':'2780'}
        page = type('Page', (), {'rect':pdf.Rect(0,0,594*72/25.4,420*72/25.4)})()
        import math
        x,y=(box.x0+box.x1)/2,(box.y0+box.y1)/2
        key=(True,4,'PUB_DIM',math.floor(x/4),math.floor(y/4))
        self.assertIsNotNone(match_number({key:[(box,{5325,5326,5327,5328},None)]},target,frame,page))
        self.assertIsNone(match_number({(*key[:2],'家具',*key[3:]):[(box,{5325},None)]},target,frame,page))
        self.assertEqual(READ_GAP,.12)

    def fixture(self):
        a,b,c,d=map(pdf.Point,[(757.08,446.52),(757.92,447.24),(759.96,443.64),(760.68,444.48)])
        mark={'type':'fs','layer':'PUB_DIM','items':[('l',a,b),('l',b,c),('l',c,a),
            ('l',b,c),('l',c,d),('l',d,b)],'width':0.,'rect':pdf.Rect(757.08,443.64,760.68,447.24),'seqno':1}
        chain={'rows':[{'layer':'PUB_DIM'}],'normal':1,'along':0,'baseline':445.197,
               'stations':[759.133],'lines':[(pdf.Point(759.133,491.969),pdf.Point(759.133,438.110))]}
        line={'type':'s','layer':'PUB_DIM','items':[('l',pdf.Point(758.88,492.24),pdf.Point(758.88,438.36))],
              'width':.72,'rect':pdf.Rect(758.88,438.36,758.88,492.24),'seqno':2}
        return mark,line,chain

    def test_actual_filled_mark_moves_with_baseline_and_keeps_fill(self):
        mark,line,chain=self.fixture()
        bound=chain_paper_paths([mark,line],chain)
        self.assertEqual(len(bound),2)
        moved=shifted_chain_paths(bound,chain,-12)
        self.assertEqual(moved[0]['type'],'fs')
        self.assertAlmostEqual(moved[0]['rect'].y0,431.64)
        self.assertEqual(moved[1]['width'],.72)
        self.assertEqual(moved[1]['items'][0][1],line['items'][0][1])
        self.assertAlmostEqual(moved[1]['items'][0][2].y,426.36)

    def test_same_layer_is_not_enough_to_claim_ink(self):
        mark,line,chain=self.fixture()
        self.assertEqual(chain_paper_paths([{**mark,'layer':'家具'}],chain),[])
        moved=shifted_chain_paths([(mark,'tick')],chain,20)
        self.assertEqual(chain_paper_paths(moved,chain),[])
        self.assertEqual(chain_paper_paths([mark],{**chain,'stations':[770]}),[])

    def test_staggered_rows_can_overhang_without_reversing_same_row(self):
        a=(False,0.,pdf.Rect(10,0,20,4),(0,0))
        b=(False,0.,pdf.Rect(9,6,19,10),(0,0))
        c=(False,0.,pdf.Rect(-5,0,5,4),(0,0))
        self.assertIsNotNone(pack_slots([[a],[b]],[10,20],0))
        self.assertIsNone(pack_slots([[a],[c]],[10,20],0))


if __name__=='__main__':
    unittest.main()
