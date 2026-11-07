#!/usr/bin/env python3
"""Build the in-process Lean source compiler providers over one shared runtime.

Lean registers options and environment extensions once per name, and only
while modules initialize, so every provider in a process must share one copy
of its modules. This tool links the union of the providers' compiled modules
into one shared library that initializes every provider root once, then one
thin provider shim per language that calls into it. Each provider then writes
its own package. Ordinary lang calls load these libraries; they launch no
compiler or other interpreter.
"""

import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess

# root module, operation prefix, provider name, provider file, package file
PROVIDERS = [
    ('Mettapedia.GSLT.LanguageDef.ModuleFormat.DatalogRuntimeProvider', 'datalog', 'DatalogD0',
     'datalog-compiler.so', 'langdef/lang/Datalog.module'),
]
RUNTIME = 'libcetta-lean-providers.so'


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_atomic(path, text):
    pending = path.with_name(path.name + f'.{os.getpid()}.pending')
    try:
        pending.write_text(text)
        os.replace(pending, path)
    finally:
        if pending.exists():
            pending.unlink()


def object_source_current(path):
    """Compare Lake's recorded C dependency hash, not timestamp ordering.

    Cached external objects may have no source receipt. They remain usable;
    an available receipt that contradicts the generated source is different.
    """
    source = Path(str(path).removesuffix('.o.export'))
    current_hash, receipt = Path(str(source) + '.hash'), Path(str(path) + '.trace')
    if not current_hash.is_file() or not receipt.is_file():
        return None
    expected = current_hash.read_text().strip()

    def recorded(value):
        if isinstance(value, list):
            if len(value) == 2 and value[0] == str(source) and isinstance(value[1], str):
                return value[1]
            for child in value:
                found = recorded(child)
                if found is not None:
                    return found
        return None

    prior = recorded(json.loads(receipt.read_text()).get('inputs', []))
    return None if prior is None else prior == expected


def native_objects(root, module, build):
    if build:
        subprocess.run(['lake', '--wfail', 'build', '+' + module + ':c.o.export'], cwd=root, check=True)
    stem = root / '.lake/build/ir' / module.replace('.', '/')
    setup = json.loads(stem.with_suffix('.setup.json').read_text())
    objects = [Path(str(stem) + '.c.o.export')]
    targets = {}
    for imported, artifacts in setup['importArts'].items():
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
        targets[obj] = '+' + imported + ':c.o.export'
    # The root facet checks imported interfaces, not every imported native
    # object, so all import facets are built explicitly.
    if build:
        subprocess.run(['lake', '--wfail', 'build', *targets.values()], cwd=root, check=True)
    generated = Path(str(stem) + '.c').read_text()
    initializer = re.findall(r'LEAN_EXPORT lean_object\* (initialize_\w+)\(uint8_t builtin\)', generated)
    if len(initializer) != 1:
        raise SystemExit('Expected one generated initializer in ' + module)
    return objects, initializer[0]


def call_provider(path, operation, argument):
    lib = ctypes.CDLL(str(path))
    call_type = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_char_p,
        ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_size_t), ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_size_t))

    class Provider(ctypes.Structure):
        _fields_ = [('abi', ctypes.c_uint32), ('name', ctypes.c_char_p),
                    ('call', call_type), ('release', ctypes.CFUNCTYPE(None, ctypes.c_void_p))]
    lib.cetta_source_compiler_v1.restype = ctypes.POINTER(Provider)
    provider = lib.cetta_source_compiler_v1().contents
    assert provider.abi == 1
    data = argument.encode()
    reply, length = ctypes.c_void_p(), ctypes.c_size_t()
    completed = provider.call(operation.encode(), (ctypes.c_char_p * 1)(data),
        (ctypes.c_size_t * 1)(len(data)), 1, ctypes.byref(reply), ctypes.byref(length))
    if not completed or not reply:
        raise SystemExit(operation + ' did not return a value from ' + str(path))
    try:
        return json.loads(ctypes.string_at(reply, length.value))
    finally:
        provider.release(reply)


