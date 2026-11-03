#!/usr/bin/env python3
"""Counterfactuals against retained actual factory/input artifacts; no execution."""
import argparse,copy,importlib.util,json
from pathlib import Path
from unittest.mock import patch
spec=importlib.util.spec_from_file_location('real_factory_auditor',Path(__file__).with_name('check_real_factory.py'));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
def test(capture):
 case=Path(capture)/'factory-owner-first';original=m.bounded
 assert m.audit_case(case)
 tape=[json.loads(line) for line in original(case/'actual-tape.jsonl').splitlines()]
 results=[]
 def run(name,file,data):
  def reader(path,*args,**kwargs):return data if Path(path)==case/file else original(path,*args,**kwargs)
  with patch.object(m,'bounded',side_effect=reader):
   try:m.audit_case(case)
   except (AssertionError,ValueError,KeyError):results.append({'name':name,'rejected':True});return
  raise AssertionError('corruption accepted: '+name)
 for name in ('missing-snapshots','short-pre','short-post','bulk-count-in','bulk-count-out','buffer-bytes','phase','gap','bool-gap','retirement','drop'):
  rows=copy.deepcopy(tape);row=next(r for r in rows if r.get('method')==2)
  if name=='missing-snapshots':row.update(pre_valid=0,post_valid=0,pre='',post='')
  if name=='short-pre':row['pre']=row['pre'][:40]
  if name=='short-post':row['post']=row['post'][:40]
  if name=='bulk-count-in':row['count_in']=17
  if name=='bulk-count-out':row['count_out']=17
  if name=='buffer-bytes':row['buffer_bytes']=20
  if name=='phase':row['return_phase']=row['entry_phase']
  if name=='gap':row['gap']=12
  if name=='bool-gap':row['gap']=False
  if name=='retirement':next(r for r in rows if r.get('method')==5)['retired_interfaces']=0
  if name=='drop':rows.remove(row)
  run(name,'actual-tape.jsonl',b''.join(json.dumps(r).encode()+b'\n' for r in rows))
 for name,file,offset in [('raw-factory','actual-factory-return.bin',560),('raw-device','actual-device-return.bin',560),('key-state','private-key-states.bin',2*256+0x1e),('key-buffer','private-key-buffer.bin',4)]:
  data=bytearray(original(case/file));data[offset]^=1;run(name,file,bytes(data))
 return {'positive_passed':True,'mutations':results,'scope':'actual artifact counterfactuals only; no process replay'}
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('capture',type=Path);a=p.parse_args();print(json.dumps(test(a.capture),indent=2))
