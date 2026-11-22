"""Seeded scenarios over stored rows with variables, and the reference
model of each operation.

A scenario is a list of operations over named spaces.  `expected(ops,
dialect)` runs the reference model (two_way_terms.unify, renaming every
stored row apart before every use, two uses of one row in one conjunction
included) and gives, for every tagged operation, the expected bag of answers
in that dialect.  The dialect laws, as each dialect's plain formulation (a
space that reads its rows in turn) answers them:
  * both dialects: unification with an occurs check;
  * PeTTa remove-atom removes every stored row that unifies with the pattern
    and answers true/false; HE remove-atom removes the first row identical to
    the pattern (never the case for a pattern with variables, whose variables
    are the statement's own), else the one alpha-variant row when there is
    exactly one, answering ();
  * a set space keeps one row per alpha-equivalence class;
  * a call no equation head unifies with: HE answers the call itself,
    PeTTa answers nothing;
  * transform (lib_transform): a read answers as match; a take fires per
    proposal made at entry, takes the first stored copy alpha-equivalent to
    the stored row the proposal met, and fires only while such a copy
    remains.

Operations (tuples):
  ('space', name, kind)                 kind: atom | set | strict | self
  ('add', name, row)                    !(add-atom name row)
  ('fact', row)                         a top-level row of &self
  ('eq', fname, lhs, rhs)               a top-level equation
  ('addeq', fname, lhs, rhs)            !(add-atom &self (= lhs rhs))
  ('remove', tag, name, pattern)
  ('match', tag, name, pattern, template)
  ('list', tag, name)
  ('fdef', fname, params, name, pattern, template)
  ('fcall', tag, fname, args)
  ('tread', tag, name, pattern, template)          lib_transform
  ('ttake', tag, name, pattern, template)          lib_transform
  ('tcond', tag, name, mode, pattern, template)    lib_transform: (absent P),
                                                   (unique P)
  ('call', tag, fname, args, template_vars)
"""

import random

from two_way_terms import (E, F, I, S, V, alpha_eq, rename, resolve, show, unify, variables)

HEADS = ('zr', 'zs', 'zt')
CONSTS = ('ka', 'kb', 'kc', 'kd')
FUNCTORS = ('fz', 'gz')
ROW_VARS = ('x', 'y', 'z')
QUERY_VARS = ('a', 'b', 'c')


class Gen:
    def __init__(self, seed, var_rate=0.35, nest_rate=0.18, int_rate=0.12,
                 head_var_rate=0.03):
        self.rng = random.Random(seed)
        self.var_rate = var_rate
        self.nest_rate = nest_rate
        self.int_rate = int_rate
        self.head_var_rate = head_var_rate

    def leaf_const(self):
        if self.rng.random() < self.int_rate:
            return I(self.rng.randrange(3))
        return S(self.rng.choice(CONSTS))

    def arg(self, pool, depth):
        r = self.rng.random()
        if r < self.var_rate:
            return V(self.rng.choice(pool))
        if depth < 2 and r < self.var_rate + self.nest_rate:
            f = self.rng.choice(FUNCTORS)
            n = 1 if f == 'fz' else 2
            return E(S(f), *[self.arg(pool, depth + 1) for _ in range(n)])
        return self.leaf_const()

    def row(self, head=None, arity=None, pool=ROW_VARS):
        head = head or self.rng.choice(('zr', 'zr', 'zr', 'zs', 'zt'))
        arity = arity or self.rng.choice((2, 2, 2, 1, 3))
        args = [self.arg(pool, 0) for _ in range(arity)]
        if self.rng.random() < self.head_var_rate:
            return E(V(pool[0] + 'h'), *args)
        return E(S(head), *args)

    def ground_instance(self, t):
        """T with each variable replaced, consistently, by a constant term."""
        sub = {}
        for v in variables(t):
            sub[v] = (self.leaf_const() if self.rng.random() < 0.8 else
                      E(S('fz'), self.leaf_const()))
        return resolve(t, sub)

    def partial_instance(self, t, pool=QUERY_VARS):
        """T with some variables replaced by constants and the rest renamed
        into the query's names, sometimes keeping the stored row's own names
        so that renaming apart is exercised."""
        sub = {}
        for v in variables(t):
            r = self.rng.random()
            if r < 0.45:
                sub[v] = self.leaf_const()
            elif r < 0.6:
                continue  # keep the stored row's own name
            else:
                sub[v] = V(self.rng.choice(pool))
        return resolve(t, sub)

    def generalize(self, t, pool=QUERY_VARS):
        """T with some leaves (not heads) replaced by query variables."""
        def go(u, head):
            if u[0] == 'e':
                return ('e', tuple(go(x, i == 0) for i, x in enumerate(u[1])))
            if not head and self.rng.random() < 0.35:
                return V(self.rng.choice(pool))
            return u
        return go(t, False)

    def near_miss(self, t):
        """T with one non-head leaf changed."""
        paths = []

        def collect(u, path):
            if u[0] == 'e':
                for i, x in enumerate(u[1]):
                    if i:
                        collect(x, path + (i,))
            else:
                paths.append(path)
        collect(t, ())
        if not paths:
            return t
        path = self.rng.choice(paths)

        def replace(u, path):
            if not path:
                c = self.leaf_const()
                return c if c != u else S('ke')
            items = list(u[1])
            items[path[0]] = replace(items[path[0]], path[1:])
            return ('e', tuple(items))
        return replace(t, path)

    def query(self, rows):
        r = self.rng.random()
        if rows and r < 0.25:
            return self.ground_instance(self.rng.choice(rows))
        if rows and r < 0.45:
            return self.partial_instance(self.rng.choice(rows))
        if rows and r < 0.55:
            return self.generalize(self.ground_instance(self.rng.choice(rows)))
        if rows and r < 0.65:
            return self.near_miss(self.ground_instance(self.rng.choice(rows)))
        if r < 0.72:
            v = V(self.rng.choice(QUERY_VARS))
            return E(S(self.rng.choice(('zr', 'zs'))), v, v)
        if r < 0.76:
            v = V(self.rng.choice(QUERY_VARS))
            return E(S('zr'), E(S('fz'), v), v)
        return self.row(pool=QUERY_VARS)

    def conjunction(self, rows):
        """Two or three patterns that share a variable."""
        first = self.query(rows)
        vs = variables(first)
        shared = V(self.rng.choice(vs)) if vs else V('b')
        second = self.row(pool=QUERY_VARS)
        if second[1][0][0] == 'v':
            second = E(S('zr'), *second[1][1:])
        items = list(second[1])
        if len(items) > 1:
            items[self.rng.randrange(1, len(items))] = shared
        else:
            items.append(shared)
        parts = [first, ('e', tuple(items))]
        if self.rng.random() < 0.25:
            parts.append(self.query(rows))
        return E(S(','), *parts)


