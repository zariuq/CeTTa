"""Optional Cesta/t-ISA/GCC mixed-leaf qualification. No default-build dependency."""

import argparse
import csv
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import shutil
import statistics
import struct
import subprocess
import sys
import tempfile
import time

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
PILOT=HERE.parent/'hol/pilot'
sys.path.insert(0,str(PILOT))
from elf_symbol import select, assert_identity

spec=importlib.util.spec_from_file_location('copy_pilot',PILOT/'run.py')
COPY=importlib.util.module_from_spec(spec);spec.loader.exec_module(COPY)
SYMBOLS=('prefix_length_tisa','copy_span_copy_u32','mixed_kernel_copy')


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def input_identities(cesta,tcalc):
    own=['src/base.c','src/atom.c','src/digest.c','src/cesta.h','src/digest.h',
         'checked/examples/CopySpan.cesta','adapters/hol/x86_support.ml']
    own += [str(p.relative_to(ROOT)) for p in HERE.rglob('*')
            if p.is_file() and p.suffix in ('.c','.cesta','.py','.ml','.txt')]
    own += [str(p.relative_to(ROOT)) for p in PILOT.glob('*.ml')]
    external=['tisa/tisa.h','tisa/tc_theory_inspect.h','tcalc.h',
              'tisa/tisa_core.c','tisa/tisa_emit_x86.c','tisa/tisa_specialize.c']
    return {'cesta/binary':sha(cesta),
            **{'cesta/'+name:sha(ROOT/name) for name in sorted(set(own))},
            **{'tcalc/'+name:sha(tcalc/name) for name in external}}


def run(args,work,log,accepted=True):
    args=list(map(str,args));start=time.monotonic()
    with log.open('ab') as out:
        out.write((shlex.join(args)+'\n').encode());out.flush()
        result=subprocess.run(args,cwd=work,stdout=out,stderr=subprocess.STDOUT,
                              env={**os.environ,'LC_ALL':'C'})
    if (result.returncode==0)!=accepted:
        raise RuntimeError(f'command status {result.returncode}: {shlex.join(args)}; see {log}')
    return time.monotonic()-start


def backend(work,cesta,tcalc,log):
    output=work/'cesta-tisa'
    command=['gcc','-std=c11','-O2','-g0','-Wall','-Wextra','-Werror',
        '-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
        '-I'+str(ROOT/'src'),'-I'+str(tcalc),HERE/'cesta_tisa.c',
        ROOT/'src/base.c',ROOT/'src/atom.c',ROOT/'src/digest.c',
        tcalc/'tisa/tisa_core.c',tcalc/'tisa/tisa_emit_x86.c',tcalc/'tisa/tisa_specialize.c','-o',output]
    duration=run(command,work,log)
    source=work/'PrefixLength.cesta';source.write_bytes((HERE/'examples/PrefixLength.cesta').read_bytes())
    command=[output,cesta,source,'count','prefix_length_tisa',work/'prefix.S',work/'prefix.json']
    emitted=run(command,work,log)
    manifest=json.loads((work/'prefix.json').read_text())
    assert manifest['source_sha256']==sha(source)
    alternate=[*command]
    tool_clang=work/'cesta-tisa-clang'
    compile_clang=['clang','-std=c11','-O2','-g0','-Wall','-Wextra','-Werror',
        '-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
        '-I'+str(ROOT/'src'),'-I'+str(tcalc),HERE/'cesta_tisa.c',
        ROOT/'src/base.c',ROOT/'src/atom.c',ROOT/'src/digest.c',
        tcalc/'tisa/tisa_core.c',tcalc/'tisa/tisa_emit_x86.c',tcalc/'tisa/tisa_specialize.c','-o',tool_clang]
    run(compile_clang,work,log)
    alternate[0]=tool_clang;alternate[-2]=work/'prefix-clang.S';alternate[-1]=work/'prefix-clang.json'
    run(alternate,work,log)
    assert (work/'prefix.S').read_bytes()==(work/'prefix-clang.S').read_bytes()
    assert json.loads((work/'prefix-clang.json').read_text())==manifest
    # The same admitted source has a GCC/Clang reference; unrelated C remains available.
    run([output,cesta,ROOT/'checked/examples/CopySpan.cesta','copy_u32','unsupported',
         work/'unsupported.S',work/'unsupported.json'],work,log,accepted=False)
    moving=work/'moving.cesta';moving.write_bytes(source.read_bytes())
    changer=work/'changing-admission'
    changer.write_text('#!/usr/bin/env python3\nimport pathlib,sys\np=pathlib.Path(sys.argv[-1])\np.write_bytes(p.read_bytes()+b"\\n")\n')
    changer.chmod(0o700)
    run([output,changer,moving,'count','changed_source',work/'changed.S',work/'changed.json'],work,log,accepted=False)
    return dict(tool_build_seconds=duration,source_admission_and_emission_seconds=emitted,
                source_sha256=sha(source),host_compiler_emission_identity=True,
                source_change_during_admission_refused=True,program=manifest['program'])


