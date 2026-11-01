#!/usr/bin/env python3
"""Independent typed tensor DAGs, TP1 bytes and module-governed contracts."""

from __future__ import annotations

import argparse
from copy import deepcopy
import itertools
import json
import math
import os
from pathlib import Path
import struct
import subprocess

from check_binary_codec import ROOT, parse_forms, tensor as byte_tensor
from check_binary_value import Text, construct, encoded
from check_ic_search import check_controls
from check_tensor_sequences import concatenate, coordinates, normalized, observation, ordinal, permute, scalar, scan


OPS = [("Add", "TP:Add", ["F64Tensor", "F64Tensor"], "F64Tensor"),
       ("Subtract", "TP:Subtract", ["F64Tensor", "F64Tensor"], "F64Tensor"),
       ("Multiply", "TP:Multiply", ["F64Tensor", "F64Tensor"], "F64Tensor"),
       ("Divide", "TP:Divide", ["F64Tensor", "F64Tensor"], "F64Tensor"),
       ("Less", "TP:Less", ["F64Tensor", "F64Tensor"], "BoolTensor"),
       ("Greater", "TP:Greater", ["F64Tensor", "F64Tensor"], "BoolTensor"),
       ("Scan", "TP:Scan", ["F64Tensor"], "F64Tensor"),
       ("Concatenate", "TP:Concatenate", ["F64Tensor", "F64Tensor"], "F64Tensor"),
       ("Permute", "TP:Permute", ["F64Tensor"], "F64Tensor"),
       ("Broadcast", "TP:Broadcast", ["F64Tensor"], "F64Tensor"),
       ("Matvec", "TP:Matvec", ["F64Tensor", "F64Tensor"], "F64Tensor"),
       ("Sum", "TP:Sum", ["F64Tensor"], "F64Tensor"),
       ("Gather", "TP:Gather", ["F64Tensor", "I64Tensor"], "F64Tensor"),
       ("BroadcastLike", "TP:BroadcastLike", ["F64Tensor", "F64Tensor"], "F64Tensor"),
       ("GatherRows", "TP:GatherRows", ["F64Tensor", "I64Tensor"], "F64Tensor"),
       ("Where", "TP:Where", ["BoolTensor", "F64Tensor", "F64Tensor"], "F64Tensor"),
       ("WhereBool", "TP:WhereBool", ["BoolTensor", "BoolTensor", "BoolTensor"], "BoolTensor"),
       ("WhereI64", "TP:WhereI64", ["BoolTensor", "I64Tensor", "I64Tensor"], "I64Tensor")]
FAMILY = ["TP:Family", Text("TF1"), [["TP:Operator", Text(name), list(map(Text, inputs)), Text(output), recipe]
                                      for name, recipe, inputs, output in OPS]]
DTYPES = {"F64Tensor": "f64", "I64Tensor": "i64", "BoolTensor": "bool"}


def array(shape, values, sort="F64Tensor"):
    points = list(coordinates(tuple(shape)))
    data = dict(values) if isinstance(values, dict) else dict(zip(points, values))
    return sort, tuple(shape), data


def source_array(value):
    sort, shape, values = value
    if len(values) == math.prod(shape):
        items = " ".join(scalar(values[point]) for point in coordinates(shape))
        return f"(tensor:from-values {DTYPES[sort]} ({' '.join(map(str,shape))}) ({items}))"
    rows = " ".join(f"({ordinal(shape,point)} {scalar(x)})" for point,x in sorted(values.items()))
    return f"(tensor:from-coo {DTYPES[sort]} ({' '.join(map(str,shape))}) ({rows}) reject)"


def transport(value):
    sort, shape, values = value
    code, fmt = {"F64Tensor": (2, "d"), "I64Tensor": (1, "q"), "BoolTensor": (3, "B")}[sort]
    assert len(values) == math.prod(shape)
    return b"CTN1" + bytes([code]) + struct.pack("<I",len(shape)) + b"".join(struct.pack("<Q",n) for n in shape) + b"".join(
        struct.pack("<"+fmt,values[point]) for point in coordinates(shape))


