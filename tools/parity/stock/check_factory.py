#!/usr/bin/env python3
"""Strict bounded audit of owned factory tapes; raw entry/return factory seam only."""
import json,sys,copy
from pathlib import Path
def audit(rows,setup):
 returns=[r for r in rows if r['event']=='input_return'];assert len(returns)==2
 assert [(r['device_id'],r['device_kind'],r['size'],r['post']) for r in returns]==[(1,1,256,'31'*256),(2,2,16,'42'*16)]
 assert [(r['seq'],r['return_seq'],r['entry_phase'],r['return_phase'],r['gap'],r['result']) for r in returns]==[(1,1,1,2,0,0),(2,2,3,4,0,0)]
 status=rows[-1];assert status['event']=='status'
 assert [status[k] for k in ('entered','returned','phases','active','pending','dropped','gaps','accepting')]==[2,2,4,0,0,0,0,0]
 devices=[r for r in setup if r['event']=='device_setup'];assert len(devices)==2
 assert [(r['device_id'],r['kind'],r['added_com_calls'],r['bound'],r['gap']) for r in devices]==[(1,1,0,1,0),(2,2,0,1,0)]
 for r in returns:assert r['last_error'][1]==0x12345678 and r['cw']==[895,895] and r['mxcsr']==[8064,8064]
if __name__=='__main__':
 out=Path(sys.argv[1]);results=[]
 for case in ('factory-owner-first','factory-proxy-first'):
  p=out/case;assert (p/'exit.txt').read_text().strip()=='0'
  raw=(p/'factory-raw.bin').read_bytes();assert len(raw)==1680 and raw[:560]==raw[560:1120]==raw[1120:]
  entry=(p/'factory-entry.bin').read_bytes();assert len(entry)==1740 and entry[:560]==entry[560:1120]==entry[1120:1680]
  assert entry[1680:1700]==entry[1700:1720]==entry[1720:1740]
  rows=[json.loads(s) for s in (p/'factory-tape.jsonl').read_text().splitlines()];setup_path=p/'factory-setup.jsonl'
  if not setup_path.exists():setup_path=p/'stock-input-setup.jsonl'
  setup=[json.loads(s) for s in setup_path.read_text().splitlines()];audit(rows,setup)
  for field,value in [('gap',1),('post','00'),('return_phase',99),('device_id',9),('cw',[0,0]),('mxcsr',[0,0])]:
   bad=copy.deepcopy(rows);bad[0][field]=value
   try:audit(bad,setup)
   except AssertionError:pass
   else:raise AssertionError('undetected mutation '+field)
  results.append({'case':case,'audit':True,'corruptions_rejected':6})
 print(json.dumps({'results':results,'scope':'owned forwarding/output/admission/drain audit; full unhooked-repeat-entry-return comparison at owned prebound seam only'}))
