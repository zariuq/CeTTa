#!/usr/bin/env python3
"""Embedding profile, batch, cache and retrieval controls; no live memory writes."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[2]


def text(value):
    return json.dumps(value,ensure_ascii=False)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta',type=Path,required=True)
    args=parser.parse_args()
    profile='(EmbeddingProfile "fixture" "model" "fixture-v1" 3 "text-v1" "http://fixture.invalid")'
    request=f'(embedding:request {profile} "document" ("alpha" "beta") ())'
    query_request=f'(embedding:request {profile} "query" ("question") ())'
    wire={'embeddings':[[1,0,0],[0,1,0]],'dim':3,'protocol':'fixture-v1'}
    encoded=text(json.dumps(wire,separators=(',',':')))
    batch=f'(embedding:decode {request} {encoded})'
    query=f'(embedding:from-tensor {profile} "query" ("question") (tensor:from-values f64 (1 3) (1.0 0.0 0.0)))'
    cases=[
        (f'(embedding-test:view {batch})','(BatchValue "document" (TensorData f64 (2 3) ((0 1.0) (1 0.0) (2 0.0) (3 0.0) (4 1.0) (5 0.0))))'),
        (f'(embedding:request {profile} "invalid" ("alpha") ())','(Embedding:Fault invalid-request)'),
        (f'(embedding:request {profile} "document" ("alpha" 1) ())','(Embedding:Fault invalid-request)'),
        (f'(embedding:request {profile} "document" not-a-sequence ())','(Embedding:Fault invalid-request)'),
        ('(embedding:request (EmbeddingProfile "fixture" "model" "fixture-v1" 3.0 "text-v1" "http://fixture.invalid") "query" ("q") ())','(Embedding:Fault invalid-request)'),
        ('(embedding:request (EmbeddingProfile "fixture" "model" "fixture-v1" -1 "text-v1" "http://fixture.invalid") "query" ("q") ())','(Embedding:Fault invalid-request)'),
        (f'''(let* (($queries (tensor:from-values f64 (2 3) (1.0 0.0 0.0 0.0 1.0 0.0)))
              ($first (embedding:from-tensor {profile} "query" ("q1") (tensor:view $queries (1 3) (3 1) 0)))
              ($second (embedding:from-tensor {profile} "query" ("q2") (tensor:view $queries (1 3) (3 1) 3))))
           (Queries (embedding-test:rank (embedding:rank-dot {batch} $first all 1))
                    (embedding-test:rank (embedding:rank-dot {batch} $second all 1))))''',
         '(Queries ((Embedding:Match 0 "alpha" 1.0)) ((Embedding:Match 1 "beta" 1.0)))'),
        (f'''(let* (($queries (tensor:from-values f64 (2 3) (1.0 0.0 0.0 0.0 1.0 0.0)))
              ($first (embedding:from-tensor {profile} "query" ("q1") (tensor:view $queries (1 3) (3 1) 0)))
              ($second (embedding:from-tensor {profile} "query" ("q2") (tensor:view $queries (1 3) (3 1) 3)))
              ($owner (embedding:index {batch}))
              ($a (embedding-test:rank (embedding:rank-cosine $owner $first all 1)))
              ($b (embedding-test:rank (embedding:rank-cosine $owner $second all 1)))
              ($closed (embedding:index-close $owner)))
           (Queries $a $b))''',
         '(Queries ((Embedding:Match 0 "alpha" 1.0)) ((Embedding:Match 1 "beta" 1.0)))'),
        (f'(== (embedding:cache-key {profile} "document" ("a")) (embedding:cache-key {profile} "query" ("a")))','False'),
        (f'(== (embedding:cache-key {profile} "document" ("a" "b")) (embedding:cache-key {profile} "document" ("b" "a")))','False'),
        (f'(embedding:accept-response {request} (http:response 503 "unavailable"))','(Embedding:Fault http-status 503)'),
        (f'(embedding-test:rank (embedding:rank-dot {batch} {query} all 2))','((Embedding:Match 0 "alpha" 1.0) (Embedding:Match 1 "beta" 0.0))'),
        (f'''(let* (($batch {batch}) ($index (embedding:index $batch))
                ($rank (embedding:rank-cosine $index {query} all 2))
                ($rows (embedding-test:rank $rank)) ($closed (embedding:index-close $index))) $rows)''',
         '((Embedding:Match 0 "alpha" 1.0) (Embedding:Match 1 "beta" 0.0))'),
        (f'''(let* (($request {request}) ($batch {batch}) ($cache (embedding:cache-new))
                ($stored (embedding:cache-put $cache $batch))
                ($stored-again (embedding:cache-put $cache $batch))
                ($got (embedding:cache-get $cache $request))
                ($equal (== $got $batch)) ($miss (embedding:cache-get $cache {query_request})))
              (Cache $equal $miss))''','(Cache True Embedding:Missing)'),
        (f'(embedding:rank-dot {batch} (embedding:from-tensor {profile} "query" ("q1" "q2") (tensor:from-values f64 (2 3) (1.0 0.0 0.0 0.0 1.0 0.0))) all 2)','(Embedding:Fault query-shape)'),
        (f'(embedding:from-tensor {profile} "document" ("alpha" "beta") (tensor:from-values f64 (1 3) (1.0 0.0 0.0)))','(Embedding:Fault invalid-vectors)'),
        (f'(embedding:from-tensor {profile} "document" ("alpha" "beta") (tensor:from-coo f64 (2 3) ((0 1.0))))','(Embedding:Fault incomplete-vectors shape-mismatch)'),
    ]
    for modification,expected in [
        ({'protocol':'other'},'(Embedding:Fault incompatible-profile)'),
        ({'dim':4},'(Embedding:Fault incompatible-profile)'),
        ({'indices':[1,0]},'(Embedding:Fault response-indices)'),
        ({'indices':[0,0]},'(Embedding:Fault response-indices)'),
        ({'indices':[0,1,2]},'(Embedding:Fault response-indices)'),
        ({'indices':[0.0,1.0]},'(Embedding:Fault response-indices)'),
        ({'embeddings':[[1,2,3]]},'(Embedding:Fault numeric-response typed-json)'),
        ({'embeddings':[[1,2,3],[4,None,6]]},'(Embedding:Fault numeric-response typed-json)'),
        ({'embeddings':[[1,2,3],[4,True,6]]},'(Embedding:Fault numeric-response typed-json)'),
    ]:
        payload=dict(wire,**modification)
        cases.append((f'(embedding:decode {request} {text(json.dumps(payload))})',expected))
    payload=dict(wire,indices=[0,1])
    cases.append((f'(== (embedding:decode {request} {text(json.dumps(payload))}) {batch})','True'))
    retrieval_vectors='(tensor:from-values f64 (4 3) (1.3 -0.2 0.7 0.1 2.1 -0.9 1.0 1.0 1.0 -0.3 0.5 2.7))'
    documents=f'(embedding:from-tensor {profile} "document" ("a" "b" "c" "future") {retrieval_vectors})'
    for direction in ('(1.0 0.0 0.0)', '(-0.1 0.7 1.1)'):
        question=f'(embedding:from-tensor {profile} "query" ("q") (tensor:from-values f64 (1 3) {direction}))'
        for mask in ('all', '(tensor:from-values bool (4) (False True True False))',
                     '(tensor:from-values bool (4) (False False False False))',
                     '(tensor:from-coo bool (4) ((0 True) (1 False) (2 True)))'):
            for k in (0,1,3,99):
                cases.append((f'''(let* (($documents {documents}) ($question {question}) ($mask {mask})
                    ($ordinary (embedding:index $documents))
                    ($selected (embedding:retrieval-index $documents))
                    ($ranking (embedding:rank-cosine $ordinary $question $mask {k}))
                    ($retrieved (embedding:retrieve-cosine $selected $question $mask {k}))
                    ($expected (embedding:matches $ranking ("a" "b" "c" "future")))
                    ($actual (embedding:matches $retrieved ("a" "b" "c" "future")))
                    ($closed (embedding:index-close $ordinary))
                    ($closed-selected (embedding:index-close $selected)))
                  (and (== $actual $expected) (embedding-test:selected-shape $retrieved)))''', 'True'))
    selected_index=f'(embedding:retrieval-index {batch})'
    cases += [
        (f'''(let* (($matrix {retrieval_vectors}) ($query (tensor:from-values f64 (3) (0.3 -0.9 0.7)))
             ($plain (tensor:cosine-index $matrix)) ($prepared (tensor:cosine-index $matrix screened))
             ($mask (tensor:from-values bool (4) (False True True False)))
             ($a (tensor:cosine-retrieve $plain $query $mask 99))
             ($b (tensor:cosine-retrieve $prepared $query $mask 99))
             ($same (== $a $b)) ($bound (== (tensor:index-source $prepared) $matrix))
             ($closed (tensor:close $plain)) ($closed-prepared (tensor:close $prepared)))
           (and $same $bound))''', 'True'),
        ('''(let* (($matrix (tensor:from-values f64 (2 2) (1.0 0.0 0.0 1.0)))
             ($other (tensor:from-values f64 (2 2) (0.0 1.0 1.0 0.0)))
             ($query (tensor:from-values f64 (2) (1.0 0.0)))
             ($a (tensor:cosine-index $matrix screened)) ($b (tensor:cosine-index $other screened))
             ((TensorRanking Exact $ids-a $scores-a) (tensor:cosine-retrieve $a $query 1))
             ((TensorRanking Exact $ids-b $scores-b) (tensor:cosine-retrieve $b $query all 1))
             ($answer (and (== (tensor:at $ids-a 0) (tensor:present 0))
                           (== (tensor:at $ids-b 0) (tensor:present 1))))
             ($closed-a (tensor:close $a)) ($closed-b (tensor:close $b))) $answer)''', 'True'),
        ('''(let* (($matrix (tensor:from-coo f64 (2 2) ((0 1.0) (3 1.0))))
             ($index (tensor:cosine-index $matrix screened))
             ($query (tensor:from-values f64 (2) (1.0 0.0)))
             ((TensorRanking Exact $ids $scores) (tensor:cosine-retrieve $index $query 1))
             ($closed (tensor:close $index)))
           (and (== (tensor:at $ids 0) (tensor:present 0))
                (== (tensor:at $scores 0) (tensor:present 1.0))))''', 'True'),
        (f'(embedding-test:rank (embedding:retrieve-cosine {selected_index} {query} all 2))',
         '((Embedding:Match 0 "alpha" 1.0) (Embedding:Match 1 "beta" 0.0))'),
        (f'(embedding:matches (embedding:retrieve-cosine {selected_index} {query} all 1) ("only-one"))',
         '(Embedding:Fault row-identities)'),
        (f'(embedding:retrieve-cosine {selected_index} (Embedding:Batch Other "query" "key" (tensor:from-values f64 (1 3) (1.0 0.0 0.0))) all 1)',
         '(Embedding:Fault incompatible-profile)'),
        (f'(embedding:retrieve-cosine {selected_index} (Embedding:Batch {profile} "query" "key" (tensor:from-values f64 (2 3) (1.0 0.0 0.0 0.0 1.0 0.0))) all 1)',
         '(Embedding:Fault query-shape)'),
        (f'(embedding:retrieve-cosine {selected_index} (Embedding:Batch {profile} "query" "key" (tensor:from-values f64 (1 3) (0.0 0.0 0.0))) all 1)',
         '(Embedding:Fault retrieval zero-norm)'),
        (f'''(let* (($index {selected_index}) ($alias $index) ($closed (embedding:index-close $alias)))
          (embedding:retrieve-cosine $index {query} all 1))''',
         '(Embedding:Fault retrieval closed-or-forged-index)'),
        (f'(embedding:retrieve-cosine {selected_index} {query} (tensor:from-values bool (1) (True)) 1)',
         '(Embedding:Fault retrieval shape-mismatch)'),
        ('(tensor:cosine-retrieve ExtensionQuery ExtensionVector 1)', 'ExtendedRetrieval'),
        ('(tensor:cosine-retrieve UnknownOwner UnknownVector all 1)',
         '(tensor:cosine-retrieve UnknownOwner UnknownVector all 1)'),
    ]
    helpers='''
(= (embedding-test:view (Embedding:Batch $profile $kind $key $vectors))
   (let $view (tensor:inspect $vectors) (BatchValue $kind $view)))
(= (embedding-test:rank $ranking) (embedding:matches $ranking ("alpha" "beta")))
(= (embedding-test:selected-shape (Embedding:Retrieved $profile $key $rows $indices $scores))
   (and (== $rows 4) (== (tensor:shape $indices) (tensor:shape $scores))))
(= (tensor:cosine-retrieve ExtensionQuery ExtensionVector 1) ExtendedRetrieval)
'''
    checks=0
    for language,profile_mode in [('petta@1.0.6',None),('petta@1.0.6','extended'),('he','extended'),('prime',None)]:
        library='(library embedding)' if language.startswith('petta') else 'embedding'
        source=f'!(import! &self {library})\n'+helpers
        source+=''.join(f'!(let $actual{i} {body} (== $actual{i} {expected}))\n' for i,(body,expected) in enumerate(cases))
        if language.startswith('petta'):
            source=source.replace('True','true').replace('False','false')
        command=[str(args.cetta.resolve()),'--lang',language]
        if profile_mode:
            command+=['--profile',profile_mode]
        result=subprocess.run(command+['-e',source],cwd=ROOT,text=True,capture_output=True,timeout=180)
        lines=result.stdout.splitlines()[-len(cases):]
        truth='true' if language.startswith('petta') else '[True]'
        bad=[(i,line,cases[i]) for i,line in enumerate(lines) if line!=truth]
        if result.returncode or lines!=[truth]*len(cases):
            isolated=[]
            for i,(body,expected) in enumerate(cases):
                one=f'!(import! &self {library})\n'+helpers+f'!(let $actual {body} (== $actual {expected}))\n'
                if language.startswith('petta'):
                    one=one.replace('True','true').replace('False','false')
                probe=subprocess.run(command+['-e',one],cwd=ROOT,text=True,capture_output=True,timeout=180)
                if probe.returncode or probe.stdout.splitlines()[-1:]!=[truth]:
                    isolated.append((i,body,probe.stdout,probe.stderr))
            raise AssertionError((language,profile_mode,bad,isolated,result.stdout[-3000:],result.stderr[-2000:]))
        checks+=len(cases)
    print(f'EmbeddingWorkflows {checks} 0')


if __name__=='__main__':
    main()
