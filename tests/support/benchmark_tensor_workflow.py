#!/usr/bin/env python3
"""Public PeTTa JSON-to-packed-matvec workflow versus JSON/NumPy.

The complete first result is compared before timings are reported. Input I/O,
typed preparation, steady calls and whole-process time are separate prices.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import time

ROOT=Path(__file__).resolve().parents[2]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--repeat',type=int,default=3)
    parser.add_argument('--iterations',type=int,default=32)
    args=parser.parse_args()
    if min(args.repeat,args.iterations)<1:
        parser.error('positive repetition counts required')
    os.environ['OPENBLAS_NUM_THREADS']='1'
    import numpy as np
    args.output.mkdir(parents=True,exist_ok=True)
    observations=[]
    for rows,cols in ((64,16),(4096,256)):
        row=np.arange(rows,dtype=np.int64)[:,None]; col=np.arange(cols,dtype=np.int64)[None,:]
        matrix=(((row%17)*13+(col%17)*7)%17-8).astype(np.float64)/16
        query=((np.arange(cols)%7)-3).astype(np.float64)/8
        expected=matrix@query
        data=args.output/f'dense-{rows}-{cols}.json'
        data.write_text(json.dumps({'matrix':matrix.tolist(),'query':query.tolist()},separators=(',',':')))
        wire=args.output/f'dense-{rows}-{cols}.ctn'
        program=args.output/f'dense-{rows}-{cols}.metta'
        quote=lambda s:json.dumps(str(s))
        program.write_text(f'''!(import! &self (library tensor))
!(import! &self fs)
!(import! &self system)
!(let* (($start (system:monotonic-ns)) ($text (fs:read-text {quote(data.resolve())}))
        ($loaded (system:monotonic-ns))
        ($matrix (tensor:from-json f64 ({rows} {cols}) $text ("matrix")))
        ($query (tensor:from-json f64 ({cols}) $text ("query")))
        ($prepared (system:monotonic-ns))
        ($first (tensor:matvec $matrix $query)) ($bytes (tensor:encode $first))
        ($written (fs:write {quote(wire.resolve())} $bytes))
        ($begin (system:monotonic-ns))
        ($observed (foldl-atom ({' '.join(map(str,range(args.iterations)))}) 0.0 $last $i
          (let $result (tensor:matvec $matrix $query)
            (case (tensor:at $result 0) (((tensor:present $value) $value))))))
        ($end (system:monotonic-ns)))
   (TensorWorkflow (- $loaded $start) (- $prepared $loaded) (- $end $begin) $observed))
''')
        native=[]; reference=[]
        for _ in range(args.repeat):
            begin=time.perf_counter_ns()
            result=subprocess.run([str(args.cetta.resolve()),'--lang','petta',str(program.resolve())],
                                  cwd=ROOT,text=True,capture_output=True,timeout=180)
            whole=time.perf_counter_ns()-begin
            assert result.returncode==0,(result.stdout[-1000:],result.stderr)
            match=re.search(r'\(TensorWorkflow (\d+) (\d+) (\d+) ([^)]+)\)',result.stdout)
            assert match,(result.stdout,result.stderr)
            load,prepare,work=map(int,match.groups()[:3])
            assert float(match[4])==float(expected[0]),match[4]
            payload=wire.read_bytes()
            assert payload[:5]==b'CTN1\x02' and int.from_bytes(payload[5:9],'little')==1
            np.testing.assert_array_equal(np.frombuffer(payload,dtype='<f8',offset=17),expected)
            native.append({'load_ns':load,'prepare_ns':prepare,'work_ns':work/args.iterations,'whole_ns':whole})
            begin=time.perf_counter_ns(); text=data.read_text(); loaded=time.perf_counter_ns()
            parsed=json.loads(text); pm=np.asarray(parsed['matrix'],dtype=np.float64); pq=np.asarray(parsed['query'],dtype=np.float64)
            prepared=time.perf_counter_ns()
            np.testing.assert_array_equal(pm@pq,expected)
            work_start=time.perf_counter_ns()
            for _ in range(args.iterations):
                observed=(pm@pq)[0]
            end=time.perf_counter_ns()
            assert observed==expected[0]
            reference.append({'load_ns':loaded-begin,'prepare_ns':prepared-loaded,'work_ns':(end-work_start)/args.iterations})
        panel={'rows':rows,'columns':cols,'equal_all_scores':True,
               'cetta':{key:statistics.median(x[key] for x in native) for key in native[0]},
               'python':{key:statistics.median(x[key] for x in reference) for key in reference[0]}}
        observations.append(panel); print(json.dumps(panel),flush=True)
    report={'scope':'public PeTTa operations, JSON typed preparation and ordered dyadic matvec',
            'binary_sha256':hashlib.sha256(args.cetta.read_bytes()).hexdigest(),
            'iterations':args.iterations,'repetitions':args.repeat,'numpy':np.__version__,
            'blas_threads':1,'panels':observations}
    (args.output/'report.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':
    main()
