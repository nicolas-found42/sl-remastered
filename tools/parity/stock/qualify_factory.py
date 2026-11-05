#!/usr/bin/env python3
"""Owned factory console qualifier derived from the bounded input runner.

No Stock execution. This fixture checks forwarding and recorder admission only;
full raw ABI/FX transparency and real DirectInput construction remain pending.
The generated runner is retained verbatim in the selected fresh output parent.
"""
import argparse
import json
import gzip
import atexit
import hashlib
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[3]
def audit_discovery_states(raw):
    """Independently decode the fixed384-case owned matrix, without Wine."""
    if len(raw) != 384 * 576:
        return False
    cleanup_bytes = (4, 0, 4, 0, 0, 48, 16, 4)
    def word(offset, size=4):
        return int.from_bytes(raw[offset:offset + size], 'little')
    arena = word(40) - 8  # First hook has observed RET4, plus its return word.
    for index in range(384):
        base = index * 576
        hook, within = divmod(index, 48)
        mode, within = divmod(within, 12)
        flag_index, behavior = divmod(within, 3)
        cw = 0x37f + (mode << 10)
        mxcsr = 0x1f80 + (mode << 13)
        flags = (0x202, 0x203, 0x246, 0x697)[flag_index]
        flags ^= 1 if behavior == 1 else 0x40 if behavior == 2 else 0
        if behavior == 2:
            cw ^= 0x400
            mxcsr ^= 0x2000
        digest = 0x811c9dc5
        for argument in range(12):
            digest = ((digest ^ (0xabc00000 + index * 16 + argument)) * 16777619) & 0xffffffff
        expected = {0: 0x66666666, 4: 0x55555555, 12: arena + cleanup_bytes[hook],
                    16: 0x44444444, 20: 0xd3d3d3d3 if behavior else 0x33333333,
                    24: 0xc2c2c2c2 if behavior else 0x22222222,
                    28: 0xa1a1a1a1 if behavior else 0x11111111,
                    36: 0x55667788 if behavior == 2 else 0x55aa55aa,
                    40: arena + 4 + cleanup_bytes[hook], 44: digest,
                    72: mxcsr, 568: 0x12345678, 572: 0x9abcdef0}
        if any(word(base + offset) != value for offset, value in expected.items()):
            return False
        if word(base + 32) & 0xcd5 != flags & 0xcd5 or word(base + 564, 2) != cw:
            return False
        xmm = bytes([0 if behavior == 2 else 255]) * 16
        if raw[base + 208:base + 224] != xmm or raw[base + 320:base + 336] != xmm:
            return False
        if behavior == 2 and (word(base + 566, 2) >> 11) & 7 != 7:
            return False
    return True


def qualification_metadata(args):
    mode = {'lazy': args.lazy, 'refusals': args.setup_failure, 'depth': args.depth,
            'fault': args.fault, 'memory_prefix': args.memory_prefix,
            'device_raw': args.device_raw, 'real_construction': args.real_construction,
            'discovery_abi': args.discovery_abi, 'native_clock': args.native_clock}
    if args.discovery_abi:
        scope = ('owned actual discovery entry/return stubs only; no Stock graph, resource identity, '
                 'game behavior, startup publication, concurrency or exception/unwind qualification')
    elif args.real_construction:
        scope = ('actual owned Wine factory/device/coherent private keyboard; no Stock/replay, '
                 'concurrent publication or unwind qualification')
    else:
        scope = ('owned factory mode only; no actual DirectInput/Stock, worker/concurrent publication '
                 'or exception/unwind qualification')
    return mode, scope


p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--out',type=Path,required=True)
p.add_argument('--prefix-seed',type=Path,required=True)
p.add_argument('--bulk-overflow',action='store_true')
p.add_argument('--bulk-owned',action='store_true')
p.add_argument('--real-construction',action='store_true')
p.add_argument('--device-raw',action='store_true')
p.add_argument('--memory-prefix',action='store_true')
p.add_argument('--print-runner',action='store_true',help='print derived runner without admission, writes or execution')
p.add_argument('--discovery-abi',action='store_true',help='owned actual discovery stub ABI and missing-restoration controls; no Stock')
p.add_argument('--native-clock',action='store_true',help='add owned return-stub ABI and missing-FX-restoration controls')
p.add_argument('--native-clock-default-profile',action='store_true',help='qualify the clock stub without a BOX64 SAFEFLAGS override')
p.add_argument('--native-clock-legacy-fault',action='store_true',help='experimental guard-page AV delivery control; not qualified on the current backend')
p.add_argument('--control-writer-reference',type=Path,help='immutable pre-batch recorder source for owned slow-sink counterfactual')
p.add_argument('--control-writer-reference-sha256',help='exact SHA256 of the owned pre-batch recorder source')
p.add_argument('--runner-parent',type=Path,help='existing admitted artifact parent for retained derivation files')
p.add_argument('--artifact-budget-root',type=Path,help='existing registered direct parent; console prepaid route')
p.add_argument('--artifact-parent-max-mib',type=int,help='exact existing parent reservation; never resize')
p.add_argument('--setup-failure',choices=('all','missing-owner','unready-owner','missing-owner','unready-owner','missing-system','long-system','missing-module','missing-factory','missing-env','long-env','exact-env','long-log-path'))
p.add_argument('--depth',action='store_true')
p.add_argument('--lazy',action='store_true')
p.add_argument('--fault',choices=('fx','edx','device-fx','device-edx','bulk-bytes'))
a=p.parse_args()
if a.discovery_abi:
    incompatible = [name for name in ('native_clock', 'native_clock_default_profile',
        'native_clock_legacy_fault', 'control_writer_reference', 'control_writer_reference_sha256',
        'real_construction', 'device_raw', 'bulk_owned', 'bulk_overflow', 'lazy',
        'depth', 'setup_failure', 'fault') if getattr(a, name)]
    if not a.memory_prefix or incompatible:
        raise SystemExit('--discovery-abi requires --memory-prefix and excludes options: ' +
                         ', '.join('--' + name.replace('_', '-') for name in incompatible))
