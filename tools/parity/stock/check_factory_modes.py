#!/usr/bin/env python3
"""Audit retained bounded factory modes without executing evidence commands."""
import json,sys,hashlib
from pathlib import Path
import importlib.util
spec=importlib.util.spec_from_file_location('factory_audit',Path(__file__).with_name('check_factory.py'));module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);audit=module.audit

def check(root,mode):
 root=Path(root).resolve(strict=True)
 result=json.loads((root/'result.json').read_text())
 assert result['image_id']=='sha256:c3105bb5c0fe829f89f6f782e79d7bfdb31708209ee4456e3ab4be5e7f94d88f'
 assert result['container_removed'] and result['seed_unchanged'] and result['producer_unchanged']
 assert 'BOX64_DYNAREC_SAFEFLAGS=2' in (root/'guest-env.txt').read_text().splitlines()
 manifest=json.loads((root/'source-manifest.json').read_text())
 for path,record in manifest.items():
  file=root/'source'/path
  if 'sha256' in record:assert hashlib.sha256(file.read_bytes()).hexdigest()==record['sha256']
 checks=[]
 for order in ('owner-first','proxy-first'):
  case=root/('factory-'+order)
  if mode=='refusals':
   for failure in ('missing-owner','unready-owner','missing-system','long-system','missing-module','missing-factory','missing-env','long-env','exact-env','long-log-path'):
    folder=root/('factory-'+order+'-'+failure)
    assert (folder/'exit.txt').read_text().strip()=='0'
    assert f'refusal={failure} first=80004005 second=80004005 factory_calls=0' in (folder/'helper.log').read_text()
    checks.append(order+':'+failure)
  elif mode=='depth':
   assert (case/'exit.txt').read_text().strip()=='0'
   setup=[json.loads(line) for line in (case/'factory-setup.jsonl').read_text().splitlines()]
   assert sum(row.get('event')=='factory_gap' and row.get('reason')=='depth_overflow' for row in setup)==3
   assert sum(row.get('event')=='create_owner' for row in setup)==8
   checks.append(order+':11-recursive-calls-3-overflow-gaps')
  elif mode=='raw':
   assert (case/'exit.txt').read_text().strip()=='0'
   raw=(case/'factory-raw.bin').read_bytes();entry=(case/'factory-entry.bin').read_bytes()
   assert len(raw)==1680 and raw[:560]==raw[560:1120]==raw[1120:]
   assert len(entry)==1740 and entry[:560]==entry[560:1120]==entry[1120:1680] and entry[1680:1700]==entry[1700:1720]==entry[1720:]
   setup=case/'factory-setup.jsonl'
   if not setup.exists():setup=case/'stock-input-setup.jsonl'
   audit([json.loads(line) for line in (case/'factory-tape.jsonl').read_text().splitlines()],[json.loads(line) for line in setup.read_text().splitlines()])
   checks.append(order+':560-entry-560-return-5-args')
  else:raise ValueError('unknown mode')
 return {'mode':mode,'checks':checks,'scope':'owned bounded mode; no actual Stock input, concurrent publication, workers or exception/unwind qualification'}
if __name__=='__main__':print(json.dumps(check(sys.argv[1],sys.argv[2]),indent=2))
