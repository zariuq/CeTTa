"""Terms, the printed answers of a run, and a reference unifier.

A term is one of
  ('s', name)          a symbol
  ('i', value)         an integer
  ('f', value)         a float
  ('v', name)          a variable
  ('e', (t1, ..., tn)) an expression
The reference unifier is the textbook one (Robinson's, with an occurs
check), written apart from CeTTa: a stored row is renamed apart from the
query before every use, and answers are compared as bags modulo a renaming
of each answer's variables.  With PROMOTE (HE), an integer and a float of
one value are equal leaves; otherwise (PeTTa) numbers of different kinds
differ.
"""

import re


def S(name):
    return ('s', name)


def I(value):
    return ('i', value)


def F(value):
    return ('f', float(value))


def V(name):
    return ('v', name)


def E(*items):
    return ('e', tuple(items))


def show(t):
    k = t[0]
    if k == 's':
        return t[1]
    if k == 'i':
        return str(t[1])
    if k == 'f':
        return repr(t[1])
    if k == 'v':
        return '$' + t[1]
    return '(' + ' '.join(show(x) for x in t[1]) + ')'


def variables(t, out=None):
    """Variables of T in order of first occurrence."""
    if out is None:
        out = []
    k = t[0]
    if k == 'v':
        if t[1] not in out:
            out.append(t[1])
    elif k == 'e':
        for x in t[1]:
            variables(x, out)
    return out


def rename(t, suffix):
    k = t[0]
    if k == 'v':
        return ('v', t[1] + suffix)
    if k == 'e':
        return ('e', tuple(rename(x, suffix) for x in t[1]))
    return t


# ---------------------------------------------------------------- unification

def walk(t, sub):
    while t[0] == 'v' and t[1] in sub:
        t = sub[t[1]]
    return t


def occurs(name, t, sub):
    t = walk(t, sub)
    if t[0] == 'v':
        return t[1] == name
    if t[0] == 'e':
        return any(occurs(name, x, sub) for x in t[1])
    return False


def leaves_equal(x, y, promote):
    """Whether two leaves are equal: alike, or, with PROMOTE, an integer and a
    float of one value."""
    if x == y:
        return True
    return (promote and {x[0], y[0]} == {'i', 'f'} and
            float(x[1]) == float(y[1]))


def unify(a, b, sub, promote=False):
    """Extend SUB (a dict, not mutated) to a most general unifier of A and
    B, or None."""
    sub = dict(sub)
    stack = [(a, b)]
    while stack:
        x, y = stack.pop()
        x = walk(x, sub)
        y = walk(y, sub)
        if leaves_equal(x, y, promote):
            continue
        if x[0] == 'v':
            if occurs(x[1], y, sub):
                return None
            sub[x[1]] = y
            continue
        if y[0] == 'v':
            if occurs(y[1], x, sub):
                return None
            sub[y[1]] = x
            continue
        if x[0] == 'e' and y[0] == 'e' and len(x[1]) == len(y[1]):
            stack.extend(zip(x[1], y[1]))
            continue
        return None
    return sub


def resolve(t, sub):
    t = walk(t, sub)
    if t[0] == 'e':
        return ('e', tuple(resolve(x, sub) for x in t[1]))
    return t


# ---------------------------------------------------- alpha-canonical forms

def canonical(t):
    """T with its variables renamed _0, _1, ... by first occurrence."""
    names = {}

    def go(u):
        k = u[0]
        if k == 'v':
            if u[1] not in names:
                names[u[1]] = '_%d' % len(names)
            return ('v', names[u[1]])
        if k == 'e':
            return ('e', tuple(go(x) for x in u[1]))
        return u
    return go(t)


def alpha_eq(a, b):
    return canonical(a) == canonical(b)


def bag(terms):
    """A bag of terms modulo renaming of each term's variables."""
    return sorted(show(canonical(t)) for t in terms)


# ------------------------------------------------------------- output parser

TOKEN = re.compile(r'\s*(?:(\()|(\))|(\[)|(\])|(,)|("(?:[^"\\]|\\.)*")|([^\s()\[\],"]+))')


def tokenize(text):
    pos = 0
    out = []
    text = text.rstrip()
    while pos < len(text):
        m = TOKEN.match(text, pos)
        if not m:
            raise ValueError('cannot tokenize at %r' % text[pos:pos + 30])
        pos = m.end()
        if m.group(1):
            out.append('(')
        elif m.group(2):
            out.append(')')
        elif m.group(3):
            out.append('[')
        elif m.group(4):
            out.append(']')
        elif m.group(5):
            out.append(',')
        elif m.group(6):
            out.append(('str', m.group(6)))
        else:
            out.append(m.group(7))
    return out


def parse_tokens(tokens, i=0):
    tok = tokens[i]
    if tok == '(':
        items = []
        i += 1
        while tokens[i] != ')':
            item, i = parse_tokens(tokens, i)
            items.append(item)
        return ('e', tuple(items)), i + 1
    if isinstance(tok, tuple):
        return ('s', tok[1]), i + 1
    if tok.startswith('$'):
        return ('v', tok[1:]), i + 1
    if re.fullmatch(r'-?\d+', tok):
        return ('i', int(tok)), i + 1
    if re.fullmatch(r'-?\d+\.\d+(?:e[-+]?\d+)?', tok):
        return ('f', float(tok)), i + 1
    return ('s', tok), i + 1


def parse_line(line):
    """One printed result line: '[a, b]' (HE) or 'a' (PeTTa); a list of
    terms."""
    tokens = tokenize(line)
    if not tokens:
        return []
    if tokens[0] == '[':
        out = []
        i = 1
        while tokens[i] != ']':
            if tokens[i] == ',':
                i += 1
                continue
            t, i = parse_tokens(tokens, i)
            out.append(t)
        return out
    t, i = parse_tokens(tokens, 0)
    return [t]