if a.native_clock and not a.memory_prefix:
    raise SystemExit('--native-clock requires the qualified --memory-prefix route')
if a.native_clock_default_profile and not a.native_clock:
    raise SystemExit('--native-clock-default-profile requires --native-clock')
if a.native_clock_legacy_fault and not a.native_clock:
    raise SystemExit('--native-clock-legacy-fault requires --native-clock')
if bool(a.control_writer_reference)!=bool(a.control_writer_reference_sha256) or (a.control_writer_reference and not a.native_clock):
    raise SystemExit('writer reference/hash require the native-clock owned qualifier')
if a.control_writer_reference and hashlib.sha256(a.control_writer_reference.read_bytes()).hexdigest()!=a.control_writer_reference_sha256:
    raise SystemExit('writer reference source identity mismatch')
if bool(a.artifact_budget_root)!=(a.artifact_parent_max_mib is not None) or (a.artifact_budget_root and not (a.native_clock or a.discovery_abi)):
    raise SystemExit('prepaid parent/exact maximum require the native-clock or discovery console route')
out=a.out.resolve()
if out.exists():raise SystemExit('output already exists')
def replace_once(text, old, new):
    if text.count(old)!=1:raise ValueError('derivation anchor count: '+old[:80])
    return text.replace(old,new)
s=(Path(__file__).with_name('qualify_input.py')).read_text()
s=replace_once(s,'import time\n','import time\nimport sys\nsys.dont_write_bytecode=True\n')
s=replace_once(s,"seed_result['cleanup_exit'] != 0", "not (seed_result.get('cleanup_exit') == 0 or (seed_result.get('execution_finished') and seed_result.get('container_removed')))")
s=replace_once(s,"ROOT = Path(__file__).resolve().parents[3]",'ROOT = Path('+repr(str(ROOT))+')')
s=replace_once(s,"'input_bind.c', 'rng_observer.inc'", "'input_bind.c', 'rng_observer.inc', 'stock_input_proxy.c'")
s=replace_once(s,"paths['qualify_input.py'] = Path(__file__).resolve()", "paths['wine_stock_input_factory_test.c'] = ROOT / 'tests/wine_stock_input_factory_test.c'\n            paths['qualify_input.py'] = Path(__file__).resolve()")
s=replace_once(s,"'binding-proof.exe': [str(frozen / 'wine_input_binding_test.c')]", "'binding-proof.exe': [str(frozen / 'wine_input_binding_test.c')],\n                    'stock-input-owned.dll': ['-shared','-static','-static-libgcc','-Wl,--kill-at','-DCONTROL_TAPE_TEST',str(frozen/'stock_input_proxy.c'),str(frozen/'input_bind.c'),'-ldxguid'],\n                    'factory-proof.exe': [str(frozen/'wine_stock_input_factory_test.c'),'-ldxguid']")
s=replace_once(s,"for case in cases:\n                (out / case).mkdir()", "for case in (*cases,'factory-owner-first','factory-proxy-first'):\n                (out / case).mkdir()")
s=replace_once(s,"export WINEPREFIX=/work/prefix", "export BOX64_DYNAREC_SAFEFLAGS=2\nexport WINEPREFIX=/work/prefix")
s=replace_once(s,'wineserver -k\nwineserver -w', 'run_case factory-owner-first factory-proof.exe owner-first\nrun_case factory-proxy-first factory-proof.exe proxy-first\nwineserver -k\nwineserver -w')
s=replace_once(s, "result['helper_exits'] = {case:", "result['factory_exits'] = {case:(out/case/'exit.txt').read_text().strip() for case in ('factory-owner-first','factory-proxy-first')}\n            result['helper_exits'] = {case:")
s=replace_once(s, "result['execution_finished'] = (result.get('container_exit') == 0", "result['execution_finished'] = (result.get('factory_exits') == {'factory-owner-first':'0','factory-proxy-first':'0'} and result.get('container_exit') == 0")
s=replace_once(s, "export BOX64_DYNAREC_SAFEFLAGS=2", "export BOX64_DYNAREC_SAFEFLAGS=2\nenv > /work/guest-env.txt\nsha256sum /usr/lib/wine/aarch64-windows/wowbox64.dll > /work/runtime-module.txt")
s=replace_once(s, "paths['wine_stock_input_factory_test.c'] = ROOT / 'tests/wine_stock_input_factory_test.c'", "paths['wine_stock_input_factory_test.c'] = ROOT / 'tests/wine_stock_input_factory_raw_test.c'")
if a.bulk_owned:
    s=replace_once(s,"paths['wine_stock_input_factory_test.c'] = ROOT / 'tests/wine_stock_input_factory_raw_test.c'", "paths['wine_stock_input_factory_test.c'] = ROOT / 'tests/wine_input_bulk_test.c'")
    s=replace_once(s,"'factory-proof.exe': [str(frozen/'wine_stock_input_factory_test.c'),'-ldxguid']", "'factory-proof.exe': ['-DCONTROL_TAPE_INPUT','-DCONTROL_TAPE_TEST',str(frozen/'wine_stock_input_factory_test.c'),str(frozen/'control_tape.c'),'-ladvapi32']")
if a.bulk_overflow:
    if not a.bulk_owned:raise ValueError('--bulk-overflow requires --bulk-owned')
    s=replace_once(s,"'factory-proof.exe': ['-DCONTROL_TAPE_INPUT','-DCONTROL_TAPE_TEST'", "'factory-proof.exe': ['-DBULK_RETURN_OVERFLOW','-DCONTROL_TAPE_INPUT','-DCONTROL_TAPE_TEST'")