def template_for(pattern):
    vs = [v for v in variables(pattern)]
    if not vs:
        return S('yes')
    return E(S('ans'), *[V(v) for v in vs])


# ------------------------------------------------------------ the model

def conjuncts(pattern):
    if pattern[0] == 'e' and pattern[1] and pattern[1][0] == S(','):
        return list(pattern[1][1:])
    return [pattern]


def model_match(rows, pattern, use, promote=False):
    """Substitutions of PATTERN (one pattern or a conjunction) against ROWS,
    each stored row renamed apart for every use.  PROMOTE: HE's numbers,
    where an integer meets the float of its value."""
    subs = [{}]
    for ci, part in enumerate(conjuncts(pattern)):
        nxt = []
        for sub in subs:
            for ri, row in enumerate(rows):
                fresh = rename(row, '@%d_%d_%d' % (use, ci, ri))
                got = unify(part, fresh, sub, promote)
                if got is not None:
                    nxt.append(got)
        subs = nxt
    return subs


def substitute_params(t, params, args):
    return resolve(t, {p: a for p, a in zip(params, args)})


class Model:
    def __init__(self, dialect):
        self.dialect = dialect
        self.promote = dialect == 'he'
        self.spaces = {}
        self.kinds = {}
        self.equations = {}
        self.functions = {}
        self.use = 0

    def space(self, name):
        return self.spaces.setdefault(name, [])

    def add(self, name, row):
        rows = self.space(name)
        if self.kinds.get(name) in ('set', 'setstrict') and any(alpha_eq(r, row) for r in rows):
            return
        rows.append(row)

    def remove(self, name, pattern):
        rows = self.space(name)
        if self.dialect == 'petta':
            self.use += 1
            keep = [r for i, r in enumerate(rows)
                    if unify(pattern, rename(r, '@rm%d_%d' % (self.use, i)), {},
                             self.promote) is None]
            removed = len(keep) != len(rows)
            self.spaces[name] = keep
            return [S('true' if removed else 'false')]
        # HE (space.c space_remove): the first row identical to the pattern;
        # failing that, the one row that is an alpha-variant of it when there
        # is exactly one.  Variables of different top-level statements are
        # different variables whatever their names, so a pattern with
        # variables is never identical to a row added by another statement.
        if not variables(pattern):
            for i, r in enumerate(rows):
                if r == pattern:
                    del rows[i]
                    return [E()]
        alike = [i for i, r in enumerate(rows) if alpha_eq(r, pattern)]
        if len(alike) == 1:
            del rows[alike[0]]
        return [E()]

    def match(self, name, pattern, template):
        self.use += 1
        subs = model_match(self.space(name), pattern, self.use, self.promote)
        return [resolve(template, s) for s in subs]

    def condition(self, name, mode, pattern, template):
        """(absent P) fires once when no row matches P; (unique P) fires
        once, with its one match, when exactly one does.  A conjunction
        counts its joint matches."""
        self.use += 1
        subs = model_match(self.space(name), pattern, self.use, self.promote)
        if mode == 'absent':
            return [template] if not subs else []
        return [resolve(template, subs[0])] if len(subs) == 1 else []

    def take(self, name, pattern, template):
        rows = self.space(name)
        self.use += 1
        proposals = []
        for ri, row in enumerate(rows):
            got = unify(pattern, rename(row, '@tk%d_%d' % (self.use, ri)), {},
                        self.promote)
            if got is not None:
                proposals.append((row, resolve(template, got)))
        answers = []
        for row, answer in proposals:
            for i, r in enumerate(rows):
                if alpha_eq(r, row):
                    del rows[i]
                    answers.append(answer)
                    break
        return answers

    def call(self, fname, args, template_vars):
        eqs = self.equations.get(fname, [])
        call = E(S(fname), *args)
        self.use += 1
        answers = []
        for ei, (lhs, rhs) in enumerate(eqs):
            suffix = '@eq%d_%d' % (self.use, ei)
            got = unify(call, rename(lhs, suffix), {}, self.promote)
            if got is not None:
                answers.append(resolve(
                    E(S('got'), *[V(v) for v in template_vars],
                      rename(rhs, suffix)), got))
        if not answers and self.dialect == 'he':
            answers.append(E(S('got'), *[V(v) for v in template_vars], call))
        return answers


