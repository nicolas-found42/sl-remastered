"""Actual recorded-response consumption; never native Stock or schedule replay."""
import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import threading
import unittest

spec = importlib.util.spec_from_file_location('response_replay', Path(__file__).with_name('response_replay.py'))
replay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(replay)
CAPTURE = REPORT = None
EVIDENCE = {}


class ActualResponseReplayTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.raw, cls.digest = replay.control.read_rows(CAPTURE,128*1024**2,500000)
        cls.calls = {row['seq']:row for row in cls.raw if row['event']=='api_return'}
        cls.phases = sorted((phase,tag,row['seq']) for row in cls.calls.values()
                            for tag,phase in [('entry',row['entry_phase']),('return',row['return_phase'])])
        cls.base=next(row['image_base'] for row in cls.raw if row['event']=='initial_global')
        cls.program=replay.RecordedProgram.from_tape(CAPTURE)
        cls.bindings={f'owned_fixture_token_{i}':lane for i,lane in enumerate(sorted({r['lane'] for r in cls.calls.values()}))}
        cls.tokens={lane:token for token,lane in cls.bindings.items()}
        EVIDENCE.update(source_sha256=cls.digest,source=str(CAPTURE.resolve()),bindings=cls.bindings,
                        binding_scope='explicit owned harness tokens; native roles unresolved')

    def session(self):return self.program.new_session(self.bindings)

    def request(self,row):
        return dict(capture_lane=row['lane'],lane_read=row['lane_read'],api=row['api'],caller_rva=row['caller_rva'],
                    argument=replay.control.pointer(row['api'],row['caller_rva'],row['arg'],self.base),
                    last_error=row['last_error_before'],cw=row['cw'][0],mxcsr=row['mxcsr'][0])

    def drive(self,session,stop_before=None):
        handles={};emitted=hashlib.sha256()
        for ordinal,phase,identifier in self.phases:
            row=self.calls[identifier];token=self.tokens[row['lane']]
            if stop_before and stop_before(row,phase):return row,handles
            if phase=='entry':handles[identifier]=session.enter(token,self.request(row))
            else:
                output=bytearray(b'\x7e'*12) if row['output_valid'] else None
                result=session.leave(token,handles.pop(identifier),output)
                expected=dict(eax=row['result'],last_error=row['last_error_after'],
                              output_u32=row['output_u32'] if row['output_valid'] else None,
                              cw=row['cw'][1],mxcsr=row['mxcsr'][1])
                self.assertEqual(result,expected)
                if output is not None:
                    self.assertEqual(output[:8],struct.pack('<II',*row['output_u32']))
                    self.assertEqual(output[8:],b'\x7e'*4)
                    emitted.update(output[:8])
                emitted.update(struct.pack('<IIII',result['eax'],result['last_error'],result['cw'],result['mxcsr']))
        return session.finish(),emitted.hexdigest()

    def test_full_actual_tape_replies_and_owned_output_bytes_repeat(self):
        first_session=self.session()
        first,first_hash=self.drive(first_session);second,second_hash=self.drive(self.session())
        self.assertEqual(first,second);self.assertEqual(first_hash,second_hash)
        self.assertEqual(first['consumed_calls'],len(self.calls))
        self.assertEqual(first['consumed_phases'],len(self.phases))
        self.assertFalse(first['process_replay_qualified'])
        self.assertFalse(first['scheduler_replay_qualified'])
        EVIDENCE.update(full_consumption=first,emitted_reply_sha256=first_hash,offline_repeat_equal=True)
        row=self.calls[self.phases[0][2]]
        with self.assertRaises(replay.ReplayMismatch):first_session.enter(self.tokens[row['lane']],self.request(row))

    def test_explicit_exact_lane_bindings_required(self):
        for bad in ({},dict(self.bindings,extra=12345),{token:True for token in self.bindings}):
            with self.subTest(binding=bad),self.assertRaises(replay.ReplayMismatch):self.program.new_session(bad)

    def test_unknown_thread_token_latches_failure(self):
        session=self.session();row=self.calls[self.phases[0][2]]
        with self.assertRaises(replay.ReplayMismatch):session.enter('unobserved_native_thread',self.request(row))
        with self.assertRaises(replay.ReplayMismatch):session.enter(self.tokens[row['lane']],self.request(row))

    def test_known_but_wrong_lane_token_refuses(self):
        row=self.calls[self.phases[0][2]]
        wrong=next(token for token,lane in self.bindings.items() if lane!=row['lane'])
        with self.assertRaises(replay.ReplayMismatch):self.session().enter(wrong,self.request(row))

    def test_mixed_key_malformed_request_latches_failure(self):
        session=self.session();row=self.calls[self.phases[0][2]]
        malformed=self.request(row);malformed[1]='mixed key'
        with self.assertRaises(replay.ReplayMismatch):session.enter(self.tokens[row['lane']],malformed)
        with self.assertRaises(replay.ReplayMismatch):session.enter(self.tokens[row['lane']],self.request(row))

    def test_cyclic_request_latches_failure(self):
        session=self.session();row=self.calls[self.phases[0][2]]
        malformed=self.request(row);malformed['argument']=malformed
        with self.assertRaises(replay.ReplayMismatch):session.enter(self.tokens[row['lane']],malformed)
        with self.assertRaises(replay.ReplayMismatch):session.enter(self.tokens[row['lane']],self.request(row))

    def test_actual_foreign_harness_thread_refuses_and_latches(self):
        session=self.session();row=self.calls[self.phases[0][2]];outcome=[]
        def foreign():
            try:session.enter(self.tokens[row['lane']],self.request(row))
            except replay.ReplayMismatch as error:outcome.append(str(error))
        worker=threading.Thread(target=foreign);worker.start();worker.join(timeout=5)
        self.assertFalse(worker.is_alive());self.assertEqual(len(outcome),1)
        with self.assertRaises(replay.ReplayMismatch):session.enter(self.tokens[row['lane']],self.request(row))

    def test_latch_retains_first_failure(self):
        session=self.session();row=self.calls[self.phases[0][2]]
        with self.assertRaises(replay.ReplayMismatch) as first:session.enter('first unknown token',self.request(row))
        outcome=[]
        def foreign():
            try:session.finish()
            except replay.ReplayMismatch as error:outcome.append(str(error))
        worker=threading.Thread(target=foreign);worker.start();worker.join(timeout=5)
        self.assertFalse(worker.is_alive());self.assertEqual(outcome,[str(first.exception)])

    def test_request_shape_caller_argument_and_machine_state_mismatches_refuse(self):
        row=self.calls[self.phases[0][2]]
        for key,value in [('api','unsupported_input_stream'),('caller_rva',0),('argument',{'invented':1}),
                          ('last_error',row['last_error_before']^1),('cw',0),('mxcsr',0),('lane_read',True)]:
            request=self.request(row);request[key]=value
            with self.subTest(key=key),self.assertRaises(replay.ReplayMismatch):
                self.session().enter(self.tokens[row['lane']],request)

    def test_wrong_global_phase_cannot_publish_reply(self):
        session=self.session();row=self.calls[self.phases[0][2]]
        with self.assertRaises(replay.ReplayMismatch):session.leave(self.tokens[row['lane']],row['seq'])
        with self.assertRaises(replay.ReplayMismatch):session.finish()

    def test_output_buffer_refusal_preserves_bytes_and_latches(self):
        session=self.session()
        row,handles=self.drive(session,lambda row,phase:phase=='return' and row['output_valid'])
        buffer=bytearray(b'\x7e'*7)
        with self.assertRaises(replay.ReplayMismatch):session.leave(self.tokens[row['lane']],handles[row['seq']],buffer)
        self.assertEqual(buffer,b'\x7e'*7)
        with self.assertRaises(replay.ReplayMismatch):session.finish()

    def test_wrong_call_handle_refuses(self):
        session=self.session();row,handles=self.drive(session,lambda row,phase:phase=='return')
        with self.assertRaises(replay.ReplayMismatch):session.leave(self.tokens[row['lane']],handles[row['seq']]+1)

    def test_scalar_response_refuses_unrecorded_buffer_effect(self):
        session=self.session();row,handles=self.drive(session,lambda row,phase:phase=='return' and not row['output_valid'])
        buffer=bytearray(b'\x7e'*8)
        with self.assertRaises(replay.ReplayMismatch):session.leave(self.tokens[row['lane']],handles[row['seq']],buffer)
        self.assertEqual(buffer,b'\x7e'*8)

    def test_unconsumed_actual_tape_refuses_completion(self):
        with self.assertRaises(replay.ReplayMismatch):self.session().finish()

    def test_phase_less_recording_refuses_instead_of_inventing_schedule(self):
        row=copy.deepcopy(self.calls[self.phases[0][2]])
        row.update(seq=1,return_seq=1,lane_read=1)
        row.pop('entry_phase');row.pop('return_phase')
        initial=[copy.deepcopy(r) for r in self.raw if r['event']=='initial_global']
        status=dict(event='status',format_version=2,entered=1,returned=1,active=0,pending=0,dropped=0,
                    gaps=0,accepting=0,mode='record_only',early_startup='unobserved')
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'without-phases.jsonl'
            path.write_text(''.join(json.dumps(r)+'\n' for r in initial+[row,status]))
            with self.assertRaises(replay.ReplayMismatch):replay.RecordedProgram.from_tape(path)

    def test_mixed_input_stream_format_refuses(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'input-format.jsonl'
            path.write_text(json.dumps({'event':'status','format_version':6})+'\n')
            with self.assertRaises(ValueError):replay.RecordedProgram.from_tape(path)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--capture',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True);args,rest=parser.parse_known_args()
    CAPTURE,REPORT=args.capture,args.report
    result=unittest.main(argv=['test_response_replay.py',*rest],verbosity=2,exit=False).result
    EVIDENCE.update(tests_run=result.testsRun,tests_successful=result.wasSuccessful(),
                    scope='actual recorded API replies consumed by owned Python harness only',
                    process_replay_qualified=False,native_ABI_application_qualified=False)
    with REPORT.open('x') as stream:json.dump(EVIDENCE,stream,indent=2);stream.write('\n')
    raise SystemExit(0 if result.wasSuccessful() else 1)