if a.real_construction:
    s=replace_once(s,"paths['wine_stock_input_factory_test.c'] = ROOT / 'tests/wine_stock_input_factory_raw_test.c'", "paths['wine_stock_input_factory_test.c'] = ROOT / 'tests/wine_stock_input_real_factory_test.c'\n            paths['wine_stock_input_device_raw_test.c'] = ROOT / 'tests/wine_stock_input_device_raw_test.c'")
    s=replace_once(s,'wine --version > /work/runtime-version.txt', 'export DISPLAY=:91\nXvfb :91 -screen 0 800x600x24 -nolisten tcp > /work/xvfb.log 2>&1 &\nsleep 0.3\nwine --version > /work/runtime-version.txt')
if a.device_raw:
    s=replace_once(s,"paths['wine_stock_input_factory_test.c'] = ROOT / 'tests/wine_stock_input_factory_raw_test.c'", "paths['wine_stock_input_factory_test.c'] = ROOT / 'tests/wine_stock_input_device_raw_test.c'")
if a.depth:
    s=replace_once(s,"paths['wine_stock_input_factory_test.c'] = ROOT / 'tests/wine_stock_input_factory_raw_test.c'", "paths['wine_stock_input_factory_test.c'] = ROOT / 'tests/wine_stock_input_factory_depth_test.c'")
if a.lazy or a.setup_failure:
    s=replace_once(s,"'factory-proof.exe': [str(frozen/'wine_stock_input_factory_test.c'),'-ldxguid']", "'factory-proof.exe': ['-DFACTORY_LAZY_TEST',str(frozen/'wine_stock_input_factory_test.c'),'-ldxguid']")
s=replace_once(s,"            cases = ('accept',", "            shutil.copy2(out/'input-owner.dll',out/'unready-owner.dll')\n            cases = ('accept',")
if a.setup_failure:
    s=replace_once(s,"export BOX64_DYNAREC_SAFEFLAGS=2", "export BOX64_DYNAREC_SAFEFLAGS=2\nexport FACTORY_FAILURE_MODE="+("missing-env" if a.setup_failure=="all" else a.setup_failure))
if a.setup_failure=='all':
    extra='for mode in missing-owner unready-owner missing-system long-system missing-module missing-factory missing-env long-env exact-env long-log-path; do\n for order in owner-first proxy-first; do\n  mkdir /work/factory-$order-$mode\n  FACTORY_FAILURE_MODE="$mode" run_case factory-$order-$mode factory-proof.exe "$order"\n done\ndone\n'
    s=replace_once(s,'wineserver -k\nwineserver -w',extra+'wineserver -k\nwineserver -w')
if a.fault:
    if a.fault=='bulk-bytes':
        mutation="pass"
    elif a.fault.startswith('device-'):
        mutation="begin=text.index('#define FACTORY_ENTRY(');end=text.index('FACTORY_ENTRY(factory_entry',begin);macro=text[begin:end].replace('FACTORY_ENTRY','DEVICE_FAULT_ENTRY');"+ ("macro=macro.replace('fxrstor (%esp)','nop');" if a.fault=='device-fx' else "macro=macro.replace('movl %ebx,%esp',r'movl $0,20(%ebx)\\n\\tmovl %ebx,%esp');")+"text=text[:end]+macro+text[end:];assert text.count('FACTORY_ENTRY(create_device,1,real_device)')==1;text=text.replace('FACTORY_ENTRY(create_device,1,real_device)','DEVICE_FAULT_ENTRY(create_device,1,real_device)')"
    else:
        mutation="text=text.replace('fxrstor (%esp)','nop')" if a.fault=='fx' else "text=text.replace('movl %eax,-4(%ebx)',r'movl $0,20(%ebx)\\n\\tmovl %eax,-4(%ebx)')"
    code="            target=frozen/'stock_input_proxy.c'\n            text=target.read_text()\n            "+mutation+"\n            target.write_text(text)\n"
    s=replace_once(s,"            save(out / 'source-manifest.json', manifest(frozen))",code+"            save(out / 'source-manifest.json', manifest(frozen))")
    if a.fault=='bulk-bytes':
        mutation_code="            target=frozen/'input_observer.inc';text=target.read_text();assert text.count('r->buffer_bytes=kind==2?r->size*r->count_in:r->size;')==1;target.write_text(text.replace('r->buffer_bytes=kind==2?r->size*r->count_in:r->size;','r->buffer_bytes=r->size;'))\n"
        s=replace_once(s,"            save(out / 'source-manifest.json', manifest(frozen))",mutation_code+"            save(out / 'source-manifest.json', manifest(frozen))")

