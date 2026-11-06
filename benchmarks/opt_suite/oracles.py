"""Independent Python controls for graph joins and the unary counter workload."""
from pathlib import Path
import re


def edges(path):
    rows = []
    with Path(path).open() as source:
        for line in source:
            match = re.fullmatch(r"\(edge (\d+) (\d+)\)\s*", line)
            if match:
                rows.append(tuple(map(int, match.groups())))
            elif line.startswith("(edge "):
                raise ValueError("graph oracle requires ground nonnegative integer edges")
    return rows


def two_hop_counts(path):
    adjacent = {}
    for x, y in edges(path):
        adjacent.setdefault(x, set()).add(y)
    # Each integer is a destination-set bit vector; rows with no outgoing
    # edges denote the empty set. OR removes duplicate intermediate witnesses.
    bits = {x: sum(1 << y for y in ys) for x, ys in adjacent.items()}
    pairs = triangles = 0
    for x, ys in adjacent.items():
        reachable = 0
        for y in ys:
            row = bits.get(y, 0)
            reachable |= row
            triangles += (bits[x] & row).bit_count()
        pairs += reachable.bit_count()
    return [pairs, triangles]


def two_hop_counts_sparse(path):
    """The same counts with hash sets, for graphs too large for bit vectors."""
    adjacent = {}
    for x, y in edges(path):
        adjacent.setdefault(x, set()).add(y)
    pairs = triangles = 0
    for x, ys in adjacent.items():
        reachable = set()
        for y in ys:
            row = adjacent.get(y, ())
            reachable.update(row)
            triangles += len(ys.intersection(row))
        pairs += len(reachable)
    return [pairs, triangles]


def clique_count(path, size=5):
    """Count cliques in the workload's strictly increasing edge representation."""
    adjacent = {}
    for x, y in edges(path):
        if x >= y:
            raise ValueError("clique oracle requires edges ordered by endpoint")
        adjacent.setdefault(x, set()).add(y)

    def extend(candidates, remaining):
        if remaining == 0:
            return 1
        if len(candidates) < remaining:
            return 0
        return sum(extend(candidates & adjacent.get(x, set()), remaining - 1)
                   for x in candidates)

    return extend(set(adjacent) | set().union(*adjacent.values()), size)


def counter_counts(value):
    if type(value) is not int or value < 0:
        raise ValueError("counter input must be a nonnegative integer")
    # An integer machine, independent of the MeTTa/MM2 term representation.
    program = [('JZ', 2, 5), ('DEC', 2), ('INC', 3), ('INC', 1),
               ('JZ', 0, 0), ('JZ', 1, 9), ('DEC', 1), ('INC', 2),
               ('JZ', 0, 5), ('H',)]
    registers = [0, 0, value, 0, 0]
    pc = steps = 0
    while program[pc][0] != 'H':
        instruction = program[pc]
        if instruction[0] == 'JZ':
            pc = instruction[2] if registers[instruction[1]] == 0 else pc + 1
        else:
            registers[instruction[1]] += 1 if instruction[0] == 'INC' else -1
            pc += 1
        steps += 1
    assert registers == [0, 0, value, value, 0]
    return [6 * (steps + 1), 1]
