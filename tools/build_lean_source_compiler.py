#!/usr/bin/env python3
"""Build a named Lean source provider within the common provider runtime.

The provider root exports the shared protocol and its force_constants hook.
Its modules join the registered roots before runtime initialization closes.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile

from build_lean_source_compilers import PROVIDERS, call_provider, main as build_providers


def build_provider(root, module, prefix, name, output, skip_build=False, package_output=None):
    root, output = root.resolve(), output.resolve()
    if not all(value.isascii() and value.isidentifier() for value in [prefix, name]):
        raise SystemExit('prefix and name must be ASCII identifiers')
    if not all(part.isascii() and part.isidentifier() for part in module.split('.')):
        raise SystemExit('module must be a qualified Lean module name')
    providers = list(PROVIDERS)
    matching = next((provider for provider in providers if provider[:3] == (module, prefix, name)), None)
    if matching is None:
        if any(provider[1] == prefix or provider[2] == name for provider in providers):
            raise SystemExit('provider prefix or name conflicts with a registered provider')
        matching = (module, prefix, name, prefix + '-compiler.so', None)
        providers.append(matching)
    options = ['--lean-root', str(root), '--output-dir', str(output.parent), '--no-packages']
    if skip_build:
        options.append('--skip-build')
    build_providers(options, providers=providers)
    _, _, _, filename, _ = matching
    built = output.parent / filename
    if built != output:
        # Keep the common runtime and canonical shim alongside custom names.
        with tempfile.NamedTemporaryFile(dir=output.parent, prefix=output.name + '.',
                                         suffix='.pending', delete=False) as staged:
            pending = Path(staged.name)
        try:
            shutil.copyfile(built, pending)
            os.replace(pending, output)
        finally:
            pending.unlink(missing_ok=True)
    shared = json.loads((output.parent / 'lean-providers.json').read_text())
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    manifest = {'module': module, 'provider_abi': 1, 'provider_sha256': digest(output),
                'compiler_source_sha256': digest(root / (module.replace('.', '/') + '.lean')),
                'shim_sha256': shared['shim_sha256'], 'native_objects': shared['native_object_pins'],
                'shared_runtime': shared['runtime'], 'shared_runtime_sha256': shared['runtime_sha256'],
                'runtime_subprocesses': False, 'inference_on_host': False}
    output.with_suffix(output.suffix + '.json').write_text(json.dumps(manifest, indent=2) + '\n')
    if package_output:
        package = package_output.resolve()
        package.parent.mkdir(parents=True, exist_ok=True)
        reply = call_provider(output, 'package', os.path.relpath(output, package.parent))
        if not reply['ok']:
            raise SystemExit(reply['message'])
        package.write_text(reply['value']['module'])
    print('Built ' + name + ' source provider over the common Lean runtime')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lean-root', required=True, type=Path)
    parser.add_argument('--module', required=True)
    parser.add_argument('--prefix', required=True)
    parser.add_argument('--name', required=True)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--skip-build', action='store_true')
    parser.add_argument('--package-output', type=Path)
    args = parser.parse_args()
    build_provider(args.lean_root, args.module, args.prefix, args.name, args.output,
                   args.skip_build, args.package_output)


if __name__ == '__main__':
    main()
