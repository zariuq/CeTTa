#!/usr/bin/env python3
"""Build and relocate a C SDK, then run shared and static consumers."""
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CONSUMER = r'''
#include "cetta_embed.h"
#include <assert.h>
#include <string.h>
/* These names must not bind the runtime's private implementation. */
int atom_int(void) { return 11; }
void arena_free(void *pointer) { (void)pointer; }
int g_symbols;
int main(int argc, char **argv) {
    cetta_engine_config_t config = {sizeof(config), CETTA_EMBED_ABI_VERSION,
        "petta", NULL, argc > 1 ? argv[1] : NULL};
    cetta_engine_t *engine = NULL;
    assert(cetta_embed_engine_new(&config, &engine) == CETTA_OK);
    const char *source = "!(import! &self (library tensor)) "
        "!(tensor:sum (tensor:from-values f64 (2) (2.0 3.0)))";
    cetta_result_t *result = NULL;
    assert(cetta_embed_run(engine, source, strlen(source), NULL, &result) == CETTA_OK);
    size_t count = cetta_embed_result_count(result);
    assert(count > 0);
    cetta_atom_t *answer = NULL;
    double number = 0.;
    assert(cetta_embed_result_get(result, count - 1u, &answer) == CETTA_OK);
    assert(cetta_embed_atom_get_float(answer, &number) == CETTA_OK && number == 5.);
    cetta_embed_result_release(result);
    cetta_embed_engine_close(engine);
    assert(cetta_embed_atom_get_float(answer, &number) == CETTA_OK && number == 5.);
    cetta_embed_atom_release(answer);
    cetta_embed_engine_release(engine);
    return 0;
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix='cetta-install-', dir=os.environ.get('CETTA_TEST_TMPDIR')) as temporary:
        directory = Path(temporary)
        prefix, stage = directory / 'configured prefix', directory / 'staging'
        subprocess.run(['make', '-j1', '-s', 'install-cabi', f'PREFIX={prefix}', f'DESTDIR={stage}'],
                       cwd=ROOT, check=True)
        installed = stage / prefix.relative_to(prefix.anchor)
        env = dict(os.environ, PKG_CONFIG_LIBDIR=str(installed / 'lib/pkgconfig'), PKG_CONFIG_PATH='')
        def pc(*args):
            return subprocess.check_output(['pkg-config', *args, 'cetta'], env=env, text=True).strip()
        source = directory / 'consumer.c'; source.write_text(CONSUMER)
        compiler = shlex.split(os.environ.get('CC', 'cc'))
        shared, static = directory / 'shared', directory / 'static'
        subprocess.run(compiler + ['-std=c11', str(source), '-o', str(shared)] +
                       shlex.split(pc('--cflags', '--libs')), check=True)
        static_flags = shlex.split(pc('--static', '--libs'))
        static_flags = [str(installed / 'lib/libcetta.a') if flag == '-lcetta' else flag for flag in static_flags]
        subprocess.run(compiler + ['-std=c11', str(source), '-o', str(static)] +
                       shlex.split(pc('--cflags')) + static_flags, check=True)
        rust_consumers = []
        if rustc := shutil.which('rustc'):
            for kind in ('shared', 'static'):
                consumer = directory / ('rust-' + kind)
                command = [rustc, '--edition=2021', str(ROOT / 'tests/support/embedding_consumer.rs'),
                           '-o', str(consumer), '-L', 'native=' + str(installed / 'lib')]
                if kind == 'shared': command += ['-l', 'dylib=cetta']
                else:
                    command += ['-l', 'static=cetta']
                    for flag in static_flags:
                        if flag == '-L' + str(installed / 'lib') or flag.endswith('libcetta.a'): continue
                        command += ['-C', 'link-arg=' + flag]
                subprocess.run(command, check=True)
                rust_consumers.append(consumer)
        dynamic = subprocess.check_output(['readelf', '-d', str(installed / 'lib/libcetta.so.1')], text=True)
        assert re.search(r'\(SONAME\).*\[libcetta.so.1\]', dynamic), dynamic
        public = set(re.findall(r'\b(cetta_embed_[a-zA-Z0-9_]+)\s*\(',
                                (installed / 'include/cetta_embed.h').read_text()))
        symbols = subprocess.check_output(['nm', '-P', '--defined-only', '--extern-only',
                                          str(installed / 'lib/libcetta.a')], text=True)
        defined = {line.split()[0] for line in symbols.splitlines()
                   if not line.endswith(':') and len(line.split()) >= 2}
        assert defined == public, (defined - public, public - defined)
        relocated = directory / 'relocated SDK'
        installed.rename(relocated)
        env['LD_LIBRARY_PATH'] = str(relocated / 'lib')
        env.pop('CETTA_ROOT', None)
        subprocess.run([str(shared)], cwd=directory, env=env, check=True)
        subprocess.run([str(static), str(relocated / 'share/cetta')], cwd=directory, env=env, check=True)
        for consumer in rust_consumers:
            subprocess.run([str(consumer), str(relocated / 'share/cetta')], cwd=directory, env=env, check=True)
        env['PKG_CONFIG_LIBDIR'] = str(relocated / 'lib/pkgconfig')
        resource_path = shlex.split(pc('--variable=resource_dir'))
        assert len(resource_path) == 1 and Path(resource_path[0]).resolve() == (relocated / 'share/cetta').resolve()
        print(f'EmbeddingInstall shared/static relocation, {len(rust_consumers)} Rust consumers, private symbols and {len(public)} public exports pass')


if __name__ == '__main__':
    main()
