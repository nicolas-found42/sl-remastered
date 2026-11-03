"""Owned protocol guards only; generated rows are not Stock observations."""
import copy
import hashlib
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

class GeneratedNativeResponseTest(unittest.TestCase):
    """Generated protocol guards; these rows are not Stock observations."""
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        rows = [copy.deepcopy(replay.control.STORED_NATIVE_INSTALLATION)]
        rows += [dict(event='initial_global', name=name, image_base=0x400000,
                      rva=rva, size=size, valid=1, words=[0] * (size // 4))
                 for name, (rva, size) in replay.control.INITIAL.items()]
        for seq, lane, entry, returned in [(1, 0, 1, 3), (2, 1, 2, 5)]:
            rows.append(dict(event='api_return', gap_reason=0, seq=seq, return_seq=seq,
                             entry_phase=entry, return_phase=returned, thread=21+lane,
                             lane=lane, lane_read=1, role='unresolved', api='GetTickCount',
                             caller=0x554eb5, caller_rva=0x154eb5, arg=0, result=100+seq,
                             last_error_before=7, last_error_after=8, output_valid=0,
                             output_u32=[0, 0], cw=[0x37f, 0x37f], mxcsr=[0x1f80, 0x1f80]))
        self.native = []
        for ordinal, phase, lane, read, bits in [(1, 4, 0, 2, 0x80000000), (2, 6, 2, 1, 1)]:
            row = dict(event='native_clock_return', native='java.lang.System.currentTime()F',
                       ordinal=ordinal, phase=phase, thread=21+lane, lane=lane, lane_read=read,
                       role='unresolved', caller=0x420090, callee=0x47bfe0,
                       value_f32_bits=bits, last_error=17, fxsave_hex='a5'*512)
            self.native.append(row)
            rows.append(row)
        rows += [dict(event='native_clock_status', returns=2, phase_source='shared_next_phase',
                      scope='return_only_not_replay'),
                 dict(event='status', format_version=9, entered=2, returned=2, phases=6,
                      active=0, pending=0, dropped=0, gaps=0, accepting=0,
                      mode='record_only', early_startup='main_iat_from_proxy_attach')]
        self.path = Path(self.temp.name) / 'generated-native.jsonl'
        self.path.write_text(''.join(json.dumps(row)+'\n' for row in rows))
        self.digest = hashlib.sha256(self.path.read_bytes()).hexdigest()

    def native_request(self, row):
        return dict(source_sha256=self.digest, capture_lane=row['lane'], lane_read=row['lane_read'],
                    native=row['native'], caller=row['caller'], callee=row['callee'],
                    ordinal=row['ordinal'], phase=row['phase'])

    def api_request(self, lane):
        return dict(capture_lane=lane, lane_read=1, api='GetTickCount', caller_rva=0x154eb5,
                    argument=None, last_error=7, cw=0x37f, mxcsr=0x1f80)

    def test_mixed_phases_return_exact_native_bits_including_native_only_lane(self):
        program = replay.RecordedProgram.from_tape(self.path)
        self.assertEqual(program.describe()['capture_lanes'], [0, 1, 2])
        session = program.new_session({'a': 0, 'b': 1, 'native_only': 2})
        a = session.enter('a', self.api_request(0))
        b = session.enter('b', self.api_request(1))
        self.assertEqual(session.leave('a', a)['eax'], 101)
        first = session.native_return('a', self.native_request(self.native[0]))
        self.assertEqual(first, dict(value_f32_bits=0x80000000, last_error=17, fxsave_hex='a5'*512))
        self.assertEqual(session.leave('b', b)['eax'], 102)
        second = session.native_return('native_only', self.native_request(self.native[1]))
        self.assertEqual(second['value_f32_bits'], 1)
        finished = session.finish()
        self.assertEqual((finished['consumed_calls'], finished['consumed_native_returns'],
                          finished['consumed_phases']), (2, 2, 6))
        self.assertFalse(finished['process_replay_qualified'])
        self.assertFalse(finished['scheduler_replay_qualified'])

    def at_native(self):
        session = replay.RecordedProgram.from_tape(self.path).new_session({'a': 0, 'b': 1, 'native_only': 2})
        a = session.enter('a', self.api_request(0))
        b = session.enter('b', self.api_request(1))
        session.leave('a', a)
        return session, b

    def test_native_request_identity_consumption_and_shape_mismatches_latch(self):
        row = self.native[0]
        for key, value in [('source_sha256', '0'*64), ('capture_lane', 1), ('lane_read', 1),
                           ('native', 'invented()F'), ('caller', 0x420091), ('callee', 0x47bfe1),
                           ('ordinal', 2), ('phase', 6), ('ordinal', True)]:
            session, _ = self.at_native()
            request = self.native_request(row)
            request[key] = value
            with self.subTest(key=key, value=value):
                with self.assertRaises(replay.ReplayMismatch):session.native_return('a', request)
                with self.assertRaises(replay.ReplayMismatch):session.native_return('a', self.native_request(row))
                with self.assertRaises(replay.ReplayMismatch):session.finish()
        for mutation in ('missing', 'extra', 'mixed_keys', 'cycle'):
            session, _ = self.at_native()
            request = self.native_request(row)
            if mutation == 'missing':del request['caller']
            elif mutation == 'extra':request['invented'] = 1
            elif mutation == 'mixed_keys':request[1] = 'bad'
            else:request['caller'] = request
            with self.subTest(mutation=mutation), self.assertRaises(replay.ReplayMismatch):
                session.native_return('a', request)

    def test_native_cannot_skip_API_phases_or_use_wrong_lane(self):
        program = replay.RecordedProgram.from_tape(self.path)
        for token in ('a', 'b', 'native_only', 'unknown'):
            session = program.new_session({'a': 0, 'b': 1, 'native_only': 2})
            with self.subTest(at='API', token=token), self.assertRaises(replay.ReplayMismatch):
                session.native_return(token, self.native_request(self.native[0]))
        for token in ('b', 'native_only', 'unknown'):
            session, _ = self.at_native()
            with self.subTest(at='Native', token=token), self.assertRaises(replay.ReplayMismatch):
                session.native_return(token, self.native_request(self.native[0]))
        session, _ = self.at_native()
        with self.assertRaises(replay.ReplayMismatch):session.enter('a', self.api_request(0))

    def test_native_only_lane_must_have_explicit_binding(self):
        program = replay.RecordedProgram.from_tape(self.path)
        with self.assertRaises(replay.ReplayMismatch):program.new_session({'a': 0, 'b': 1})

    def test_unused_native_value_or_exhaustion_never_falls_back(self):
        session, b = self.at_native()
        session.native_return('a', self.native_request(self.native[0]))
        session.leave('b', b)
        with self.assertRaises(replay.ReplayMismatch):session.finish()
        with self.assertRaises(replay.ReplayMismatch):
            session.native_return('native_only', self.native_request(self.native[1]))
        session, b = self.at_native()
        session.native_return('a', self.native_request(self.native[0]))
        session.leave('b', b)
        session.native_return('native_only', self.native_request(self.native[1]))
        self.assertEqual(session.finish()['consumed_native_returns'], 2)
        with self.assertRaises(replay.ReplayMismatch):
            session.native_return('native_only', self.native_request(self.native[1]))

    def test_foreign_native_thread_latches_before_consumption(self):
        session, _ = self.at_native()
        errors = []
        def foreign():
            try:session.native_return('a', self.native_request(self.native[0]))
            except replay.ReplayMismatch as error:errors.append(str(error))
        worker = threading.Thread(target=foreign)
        worker.start();worker.join(timeout=5)
        self.assertFalse(worker.is_alive());self.assertEqual(len(errors), 1)
        with self.assertRaises(replay.ReplayMismatch):
            session.native_return('a', self.native_request(self.native[0]))

    def test_native_reply_mutation_does_not_change_another_session(self):
        session, _ = self.at_native()
        first = session.native_return('a', self.native_request(self.native[0]))
        first['value_f32_bits'] = 0
        first['fxsave_hex'] = ''
        session, _ = self.at_native()
        second = session.native_return('a', self.native_request(self.native[0]))
        self.assertEqual(second, dict(value_f32_bits=0x80000000, last_error=17, fxsave_hex='a5'*512))


if __name__=='__main__':unittest.main(verbosity=2)