def scalar_profile(work,cesta,tcalc,log):
    fixtures={
        'bool':('bool','bool','a','true','false'),
        'u32':('u32','u32','(le a b)','a','b'),
        'u64':('u64','u64','(lt a b)','a','b'),
        'literal':('u32','u32','(ne a b)','(u32 4294967295)','(u32 0)'),
    }
    for name,(parameter,result,condition,yes,no) in fixtures.items():
        out=work/('scalar-'+name);out.mkdir()
        source=out/'source.cesta'
        source.write_text(f'''(cesta checked-c/1 (module scalar_ref) (word-bits 64)
 (failure scalar_ref_failure)
 (fn choose (params (a {parameter}) (b {parameter})) (returns {result}) (effects)
  (body (if {condition} (then (return {yes})) (else (return {no}))))))''')
        run([work/'cesta-tisa',cesta,source,'choose','scalar_native',out/'native.S',out/'native.json'],out,log)
        run([cesta,'export-c',source,'-o',out/'reference.c'],out,log)
        run([cesta,'checked','header',source,'-o',out/'reference.h'],out,log)
        program=json.loads((out/'native.json').read_text())['program']
        steps=sum(x['kind']=='FUEL_STEP' for x in program['x86_64_artifact']['emitted_spans'])
        mask='1' if parameter=='bool' else 'UINT32_MAX' if parameter=='u32' else 'UINT64_MAX'
        (out/'host.c').write_text(f'''#include "reference.h"
#include "tisa/tisa.h"
#include <stdlib.h>
uint64_t scalar_native(TcIsaRuntimeFrame*,uint64_t,uint64_t);
void scalar_ref_failure(void){{abort();}}
int main(void){{
 uint64_t x=UINT64_C(0xabcdef01deadbeef);
 for(unsigned i=0;i<1000;i++){{
  x^=x<<13;x^=x>>7;x^=x<<17;uint64_t a=x&{mask};
  x^=x<<13;x^=x>>7;x^=x<<17;uint64_t b=x&{mask};
  TcIsaRuntimeFrame f={{0}};
  uint64_t n=scalar_native(&f,a,b);
  if(n!=(uint64_t)scalar_ref_choose(a,b)||f.status!=1||f.fuel_used!={steps})return 1;
 }}return 0;}}
''')
        for cc in ('gcc','clang'):
            run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(tcalc),
                 'native.S','reference.c','host.c','-o',cc],out,log)
            run([out/cc],out,log)
    return dict(types=list(fixtures),cases_per_type_and_compiler=1000)


def export(work,cesta,tcalc,log):
    for source,name in [(ROOT/'checked/examples/CopySpan.cesta','copy'),
                         (HERE/'examples/PrefixLength.cesta','count'),
                         (HERE/'examples/MixedCopy.cesta','checked')]:
        run([cesta,'export-c',source,'-o',work/(name+'.c'),'--plan',work/(name+'.plan')],work,log)
        run([cesta,'checked','header',source,'-o',work/(name+'.h')],work,log)
    run([cesta,'checked','contract',ROOT/'checked/examples/CopySpan.cesta',
         '-o',work/'copy.contract.json'],work,log)
    (work/'mixed_kernel.h').write_text('''#ifndef CESTA_MIXED_KERNEL_H
#define CESTA_MIXED_KERNEL_H
#include "tisa/tisa.h"
#include "copy.h"
#include "count.h"
#include "checked.h"
uint64_t prefix_length_tisa(TcIsaRuntimeFrame *,uint64_t,uint64_t);
size_t mixed_kernel_copy(TcIsaRuntimeFrame *,cesta_read_u32_span,cesta_write_u32_span);
#endif
''')
    run([cesta,'export-c',HERE/'examples/MixedKernel.cesta','-o',work/'mixed.c'],work,log)
    # Original, authoritative ABI declarations accompany the standalone C.
    (work/'abi/tisa').mkdir(parents=True)
    for name in ('tisa/tisa.h','tisa/tc_theory_inspect.h','tcalc.h'):
        shutil.copyfile(tcalc/name,work/'abi'/name)
    for label,path in [('CESTA-LICENSE',ROOT/'LICENSE'),('TCALC-LICENSE',tcalc/'LICENSE')]:
        if path.is_file():shutil.copyfile(path,work/label)
    shutil.copyfile(HERE/'host.c',work/'host.c')
    shutil.copyfile(PILOT/'failure.c',work/'failure.c')
    shutil.copyfile(PILOT/'text.ld',work/'text.ld')