# Serialize every seed-live inspection/clone with other Stock-class users.
start=s.index("    running = subprocess.check_output")
end=s.index("    out.mkdir",start)
check=s[start:end]
s=s[:start]+s[end:]
s=replace_once(s,"{'cpu': 1, 'host_memory': 2, 'vm_memory': 2}, timeout=30", "{'cpu': 1, 'host_memory': 2, 'vm_memory': 2, 'stock': 1}, timeout=180")
s=replace_once(s,"        result['admission'] = lease.description", ''.join('    '+line+'\n' for line in check.splitlines())+"        result['admission'] = lease.description")
if a.memory_prefix:
    begin=s.index("            subprocess.run(['cp', '-c', '-R'")
    end=s.index("            result['seed_result_sha256']",begin)
    s=s[:begin]+s[end:]
    fixture_root=ROOT/'local/verify/parity-memory-prefix-20261102'
    source_code="            fixture_root=Path("+repr(str(fixture_root))+")\n            for n in ('prefix_fixture.sh','prefix_shell.py'):shutil.copy2(fixture_root/n,frozen/n)\n            spec_=importlib.util.spec_from_file_location('prefix_shell',frozen/'prefix_shell.py');shell=importlib.util.module_from_spec(spec_);spec_.loader.exec_module(shell)\n            (frozen/'expected-prefix.tsv').write_bytes(shell.encode_manifest(before))\n"
    s=replace_once(s,"            save(out / 'source-manifest.json', manifest(frozen))",source_code+"            save(out / 'source-manifest.json', manifest(frozen))")
    s=replace_once(s,"pool.ArtifactBudget([out], 4 * 1024**3", "pool.ArtifactBudget([out], 64 * 1024**2")
    s=replace_once(s,"'host_memory': 2, 'vm_memory': 2, 'stock': 1", "'host_memory': 9, 'vm_memory': 9, 'stock': 1")
    s=replace_once(s,"export WINEPREFIX=/work/prefix", "export WINEPREFIX=/privateprefix")
    s=replace_once(s,'wine --version > /work/runtime-version.txt','/bin/bash /payload/prefix_fixture.sh prepare\nwine --version > /work/runtime-version.txt')
    s=replace_once(s,'wineserver -k\nwineserver -w', 'wineserver -k\nstopped=$?\nwineserver -w\nwaited=$?\nprintf \'%s %s\\n\' "$stopped" "$waited" > /work/prefix-quiescence.txt\nif [ "$stopped" -ne 0 ] || [ "$waited" -ne 0 ]; then exit 81; fi\nexport PREFIX_QUIESCENCE=1\n/bin/bash /payload/prefix_fixture.sh finish')
    s=replace_once(s,"'--memory', '1g', '--memory-swap', '1g'", "'--memory', '4608m', '--memory-swap', '4608m'")
    s=replace_once(s,"'--tmpfs', '/tmp:rw,nosuid,nodev,size=128m', '--mount', f'type=bind,src={out},dst=/work'", "'--tmpfs', '/tmp:rw,nosuid,nodev,size=128m', '--tmpfs','/privateprefix:rw,nosuid,nodev,size=3g','--mount',f'type=bind,src={seed},dst=/seed,readonly','--mount',f'type=bind,src={frozen},dst=/payload,readonly','--mount', f'type=bind,src={out},dst=/work'")
    s=replace_once(s,"            result['helper_exits'] = {case:", "            fixture,finished,_=shell.validate_capture(out,before)\n            result['private_prefix_inodes']=fixture['private_inode_identities']\n            result['memory_prefix_audit']=finished\n            result['helper_exits'] = {case:")
if a.real_construction:
    s=replace_once(s,"'factory-proof.exe': [str(frozen/'wine_stock_input_factory_test.c'),'-ldxguid']","'factory-proof.exe': [str(frozen/'wine_stock_input_factory_test.c'),'-ldxguid','-ldinput8']")
    s=replace_once(s,'wine --version > /work/runtime-version.txt', 'sha256sum "$WINEPREFIX/drive_c/windows/syswow64/dinput8.dll" "$WINEPREFIX/drive_c/windows/system32/wowbox64.dll" > /work/actual-module-hashes.txt\nwine --version > /work/runtime-version.txt')
    s=replace_once(s,"'vm_memory': 9, 'stock': 1","'vm_memory': 9, 'stock': 1, 'renderer': 1")
if a.real_construction:
    s=replace_once(s,"'scope': 'owned console only; no Stock/device/window/audio; no replay qualification'", "'scope': 'actual Wine factory/device and coherent keyboard in private Xvfb; no host input/audio/display or Stock/replay'")
    inspect_code="            create=command.copy();create[1]='create';create.remove('--rm')\n            subprocess.run(create,check=True,capture_output=True,timeout=20)\n            inspected=json.loads(subprocess.check_output(['docker','inspect',name],timeout=10))[0]\n            save(out/'container-inspect.json',inspected)\n            host=inspected['HostConfig'];mounts=inspected['Mounts']\n            expected_mounts={('/work',str(out),True),('/payload',str(frozen),False),('/seed',str(seed),False)}\n            result['actual_isolation']=(inspected['Image']==image['Id'] and host['NetworkMode']=='none' and host['ReadonlyRootfs'] is True and not host['Privileged'] and not host.get('Devices') and not host.get('DeviceRequests') and not host.get('CapAdd') and host['CapDrop']==['ALL'] and host['SecurityOpt']==['no-new-privileges'] and host['NanoCpus']==1000000000 and host['Memory']==host['MemorySwap']==4608*1024**2 and host['PidsLimit']==128 and {(m['Destination'],m['Source'],m['RW']) for m in mounts}==expected_mounts and len(mounts)==3 and host['Tmpfs'].get('/privateprefix')=='rw,nosuid,nodev,size=3g')\n            if not result['actual_isolation']:raise ValueError('actual private isolation differs')\n            command=['docker','start','--attach',name]\n            save(out/'container-launch.json',{'create':create,'start':command})\n"
    s=replace_once(s,"            with (out / 'container.log').open('wb') as log:", inspect_code+"            with (out / 'container.log').open('wb') as log:")
