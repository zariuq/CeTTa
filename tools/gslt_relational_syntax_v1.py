"""Relational syntax for GSLT presentations: a parser written as short
sequential programs and compiled into deterministic relations, for languages
whose parsing a context-free grammar does not state (a notation environment
that the document itself extends, a longest-match lexer).

A parse function reads tokens from a stream, calls other parse functions,
branches on what it read, and answers (Parsed VALUE REST), Eof (the end of
the input, wherever it is met), or (Error E).  Each token read, call and
branch starts a new relation; the stream and the environment are threaded
through.  End of input and errors propagate through every call and read
without further statements, so a function states only its own errors.

Errors are values, so a reader can report them:
  (Error (SyntaxError MESSAGE START END))  a syntax error between two
      positions: START is a stream the function kept, or ItemStart, the
      position the read of the current item began at; END is the stream as
      the error was found;
  (Error (Failure MESSAGE AT))  a failure at a stream's position.
MESSAGE is a list of parts: texts (code point lists) and integers, written
out in order.  A stream is the reader's: tokens put back, then the input,
with whatever the lexer records for positions; a driver turns streams into
positions.

The constructors are named by a vocabulary (a prefix and a suffix): Mg and
V1 make MgParsedV1, MgEofV1, MgErrorV1 and so on.  tools/megalodon_gslt is
the first language written this way.
"""


class Vocabulary:
    """The constructor names of one language's presentations."""

    def __init__(self, prefix, suffix='V1'):
        def name(stem):
            return prefix + stem + suffix
        self.parsed = name('Parsed')
        self.eof = name('Eof')
        self.error = name('Error')
        self.syntax_error = name('SyntaxError')
        self.failure = name('Failure')
        self.item_start = name('ItemStart')
        self.read_token = name('ReadToken')
        self.token_read = name('TokenRead')
        self.tokens_end = name('TokensEnd')
        self.pushed = name('Pushed')
        self.cp_cons = name('CpCons')
        self.cp_nil = name('CpNil')
        self.list_cons = name('ListCons')
        self.list_nil = name('ListNil')

    def ops(self):
        """The constructors the compiled relations use, with their arities."""
        return [(self.parsed, 2), (self.eof, 0), (self.error, 1), (self.syntax_error, 3),
                (self.failure, 2), (self.item_start, 0), (self.read_token, 2), (self.token_read, 2),
                (self.tokens_end, 0), (self.pushed, 2), (self.cp_cons, 2), (self.cp_nil, 0),
                (self.list_cons, 2), (self.list_nil, 0)]


def cp(c):
    return '(cp %d)' % ord(c)


def lit(vocab, s):
    out = vocab.cp_nil
    for ch in reversed(s):
        out = '(%s %s %s)' % (vocab.cp_cons, cp(ch), out)
    return out


def mglist(vocab, items):
    out = vocab.list_nil
    for it in reversed(items):
        out = '(%s %s %s)' % (vocab.list_cons, it, out)
    return out


class Module:
    """One presentation: its signature and its rules, written in order."""

    def __init__(self, name, comment):
        self.name = name
        self.comment = comment
        self.rules = []
        self.ops = {}

    def op(self, name, arity):
        if name in self.ops and self.ops[name] != arity:
            raise SystemExit('arity clash for %s' % name)
        self.ops[name] = arity

    def rule(self, name, head, body=()):
        for r in self.rules:
            if r[0] == name:
                raise SystemExit('duplicate rule %s' % name)
        self.rules.append((name, head, list(body)))

    def text(self):
        out = [self.comment.rstrip()]
        out.append('(gslt-presentation-v1 %s' % self.name)
        out.append('  (signature')
        for n in sorted(self.ops):
            out.append('    (operator %s %d)' % (n, self.ops[n]))
        out[-1] += ')'
        out.append('  (equations)')
        out.append('  (rewrites')
        for name, head, body in self.rules:
            if body:
                out.append('    (rule %s\n      (head %s)\n      (body\n        %s))' % (
                    name, head, '\n        '.join(body)))
            else:
                out.append('    (rule %s\n      (head %s)\n      (body))' % (name, head))
        out[-1] += '))'
        return '\n'.join(out) + '\n'


# ---------------------------------------------------------------------------
# The sequential language

class S:  # statements
    pass


class Read(S):
    """Read the next token into VAR."""

    def __init__(self, var):
        self.var = var


class Expect(S):
    """Read the next token, which must be TOK; any other token is ERROR (a
    Fail), or has no reading when ERROR is None."""

    def __init__(self, tok, error=None):
        self.tok = tok
        self.error = error


class Pure(S):
    def __init__(self, premise, binds):
        self.premise = premise
        self.binds = binds


class Call(S):
    def __init__(self, rel, args, out, binds=None):
        self.rel = rel
        self.args = args
        self.out = out
        self.binds = binds if binds is not None else [out]