def runtime_assembly(symbols):
    text=['.section .text.cesta_mixed,"ax",@progbits','.p2align 4',
          '.globl cesta_mixed_blob','cesta_mixed_blob:','.incbin "image.text"']
    for name,s in symbols.items():
        text += [f'.globl {name}',f'.type {name},@function',
                 f'.set {name},cesta_mixed_blob+{s.offset}',f'.size {name},{s.size}']
    return '\n'.join(text+['.section .note.GNU-stack,"",@progbits',''])


def build_image(work,cc,cesta,log):
    flags=COPY.FLAGS[cc]+['-std=c11','-Wall','-Wextra','-Werror','-I.','-Iabi']
    durations={}
    for source in ('prefix.S','copy.c','mixed.c','failure.c'):
        durations[source]=run([cc,*flags,'-c',source,'-o',Path(source).with_suffix('.o')],work,log)
    command=['ld','--build-id=none','-e','mixed_kernel_copy','-T','text.ld',
             'prefix.o','copy.o','mixed.o','failure.o','-o','image.elf']
    run(command,work,log)
    image=work/'image.elf';symbols={name:select(image,name) for name in SYMBOLS}
    raw=bytes.fromhex(json.loads((work/'prefix.json').read_text())['program']['x86_64_artifact']['bytes_hex'])
    assert symbols['prefix_length_tisa'].code==raw
    (work/'image.text').write_bytes(symbols['prefix_length_tisa'].text)
    (work/'runtime.S').write_text(runtime_assembly(symbols))
    run([cc,'-c','runtime.S','-o','runtime.o'],work,log)
    if subprocess.check_output(['nm','-u','runtime.o'],cwd=work):
        raise RuntimeError('static runtime object has an unresolved dependency')
    # Authored Cesta and readable C must produce the same object instructions.
    for name,source in [('copy',ROOT/'checked/examples/CopySpan.cesta'),
                        ('mixed',HERE/'examples/MixedKernel.cesta')]:
        run([cesta,'cc','--cc',cc,*flags,'-c',source,'-o',name+'-authored.o'],work,log)
        for label in (name,name+'-authored'):
            run(['objcopy','-O','binary','--only-section=.text',label+'.o',label+'.text'],work,log)
        assert (work/(name+'.text')).read_bytes()==(work/(name+'-authored.text')).read_bytes()
    receipts={name:{**s.identity(),'code_sha256':hashlib.sha256(s.code).hexdigest()} for name,s in symbols.items()}
    return symbols,dict(compiler_command=[cc,*flags],link_command=command,
                        compiler_version=subprocess.check_output([cc,'--version'],text=True).splitlines()[0],
                        compilation_seconds=durations,symbols=receipts)