if a.native_clock:
    s=replace_once(s,"pool.ArtifactBudget([out], 64 * 1024**2", "pool.ArtifactBudget([out], 8 * 1024**2")
    if a.native_clock_default_profile:
        s=replace_once(s,'export BOX64_DYNAREC_SAFEFLAGS=2', 'unset BOX64_DYNAREC_SAFEFLAGS')
    s=replace_once(s,"'input_bind.c', 'rng_observer.inc', 'stock_input_proxy.c'",
                   "'input_bind.c', 'rng_observer.inc', 'stock_input_proxy.c', 'native_clock_observer.inc'")
    s=replace_once(s,"            paths['qualify_input.py'] = Path(__file__).resolve()",
                   "            paths['wine_native_clock_test.c'] = ROOT / 'tests/wine_native_clock_test.c'\n            paths['qualify_input.py'] = Path(__file__).resolve()")
    compile_clock="""                clock_fault=out/'native-clock-fault-source'
                shutil.copytree(frozen,clock_fault)
                target=clock_fault/'native_clock_observer.inc'
                text=target.read_text()
                if text.count('fxrstor (%esp)')!=1:raise ValueError('native clock FX fault anchor changed')
                target.write_text(text.replace('fxrstor (%esp)','nop'))
                save(out/'native-clock-fault-manifest.json',manifest(clock_fault))
                clock_legacy_fault=out/'native-clock-legacy-fault-source'
                shutil.copytree(frozen,clock_legacy_fault)
                target=clock_legacy_fault/'control_tape.c'
                text=target.read_text()
                if text.count('if(plan->version==2) {')!=1:raise ValueError('native clock descriptor fault anchor changed')
                target.write_text(text.replace('if(plan->version==2) {','if(plan->version==1 || plan->version==2) {'))
                save(out/'native-clock-legacy-fault-manifest.json',manifest(clock_legacy_fault))
                for target_name,source in (('native-clock.exe',frozen),('native-clock-no-fx.exe',clock_fault),('native-clock-legacy-oob.exe',clock_legacy_fault)):
                    argv=[compiler,'-O2','-Wall','-Wextra','-msse2','-DCONTROL_TAPE_NATIVE_CLOCK','-DCONTROL_TAPE_TEST','-I',str(source),str(source/'wine_native_clock_test.c'),str(source/'control_tape.c'),'-ladvapi32','-o',str(out/target_name)]
                    save(out/(target_name+'.compile.json'),argv)
                    subprocess.run(argv,check=True,stdout=log,stderr=subprocess.STDOUT,timeout=30)
"""
    if not a.native_clock_legacy_fault:
        compile_clock=replace_once(compile_clock,
            "(('native-clock.exe',frozen),('native-clock-no-fx.exe',clock_fault),('native-clock-legacy-oob.exe',clock_legacy_fault))",
            "(('native-clock.exe',frozen),('native-clock-no-fx.exe',clock_fault))")
    s=replace_once(s,"            shutil.copy2(out/'input-owner.dll',out/'unready-owner.dll')",
                   compile_clock+"            shutil.copy2(out/'input-owner.dll',out/'unready-owner.dll')")
    s=replace_once(s,"            for case in (*cases,'factory-owner-first','factory-proxy-first'):",
                   "            for case in (*cases,'factory-owner-first','factory-proxy-first','native-clock','native-clock-no-fx','native-clock-legacy-oob'):")
    s=replace_once(s,'run_case factory-owner-first factory-proof.exe owner-first',
                   'run_case native-clock native-clock.exe accept\nrun_case native-clock-no-fx native-clock-no-fx.exe accept\nrun_case native-clock-legacy-oob native-clock-legacy-oob.exe accept\nrun_case factory-owner-first factory-proof.exe owner-first')
    if not a.native_clock_legacy_fault:
        s=replace_once(s,'run_case native-clock-legacy-oob native-clock-legacy-oob.exe accept\n','')
    compression='''retain_clock_snapshots() {
  for snapshot in baseline repeat observed; do
    gzip -n -c "/tmp/native-clock-$snapshot.bin" > "/work/$1/$snapshot.bin.gz" || exit 82
  done
}
command -v gzip > /work/gzip-path.txt || exit 82
gzip --version > /work/gzip-version.txt || exit 82
'''
    s=replace_once(s,'run_case native-clock native-clock.exe accept',
                   compression+'run_case native-clock native-clock.exe accept\nretain_clock_snapshots native-clock')
    s=replace_once(s,'run_case native-clock-no-fx native-clock-no-fx.exe accept',
                   'run_case native-clock-no-fx native-clock-no-fx.exe accept\nretain_clock_snapshots native-clock-no-fx')
    if a.control_writer_reference:
        s=replace_once(s,"            paths['qualify_input.py'] = Path(__file__).resolve()",
            "            paths['writer_reference.c'] = Path("+repr(str(a.control_writer_reference.resolve()))+")\n            paths['qualify_input.py'] = Path(__file__).resolve()")
        writer_compile="""                reference=out/'writer-reference-source'
                reference.mkdir()
                text=(frozen/'writer_reference.c').read_text()
                if text.count('#include <string.h>')!=1:raise ValueError('writer reference include anchor changed')
                text=text.replace('#include <string.h>','#include <string.h>\\n#include "control_writer_test.inc"')
                text=text.replace('fflush(', 'writer_flush(')
                (reference/'control_tape.c').write_text(text)
                save(out/'writer-reference-manifest.json',manifest(reference))
                argv=[compiler,'-O2','-Wall','-Wextra','-msse2','-DCONTROL_TAPE_NATIVE_CLOCK','-DCONTROL_TAPE_TEST','-I',str(frozen),str(frozen/'wine_native_clock_test.c'),str(reference/'control_tape.c'),'-ladvapi32','-o',str(out/'writer-reference.exe')]
                save(out/'writer-reference.compile.json',argv)
                subprocess.run(argv,check=True,stdout=log,stderr=subprocess.STDOUT,timeout=30)
"""
        s=replace_once(s,"            shutil.copy2(out/'input-owner.dll',out/'unready-owner.dll')",
                       writer_compile+"            shutil.copy2(out/'input-owner.dll',out/'unready-owner.dll')")
        s=replace_once(s,"'native-clock-legacy-oob'):","'native-clock-legacy-oob','writer-fast','writer-old','writer-fail'):")
        writer_script='''retain_writer() {
  gzip -n -c "/tmp/$1-tape.jsonl" > "/work/$1/writer-tape.jsonl.gz" || exit 82
  if [ "$1" = writer-fail ]; then gzip -n -c /tmp/writer-fail-failed.jsonl > "/work/$1/failed-tape.jsonl.gz" || exit 82; fi
}
run_case writer-fast native-clock.exe writer-fast
retain_writer writer-fast
run_case writer-old writer-reference.exe writer-old
retain_writer writer-old
run_case writer-fail native-clock.exe writer-fail
retain_writer writer-fail
'''
        s=replace_once(s,'run_case factory-owner-first factory-proof.exe owner-first',
                       writer_script+'run_case factory-owner-first factory-proof.exe owner-first')
