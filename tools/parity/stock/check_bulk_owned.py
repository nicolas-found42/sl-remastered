#!/usr/bin/env python3
"""Audit owned bulk20 raw ABI and explicit refused shapes without execution."""
import json,struct,sys,importlib.util
from pathlib import Path
spec=importlib.util.spec_from_file_location('stock_input_tape',Path(__file__).with_name('stock_input_tape.py'));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
def check(root,overflow=False):
 root=Path(root);checks=[]
 for case,counts in [('factory-owner-first',(1,2,16)),('factory-proxy-first',(0,17,0xffffffff))]:
  p=root/case;assert (p/'exit.txt').read_text().strip()=='0'
  raw=(p/'bulk-raw.bin').read_bytes();out=(p/'bulk-output.bin').read_bytes();nums=(p/'bulk-count.bin').read_bytes()
  assert len(raw)==5040 and len(out)==3600 and len(nums)==36
  for i in range(3):
   assert raw[i*560:(i+1)*560]==raw[(i+3)*560:(i+4)*560]==raw[(i+6)*560:(i+7)*560]
   assert out[i*400:(i+1)*400]==out[(i+3)*400:(i+4)*400]==out[(i+6)*400:(i+7)*400]
  assert struct.unpack('<9I',nums)==((17 if overflow else 1),)*9
  rows=[json.loads(line) for line in (p/'bulk-tape.jsonl').read_text().splitlines()];calls=[r for r in rows if r['event']=='input_return'];status=rows[-1]
  assert len(calls)==3 and [r['count_in'] for r in calls]==list(counts)
  assert status['format_version']==7 and all(status[k]==0 for k in ('active','pending','dropped','accepting'))
  if counts[0]:
   assert (status['entered'],status['returned'],status['phases'],status['gaps'])==(3,3,6,3 if overflow else 0)
   for row,n in zip(calls,counts):
    assert row['gap']==(17 if overflow else 0) and row['count_out']==(17 if overflow else 1)
    if overflow:
     try:m.validate_payload(row,7)
     except ValueError:pass
     else:raise AssertionError('overflow response admitted')
     assert row['buffer_bytes']==20*n and len(bytes.fromhex(row['post']))==20*n
    else:assert m.validate_payload(row,7)==20*n
    assert bytes.fromhex(row['pre'])==b'\x7e'*(20*n)
    assert bytes.fromhex(row['post'])==out[6*400+counts.index(n)*400+16:6*400+counts.index(n)*400+16+20*n]
  else:
   assert (status['entered'],status['returned'],status['phases'],status['gaps'])==(3,0,3,3)
   assert all(r['gap']==12 and r['return_seq']==0 and not r['pre_valid'] and not r['post_valid'] for r in calls)
  checks.append({'case':case,'counts':counts,'raw9x560_equal':True,'scope':'owned bulk20 full caller buffers and refused-shape forwarding only'})
 return checks
if __name__=='__main__':print(json.dumps(check(sys.argv[1],overflow='--overflow' in sys.argv[2:]),indent=2))