def compact_integer_literal(values):
    ordinary = encoded(["TP:Constant", Text("I64Tensor"), transport(array((len(values),), values, "I64Tensor"))])
    if not all(0 <= value <= 255 for value in values):
        return ordinary
    payload = b"CTN1\x03" + struct.pack("<IQ", 1, len(values)) + bytes(values)
    compact = encoded(["TP:PackedConstant", Text("I64Tensor"), Text("u8"), payload])
    return compact if len(compact) < len(ordinary) else ordinary


def records(semantic):
    result=[]
    for row in semantic:
        if row[0] == "input":
            result.append(["Binary:Leaf", encoded(["TP:Input", Text(row[2]), row[1]])])
        elif row[0] == "constant":
            result.append(["Binary:Leaf", encoded(["TP:Constant", Text(row[1][0]), transport(row[1])])])
        else:
            result.append(["Binary:Node",encoded(["TP:Apply",row[1],row[2]]),row[3]])
    return result


def canonical(roots, rows):
    result=[]; positions=[]
    for row in rows:
        item=deepcopy(row)
        if item[0] == "Binary:Node": item[2]=[positions[i] for i in item[2]]
        if item not in result: result.append(item)
        positions.append(result.index(item))
    return [positions[i] for i in roots], result


def program(roots, semantic):
    projected=records(semantic)
    selected, rows=canonical(roots,projected)
    forest=["Dag:Forest",selected,rows]
    return encoded(["TP:Program",Text("TP1"),FAMILY,forest]), ["Dag:Forest",roots,projected]


