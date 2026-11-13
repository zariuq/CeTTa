"""Interface selection for the qualified copy proof recipe.

This validates evidence shape; HOL proves the selected artifact contract.
It does not turn metadata or hashes into a source-refinement theorem.
"""

import copy
import json


def copy_contract(record, symbol):
    if record.get('format') != 'cesta-span-contract/1' or record.get('word_bits') != 64:
        raise ValueError('copy proof requires a 64-bit span boundary contract')
    matches = [f for f in record['functions'] if f['symbol'] == symbol]
    if len(matches) != 1: raise ValueError('copy symbol must have one contract')
    function, = matches
    if len(function['params']) != 2: raise ValueError('copy ABI needs two spans')
    for parameter, mode in zip(function['params'], ('read', 'write')):
        expected = dict(element='u32',mode=mode,element_bytes=4,
                        alignment='c-alignof',count_limit='PTRDIFF_MAX/element_bytes')
        if parameter['type'] != expected: raise ValueError('copy span type/extent disagrees')
    if function['result'] != {'scalar':'unit'} or function['borrowed_from'] != -1:
        raise ValueError('copy result/lifetime disagrees')
    if function['separate'] != [[0,1]]: raise ValueError('exclusive output requires input separation')
    source, destination = (p['name'] for p in function['params'])
    if function['entry_requires'] != [['le',['len',source],['len',destination]]]:
        raise ValueError('copy proof requires the declared input/output length relation')
    return function


def mutation_controls(record, symbol):
    def width(r):r['functions'][0]['params'][0]['type']['element_bytes'] = 8
    def mode(r):r['functions'][0]['params'][1]['type']['mode'] = 'read'
    def pair(r):r['functions'][0]['separate'] = []
    def origin(r):r['functions'][0]['borrowed_from'] = 0
    def guard(r):r['functions'][0]['entry_requires'] = []
    def target(r):r['word_bits'] = 32
    # The pilot has one local function; larger interfaces are handled by the
    # general emitter/admission tests, not by silently selecting another row.
    for mutate in (width,mode,pair,origin,guard,target):
        changed=copy.deepcopy(record);mutate(changed)
        try:copy_contract(changed,symbol)
        except ValueError:pass
        else:raise AssertionError('changed interface was accepted')
    return 6


def load(path,symbol):
    record=json.loads(path.read_text())
    copy_contract(record,symbol)
    return record
