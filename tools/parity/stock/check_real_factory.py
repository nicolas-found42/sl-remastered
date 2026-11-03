#!/usr/bin/env python3
"""Audit actual private Wine factory/device/keyboard artifacts, never run them."""
import hashlib,importlib.util,json,struct,sys
from pathlib import Path
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('input_schema',HERE/'stock_input_tape.py');schema=importlib.util.module_from_spec(spec);spec.loader.exec_module(schema)
def bounded(path,limit=64*1024**2):
 path=Path(path);assert not path.is_symlink() and path.is_file() and path.stat().st_size<=limit;return path.read_bytes()
def audit_case(case):
 case=Path(case);assert bounded(case/'exit.txt').strip()==b'0'
 assert bounded(case/'actual-load-order.txt').decode()==case.name.removeprefix('factory-')
 for name in ('actual-factory-return.bin','actual-device-return.bin'):
  raw=bounded(case/name);assert len(raw)==1680 and raw[:560]==raw[560:1120]==raw[1120:]
 assert bounded(case/'actual-module-path.bin').decode().lower()=='c:\\windows\\syswow64\\dinput8.dll'
 assert struct.unpack('<3I',bounded(case/'actual-release-counts.bin'))==(0,0,0)
 for name in ('actual-outputs.bin','actual-device-outputs.bin'):
  values=struct.unpack('<3I',bounded(case/name));assert all(values)
 states=bounded(case/'private-key-states.bin');assert len(states)==768 and [states[i*256+0x1e] for i in range(3)]==[0,128,0]
 events=bounded(case/'private-key-buffer.bin');assert len(events)==40
 event_words=struct.unpack('<10I',events);assert (event_words[0],event_words[1],event_words[5],event_words[6])==(0x1e,128,0x1e,0)
 assert bounded(case/'private-key.txt').decode().splitlines()==['message 256 65','state 1 128','message 257 65','state 2 0','buffer_count 2']
 rows=[json.loads(line) for line in bounded(case/'actual-tape.jsonl').splitlines()];status=rows[-1]
 assert status['event']=='status' and status['format_version']==7
 calls=[r for r in rows if r['event']=='input_return'];n=len(calls)
 assert (status['entered'],status['returned'],status['phases'])==(n,n,2*n)
 assert all(status[k]==0 for k in ('active','pending','dropped','gaps','accepting'))
 assert sorted(r['seq'] for r in calls)==list(range(1,n+1)) and sorted(r['return_seq'] for r in calls)==list(range(1,n+1))
 phases=[]
 for row in calls:
  for key in ('seq','return_seq','entry_phase','return_phase','lane','lane_read','thread','method','device_id','incarnation','device_kind','caller_raw','object_raw','related_raw','retired_interfaces','gap','result','size','count_in','count_out','flags','pre_valid','post_valid','buffer_bytes'):
   assert type(row[key]) is int and 0<=row[key]<=0xffffffff
  for key in ('cw','mxcsr','last_error'):
   assert type(row[key]) is list and len(row[key])==2 and all(type(v) is int and 0<=v<=0xffffffff for v in row[key])
  assert row['scope']=='owned_console_only' and row['device_id']==1 and row['incarnation']==1 and row['device_kind']==1
  assert row['object_raw']==struct.unpack('<3I',bounded(case/'actual-device-outputs.bin'))[2]
  assert row['gap']==0 and 0<row['entry_phase']<row['return_phase'];phases.extend((row['entry_phase'],row['return_phase']));schema.validate_payload(row,7)
 assert sorted(phases)==list(range(1,2*n+1))
 bulk=[r for r in calls if r['method']==2];assert len(bulk)==2 and all(r['size']==20 and r['count_in']==16 and r['buffer_bytes']==320 for r in bulk)
 assert [r['count_out'] for r in bulk]==[0,2]
 assert bytes.fromhex(bulk[-1]['post'])[:40]==events
 state_rows=[bytes.fromhex(r['post']) for r in calls if r['method']==0]
 at=0
 for wanted in (states[:256],states[256:512],states[512:]):
  while at<len(state_rows) and state_rows[at]!=wanted:at+=1
  assert at<len(state_rows);at+=1
 release=[r for r in calls if r['method']==5];assert len(release)==1 and release[0]['result']==0 and release[0]['retired_interfaces']==1
 return {'calls':n,'phases':2*n,'bulk_bytes_each':320,'input_A_states':[0,128,0],'WndProc_A':[256,257],'bulk_events':2,'retired_interfaces':1,'raw_factory_device_returns_equal':True,'internal_real_target_entry_observed':False}
def audit(root):
 root=Path(root);result=json.loads(bounded(root/'result.json'))
 assert result['actual_isolation'] and result['container_removed'] and result['seed_unchanged'] and result['producer_unchanged']
 assert 'BOX64_DYNAREC_SAFEFLAGS=2' in bounded(root/'guest-env.txt').decode().splitlines()
 module_hashes=[line.split()[0] for line in bounded(root/'actual-module-hashes.txt').decode().splitlines()]
 assert module_hashes==['0332b5210a9f287b0c75aee67d3c8e1a5e5a7b05ad5d51e1c8526685b152b582','58d036888d638f05a72193702f97071aabc9baf7959e7736d48d9c450acc3440']
 for name,identity in json.loads(bounded(root/'source-manifest.json')).items():
  if 'sha256' in identity:assert hashlib.sha256(bounded(root/'source'/name)).hexdigest()==identity['sha256']
 inspected=json.loads(bounded(root/'container-inspect.json'));host=inspected['HostConfig']
 assert inspected['Image']==result['image_id']=='sha256:c3105bb5c0fe829f89f6f782e79d7bfdb31708209ee4456e3ab4be5e7f94d88f'
 assert host['NetworkMode']=='none' and host['ReadonlyRootfs'] and not host['Privileged'] and not host['Devices'] and not host['DeviceRequests'] and host['CapDrop']==['ALL'] and host['SecurityOpt']==['no-new-privileges']
 assert host['Memory']==host['MemorySwap']==4608*1024**2 and host['NanoCpus']==1000000000 and host['PidsLimit']==128
 assert len(inspected['Mounts'])==3 and {(m['Destination'],m['Source'],m['RW']) for m in inspected['Mounts']}=={('/work',str(root.resolve()),True),('/payload',str((root/'source').resolve()),False),('/seed',result['warm_prefix_seed'],False)}
 return {'scope':'actual owned Wine factory/device/keyboard; no Stock, scheduler replay, publication concurrency or exception/unwind qualification','cases':{order:audit_case(root/('factory-'+order)) for order in ('owner-first','proxy-first')}}
if __name__=='__main__':print(json.dumps(audit(sys.argv[1]),indent=2))