def expected(ops, dialect):
    """Tag -> (kind, expected list of answer terms)."""
    m = Model(dialect)
    out = {}
    for op in ops:
        kind = op[0]
        if kind == 'space':
            m.kinds[op[1]] = op[2]
            m.space(op[1])
        elif kind == 'add':
            m.add(op[1], op[2])
        elif kind == 'fact':
            m.add('&self', op[1])
        elif kind == 'module':
            # &self reads its own rows, then the imported module's
            for row in op[2]:
                m.add('&self', row)
        elif kind in ('eq', 'addeq'):
            m.equations.setdefault(op[1], []).append((op[2], op[3]))
        elif kind == 'remove':
            out[op[1]] = ('remove', m.remove(op[2], op[3]))
        elif kind in ('match', 'tread'):
            out[op[1]] = ('bag', m.match(op[2], op[3], op[4]))
        elif kind == 'list':
            out[op[1]] = ('bag', list(m.space(op[2])))
        elif kind == 'fdef':
            m.functions[op[1]] = op[2:]
        elif kind == 'fcall':
            params, name, pattern, template = m.functions[op[2]]
            args = op[3]
            # the body's own variables are local to each call: rename them
            # apart from the caller's before the parameters are bound
            local = {v: V(v + '@call%d' % op[1]) for v in variables(pattern)
                     if v not in params}
            pattern = resolve(pattern, local)
            template = resolve(template, local)
            out[op[1]] = ('bag', m.match(name,
                                         substitute_params(pattern, params, args),
                                         substitute_params(template, params, args)))
        elif kind == 'ttake':
            out[op[1]] = ('bag', m.take(op[2], op[3], op[4]))
        elif kind == 'tcond':
            out[op[1]] = ('bag', m.condition(op[2], op[3], op[4], op[5]))
        elif kind == 'call':
            out[op[1]] = ('bag', m.call(op[2], op[3], op[4]))
        elif kind == 'addmatch':
            # one statement: the row is added, then read by a pattern that may
            # name the row's own variables; the model renames the row apart.
            m.add(op[2], op[3])
            out[op[1]] = ('bag', m.match(op[2], op[4], op[5]))
        elif kind == 'addcall':
            m.equations.setdefault(op[2], []).append((op[3], op[4]))
            out[op[1]] = ('bag', m.call(op[2], op[5], op[6]))
        elif kind == 'raw':
            pass
        else:
            raise ValueError(kind)
    return out


# ------------------------------------------------------------ program text

def size_read(space, pattern):
    """The number of a read's answers, by the routes that count a match
    without enumerating its answers."""
    return ('(let $__c (collapse (match %s %s yes)) (size-atom $__c))'
            % (space, pattern))


def emptiness_read(space, pattern):
    """(none) if a read has no answer and () otherwise, by HE's empty-only
    case, which observes a match only by its emptiness."""
    return ('(collapse (case (collapse (match %s %s yes)) ((() none))))'
            % (space, pattern))


