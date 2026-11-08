import argparse
import os
from pathlib import Path
import subprocess
import tempfile


parser = argparse.ArgumentParser(description='Run 32-bit Linux L4D2 regression tests.')
parser.add_argument('--sdk', type=Path, required=True)
parser.add_argument('--sourcemod', type=Path, required=True)
parser.add_argument('--metamod', type=Path, required=True)
parser.add_argument('--cxx', default='g++')
options = parser.parse_args()

repository = Path(__file__).resolve().parent.parent
sdk = options.sdk.resolve()
sourcemod = options.sourcemod.resolve()
metamod = options.metamod.resolve()
include_paths = [
    repository / 'src', metamod / 'core', metamod / 'core/sourcehook',
    sourcemod / 'public', sourcemod / 'public/amtl', sourcemod / 'public/amtl/amtl',
    sdk / 'public', sdk / 'public/engine', sdk / 'public/tier0',
    sdk / 'public/tier1', sdk / 'common',
]
for include_path in include_paths:
    if not include_path.is_dir():
        parser.error('Missing include directory: {}'.format(include_path))

compiler_flags = [
    '-m32', '-msse', '-mfpmath=sse', '-std=c++20', '-fno-strict-aliasing', '-ffunction-sections',
    '-fdata-sections', '-fsanitize=undefined,float-cast-overflow',
    '-fno-sanitize-recover=all', '-Wno-register',
    '-D_LINUX', '-DLINUX', '-DPOSIX', '-DHAVE_STDINT_H', '-DGNUC',
    '-DSE_LEFT4DEAD=15', '-DSE_LEFT4DEAD2=16', '-DSOURCE_ENGINE=16',
    '-DNO_HOOK_MALLOC', '-DNO_MALLOC_OVERRIDE',
    '-Dstricmp=strcasecmp', '-D_stricmp=strcasecmp',
    '-D_snprintf=snprintf', '-D_vsnprintf=vsnprintf',
]
compiler_flags += ['-I{}'.format(path) for path in include_paths]
library_path = sdk / 'lib/linux'
test_names = ['basicbinpatch', 'nextbotfixes', 'boomervomitpatch', 'cvarinfo']

with tempfile.TemporaryDirectory(prefix='tickrate-tests-') as build_directory:
    for test_name in test_names:
        executable = Path(build_directory) / test_name
        command = [options.cxx] + compiler_flags + [
            str(repository / 'tests/{}_test.cpp'.format(test_name)),
            '-Wl,--gc-sections', '-o', str(executable),
        ]
        if test_name in ['nextbotfixes', 'boomervomitpatch']:
            command += [
                str(library_path / 'tier1_i486.a'),
                '-L{}'.format(library_path), '-Wl,-rpath,{}'.format(library_path),
                '-ltier0_srv', '-lvstdlib_srv', '-ldl', '-lpthread',
            ]
        subprocess.run(command, check=True)
        environment = os.environ.copy()
        environment['LD_LIBRARY_PATH'] = str(library_path) + os.pathsep + environment.get('LD_LIBRARY_PATH', '')
        subprocess.run([str(executable)], check=True, env=environment)
        print('{}: passed'.format(test_name), flush=True)
