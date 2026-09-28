"""Prepare an isolated, source-pinned copy of the firmware storage dependency."""
import importlib.util
from pathlib import Path
import shutil
import sys

source, project, repository = (Path(arg).resolve() for arg in sys.argv[1:])
config = (repository / 'platformio.ini').read_text()
for required in ('-DUSE_SEPARATE_FAT_CACHE=1', 'greiman/SdFat @ 2.3.1', 'post:scripts/patch_sdfat.py'):
    if config.count(required) != 1:
        raise RuntimeError('Firmware storage configuration changed: ' + required)
dependency = project / '.pio/libdeps/host/SdFat'
if not dependency.exists():
    shutil.copytree(source, dependency)
patches = repository / 'scripts/sdfat_patches'
(project / 'scripts').mkdir(parents=True, exist_ok=True)
shutil.copytree(patches, project / 'scripts/sdfat_patches', dirs_exist_ok=True)
spec = importlib.util.spec_from_file_location('firmware_sdfat_patch', repository / 'scripts/patch_sdfat.py')
hook = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hook)
hook.apply_patches(project, dependency)
expected = {relative: patched for _, relative, _, patched in hook.PATCHES}
# Every compiled source/header must match the pinned archive or a reviewed patch.
for path in (source / 'src').rglob('*'):
    if path.is_file():
        relative = str(path.relative_to(source))
        if hook.digest(dependency / relative) != expected.get(relative, hook.digest(path)):
            raise RuntimeError('Unexpected test dependency source: ' + relative)
source_names = {str(p.relative_to(source)) for p in (source / 'src').rglob('*') if p.is_file()}
candidate_names = {str(p.relative_to(dependency)) for p in (dependency / 'src').rglob('*') if p.is_file()}
if source_names != candidate_names:
    raise RuntimeError('Unexpected test dependency file inventory')