def program(ops, transform_lib=False, checks=False):
    """The MeTTa text of a scenario (the same text in both dialects).  With
    CHECKS, every match is also counted, (rc TAG N), and observed for
    emptiness, (re TAG E); a read in the statement that adds its row does
    both in that statement, (r TAG (rcx ANSWERS N E))."""
    lines = []
    if transform_lib:
        lines.append('!(import! &self (library lib_transform))')
    for op in ops:
        kind = op[0]
        if kind == 'space':
            name, skind = op[1], op[2]
            if name == '&self':
                continue
            ctor = {'atom': '(new-space)', 'set': '(new-space set)',
                    'strict': '(new-space atom strict)',
                    'setstrict': '(new-space set strict)'}[skind]
            lines.append('!(bind! %s %s)' % (name, ctor))
        elif kind == 'add':
            lines.append('!(add-atom %s %s)' % (op[1], show(op[2])))
        elif kind == 'fact':
            lines.append(show(op[1]))
        elif kind == 'module':
            lines.append('!(import! &self %s)' % op[1])
        elif kind == 'eq':
            lines.append('(= %s %s)' % (show(op[2]), show(op[3])))
        elif kind == 'addeq':
            lines.append('!(add-atom &self (= %s %s))' % (show(op[2]), show(op[3])))
        elif kind == 'remove':
            lines.append('!(r %d (remove-atom %s %s))' % (op[1], op[2], show(op[3])))
        elif kind in ('match', 'list'):
            pattern = show(op[3]) if kind == 'match' else '$all'
            template = show(op[4]) if kind == 'match' else '$all'
            lines.append('!(r %d (collapse (match %s %s %s)))'
                         % (op[1], op[2], pattern, template))
            if checks:
                lines.append('!(rc %d %s)' % (op[1], size_read(op[2], pattern)))
                lines.append('!(re %d %s)' % (op[1], emptiness_read(op[2], pattern)))
        elif kind == 'fdef':
            fname, params, name, pattern, template = op[1:]
            lines.append('(= (%s %s) (collapse (match %s %s %s)))'
                         % (fname, ' '.join('$' + p for p in params), name,
                            show(pattern), show(template)))
        elif kind == 'fcall':
            lines.append('!(r %d (%s %s))'
                         % (op[1], op[2], ' '.join(show(a) for a in op[3])))
        elif kind == 'tread':
            lines.append('!(r %d (collapse (transform %s %s %s)))'
                         % (op[1], op[2], show(op[3]), show(op[4])))
        elif kind == 'ttake':
            lines.append('!(r %d (collapse (transform %s (take %s) %s)))'
                         % (op[1], op[2], show(op[3]), show(op[4])))
        elif kind == 'tcond':
            lines.append('!(r %d (collapse (transform %s (%s %s) %s)))'
                         % (op[1], op[2], op[3], show(op[4]), show(op[5])))
        elif kind == 'raw':
            lines.append(op[1])
        elif kind == 'addmatch':
            read = '(collapse (match %s %s %s))' % (op[2], show(op[4]), show(op[5]))
            if checks:
                read = '(rcx %s %s %s)' % (read, size_read(op[2], show(op[4])),
                                           emptiness_read(op[2], show(op[4])))
            lines.append('!(r %d (let $__ (add-atom %s %s) %s))'
                         % (op[1], op[2], show(op[3]), read))
        elif kind == 'addcall':
            lines.append('!(r %d (let $__ (add-atom &self (= %s %s)) '
                         '(collapse (let $out (%s %s) (got %s $out)))))'
                         % (op[1], show(op[3]), show(op[4]), op[2],
                            ' '.join(show(a) for a in op[5]),
                            ' '.join('$' + v for v in op[6])))
        elif kind == 'call':
            lines.append('!(r %d (collapse (let $out (%s %s) (got %s $out))))'
                         % (op[1], op[2], ' '.join(show(a) for a in op[3]),
                            ' '.join('$' + v for v in op[4])))
        else:
            raise ValueError(kind)
    return '\n'.join(lines) + '\n'


def module_files(ops):
    """The files a scenario imports: name -> text."""
    return {op[1] + '.metta': ''.join(show(row) + '\n' for row in op[2])
            for op in ops if op[0] == 'module'}


# ------------------------------------------------------------ flavours

class Tags:
    def __init__(self):
        self.n = 0

    def __call__(self):
        self.n += 1
        return self.n


