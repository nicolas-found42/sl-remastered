#!/usr/bin/env python3
"""Retained mutations of actual owned input records, never fabricated game proof."""
import argparse
import collections
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import unittest
from unittest import mock

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('real_input_auditor',HERE/'check_real_input.py')
checker=importlib.util.module_from_spec(spec);spec.loader.exec_module(checker)
CAPTURE=OUTPUT=None


class ActualInputMutationTest(unittest.TestCase):
    def directory(self,case):
        destination=OUTPUT/self._testMethodName
        shutil.copytree(CAPTURE/'artifacts'/case,destination)
        return destination

    def mutate(self,directory,name,operation):
        path=directory/name
        rows=[json.loads(line) for line in path.read_text().splitlines()]
        operation(rows)
        path.write_text(''.join(json.dumps(r)+'\n' for r in rows))

    def test_actual_bounded_qualification(self):
        result=checker.audit(CAPTURE)
        (OUTPUT/'actual-audit.json').write_text(json.dumps(result,indent=2)+'\n')
        self.assertTrue(result['passed'])
        self.assertEqual(result['COM']['calls'],124)
        self.assertEqual(result['COM']['shared_phases'],248)
        self.assertEqual(result['actual']['input_calls'],48)

    def test_removed_actual_COM_call_is_rejected(self):
        root=self.directory('com')
        self.mutate(root,'real-com-tape.jsonl',lambda rows:rows.pop(next(i for i,r in enumerate(rows) if r['event']=='input_return')))
        with self.assertRaises(ValueError):checker.audit_com_case(root)

    def test_actual_COM_alias_cannot_change_device_identity(self):
        root=self.directory('com')
        def corrupt(rows):
            row=next(r for r in rows if r['event']=='input_return' and r['device_id']==10)
            row['device_id']=20
        self.mutate(root,'real-com-tape.jsonl',corrupt)
        with self.assertRaises(ValueError):checker.audit_com_case(root)

    def test_rebound_actual_interface_cannot_use_stale_incarnation(self):
        root=self.directory('com')
        def corrupt(rows):
            next(r for r in rows if r['event']=='input_return' and r['incarnation']==2)['incarnation']=1
        self.mutate(root,'real-com-tape.jsonl',corrupt)
        with self.assertRaises(ValueError):checker.audit_com_case(root)

    def test_self_consistent_QI_pointer_lie_is_caught_by_actual_raw_output(self):
        root=self.directory('com')
        def corrupt(rows):
            calls=[r for r in rows if r['event']=='input_return']
            caller=next(c for c,n in collections.Counter(r['caller_raw'] for r in calls).items() if n==64)
            row=next(r for r in calls if r['caller_raw']==caller and r['method']==3)
            value=row['count_out']^4
            row.update(count_out=value,related_raw=value,post=struct.pack('<I',value).hex())
        self.mutate(root,'real-com-tape.jsonl',corrupt)
        with self.assertRaisesRegex(ValueError,'raw output'):checker.audit_com_case(root)

    def test_actual_zero_Release_retirement_cannot_be_omitted(self):
        root=self.directory('com')
        def corrupt(rows):
            for r in rows:
                if r['event']=='input_return':r['retired_interfaces']=0
        self.mutate(root,'real-com-tape.jsonl',corrupt)
        with self.assertRaisesRegex(ValueError,'retirement'):checker.audit_com_case(root)

    def test_actual_shared_phase_duplication_is_rejected(self):
        root=self.directory('com')
        def corrupt(rows):
            calls=[r for r in rows if r['event']=='input_return'];calls[1]['entry_phase']=calls[0]['entry_phase']
        self.mutate(root,'real-com-tape.jsonl',corrupt)
        with self.assertRaisesRegex(ValueError,'phase'):checker.audit_com_case(root)

    def test_boolean_incarnation_does_not_equal_integer_generation(self):
        root=self.directory('com')
        self.mutate(root,'real-com-tape.jsonl',lambda rows:next(r for r in rows if r['event']=='input_return').update(incarnation=True))
        with self.assertRaisesRegex(ValueError,'typed'):checker.audit_com_case(root)

    def test_actual_refusal_Gaps_cannot_be_relabelled_as_zero(self):
        root=self.directory('com-refuse')
        self.mutate(root,'real-com-tape.jsonl',lambda rows:next(r for r in rows if r['event']=='status').update(gaps=0))
        with self.assertRaises(ValueError):checker.audit_com_case(root,refusal=True)

    def test_actual_failed_state_keeps_caller_bytes(self):
        root=self.directory('actual')
        def corrupt(rows):
            r=next(r for r in rows if r['event']=='input_return' and r['method']==0 and r['result']&0x80000000)
            r['post']='00'*r['size']
        self.mutate(root,'real-input-tape.jsonl',corrupt)
        with self.assertRaisesRegex(ValueError,'raw output'):checker.audit_case(root)

    def test_actual_HRESULT_branch_is_bound_to_raw_snapshot(self):
        root=self.directory('actual')
        def corrupt(rows):
            r=next(r for r in rows if r['event']=='input_return' and r['method']==0 and r['result']&0x80000000)
            r['result']=0
        self.mutate(root,'real-input-tape.jsonl',corrupt)
        with self.assertRaisesRegex(ValueError,'raw method'):checker.audit_case(root)

    def test_actual_FLAGS_difference_is_visible_even_when_summary_agrees(self):
        root=self.directory('com')
        path=root/'real-com-snapshots.bin';raw=bytearray(path.read_bytes())
        raw[2*32*560+32]^=1;path.write_bytes(raw)
        self.mutate(root,'real-com-events.jsonl',lambda rows:rows[-1].update(hooked_differences=1))
        result=checker.audit_com_case(root)
        self.assertEqual(len(result['hooked_differences']),1)
        self.assertEqual(result['hooked_differences'][0]['snapshot_offsets'],[32])

    def test_actual_COM_CW_is_bound_to_raw_method_state(self):
        root=self.directory('com')
        def corrupt(rows):
            for row in rows:
                if row['event']=='input_return':row['cw']=[0,0]
        self.mutate(root,'real-com-tape.jsonl',corrupt)
        with self.assertRaisesRegex(ValueError,'control state'):checker.audit_com_case(root)

    def test_actual_COM_MXCSR_is_bound_to_raw_method_state(self):
        root=self.directory('com')
        def corrupt(rows):
            for row in rows:
                if row['event']=='input_return':row['mxcsr']=[0,0]
        self.mutate(root,'real-com-tape.jsonl',corrupt)
        with self.assertRaisesRegex(ValueError,'control state'):checker.audit_com_case(root)

    def test_actual_COM_nonzero_process_exit_cannot_qualify_raw_matches(self):
        path=CAPTURE/'artifacts/com/launcher-exit.txt';original=Path.read_text
        (OUTPUT/'com-exit-mutant.txt').write_text('99\n')
        def replacement(value,*args,**kwargs):return '99\n' if value==path else original(value,*args,**kwargs)
        with mock.patch.object(Path,'read_text',replacement),self.assertRaisesRegex(ValueError,'helper/fault exit'):
            checker.audit(CAPTURE)

    def test_actual_COM_failed_drain_is_incomplete(self):
        root=self.directory('com')
        self.mutate(root,'real-com-events.jsonl',lambda rows:rows[-1].update(stop=-1))
        with self.assertRaisesRegex(ValueError,'incomplete|missing'):checker.audit_com_case(root)

    def test_private_isolation_boolean_cannot_replace_actual_configuration(self):
        inspected=checker.load(CAPTURE/'container-inspect.json');inspected['HostConfig']['NetworkMode']='host'
        (OUTPUT/'host-network-mutant.json').write_text(json.dumps(inspected,indent=2)+'\n')
        original=checker.load
        def replacement(path):return inspected if Path(path)==CAPTURE/'container-inspect.json' else original(path)
        with mock.patch.object(checker,'load',side_effect=replacement),self.assertRaisesRegex(ValueError,'isolation'):
            checker.audit(CAPTURE)

    def test_SAFEFLAGS_identity_cannot_lie_about_actual_container_environment(self):
        inspected=checker.load(CAPTURE/'container-inspect.json')
        inspected['Config']['Env']=[s.replace('BOX64_DYNAREC_SAFEFLAGS=2','BOX64_DYNAREC_SAFEFLAGS=1') for s in inspected['Config']['Env']]
        (OUTPUT/'flags-profile-mutant.json').write_text(json.dumps(inspected,indent=2)+'\n')
        original=checker.load
        def replacement(path):return inspected if Path(path)==CAPTURE/'container-inspect.json' else original(path)
        with mock.patch.object(checker,'load',side_effect=replacement),self.assertRaisesRegex(ValueError,'runtime profile'):
            checker.audit(CAPTURE)


def main():
    global CAPTURE,OUTPUT
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--capture',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True);args=parser.parse_args()
    CAPTURE=args.capture.resolve(strict=True);OUTPUT=args.out.resolve()
    if CAPTURE==OUTPUT or CAPTURE in OUTPUT.parents or OUTPUT in CAPTURE.parents:
        parser.error('mutations must not overlap retained capture')
    OUTPUT.mkdir(parents=True,exist_ok=False)
    result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(ActualInputMutationTest))
    (OUTPUT/'result.json').write_text(json.dumps({'tests':result.testsRun,'failures':len(result.failures),
        'errors':len(result.errors),'passed':result.wasSuccessful(),'scope':'actual-record mutations; no process replay or Stock proof'},indent=2)+'\n')
    return 0 if result.wasSuccessful() else 1


if __name__=='__main__':raise SystemExit(main())
