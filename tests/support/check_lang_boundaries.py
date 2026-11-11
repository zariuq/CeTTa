#!/usr/bin/env python3
"""Parser alternatives, package composition, editor spans and REPL boundaries."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from library_spelling import read_forms, show, Symbol as S
from check_lang_library import raw, quote


def field(record, key):
    return next(row[2] for row in record[1:] if row[:2] == [S('Field'), quote(key)])


def record(**fields):
    return [S('Record'), *[[S('Field'), quote(key), value] for key, value in fields.items()]]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary',type=Path);parser.add_argument('work',type=Path)
    args=parser.parse_args();binary=args.binary.resolve(strict=True)
    engine=Path(__file__).resolve().parents[2];work=args.work.resolve();work.mkdir(parents=True,exist_ok=False)
    common='''!(import! &self lang)
(= (boundary:language (LangDefinitionV1 $bound)) Loaded)
(= (boundary:language (LangDefinitionUnadmittedV1 $m $why)) Unadmitted)
(= (boundary:read (LangSourceReadV1 $s $why)) Unadmitted)
(= (boundary:read (LangRunUnadmittedV1 $l $s)) Unadmitted)
(= (boundary:parse (SourceParseV1 $code $accepted (SourceValuesV1 $a $b) $forest))
 (case ($a $b)
   ((((result A nil) (result B nil)) ($code A B))
    (((result B nil) (result A nil)) ($code A B))
    (((result A nil) (result A nil)) ($code A A))
    ($other (Unexpected $other)))))
(= (boundary:error (Error $call $reason)) ErrorSeen)
'''
    cases={}
    productions='\n'.join('(pp-production (pp-label (pp-def "S") "'+name+'") (pp-def "S") (pp-items-cons (pp-terminal (pp-terminal-char (cp 120))) pp-items-nil) (pa-const '+value+'))' for name,value in [('left','A'),('right','B')])+'\n'
    load='(langdef:load-source-plan '+raw(productions)+' "" "(pp-def \\"S\\")" "(source-selection-v1 LNil)")'
    # JSON quoting avoids relying on either host's string escape spelling.
    load='(langdef:load-source-plan '+raw(productions)+' "" '+quote('(pp-def "S")')+' '+quote('(source-selection-v1 LNil)')+')'
    cases['gll-glr-alternatives']=f'''!(let $p {load}
 (test (boundary:parse (langdef:parse-source-plan $p "x" 10000 100 SourceGLLV1)) (0 A B)))
!(let $p {load}
 (test (boundary:parse (langdef:parse-source-plan $p "x" 10000 100 SourceGLRV1)) (0 A B)))
!(let $p {load}
 (test (case (langdef:parse-source-plan $p "y" 10000 100)
   (((SourceParseV1 0 $accepted (SourceValuesV1) $forest) Rejected))) Rejected))
!(let $p {load}
 (test (case (langdef:parse-source-plan $p "x" 10000 1)
   (((SourceParseV1 4 $accepted $values $forest) Pending))) Pending))
!(let $p {load}
 (test (case (langdef:parse-source-plan $p "x" 1 100)
   (((SourceParseV1 2 $accepted $values $forest) Pending))) Pending))
'''
    equal_productions=productions.replace('(pa-const B)','(pa-const A)')
    cases['equal-valued-alternatives']=f'''!(let $p (langdef:load-source-plan {raw(equal_productions)} "" {quote('(pp-def "S")')} {quote('(source-selection-v1 LNil)')})
 (test (boundary:parse (langdef:parse-source-plan $p "x" 10000 100)) (0 A A)))
'''
    cases['resource-ownership']=f'''!(let $p {load}
 (let $r (langdef:parse-source-plan $p "x" 10000 100)
  (case $r (((SourceParseV1 $code $accepted $values $forest)
    (let* (($closed (langdef:close-source-plan $p)))
      (test (boundary:error (langdef:source-plan-forest $forest 10000)) ErrorSeen)))))))
!(test (boundary:error (langdef:parse-source-plan (NativeHandle "cetta.source-parser-plan.v1" 1) "x" 10000 100)) ErrorSeen)
!(test (boundary:error (langdef:source-plan-evidence (NativeHandle "cetta.source-parser-forest.v1" 1))) ErrorSeen)
!(test (boundary:error (langdef:source-compiler-call (NativeHandle "cetta.source-compiler.v1" 1) "read" LNil)) ErrorSeen)
'''
    package=read_forms((engine/'langdef/lang/Datalog.module').read_text())[0]
    compiler=next(row for row in field(package[1],'exports')[1:] if field(row,'name')==quote('compiler'))
    resource=next(row for row in field(compiler,'spec')[1:] if row[1]==quote('resource'))
    resource[2]=quote(str(engine/'runtime/lang/datalog-compiler.so'))
    dependency=work/'dependency.module';dependency.write_text(show(package))
    digest=hashlib.sha256(dependency.read_bytes()).digest()
    names=['syntax','core','parserPlan','compiler','execution','regions']
    def wrapper(path,commitment=digest,uri='dependency.module',preferred=None):
        exports=[]
        for name in names:
            spec=record(dependency=quote(uri),export=quote(name))
            if name=='regions' and preferred is not None:
                spec=record(preferredRawFenceDepth=[S('Integer'),S(str(preferred))])
            exports.append(record(name=quote(name),spec=spec))
        declaration=record(mettail=quote('module/1'),name=quote('Wrapped'),
            dependencies=[S('List'),record(uri=quote(uri),commitment=[S('Bytes'),*[S(str(b)) for b in commitment]])],
            exports=[S('List'),*exports])
        path.write_text(show([S('module:document'),declaration]));return path
    linked=wrapper(work/'linked.module',preferred=3)
    bad=wrapper(work/'bad-commitment.module',bytes(32))
    missing=wrapper(work/'missing.module',digest,'missing.module')
    reach=(engine/'tests/lang/reachability.dl').read_text()
    cases['declared-imports']=f'''(Edge "a" "b")
!(let $dl (lang:load {quote(str(linked))})
 (test (case (lang:run $dl &self {raw(reach)})
   (((LangCompleteV1 $outputs) $outputs))) ((Reach "a" "b"))))
!(test (boundary:language (lang:load {quote(str(bad))})) Unadmitted)
!(test (boundary:language (lang:load {quote(str(missing))})) Unadmitted)
!(test (lang:region-text (lang:load {quote(str(linked))}) "body") {quote('r###"body"###')})
!(test (lang:region-text (lang:load {quote(str(linked))}) {quote('quote "### inside')}) {quote('r####"quote "### inside"####')})
'''
    # A valid envelope cannot attach this compiler to a different meaning.
    for changed in ('core', 'syntax', 'selection', 'operations'):
        altered=read_forms(dependency.read_text())[0]
        entries=field(altered[1],'exports')[1:]
        if changed in ('core','syntax'):
            entry=next(e for e in entries if field(e,'name')==quote(changed))
            next(row for row in entry[1:] if row[1]==quote('spec'))[2]=quote('different declared meaning')
        elif changed=='selection':
            entry=next(e for e in entries if field(e,'name')==quote('parserPlan'))
            next(row for row in field(entry,'spec')[1:] if row[1]==quote('selection'))[2]=quote('(source-selection-v1 LNil)')
        else:
            entry=next(e for e in entries if field(e,'name')==quote('compiler'))
            next(row for row in field(entry,'spec')[1:] if row[1]==quote('observe'))[2]=quote('prepare')
        wrong=work/('wrong-'+changed+'.module');wrong.write_text(show(altered))
        cases['binding-rejects-'+changed]=f'''!(test (boundary:language (lang:load {quote(str(wrong))})) Unadmitted)
!(test (boundary:read (lang:run (lang:load {quote(str(wrong))}) &self {raw(reach)})) Unadmitted)
!(test (collapse (match &self (Reach $x $y) ($x $y))) ())
'''
    cases['language-owner']=f'''!(test (boundary:read (lang:run (LangDefinitionV1 (NativeHandle "cetta.language.v1" 3)) &self {raw(reach)})) Unadmitted)
'''
    syntax_only=record(mettail=quote('module/1'),name=quote('SyntaxOnly'),dependencies=[S('List')],
        exports=[S('List'),record(name=quote('syntax'),spec=quote('no execution realization'))])
    syntax_path=work/'syntax-only.module';syntax_path.write_text(show([S('module:document'),syntax_only]))
    cases['syntax-is-not-execution']=f'''!(test (boundary:language (lang:load {quote(str(syntax_path))})) Unadmitted)
!(test (boundary:read (lang:run (lang:load {quote(str(syntax_path))}) &self {raw(reach)})) Unadmitted)
!(test (collapse (match &self (Reach $x $y) ($x $y))) ())
'''
    host='; r#"comment\n(lang:run $dl &self r##"日本語\r\n \\" ) ;"##)'
    start=host.index('r##');body=start+4;end=host.index('"##',body);length=len(host.encode())
    offsets=[len(host[:index].encode()) for index in (start,body,end,end+3)]
    expected=f'(SourceRegionsV1 SourceLexicallyCompleteV1 {length} (RawSourceRegionsV1 (RawSourceRegionV1 '+ ' '.join(map(str,offsets))+' 2)))'
    incomplete='(lang:run $dynamic &self r#"unfinished\n'
    start2=incomplete.index('r#');n=len(incomplete.encode())
    cases['region-spans']=f'''!(test (lang:regions {quote(host)}) {expected})
!(test (lang:regions {quote(incomplete)}) (SourceRegionsV1 SourceLexicallyIncompleteV1 {n} (RawSourceRegionsV1 (RawSourceRegionIncompleteV1 {start2} {start2+3} {n} {n} 1))))
'''
    # Invalid protocol data stays data and cannot become output facts.
    cases['protocol-rejection']='''!(test (case (lang-internal:reply "{\\"ok\\":7}")
 (((LangProtocolErrorV1 $site $value) Rejected))) Rejected)
!(test (case (lang-internal:outputs (json:parse "[{\\"relation\\":\\"P\\",\\"values\\":[{\\"kind\\":\\"unsigned\\",\\"value\\":\\"-1\\"}]}]"))
 (((LangProtocolErrorV1 $site $value) Rejected))) Rejected)
!(test (boundary:error (module:install-definitions (new-space) "!(add-atom &self (Unexpected))")) ErrorSeen)
!(test (collapse (match &self (Unexpected) yes)) ())
'''
    passed=[]
    for name,body in cases.items():
        path=work/(name+'.metta');path.write_text(common+body+f'!(println! (BoundaryCasePassed {name}))\n')
        result=subprocess.run([binary,'--lang','petta','--profile','extended','--run-contract',path],capture_output=True,timeout=120)
        (work/(name+'.out')).write_bytes(result.stdout);(work/(name+'.err')).write_bytes(result.stderr)
        assert result.stdout.count('✅'.encode())==body.count('(test ') and result.returncode==0 and f'(BoundaryCasePassed {name})'.encode() in result.stdout,(name,result.returncode,result.stdout[-1800:],result.stderr[-1000:])
        passed.append(name)
    repl='''(= (repl:value) 37)
!(repl:value)
!(+ 1
 2)
!(broken]
:cancel
!(+ 5 6)
:quit
'''
    result=subprocess.run([binary,'--repl'],input=repl.encode(),capture_output=True,timeout=30)
    (work/'repl.out').write_bytes(result.stdout);(work/'repl.err').write_bytes(result.stderr)
    assert result.returncode==0 and not result.stderr and b'37\n3\n' in result.stdout and b'11\n' in result.stdout and b'SourceInputInvalidV1' in result.stdout,result
    incomplete_result=subprocess.run([binary,'--repl'],input=b'!(lang:run (lang:load Datalog) &self r#"unfinished\n:cancel\n',capture_output=True,timeout=30)
    assert incomplete_result.returncode==0 and b'SourceInputIncompleteV1' in incomplete_result.stdout,incomplete_result
    profiles=[]
    for dialect,profile in [('petta','default'),('he','extended')]:
        path=work/(dialect+'-'+profile+'.metta')
        path.write_text('!(import! &self lang)\n(Edge "a" "b")\n'
            +f'!(lang:run (lang:load Datalog) &self {quote(reach)})\n')
        command=[binary,'--lang',dialect]
        if profile!='default':command+=['--profile',profile]
        result=subprocess.run([*command,'--run-contract',path],capture_output=True,timeout=120)
        (work/(dialect+'-'+profile+'.out')).write_bytes(result.stdout)
        (work/(dialect+'-'+profile+'.err')).write_bytes(result.stderr)
        assert result.returncode==0 and b'LangRunUnadmittedV1' in result.stdout and b'LangCompleteV1' not in result.stdout,(dialect,profile,result)
        profiles.append([dialect,profile])
    (work/'summary.json').write_text(json.dumps({'cases':passed,'repl':True,'incomplete_eof':True,
        'unadmitted_host_profiles':profiles},indent=2)+'\n')
    print(f'(LangBoundariesV1Summary {len(passed)} passed; REPL passed; 2 host-profile refusals)')

if __name__=='__main__':main()