def flavour_space(seed, kind, nrows, nops, with_transform=False):
    """Rows in one space of KIND (atom | set | strict | self); queries,
    conjunctions, adds and removes interleaved; a listing after each
    mutation."""
    # A top-level row of &self whose head is a variable is an equation for
    # HE (its head unifies with =), so &self scenarios keep symbol heads.
    g = Gen(seed, head_var_rate=0.0 if kind == 'self' else 0.03)
    tag = Tags()
    name = '&self' if kind == 'self' else '&s'
    ops = [('space', name, kind)]
    rows = []
    for _ in range(nrows):
        row = g.row()
        rows.append(row)
        ops.append(('fact', row) if kind == 'self' else ('add', name, row))
    for _ in range(nops):
        r = g.rng.random()
        if r < 0.45:
            q = g.query(rows)
            if kind == 'self' and q[1][0][0] == 'v':
                q = E(S('zr'), *q[1][1:])
            ops.append(('match', tag(), name, q, template_for(q)))
        elif r < 0.62:
            q = g.conjunction(rows)
            if kind == 'self':
                q = E(S(','), *[E(S('zr'), *p[1][1:]) if p[1][0][0] == 'v' else p
                                for p in q[1][1:]])
            ops.append(('match', tag(), name, q, template_for(q)))
        elif r < 0.72 and kind != 'self':
            row = g.row() if g.rng.random() < 0.6 else rename_pick(g, rows)
            rows.append(row)
            ops.append(('add', name, row))
        elif r < 0.80 and kind != 'self' and rows:
            # remove: an exact copy, a renamed copy, or an instance
            row = g.rng.choice(rows)
            rr = g.rng.random()
            pat = row if rr < 0.5 else (rename_pick(g, [row]) if rr < 0.75
                                        else g.ground_instance(row))
            if pat[0] == 'e' and pat[1][0][0] == 'v':
                pat = row if row[1][0][0] != 'v' else E(S('zr'), S('ka'))
            ops.append(('remove', tag(), name, pat))
            ops.append(('list', tag(), name))
        elif with_transform and r < 0.90:
            q = g.query(rows)
            if q[0] == 'e' and q[1][0][0] == 'v':
                q = E(S('zr'), *q[1][1:])
            ops.append(('tread', tag(), name, q, template_for(q)))
        elif with_transform and kind != 'self':
            q = g.query(rows)
            if q[0] == 'e' and q[1][0][0] == 'v':
                q = E(S('zr'), *q[1][1:])
            ops.append(('ttake', tag(), name, q,
                        E(S('took'), *[V(v) for v in variables(q)])))
            ops.append(('list', tag(), name))
        else:
            q = g.query(rows)
            if kind == 'self' and q[1][0][0] == 'v':
                q = E(S('zr'), *q[1][1:])
            ops.append(('match', tag(), name, q, template_for(q)))
    if kind != 'self':
        ops.append(('list', tag(), name))
    return ops


def rename_pick(g, rows):
    """A stored row with its variables renamed (an alpha-variant)."""
    row = g.rng.choice(rows)
    sub = {v: V(v + 'r') for v in variables(row)}
    return resolve(row, sub)


def flavour_function(seed, kind, nrows, ncalls):
    """Queries made inside function bodies: one body, called with different
    arguments, so that prepared plans are reused across calls."""
    g = Gen(seed, head_var_rate=0.0 if kind == 'self' else 0.03)
    tag = Tags()
    name = '&self' if kind == 'self' else '&s'
    ops = [('space', name, kind)]
    rows = []
    for _ in range(nrows):
        row = g.row()
        rows.append(row)
        ops.append(('fact', row) if kind == 'self' else ('add', name, row))
    for fi in range(3):
        # a pattern with one or two parameter positions
        base = g.query(rows)
        if base[1][0][0] == 'v':
            base = E(S('zr'), *base[1][1:])
        items = list(base[1])
        params = []
        for pi in range(1, len(items)):
            if len(params) < 2 and g.rng.random() < 0.6:
                p = 'p%d' % len(params)
                params.append(p)
                items[pi] = V(p)
        if not params:
            params = ['p0']
            items[-1] = V('p0')
        pattern = ('e', tuple(items))
        if g.rng.random() < 0.5:
            # a conjunction whose second pattern shares a variable with the
            # first: a prepared join reused across calls
            second = g.row(pool=QUERY_VARS)
            if second[1][0][0] == 'v':
                second = E(S('zr'), *second[1][1:])
            sitems = list(second[1])
            shared = V(params[0]) if g.rng.random() < 0.5 else (
                V(variables(pattern)[-1]) if variables(pattern) else V(params[0]))
            if len(sitems) > 1:
                sitems[g.rng.randrange(1, len(sitems))] = shared
            else:
                sitems.append(shared)
            pattern = E(S(','), pattern, ('e', tuple(sitems)))
        fname = 'fm%d' % fi
        ops.append(('fdef', fname, params, name, pattern, template_for(pattern)))
        for _ in range(ncalls):
            args = []
            for p in params:
                rr = g.rng.random()
                if rr < 0.6:
                    args.append(g.leaf_const())
                elif rr < 0.75:
                    args.append(E(S('fz'), g.leaf_const()))
                elif rr < 0.9:
                    args.append(V(g.rng.choice(QUERY_VARS)))
                else:
                    args.append(E(S('fz'), V(g.rng.choice(QUERY_VARS))))
            ops.append(('fcall', tag(), fname, args))
    return ops


