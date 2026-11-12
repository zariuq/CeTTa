"""Optional exact-object span-copy pilot using external HOL Light/s2n-bignum.

This proves a declared copy contract for selected compiler artifacts. It does
not prove the frontend's translation or general C execution.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

from elf_symbol import select, assert_identity
import contracts

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
SYMBOL='copy_span_copy_u32'
COMMON=['-O2','-g0','-fno-stack-protector','-fno-asynchronous-unwind-tables','-fno-pie']
FLAGS={'gcc':COMMON+['-fno-tree-vectorize','-fno-tree-loop-distribute-patterns'],
       'clang':COMMON+['-fno-vectorize','-fno-slp-vectorize','-fno-unroll-loops']}


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command,work,log,accepted=True):
    command=list(map(str,command))
    with log.open('ab') as stream:
        stream.write((shlex.join(command)+'\n').encode());stream.flush()
        result=subprocess.run(command,cwd=work,stdout=stream,stderr=subprocess.STDOUT,
                              env={**os.environ,'LC_ALL':'C'})
    if (result.returncode==0)!=accepted:
        raise RuntimeError(f'command status {result.returncode}: {shlex.join(command)}; see {log}')


def build(work,cesta,log):
    source=ROOT/'checked/examples/CopySpan.cesta'
    (work/'text.ld').write_bytes((HERE/'text.ld').read_bytes())
    run([cesta,'export-c',source,'-o',work/'copy.c','--plan',work/'copy.plan'],ROOT,log)
    run([cesta,'checked','header',source,'-o',work/'copy.h'],ROOT,log)
    run([cesta,'checked','contract',source,'-o',work/'copy.contract.json'],ROOT,log)
    contracts.load(work/'copy.contract.json',SYMBOL)
    run(['gcc',*COMMON,'-c',HERE/'failure.c','-o','failure.o'],work,log)
    records={}
    for cc,flags in FLAGS.items():
        command=[cc,*flags,'-c','copy.c','-o',f'copy-{cc}.o']
        run(command,work,log)
        image=work/f'copy-{cc}.elf'
        link=['ld','--build-id=none','-e',SYMBOL,'-T','text.ld',f'copy-{cc}.o','failure.o','-o',image.name]
        run(link,work,log)
        symbol=select(image,SYMBOL)
        if symbol.offset!=0:raise ValueError('pilot requires the selected function at text offset zero')
        (work/f'{cc}.text').write_bytes(symbol.text)
        records[cc]={**symbol.identity(),'symbol':SYMBOL,'compiler_command':command,
                     'link_command':link,
                     'compiler_version':subprocess.check_output([cc,'--version'],text=True).splitlines()[0],
                     'object_sha256':sha(work/f'copy-{cc}.o')}
    return records


def runtime_controls(work,records,log):
    cases=0
    for cc in FLAGS:
        host_flags=['-O1','-g','-std=c11','-Wall','-Wextra','-Werror','-I',work]
        run([cc,*host_flags,HERE/'host.c','-o',work/f'host-{cc}'],work,log)
        run([work/f'host-{cc}',work/f'{cc}.text'],work,log)
        run([cc,*host_flags,'-DCESTA_PILOT_NATIVE','-fsanitize=address,undefined','-fno-pie','-no-pie',
             HERE/'host.c','copy.c','-o',f'native-{cc}'],work,log)
        run([work/f'native-{cc}'],work,log)
        image=work/f'copy-{cc}.elf';symbol=select(image,SYMBOL)
        assert_identity(image,SYMBOL, {k:v for k,v in records[cc].items() if k in symbol.identity()})
        for name,offset,value in [('instruction',symbol.file_offset,0x90),
                                  ('extent',symbol.symbol_offset+16,0),
                                  ('target',18,183)]:
            data=bytearray(symbol.image);data[offset]=value
            path=work/f'{cc}-bad-{name}.elf';path.write_bytes(data)
            try:assert_identity(path,SYMBOL,symbol.identity())
            except ValueError:pass
            else:raise AssertionError('changed artifact accepted: '+name)
            cases+=1
    # A valid instruction change with wrong semantics: read destination
    # instead of source. The independent runtime and fresh HOL proof must fail.
    image=work/'copy-gcc.elf';symbol=select(image,SYMBOL)
    at=symbol.code.find(bytes.fromhex('8b0c87890c82'))
    if at<0:raise ValueError('unsupported copy-loop profile')
    data=bytearray(symbol.image);data[symbol.file_offset+at+2]=0x82
    mutant=work/'copy-mutant.elf';mutant.write_bytes(data)
    (work/'mutant.text').write_bytes(select(mutant,SYMBOL).text)
    run([work/'host-gcc',work/'mutant.text'],work,log,accepted=False)
    return cases


def bytes_declaration(work,cc,image):
    symbol=select(image,SYMBOL)
    literal=lambda data:'['+';'.join(f'0x{v:02x}' for v in data)+']'
    path=json.dumps(str(image))
    return f'''let copy_{cc}_image = define_assert_from_elf "copy_{cc}_image" {path} {literal(symbol.text)};;
let copy_{cc}_body_mc = define_assert_word_list "copy_{cc}_body_mc"
  (term_of_bytes(Bytes.sub (load_elf_contents_x86 {path}) {symbol.offset} {symbol.size})) {literal(symbol.code)};;
let COPY_{cc.upper()}_EXEC = X86_MK_EXEC_RULE copy_{cc}_body_mc;;
let COPY_{cc.upper()}_IN_IMAGE = BYTES_LOADED_SUBPROGRAM_RULE copy_{cc}_image copy_{cc}_body_mc {symbol.offset};;
'''


def prove(work,s2n,hol,log):
    notices=work/'dependency-licences';notices.mkdir()
    for label,source in (('s2n-LICENSE',s2n/'LICENSE'),('s2n-NOTICE',s2n/'NOTICE'),
                         ('hol-light-LICENSE',hol/'LICENSE')):
        if source.is_file():(notices/label).write_bytes(source.read_bytes())
    declarations=work/'bytes.ml'
    declarations.write_text(''.join(bytes_declaration(work,cc,work/f'copy-{cc}.elf') for cc in FLAGS))
    mutant=work/'mutant.ml'
    mutant.write_text(bytes_declaration(work,'mutant',work/'copy-mutant.elf')+'''
let mutant_refused =
  try ignore(cesta_copy_loop_rule copy_mutant_body_mc COPY_MUTANT_EXEC 0x32 0x3c 0x41); false
  with Failure _ -> true;;
if not mutant_refused then failwith "wrong copy instruction admitted";;
print_endline "CESTA_SEMANTIC_MUTANT_REFUSED";;
''')
    driver=work/'proof.ml'
    driver.write_text('needs "x86/proofs/base.ml";;\n'+
        '\n'.join('loadt '+json.dumps(str(path))+';;' for path in
            [declarations,HERE/'span_words.ml',HERE.parent/'x86_support.ml',HERE/'copy_loop.ml',HERE/'copy_function.ml',
             HERE/'span_resources.ml',mutant])+
        '\ncheck_axioms ();;\nprint_endline "CESTA_AXIOMS_CHECKED";;\n'+
        'Printf.printf "CESTA_ACCESS_TRACE %s\\n" (string_of_thm cesta_copy_trace);;\n'+
        '\n'.join(f'Printf.printf "CESTA_THEOREM {name} %s\\n" (string_of_thm {name});;' for name in
                  ['COPY_GCC_CORRECT','COPY_CLANG_CORRECT',
                   'COPY_GCC_RESOURCE_CORRECT','COPY_CLANG_RESOURCE_CORRECT'])+'\n')
    inlined=work/'proof.inlined.ml';compiled=work/'proof.inlined.cmx';binary=work/'proof.native'
    os.environ['HOLLIGHT_DIR']=str(hol)
    os.environ['HOLLIGHT_USE_MODULE']='1'
    run([hol/'hol.sh','inline-load',driver,inlined],s2n,log)
    run([hol/'hol.sh','compile',inlined,'-o',compiled,'-w','-a'],s2n,log)
    run([hol/'hol.sh','link',compiled,'-o',binary],s2n,log)
    proof_log=work/'proof.log'
    run([binary],s2n,proof_log)
    result=proof_log.read_text()
    if any(marker not in result for marker in ('CESTA_SEMANTIC_MUTANT_REFUSED',
                                               'CESTA_AXIOMS_CHECKED','CESTA_ACCESS_TRACE')) or any(
        f'CESTA_THEOREM {name} ' not in result for name in
            ('COPY_GCC_CORRECT','COPY_CLANG_CORRECT','COPY_GCC_RESOURCE_CORRECT','COPY_CLANG_RESOURCE_CORRECT')):
        raise RuntimeError('required proof results missing')
    return dict(proof_input_sha256=sha(inlined),proof_executable_sha256=sha(binary),
                hol_commit=subprocess.check_output(['git','-C',hol,'rev-parse','HEAD'],text=True).strip(),
                s2n_commit=subprocess.check_output(['git','-C',s2n,'rev-parse','HEAD'],text=True).strip())


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--out',type=Path)
    parser.add_argument('--s2n',type=Path,default=Path(os.environ.get('S2N',ROOT.parent/'repos/s2n-bignum')))
    parser.add_argument('--hol',type=Path,default=Path(os.environ.get('HOLDIR',ROOT.parent/'repos/hol-light')))
    parser.add_argument('--cesta',type=Path,default=ROOT/'build/cesta')
    parser.add_argument('--runtime-only',action='store_true')
    args=parser.parse_args()
    work=args.out.resolve() if args.out else Path(tempfile.mkdtemp(prefix='hol-copy-',dir=ROOT/'build'))
    if args.out:
        work.mkdir(parents=True,exist_ok=True)
        if any(work.iterdir()):parser.error('--out must be empty; previous evidence is retained')
    log=work/'commands.log'
    records=build(work,args.cesta.resolve(),log)
    mutations=runtime_controls(work,records,log)
    contract_mutations=contracts.mutation_controls(contracts.load(work/'copy.contract.json',SYMBOL),SYMBOL)
    relocated=work/'relocated';relocated.mkdir()
    second=build(relocated,args.cesta.resolve(),log)
    if any(records[cc]['image_sha256']!=second[cc]['image_sha256'] for cc in FLAGS):
        raise RuntimeError('linked image depends on build directory')
    if (work/'copy.contract.json').read_bytes() != (relocated/'copy.contract.json').read_bytes():
        raise RuntimeError('span boundary evidence depends on build directory')
    report=dict(format='cesta-hol-copy-pilot/1',contract='u32-span-copy-prefix',
                source_sha256=sha(ROOT/'checked/examples/CopySpan.cesta'),
                exported_c_sha256=sha(work/'copy.c'),permission_plan_sha256=sha(work/'copy.plan'),
                span_contract_sha256=sha(work/'copy.contract.json'),
                contract_mutations_refused=contract_mutations,
                artifacts=records,identity_mutations_refused=mutations,
                source_to_ir_refinement=False,constant_time_proof=False)
    if not args.runtime_only:
        report.update(prove(work,args.s2n.resolve(),args.hol.resolve(),log))
        report['theorems']=['COPY_GCC_CORRECT','COPY_CLANG_CORRECT']
        report['resource_bridge_theorems']=['COPY_GCC_RESOURCE_CORRECT','COPY_CLANG_RESOURCE_CORRECT']
        report['access_trace_proof']=True
        (work/'receipt.json').write_text(json.dumps(report,sort_keys=True,indent=2)+'\n')
        print('HOL copy pilot: both compiler calls, frames and access traces proved; semantic mutant refused; no new axioms')
    else:
        (work/'runtime.json').write_text(json.dumps(report,sort_keys=True,indent=2)+'\n')
        print('Copy runtime controls passed; HOL was not run and no proof receipt was issued')
    print('Evidence:',work)


if __name__=='__main__':main()