if a.discovery_abi:
    s=replace_once(s,"pool.ArtifactBudget([out], 64 * 1024**2", "pool.ArtifactBudget([out], 4 * 1024**2")
    s=replace_once(s,"            paths['qualify_input.py'] = Path(__file__).resolve()",
        "            paths['wine_discovery_test.c'] = ROOT / 'tests/wine_discovery_test.c'\n            paths['discovery_trace.c'] = ROOT / 'tools/wine/capture/discovery_trace.c'\n            paths['discovery_trace.h'] = ROOT / 'tools/wine/capture/discovery_trace.h'\n            paths['qualify_input.py'] = Path(__file__).resolve()")
    compile_discovery="""                discovery_fault=out/'discovery-fault-source'
                discovery_fault.mkdir()
                for filename in ('discovery_trace.c','discovery_trace.h','wine_discovery_test.c'):
                    shutil.copy2(frozen/filename,discovery_fault/filename)
                target=discovery_fault/'discovery_trace.c'
                text=target.read_text()
                if text.count('fxrstor (%esp)')!=2:raise ValueError('discovery restoration anchors changed')
                target.write_text(text.replace('fxrstor (%esp)','nop'))
                save(out/'discovery-fault-manifest.json',manifest(discovery_fault))
                for target_name,source in (('discovery.exe',frozen),('discovery-no-fx.exe',discovery_fault)):
                    argv=[compiler,'-O2','-Wall','-Wextra','-msse2','-DDISCOVERY_TRACE_TEST','-I',str(source),str(source/'wine_discovery_test.c'),str(source/'discovery_trace.c'),'-ladvapi32','-o',str(out/target_name)]
                    save(out/(target_name+'.compile.json'),argv)
                    subprocess.run(argv,check=True,stdout=log,stderr=subprocess.STDOUT,timeout=30)
"""
    s=replace_once(s,"            shutil.copy2(out/'input-owner.dll',out/'unready-owner.dll')",
                   compile_discovery+"            shutil.copy2(out/'input-owner.dll',out/'unready-owner.dll')")
    s=replace_once(s,"            for case in (*cases,'factory-owner-first','factory-proxy-first'):",
                   "            for case in (*cases,'factory-owner-first','factory-proxy-first','discovery','discovery-no-fx'):")
    script='''retain_discovery() {
  for snapshot in baseline repeat observed; do
    gzip -n -c "/tmp/discovery-$snapshot.bin" > "/work/$1/$snapshot.bin.gz" || exit 82
  done
}
run_case discovery discovery.exe accept
retain_discovery discovery
run_case discovery-no-fx discovery-no-fx.exe accept
retain_discovery discovery-no-fx
'''
    s=replace_once(s,'run_case factory-owner-first factory-proof.exe owner-first',
                   script+'run_case factory-owner-first factory-proof.exe owner-first')
# Discovery is a standalone owned stub test, not an implicit rerun of the
# unrelated input/factory suite. Preserve the qualified execution boundary.
if a.discovery_abi:
    begin=s.index("            names = ")
    end=s.index("            for name_, path in paths.items():",begin)
    s=s[:begin]+"            paths = {'wine_discovery_test.c': ROOT/'tests/wine_discovery_test.c',\n                     'discovery_trace.c': ROOT/'tools/wine/capture/discovery_trace.c',\n                     'discovery_trace.h': ROOT/'tools/wine/capture/discovery_trace.h',\n                     'qualify_input.py': Path(__file__).resolve()}\n"+s[end:]
    begin=s.index("            fault = out / 'fault-source'")
    end=s.index("            compiler = ",begin)
    s=s[:begin]+s[end:]
    begin=s.index("                for variant, source in ")
    end=s.index("                discovery_fault=",begin)
    s=s[:begin]+s[end:]
    begin=s.index("            shutil.copy2(out/'input-owner.dll'")
    end=s.index("            script = ",begin)
    s=s[:begin]+"            cases = ('discovery','discovery-no-fx')\n            for case in cases:\n                (out/case).mkdir()\n"+s[end:]
    s='\n'.join(line for line in s.split('\n') if not
        (line.startswith('run_case ') and not line.startswith(('run_case discovery ', 'run_case discovery-no-fx '))))
    begin=s.index("            result['factory_exits'] = ")
    end=s.index("            result['helper_exits'] = ",begin)
    s=s[:begin]+s[end:]
    begin=s.index("            result['execution_finished'] = ")
    end=s.index("            save(out / 'result.json'",begin)
    s=s[:begin]+"            result['execution_finished'] = (result.get('container_exit') == 0 and result.get('container_removed') and\n                result.get('seed_unchanged') and result.get('producer_unchanged') and\n                result.get('helper_exits') == {'discovery':'0','discovery-no-fx':'1'})\n"+s[end:]
