#!/usr/bin/env python3
"""Audit actual owned Wine DirectInput identities, raw ABI, records and faults."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import stat
import collections
import re



SNAPSHOT = 560
CASES = 32
OUTPUT = 288


def decode(text):
    def pairs(values):
        result = {}
        for k, v in values:
            if k in result: raise ValueError('duplicate JSON key')
            result[k] = v
        return result
    def constant(value): raise ValueError('nonfinite JSON: ' + value)
    return json.loads(text, object_pairs_hook=pairs, parse_constant=constant)


def bounded(path, limit=16*1024**2):
    path=Path(path)
    if any(p.is_symlink() for p in [path,*path.parents]) or not stat.S_ISREG(path.stat().st_mode) or path.stat().st_size>limit:
        raise ValueError('invalid/oversized audit artifact: '+str(path))
    return path.read_bytes()


def load(path):
    return decode(bounded(path).decode())


def events(path):
    return [decode(line) for line in bounded(path,4*1024**2).decode().splitlines() if line.strip()]


def same(left,right):
    return json.dumps(left,sort_keys=True,allow_nan=False)==json.dumps(right,sort_keys=True,allow_nan=False)


def input_shape(row,version=6):
    numeric={'seq','return_seq','entry_phase','return_phase','lane','lane_read','thread','method','device_id',
             'incarnation','device_kind','caller_raw','size','count_in','count_out','flags','result','gap',
             'pre_valid','post_valid','object_raw','related_raw','retired_interfaces'}
    if version==7:numeric.add('buffer_bytes')
    if version not in (6,7):raise ValueError('unsupported input format')
    other={'event','scope','last_error','cw','mxcsr','pre','post'}
    if set(row)!=numeric|other or any(type(row[k]) is not int or not 0<=row[k]<=0xffffffff for k in numeric):
        raise ValueError('invalid typed input-return schema')
    for k in ('last_error','cw','mxcsr'):
        if not isinstance(row[k],list) or len(row[k])!=2 or any(type(v) is not int or not 0<=v<=0xffffffff for v in row[k]):
            raise ValueError('invalid input-return machine state')
    if row['scope']!='owned_console_only':raise ValueError('input-return scope differs')
    if version==7:
        if row['buffer_bytes']>320:raise ValueError('input byte bound exceeds format7')
        if not row['gap']:
            if row['method']==2 and not (row['size']==20 and 1<=row['count_in']<=16 and row['count_out']<=row['count_in']):raise ValueError('unsupported buffered shape')
            expected=row['size']*(row['count_in'] if row['method']==2 else 1)
            if row['buffer_bytes']!=expected:raise ValueError('buffer byte count mismatch')


def hashed(path):
    return hashlib.sha256(bounded(path,64*1024**2)).hexdigest()


def audit_case(root):
    root=Path(root).resolve(strict=True)
    rows = events(root / 'real-input-events.jsonl')
    if not rows or [r['seq'] for r in rows] != list(range(1, len(rows) + 1)):
        raise ValueError('missing/reordered actual COM observation')
    if rows[0] != {'seq': 1, 'event': 'header', 'schema': 1,
                   'scope': 'owned_real_Wine_DirectInput', 'process_replay_qualified': False}:
        raise ValueError('unknown real-input observation scope')
    if (rows[-1]['event'] != 'complete' or rows[-1]['cases'] != CASES or
        type(rows[-1].get('stop')) is not int or rows[-1]['stop']!=0):
        raise ValueError('actual COM helper is incomplete')
    window = [r for r in rows if r['event'] == 'window']
    if len(window) != 1 or window[0]['hwnd'] != window[0]['foreground'] or not window[0]['hwnd']:
        raise ValueError('owned guest window was not foreground')
    identities = []
    for device in range(3):
        base = next(r for r in rows if r['event'] == ('direct-input-owner' if device == 0 else 'device-created') and r['device'] == device)
        canonical = next(r for r in rows if r['event'] == 'canonical-IUnknown' and r['device'] == device)
        unicode = next(r for r in rows if r['event'] == 'unicode-interface' and r['device'] == device)
        recanonical = next(r for r in rows if r['event'] == 'unicode-canonical-IUnknown' and r['device'] == device)
        if (not all(r['object'] and r['vtable'] and len(r['methods']) == r['slots_read'] and all(r['methods'])
                    for r in (base, canonical, unicode, recanonical)) or canonical['object'] != recanonical['object']):
            raise ValueError('actual canonical COM identity evidence differs')
        identities.append({'device': device, 'base': base['object'], 'canonical_IUnknown': canonical['object'],
                           'unicode_alias': unicode['object'], 'base_vtable': base['vtable'],
                           'unicode_vtable': unicode['vtable'], 'distinct_unicode_vtable': unicode['vtable'] != base['vtable']})
        methods = [r for r in rows if r['event'] == 'method' and r['device'] == device]
        required = ['DirectInput8Create'] if device == 0 else ['CreateDevice', 'GetDeviceInfo', 'SetDataFormat',
                    'SetCooperativeLevel', 'SetProperty-buffer16', 'Acquire']
        required += ['QI-IUnknown', 'QI-unicode', 'QI-unicode-IUnknown']
        for method in required:
            matching = [r for r in methods if r['method'] == method]
            if len(matching) != 1 or matching[0]['result'] & 0x80000000:
                raise ValueError('actual creation/configuration failed or missing: ' + method)
    snapshots = bounded(root / 'real-snapshots.bin')
    output = bounded(root / 'real-output.bin')
    counts = bounded(root / 'real-counts.bin')
    if len(snapshots) != 3 * CASES * SNAPSHOT or len(output) != 3 * CASES * OUTPUT or len(counts) != 3 * CASES * 4:
        raise ValueError('missing/truncated actual method snapshots')
    def matrix_differences(left, right):
        differences = []
        for i in range(CASES):
            offsets = []
            for blob, width in ((snapshots, SNAPSHOT), (output, OUTPUT), (counts, 4)):
                a=blob[(left*CASES+i)*width:(left*CASES+i+1)*width]
                b=blob[(right*CASES+i)*width:(right*CASES+i+1)*width]
                offsets.append([n for n, (x,y) in enumerate(zip(a,b)) if x != y])
            if any(offsets): differences.append({'case': i, 'snapshot_offsets': offsets[0],
                                                'output_offsets': offsets[1], 'count_offsets': offsets[2]})
        return differences
    repeats = matrix_differences(0, 1)
    hooked = matrix_differences(0, 2)
    if (len(repeats) != rows[-1]['unhooked_repeat_differences'] or
            len(hooked) != rows[-1]['hooked_differences']):
        raise ValueError('raw snapshot differences contradict helper summary')
    # The measured negative cases follow actual Unacquire. No result mask or
    # tolerance may hide a differing refusal branch, write, flag or FX byte.
    for run in range(3):
        for i in range(CASES):
            offset=(run*CASES+i)*SNAPSHOT
            hr=struct.unpack_from('<I',snapshots,offset+28)[0]
            expected_failure=i%8>=5
            if bool(hr & 0x80000000) != expected_failure:
                raise ValueError('actual method success/refusal branch differs')
    tape = events(root / 'real-input-tape.jsonl')
    status = [r for r in tape if r['event']=='status']
    returns = [r for r in tape if r['event']=='input_return']
    if len(status)!=1:raise ValueError('missing status')
    for row in returns:input_shape(row,status[0]['format_version'])
    if len(status)!=1 or status[0]['format_version'] not in (6,7) or len(returns)!=48:
        raise ValueError('actual input recorder is incomplete')
    if any(status[0][k]!=0 for k in ('active','pending','dropped','gaps')):
        raise ValueError('actual input recorder has missing/refused observations')
    phases=sorted(p for r in returns for p in (r['entry_phase'],r['return_phase']))
    if phases!=list(range(1,97)) or status[0]['phases']!=96 or status[0]['entered']!=48 or status[0]['returned']!=48:
        raise ValueError('actual input call/phase loss')
    for row in returns:
        if row['device_id'] not in (1,2) or row['incarnation']!=1 or row['gap']:
            raise ValueError('actual input identity/gap differs')
        expected_kind=1 if row['device_id']==1 else 2
        if row['device_kind']!=expected_kind: raise ValueError('actual input kind differs')
        if row['method'] in (0,2):
            size=20 if row['method']==2 else (256 if expected_kind==1 else 16)
            if (row['size']!=size or row['pre_valid']!=1 or row['post_valid']!=1 or
                    len(bytes.fromhex(row['pre']))!=size or len(bytes.fromhex(row['post']))!=size):
                raise ValueError('actual input pre/postimage missing')
    callers=collections.Counter(r['caller_raw'] for r in returns)
    candidates=[c for c,n in callers.items() if n==CASES]
    if len(candidates)!=1:raise ValueError('actual raw caller boundary is ambiguous')
    measured=sorted((r for r in returns if r['caller_raw']==candidates[0]),key=lambda r:r['entry_phase'])
    methods=(0,0,2,1,1,0,0,2);devices=(1,2,1,1,2,1,2,1)
    for i,r in enumerate(measured):
        snapshot_offset=(2*CASES+i)*SNAPSHOT
        actual_hr=struct.unpack_from('<I',snapshots,snapshot_offset+28)[0]
        actual_error=struct.unpack_from('<I',snapshots,snapshot_offset+36)[0]
        f=i//8;method=methods[i%8];device=devices[i%8]
        if (r['method']!=method or r['device_id']!=device or r['result']!=actual_hr or
            r['last_error']!=[0x55aa55aa,actual_error] or
            r['cw']!=[0x037f+(f<<10),struct.unpack_from('<H',snapshots,snapshot_offset+48)[0]] or
            r['mxcsr']!=[0x1f80+(f<<13),struct.unpack_from('<I',snapshots,snapshot_offset+72)[0]]):
            raise ValueError('actual input tape differs from raw method snapshot')
        if method in (0,2):
            size=20 if method==2 else (256 if device==1 else 16)
            post=output[(2*CASES+i)*OUTPUT+16:(2*CASES+i)*OUTPUT+16+size]
            if r['pre']!=(b'\x7e'*size).hex() or r['post']!=post.hex():
                raise ValueError('actual input postimage differs from raw output buffer')
            if method==2 and (r['count_in']!=1 or r['count_out']!=struct.unpack_from('<I',counts,(2*CASES+i)*4)[0]):
                raise ValueError('actual buffered count differs from raw output cell')
    return {'identities':identities,'unhooked_repeat_differences':repeats,'hooked_differences':hooked,
            'actual_methods':CASES,'input_calls':len(returns),'shared_phases':len(phases),
            'record_sha256':hashlib.sha256((root/'real-input-tape.jsonl').read_bytes()).hexdigest()}


def audit_com_case(root, refusal=False):
    root=Path(root).resolve(strict=True)
    rows=events(root/'real-com-events.jsonl')
    if (not rows or [r['seq'] for r in rows]!=list(range(1,len(rows)+1)) or
        not same(rows[0],{'seq':1,'event':'header','schema':1,'scope':'owned_real_Wine_COM','refusal':refusal}) or
        rows[-1]['event']!='complete' or rows[-1]['cases']!=(0 if refusal else 64) or
        type(rows[-1].get('stop')) is not int or rows[-1]['stop']!=0):
        raise ValueError('real COM lifecycle/helper record missing or reordered')
    tape=events(root/'real-com-tape.jsonl')
    status=[r for r in tape if r['event']=='status']
    calls=[r for r in tape if r['event']=='input_return']
    if len(status)!=1:raise ValueError('missing status')
    for row in calls:input_shape(row,status[0]['format_version'])
    expected=14 if refusal else 124
    if (len(status)!=1 or len(calls)!=expected or status[0]['entered']!=expected or
        status[0]['returned']!=expected or status[0]['phases']!=2*expected or
        any(status[0][k]!=0 for k in ('active','pending','dropped')) or
        status[0]['gaps']!=(4 if refusal else 0)):
        raise ValueError('real COM lifecycle calls, gaps or final status differ')
    if sorted(r['seq'] for r in calls)!=list(range(1,expected+1)):
        raise ValueError('real COM sequence loss')
    if sorted(p for r in calls for p in (r['entry_phase'],r['return_phase']))!=list(range(1,2*expected+1)):
        raise ValueError('real COM shared phase loss')
    if sum(r['gap']==20 for r in calls)!=(1 if refusal else 0) or any(r['gap'] not in (0,20) for r in calls):
        raise ValueError('real COM wrong identity/alias refusal')
    identities={}
    for device in range(1,2 if refusal else 3):
        a=next(r for r in rows if r['event']=='ANSI-before-hook' and r['device']==device)
        w=next(r for r in rows if r['event']=='Unicode-before-hook' and r['device']==device)
        new=next(r for r in rows if r['event']=='recreated-ANSI' and r['device']==device)
        if a['vtable']==w['vtable'] or a['object']==w['object']:
            raise ValueError('real COM separate A/W layout evidence missing')
        identifier=10*device
        identities[(identifier,1)]={a['object'],w['object']}
        qs=sorted((r for r in calls if r['device_id']==identifier and r['incarnation']==2 and r['method']==3),
                  key=lambda r:r['entry_phase'])
        if not qs or not qs[0]['related_raw'] or qs[0]['related_raw']==new['object']:
            raise ValueError('rebound real COM sibling identity missing')
        identities[(identifier,2)]={new['object'],qs[0]['related_raw']}
        if not refusal:
            for generation in (1,2):
                group=[r for r in calls if r['device_id']==identifier and r['incarnation']==generation]
                zero=[r for r in group if r['method']==5 and r['result']==0]
                if len(zero)!=2 or sum(r['retired_interfaces'] for r in zero)!=2:
                    raise ValueError('real COM zero-Release retirement differs')
                end=min(r['return_phase'] for r in zero)
                if any(r['entry_phase']>end for r in group):
                    raise ValueError('real COM call entered after zero-Release retirement')
            old=max(r['return_phase'] for r in calls if r['device_id']==identifier and r['incarnation']==1)
            new_first=min(r['entry_phase'] for r in calls if r['device_id']==identifier and r['incarnation']==2)
            if new_first<=old: raise ValueError('real COM reincarnation precedes retirement')
    if refusal:
        identities[(99,1)]={next(r['object'] for r in rows if r['event']=='Unicode-before-hook')}
    guids={'0000000000000000c000000000000046','8010d45415dc3348a41b748f73a38179','8110d45415dc3348a41b748f73a38179'}
    for r in calls:
        identity=(r['device_id'],r['incarnation'])
        if (identity not in identities or r['object_raw'] not in identities[identity] or
            r['method'] not in (3,4,5) or r['entry_phase']>=r['return_phase']):
            raise ValueError('real COM call has conflicting/stale identity')
        if r['method']==3:
            if (r['size']!=16 or r['pre'] not in guids or r['pre_valid']!=1 or r['post_valid']!=1 or
                r['result']!=0 or len(bytes.fromhex(r['post']))!=4 or
                struct.unpack('<I',bytes.fromhex(r['post']))[0]!=r['count_out'] or
                (not r['gap'] and r['related_raw']!=r['count_out'])):
                raise ValueError('real COM QI request/response differs')
    snapshots=bounded(root/'real-com-snapshots.bin');output=bounded(root/'real-com-outputs.bin')
    if len(snapshots)!=2*3*CASES*SNAPSHOT or len(output)!=2*3*CASES*4:
        raise ValueError('real COM raw snapshots truncated')
    if not refusal:
        callers=collections.Counter(r['caller_raw'] for r in calls)
        candidates=[c for c,n in callers.items() if n==64]
        if len(candidates)!=1:raise ValueError('real COM raw caller boundary is ambiguous')
        measured=sorted((r for r in calls if r['caller_raw']==candidates[0]),key=lambda r:r['entry_phase'])
        for i,r in enumerate(measured):
            device=i//32;case=i%32;method=3 if case%8<3 else (4 if case%8==3 else 5)
            at=((device*3+2)*CASES+case)
            raw=snapshots[at*SNAPSHOT:(at+1)*SNAPSHOT]
            f=case//8
            if (r['device_id']!=10*(device+1) or r['incarnation']!=1 or r['method']!=method or
                r['result']!=struct.unpack_from('<I',raw,28)[0] or
                r['last_error']!=[0x55aa55aa,struct.unpack_from('<I',raw,36)[0]]):
                raise ValueError('real COM tape differs from raw method snapshot')
            if (r['cw']!=[0x037f+(f<<10),struct.unpack_from('<H',raw,48)[0]] or
                r['mxcsr']!=[0x1f80+(f<<13),struct.unpack_from('<I',raw,72)[0]]):
                raise ValueError('real COM tape control state differs from raw method snapshot')
            if method==3 and r['count_out']!=struct.unpack_from('<I',output,at*4)[0]:
                raise ValueError('real COM QI pointer differs from raw output cell')
    def differences(left,right):
        result=[]
        for device in range(2):
            for case in range(CASES):
                start=lambda run:((device*3+run)*CASES+case)
                a=snapshots[start(left)*SNAPSHOT:(start(left)+1)*SNAPSHOT]
                b=snapshots[start(right)*SNAPSHOT:(start(right)+1)*SNAPSHOT]
                x=output[start(left)*4:(start(left)+1)*4];y=output[start(right)*4:(start(right)+1)*4]
                if a!=b or x!=y:result.append({'device':device+1,'case':case,
                    'snapshot_offsets':[i for i,(v,w) in enumerate(zip(a,b)) if v!=w],
                    'pointer_output_differs':x!=y})
        return result
    repeats=[] if refusal else differences(0,1)
    hooked=[] if refusal else differences(0,2)
    if len(repeats)!=rows[-1]['unhooked_repeat_differences'] or len(hooked)!=rows[-1]['hooked_differences']:
        raise ValueError('real COM raw bytes contradict helper summary')
    return {'calls':len(calls),'shared_phases':2*len(calls),'gaps':status[0]['gaps'],
            'measured_cases':0 if refusal else 64,'unhooked_repeat_differences':repeats,
            'hooked_differences':hooked,'logical_generations':len(identities),
            'source_diagnostic_sha256':hashed(root/'real-com-tape.jsonl')}


def audit_cold_guards(root,fault=False):
    root=Path(root).resolve(strict=True)
    rows=events(root/'cold-guards.jsonl')
    names=['register-base','prepare-W','bind-W','idempotent-binding','changed-vtable-refused',
           'restored-binding','changed-target-cell-refused','restored-target-cell','register-foreign',
           'conflicting-id-refused','unknown-base-refused','unknown-IID-refused','retire-base-and-alias',
           'stale-incarnation-refused','new-incarnation','rebind-new-incarnation']
    expected=[1,1,1,1,0,1,0,1,1,0,0,0,1,0,1,1]
    if any(set(r)!={'case','result','expected','last_error'} or
           type(r['result']) is not int or type(r['expected']) is not int or type(r['last_error']) is not int
           for r in rows):raise ValueError('invalid typed cold guard record')
    if [r['case'] for r in rows]!=names or [r['expected'] for r in rows]!=expected:
        raise ValueError('cold alias guard cases missing/relabelled')
    bad=[r['case'] for r,e in zip(rows,expected) if r['result']!=e]
    if bad!=(['changed-vtable-refused','changed-target-cell-refused'] if fault else []):
        raise ValueError('cold alias vtable refusal/control differs')
    if any(r['last_error']!=0x31415926 for r in rows):raise ValueError('cold alias guard changed LastError')
    tape=events(root/'cold-guards-tape.jsonl');status=[r for r in tape if r['event']=='status']
    if (len(status)!=1 or status[0]['gaps']!=(4 if fault else 6) or
        any(status[0][k]!=0 for k in ('entered','returned','phases','active','pending','dropped'))):
        raise ValueError('cold alias refusal count/state differs')
    return {'cases':len(rows),'expected_refusals':6,'faults_detected':len(bad),'gaps':status[0]['gaps']}


def audit_legacy(root,lifecycle=False,fault=False):
    root=Path(root).resolve(strict=True);count=48 if lifecycle else 64
    prefix='lifecycle-' if lifecycle else ''
    a=bounded(root/(prefix+'baseline.bin'));b=bounded(root/(prefix+'hooked.bin'))
    if len(a)!=count*SNAPSHOT or len(b)!=len(a):raise ValueError('v1 raw snapshot count differs')
    differences=[i for i in range(count) if a[i*SNAPSHOT:(i+1)*SNAPSHOT]!=b[i*SNAPSHOT:(i+1)*SNAPSHOT]]
    if len(differences)!=(count if fault else 0):raise ValueError('v1 raw ABI/FX control differs')
    if lifecycle:
        pairs=[('lifecycle-out-baseline.bin','lifecycle-out-hooked.bin',48*4)]
    else:
        if bounded(root/'unhooked-repeat.bin')!=a:raise ValueError('v1 unhooked repeat is unstable')
        pairs=[('post-baseline.bin','post-hooked.bin',64*288),('count-baseline.bin','count-hooked.bin',64*4),
               ('entry-baseline.bin','entry-hooked.bin',64*4),('entry-baseline.bin','entry-repeat.bin',64*4)]
    for before,after,size in pairs:
        x=bounded(root/before);y=bounded(root/after)
        if len(x)!=size or len(y)!=size or x!=y:raise ValueError('v1 output/count/entry flags differ')
    tape=events(root/('lifecycle-tape.jsonl' if lifecycle else 'input-tape.jsonl'))
    calls=[r for r in tape if r['event'] in ('api_return','input_return')]
    status=[r for r in tape if r['event']=='status'];expected=48 if lifecycle else 72
    if (len(status)!=1 or len(calls)!=expected or status[0]['entered']!=expected or status[0]['returned']!=expected or
        sorted(p for r in calls for p in (r['entry_phase'],r['return_phase']))!=list(range(1,expected*2+1)) or
        any(status[0][k]!=0 for k in ('active','pending','dropped','gaps'))):
        raise ValueError('v1 accepted recorder regression')
    return {'raw_cases':count,'raw_snapshot_bytes_each':SNAPSHOT,'FX_faults_detected':len(differences),
            'calls':expected,'shared_phases':expected*2}


def audit(root):
    root=Path(root).resolve(strict=True)
    result=load(root/'result.json')
    required=('execution_finished','actual_isolation','container_removed','seed_unchanged',
              'producer_unchanged','binaries_unchanged','private_prefix_inodes')
    if not all(result.get(k) is True for k in required) or type(result.get('container_exit')) is not int or result['container_exit']!=0:
        raise ValueError('bounded execution/preservation not complete')
    inspected=load(root/'container-inspect.json');host=inspected['HostConfig'];mounts=inspected['Mounts']
    if (inspected['Image']!=result['image_id'] or host['NetworkMode']!='none' or host['Privileged'] is not False or
        host['ReadonlyRootfs'] is not True or host.get('Devices') or host.get('DeviceRequests') or host.get('CapAdd') or
        host['CapDrop']!=['ALL'] or host['SecurityOpt']!=['no-new-privileges'] or host.get('PidMode')=='host' or
        host.get('IpcMode')=='host' or host['NanoCpus']!=10**9 or host['Memory']!=1024**3 or
        host['MemorySwap']!=1024**3 or host['PidsLimit']!=128 or len(mounts)!=1 or
        mounts[0]['Destination']!='/work' or mounts[0]['RW'] is not True or Path(mounts[0]['Source']).resolve()!=root):
        raise ValueError('actual private DirectInput container isolation differs')
    profile=[s for s in inspected['Config']['Env'] if s.startswith('BOX64_DYNAREC_SAFEFLAGS=')]
    requested=result.get('safe_flags_requested')
    if profile!=([] if requested is None else ['BOX64_DYNAREC_SAFEFLAGS='+str(requested)]):
        raise ValueError('requested/observed flags runtime profile differs')
    frozen=load(root/'source-manifest.json')
    for name,identity in frozen.items():
        if 'sha256' in identity and (hashed(root/'source'/name)!=identity['sha256'] or
                                    (root/'source'/name).stat().st_size!=identity['size']):
            raise ValueError('frozen real-input producer changed')
    binaries=result['binary_sha256']
    for name,digest in binaries.items():
        if Path(name).name!=name or hashed(root/name)!=digest:raise ValueError('actual helper binary changed')
    for name in ('real-input.exe','real-com.exe'):
        raw=bounded(root/name);pe=struct.unpack_from('<I',raw,0x3c)[0]
        if raw[pe:pe+4]!=b'PE\x00\x00' or struct.unpack_from('<H',raw,pe+24+68)[0]!=2:
            raise ValueError('owned GUI helper PE subsystem differs')
    base=(root/'source/input_observer.inc').read_text()
    for directory,needle,replacement in [('alias-fault-source','input_alias_layout[index]=1;result=1;','result=1;'),
            ('retirement-fault-source','d->live=0;++r->retired_interfaces;','++r->retired_interfaces;')]:
        if base.count(needle)!=1 or bounded(root/directory/'input_observer.inc').decode()!=base.replace(needle,replacement):
            raise ValueError('semantic fault source differs')
    needle='''d->id==id && d->incarnation==incarnation &&
         input_vtables[d->vtable_index]==*(void***)alias &&
         input_publish_vtable(*(void***)alias,1)==d->vtable_index'''
    if (base.count(needle)!=1 or bounded(root/'cold-vtable-fault-source/input_observer.inc').decode()!=
        base.replace(needle,'d->id==id && d->incarnation==incarnation')):
        raise ValueError('cold vtable fault source differs')
    for name in ('control_tape.c','input_observer.inc'):
        text=bounded(root/'source'/name).decode()
        if 'fxrstor (%esp)' not in text or bounded(root/'fault-source'/name).decode()!=text.replace('fxrstor (%esp)','nop'):
            raise ValueError('FX fault source differs')
    expected={'actual':'0','fx-fault':'1','com':'0','com-fx-fault':'1','com-alias-fault':'92',
              'com-refuse':'0','com-retirement-fault':'87','cold-guard':'0','cold-guard-no-vtable':'61',
              'v1-state':'0','v1-state-no-fx':'1','v1-lifecycle':'0','v1-lifecycle-no-fx':'1'}
    for case,code in expected.items():
        if (root/'artifacts'/case/'launcher-exit.txt').read_text().strip()!=code:
            raise ValueError('actual helper/fault exit differs: '+case)
    positive=audit_case(root/'artifacts/actual')
    negative=audit_case(root/'artifacts/fx-fault')
    com=audit_com_case(root/'artifacts/com')
    com_fault=audit_com_case(root/'artifacts/com-fx-fault')
    refused=audit_com_case(root/'artifacts/com-refuse',refusal=True)
    cold=audit_cold_guards(root/'artifacts/cold-guard')
    cold_fault=audit_cold_guards(root/'artifacts/cold-guard-no-vtable',fault=True)
    legacy={}
    for name,lifecycle,fault in [('v1-state',False,False),('v1-state-no-fx',False,True),
                                ('v1-lifecycle',True,False),('v1-lifecycle-no-fx',True,True)]:
        legacy[name]=audit_legacy(root/'artifacts'/name,lifecycle,fault)
    stable=not positive['unhooked_repeat_differences']
    transparent=stable and not positive['hooked_differences']
    fault_detected=not negative['unhooked_repeat_differences'] and bool(negative['hooked_differences'])
    com_stable=not com['unhooked_repeat_differences']
    com_transparent=com_stable and not com['hooked_differences']
    com_fault_detected=not com_fault['unhooked_repeat_differences'] and len(com_fault['hooked_differences'])==64
    identity={'image_id':result['image_id'],'BOX64_DYNAREC_SAFEFLAGS':requested,
              'wowbox64_file':bounded(root/'artifacts/runtime-dll-sha256.txt').decode().strip(),
              'dinput8_file':bounded(root/'artifacts/dinput8-file-sha256.txt').decode().strip()}
    for field,name in [('wowbox64_file','system32/wowbox64.dll'),('dinput8_file','syswow64/dinput8.dll')]:
        digest=identity[field].split()[0]
        if not re.fullmatch('[0-9a-f]{64}',digest) or hashed(root/'prefix/drive_c/windows'/name)!=digest:
            raise ValueError('actual Wine/backend module identity differs')
    return {'scope':'owned real Wine DirectInput only; no Stock/replay qualification',
            'passed':transparent and fault_detected and com_transparent and com_fault_detected,
            'runtime_identity':identity,'unhooked_repeat_stable':stable,
            'method_transparency_qualified':transparent,'FX_fault_detected':fault_detected,
            'COM_transparency_qualified':com_transparent,'COM_FX_fault_detected':com_fault_detected,
            'COM':com,'COM_fault':com_fault,'COM_refusal':refused,
            'cold_guards':cold,'cold_guard_fault':cold_fault,'v1_regressions':legacy,
            'actual':positive,'fault':negative,'process_replay_qualified':False,
            'auditor_sha256':hashed(Path(__file__)),
            'limits':['only observed actual COM identities/configurations; no construction interception',
                      'v2 aliases are caller-observed and cold-bound; no arbitrary canonical identity discovery',
                      'replacement objects are precreated; no actual raw-address-reuse observation',
                      'zero-Release retirement qualified only for these observed shared-count Wine A/W interfaces',
                      'no configuration interception, arbitrary COM tear-offs or independent interface reference counts',
                      'no raw differences masked; no Stock caller, input injection, WndProc or replay qualification',
                      'quiescent publication only; no concurrency/thread lifetime/exception/unwind proof']}


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('output',type=Path)
    parser.add_argument('--report',type=Path);args=parser.parse_args()
    try: result=audit(args.output)
    except (OSError,ValueError,KeyError,TypeError,StopIteration) as error:
        result={'passed':False,'reason':str(error),'process_replay_qualified':False}
    report=args.report or args.output/'checks.json'
    with report.open('x') as stream:stream.write(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result));return 0 if result['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
