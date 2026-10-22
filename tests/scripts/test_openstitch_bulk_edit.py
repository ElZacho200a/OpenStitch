"""Non regression des lots de retouche agent (pas de binaire necessaire)."""
import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

SCRIPT=Path(__file__).resolve().parents[2]/'scripts/openstitch_bulk_edit.py'
spec=importlib.util.spec_from_file_location('bulk',SCRIPT)
mod=importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


class BulkTest(unittest.TestCase):
    def setUp(self):
        self.objects=[{'id':5,'type':'tatami','color':'#FFAA00','region_area_mm2':10,'locked':False},
                      {'id':6,'type':'tatami','color':'#FFAA00','region_area_mm2':20,'locked':True},
                      {'id':7,'type':'running','color':'#001122','region_area_mm2':30,'locked':False}]

    def test_selection_and_locked(self):
        got=mod.select(self.objects,kind='tatami',color='#ffaa00',min_area=5,max_area=100,ids=None)
        self.assertEqual([x['id'] for x in got],[5])

    def test_params_as_native_batch(self):
        got=mod.make_ops([self.objects[0]],'set_params',{'row_spacing_mm':.45})
        self.assertEqual(got,[{'op':'set_params','object':5,'params':{'row_spacing_mm':.45}}])

    def test_auto_satin_existing_and_new(self):
        existing={**self.objects[0],'type':'auto_satin'}
        ops=mod.make_ops([existing,self.objects[0]],'auto_satin',{'strategy':'skeleton'})
        self.assertEqual([x['op'] for x in ops],['set_params','auto_satin'])

    def test_refuse_mass_change_and_preserve_source(self):
        with tempfile.TemporaryDirectory() as d:
            original=Path(d)/'source.osp'; original.write_text('SOURCE')
            with patch.object(mod,'run',return_value={'schema':'openstitch.project-inspect/1',
                                                       'embroidery_objects':self.objects}):
                with self.assertRaisesRegex(RuntimeError,'max-objects'):
                    mod.edit('/cli',original,Path(d)/'out.osp',kind=None,color=None,
                             min_area=None,max_area=None,ids=None,max_objects=1,
                             operation='set_params',params={'angle_deg':45},commit=True)
            self.assertEqual(original.read_text(),'SOURCE')

    def test_failed_dry_run_does_not_write_osp(self):
        with tempfile.TemporaryDirectory() as d:
            original=Path(d)/'source.osp'; original.write_text('SOURCE')
            dest=Path(d)/'new.osp'
            with patch.object(mod,'run',side_effect=[
                 {'schema':'openstitch.project-inspect/1','embroidery_objects':self.objects},
                 {'ok':False,'outcomes':[{'ok':False,'error':{'code':'E_WRONG_STITCH_TYPE'}}]}]):
                res=mod.edit('/cli',original,dest,kind='tatami',color=None,min_area=None,
                             max_area=None,ids=None,max_objects=20,operation='set_params',
                             params={'bad_param':45},commit=True)
            self.assertFalse(res['ok'])
            self.assertFalse(dest.exists())
            self.assertEqual(original.read_text(),'SOURCE')


if __name__=='__main__':
    unittest.main()
