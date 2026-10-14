"""Finite completed-observation comparison, with an explicit name bijection.

Only Factor fields at declared runtime-wrapper paths are names. All other
integers, constructor shapes, coefficients and ordered factor positions stay
literal. Answer positions are a multiset of occurrences: a permutation may
reorder them, but may neither duplicate nor omit a position. The one name map
covers both Factor columns and every answer, retaining all equality relations.
This is an observation relation, not replay authentication or a cost quotient.
"""
from copy import deepcopy
from collections import Counter
import json


def factor_paths(term, wrappers=((),)):
    """Inspect only externally declared runtime-wrapper locations.

    The schema is supplied by the authored client contract. Constructor names
    in arbitrary subject/coefficient data grant no identity-renaming authority.
    """
    wrappers = tuple(tuple(path) for path in wrappers)
    if len(set(wrappers)) != len(wrappers):
        raise ValueError('duplicate runtime-wrapper path')
    for path in wrappers:
        if any(type(i) is not int or i < 0 for i in path):
            raise ValueError('invalid runtime-wrapper path')
        node = at(term, path)
        if (not isinstance(node, list) or len(node) != 4 or node[0] != 'Weighted' or
            not isinstance(node[3], list) or len(node[3]) != 2 or node[3][0] != 'provenance' or
            not isinstance(node[3][1], list)):
            raise ValueError('declared wrapper is not a runtime Weighted record')
        for j, factor in enumerate(node[3][1]):
            if (not isinstance(factor, list) or len(factor) != 4 or factor[0] != 'Factor' or
                any(type(factor[i]) is not int or factor[i] <= 0 for i in (1, 2))):
                raise ValueError('Factor names must be positive; zero is not a fresh name')
            yield path + (3, 1, j, 1), factor[1]
            yield path + (3, 1, j, 2), factor[2]


def at(term, path):
    for i in path:
        term = term[i]
    return term


def signature(term, wrappers=((),)):
    copy = deepcopy(term)
    for path, _ in factor_paths(term, wrappers):
        cursor = copy
        for i in path[:-1]:
            cursor = cursor[i]
        cursor[path[-1]] = {'source-occurrence-name': True}
    return json.dumps(copy, separators=(',', ':'), sort_keys=True)


def occurrence_witness(left, right, ordered=False, wrappers=((),)):
    """Search a whole-observation bijection; return a concrete witness or None."""
    if len(left) != len(right):
        return None
    left_sig = [signature(x, wrappers) for x in left]
    right_sig = [signature(x, wrappers) for x in right]
    if Counter(left_sig) != Counter(right_sig):
        return None
    candidate = [[i] if ordered else [j for j, s in enumerate(right_sig) if s == left_sig[i]]
                 for i in range(len(left))]
    paths = [list(factor_paths(x, wrappers)) for x in left]
    right_paths = [dict(factor_paths(x, wrappers)) for x in right]

    def search(i, used, forward, inverse, permutation):
        if i == len(left):
            return {'answer_permutation': permutation, 'name_bijection': sorted(forward.items()),
                    'ordered': ordered, 'runtime_wrapper_paths': [list(p) for p in wrappers]}
        for j in candidate[i]:
            if j in used or left_sig[i] != right_sig[j]:
                continue
            f, inv = dict(forward), dict(inverse)
            okay = True
            for path, source_name in paths[i]:
                target_name = right_paths[j][path]
                if ((source_name in f and f[source_name] != target_name) or
                    (target_name in inv and inv[target_name] != source_name)):
                    okay = False
                    break
                f[source_name] = target_name
                inv[target_name] = source_name
            if okay:
                found = search(i + 1, used | {j}, f, inv, permutation + [j])
                if found is not None:
                    return found
        return None

    return search(0, set(), {}, {}, [])