def reference(roots, rows, inputs):
    # Expand each selected root independently. No memo table or chronological
    # value fold is shared with the native compact-DAG execution.
    def visit(index):
        row=rows[index]
        if row[0] == "input": return inputs[row[1]]
        if row[0] == "constant": return row[1]
        code, parameters, children=row[1:]
        values=[visit(child) for child in children]
        sort=OPS[code][3]; _, shape, left=values[0]
        if code in (15, 16, 17):
            assert values[0][0] == 'BoolTensor' and values[1][0] == values[2][0] == sort
            assert shape == values[1][1] == values[2][1]
            return array(shape, {point: values[1 if flag else 2][2][point]
                                 for point, flag in left.items()
                                 if point in values[1 if flag else 2][2]}, sort)
        if code < 6:
            _, other_shape, right=values[1]
            assert shape==other_shape and left.keys()==right.keys()
            operation=[lambda x,y:x+y,lambda x,y:x-y,lambda x,y:x*y,lambda x,y:x/y,
                       lambda x,y:x<y,lambda x,y:x>y][code]
            return array(shape,{point:operation(x,right[point]) for point,x in left.items()},sort)
        if code == 6: return array(shape,scan(shape,left,parameters[0],"f64"))
        if code == 7:
            _, other_shape, right=values[1]
            dims, data=concatenate(shape,left,other_shape,right,parameters[0]); return array(dims,data)
        if code == 8:
            dims,data=permute(shape,left,parameters[0]); return array(dims,data)
        if code == 9: return array(parameters[0],{point:left[()] for point in coordinates(parameters[0])})
        if code == 10:
            _, vector_shape, vector=values[1]
            result={}
            for row_index in range(shape[0]):
                if parameters[0]=="sequential":
                    total=0.0
                    for column in range(shape[1]): total += left[row_index,column]*vector[column,]
                else:
                    lanes=[0.0]*4
                    for column in range(shape[1]): lanes[column%4] += left[row_index,column]*vector[column,]
                    total=((lanes[0]+lanes[1])+lanes[2])+lanes[3]
                result[row_index,]=total
            return array((shape[0],),result)
        if code == 11:
            if parameters[0]=="sequential":
                total=0.0
                for point in sorted(left): total += left[point]
            else:
                lanes=[0.0]*4
                for i,point in enumerate(sorted(left)): lanes[i%4]+=left[point]
                total=((lanes[0]+lanes[1])+lanes[2])+lanes[3]
            return array((),[total])
        if code==13:
            assert shape==()
            return array(values[1][1],[left[()] for _point in coordinates(values[1][1])])
        if code==14:
            _, index_shape, indices=values[1]
            assert len(shape)>0 and len(index_shape)==1
            assert all(0<=key<shape[0] for key in indices.values())
            target_shape=index_shape+shape[1:]
            return array(target_shape,{point:left[(indices[point[:1]],)+point[1:]]
                for point in coordinates(target_shape)
                if point[:1] in indices and (indices[point[:1]],)+point[1:] in left})
        _, dims, indices=values[1]
        flattened={ordinal(shape,point):value for point,value in left.items()}
        return array(dims,{point:flattened[target] for point,target in indices.items() if target in flattened})
    return [[json.dumps(sort),observation(shape,data,DTYPES[sort])] for sort,shape,data in map(visit,roots)]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args(); args.output.mkdir(parents=True,exist_ok=True)
    for name in ("lib_tensor_program.metta","lib_tensor_program_module.metta"):
        check_controls((ROOT/"lib/petta"/name).read_text())
    code=['!(import! &self (library lib_tensor_program))', '''
(= (program-test:view $value)
   (case $value
     (((TP:Outputs $values)
       (ProgramResult (map-atom $values $item
         (case $item
           (((TP:Value $sort $tensor) ($sort (tensor:inspect $tensor))) ($other $other))))))
      ($other $other))))
(= (program-test:packed $family $forest $expected)
   (case (tensor-program:pack $family $forest)
     (((TP:Artifact $bytes) (== $bytes $expected)) ($other $other))))
(= (program-test:changed-rule $mode $rule)
   (case $rule
     (((GrammarRule $name $sort $parameters $syntax $evaluation)
       (if (== $mode Output)
         (GrammarRule $name "BoolTensor" $parameters $syntax $evaluation)
         (if (== $mode Arity) (GrammarRule $name $sort LNil $syntax $evaluation)
           (GrammarRule $name $sort
             (LCons (TermAbstraction "bound" (TBase "F64Tensor")) LNil) $syntax $evaluation)))))))
(= (program-test:changed-language $mode $language)
   (case $language
     (((GSLTLanguageDefWireV1 $name $types (LCons $rule $tail) $rewrites $equations)
       (let $changed (program-test:changed-rule $mode $rule)
         (GSLTLanguageDefWireV1 $name $types (LCons $changed $tail) $rewrites $equations)))
      ((LanguageDefV2 $name $types (LCons $rule $tail) $rewrites $equations)
       (let $changed (program-test:changed-rule $mode $rule)
         (LanguageDefV2 $name $types (LCons $changed $tail) $rewrites $equations))))))
''']
    expected=[]
    def add(call,wanted):
        i=len(expected); code.append(f"!(println! (ProgramCase {i} {call}))"); expected.append(["ProgramCase",str(i),wanted])
    a=array((3,),[1.0,2.0,3.0]); b=array((3,),[2.0,3.0,4.0])
    fixtures=[]
    for op in range(6): fixtures.append(([2],[('input',0,'F64Tensor'),('input',1,'F64Tensor'),('op',op,[],[0,1])],[a,b]))
    fixtures += [([3,2,3],[('input',0,'F64Tensor'),('input',1,'F64Tensor'),('op',0,[],[0,1]),('op',6,[0],[2])],[a,b]),
                 ([2],[('input',0,'F64Tensor'),('input',1,'F64Tensor'),('op',7,[0],[0,1])],[a,b]),
                 ([1],[('input',0,'F64Tensor'),('op',8,[[1,0]],[0])],[array((2,3),[1.,2.,3.,4.,5.,6.])]),
                 ([1],[('constant',array((),[2.5])),('op',9,[[2,3]],[0])],[]),
                 ([2],[('input',0,'F64Tensor'),('input',1,'F64Tensor'),('op',10,['sequential'],[0,1])],
                  [array((2,3),[1.,2.,3.,4.,5.,6.]),a]),
                 ([1],[('input',0,'F64Tensor'),('op',11,['fixed4'],[0])],[a]),
                 ([2],[('input',0,'F64Tensor'),('constant',array((4,),[2,0,2,1],'I64Tensor')),('op',12,[],[0,1])],[a]),
                 ([2],[('input',0,'F64Tensor'),('constant',array((3,),[1,0,1],'I64Tensor')),('op',14,[],[0,1])],
                  [array((2,3),[1.,2.,3.,4.,5.,6.])]),
                 ([1],[('input',0,'F64Tensor'),('op',6,[0],[0])],[array((5,),{(1,):2.0,(4,):3.0})]),
                 ([1,0],[('input',0,'F64Tensor'),('input',0,'F64Tensor')],[a]),
                 ([2,0,2],[('input',0,'F64Tensor'),('input',7,'F64Tensor'),('op',0,[],[0,0])],[a]),
                 ([0],[('constant',array((2,),[True,False],'BoolTensor'))],[]),
                 ([],[],[])]
    for shape, data in (((3,),[1.0,2.0,3.0]), ((2,2),[0.0,1.0,2.0,3.0]), ((0,2),[])):
        fixtures.append(([2],[('input',0,'F64Tensor'),('input',1,'F64Tensor'),('op',13,[],[0,1])],
                         [array((),[2.5]),array(shape,data)]))
    for operation, sort, left, right in (
            (15, 'F64Tensor', [-0., 2., 3.], [7., 8., 9.]),
            (16, 'BoolTensor', [True, False, True], [False, True, False]),
            (17, 'I64Tensor', [1, 2, 3], [7, 8, 9])):
        fixtures.append(([3], [('input', 0, 'BoolTensor'), ('input', 1, sort), ('input', 2, sort),
                               ('op', operation, [], [0, 1, 2])],
                         [array((3,), [True, False, True], 'BoolTensor'),
                          array((3,), left, sort), array((3,), right, sort)]))
    fixtures.append(([3], [('input', 0, 'BoolTensor'), ('input', 1, 'F64Tensor'), ('input', 2, 'F64Tensor'),
                           ('op', 15, [], [0, 1, 2])],
                     [array((3,), {(0,): True, (1,): False}, 'BoolTensor'),
                      array((3,), {(0,): -0., (2,): 9.}), array((3,), {})]))
    blobs=[]
    for roots, rows, inputs in fixtures:
        blob, forest=program(roots,rows); blobs.append(blob)
        add(f"(program-test:packed {construct(FAMILY)} {construct(forest)} {byte_tensor(blob)})","true")
        source_inputs='('+' '.join(map(source_array,inputs))+')'
        add(f"(program-test:view (tensor-program:run {byte_tensor(blob)} {source_inputs}))",
            ["ProgramResult",reference(roots,rows,inputs)])
    # Scalar recipes observe the logical value across storage layouts and
    # nonzero backing offsets. They may not invent missing scalar support.
    scalar_broadcast, _ = program([1], [('input',0,'F64Tensor'), ('op',9,[[2,2]],[0])])
    like_broadcast, _ = program([2], [('input',0,'F64Tensor'), ('input',1,'F64Tensor'), ('op',13,[],[0,1])])
    scalar_inputs = ['(tensor:from-coo f64 () ((0 2.5)) reject)',
                     '(tensor:view (tensor:from-values f64 (2) (17.0 2.5)) () () 1)']
    expected_broadcast = ['ProgramResult', [[json.dumps('F64Tensor'),
        observation((2,2), dict.fromkeys(coordinates((2,2)), 2.5), 'f64')]]]
    shape_input = source_array(array((2,2), [0.,1.,2.,3.]))
    for scalar_input in scalar_inputs:
        add(f'(program-test:view (tensor-program:run {byte_tensor(scalar_broadcast)} ({scalar_input})))', expected_broadcast)
        add(f'(program-test:view (tensor-program:run {byte_tensor(like_broadcast)} ({scalar_input} {shape_input})))', expected_broadcast)
    absent_scalar = '(tensor:from-coo f64 () () reject)'
    for program_bytes, inputs in ((scalar_broadcast, f'({absent_scalar})'),
                                  (like_broadcast, f'({absent_scalar} {shape_input})')):
        add(f'(program-test:view (tensor-program:run {byte_tensor(program_bytes)} {inputs}))',
            ['ProgramResult', [['TP:Fault', 'Operation']]])
    bad=deepcopy(FAMILY); bad[2][0][3]=Text("BoolTensor")
    add(f"(tensor-program:pack {construct(bad)} (Dag:Forest () ()))",["TP:Fault","Family"])
    bad=deepcopy(FAMILY); bad[2][0][4]="println!"
    add(f"(tensor-program:pack {construct(bad)} (Dag:Forest () ()))",["TP:Fault","Family"])
    for forest in (["Dag:Forest",[0],[["Binary:Node",encoded(["TP:Apply",0,[]]),[0,0]]]],
                   ["Dag:Forest",[1],[]]):
        add(f"(tensor-program:pack {construct(FAMILY)} {construct(forest)})",["TP:Fault","References"])
    for row in (("op",0,[],[0]),("op",99,[],[0]),("op",6,[-1],[0])):
        if row[2]==[-1]:
            # BV1 deliberately excludes signed metadata; test the public
            # parameter validator without pretending it encoded this value.
            add('(tensor-program:parameters? Axis (-1))',"false"); continue
        forest=["Dag:Forest",[1],records([("input",0,"F64Tensor"),row])]
        add(f"(tensor-program:pack {construct(FAMILY)} {construct(forest)})",["TP:Fault","Types"])
    add('(tensor-program:node 0.0 () ())',["TP:Fault","Application"])
    add(f"(program-test:view (tensor-program:run {byte_tensor(blobs[0])} "
        f"({source_array(array((3,),[1,2,3],'I64Tensor'))} {source_array(b)})))",
        ["ProgramResult",[["TP:Fault","InputSort"]]])
    bad_types=["Dag:Forest",[2],records([('input',0,'BoolTensor'),('input',1,'F64Tensor'),('op',0,[],[0,1])])]
    add(f"(tensor-program:pack {construct(FAMILY)} {construct(bad_types)})",["TP:Fault","Types"])
    add(f"(program-test:view (tensor-program:run {byte_tensor(blobs[0])} ()))",
        ["ProgramResult",[["TP:Fault","InputIndex"]]])
    bad_version=encoded(["TP:Program",Text("TP2"),FAMILY,["Dag:Forest",[],[]]])
    add(f"(tensor-program:unpack {byte_tensor(bad_version)})",["TP:Fault","Codec"])
    for storage in (transport(array((2,),[0.0,1.0])),
                    b"CTN1"+bytes([3])+struct.pack("<IQ",1,1)+bytes([2])):
        bad_mask=["Dag:Forest",[0],[["Binary:Leaf",encoded(["TP:Constant",Text("BoolTensor"),storage])]]]
        add(f"(tensor-program:pack {construct(FAMILY)} {construct(bad_mask)})",["TP:Fault","Types"])
    # A finite task domain rejects a nonfinite operation result. The generic
    # tensor kernel is still allowed to represent IEEE infinity itself.
    overflow, _forest=program([2],[('input',0,'F64Tensor'),('input',1,'F64Tensor'),('op',3,[],[0,1])])
    add(f"(program-test:view (tensor-program:run {byte_tensor(overflow)} "
        f"({source_array(array((1,),[1.0]))} {source_array(array((1,),[0.0]))})))",
        ["ProgramResult",[["TP:Fault","Operation"]]])
    # Selection consumes values, not lazy code. A bad computation in the
    # unused branch still propagates through the strict TP1 DAG.
    strict_selection, _ = program([4], [('constant', array((1,), [True], 'BoolTensor')),
        ('input', 0, 'F64Tensor'), ('input', 1, 'F64Tensor'), ('op', 3, [], [1, 2]),
        ('op', 15, [], [0, 1, 3])])
    add(f'(program-test:view (tensor-program:run {byte_tensor(strict_selection)} '
        f'({source_array(array((1,), [1.]))} {source_array(array((1,), [0.]))})))',
        ['ProgramResult', [['TP:Fault', 'Operation']]])
    where_rows = [('input', 0, 'F64Tensor'), ('input', 1, 'F64Tensor'), ('input', 2, 'F64Tensor'),
                  ('op', 15, [], [0, 1, 2])]
    _, wrong_where = program([3], where_rows)
    add(f'(tensor-program:pack {construct(FAMILY)} {construct(wrong_where)})', ['TP:Fault', 'Types'])
    used_fault,_forest=program([2,0],[('input',0,'F64Tensor'),('input',7,'F64Tensor'),('op',0,[],[0,1])])
    add(f"(program-test:view (tensor-program:run {byte_tensor(used_fault)} ({source_array(a)})))",
        ["ProgramResult",[["TP:Fault","InputIndex"],[json.dumps("F64Tensor"),observation(a[1],a[2],"f64")]]])
    ordered_faults,_forest=program([2,3],[('input',0,'F64Tensor'),('input',7,'F64Tensor'),
                                        ('op',0,[],[0,1]),('op',0,[],[1,0])])
    add(f"(program-test:view (tensor-program:run {byte_tensor(ordered_faults)} "
        f"({source_array(array((3,),[1,2,3],'I64Tensor'))})))",
        ["ProgramResult",[["TP:Fault","InputSort"],["TP:Fault","InputIndex"]]])
    code += ['!(import! &self (library lib_tensor_program_module))','!(import! &self (library lib_module))',
             '(= (Add $x $y) (println! "OperatorLabelMustNotRun"))',
             '(: ProgramPretender Tensor)', '(= (tensor:dtype ProgramPretender) f64)',
             '(= (tensor:all-finite ProgramPretender) True)']
    add('(tensor-program:value? "F64Tensor" ProgramPretender)',"false")
    implementations=[["TP:Implementation",Text(name),recipe] for name,recipe,_inputs,_out in OPS]
    # The module-derived family actually packs and runs a program. Its field
    # sorts and arities are executable admission conditions, not label checks.
    _, forest=program(*fixtures[6][:2])
    module_prefix=f'''(let* (($reader (langdef:load-baked-parser "module-core-reader"))
                   ($document (module:read-file $reader "tensor-operators" "langdef/tensor_program/Operators.module"))
                   ($closed (langdef:close-parser $reader))
                   ($presentation (module:assemble $document "Operators" LNil LNil OriginViewV1 "tensor-program"))
                   ($language (module:core $presentation "TensorOperators")))'''
    add(module_prefix+f'''(let $projected (tensor-program:family-from-language $language {construct(implementations)})
          (case $projected
            (((TP:Some $family)
              (case (tensor-program:pack $family {construct(forest)})
                (((TP:Artifact $bytes) (program-test:view (tensor-program:run $bytes ({source_array(a)} {source_array(b)}))))
                 ($other $other)))) ($other $other)))))''',["ProgramResult",reference(*fixtures[6])])
    for mode, reason in (("Output","Contract"),("Arity","Contract"),("Binding","Projection")):
        add(module_prefix+f'''(tensor-program:family-from-language
            (program-test:changed-language {mode} $language) {construct(implementations)}))''',["TP:Fault",reason])
    source='\n'.join(code)+'\n'; check_controls(source)
    path=args.output/'program.metta'; path.write_text(source)
    result=subprocess.run([str(args.cetta.resolve()),'--lang','petta',str(path.resolve())],cwd=ROOT,
                          text=True,capture_output=True,timeout=90)
    (args.output/'program.log').write_text(result.stdout+result.stderr)
    actual=[form for form in parse_forms(result.stdout) if isinstance(form,list) and form and form[0]=='ProgramCase']
    found={form[1]:form for form in actual}
    wrong=[{'case':i,'expected':want,'actual':found.get(str(i))} for i,want in enumerate(expected)
           if normalized(found.get(str(i))) != normalized(want)]
    receipt={'passed':result.returncode==0 and len(actual)==len(expected) and not wrong and 'OperatorLabelMustNotRun' not in result.stdout,
             'checks':len(expected),'answers':len(actual),'mismatches':wrong,'program_fixtures':len(fixtures),
             'recipes':[row[1] for row in OPS],'scope':'TP1 byte correspondence, typed closed recipes, compact versus unfolded execution and module projection'}
    (args.output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({**receipt,'mismatches':[row['case'] for row in wrong]}))
    for row in wrong[:4]: print(json.dumps(row)[:2500])
    if result.returncode: print(result.stderr[-1500:])
    return 0 if receipt['passed'] else 1


if __name__=='__main__': raise SystemExit(main())