class Tail(S):
    def __init__(self, rel, args):
        self.rel = rel
        self.args = args


class Push(S):
    """Put VAR, a token read, back in front of the stream."""

    def __init__(self, var):
        self.var = var


class Keep(S):
    """Bind VAR to the stream as it is now, a position for a later Fail."""

    def __init__(self, var):
        self.var = var


class Ret(S):
    def __init__(self, value):
        self.value = value


class Fail(S):
    """Answer an error: a syntax error from START to here (START None: here
    too; 'item': where the current item's read began; a variable a Keep
    bound), or with failure=True a failure here.  MESSAGE is a list of parts,
    each a literal text or a variable bound to a text or an integer."""

    def __init__(self, message, start=None, failure=False):
        self.message = message if isinstance(message, list) else [message]
        self.start = start
        self.failure = failure


class Case(S):
    def __init__(self, var, branches, default=None, binds=None):
        self.var = var
        self.branches = branches        # list of (pattern, [stmts], [bound vars])
        self.default = default          # [stmts] or None


def B(pattern, stmts, binds=()):
    return (pattern, stmts, list(binds))


def _variables(term):
    out = []
    for word in term.replace('(', ' ').replace(')', ' ').split():
        if word.startswith('?') and word not in out:
            out.append(word)
    return out


def _kept(st, var, stream):
    """ST with the kept stream VAR (a Fail's start, or an argument passed on)
    made STREAM."""
    if isinstance(st, Tail):
        return Tail(st.rel, [stream if a == var else a for a in st.args])
    if isinstance(st, Call):
        return Call(st.rel, [stream if a == var else a for a in st.args], st.out, st.binds)
    if isinstance(st, Fail):
        if st.start == var:
            return Fail(st.message, stream, st.failure)
        return st
    if isinstance(st, Expect) and st.error is not None:
        return Expect(st.tok, _kept(st.error, var, stream))
    if isinstance(st, Case):
        branches = [(p, [_kept(s, var, stream) for s in body], binds) for p, body, binds in st.branches]
        default = None if st.default is None else [_kept(s, var, stream) for s in st.default]
        return Case(st.var, branches, default)
    return st


