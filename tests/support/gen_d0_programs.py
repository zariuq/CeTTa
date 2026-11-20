#!/usr/bin/env python3
"""Write seeded random Souffle-syntax Datalog (D0) programs.

The programs exercise the source reader and the static checks: every
literal spelling (decimal, binary, hexadecimal, negative, unsigned, symbols
with escapes and non-ASCII text), comments and layout, several relations in
one declaration, inputs and outputs, comparisons, anonymous arguments, and
some statements that are ill typed, not range restricted or out of the
64-bit domain, so refusals are exercised too.

    gen_d0_programs.py OUTPUT_DIR [--count N] [--seed S]
"""
import argparse
from pathlib import Path
import random

ESCAPES = ['\\t', '\\n', '\\"', '\\\\', "\\'", '\\r', '\\b', '\\f', '\\a', '\\v']
WORDS = ['a', 'b', 'node', 'x_1', 'Été', 'λ', '漢字', 'with space']
TYPES = ['number', 'number', 'unsigned', 'symbol']
COMPARISONS = ['=', '!=', '<', '<=', '>', '>=']


class Program:
    def __init__(self, rng):
        self.rng = rng
        count = rng.randint(2, 5)
        self.relations = [(f'r{i}', [rng.choice(TYPES) for _ in range(rng.randint(1, 3))]) for i in range(count)]

    def layout(self):
        rng = self.rng
        roll = rng.random()
        if roll < 0.6:
            return ' '
        if roll < 0.75:
            return '  '
        if roll < 0.85:
            return ' /* note */ '
        return '\n'

    def number(self):
        rng = self.rng
        roll = rng.random()
        value = rng.choice([0, 1, 7, 42, 255, 2**31, 2**62, 2**63 - 1])
        if roll < 0.04:
            return str(2**63)                       # outside the domain
        if roll < 0.08:
            return '-' + str(2**63)                 # the least number
        if roll < 0.3:
            return '0x' + format(value, 'x')
        if roll < 0.45:
            return '0b' + format(value % 64, 'b')
        if roll < 0.65:
            return '-' + str(value)
        if roll < 0.75:
            return '-0x' + format(value, 'X')
        return str(value)

    def unsigned(self):
        rng = self.rng
        value = rng.choice([0, 3, 2**32, 2**64 - 1])
        roll = rng.random()
        if roll < 0.04:
            return str(2**64) + 'u'                 # outside the domain
        if roll < 0.3:
            return '0x' + format(value, 'x') + 'u'
        if roll < 0.45:
            return '0b' + format(value % 32, 'b') + 'u'
        return str(value) + 'u'

    def symbol(self):
        rng = self.rng
        parts = []
        for _ in range(rng.randint(0, 3)):
            parts.append(rng.choice(ESCAPES) if rng.random() < 0.35 else rng.choice(WORDS))
        return '"' + ''.join(parts) + '"'

    def literal(self, kind):
        rng = self.rng
        if rng.random() < 0.05:                     # a literal of another type
            kind = rng.choice(TYPES)
        return {'number': self.number, 'unsigned': self.unsigned, 'symbol': self.symbol}[kind]()

    def declaration(self, names):
        rng = self.rng
        name, types = names[0], [t for _, t in names][0]
        fields = ', '.join(f'f{i}:{t}' for i, t in enumerate(types))
        if rng.random() < 0.03:
            fields = 'f0:float'
        relations = (',' + self.layout()).join(n for n, _ in names)
        # A directive is followed by horizontal space; layout may follow that.
        after = ' ' + (self.layout() if rng.random() < 0.3 else '')
        return f'.decl{after}{relations}({fields})'

    def rule(self):
        rng = self.rng
        head_name, head_types = rng.choice(self.relations)
        body, bound = [], {}
        for _ in range(rng.randint(1, 3)):
            name, types = rng.choice(self.relations)
            arguments = []
            for t in types:
                roll = rng.random()
                if roll < 0.12:
                    arguments.append('_')
                elif roll < 0.25:
                    arguments.append(self.literal(t))
                else:
                    variable = rng.choice(['x', 'y', 'z', 'w'])
                    bound.setdefault(variable, t)
                    arguments.append(variable)
            body.append(f'{name}({", ".join(arguments)})')
        variables = list(bound)
        for _ in range(rng.randint(0, 2)):
            if variables:
                left = rng.choice(variables)
                same = [v for v in variables if bound[v] == bound[left]]
                right = rng.choice(same) if rng.random() < 0.7 else self.literal(bound[left])
                body.append(f'{left} {rng.choice(COMPARISONS)} {right}')
        head = []
        for t in head_types:
            roll = rng.random()
            if roll < 0.75 and variables:
                head.append(rng.choice(variables))
            elif roll < 0.85:
                head.append('_')                    # not range restricted
            else:
                head.append(self.literal(t))
        separator = ',' + self.layout()
        return f'{head_name}({", ".join(head)}) :-{self.layout()}{separator.join(body)}.'

    def text(self):
        rng = self.rng
        lines = ['// generated D0 program']
        index = 0
        while index < len(self.relations):
            take = 2 if rng.random() < 0.2 and index + 1 < len(self.relations) and \
                self.relations[index][1] == self.relations[index + 1][1] else 1
            lines.append(self.declaration(self.relations[index:index + take]))
            index += take
        for name, _ in self.relations:
            if rng.random() < 0.4:
                lines.append(f'.input {name}')
            if rng.random() < 0.5:
                lines.append(f'.output {name}')
        if rng.random() < 0.05:
            lines.append('.output undeclared')
        for _ in range(rng.randint(0, 4)):
            name, types = rng.choice(self.relations)
            lines.append(f'{name}({", ".join(self.literal(t) for t in types)}).')
        for _ in range(rng.randint(1, 4)):
            lines.append(self.rule())
        return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('output', type=Path)
    parser.add_argument('--count', type=int, default=50)
    parser.add_argument('--seed', type=int, default=0)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    for index in range(args.count):
        rng = random.Random(f'{args.seed}:{index}')
        (args.output / f'generated_{args.seed}_{index:03d}.dl').write_text(Program(rng).text(), encoding='utf-8')
    print(f'wrote {args.count} D0 programs (seed {args.seed})')


if __name__ == '__main__':
    main()