# Retain execution output even if extra cases fail: parent auditor must check them.
if a.artifact_budget_root:
    # Reuse the existing composite guard without a nested retention row. Execute
    # the retained derived runner in-process so its writer/maintenance locks can
    # remain held through these independent postchecks and final publication.
    parent=a.artifact_budget_root.resolve(strict=True)
    s=replace_once(s,'def run(out, seed, image_name):',
                   'def run(out, seed, image_name):\n    global _prepaid_budget')
    s=replace_once(s,"    out.mkdir(parents=True, exist_ok=False)\n    artifact_budget = pool.ArtifactBudget([out], "+str(4 if a.discovery_abi else 8)+" * 1024**2, link_policy='count', retained=True)",
                   '    artifact_budget = None')
    child_mib=4 if a.discovery_abi else 8
    admission="""        spec_=importlib.util.spec_from_file_location('owned_console_prepaid',ROOT/'tools/parity/prepaid_budget.py')
        prepaid=importlib.util.module_from_spec(spec_);spec_.loader.exec_module(prepaid)
        artifact_budget=prepaid.LeaseBudgetAdapter.admit(pool.ArtifactLimit,Path(PARENT),out,ROOT/'local/storage-index',PARENT_MAX,child_max=CHILD_MIB*1024**2)
        _prepaid_budget=artifact_budget
""".replace('CHILD_MIB',str(child_mib)).replace('PARENT_MAX',str(a.artifact_parent_max_mib*1024**2)).replace('PARENT',repr(str(parent)))
    s=replace_once(s,"        result['admission'] = lease.description",admission+"        result['admission'] = lease.description")
if a.print_runner:
    print(s)
    raise SystemExit(0)
out.parent.mkdir(parents=True,exist_ok=True)
runner_parent=a.runner_parent.resolve(strict=True) if a.runner_parent else out.parent
if not runner_parent.is_dir() or runner_parent==ROOT/'ref' or ROOT/'ref' in runner_parent.parents:
    raise SystemExit('invalid runner artifact parent')
runner=runner_parent/(out.name+'-runner.py')
with runner.open('x') as f:f.write(s)
derivation={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in (Path(__file__).resolve(),Path(__file__).with_name('qualify_input.py'),runner)}
(runner_parent/(out.name+'-derivation.json')).write_text(json.dumps(derivation,indent=2)+'\n')
prepaid_budget=None
if a.artifact_budget_root:
    namespace={'__name__':'owned_console_derived','__file__':str(runner)}
    try:
        exec(compile(s,str(runner),'exec'),namespace)
        core=namespace['run'](out,a.prefix_seed.resolve(),namespace['IMAGE'])
        status=0 if core.get('execution_finished') else 1
    finally:
        prepaid_budget=namespace.get('_prepaid_budget')
        if prepaid_budget:atexit.register(prepaid_budget.close)
else:
    status=subprocess.call(['python3','-I',str(runner),'--out',str(out),'--prefix-seed',str(a.prefix_seed.resolve())])
if status:raise SystemExit(status)
checks={}
if a.control_writer_reference:
    for case,expected in (('writer-fast',0),('writer-old',1),('writer-fail',0)):
        folder=out/case;report=json.loads((folder/'helper.log').read_text())
        rows=[json.loads(line) for line in gzip.decompress((folder/'writer-tape.jsonl.gz').read_bytes()).splitlines()]
        events=sorted((row for row in rows if row['event']=='api_return'),key=lambda row:row['seq'])
        status=rows[-1]
        checks[case]=((folder/'exit.txt').read_text().strip()==str(expected) and report['rows']==2000 and report['stop_result']==2000 and
                      (report['elapsed_ms']<5000 if expected==0 else report['elapsed_ms']>=5000) and
                      (case!='writer-fail' or report['first_result']<0) and len(events)==2000 and
                      [row['seq'] for row in events]==list(range(1,2001)) and
                      [row['result'] for row in events]==list(range(0x12340000,0x12340000+2000)) and
                      status['entered']==status['returned']==2000 and status['phases']==4000 and
                      all(status[key]==0 for key in ('active','pending','dropped','gaps','accepting')))
if a.discovery_abi:
    for case,expected in (('discovery',0),('discovery-no-fx',1)):
        report=json.loads((out/case/'helper.log').read_text())
        snapshots=[gzip.decompress((out/case/(name+'.bin.gz')).read_bytes())
                   for name in ('baseline','repeat','observed')]
        checks[case]=((out/case/'exit.txt').read_text().strip()==str(expected) and
                      report['cases']==384 and report['unstable']==0 and
                      report['reserved']==384 and report['pending']==report['dropped']==report['stack_limits']==0 and
                      len(snapshots[0])==384*576 and snapshots[0]==snapshots[1] and
                      audit_discovery_states(snapshots[0]) and audit_discovery_states(snapshots[1]) and
                      (audit_discovery_states(snapshots[2]) if expected==0 else not audit_discovery_states(snapshots[2])) and
                      (snapshots[0]==snapshots[2] and report['failures']==report['saved_failures']==report['expectation_failures']==0
                       if expected==0 else snapshots[0]!=snapshots[2] and report['failures']>0))
