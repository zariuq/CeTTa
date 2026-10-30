#!/usr/bin/env python3
"""Bounded native source discovery with preparation/search costs separated."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

from check_binary_codec import ROOT, parse_forms
from check_source_codec import text_arg
from check_ic_search import check_controls


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta',type=Path,required=True)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--depth',type=int,default=2)
    parser.add_argument('--budget',type=int,default=96)
    parser.add_argument('--quantum',type=int,default=2048)
    args=parser.parse_args()
    if min(args.depth,args.budget)<0 or args.quantum<1:parser.error('invalid discovery bounds')
    args.output.mkdir(parents=True,exist_ok=True)
    text=args.source.read_text()
    source=f'''!(import! &self (library lib_ic_context_grammar))
!(import! &self (library system))
(= (bench-source:result $document $start $ready $state $result)
   (case $result
     (((IC:SearchResult $best $statistics $reason $first $next)
       (let* (($end (system:monotonic-ns)) ($closed (ic:release $next)))
         (println! (SourceDiscovery $reason $statistics $first
           (tensor:shape (ic:source-pack $document)) (tensor:shape $best)
           (- $ready $start) (- $end $ready) (tensor:inspect $best)))))
      ($failure (let $closed (ic:release $state) (println! (SourceDiscoveryFault $failure)))))))
(= (bench-source:run $text)
 (let* (($start (system:monotonic-ns)) ($document (syntax:read $text))
        ($prepared (ic:learn-library Deferred $document "bounded" (IC:Grammar {args.depth}) {args.quantum}))
        ($ready (system:monotonic-ns)))
   (case $prepared
     (((IC:PreparedSearch $state)
       (bench-source:result $document $start $ready $state (ic:resume $state {args.budget})))
      ($bad-preparation (println! (SourceDiscoveryFault $bad-preparation)))))))
!(bench-source:run {text_arg(text)})
'''
    path=args.output/'discovery.metta';path.write_text(source);check_controls(source)
    started=time.perf_counter()
    log=args.output/'discovery.log'
    with log.open('w') as output:
        process=subprocess.Popen([str(args.cetta.resolve()),'--lang','petta',str(path.resolve())],cwd=ROOT,
                                 stdout=output,stderr=subprocess.STDOUT)
        if hasattr(os,'wait4'):
            _pid,status,usage=os.wait4(process.pid,0)
            process.returncode=os.waitstatus_to_exitcode(status)
            peak_rss=usage.ru_maxrss
        else:
            process.wait();peak_rss=None
    wall=time.perf_counter()-started
    output=log.read_text()
    forms=[row for row in parse_forms(output) if isinstance(row,list) and row and row[0] in ('SourceDiscovery','SourceDiscoveryFault')]
    if process.returncode or len(forms)!=1 or forms[0][0]!='SourceDiscovery':raise RuntimeError((process.returncode,forms,output[-1000:]))
    row=forms[0];data=bytes(int(value[1]) for value in row[8][3]);(args.output/'best.sic').write_bytes(data)
    result={'source':args.source.name,'source_sha256':hashlib.sha256(text.encode()).hexdigest(),
            'binary_sha256':hashlib.sha256(args.cetta.read_bytes()).hexdigest(),
            'depth':args.depth,'scheduling_budget':args.budget,'quantum':args.quantum,
            'reason':row[1],'statistics':row[2],'first_fault':row[3],
            'original_bytes':int(row[4][0]),'best_bytes':int(row[5][0]),
            'preparation_seconds':int(row[6])/1e9,'search_seconds':int(row[7])/1e9,'whole_process_seconds':wall,
            'child_peak_rss_kib':peak_rss,
            'qualified_utility_claim':False,'scope':'bounded source discovery; current best checked against original, not an exhaustion or held-out reuse claim'}
    (args.output/'receipt.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))


if __name__=='__main__':main()