def check_witness(left, right, witness, wrappers=((),), *, ordered=False):
    """Independently apply a supplied map, then compare full trees literally."""
    if witness is None or len(left) != len(right):
        return False
    # Ordering belongs to the requested observation, never to untrusted evidence.
    if type(ordered) is not bool or witness.get('ordered') is not ordered:
        return False
    if witness.get('runtime_wrapper_paths') != [list(p) for p in wrappers]:
        return False
    permutation = witness['answer_permutation']
    pairs = witness['name_bijection']
    if sorted(permutation) != list(range(len(right))):
        return False
    if ordered and permutation != list(range(len(right))):
        return False
    mapping = dict(pairs)
    if len(mapping) != len(pairs) or len(set(mapping.values())) != len(mapping):
        return False
    # This verifier validates the declared fields directly; it does not call
    # the searcher's field extractor or its masked signatures.
    def locations(answers):
        result = []
        if len(set(tuple(p) for p in wrappers)) != len(wrappers):
            raise ValueError('duplicate schema path')
        for answer in answers:
            current = []
            for path in wrappers:
                node = answer
                for index in path:
                    if type(index) is not int or index < 0:
                        raise ValueError('invalid schema index')
                    node = node[index]
                if not isinstance(node, list) or len(node) != 4 or node[0] != 'Weighted':
                    raise ValueError('missing declared wrapper')
                provenance = node[3]
                if (not isinstance(provenance, list) or len(provenance) != 2 or
                    provenance[0] != 'provenance' or not isinstance(provenance[1], list)):
                    raise ValueError('missing declared provenance')
                for j, factor in enumerate(provenance[1]):
                    if not isinstance(factor, list) or len(factor) != 4 or factor[0] != 'Factor':
                        raise ValueError('missing declared factor')
                    for column in (1, 2):
                        name = factor[column]
                        if type(name) is not int or name <= 0:
                            raise ValueError('nonpositive occurrence name')
                        current.append((tuple(path) + (3, 1, j, column), name))
            result.append(current)
        return result
    try:
        left_locations, right_locations = locations(left), locations(right)
    except (ValueError, TypeError, IndexError):
        return False
    source_names = {name for locations0 in left_locations for _, name in locations0}
    target_names = {name for locations0 in right_locations for _, name in locations0}
    if set(mapping) != source_names or set(mapping.values()) != target_names:
        return False
    translated = deepcopy(left)
    for answer, fields in zip(translated, left_locations):
        for path, name in fields:
            parent = answer
            for index in path[:-1]:
                parent = parent[index]
            parent[path[-1]] = mapping[name]
    return all(translated[i] == right[j] for i, j in enumerate(permutation))


def meaning(value):
    if not isinstance(value, list):
        return value
    if value and value[0] == 'Weighted':
        if len(value) != 4 or len(value[3]) != 2 or value[3][0] != 'provenance':
            raise ValueError('malformed Weighted')
        factors = value[3][1]
        ids = [f[1] for f in factors]
        if len(set(ids)) != len(ids):
            raise ValueError('duplicate production in one contribution')
        list(factor_paths(value))
        return [deepcopy(value[1]), deepcopy(value[2]), [deepcopy(f[3]) for f in factors]]
    return deepcopy(value)


