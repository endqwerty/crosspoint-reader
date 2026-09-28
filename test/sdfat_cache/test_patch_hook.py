from pathlib import Path
import importlib.util, tempfile, shutil, unittest, subprocess, types, sys
from unittest.mock import patch
REPO=Path(__file__).resolve().parents[2]
SOURCE=Path(sys.argv.pop(1)).resolve()
spec=importlib.util.spec_from_file_location('firmware_patch',REPO/'scripts/patch_sdfat.py')
hook=importlib.util.module_from_spec(spec);spec.loader.exec_module(hook)
real_run=subprocess.run

def run_without_git_locks(*args,**kwargs):
    kwargs['env']=dict(kwargs.get('env',{}),GIT_OPTIONAL_LOCKS='0')
    return real_run(*args,**kwargs)

class Environment(dict):
    def __init__(self,root,builders,**fields):
        super().__init__(PROJECT_DIR=str(root),PIOENV='unit',**fields);self.builders=builders
    def GetLibBuilders(self): return self.builders
    def subst(self,key):
        assert key=='$PROJECT_LIBDEPS_DIR'
        return str(Path(self['PROJECT_DIR'])/'.pio/libdeps')

class PatchHook(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='crosspoint-sdfat-hook-')
        self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
        self.dependency=self.root/'.pio/libdeps/unit/SdFat'
        shutil.copytree(REPO/'scripts',self.root/'scripts')
        for relative in ['library.properties',*[p[1] for p in hook.PATCHES]]:
            target=self.dependency/relative;target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(SOURCE/relative,target)
        self.original={relative:(self.dependency/relative).read_bytes() for _,relative,_,_ in hook.PATCHES}
        self.addCleanup(patch.stopall)
        patch.object(hook.subprocess,'run',side_effect=run_without_git_locks).start()
    def apply(self):hook.apply_patches(self.root,self.dependency)
    def assertUnchanged(self):
        for relative,data in self.original.items():self.assertEqual((self.dependency/relative).read_bytes(),data)
    def builder(self,path=None):return types.SimpleNamespace(name='SdFat',is_dependent=True,path=str(path or self.dependency))
    def test_known_sources_apply_and_match_expected_hashes(self):
        self.apply()
        for _,relative,_,expected in hook.PATCHES:self.assertEqual(hook.digest(self.dependency/relative),expected)
    def test_repeated_application_preserves_bytes_and_mtime(self):
        self.apply();before={relative:((self.dependency/relative).stat().st_mtime_ns,(self.dependency/relative).read_bytes()) for _,relative,_,_ in hook.PATCHES}
        self.apply()
        for relative,state in before.items():self.assertEqual(((self.dependency/relative).stat().st_mtime_ns,(self.dependency/relative).read_bytes()),state)
    def test_unknown_second_source_leaves_first_untouched(self):
        p=self.dependency/'src/SdFatConfig.h';p.write_text(p.read_text()+'\n// unknown\n')
        with self.assertRaisesRegex(RuntimeError,'Unrecognized'):self.apply()
        self.assertEqual((self.dependency/'src/common/FsCache.cpp').read_bytes(),self.original['src/common/FsCache.cpp'])
    def test_wrong_version_changes_nothing(self):
        p=self.dependency/'library.properties';p.write_text(p.read_text().replace('version=2.3.1','version=2.3.2'))
        with self.assertRaisesRegex(RuntimeError,'2.3.1'):self.apply()
        self.assertUnchanged()
    def test_unknown_patch_rejected_before_mutation(self):
        (self.root/'scripts/sdfat_patches'/next(p[0] for p in hook.PATCHES if p[1]=='src/SdFatConfig.h')).write_text('not a patch\n')
        with self.assertRaisesRegex(RuntimeError,'does not apply'):self.apply()
        self.assertUnchanged()
    def test_escaping_target_symlink_rejected(self):
        p=self.dependency/'src/SdFatConfig.h';outside=self.root/'outside.h';shutil.copy2(p,outside);p.unlink();p.symlink_to(outside)
        with self.assertRaisesRegex(RuntimeError,'escaping'):self.apply()
        self.assertEqual(outside.read_bytes(),self.original['src/SdFatConfig.h'])
    def test_escaping_dependency_rejected(self):
        outside=self.root/'outside';shutil.copytree(self.dependency,outside)
        with self.assertRaisesRegex(RuntimeError,'inside'):hook.apply_patches(self.root,outside)
        self.assertUnchanged()
    def test_selected_dependency_applies(self):
        hook.patch_selected_dependency(Environment(self.root,[self.builder()]))
        for _,relative,_,expected in hook.PATCHES:self.assertEqual(hook.digest(self.dependency/relative),expected)
    def test_wrong_environment_rejected(self):
        env=Environment(self.root,[self.builder()]);env['PIOENV']='other'
        with self.assertRaisesRegex(RuntimeError,'selected environment'):hook.patch_selected_dependency(env)
        self.assertUnchanged()
    def test_missing_selection_rejected(self):
        with self.assertRaisesRegex(RuntimeError,'exactly one'):hook.patch_selected_dependency(Environment(self.root,[]))
        self.assertUnchanged()
    def test_ambiguous_selection_rejected(self):
        with self.assertRaisesRegex(RuntimeError,'exactly one'):hook.patch_selected_dependency(Environment(self.root,[self.builder(),self.builder()]))
        self.assertUnchanged()
    def test_sdk_only_pass_skips_dependency_lookup(self):
        hook.patch_selected_dependency({'ARDUINO_LIB_COMPILE_FLAG':'Build'})
        self.assertUnchanged()
    def test_unselected_builder_is_ignored(self):
        builder=self.builder();builder.is_dependent=False
        with self.assertRaisesRegex(RuntimeError,'exactly one'):hook.patch_selected_dependency(Environment(self.root,[builder]))
        self.assertUnchanged()

if __name__=='__main__':unittest.main(verbosity=2)