class Compiler:
    """Compiles parse functions into MODULE.  With ERRORS, end of input's
    companion: errors propagate through every call and read, and Fail and
    Expect's errors can be stated."""

    def __init__(self, module, vocab, errors=True):
        self.m = module
        self.v = vocab
        self.errors = errors
        self.counter = {}
        if errors:
            for name, arity in [(vocab.error, 1), (vocab.syntax_error, 3), (vocab.failure, 2),
                                (vocab.item_start, 0), (vocab.cp_cons, 2), (vocab.list_cons, 2)]:
                module.op(name, arity)

    def fresh(self, base):
        n = self.counter.get(base, 0) + 1
        self.counter[base] = n
        return '%s%d' % (base, n)

    def function(self, name, params, body):
        self.m.op(name, len(params) + 3)
        self.block(name, name, params + ['?toks', '?env'], list(params), '?toks', [], body)

    def message(self, parts):
        return mglist(self.v, [p if p.startswith('?') else lit(self.v, p) for p in parts])

    def error_value(self, st, toks):
        msg = self.message(st.message)
        if st.failure:
            return '(%s (%s %s %s))' % (self.v.error, self.v.failure, msg, toks)
        start = toks if st.start is None else self.v.item_start if st.start == 'item' else st.start
        return '(%s (%s %s %s %s))' % (self.v.error, self.v.syntax_error, msg, start, toks)

    def propagate(self, k, bound):
        """K passes on an error it receives instead of a result."""
        if not self.errors:
            return
        self.m.rule(self.rule_name(k + '-error'),
                    '(%s %s (%s ?e) ?env (%s ?e))' % (k, ' '.join(bound), self.v.error, self.v.error))

    def block(self, rel, rulebase, head_args, bound, toks, premises, stmts):
        """Emit one clause of REL whose head arguments are HEAD_ARGS and whose
    answer is the last argument; STMTS run with BOUND in scope and TOKS as
    the current stream."""
        v = self.v
        stmts = list(stmts)
        while stmts:
            st = stmts.pop(0)
            if isinstance(st, Pure):
                premises.append(st.premise)
                bound = bound + [b for b in st.binds if b not in bound]
            elif isinstance(st, Push):
                toks = '(%s %s %s)' % (v.pushed, st.var, toks)
            elif isinstance(st, Keep):
                # the kept stream is the stream term itself: its variables
                # travel with the bound ones, and the later Fail names it
                bound = bound + [x for x in _variables(toks) if x not in bound]
                stmts = [_kept(s, st.var, toks) for s in stmts]
            elif isinstance(st, Ret):
                head = '(%s %s (%s %s %s))' % (rel, ' '.join(head_args), v.parsed, st.value, toks)
                self.m.rule(self.rule_name(rulebase), head, premises)
                return
            elif isinstance(st, Fail):
                if not self.errors:
                    raise SystemExit('Fail in %s needs a compiler with errors' % rel)
                head = '(%s %s %s)' % (rel, ' '.join(head_args), self.error_value(st, toks))
                self.m.rule(self.rule_name(rulebase), head, premises)
                return
            elif isinstance(st, Tail):
                premises.append('(%s %s %s ?env ?out)' % (st.rel, ' '.join(st.args), toks))
                head = '(%s %s ?out)' % (rel, ' '.join(head_args))
                self.m.rule(self.rule_name(rulebase), head, premises)
                return
            elif isinstance(st, (Read, Expect)):
                k = self.fresh(rel + ('Read' if isinstance(st, Read) else 'Expect'))
                read = self.fresh('?read')
                self.m.op(k, len(bound) + 3)
                premises.append('(%s %s %s)' % (v.read_token, toks, read))
                premises.append('(%s %s %s ?env ?out)' % (k, ' '.join(bound), read))
                head = '(%s %s ?out)' % (rel, ' '.join(head_args))
                self.m.rule(self.rule_name(rulebase), head, premises)
                self.m.rule(self.rule_name(k + '-eof'), '(%s %s %s ?env %s)' % (k, ' '.join(bound), v.tokens_end, v.eof))
                self.propagate(k, bound)
                rest = self.fresh('?rest')
                token = st.var if isinstance(st, Read) else st.tok
                if isinstance(st, Expect) and st.error is not None:
                    if not self.errors:
                        raise SystemExit('Expect with an error in %s needs a compiler with errors' % rel)
                    other, after = self.fresh('?other'), self.fresh('?rest')
                    self.m.rule(self.rule_name(k + '-unexpected'),
                                '(%s %s (%s %s %s) ?env %s)' % (k, ' '.join(bound), v.token_read, other, after,
                                                                 self.error_value(st.error, after)),
                                ['(different %s %s)' % (other, st.tok)])
                self.block(k, k + '-token', bound + ['(%s %s %s)' % (v.token_read, token, rest), '?env'],
                           bound + ([st.var] if isinstance(st, Read) else []), rest, [], stmts)
                return
            elif isinstance(st, Call):
                k = self.fresh(rel + 'After')
                res = self.fresh('?res')
                self.m.op(k, len(bound) + 3)
                premises.append('(%s %s %s ?env %s)' % (st.rel, ' '.join(st.args), toks, res))
                premises.append('(%s %s %s ?env ?out)' % (k, ' '.join(bound), res))
                head = '(%s %s ?out)' % (rel, ' '.join(head_args))
                self.m.rule(self.rule_name(rulebase), head, premises)
                self.m.rule(self.rule_name(k + '-eof'), '(%s %s %s ?env %s)' % (k, ' '.join(bound), v.eof, v.eof))
                self.propagate(k, bound)
                rest = self.fresh('?rest')
                self.block(k, k + '-parsed', bound + ['(%s %s %s)' % (v.parsed, st.out, rest), '?env'],
                           bound + [b for b in st.binds if b not in bound], rest, [], stmts)
                return
            elif isinstance(st, Case):
                if stmts:
                    raise SystemExit('Case must end its block in %s' % rel)
                k = self.fresh(rel + 'Case')
                tv = self.fresh('?toks')
                rest_bound = [b for b in bound if b != st.var]
                self.m.op(k, len(rest_bound) + 4)
                premises.append('(%s %s %s %s ?env ?out)' % (k, st.var, ' '.join(rest_bound), toks))
                head = '(%s %s ?out)' % (rel, ' '.join(head_args))
                self.m.rule(self.rule_name(rulebase), head, premises)
                patterns = []
                for pattern, body, binds in st.branches:
                    patterns.append(pattern)
                    self.block(k, k + '-branch',
                               [pattern] + rest_bound + [tv, '?env'],
                               rest_bound + [b for b in binds if b not in rest_bound], tv, [], body)
                if st.default is not None:
                    for p in patterns:
                        if '?' in p:
                            raise SystemExit('default after a non-ground pattern in %s' % rel)
                    self.block(k, k + '-default', [st.var] + rest_bound + [tv, '?env'],
                               rest_bound + [st.var], tv,
                               ['(different %s %s)' % (st.var, p) for p in patterns], st.default)
                return
            else:
                raise SystemExit('unknown statement %r' % st)
        raise SystemExit('block of %s does not end' % rel)

    def rule_name(self, base):
        n = self.counter.get('rule:' + base, 0) + 1
        self.counter['rule:' + base] = n
        stem = ''.join(('-' + c.lower()) if c.isupper() else c for c in base).strip('-')
        return '%s-%d' % (stem.replace('--', '-'), n)