def controls(work,cc,symbols,log):
    common=['-std=c11','-g0','-Wall','-Wextra','-Werror','-I.','-Iabi','-fno-pie','-no-pie']
    for opt in ('-O0','-O2','-O1'):
        extra=['-fsanitize=address,undefined'] if opt=='-O1' else []
        binary='host'+opt
        run([cc,*common,opt,*extra,'runtime.o','count.c','checked.c','host.c','-o',binary],work,log)
        run([work/binary],work,log)
        for name,s in symbols.items():
            actual=select(work/binary,name)
            assert actual.code==s.code,(cc,name,'runtime bytes differ from proof image')
    # Instrument the actual C sources as a separate lane; blob instructions are not instrumented.
    run([cc,*common,'-O1','-fsanitize=address,undefined','prefix.o','copy.c','mixed.c',
         'count.c','checked.c','host.c','-o','source-sanitizer'],work,log)
    run([work/'source-sanitizer'],work,log)
    # Decode a changed, executable compare and require independent behavior to reject it.
    text=bytearray((work/'image.text').read_bytes())
    at=symbols['prefix_length_tisa'].offset+symbols['prefix_length_tisa'].code.index(bytes.fromhex('410f96c2'))+2
    text[at]=0x97
    (work/'mutant.text').write_bytes(text)
    (work/'mutant.S').write_text((work/'runtime.S').read_text().replace('"image.text"','"mutant.text"'))
    run([cc,*common,'-O2','mutant.S','count.c','checked.c','host.c','-o','mutant-host'],work,log)
    run([work/'mutant-host'],work,log,accepted=False)
    for name,s in symbols.items():
        assert_identity(work/'image.elf',name,s.identity())
    changed=bytearray((work/'image.elf').read_bytes())
    prefix=symbols['prefix_length_tisa'];changed[prefix.file_offset+at-prefix.offset]=0x97
    (work/'mutant.elf').write_bytes(changed)
    try:assert_identity(work/'mutant.elf','prefix_length_tisa',prefix.identity())
    except ValueError:pass
    else:raise RuntimeError('changed artifact accepted')
    wrong_target=bytearray((work/'image.elf').read_bytes())
    struct.pack_into('<H',wrong_target,18,183)
    (work/'wrong-target.elf').write_bytes(wrong_target)
    try:select(work/'wrong-target.elf','prefix_length_tisa')
    except ValueError:pass
    else:raise RuntimeError('wrong machine target accepted')
    wrong_extent=bytearray((work/'image.elf').read_bytes())
    struct.pack_into('<Q',wrong_extent,prefix.symbol_offset+16,prefix.size+1)
    (work/'wrong-extent.elf').write_bytes(wrong_extent)
    try:assert_identity(work/'wrong-extent.elf','prefix_length_tisa',prefix.identity())
    except ValueError:pass
    else:raise RuntimeError('changed symbol extent accepted')
    return dict(runtime_routes=4,scalar_cases_per_route=10081,copy_cases_per_route=4225,
                semantic_mutant_refused=True,changed_artifact_refused=True,
                changed_machine_refused=True,changed_symbol_extent_refused=True,
                runtime_bytes_identical=True,authored_exported_bytes_identical=True)


def offline(work,cc,log):
    flags=' '.join(COPY.FLAGS[cc]+['-std=c11','-Wall','-Wextra','-Werror','-I.','-Iabi'])
    makefile=f'''CC = {cc}
CFLAGS = {flags}
.PHONY: all check check-sources
all: check-sources host
check-sources:
\tsha256sum -c SHA256SUMS
prefix.o: prefix.S
\t$(CC) $(CFLAGS) -c $< -o $@
copy.o: copy.c copy.h
\t$(CC) $(CFLAGS) -c $< -o $@
mixed.o: mixed.c mixed_kernel.h copy.h count.h
\t$(CC) $(CFLAGS) -c $< -o $@
failure.o: failure.c
\t$(CC) $(CFLAGS) -c $< -o $@
image.elf: prefix.o copy.o mixed.o failure.o text.ld
\tld --build-id=none -e mixed_kernel_copy -T text.ld prefix.o copy.o mixed.o failure.o -o $@
image.text: image.elf
\tobjcopy -O binary --only-section=.text $< $@
runtime.o: runtime.S image.text
\t$(CC) -c runtime.S -o $@
host: runtime.o count.c checked.c host.c mixed_kernel.h checked.h
\t$(CC) $(CFLAGS) -no-pie runtime.o count.c checked.c host.c -o $@
check: all
\t./host
'''
    (work/'Makefile').write_text(makefile)
    files=['Makefile','prefix.S','copy.c','copy.h','count.c','count.h','checked.c','checked.h','mixed.c',
           'mixed_kernel.h','failure.c','host.c','text.ld','runtime.S',
           'abi/tisa/tisa.h','abi/tisa/tc_theory_inspect.h','abi/tcalc.h']
    files += [name for name in ('CESTA-LICENSE','TCALC-LICENSE') if (work/name).exists()]
    (work/'SHA256SUMS').write_text(''.join(f'{sha(work/name)}  {name}\n' for name in sorted(files)))
    relocated=work.parent/('offline-'+cc);relocated.mkdir()
    for name in [*files,'SHA256SUMS']:
        dest=relocated/name;dest.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(work/name,dest)
    run(['make','check'],relocated,log)
    assert (relocated/'image.text').read_bytes()==(work/'image.text').read_bytes()
    # Omission and mutation fail before rebuilding the portable sources.
    saved=(relocated/'copy.c').read_bytes()
    (relocated/'copy.c').write_bytes(saved+b'\n')
    run(['make','check-sources'],relocated,log,accepted=False)
    (relocated/'copy.c').write_bytes(saved)
    header=relocated/'count.h';header.rename(relocated/'count.h.omitted')
    run(['make','check-sources'],relocated,log,accepted=False)
    (relocated/'count.h.omitted').rename(header)
    return dict(relocated_plain_c_reproduction=True,source_mutation_refused=True,source_omission_refused=True)


