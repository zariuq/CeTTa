#!/usr/bin/env python3
"""Build the checked D0 preparation functions as an in-process provider."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import ctypes


MODULE = 'Mettapedia.GSLT.LanguageDef.ModuleFormat.DatalogRuntimeCompiler'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lean-root', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--skip-build', action='store_true')
    parser.add_argument('--package-output', type=Path,
                        help='write the packaged language with a relative compiler resource')
    args = parser.parse_args()
    root = args.lean_root.resolve()
    output = args.output.resolve()
    engine = Path(__file__).resolve().parents[1]
    output.parent.mkdir(parents=True, exist_ok=True)
    work = output.parent / (output.name + '.build')
    work.mkdir(exist_ok=True)
    if not args.skip_build:
        subprocess.run(['lake', '--wfail', 'build', '+' + MODULE + ':c.o.export'], cwd=root, check=True)
    stem = root / '.lake/build/ir' / MODULE.replace('.', '/')
    setup = json.loads(stem.with_suffix('.setup.json').read_text())
    objects = [Path(str(stem) + '.c.o.export')]
    targets = {}
    for module, artifacts in setup['importArts'].items():
        if not artifacts or not artifacts[0]:
            continue
        source_artifact = str(artifacts[0][0])
        if '/.lake/build/lib/lean/' not in source_artifact:
            continue  # Lean/Std runtime modules come from libleanshared.
        path = source_artifact.removesuffix('.olean').replace('/.lake/build/lib/lean/', '/.lake/build/ir/')
        obj = Path(path + '.c.o.export')
        if not obj.is_absolute():
            obj = root / obj
        objects.append(obj)
        targets[obj] = '+' + module + ':c.o.export'
    objects = list(dict.fromkeys(objects))
    # The root object's facet checks imported interfaces, not every imported
    # native object. Existing .c.o.export files may therefore be stale after
    # a dependency edit. Ask Lake to check all facets, rather than treating
    # file existence as evidence that native code matches the checked imports.
    if not args.skip_build:
        subprocess.run(['lake', '--wfail', 'build', *targets.values()], cwd=root, check=True)
    missing = [str(p) for p in objects if not p.is_file()]
    if missing:
        raise SystemExit('Missing native import objects; build their c.o.export facets: ' + ', '.join(missing))
    generated = Path(str(stem) + '.c').read_text()
    initializer = re.findall(r'LEAN_EXPORT lean_object\* (initialize_\w+)\(uint8_t builtin\)', generated)
    if len(initializer) != 1:
        raise SystemExit('Expected one generated compiler initializer')
    sysroot = Path(subprocess.check_output(['lake', 'env', 'lean', '--print-prefix'], cwd=root, text=True).strip())
    source = engine / 'native/datalog_source_compiler_v1.c'
    shim = work / 'provider.o'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC',
                    '-I' + str(sysroot / 'include'), '-I' + str(engine),
                    '-DCETTA_DATALOG_COMPILER_INITIALIZER=' + initializer[0],
                    '-c', str(source), '-o', str(shim)], check=True)
    response = work / 'objects.rsp'
    response.write_text('\n'.join(shlex.quote(str(p)) for p in [shim, *objects]) + '\n')
    exports = work / 'exports.map'
    exports.write_text('{ global: cetta_source_compiler_v1; local: *; };\n')
    pending = work / 'provider.so'
    subprocess.run([os.environ.get('CXX', 'c++'), '-shared', '-o', str(pending), '@' + str(response),
                    '-L' + str(sysroot / 'lib/lean'), '-lleanshared', '-lLake_shared', '-pthread', '-ldl',
                    '-Wl,--no-undefined', '-Wl,--gc-sections', '-Wl,--version-script=' + str(exports),
                    '-Wl,-rpath,' + str(sysroot / 'lib/lean')], check=True)
    os.replace(pending, output)
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    manifest = {'module': MODULE, 'provider_abi': 1, 'provider_sha256': digest(output),
                'compiler_source_sha256': digest(root / (MODULE.replace('.', '/') + '.lean')),
                'shim_sha256': digest(source), 'native_objects': {str(p): digest(p) for p in objects},
                'runtime_subprocesses': False, 'inference_on_host': False}
    output.with_suffix(output.suffix + '.json').write_text(json.dumps(manifest, indent=2) + '\n')
    if args.package_output:
        package = args.package_output.resolve()
        package.parent.mkdir(parents=True, exist_ok=True)
        lib = ctypes.CDLL(str(output))
        call_type = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_size_t), ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_size_t))
        class Provider(ctypes.Structure):
            _fields_ = [('abi', ctypes.c_uint32), ('name', ctypes.c_char_p),
                        ('call', call_type), ('release', ctypes.CFUNCTYPE(None, ctypes.c_void_p))]
        lib.cetta_source_compiler_v1.restype = ctypes.POINTER(Provider)
        provider = lib.cetta_source_compiler_v1().contents
        assert provider.abi == 1
        resource = os.path.relpath(output, package.parent).encode()
        reply, length = ctypes.c_void_p(), ctypes.c_size_t()
        completed = provider.call(b'package', (ctypes.c_char_p * 1)(resource),
            (ctypes.c_size_t * 1)(len(resource)), 1, ctypes.byref(reply), ctypes.byref(length))
        if not completed or not reply:
            raise SystemExit('Compiler package generation did not return a value')
        try:
            result = json.loads(ctypes.string_at(reply, length.value))
        finally:
            provider.release(reply)
        if not result['ok']:
            raise SystemExit(result['message'])
        package.write_text(result['value']['module'])
    print(f'Built source compiler provider with {len(objects)} native module objects')


if __name__ == '__main__':
    main()
