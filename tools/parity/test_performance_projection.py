"""Counterfactual arithmetic/refusal controls; no invented Stock observations."""
import importlib.util
from pathlib import Path
import struct
import unittest

spec=importlib.util.spec_from_file_location('performance_projection',Path(__file__).with_name('performance_projection.py'))
projection=importlib.util.module_from_spec(spec);spec.loader.exec_module(projection)

def words(value):return [value&0xffffffff,(value>>32)&0xffffffff]

class ProjectionTest(unittest.TestCase):
    def setUp(self):
        self.initial={'engine_frequency':words(10000000),'engine_qpc_epoch':words(123456789012345)}
        self.epoch=projection.signed_words(self.initial['engine_qpc_epoch'])
    def request(self,site):return dict(api='QueryPerformanceFrequency' if site==0x1516cb else 'QueryPerformanceCounter',caller_rva=site,capture_lane=0,cw=0x37f)
    def response(self,value):return dict(eax=1,output_u32=words(value),cw=0x37f)
    def ready(self):
        model=projection.EngineClockProjection(self.initial)
        self.assertIsNone(model.reply(self.request(0x1516cb),self.response(10000000)))
        return model
    def test_counter_delta_reaches_seconds_bits(self):
        ms,bits=self.ready().reply(self.request(0x1516d6),self.response(self.epoch+10000000))
        self.assertEqual(ms,1000.0);self.assertEqual(bits,struct.unpack('<I',struct.pack('<f',1.0))[0])
    def test_counter_change_changes_consumed_projection(self):
        a=self.ready().reply(self.request(0x1516d6),self.response(self.epoch+2500000))
        b=self.ready().reply(self.request(0x1516d6),self.response(self.epoch+5000000))
        self.assertNotEqual(a,b)
    def test_missing_frequency_response_refuses(self):
        with self.assertRaises(ValueError):projection.EngineClockProjection(self.initial).reply(self.request(0x1516d6),self.response(self.epoch))
    def test_failed_or_nonpositive_frequency_refuses(self):
        for result in [dict(eax=0,output_u32=words(10000000),cw=0x37f),self.response(0),self.response(-1)]:
            with self.subTest(result=result),self.assertRaises(ValueError):projection.EngineClockProjection(self.initial).reply(self.request(0x1516cb),result)
    def test_unsupported_x87_modes_refuse(self):
        for cw in [0x77f,0x37e,0x17f]:
            model=self.ready();request=self.request(0x1516d6);request['cw']=cw;reply=self.response(self.epoch+1);reply['cw']=cw
            with self.subTest(cw=cw),self.assertRaises(ValueError):model.reply(request,reply)
    def test_changed_control_word_refuses(self):
        reply=self.response(self.epoch+1);reply['cw']=0x27f
        with self.assertRaises(ValueError):self.ready().reply(self.request(0x1516d6),reply)
    def test_boolean_counter_word_refuses(self):
        with self.assertRaises(ValueError):projection.signed_words([True,0])
    def test_signed_qword_is_not_unsigned_epoch(self):
        self.assertEqual(projection.signed_words(words(-1)),-1)
    def test_impossible_API_site_crosspairs_refuse(self):
        for api,site in [('QueryPerformanceFrequency',0x1516d6),('QueryPerformanceCounter',0x1516cb),('GetTickCount',0x1516cb)]:
            request=self.request(site);request['api']=api
            with self.subTest(api=api,site=site),self.assertRaises(ValueError):self.ready().reply(request,self.response(10000000))

if __name__=='__main__':unittest.main(verbosity=2)
