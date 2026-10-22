#!/usr/bin/env python3
"""Matched dense/sparse tensor kernels; this is not a whole-script benchmark.

Exact dyadic fixtures make sequential and fixed4 observations comparable to
NumPy/SciPy without concealing schedule-dependent rounding on general inputs.
The correctness suite separately exercises those non-associative cases.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import struct
import subprocess
import time


def median_call(call, repetitions, iterations):
    walls, cpus = [], []
    for _ in range(repetitions):
        start, cpu = time.perf_counter_ns(), time.process_time_ns()
        for _ in range(iterations):
            call()
        walls.append((time.perf_counter_ns()-start)/iterations)
        cpus.append((time.process_time_ns()-cpu)/iterations)
    return {'wall_ns':statistics.median(walls), 'cpu_ns':statistics.median(cpus)}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--full',action='store_true',help='Include beyond-cache inputs')
    parser.add_argument('--repetitions',type=int,default=7)
    parser.add_argument('--threads',type=int,default=1)
    args=parser.parse_args()
    if min(args.threads,args.repetitions)<1:
        parser.error('positive repetitions and threads required')
    for name in ('OPENBLAS_NUM_THREADS','OMP_NUM_THREADS','MKL_NUM_THREADS'):
        os.environ[name]=str(args.threads)
    import numpy as np
    import scipy
    from scipy import sparse

    args.output.parent.mkdir(parents=True,exist_ok=True)
    panels=[('dense','f64',64,16,16),('dense','f64',256,64,64),
            ('dense','f64',4096,256,256),('dense','f32',4096,256,256),
            ('coo','f64',4096,4096,16),('csr','f64',4096,4096,16),
            ('csc','f64',4096,4096,16)]
    if args.full:
        panels += [('dense','f64',32768,1024,1024),
                   ('dense','f32',32768,1024,1024),
                   ('csr','f64',65536,4096,16),('csc','f64',65536,4096,16)]
    report={'scope':'native packed kernels, not evaluator/application parity',
            'native_binary_sha256':hashlib.sha256(args.native.read_bytes()).hexdigest(),
            'numpy':np.__version__,'scipy':scipy.__version__,
            'threads':args.threads,'repetitions':args.repetitions,'panels':[]}
    for layout,dtype,rows,cols,per_row in panels:
        label=f'{layout}-{dtype}-{rows}-{cols}-{per_row}'
        wire=args.output.parent/(label+'.ctn')
        start=time.perf_counter_ns()
        native=subprocess.run([str(args.native.resolve()),layout,dtype,str(rows),str(cols),
                               str(per_row),str(args.repetitions),str(wire)],
                              text=True,capture_output=True,check=True,timeout=600)
        native_wall=time.perf_counter_ns()-start
        result=json.loads(native.stdout)
        payload=wire.read_bytes()
        assert payload[:5]==b'CTN1\x02'
        rank=struct.unpack_from('<I',payload,5)[0]
        assert rank==1 and struct.unpack_from('<Q',payload,9)[0]==rows
        actual=np.frombuffer(payload,dtype='<f8',offset=17)
        assert len(actual)==rows
        load_start=time.perf_counter_ns()
        row_ids=np.arange(rows,dtype=np.int64)
        column_ids=np.arange(cols,dtype=np.int64)
        query=((column_ids%7)-3).astype(np.float64)/8
        scalar_type=np.float64 if dtype=='f64' else np.float32
        if layout=='dense':
            values=(((row_ids[:,None]%17)*13+(column_ids[None,:]%17)*7)%17-8).astype(scalar_type)/16
            loaded=time.perf_counter_ns()
            matrix=values
            prepared=time.perf_counter_ns()
        else:
            coordinates=np.tile(np.arange(per_row,dtype=np.int64)*(cols//per_row),rows)
            owners=np.repeat(row_ids,per_row)
            values=(((owners%17)*13+(coordinates%17)*7)%17-8).astype(scalar_type)/16
            loaded=time.perf_counter_ns()
            matrix=sparse.coo_matrix((values,(owners,coordinates)),shape=(rows,cols))
            if layout=='csr':
                matrix=matrix.tocsr()
            elif layout=='csc':
                matrix=matrix.tocsc()
            prepared=time.perf_counter_ns()
        operation=lambda: matrix @ query
        first_start=time.perf_counter_ns()
        expected=operation()
        first=time.perf_counter_ns()-first_start
        np.testing.assert_array_equal(actual,expected,err_msg=label)
        # Partition finds the threshold; source-row order resolves its ties.
        k=min(rows,32)
        def top_k():
            if not k:
                return np.empty(0,dtype=np.int64)
            threshold=np.partition(expected,rows-k)[rows-k]
            above=np.flatnonzero(expected>threshold)
            tied=np.flatnonzero(expected==threshold)[:k-len(above)]
            candidates=np.concatenate((above,tied))
            return candidates[np.lexsort((candidates,-expected[candidates]))]
        assert result['top_k']==top_k().tolist(),label
        matched=median_call(operation,args.repetitions,result['iterations'])
        top=median_call(top_k,args.repetitions,1)
        retained=matrix.nbytes if layout=='dense' else matrix.data.nbytes+(
            matrix.row.nbytes+matrix.col.nbytes if layout=='coo' else matrix.indices.nbytes+matrix.indptr.nbytes)
        result['whole_process_ns']=native_wall
        panel={'fixture':label,'equal_scores':True,'equal_top_k':True,'native':result,
               'reference':{'load_ns':loaded-load_start,'prepare_ns':prepared-loaded,
                            'first_ns':first,'steady':matched,'top_k':top,
                            'retained_bytes':retained+query.nbytes+expected.nbytes}}
        if dtype=='f32':
            # A strong repeated-query reference may widen once during
            # preparation rather than make NumPy promote the matrix per call.
            begin=time.perf_counter_ns(); widened=matrix.astype(np.float64)
            widening_ns=time.perf_counter_ns()-begin
            np.testing.assert_array_equal(widened@query,actual)
            panel['reference']['prepared_f64']={'prepare_ns':widening_ns,
                'steady':median_call(lambda:widened@query,args.repetitions,result['iterations']),
                'dtype':'f64 (exact widening of source f32)'}
        report['panels'].append(panel)
        print(json.dumps({'fixture':label,'native_sequential_ns':result['sequential_ns'],
                          'native_fixed4_ns':result['fixed4_ns'],
                          'reference_ns':matched['wall_ns'],'equal':True}),flush=True)
        args.output.write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':
    main()