def controls():
    def w(answer, factors):
        return ['Weighted', answer, ['Then', *[f[3] for f in factors]], ['provenance', factors]]
    f = lambda i, d, v: ['Factor', i, d, v]
    source = [w('A', [f(1, 2, 'X'), f(3, 4, 'Y')]),
              w('A', [f(1, 2, 'X'), f(5, 4, 'Y')]),
              w('A', [f(1, 2, 'X'), f(5, 4, 'Y')])]
    target = deepcopy([source[2], source[0], source[1]])
    for answer in target:
        for path, n in list(factor_paths(answer)):
            parent = answer
            for i in path[:-1]: parent = parent[i]
            parent[path[-1]] = n * 7 + 100
    cases = [('fresh names and duplicate permutation', target, True),
             ('missing duplicate', target[:-1], False),
             ('extra duplicate', target + [deepcopy(target[0])], False)]
    changed = deepcopy(target); changed[0][3][1][0][1] = 999
    cases.append(('fractured shared production', changed, False))
    changed = deepcopy(target); changed[0][3][1][1][2] = 999
    cases.append(('fractured dependency', changed, False))
    changed = deepcopy(target)
    for a in changed:
        for f0 in a[3][1]:
            if f0[1] == 135: f0[1] = 121
    cases.append(('coalesced distinct production', changed, False))
    changed = deepcopy(target); changed[0][3][1].reverse()
    cases.append(('reordered factors', changed, False))
    changed = deepcopy(target); changed[0][2] = ['Then', 'X', 'Wrong']
    cases.append(('changed grade', changed, False))
    changed = deepcopy(target); changed[0][3][1].append(f(900, 901, 'X'))
    cases.append(('invented factor', changed, False))
    changed = deepcopy(target); changed[0][1] = 'B'
    cases.append(('changed answer', changed, False))
    # The dependency of this event names another event in the same namespace.
    cross = [w(8, [f(1, 2, 'X'), f(2, 3, 'Y')])]
    bad_cross = [w(8, [f(11, 22, 'X'), f(33, 44, 'Y')])]
    results = []
    for name, other, expected in cases:
        witness = occurrence_witness(source, other)
        actual = witness is not None
        assert actual == expected, (name, witness)
        assert check_witness(source, other, witness) == expected, name
        results.append({'name': name, 'accepted': actual, 'witness': witness})
    assert occurrence_witness(cross, bad_cross) is None
    results.append({'name': 'one namespace across factor and dependency', 'accepted': False})
    literal = [w(['Factor', 7, 8, 'Data'], [f(1, 2, 'X')])]
    literal_changed = [w(['Factor', 17, 18, 'Data'], [f(11, 12, 'X')])]
    assert occurrence_witness(literal, literal_changed) is None
    results.append({'name': 'Factor-shaped ordinary data stays literal', 'accepted': False})
    witness = occurrence_witness(source, target)
    assert witness is not None
    corrupt = deepcopy(witness); corrupt['answer_permutation'] = [0, 0, 2]
    assert not check_witness(source, target, corrupt)
    results.append({'name': 'independent verifier rejects reused answer position', 'accepted': False})
    corrupt = deepcopy(witness); corrupt['name_bijection'][0] = (1, 128)
    assert not check_witness(source, target, corrupt)
    results.append({'name': 'independent verifier rejects noninjective names', 'accepted': False})
    lookalike = ['Weighted', 'Literal', 7, ['provenance', [f(7, 8, 9)]]]
    literal_outer = [w(['quote', lookalike], [f(1, 2, 'X')])]
    changed = deepcopy(literal_outer)
    changed[0][1][1][3][1][0][1] = 77
    assert occurrence_witness(literal_outer, changed) is None
    results.append({'name': 'quoted complete Weighted-shaped literal stays exact', 'accepted': False})
    coefficient_outer = [w('Value', [f(1, 2, lookalike)])]
    changed = deepcopy(coefficient_outer)
    changed[0][3][1][0][3][3][1][0][2] = 88
    assert occurrence_witness(coefficient_outer, changed) is None
    results.append({'name': 'Weighted-shaped coefficient stays literal', 'accepted': False})
    nested = [w(lookalike, [f(1, 2, 'X')])]
    changed = deepcopy(nested)
    changed[0][1][3][1][0][1] = 77
    assert occurrence_witness(nested, changed) is None
    schema = ((), (1,))
    nested_witness = occurrence_witness(nested, changed, wrappers=schema)
    assert nested_witness and check_witness(nested, changed, nested_witness, wrappers=schema)
    assert not check_witness(nested, changed, nested_witness)
    results.append({'name': 'nested handler needs explicit independently supplied schema',
                    'accepted': True, 'witness': nested_witness})
    for column in (1, 2):
        invalid = deepcopy(source)
        invalid[0][3][1][0][column] = 0
        try:
            occurrence_witness(source, invalid)
        except ValueError:
            pass
        else:
            raise AssertionError('zero sentinel granted fresh-name authority')
        results.append({'name': 'zero name refused in Factor column ' + str(column), 'accepted': False})
    unordered = occurrence_witness(source, target)
    assert unordered and not check_witness(source, target, unordered, ordered=True)
    forged = deepcopy(unordered); forged['ordered'] = True
    assert not check_witness(source, target, forged, ordered=True)
    results.append({'name': 'caller ordered prefix rejects unordered or relabelled evidence',
                    'accepted': False})
    exact = occurrence_witness(source, source, ordered=True)
    assert exact and check_witness(source, source, exact, ordered=True)
    assert not check_witness(source, source, exact, ordered=False)
    results.append({'name': 'ordered prefix requires agreement with caller order contract',
                    'accepted': True, 'witness': exact})
    return results
