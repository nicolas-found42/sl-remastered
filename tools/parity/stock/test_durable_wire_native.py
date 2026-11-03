"""Cross-language native publisher audit; generated snapshots, no target ABI claim."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('wire_native_audit', Path(__file__).with_name('durable_wire.py'))
w = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = w
spec.loader.exec_module(w)


def audit(root):
    mapping = (root/'mapping.bin').read_bytes()
    journal = (root/'journal.bin').read_bytes()
    if journal != (root/'entry.bin').read_bytes() + (root/'reply.bin').read_bytes():
        raise AssertionError('journal differs from acknowledged records')
    binding = w.OwnedMain(11, 22, 1, 1)
    terminal = dict(process_id=11, wait_result=0, exit_code=0, after_phase=2)
    # Identity/terminal are synthetic for this host fixture, not OS observations.
    result = w.inspect(mapping, journal, (1,2,3,4), binding, terminal)
    assert result['normal_pairs_complete'] and not result['target_abi_qualified']
    expected_fx = bytearray((i*17)&255 for i in range(512))
    struct.pack_into('<H', expected_fx, 0, 0x37f)
    struct.pack_into('<I', expected_fx, 24, 0x1f80)
    keys = ('edi','esi','ebp','esp','ebx','edx','ecx','eax_raw')
    for record, entry in ((result['entries'][1], True), (result['replies'][1], False)):
        expected = list(range(0xabc000,0xabc008))
        if not entry: expected[7] = 0
        assert [record[k] for k in keys] == expected
        assert [record[f'arg{i}'] for i in range(5)] == [0x1000,20,0x2000,0x3000,0]
        assert record['eflags'] == 0x202 and record['fx'] == bytes(expected_fx)
        assert record['input']['count_in'] == 16 and record['input']['count_value'] == (16 if entry else 2)
        assert record['input']['snapshot'] == bytes((i ^ (255 if not entry and i < 40 else 0)) & 255 for i in range(320))
    hole = w.archive.inspect((root/'hole-mapping.bin').read_bytes(), b'', (1,2,3,4))
    assert not hole.transport_complete and hole.next_ticket == 2 and hole.ack_ticket == 1
    # Coherent transport mutation: both retained slot and journal agree on a
    # changed header. The semantic wire audit must still refuse it.
    rejected = []
    for field, value in (('eax_raw',99), ('call_entry',0), ('data_role',1), ('mxcsr',0), ('phase',1)):
        m, j = bytearray(mapping), bytearray(journal)
        offset = w.FIELDS.index(field)*4
        struct.pack_into('<I', j, 1040+16+offset, value)
        struct.pack_into('<I', m, 128+1040+16+offset, value)
        try: w.inspect(bytes(m), bytes(j), (1,2,3,4), binding, terminal)
        except ValueError: rejected.append(field)
        else: raise AssertionError('accepted coherent mutation: '+field)
    return {'scope':'native C serialization and Python interpretation of generated state only',
            'paired_records':2, 'full_register_fx_and_data_match':True,
            'coherent_corruptions_rejected':rejected, 'unfinished_reservation_retained':True,
            'target_abi_qualified':False, 'process_replay_qualified':False,
            'sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.glob('*.bin')}}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    mode = p.add_mutually_exclusive_group(required=True)
    mode.add_argument('--producer',type=Path)
    mode.add_argument('--capture',type=Path)
    p.add_argument('--report',type=Path)
    args = p.parse_args()
    if args.producer:
        with tempfile.TemporaryDirectory(prefix='slrr-wire-') as root:
            subprocess.run([str(args.producer.resolve(strict=True)),root],check=True,timeout=15)
            result = audit(Path(root))
    else: result = audit(args.capture.resolve(strict=True))
    text = json.dumps(result,indent=2)+'\n'
    if args.report:
        with args.report.open('x') as f:f.write(text)
    print(text,end='')


if __name__ == '__main__': main()
