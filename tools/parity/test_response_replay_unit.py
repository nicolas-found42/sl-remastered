"""Owned protocol guards only; generated rows are not Stock observations."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import threading
import unittest

spec=importlib.util.spec_from_file_location('unit_response_replay',Path(__file__).with_name('response_replay.py'))
replay=importlib.util.module_from_spec(spec);spec.loader.exec_module(replay)

class ResponseGuards(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        rows=[dict(event='initial_global',name=name,image_base=0x400000,rva=rva,size=size,valid=1,words=[0]*(size//4))
              for name,(rva,size) in replay.control.INITIAL.items()]
        for seq,lane,caller,entry,returned in [(1,0,0x154eb5,1,3),(2,1,0x155278,2,4)]:
            rows.append(dict(event='api_return',gap_reason=0,seq=seq,return_seq=seq,entry_phase=entry,return_phase=returned,
                thread=21+lane,lane=lane,lane_read=1,role='unresolved',api='GetTickCount',caller=0x400000+caller,
                caller_rva=caller,arg=0,result=123+seq,last_error_before=7,last_error_after=8,
                output_valid=0,output_u32=[0,0],cw=[0x37f,0x37f],mxcsr=[0x1f80,0x1f80]))
        rows.append(dict(event='status',format_version=3,entered=2,returned=2,phases=4,active=0,pending=0,dropped=0,gaps=0,accepting=0,mode='record_only',early_startup='unobserved'))
        path=Path(self.temp.name)/'owned.jsonl';path.write_text(''.join(json.dumps(row)+'\n' for row in rows))
        self.program=replay.RecordedProgram.from_tape(path)
        self.session=self.program.new_session({'a':0,'b':1})
        self.requests=[dict(capture_lane=lane,lane_read=1,api='GetTickCount',caller_rva=caller,
                            argument=None,last_error=7,cw=0x37f,mxcsr=0x1f80)
                       for lane,caller in [(0,0x154eb5),(1,0x155278)]]

    def test_owned_overlap_requires_recorded_entry_entry_return_return(self):
        a=self.session.enter('a',self.requests[0]);b=self.session.enter('b',self.requests[1])
        self.assertEqual(self.session.leave('a',a)['eax'],124)
        self.assertEqual(self.session.leave('b',b)['eax'],125)
        result=self.session.finish();self.assertEqual(result['consumed_phases'],4)
        self.assertFalse(result['process_replay_qualified'])

    def test_reordered_known_lane_refuses(self):
        with self.assertRaises(replay.ReplayMismatch):self.session.enter('b',self.requests[1])
        with self.assertRaises(replay.ReplayMismatch):self.session.enter('a',self.requests[0])

    def test_early_return_is_not_a_schedule(self):
        a=self.session.enter('a',self.requests[0])
        with self.assertRaises(replay.ReplayMismatch):self.session.leave('a',a)

    def test_malformed_mixed_keys_and_cycles_latch(self):
        for mutation in ('mixed','cycle'):
            session=self.program.new_session({'a':0,'b':1});request=copy.deepcopy(self.requests[0])
            if mutation=='mixed':request[1]='bad'
            else:request['argument']=request
            with self.subTest(mutation=mutation),self.assertRaises(replay.ReplayMismatch):session.enter('a',request)
            with self.assertRaises(replay.ReplayMismatch):session.enter('a',self.requests[0])

    def test_wrong_argument_and_bool_machine_word_refuse(self):
        for key,value in [('argument',{}),('last_error',True),('cw',0),('api','input')]:
            session=self.program.new_session({'a':0,'b':1});request=copy.deepcopy(self.requests[0]);request[key]=value
            with self.subTest(key=key),self.assertRaises(replay.ReplayMismatch):session.enter('a',request)

    def test_foreign_actual_thread_refuses_before_consumption(self):
        outcomes=[]
        def foreign():
            try:self.session.enter('a',self.requests[0])
            except replay.ReplayMismatch as error:outcomes.append(str(error))
        thread=threading.Thread(target=foreign);thread.start();thread.join(timeout=5)
        self.assertFalse(thread.is_alive());self.assertEqual(len(outcomes),1)
        with self.assertRaises(replay.ReplayMismatch):self.session.finish()

    def test_incomplete_or_exhausted_stream_never_falls_back(self):
        with self.assertRaises(replay.ReplayMismatch):self.session.finish()
        with self.assertRaises(replay.ReplayMismatch):self.session.enter('a',self.requests[0])

    def test_lane_binding_is_explicit_and_bijective(self):
        for bindings in ({'a':0},{'a':0,'b':0},{'a':False,'b':1}):
            with self.subTest(bindings=bindings),self.assertRaises(replay.ReplayMismatch):self.program.new_session(bindings)

if __name__=='__main__':unittest.main(verbosity=2)
