"""Replay exact selected artifacts in external HOL Light, including real calls."""

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import hashlib
import time

HERE=Path(__file__).resolve().parent
PILOT=HERE.parent/'hol/pilot'
sys.path.insert(0,str(PILOT))
from elf_symbol import select

spec=importlib.util.spec_from_file_location('copy_proof_pilot',PILOT/'run.py')
COPY=importlib.util.module_from_spec(spec);spec.loader.exec_module(COPY)


def declarations(work):
    literal=lambda xs:'['+';'.join(f'0x{b:02x}' for b in xs)+']'
    text=''
    for cc in ('gcc','clang'):
        image=work/cc/'image.elf'
        text+=COPY.bytes_declaration(work,cc,image)
        s=select(image,'mixed_kernel_copy')
        text+=f'''let mixed_{cc}_body_mc = define_assert_word_list "mixed_{cc}_body_mc"
  (term_of_bytes(Bytes.sub (load_elf_contents_x86 {json.dumps(str(image))}) {s.offset} {s.size})) {literal(s.code)};;
let MIXED_{cc.upper()}_BODY_EXEC = X86_MK_EXEC_RULE mixed_{cc}_body_mc;;
'''
    image=work/'gcc/image.elf';s=select(image,'prefix_length_tisa')
    text+=f'''let prefix_count_mc = define_assert_word_list "prefix_count_mc"
  (term_of_bytes(Bytes.sub (load_elf_contents_x86 {json.dumps(str(image))}) {s.offset} {s.size})) {literal(s.code)};;
let PREFIX_COUNT_EXEC = X86_MK_EXEC_RULE prefix_count_mc;;
'''
    mutant=work/'gcc/mutant.elf';s=select(mutant,'prefix_length_tisa')
    text+=f'''let prefix_mutant_image = define_assert_from_elf "prefix_mutant_image" {json.dumps(str(mutant))} {literal(s.text)};;
let prefix_mutant_mc = define_assert_word_list "prefix_mutant_mc"
  (term_of_bytes(Bytes.sub (load_elf_contents_x86 {json.dumps(str(mutant))}) {s.offset} {s.size})) {literal(s.code)};;
let PREFIX_MUTANT_EXEC = X86_MK_EXEC_RULE prefix_mutant_mc;;
'''
    for cc in ('gcc','clang'):
        image=work/cc/'image.elf';copy=select(image,'copy_span_copy_u32');mixed=select(image,'mixed_kernel_copy')
        text+=f'''let MIXED_{cc.upper()}_EXEC = cesta_join_exec_rule copy_{cc}_image
 [(0,prefix_count_mc,PREFIX_COUNT_EXEC);
  ({copy.offset},copy_{cc}_body_mc,COPY_{cc.upper()}_EXEC);
  ({mixed.offset},mixed_{cc}_body_mc,MIXED_{cc.upper()}_BODY_EXEC)];;
'''
    out=work/'bytes.ml';out.write_text(text);return out


def driver(work):
    paths=[HERE/'join_exec.ml',declarations(work),PILOT/'span_words.ml',HERE.parent/'hol/x86_support.ml',
           HERE/'proof_support.ml',HERE/'count_spec.ml',HERE/'count.ml',
           PILOT/'copy_loop.ml',PILOT/'copy_function.ml',PILOT/'span_resources.ml',
           HERE/'compose_spec.ml',HERE/'compose.ml']
    names=['PREFIX_COUNT_CORRECT','COPY_GCC_CORRECT','COPY_CLANG_CORRECT',
           'COPY_GCC_RESOURCE_CORRECT','COPY_CLANG_RESOURCE_CORRECT',
           'MIXED_GCC_CORRECT','MIXED_CLANG_CORRECT']
    text='needs "x86/proofs/base.ml";;\n'
    for path in paths:text+='loadt '+json.dumps(str(path))+';;\n'
    text+='''let semantic_mutant_refused =
  try ignore(prefix_count_rule prefix_mutant_mc PREFIX_MUTANT_EXEC); false
  with Failure _ -> true;;
if not semantic_mutant_refused then failwith "wrong count semantics admitted";;
print_endline "MIXED_SEMANTIC_MUTANT_REFUSED";;
check_axioms ();;
print_endline "MIXED_AXIOMS_CHECKED";;
'''
    text+='\n'.join(f'Printf.printf "MIXED_THEOREM {name} %s\\n" (string_of_thm {name});;' for name in names)+'\n'
    out=work/'proof.ml';out.write_text(text);return out,names


def prove(work,s2n,hol,log):
    started=time.monotonic()
    source,names=driver(work)
    env={**os.environ,'HOLLIGHT_DIR':str(hol),'HOLLIGHT_USE_MODULE':'1'}
    def run(args,target):
        with target.open('ab') as stream:
            result=subprocess.run(list(map(str,args)),cwd=s2n,env=env,
                                  stdout=stream,stderr=subprocess.STDOUT)
        if result.returncode:raise RuntimeError(f'proof command failed: {args}; see {target}')
    inline=work/'proof.inlined.ml';compiled=work/'proof.inlined.cmx';binary=work/'proof.native'
    run([hol/'hol.sh','inline-load',source,inline],log)
    run([hol/'hol.sh','compile',inline,'-o',compiled,'-w','-a'],log)
    run([hol/'hol.sh','link',compiled,'-o',binary],log)
    proof_log=work/'proof.log';run([binary],proof_log)
    result=proof_log.read_text()
    for marker in ['MIXED_SEMANTIC_MUTANT_REFUSED','MIXED_AXIOMS_CHECKED',
                   *['MIXED_THEOREM '+name+' ' for name in names]]:
        if marker not in result:raise RuntimeError(f'missing acceptance marker {marker}')
    def revision(path):
        return subprocess.check_output(['git','-C',path,'rev-parse','HEAD'],text=True).strip()
    return dict(theorems=names,axiom_audit=True,semantic_mutant_refused=True,
                fresh_process=True,seconds=time.monotonic()-started,
                hol_revision=revision(hol),s2n_revision=revision(s2n),
                hol_library_sha256=hashlib.sha256((hol/'hol_lib.cmxa').read_bytes()).hexdigest(),
                log_sha256=hashlib.sha256(proof_log.read_bytes()).hexdigest())