def benchmark(work,cc,log):
    run([cc,'-std=c11','-O2','-g0','-Wall','-Wextra','-Werror','-I.','-Iabi',
         '-fno-pie','-no-pie','runtime.o','count.c',HERE/'bench.c','-o','benchmark'],work,log)
    result=subprocess.run([work/'benchmark'],cwd=work,capture_output=True,text=True)
    if result.returncode:raise RuntimeError('benchmark payload receipts differ')
    (work/'benchmark.csv').write_text(result.stdout)
    rows=list(csv.DictReader(result.stdout.splitlines()))
    assert len(rows)==7
    return dict(ordinary_median_ns=statistics.median(float(r['ordinary_ns']) for r in rows),
                mixed_median_ns=statistics.median(float(r['mixed_ns']) for r in rows),
                trials=7,iterations_per_trial=500000,warmup_iterations=20000,
                observation='same compiled copy leaf; ordinary C count versus explicit tISA frame/count',
                clock='monotonic wall time on the shared workstation')


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--out',type=Path)
    parser.add_argument('--cesta',type=Path,default=ROOT/'build/cesta')
    parser.add_argument('--tcalc',type=Path,default=ROOT.parent/'tcalc')
    parser.add_argument('--prove',action='store_true')
    parser.add_argument('--s2n',type=Path,default=ROOT.parent/'repos/s2n-bignum')
    parser.add_argument('--hol',type=Path,default=ROOT.parent/'repos/hol-light')
    args=parser.parse_args();cesta=args.cesta.resolve();tcalc=args.tcalc.resolve()
    work=args.out.resolve() if args.out else Path(tempfile.mkdtemp(prefix='mixed-leaves-',dir=ROOT/'build'))
    if args.out:work.mkdir(parents=True,exist_ok=False)
    log=work/'qualification.log'
    inputs=input_identities(cesta,tcalc)
    summary={'format':'cesta-mixed-leaves/1','proof_status':'not-run','runtime':{},
             'proof_scope':'exact selected count, copy and two-call wrapper artifacts',
             'source_lowering':'tested restricted profile; no general compiler refinement claim',
             'inputs':inputs}
    summary['backend']=backend(work,cesta,tcalc,log)
    summary['scalar_profile']=scalar_profile(work,cesta,tcalc,log)
    base=work/'export';base.mkdir();export(base,cesta,tcalc,log)
    for cc in ('gcc','clang'):
        out=work/cc;shutil.copytree(base,out)
        for name in ('prefix.S','prefix.json'):shutil.copyfile(work/name,out/name)
        symbols,record=build_image(out,cc,cesta,log)
        record['controls']=controls(out,cc,symbols,log)
        record['offline']=offline(out,cc,log)
        record['benchmark']=benchmark(out,cc,log)
        summary['runtime'][cc]=record
    if args.prove:
        from prove import prove
        summary['proof']=prove(work,args.s2n.resolve(),args.hol.resolve(),log)
        summary['proof_status']='accepted'
    if inputs!=input_identities(cesta,tcalc):
        raise RuntimeError('qualification inputs changed; no current receipt issued')
    (work/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print('mixed leaves: GCC/Clang static consumers qualified; exact byte identity; '
          '10081 scalar and 4225 copy cases per route; proof '+summary['proof_status'])


if __name__=='__main__':main()