def flavour_equations(seed, neqs, ncalls, late_adds):
    """An equation family over one function, schematic and ground heads,
    keyed and unkeyed; calls with ground and open arguments; equations
    added after the first calls have run."""
    g = Gen(seed, var_rate=0.3)
    tag = Tags()
    ops = []
    arity = g.rng.choice((1, 1, 2))
    fname = 'fq'
    heads = []
    for k in range(neqs):
        mode = g.rng.random()
        if mode < 0.55:
            # distinct rigid keys: a constructor per equation
            args = [E(S('c%d' % k), *[g.arg(ROW_VARS, 1) for _ in range(g.rng.choice((0, 1, 2)))])
                    if g.rng.random() < 0.7 else S('k%d' % k)]
            args += [g.arg(ROW_VARS, 0) for _ in range(arity - 1)]
        else:
            args = [g.arg(ROW_VARS, 0) for _ in range(arity)]
        lhs = E(S(fname), *args)
        rhs = E(S('res'), I(k), *[V(v) for v in variables(lhs)])
        heads.append(lhs)
        ops.append(('eq', fname, lhs, rhs))
    added = 0
    for ci in range(ncalls):
        if late_adds and ci == ncalls // 2:
            for k in range(late_adds):
                if g.rng.random() < 0.5:
                    args = [V('x')] + [g.arg(ROW_VARS, 0) for _ in range(arity - 1)]
                else:
                    args = [g.arg(ROW_VARS, 0) for _ in range(arity)]
                lhs = E(S(fname), *args)
                rhs = E(S('res'), I(1000 + k), *[V(v) for v in variables(lhs)])
                heads.append(lhs)
                ops.append(('addeq', fname, lhs, rhs))
                added += 1
        r = g.rng.random()
        h = g.rng.choice(heads)
        if r < 0.35:
            call = g.ground_instance(h)
        elif r < 0.6:
            call = g.partial_instance(h)
        elif r < 0.7:
            call = g.near_miss(g.ground_instance(h))
        elif r < 0.8:
            call = E(S(fname), *[V(QUERY_VARS[i]) for i in range(arity)])
        else:
            call = E(S(fname), *[g.arg(QUERY_VARS, 0) for _ in range(arity)])
        args = list(call[1][1:])
        ops.append(('call', tag(), fname, args, variables(call)))
    return ops


def flavour_dense(seed, nfacts, nschematic, nops):
    """More ground facts of one head than the size at which an atom space
    keeps them as a relation, with schematic rows of the same head."""
    g = Gen(seed)
    tag = Tags()
    ops = [('space', '&s', 'atom')]
    rows = []
    for i in range(nfacts):
        row = E(S('zr'), S('n%d' % (i % 997)), I(i))
        rows.append(row)
        ops.append(('add', '&s', row))
    for _ in range(nschematic):
        rr = g.rng.random()
        if rr < 0.4:
            row = E(S('zr'), V('x'), I(g.rng.randrange(nfacts)))
        elif rr < 0.7:
            row = E(S('zr'), S('n%d' % g.rng.randrange(997)), V('y'))
        elif rr < 0.85:
            row = E(S('zr'), V('z'), V('z'))
        else:
            row = E(V('h'), S('n%d' % g.rng.randrange(997)), V('w'))
        rows.append(row)
        ops.append(('add', '&s', row))
    for _ in range(nops):
        rr = g.rng.random()
        i = g.rng.randrange(nfacts + 50)
        if rr < 0.4:
            q = E(S('zr'), S('n%d' % (i % 997)), I(i))
        elif rr < 0.6:
            q = E(S('zr'), V('a'), I(i))
        elif rr < 0.75:
            q = E(S('zr'), S('n%d' % (i % 997)), V('b'))
        elif rr < 0.85:
            q = E(S('zr'), V('c'), V('c'))
        elif rr < 0.92:
            pat = E(S('zr'), S('n%d' % (i % 997)), I(i))
            ops.append(('remove', tag(), '&s', pat))
            continue
        else:
            row = E(S('zr'), V('x'), I(i))
            rows.append(row)
            ops.append(('add', '&s', row))
            continue
        ops.append(('match', tag(), '&s', q, template_for(q)))
    return ops


def plain_formulation(ops):
    """The same scenario over a space that reads its rows in turn: a strict
    space of the same kind; top-level facts of &self become rows of a strict
    space &p, and every read of &self reads &p."""
    out = []
    for op in ops:
        if op[0] == 'space':
            if op[2] == 'self':
                out.append(('space', '&p', 'strict'))
            else:
                out.append(('space', op[1], {'atom': 'strict',
                                             'set': 'setstrict'}.get(op[2], op[2])))
        elif op[0] == 'fact':
            out.append(('add', '&p', op[1]))
        elif op[0] == 'module':
            out.extend(('add', '&p', row) for row in op[2])
        elif op[0] == 'addmatch':
            out.append(('addmatch', op[1], '&p' if op[2] == '&self' else op[2]) + tuple(op[3:]))
            continue
        elif op[0] in ('match', 'tread', 'ttake', 'tcond', 'list', 'remove') and op[2] == '&self':
            out.append(op[:2] + ('&p',) + op[3:])
        elif op[0] == 'fdef' and op[3] == '&self':
            out.append(op[:3] + ('&p',) + op[4:])
        else:
            out.append(op)
    return out


