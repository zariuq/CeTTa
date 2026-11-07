#!/usr/bin/env python3
"""Write seeded random ground CHR (C0) programs for reference comparison.

Constraints are ranked c0, c1, ...; a rule's body posts only constraints
ranked above every head, so each run terminates. Argument positions are
typed (integer, atom or tuple) so guards and arithmetic never meet a value
of the wrong kind. Programs mix simplification, simpagation and propagation,
repeated and anonymous head variables, literal and tuple patterns, guards,
arithmetic and competing rules, whose outcome depends on the refined rule
and partner order.

    gen_chr_c0_programs.py OUTPUT_DIR [--count N] [--seed S]
"""
import argparse
from pathlib import Path
import random

ATOMS = ['a', 'b', 'c']
VARIABLES = ['X', 'Y', 'Z', 'W', 'V', 'U']


class Program:
    def __init__(self, rng):
        self.rng = rng
        self.ranks = rng.randint(3, 5)
        self.types = [[rng.choice(['int', 'int', 'atom', 'tuple']) for _ in range(rng.randint(0, 3))]
                      for _ in range(self.ranks)]

    def literal(self, kind):
        rng = self.rng
        if kind == 'int':
            return str(rng.randint(-2, 4))
        if kind == 'atom':
            return rng.choice(ATOMS)
        return f'tuple({rng.randint(0, 2)},{rng.choice(ATOMS)})'

    def head(self, rank, bound):
        """A head pattern for constraint RANK; BOUND maps variables to types."""
        rng = self.rng
        arguments = []
        for kind in self.types[rank]:
            roll = rng.random()
            reusable = [name for name, t in bound.items() if t == kind]
            if roll < 0.15:
                arguments.append('_')
            elif roll < 0.35 and reusable:
                arguments.append(rng.choice(reusable))
            elif roll < 0.5:
                arguments.append(self.literal(kind))
            else:
                fresh = [name for name in VARIABLES if name not in bound]
                if not fresh:
                    arguments.append(self.literal(kind))
                    continue
                name = rng.choice(fresh)
                bound[name] = kind
                arguments.append(name)
        return f'c{rank}' + (f'({",".join(arguments)})' if arguments else '')

    def expression(self, ints):
        rng = self.rng
        left = rng.choice(ints) if ints else str(rng.randint(0, 3))
        roll = rng.random()
        if roll < 0.25:
            return left
        if roll < 0.75:
            right = rng.choice(ints + [str(rng.randint(0, 3))])
            return f'{left}{rng.choice(["+", "-", "*"])}{right}'
        return f'{left} {rng.choice(["//", "mod", "rem"])} {rng.randint(1, 3)}'

    def guard(self, bound):
        rng = self.rng
        ints = [name for name, t in bound.items() if t == 'int']
        if ints and rng.random() < 0.7:
            operator = rng.choice(['<', '=<', '>', '>=', '=:=', '=\\='])
            return f'{self.expression(ints)} {operator} {self.expression(ints)}'
        kind = rng.choice(['int', 'atom', 'tuple'])
        names = [name for name, t in bound.items() if t == kind]
        if not names:
            return None
        other = rng.choice(names + [self.literal(kind)])
        operator = rng.choice(['==', '\\=='])
        return f'{rng.choice(names)} {operator} {other}'

    def body(self, low, bound):
        rng = self.rng
        goals, local = [], dict(bound)
        for _ in range(rng.randint(0, 3)):
            if low + 1 >= self.ranks:
                break
            rank = rng.randint(low + 1, self.ranks - 1)
            ints = [name for name, t in local.items() if t == 'int']
            arguments = []
            for kind in self.types[rank]:
                names = [name for name, t in local.items() if t == kind]
                if kind == 'int' and ints and rng.random() < 0.3:
                    fresh = [name for name in VARIABLES if name not in local]
                    if fresh:
                        name = fresh[0]
                        goals.append(f'{name} is {self.expression(ints)}')
                        local[name] = 'int'
                        arguments.append(name)
                        continue
                arguments.append(rng.choice(names) if names and rng.random() < 0.7 else self.literal(kind))
            goals.append(f'c{rank}' + (f'({",".join(arguments)})' if arguments else ''))
        return ', '.join(goals) if goals else 'true'

    def rule(self, index):
        rng = self.rng
        low = rng.randint(0, self.ranks - 2)
        bound = {}
        count = rng.randint(1, 2)
        heads = [self.head(rng.randint(0, low), bound) for _ in range(count)]
        guards = [g for g in (self.guard(bound) for _ in range(rng.randint(0, 2))) if g]
        guard = ', '.join(guards) + ' | ' if guards else ''
        body = self.body(low, bound)
        shape = rng.choice(['simplify', 'simpagate', 'propagate'] if count == 2 else ['simplify', 'propagate'])
        if shape == 'simplify':
            text = f'{", ".join(heads)} <=> {guard}{body}'
        elif shape == 'simpagate':
            text = f'{heads[0]} \\ {heads[1]} <=> {guard}{body}'
        else:
            text = f'{", ".join(heads)} ==> {guard}{body}'
        return f'r{index} @ {text}.'

    def text(self):
        rng = self.rng
        declarations = ','.join(f'c{rank}/{len(types)}' for rank, types in enumerate(self.types))
        rules = [self.rule(index) for index in range(rng.randint(2, 5))]
        query = []
        for _ in range(rng.randint(1, 5)):
            rank = rng.randint(0, min(1, self.ranks - 1))
            arguments = [self.literal(kind) for kind in self.types[rank]]
            query.append(f'c{rank}' + (f'({",".join(arguments)})' if arguments else ''))
        return f':- chr_constraint {declarations}.\n' + '\n'.join(rules) + f'\n?- {", ".join(query)}.\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('output', type=Path)
    parser.add_argument('--count', type=int, default=50)
    parser.add_argument('--seed', type=int, default=0)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    for index in range(args.count):
        rng = random.Random(f'{args.seed}:{index}')
        (args.output / f'generated_{args.seed}_{index:03d}.chr').write_text(Program(rng).text())
    print(f'wrote {args.count} C0 programs (seed {args.seed})')


if __name__ == '__main__':
    main()
