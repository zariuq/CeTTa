#!/usr/bin/env python3
"""Independent PV1 bytes, signed/F64 payloads and inert portable reconstruction."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import struct
import subprocess

from check_binary_codec import ROOT, observation, parse_forms, tensor
from check_binary_tree import packed
from check_binary_value import Text, construct as basic_construct, encoded_value as basic_value
from check_ic_archive import uint
from check_ic_search import check_controls


@dataclass
class ArrayValue:
    dtype: str
    shape: tuple
    values: tuple


def ctn1(dtype, shape, values):
    code, fmt = {'i64': (1, 'q'), 'f64': (2, 'd'), 'u8': (3, 'B'), 'f32': (4,'f')}[dtype]
    return b'CTN1'+bytes([code])+struct.pack('<I',len(shape))+b''.join(struct.pack('<Q',x) for x in shape)+b''.join(
        struct.pack('<'+fmt,x) for x in values)


def encoded_value(value):
    if isinstance(value, ArrayValue):
        if value.dtype == 'u8' and len(value.shape) == 1:
            return basic_value(bytes(value.values))
        stored={'bool':'u8','index':'i64'}.get(value.dtype,value.dtype)
        return 'node', uint(8), [('leaf',value.dtype.encode()),('leaf',ctn1(stored,value.shape,value.values))]
    if type(value) is float or type(value) is int and value < 0:
        dtype = 'f64' if type(value) is float else 'i64'
        return 'node', uint(7), [('leaf',dtype.encode()),('leaf',ctn1(dtype,(),(value,)))]
    if isinstance(value,list):
        return 'node',uint(5),[('node',b'',[encoded_value(x) for x in value])]
    return basic_value(value)


def encoded(value, version=b'PV1'):
    return packed(('node',uint(6),[('leaf',version),encoded_value(value)]))


def construct(value):
    if isinstance(value,ArrayValue):
        values=' '.join(('True' if x else 'False') if value.dtype=='bool' else repr(x) for x in value.values)
        return f'(tensor:from-values {value.dtype} ({" ".join(map(str,value.shape))}) ({values}))'
    if type(value) is float or type(value) is int and value<0:
        return repr(value)
    if isinstance(value,list):
        result='()'
        for child in reversed(value):result=f'(cons-atom {construct(child)} {result})'
        return result
    return basic_construct(value)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    check_controls((ROOT/'lib/petta/lib_portable_value.metta').read_text())
    code=['!(import! &self (library lib_portable_value))',r'''
(= (portable-test:roundtrip $value)
   (case (portable-value:pack $value)
     (((Binary:TreeBytes $bytes)
       (case (portable-value:unpack $bytes)
         (((BinaryValue:Value $restored)
           (case (portable-value:pack $restored)
             (((Binary:TreeBytes $after) (PortableBytes (tensor:inspect $bytes) (== $bytes $after)))
              ($failed $failed)))) ($failed $failed)))) ($failed $failed))))
(= (portable-test:kind $value) (binary:portable-kind $value))
''']
    expected=[]
    def add(call,wanted):
        index=len(expected);code.append(f'!(println! (PortableCase {index} {call}))')
        expected.append(['PortableCase',str(index),wanted])
    values=[0,2**63-1,-1,-2**63,0.0,-0.0,1.25,-3.5,Text('λ\0□'),b'\0\xff',
            ['a',-1,1.0,Text('a'),'a'],ArrayValue('f64',(2,),(-0.0,2.5)),
            ArrayValue('i64',(2,),(-2**63,2**63-1)),ArrayValue('bool',(3,),(1,0,1)),
            ArrayValue('index',(2,),(0,2**63-1)),ArrayValue('f32',(2,),(1.5,-2.0)),
            ArrayValue('f64',(0,),()),ArrayValue('u8',(2,),(0,255)),
            ['println!',Text('PORTABLE-MUST-NOT-RUN')],['NativeHandle',Text('dag'),1]]
    for value in values:
        add(f'(portable-test:roundtrip {construct(value)})',['PortableBytes',observation(encoded(value)),'true'])
    for value in values[-2:]:
        add(f'(case (portable-value:unpack {tensor(encoded(value))}) '
            '(((BinaryValue:Value $value) (let $copy $value (portable-test:kind $copy))) ($bad $bad)))','Expression')
    for value,kind in [('-1','Integer'),('1.0','Float'),('0','Natural'),('(new-space)','Unsupported')]:
        add(f'(binary:portable-kind {value})',kind)
    for value in ('-1','1.0'):
        add(f'(binary-value:pack {value})',['BinaryValue:Fault','UnsupportedValue'])
    for bad in ('$unbound','(new-space)'):
        add(f'(portable-value:pack {bad})',['BinaryValue:Fault','UnsupportedValue'])
    add('(portable-value:pack (tensor:from-coo f64 (2) ((0 0.0))))',
        ['BinaryValue:Fault','UnsupportedValue'])
    add('(portable-value:pack (tensor:from-values index (1) (18446744073709551615)))',
        ['BinaryValue:Fault','UnsupportedValue'])
    for tag,children,expected_fault in [
        (7,[('leaf',b'bool'),('leaf',ctn1('u8',(),(1,)))],'InvalidValue'),
        (7,[('leaf',b'i64'),('leaf',ctn1('f64',(),(1.0,)))],'InvalidValue'),
        (7,[('leaf',b'i64'),('leaf',ctn1('i64',(1,),(1,)))],'InvalidValue'),
        (7,[('leaf',b'i64'),('leaf',ctn1('i64',(),(1,)))],'Noncanonical'),
        (8,[('leaf',b'f64'),('leaf',b'invalid')],'InvalidValue'),
        (8,[('leaf',b'bool'),('leaf',ctn1('u8',(1,),(2,)))],'InvalidValue'),
    ]:
        blob=packed(('node',uint(6),[('leaf',b'PV1'),('node',uint(tag),children)]))
        add(f'(portable-value:unpack {tensor(blob)})',['BinaryValue:Fault',expected_fault])
    add(f'(portable-value:unpack {tensor(encoded(1,b"BV1"))})',['BinaryValue:Fault','Version'])
    code.extend(['!(import! &self (library dag))','!(import! &self (library syntax))'])
    add('(let $handle (dag:from-forest ()) '
        '(let $result (portable-value:pack $handle) (let $closed (dag:close $handle) $result)))',
        ['BinaryValue:Fault','UnsupportedValue'])
    add('(portable-value:pack (syntax:restore (syntax:read "(f $x)")))',
        ['BinaryValue:Fault','UnsupportedValue'])
    source='\n'.join(code)+'\n';check_controls(source)
    path=args.output/'portable-values.metta';path.write_text(source)
    run=subprocess.run([str(args.cetta.resolve()),'--lang','petta',str(path.resolve())],
                       cwd=ROOT,capture_output=True,text=True,timeout=60)
    (args.output/'stdout').write_text(run.stdout);(args.output/'stderr').write_text(run.stderr)
    actual=[row for row in parse_forms(run.stdout) if isinstance(row,list) and row[:1]==['PortableCase']]
    indexed={row[1]:row for row in actual}
    differences=[{'case':i,'expected':want,'actual':indexed.get(str(i))}
                 for i,want in enumerate(expected) if want!=indexed.get(str(i))]
    inert='"PORTABLE-MUST-NOT-RUN"' not in run.stdout.splitlines()
    report={'passed':run.returncode==0 and not run.stderr and not differences and len(actual)==len(expected) and inert,
            'checks':len(expected),'observations':len(actual),'differences':differences,'decoded_execution_absent':inert,
            'scope':'PV1 exact bytes and CTN1 scalar/tensor payloads; BV1 contract unchanged; no authority transport'}
    (args.output/'receipt.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({**report,'differences':[{'case':row['case']} for row in differences]}))
    if not report['passed']:raise SystemExit(1)


if __name__=='__main__':main()