def equation_plain_formulation(ops):
    """Equation dispatch as a match of (= CALL $out) over a strict space
    holding the equations as rows."""
    out = [('space', '&q', 'strict')]
    for op in ops:
        if op[0] in ('eq', 'addeq'):
            out.append(('add', '&q', E(S('='), op[2], op[3])))
        elif op[0] == 'call':
            tag, fname, args, tvars = op[1:]
            pattern = E(S('='), E(S(fname), *args), V('out'))
            out.append(('match', tag, '&q', pattern,
                        E(S('got'), *[V(v) for v in tvars], V('out'))))
        elif op[0] == 'addcall':
            tag, fname, lhs, rhs, args, tvars = op[1:]
            pattern = E(S('='), E(S(fname), *args), V('out'))
            out.append(('addmatch', tag, '&q', E(S('='), lhs, rhs), pattern,
                        E(S('got'), *[V(v) for v in tvars], V('out'))))
        else:
            out.append(op)
    return out


def flavour_memo(seed, neqs, ncalls, arity):
    """A memoized function (PeTTa lib_memo) over schematic and ground
    equation heads.  Every call is made twice, the second time with its
    variables renamed (a variant: the same memo key), and calls whose
    arguments repeat a variable alternate with calls of the same arguments
    with distinct variables (different keys)."""
    g = Gen(seed, var_rate=0.35)
    tag = Tags()
    fname = 'fm'
    ops = [('raw', '!(import! &self (library lib_memo))'), ('raw', '!(memoize fm)')]
    heads = []
    for k in range(neqs):
        args = [g.arg(ROW_VARS, 0) for _ in range(arity)]
        lhs = E(S(fname), *args)
        rhs = E(S('res'), I(k), *[V(v) for v in variables(lhs)])
        heads.append(lhs)
        ops.append(('eq', fname, lhs, rhs))
    for ci in range(ncalls):
        r = g.rng.random()
        if r < 0.3:
            call_args = [V('a')] * arity
        elif r < 0.5:
            call_args = [V(QUERY_VARS[i % 3]) for i in range(arity)]
        elif r < 0.75:
            call_args = list(g.partial_instance(g.rng.choice(heads))[1][1:])
        else:
            call_args = [g.arg(QUERY_VARS, 0) for _ in range(arity)]
        call = E(S(fname), *call_args)
        ops.append(('call', tag(), fname, call_args, variables(call)))
        variant = resolve(call, {v: V(v + 'v') for v in variables(call)})
        vargs = list(variant[1][1:])
        ops.append(('call', tag(), fname, vargs, variables(variant)))
        if arity > 1 and variables(call):
            # the other shape: repeated <-> distinct variables
            vs = variables(call)
            if len(vs) == 1:
                other = [V('p%d' % i) if a == V(vs[0]) else a for i, a in enumerate(call_args)]
            else:
                other = [V('q') if a[0] == 'v' else a for a in call_args]
            ops.append(('call', tag(), fname, other, variables(E(S(fname), *other))))
    return ops


# ------------------------------------------------------------ collisions
# A read in the statement that added its row: the pattern may carry the new
# row's own variables (one scope), which every reader must still keep apart
# from the row's renaming for that use.

def own_names_instance(g, row, pool=QUERY_VARS):
    """A query position-wise from ROW: a row variable becomes a constant, the
    same variable, a nested term holding the row's own variable, a nested
    constant or a query variable; other leaves stay or are generalized."""
    own = list(variables(row))
    def arg(u):
        if u[0] == 'v':
            r = g.rng.random()
            if r < 0.25:
                return g.leaf_const()
            if r < 0.45:
                return u
            if r < 0.70 and own:
                return E(S('fz'), V(g.rng.choice(own)))
            if r < 0.85:
                return E(S('fz'), g.leaf_const())
            return V(g.rng.choice(pool))
        if g.rng.random() < 0.2:
            return V(g.rng.choice(pool))
        return u
    return E(row[1][0], *[arg(u) for u in row[1][1:]])


def flavour_collide(seed, nrows, nops):
    """Rows of an atom space past the substitution tree's rent, then reads in
    the statements that add their rows, alone and in conjunctions."""
    g = Gen(seed, head_var_rate=0.0)
    tag = Tags()
    name = '&s'
    ops = [('space', name, 'atom')]
    rows = []
    for _ in range(nrows):
        row = g.row(head='zr', arity=g.rng.choice((2, 3)))
        rows.append(row)
        ops.append(('add', name, row))
    for arity in (2, 3) * 6:
        q = E(S('zr'), *[V(v) for v in ('a', 'b', 'c')[:arity]])
        ops.append(('match', tag(), name, q, template_for(q)))
    for _ in range(nops):
        new = g.row(head='zr', arity=g.rng.choice((2, 3)))
        r = g.rng.random()
        if r < 0.65:
            q = own_names_instance(g, new)
        elif r < 0.9:
            q = E(S(','), own_names_instance(g, new), g.query(rows))
        else:
            q = g.query(rows + [new])
        rows.append(new)
        ops.append(('addmatch', tag(), name, new, q, template_for(q)))
    ops.append(('list', tag(), name))
    return ops