if a.native_clock:
    for case,expected in (('native-clock',0),('native-clock-no-fx',1)):
        report=json.loads((out/case/'helper.log').read_text())
        checks[case]=((out/case/'exit.txt').read_text().strip()==str(expected) and
                      report['cases']==384 and report['unstable']==0 and
                      report.get('legacy_descriptor_refused_safely') is True and
                      (report['failures']==0 if expected==0 else report['failures']>0))
    # Independent raw-state and original-store witness check, not merely the
    # fixture's pass boolean. Nonclock calls must produce no records.
    clock_dir=out/'native-clock'
    snapshots=[gzip.decompress((clock_dir/(name+'.gz')).read_bytes()) for name in ('baseline.bin','repeat.bin','observed.bin')]
    rows=[json.loads(line) for line in (clock_dir/'native-clock-tape.jsonl').read_text().splitlines()]
    returns=sorted((row for row in rows if row['event']=='native_clock_return'),key=lambda row:row['ordinal'])
    status=rows[-1]
    expected_bits=[int.from_bytes(snapshots[0][i*576+560:i*576+564],'little') for i in range(0,384,2)]
    checks['native-clock-raw-witness']=(len(snapshots[0])==384*576 and snapshots[0]==snapshots[1]==snapshots[2] and
        len(returns)==192 and [row['value_f32_bits'] for row in returns]==expected_bits and
        [row['ordinal'] for row in returns]==list(range(1,193)) and
        all(row['callee']==0x47bfe0 and row['caller']==0x420090 for row in returns) and
        status['format_version']==9 and status['phases']==192 and
        all(status[key]==0 for key in ('entered','returned','active','pending','dropped','gaps','accepting')))
    if a.native_clock_legacy_fault:
        legacy=json.loads((out/'native-clock-legacy-oob/helper.log').read_text())
        checks['native-clock-legacy-oob']=((out/'native-clock-legacy-oob/exit.txt').read_text().strip()=='91' and
                                          legacy=={'scope':'owned_descriptor_guard_page','legacy_descriptor_oob':True})
for case in (() if a.discovery_abi else ('factory-owner-first','factory-proxy-first')):
    if a.setup_failure:
        folders=[out/case]
        if a.setup_failure=='all':folders += [out/(case+'-'+mode) for mode in ('missing-owner','unready-owner','missing-system','long-system','missing-module','missing-factory','missing-env','long-env','exact-env','long-log-path')]
        checks[case]=all((folder/'exit.txt').read_text().strip()=='0' and 'factory_calls=0' in (folder/'helper.log').read_text() for folder in folders)
        continue
    if a.depth:
        setup=[json.loads(line) for line in (out/case/'factory-setup.jsonl').read_text().splitlines()]
        checks[case]=(out/case/'exit.txt').read_text().strip()=='0' and sum(row.get('event')=='factory_gap' and row.get('reason')=='depth_overflow' for row in setup)==3
        continue
    if a.bulk_owned:
        checks[case]=(out/case/'exit.txt').read_text().strip()=='0' and 'bulk20_raw_equal=1' in (out/case/'helper.log').read_text()
        continue
    if a.real_construction:
        raw=(out/case/'actual-factory-return.bin').read_bytes();device=(out/case/'actual-device-return.bin').read_bytes()
        tape=[json.loads(line) for line in (out/case/'actual-tape.jsonl').read_text().splitlines()]
        status=tape[-1]
        complete=status.get('event')=='status' and status.get('entered')==status.get('returned') and all(status.get(k)==0 for k in ('gaps','dropped','pending','active','accepting'))
        checks[case]=complete and len(raw)==1680 and raw[:560]==raw[560:1120]==raw[1120:] and len(device)==1680 and device[:560]==device[560:1120]==device[1120:]
        continue
    if a.device_raw:
        raw=(out/case/'device-return.bin').read_bytes();entry=(out/case/'device-entry.bin').read_bytes()
        checks[case]=len(raw)==1680 and raw[:560]==raw[560:1120]==raw[1120:] and len(entry)==1728 and entry[:560]==entry[560:1120]==entry[1120:1680] and entry[1680:1696]==entry[1696:1712]==entry[1712:]
        continue
    raw=(out/case/'factory-raw.bin').read_bytes()
    checks[case]=(out/case/'exit.txt').read_text().strip()=='0' and len(raw)==1680 and raw[:560]==raw[560:1120]==raw[1120:]
if a.bulk_owned:
    audit=subprocess.run(['python3','-I',str(Path(__file__).with_name('check_bulk_owned.py')),str(out)]+(['--overflow'] if a.bulk_overflow else []),capture_output=True,text=True,timeout=10)
    (out/'bulk-audit.txt').write_text(audit.stdout+audit.stderr)
    checks['bulk_schema_audit']=audit.returncode==0
result=json.loads((out/'result.json').read_text())
result['factory_raw_checks']=checks
result['factory_mode'], result['scope'] = qualification_metadata(a)
result['factory_fault_lineage']={} if a.discovery_abi else {'intervention':a.fault,'original_proxy_sha256':hashlib.sha256((ROOT/'tools/parity/stock/stock_input_proxy.c').read_bytes()).hexdigest(),'original_input_sha256':hashlib.sha256((ROOT/'tools/parity/stock/input_observer.inc').read_bytes()).hexdigest(),'frozen_input_sha256':hashlib.sha256((out/'source/input_observer.inc').read_bytes()).hexdigest(),'frozen_proxy_sha256':hashlib.sha256((out/'source/stock_input_proxy.c').read_bytes()).hexdigest()}

result['functional_execution_finished']=result.get('execution_finished',False)
result['execution_finished']=result['functional_execution_finished'] and all(checks.values())
result['factory_transparency_qualified']=False  # entry/seam controls do not qualify lazy setup, concurrency or unwind
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
(out/'factory-checks.json').write_text(json.dumps({'cases':checks,'qualified':False,'mode':result['factory_mode'],'scope':result['scope']},indent=2)+'\n')
if prepaid_budget:
    try:
        prepaid_budget.check('final')
        encoded=gzip.compress((json.dumps(prepaid_budget.budget.report(),indent=2)+'\n').encode(),mtime=0)
        (out/'prepaid-audit.json.gz').write_bytes(encoded)
        result['artifact_finalization']='verified'
        (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        prepaid_budget.check('final')
    except (OSError,ValueError,prepaid_budget.artifact_limit) as error:
        result.update(execution_finished=False,artifact_finalization='failed',artifact_error=str(error))
        (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        raise
    finally:
        prepaid_budget.close()
raise SystemExit(0 if all(checks.values()) else 1)
