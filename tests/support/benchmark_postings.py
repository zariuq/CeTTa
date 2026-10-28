#!/usr/bin/env python3
"""Grouped occurrence-prefix queries versus Python bisect, including delivery.

The MeTTa measurement includes prepared-handle lookup and result allocation.
Validation compares every returned group before the repeated timed queries.
"""
import argparse
from bisect import bisect_left
import json
from pathlib import Path
import re
import statistics
import subprocess
import threading
import time

ROOT=Path(__file__).resolve().parents[2]


def expression(rows):
    return '('+' '.join('('+str(key)+' ('+' '.join(map(str,values))+'))' for key,values in rows)+')'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--full',action='store_true')
    parser.add_argument('--repeat',type=int,default=3)
    args=parser.parse_args()
    if args.repeat<1:
        parser.error('positive repeat count required')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    panels=[(8,8,16),(64,128,32),(512,256,64)]
    if args.full:
        panels += [(4096,256,64),(1024,2048,128)]
    observations=[]
    for group_count,per_group,query_count in panels:
        groups=[(key,[(i*3+key%7)//2 for i in range(per_group)]) for key in range(group_count)]
        bounds=[-1,per_group//2,per_group*2,per_group*4]
        expected=[[(key,bisect_left(values,bound)) for key,values in groups if bisect_left(values,bound)] for bound in bounds]
        expected_text='('+' '.join('('+' '.join(f'({key} {count})' for key,count in row)+')' for row in expected)+')'
        queries=[(i*47)%(per_group*2+1) for i in range(query_count)]
        query_text='('+' '.join(map(str,queries))+')'
        source='!(import! &self (library postings))\n!(import! &self system)\n'
        source+=f'''!(let* (($groups {expression(groups)})
             ($start (system:monotonic-ns)) ($index (postings:prepare $groups))
             ($prepared (system:monotonic-ns))
             ($controls (map-atom ({' '.join(map(str,bounds))}) $bound (postings:counts-before $index $bound)))
             ($equal (== $controls {expected_text}))
             ($begin (system:monotonic-ns))
             ($rows (foldl-atom {query_text} 0 $total $bound
                      (let $counts (postings:counts-before $index $bound) (+ $total (size-atom $counts)))))
             ($end (system:monotonic-ns))
             ($stats (postings:stats $index)) ($closed (postings:close $index)))
          (PostingsTiming $equal (- $prepared $start) (- $end $begin) $rows $stats))\n'''
        script=args.output.parent/f'postings-{group_count}-{per_group}-{query_count}.metta'
        script.write_text(source)
        prepared,work,whole,peaks=[],[],[],[]
        retained=None
        for _ in range(args.repeat):
            start=time.perf_counter_ns()
            child=subprocess.Popen([str(args.cetta.resolve()),'--lang','petta',str(script)],
                                   cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            peak=[0]
            finished=threading.Event()
            def monitor():
                while not finished.is_set():
                    try:
                        status=Path(f'/proc/{child.pid}/status').read_text()
                        resident=re.search(r'^VmHWM:\s+(\d+)',status,re.M)
                        if resident:
                            peak[0]=max(peak[0],int(resident[1]))
                    except (FileNotFoundError,ProcessLookupError):
                        break
                    finished.wait(.005)
            observer=threading.Thread(target=monitor,daemon=True)
            observer.start()
            stdout,stderr=child.communicate(timeout=300)
            finished.set(); observer.join(); peaks.append(peak[0])
            whole.append(time.perf_counter_ns()-start)
            assert child.returncode==0,(stdout[-1000:],stderr)
            match=re.search(r'\(PostingsTiming true (\d+) (\d+) (\d+) \(PostingsStats packed (\d+)\)\)',stdout)
            assert match,(stdout[-1000:],stderr)
            prep,timed,rows,retained=map(int,match.groups())
            assert rows==sum(sum(bool(bisect_left(values,b)) for _,values in groups) for b in queries)
            prepared.append(prep); work.append(timed)
        python=[]
        for _ in range(args.repeat):
            start=time.perf_counter_ns()
            for bound in queries:
                result=[]
                for key,values in groups:
                    count=bisect_left(values,bound)
                    if count:
                        result.append((key,count))
            python.append(time.perf_counter_ns()-start)
        observation={'groups':group_count,'positions_per_group':per_group,'queries':query_count,
                     'equal':True,'cetta_prepare_ns':statistics.median(prepared),
                     'cetta_work_ns':statistics.median(work),'python_work_ns':statistics.median(python),
                     'whole_ns':statistics.median(whole),'retained_bytes':retained,
                     'sampled_peak_rss_kib':max(peaks)}
        observations.append(observation)
        print(json.dumps(observation),flush=True)
        args.output.write_text(json.dumps(observations,indent=2)+'\n')


if __name__=='__main__':
    main()