def flavour_module(seed, nrows, nmodule, nops):
    """&self with rows of its own and rows of an imported module, read by
    match and by transform, and tested by transform's absent and unique: HE
    reads the space's own rows, then the module's own rows.  A query is one
    pattern or a conjunction.  Each scenario imports a module of its own
    name."""
    g = Gen(seed, head_var_rate=0.0)
    tag = Tags()
    own = [g.row() for _ in range(nrows)]
    imported = [g.row() for _ in range(nmodule)]
    rows = own + imported
    ops = [('space', '&self', 'self'),
           ('module', 'twmod%d' % seed, imported)]
    for row in own:
        ops.append(('fact', row))

    def headed(q):
        return E(S('zr'), *q[1][1:]) if q[1][0][0] == 'v' else q

    for _ in range(nops):
        if g.rng.random() < 0.3:
            q = g.conjunction(rows)
            q = E(S(','), *[headed(part) for part in q[1][1:]])
        else:
            q = headed(g.query(rows))
        r = g.rng.random()
        if r < 0.15:
            ops.append(('tcond', tag(), '&self', 'absent', q, S('nothere')))
        elif r < 0.3:
            ops.append(('tcond', tag(), '&self', 'unique', q, template_for(q)))
        else:
            kind = 'tread' if r < 0.72 else 'match'
            ops.append((kind, tag(), '&self', q, template_for(q)))
    return ops


def flavour_promote(seed, nrows, nops):
    """Rows and queries holding integers and floats of one value, past the
    sizes at which a space reads its indexes: single reads, ground reads (the
    exact index), and joins of two and three patterns through a number (the
    counting routes).  HE's matching promotes 2 to meet 2.0; PeTTa's keeps the
    kinds apart."""
    rng = random.Random(seed)
    numbers = [I(1), F(1.0), I(2), F(2.0), I(3), F(3.0), I(7)]
    symbols = [S('ka'), S('kb'), S('kc')]
    tag = Tags()
    ops = [('space', '&s', 'atom')]
    for _ in range(nrows):
        r = rng.random()
        if r < 0.4:
            row = E(S('pv'), rng.choice(numbers + symbols), rng.choice(numbers))
        elif r < 0.6:
            row = E(S('link'), rng.choice(symbols), rng.choice(numbers))
        elif r < 0.8:
            row = E(S('right'), rng.choice(numbers), rng.choice(symbols))
        else:
            row = E(S('tail'), rng.choice(symbols), S('e'))
        ops.append(('add', '&s', row))
    for _ in range(nops):
        r = rng.random()
        if r < 0.3:
            q = E(S('pv'), rng.choice(numbers + symbols), rng.choice(numbers))
        elif r < 0.5:
            q = E(S('pv'), V('a'), rng.choice(numbers))
        elif r < 0.6:
            q = E(S('right'), rng.choice(numbers), V('b'))
        elif r < 0.8:
            q = E(S(','), E(S('link'), V('x'), V('y')),
                  E(S('right'), V('y'), V('z')))
        else:
            q = E(S(','), E(S('link'), V('x'), V('y')),
                  E(S('right'), V('y'), V('z')), E(S('tail'), V('z'), V('w')))
        ops.append(('match', tag(), '&s', q, template_for(q)))
    ops.append(('list', tag(), '&s'))
    return ops


def flavour_collide_equations(seed, neqs, ncalls):
    """Equations of one head called with an open first argument (the call
    shape the equation index's substitution tree serves), and equations
    added in the statement that calls them with their own variables."""
    g = Gen(seed, head_var_rate=0.0)
    tag = Tags()
    ops = []
    for i in range(neqs):
        ops.append(('eq', 'fq', E(S('fq'), E(S('c%d' % i), V('x')), V('y')),
                    E(S('res'), I(i), V('x'), V('y'))))
    for _ in range(6):
        ops.append(('call', tag(), 'fq', [V('a'), E(S('d1'), S('kb'))], ['a']))
    for k in range(ncalls):
        body = g.row(head='h2', arity=g.rng.choice((2, 3)))
        lhs = E(S('fq'), V('u'), body)
        arg = own_names_instance(g, body)
        ops.append(('addcall', tag(), 'fq', lhs, S('ok%d' % k), [V('o'), arg], ['o']))
    return ops
