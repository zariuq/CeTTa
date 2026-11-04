#!/usr/bin/env python3
"""Compare public lang execution with a selected native Souffle reference."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from library_spelling import read_forms,Symbol
from check_lang_library import raw


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('binary',type=Path);p.add_argument('souffle',type=Path);p.add_argument('corpus',type=Path);p.add_argument('work',type=Path)
    p.add_argument('--timeout',type=float,default=300)
    p.add_argument('--work-per-call',type=int,default=10,dest='allowance',help='work units per resumable call')
    p.add_argument('--case',action='append',dest='selected')
    p.add_argument('--standard-corpus',type=Path,help='additional D0 standard .dl programs')
    a=p.parse_args();binary=a.binary.resolve(strict=True);souffle=a.souffle.resolve(strict=True)
    if a.allowance<=0:p.error('--work-per-call must be positive')
    work=a.work.resolve();work.mkdir(parents=True,exist_ok=False)
    cases=[]
    for name in ('cyclic_graph','points_to'):
        source=(a.corpus/(name+'.dl')).read_text()
        inputs={f.stem:list(csv.reader(f.read_text().splitlines(),delimiter='\t')) for f in sorted((a.corpus/name).glob('*.facts'))}
        cases.append((name,source,inputs));cases.append((name+'-empty',source,{k:[] for k in inputs}))
    standard=a.standard_corpus or a.corpus.resolve().parents[3]/'tests/datalog'
    for name in ('basic','nonlinear_closure','same_generation','boundaries'):
        cases.append((name,(standard/(name+'.dl')).read_text(),{}))
    for operator,label in [('=','equal'),('!=','different'),('<','less'),('<=','less-equal'),('>','greater'),('>=','greater-equal')]:
        source=f'.decl Pair(x:number, y:number)\n.input Pair\n.decl Result(x:number, y:number)\n.output Result\nResult(X,Y) :- Pair(X,Y), X {operator} Y.\n'
        cases.append(('compare-'+label,source,{'Pair':[[str(x),str(y)] for x,y in [(-3,2),(2,-3),(2,2),(0,0)]]}))
    cases.append(('integer-spellings','.decl Scalar(x:number)\n.output Scalar\nScalar(0x2a).\nScalar(0b101).\nScalar(00012).\nScalar(-0).\n',{}))
    words=['z','é','λ','🙂','Z','a','aa','','10','2']
    symbols='.decl Word(x:symbol)\n.decl Compared(x:symbol,y:symbol,op:symbol)\n.output Compared\n'
    symbols+=''.join('Word('+json.dumps(word,ensure_ascii=False)+').\n' for word in words)
    symbols+=''.join(f'Compared(X,Y,"{name}") :- Word(X),Word(Y),X {operator} Y.\n'
        for name,operator in [('lt','<'),('le','<='),('gt','>'),('ge','>=')])
    cases.append(('symbol-ordering',symbols,{}))
    integers='.decl Scalar(x:number)\n.output Scalar\n'+''.join('Scalar('+value+').\n' for value in
        ['00012','-00012','-0','0b1010','-0b1010','0xAf','-0xAf','-9223372036854775808','9223372036854775807'])
    cases.append(('integer-widths-and-signs',integers,{}))
    rows=[]
    for name,source,inputs in cases:
        if a.selected and name not in a.selected:continue
        folder=work/name;folder.mkdir();facts=folder/'facts';facts.mkdir();out=folder/'souffle';out.mkdir()
        (folder/'program.dl').write_text(source)
        for relation,values in inputs.items():
            (facts/(relation+'.facts')).write_text(''.join('\t'.join(row)+'\n' for row in values))
        result=subprocess.run([souffle,'-j','1','-F',facts,'-D',out,folder/'program.dl'],capture_output=True,timeout=a.timeout)
        (folder/'souffle.out').write_bytes(result.stdout);(folder/'souffle.err').write_bytes(result.stderr)
        assert result.returncode==0,(name,result.returncode,result.stderr)
        reference_diagnostics=result.stderr.decode()
        declarations={n:[col.split(':')[-1].strip() for col in args.split(',') if col.strip()] for n,args in re.findall(r'\.decl\s+([\w.]+)\s*\(([^)]*)\)',source)}
        host=[]
        for relation,values in inputs.items():
            for value_row in values:
                values_text=[json.dumps(v) if t=='symbol' else v for v,t in zip(value_row,declarations[relation])]
                host.append('('+relation.replace('.',':')+' '+ ' '.join(values_text)+')')
        output_counts=[]
        for relation in re.findall(r'\.output\s+([\w.]+)',source):
            fields=' '.join('$field'+str(i) for i in range(len(declarations[relation])))
            output_counts.append('(size-atom (collapse (match &self ('+relation.replace('.',':')+' '+fields+') present)))')
        driver='''!(import! &self lang)
(= (reference:output-counts) (COUNTS))
(= (reference:finish $result $left)
 (case $result
   (((LangCompleteV1 $facts) (println! (ReferenceRowsV1 $facts)))
    ((LangPendingV1 $cursor) (if (> $left 0) (reference:finish (lang:resume $result WORK_ALLOWANCE) (- $left 1)) (println! ReferencePendingV1)))
    ($other (println! (ReferenceFailureV1 $other))))))
'''.replace('WORK_ALLOWANCE',str(a.allowance)).replace('COUNTS',' '.join(output_counts))+ '\n'.join(host)+'\n!(let* (($before (reference:output-counts)) ($_ (reference:finish (lang:eval (lang:load Datalog) &self '+str(a.allowance)+' '+raw(source)+') 10000))) (test (reference:output-counts) $before))\n'
        (folder/'program.metta').write_text(driver)
        native=subprocess.run([binary,'--lang','petta','--profile','extended',folder/'program.metta'],capture_output=True,timeout=a.timeout)
        (folder/'cetta.out').write_bytes(native.stdout);(folder/'cetta.err').write_bytes(native.stderr)
        assert native.returncode==0 and not native.stderr,(name,native.returncode,native.stderr)
        assert native.stdout.count('✅'.encode())==1,(name,'isolated evaluation changed output Space',native.stdout[-1200:])
        transcripts=[line for line in native.stdout.decode().splitlines() if line.startswith('(ReferenceRowsV1 ')]
        assert len(transcripts)==1,(name,native.stdout[-1200:])
        decoded=read_forms(transcripts[0])[0][1];observed={}
        for fact in decoded:
            relation=str(fact[0]).replace(':','.')
            value_row=[]
            for token,kind in zip(fact[1:],declarations[relation]):
                if kind=='symbol':
                    assert not isinstance(token,Symbol),(name,token)
                    value_row.append(json.loads(token))
                else:
                    assert isinstance(token,Symbol) and re.fullmatch('-?[0-9]+',token),(name,token)
                    value_row.append(str(int(token)))
            assert len(fact)-1==len(declarations[relation])
            value_row=tuple(value_row)
            assert value_row not in observed.setdefault(relation,set()),(name,'duplicate',fact)
            observed[relation].add(value_row)
        expected={f.stem:set(tuple(row) for row in csv.reader(f.read_text().splitlines(),delimiter='\t')) for f in out.glob('*.csv')}
        assert all(observed.get(key,set())==values for key,values in expected.items()) and not (set(observed)-set(expected)),(name,observed,expected)
        rows.append({'case':name,'relations':{key:len(value) for key,value in expected.items()},'souffle_diagnostics':reference_diagnostics,'isolated_output_space_unchanged':True,'passed':True})
    record={'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'souffle_sha256':hashlib.sha256(souffle.read_bytes()).hexdigest(),'work_per_call':a.allowance,'cases':rows}
    (work/'summary.json').write_text(json.dumps(record,indent=2)+'\n')
    print(f'(LangSouffleV1Summary {len(rows)} passed)')

if __name__=='__main__':main()