def main(arguments=None, providers=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lean-root', required=True, type=Path)
    parser.add_argument('--output-dir', required=True, type=Path)
    parser.add_argument('--skip-build', action='store_true')
    parser.add_argument('--no-packages', action='store_true',
                        help='build providers without rewriting the engine language packages')
    args = parser.parse_args(arguments)
    providers = PROVIDERS if providers is None else providers
    root = args.lean_root.resolve()
    engine = Path(__file__).resolve().parents[1]
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    work = output / 'lean-providers.build'
    work.mkdir(exist_ok=True)
    cc, cxx = os.environ.get('CC', 'cc'), os.environ.get('CXX', 'c++')
    sysroot = Path(subprocess.check_output(['lake', 'env', 'lean', '--print-prefix'], cwd=root, text=True).strip())
    objects, roots = [], []
    for module, prefix, _, _, _ in providers:
        module_objects, initializer = native_objects(root, module, not args.skip_build)
        objects += module_objects
        roots.append((initializer, f'cetta_{prefix}_force_constants_v1'))
    objects = list(dict.fromkeys(objects))
    missing = [str(p) for p in objects if not p.is_file()]
    if missing:
        raise SystemExit('Missing native module objects: ' + ', '.join(missing[:8]))
    stale = [str(p) for p in objects if object_source_current(p) is False]
    if stale:
        raise SystemExit('Native object receipts name different generated C; rebuild before linking: ' + ', '.join(stale[:8]))
    header = work / 'lean_provider_roots_v1.h'
    header.write_text('#define CETTA_LEAN_PROVIDER_ROOTS(X) \\\n' +
                      ''.join(f'    X({init}, {force}) \\\n' for init, force in roots) + '\n')
    runtime_object = work / 'runtime.o'
    flags = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC', '-I' + str(sysroot / 'include'), '-I' + str(engine)]
    subprocess.run([cc, *flags, '-I' + str(work), '-c', str(engine / 'native/lean_provider_runtime_v1.c'),
                    '-o', str(runtime_object)], check=True)
    response = work / 'objects.rsp'
    response.write_text('\n'.join(shlex.quote(str(p)) for p in [runtime_object, *objects]) + '\n')
    runtime_exports = work / 'runtime.map'
    runtime_exports.write_text('{ global: cetta_*; local: *; };\n')
    runtime_pending = work / RUNTIME
    lean_link = ['-L' + str(sysroot / 'lib/lean'), '-lleanshared', '-lLake_shared', '-pthread', '-ldl',
                 '-Wl,-rpath,' + str(sysroot / 'lib/lean')]
    subprocess.run([cxx, '-shared', '-o', str(runtime_pending), '-Wl,-soname,' + RUNTIME, '@' + str(response),
                    *lean_link, '-Wl,--no-undefined', '-Wl,--version-script=' + str(runtime_exports)], check=True)
    os.replace(runtime_pending, output / RUNTIME)
    shim_exports = work / 'provider.map'
    shim_exports.write_text('{ global: cetta_source_compiler_v1; local: *; };\n')
    built = []
    for module, prefix, name, provider_file, _ in providers:
        shim = work / (prefix + '-provider.o')
        subprocess.run([cc, *flags, '-DCETTA_SOURCE_COMPILER_PREFIX=' + prefix,
                        '-DCETTA_SOURCE_COMPILER_NAME="' + name + '"', '-c',
                        str(engine / 'native/lean_source_compiler_v1.c'), '-o', str(shim)], check=True)
        pending = work / provider_file
        subprocess.run([cxx, '-shared', '-o', str(pending), str(shim), '-L' + str(output),
                        '-l:' + RUNTIME, "-Wl,-rpath,$ORIGIN", *lean_link, '-Wl,--no-undefined',
                        '-Wl,--version-script=' + str(shim_exports)], check=True)
        os.replace(pending, output / provider_file)
        built.append(output / provider_file)
    if not args.no_packages:
        for (module, prefix, name, provider_file, package_file), provider in zip(providers, built):
            package = engine / package_file
            resource = os.path.relpath(provider, package.parent)
            result = call_provider(provider, 'package', resource)
            if not result['ok']:
                raise SystemExit(result['message'])
            write_atomic(package, result['value']['module'])
    manifest = {'runtime': str(output / RUNTIME), 'runtime_sha256': digest(output / RUNTIME),
                'providers': [{'module': module, 'provider': str(output / provider_file),
                               'provider_sha256': digest(output / provider_file),
                               'module_source_sha256': digest(root / (module.replace('.', '/') + '.lean')),
                               'package': None if args.no_packages else package_file,
                               'package_sha256': None if args.no_packages else digest(engine / package_file)}
                              for module, _, _, provider_file, package_file in providers],
                'shim_sha256': digest(engine / 'native/lean_source_compiler_v1.c'),
                'runtime_source_sha256': digest(engine / 'native/lean_provider_runtime_v1.c'),
                'native_objects': len(objects),
                'native_source_receipts': {str(path): object_source_current(path) for path in objects},
                'native_object_pins': {str(path): digest(path) for path in objects},
                'runtime_subprocesses': False, 'inference_on_host': False}
    write_atomic(output / 'lean-providers.json', json.dumps(manifest, indent=2) + '\n')
    print(f'Built {len(providers)} source compiler providers over one runtime with {len(objects)} native module objects')


if __name__ == '__main__':
    main()
