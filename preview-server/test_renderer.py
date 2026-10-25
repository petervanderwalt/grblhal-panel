"""Parser correctness checks independent of the pendant."""
import tempfile
import unittest
from pathlib import Path
import numpy as np
from renderer import geometry, render, PreviewError, WIDTH, HEIGHT
import struct

class GeometryTests(unittest.TestCase):
    def test_native_views(self):
        base=Path(__file__).resolve().parent.parent/'captures'/'renderer-tests'
        base.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=base) as directory:
            p=Path(directory);source=p/'in.nc';source.write_text('G1X10Y5Z2\n',encoding='ascii')
            updates=[]
            meta=render(source,p,progress=lambda **fields: updates.append(fields))
            self.assertEqual(updates[0], {"phase":"parsing", "percent":0})
            self.assertEqual({u["view"] for u in updates if u["phase"]=="rendering"},set(range(1,9)))
            self.assertTrue(all(0 <= u["percent"] <= 100 for u in updates))
            self.assertEqual(updates[-1]["percent"],100)
            self.assertEqual(len(meta['views']),8)
            for view in meta['views']:
                data=(p/(view+'.bin')).read_bytes()
                self.assertEqual(struct.unpack('<BBHHHHH',data[:12]),(0x19,0x12,0,WIDTH,HEIGHT,WIDTH*2,0))
                self.assertEqual(len(data),12+WIDTH*HEIGHT*2)
                self.assertTrue((p/(view+'.png')).is_file())
    def parse(self, code):
        base=Path(__file__).resolve().parent.parent/"captures"/"renderer-tests"
        base.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=base) as directory:
            p=Path(directory);(p/'in.nc').write_text(code,encoding='ascii')
            meta=geometry(p/'in.nc',p/'out.bin')
            points=np.fromfile(p/'out.bin',dtype='<f4').reshape(-1,7)
            return meta,points
    def test_modal_units_and_comments(self):
        meta,p=self.parse('G20 G90\nG1X1F10 (X99)\nG91\nX1\nG21\nX1\nM30\nX999\n')
        self.assertAlmostEqual(p[-1,3],51.8,places=3)
        self.assertEqual(meta['segments'],3)
    def test_arc_endpoint_and_planes(self):
        for plane,words,expected in [(17,'X10Y10I0J10',[10,10,0]),(18,'Z10X10K0I10',[10,0,10]),(19,'Y10Z10J0K10',[0,10,10])]:
            meta,p=self.parse(f'G21G90G{plane}\nG3{words}\n')
            np.testing.assert_allclose(p[-1,3:6],expected,atol=.001)
            self.assertGreater(meta['segments'],2)
    def test_radius_arc(self):
        _,p=self.parse('G21G90G17\nG2X10Y10R10\n')
        np.testing.assert_allclose(p[-1,3:6],[10,10,0],atol=.001)
    def test_unsupported_not_partial(self):
        for code in ['G81X1','G1X#1','G53G0X0','G1X1A20','G54\nG55\nG1X1','G2X10I1','G1X1X2']:
            with self.assertRaises(PreviewError): self.parse(code)
    def test_default_cut_and_laser_off(self):
        _,p=self.parse('G1X1\nM3S0\nX2\nS100\nX3\n')
        self.assertGreater(p[0,6],0)
        self.assertEqual(p[1,6],0)
        self.assertEqual(p[2,6],100)
if __name__=='__main__': unittest.main()
