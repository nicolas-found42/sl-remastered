"""Small owned prefix admission controls; no Docker or real prefix cloning."""
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('stock_runner', Path(__file__).parents[1] / 'run_stock.py')
stock = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stock)
WOW = '58d036888d638f05a72193702f97071aabc9baf7959e7736d48d9c450acc3440'
INPUT = '0332b5210a9f287b0c75aee67d3c8e1a5e5a7b05ad5d51e1c8526685b152b582'
IMAGE = 'sha256:owned-test-image'


class PrefixAdmissionTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.seed = self.root/'retained'/'prefix'
        self.out = self.root/'new';self.out.mkdir()
        self.seed.mkdir(parents=True)
        for relative, contents in [('system.reg', b'system'),('user.reg',b'user'),('userdef.reg',b'default'),
                ('drive_c/windows/system32/wowbox64.dll', b'qualified-wow'),
                ('drive_c/windows/syswow64/dinput8.dll', b'qualified-input')]:
            path=self.seed/relative;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(contents)
        self.result = dict(image_id=IMAGE,execution_finished=True,container_removed=True,helper_exits={'actual':'0'})
        self.report = dict(passed=True,runtime_identity={'image_id':IMAGE,'BOX64_DYNAREC_SAFEFLAGS':2,
            'wowbox64_file':WOW+'  /usr/lib/wine/aarch64-windows/wowbox64.dll',
            'dinput8_file':INPUT+'  /work/prefix/drive_c/windows/syswow64/dinput8.dll'})
        self.checks=self.seed.parent/'checks-reviewed.json'
        self.write_reports()
        original=stock.digest
        def owned_digest(path):
            contents=Path(path).read_bytes()
            # Fixture substitutions stand in for exact retained module bytes;
            # altered fixture bytes retain their true hash and must refuse.
            if contents==b'qualified-wow':return WOW
            if contents==b'qualified-input':return INPUT
            return original(path)
        self.addCleanup(mock.patch.stopall)
        mock.patch.object(stock,'digest',side_effect=owned_digest).start()
        self.check_output=mock.patch.object(stock.subprocess,'check_output',return_value=b'').start()
        self.run=mock.patch.object(stock.subprocess,'run',side_effect=self.copy).start()

    def write_reports(self):
        (self.seed.parent/'result.json').write_text(json.dumps(self.result))
        self.checks.write_text(json.dumps(self.report))

    def copy(self,argv,**kwargs):
        self.assertEqual(argv[:4],['cp','-c','-p','-R'])
        shutil.copytree(argv[4],argv[5],symlinks=True)
        return subprocess.CompletedProcess(argv,0)

    def admit(self):
        return stock.clone_stopped_prefix(self.seed,self.checks,self.out,IMAGE,'owned-docker-never-executed')

    def test_manifest_private_inodes_and_provenance(self):
        before=stock.prefix_manifest(self.seed)
        result=self.admit()
        self.assertTrue(result['private_inodes'])
        self.assertEqual(before,stock.prefix_manifest(self.out/'wineprefix'))
        self.assertEqual(before,stock.prefix_manifest(self.seed))
        self.assertEqual(result['qualification_sha256'],stock.digest(self.checks))

    def test_ephemeral_preparation_binds_seed_without_host_prefix_clone(self):
        frozen = self.out / 'instrumentation-source'
        frozen.mkdir()
        before = stock.prefix_manifest(self.seed)
        # The existing full-Career auditor has its own corruption suite. This
        # boundary test isolates retention after that auditor has admitted data.
        with mock.patch.object(stock, 'qualify_functional_seed', return_value={'qualification': 'owned fixture'}):
            result = stock.prepare_ephemeral_prefix(self.seed, self.checks, self.out, IMAGE, 'owned')
        self.assertEqual(result['storage'], 'private_tmpfs_with_retained_delta')
        self.assertFalse((self.out / 'wineprefix').exists())
        self.run.assert_not_called()
        self.assertEqual(stock.prefix_archive.parse_manifest(frozen / 'expected-prefix.tsv'), before)
        self.assertEqual(stock.prefix_manifest(self.seed), before)

    def test_console_seed_cannot_substitute_for_complete_functional_seed(self):
        frozen = self.out / 'instrumentation-source'
        frozen.mkdir()
        with self.assertRaisesRegex(ValueError, 'functional seed'):
            stock.prepare_ephemeral_prefix(self.seed, self.checks, self.out, IMAGE, 'owned')
        self.run.assert_not_called()
        self.assertFalse((frozen / 'expected-prefix.tsv').exists())

    def test_changed_each_actual_module_refuses_before_clone(self):
        for relative in ('system32/wowbox64.dll','syswow64/dinput8.dll'):
            with self.subTest(relative=relative):
                p=self.seed/'drive_c/windows'/relative;old=p.read_bytes();p.write_bytes(b'changed')
                with self.assertRaises(ValueError):self.admit()
                p.write_bytes(old)
        self.run.assert_not_called()

    def test_wrong_image_or_backend_or_report_module_refuses(self):
        for key,value in [('image_id','wrong'),('BOX64_DYNAREC_SAFEFLAGS',1),('wowbox64_file','wrong'),('dinput8_file','wrong')]:
            with self.subTest(key=key):
                old=self.report['runtime_identity'][key];self.report['runtime_identity'][key]=value;self.write_reports()
                with self.assertRaises(ValueError):self.admit()
                self.report['runtime_identity'][key]=old;self.write_reports()
        self.run.assert_not_called()

    def test_foreign_check_report_parent_refuses(self):
        foreign=self.root/'foreign.json';foreign.write_text(json.dumps(self.report))
        with self.assertRaises(ValueError):stock.clone_stopped_prefix(self.seed,foreign,self.out,IMAGE,'owned')
        self.run.assert_not_called()

    def test_unstopped_seed_refuses(self):
        self.result['container_removed']=False;self.write_reports()
        with self.assertRaises(ValueError):self.admit()
        self.run.assert_not_called()

    def test_live_container_mount_refuses(self):
        self.check_output.side_effect=[b'owned-container\n',json.dumps([{'Mounts':[{'Source':str(self.seed.parent)}]}]).encode()]
        with self.assertRaises(ValueError):self.admit()
        self.run.assert_not_called()

    def test_hardlinked_clone_refuses(self):
        def linked(argv,**kwargs):
            shutil.copytree(argv[4],argv[5],symlinks=True,copy_function=__import__('os').link)
        self.run.side_effect=linked
        with self.assertRaises(ValueError):self.admit()
        self.assertTrue((self.out/'prefix-seed-before.json').is_file())

    def test_partial_clone_failure_preserves_beforemanifest(self):
        def failed(argv,**kwargs):
            Path(argv[5]).mkdir();raise subprocess.CalledProcessError(1,argv)
        self.run.side_effect=failed
        with self.assertRaises(subprocess.CalledProcessError):self.admit()
        self.assertEqual(json.loads((self.out/'prefix-seed-before.json').read_text()),stock.prefix_manifest(self.seed))

    def test_seed_module_symlink_refuses(self):
        p=self.seed/'drive_c/windows/system32/wowbox64.dll';target=self.root/'foreign-wow';p.rename(target);p.symlink_to(target)
        with self.assertRaises(ValueError):self.admit()
        self.run.assert_not_called()

    def test_partial_failure_audits_without_success_result(self):
        def failed(argv,**kwargs):raise subprocess.CalledProcessError(1,argv)
        self.run.side_effect=failed
        with self.assertRaises(subprocess.CalledProcessError):self.admit()
        self.assertTrue(stock.audit_prefix_seed(self.seed,self.out,'prefix-seed-after.json'))
        (self.seed/'user.reg').write_bytes(b'changed after failed clone')
        self.assertFalse(stock.audit_prefix_seed(self.seed,self.out,'prefix-seed-after-parent.json'))
        self.assertNotEqual(json.loads((self.out/'prefix-seed-after.json').read_text()),
                            json.loads((self.out/'prefix-seed-after-parent.json').read_text()))

    def test_missing_beforemanifest_never_claims_preservation(self):
        self.assertIsNone(stock.audit_prefix_seed(self.seed,self.out,'unused.json'))
        self.assertFalse((self.out/'unused.json').exists())

    def test_actual_ephemeral_runtime_modules_must_remain_pinned(self):
        rows = [WOW + '  /usr/lib/wine/aarch64-windows/wowbox64.dll',
                WOW + '  /privateprefix/drive_c/windows/system32/wowbox64.dll',
                INPUT + '  /privateprefix/drive_c/windows/syswow64/dinput8.dll']
        data = '\n'.join(rows) + '\n'
        before, after = (self.out / name for name in ('input-runtime-sha256.txt', 'input-runtime-after-sha256.txt'))
        before.write_text(data)
        after.write_text(data)
        self.assertEqual(len(stock.audit_runtime_modules(self.out, '/privateprefix')), 3)
        final = self.out / 'input-runtime-final-sha256.txt'
        final.write_text(data)
        final_prefix = {'drive_c/windows/system32/wowbox64.dll': {'sha256': WOW},
                        'drive_c/windows/syswow64/dinput8.dll': {'sha256': INPUT}}
        self.assertEqual(len(stock.audit_runtime_modules(self.out, '/privateprefix', final_prefix)), 3)
        final_prefix['drive_c/windows/syswow64/dinput8.dll']['sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'final prefix'):
            stock.audit_runtime_modules(self.out, '/privateprefix', final_prefix)
        for mutation in (data.replace(WOW, '0' * 64), '\n'.join(rows[:-1]) + '\n', data + rows[0] + '\n'):
            before.write_text(mutation)
            after.write_text(mutation)
            with self.assertRaises(ValueError):
                stock.audit_runtime_modules(self.out, '/privateprefix')
        before.write_text(data)
        after.write_text(data.replace(INPUT, '0' * 64))
        with self.assertRaises(ValueError):
            stock.audit_runtime_modules(self.out, '/privateprefix')


if __name__=='__main__':unittest.main(verbosity=2)
