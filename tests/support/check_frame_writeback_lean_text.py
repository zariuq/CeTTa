#!/usr/bin/env python3
"""The Lean writeback bridge (CeTTaFrameWriteback.lean) quotes the runtime's
C text and proves that the text parses to the statements it models.  A
quoted text that drifts from the runtime leaves those theorems about code
the runtime no longer runs.

This guard reads every `source` and `*Source : String` the bridge defines,
joining the piece lists they are built from, and compares each with the C
file the manifest names for it, as C tokens (layout and comments aside;
preprocessor lines are tokens, so a text cannot match across one).  Every
source must be listed, with one of:

  file   PATH    the text's tokens occur contiguously in PATH;
  block  PATH    the text is `{ S }` for statements S that close a block in
                 PATH (the bridge models statements selected from a body);
  absent REASON  the text describes code this tree does not have.

A source missing from the manifest, a row the bridge no longer defines, and
a text its file lacks all fail.

After a passing check, the guard runs its controls on altered copies of the
texts: a relaid file still passes; a changed token, a statement after a
selected block, a directive inside a quoted body, a source missing from the
manifest and a row the bridge lacks each fail.

usage: check_frame_writeback_lean_text.py BRIDGE.lean MANIFEST.tsv ROOT"""
import re
import sys
from pathlib import Path


def lean_string(literal):
    """The value of a Lean string literal's body (the text between quotes)."""
    escapes = {'n': '\n', 't': '\t', 'r': '\r', '"': '"', '\\': '\\', "'": "'"}
    out, i = [], 0
    while i < len(literal):
        c = literal[i]
        if c == '\\':
            nxt = literal[i + 1]
            if nxt not in escapes:
                raise ValueError(f'unsupported escape \\{nxt}')
            out.append(escapes[nxt])
            i += 2
            continue
        out.append(c)
        i += 1
    return ''.join(out)


STRING = r'"((?:[^"\\]|\\.)*)"'
NAME = r"[A-Za-z_][\w'.]*"
DECL = re.compile(r'^(?:@\[[^\]]*\]\s*)?(?:(?:private|protected|noncomputable)\s+)*'
                  r'(?:def|abbrev)\s+(' + NAME + r')\s*:(.*)', re.S)
PIECE = re.compile(r'String\s*×\s*List\s+Token\s*:=\s*\(\s*' + STRING + r'\s*,', re.S)
LIST_TYPE = re.compile(r'List\s*\(\s*String\s*×\s*List\s+Token\s*\)\s*:=\s*(.*)', re.S)
SOURCE_JOIN = re.compile(r'String\s*:=\s*String\.join\s*\(\s*(' + NAME +
                         r')\.map\s+Prod\.fst\s*\)\s*$', re.S)
SOURCE_LITERAL = re.compile(r'String\s*:=\s*' + STRING + r'\s*$', re.S)


def strip_comments(text):
    """The text with Lean comments blanked, string literals kept."""
    out, i, depth = [], 0, 0
    while i < len(text):
        if depth:
            if text.startswith('/-', i):
                depth += 1
                i += 2
            elif text.startswith('-/', i):
                depth -= 1
                i += 2
            else:
                out.append('\n' if text[i] == '\n' else ' ')
                i += 1
            continue
        if text.startswith('/-', i):
            depth = 1
            i += 2
            continue
        if text.startswith('--', i):
            while i < len(text) and text[i] != '\n':
                i += 1
            continue
        if text[i] == '"':
            m = re.compile(STRING).match(text, i)
            out.append(m.group(0))
            i = m.end()
            continue
        out.append(text[i])
        i += 1
    return ''.join(out)


def commands(text):
    """Top-level commands: each starts at column 0, continuations are indented."""
    current = []
    for line in strip_comments(text).split('\n'):
        if line[:1] and not line[:1].isspace() and current:
            yield '\n'.join(current)
            current = []
        if line.strip() or current:
            current.append(line)
    if current:
        yield '\n'.join(current)


class Bridge:
    """The bridge's quoted texts, named as Lean names them."""

    def __init__(self, text):
        self.scopes = []          # (kind, components, opened)
        self.known = set()        # namespaces seen so far
        self.pieces = {}
        self.lists = {}
        self.sources = []
        for command in commands(text):
            self.command(command.strip())

    def namespace(self):
        return [c for _, comps, _ in self.scopes for c in comps]

    def candidates(self, name):
        """Lean's resolution order: enclosing namespaces innermost first, then
        the namespaces opened in scope."""
        ns = self.namespace()
        for k in range(len(ns), -1, -1):
            yield '.'.join(ns[:k] + [name])
        for _, _, opened in reversed(self.scopes):
            for o in opened:
                yield o + '.' + name

    def resolve(self, table, name):
        for full in self.candidates(name):
            if full in table:
                return table[full]
        raise KeyError(f'{".".join(self.namespace())}: unresolved {name}')

    def list_value(self, expression):
        expression = expression.strip()
        if expression.startswith('['):
            if not expression.endswith(']'):
                raise ValueError(f'unsupported piece list: {expression[:80]}')
            names = [n.strip() for n in expression[1:-1].split(',') if n.strip()]
            return ''.join(self.resolve(self.pieces, n) for n in names)
        parts = [p.strip() for p in expression.split('::')]
        if len(parts) > 1 and all(re.fullmatch(NAME, p) for p in parts):
            return ''.join(self.resolve(self.pieces, p) for p in parts[:-1]) + \
                self.resolve(self.lists, parts[-1])
        if re.fullmatch(NAME, expression):
            return self.resolve(self.lists, expression)
        raise ValueError(f'unsupported piece list: {expression[:80]}')

    def command(self, command):
        head = command.split()
        if head[0] == 'namespace':
            self.scopes.append(('namespace', head[1].split('.'), []))
            ns = self.namespace()
            self.known.update('.'.join(ns[:k]) for k in range(1, len(ns) + 1))
        elif head[0] == 'section':
            self.scopes.append(('section', [], []))
        elif head[0] == 'end':
            self.scopes.pop()
        elif head[0] == 'open':
            # `open X` names X relative to the enclosing namespaces first.
            ns = self.namespace()
            for name in (n for n in head[1:] if re.fullmatch(NAME, n)):
                opened = next((full for full in ('.'.join(ns[:k] + [name])
                                                 for k in range(len(ns), -1, -1))
                               if full in self.known), name)
                if self.scopes:
                    self.scopes[-1][2].append(opened)
        m = DECL.match(command)
        if not m:
            return
        full = '.'.join(self.namespace() + [m.group(1)])
        signature = m.group(2)
        piece = PIECE.match(signature.lstrip())
        if piece:
            self.pieces[full] = lean_string(piece.group(1))
            return
        pieces = LIST_TYPE.match(signature.lstrip())
        if pieces:
            self.lists[full] = self.list_value(pieces.group(1))
            return
        if not (m.group(1) == 'source' or m.group(1).endswith('Source')):
            return
        joined = SOURCE_JOIN.match(signature.lstrip())
        literal = SOURCE_LITERAL.match(signature.lstrip())
        if joined:
            self.sources.append((full, self.resolve(self.lists, joined.group(1))))
        elif literal:
            self.sources.append((full, lean_string(literal.group(1))))
        elif re.match(r'String\s*:=', signature.lstrip()):
            raise ValueError(f'{full}: unsupported source form')


PREFIX = 'Mettapedia.Languages.MeTTa.Bridges.GSLT.CeTTaFrameWriteback.'


C_TOKEN = re.compile(r"""
    (?P<space>\s+)
  | (?P<comment>/\*.*?\*/|//[^\n]*)
  | (?P<string>"(?:[^"\\\n]|\\.)*"|'(?:[^'\\\n]|\\.)*')
  | (?P<number>\.?\d(?:[eEpP][+-]|[\w.])*)
  | (?P<name>[A-Za-z_]\w*)
  | (?P<punct>>>=|<<=|\.\.\.|->|\+\+|--|<<|>>|<=|>=|==|!=|&&|\|\||[-+*/%&^|]=|\#\#|[^\s\w])
""", re.S | re.X)


DIRECTIVE = re.compile(r'#(?:[^\n\\]|\\.)*', re.S)


def c_tokens(text):
    """C tokens, layout and comments dropped; a directive is one token."""
    out, i = [], 0
    while i < len(text):
        if text[i] == '#' and not text[text.rfind('\n', 0, i) + 1:i].strip():
            m = DIRECTIVE.match(text, i)
            out.append(' '.join(m.group().replace('\\\n', ' ').split()))
            i = m.end()
            continue
        m = C_TOKEN.match(text, i)
        if not m:
            raise ValueError(f'cannot read C text at {text[i:i + 40]!r}')
        if m.lastgroup not in ('space', 'comment'):
            out.append(m.group())
        i = m.end()
    return out


def occurs(needle, haystack):
    first = needle[0]
    return any(haystack[k:k + len(needle)] == needle
               for k, token in enumerate(haystack) if token == first)


def manifest(path):
    rows = {}
    for number, line in enumerate(Path(path).read_text().splitlines(), 1):
        if not line.strip() or line.startswith('#'):
            continue
        fields = line.split('\t')
        if len(fields) != 3 or fields[1] not in ('file', 'block', 'absent') or not fields[2]:
            sys.exit(f'FAIL: {path}:{number}: expected NAME<TAB>file|block|absent<TAB>PATH|REASON')
        if fields[0] in rows:
            sys.exit(f'FAIL: {path}:{number}: {fields[0]} listed twice')
        rows[fields[0]] = (fields[1], fields[2])
    return rows


def check(quoted, rows, read):
    """Failures, then the checked and absent counts, reading C files by `read`."""
    failures, checked, absent, files = [], 0, 0, {}
    for name, text in quoted:
        if name not in rows:
            failures.append(f'{name}: not in the manifest')
            continue
        kind, where = rows[name]
        if kind == 'absent':
            absent += 1
            continue
        if where not in files:
            files[where] = c_tokens(read(where))
        needle = c_tokens(text)
        if kind == 'block':
            if needle[:1] != ['{'] or needle[-1:] != ['}']:
                failures.append(f'{name}: a block row needs a braced text')
                continue
            needle = needle[1:]
        if occurs(needle, files[where]):
            checked += 1
        else:
            failures.append(f'{name}: {where} no longer has the quoted text')
    names = {name for name, _ in quoted}
    failures += [f'{name}: listed, but the bridge no longer defines it'
                 for name in rows if name not in names]
    return failures, checked, absent


def controls(quoted, rows, read):
    """Each control alters one input and states whether the check must pass."""
    by_kind = {}
    for name, (kind, where) in rows.items():
        by_kind.setdefault(kind, []).append((name, where))
    if not by_kind.get('file') or not by_kind.get('block'):
        return ['controls need a file row and a block row']
    file_name, file_path = by_kind['file'][0]
    block_name, block_path = by_kind['block'][0]
    text_of = dict(quoted)
    quoted_file = text_of[file_name]
    file_tokens = c_tokens(quoted_file)
    # A token of the quoted text, replaced in its file wherever the text occurs.
    word = next(t for t in file_tokens if re.fullmatch(r'[A-Za-z_]\w*', t) and len(t) > 3)

    unchanged = []

    def altered(path, change):
        if change(read(path)) == read(path):
            unchanged.append(path)
        return lambda where: change(read(where)) if where == path else read(where)

    def edit_quoted(text, quoted_text, edit):
        return text.replace(quoted_text, edit(quoted_text))

    block_body = text_of[block_name].strip()[1:-1].strip()
    cases = [
        ('relaid', True, quoted, rows, altered(file_path, lambda t: edit_quoted(
            t, quoted_file, lambda q: re.sub(r'[ \t]+', '\n\t ', q)))),
        ('changed token', False, quoted, rows, altered(file_path, lambda t: edit_quoted(
            t, quoted_file, lambda q: re.sub(r'\b' + word + r'\b', word + '_x', q)))),
        ('statement after the block', False, quoted, rows, altered(block_path, lambda t: re.sub(
            re.escape(block_body).replace(r'\ ', r'\s*'), lambda m: m.group() + ' (void)0;', t))),
        ('directive in a body', False, quoted, rows, altered(file_path, lambda t: edit_quoted(
            t, quoted_file, lambda q: q.replace('\n', '\n#if 1\n', 1) + '\n#endif'))),
        ('unlisted source', False, quoted,
         {n: r for n, r in rows.items() if n != file_name}, read),
        ('row without a source', False, quoted,
         dict(rows, **{'NativeFrames.NoSuchSource': ('file', file_path)}), read),
    ]
    wrong = [f'a control left {path} unchanged' for path in unchanged]
    for label, must_pass, case_quoted, case_rows, case_read in cases:
        if (not check(case_quoted, case_rows, case_read)[0]) != must_pass:
            wrong.append(f'control "{label}" should {"pass" if must_pass else "fail"}')
    return wrong


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    bridge, rows, root = Path(sys.argv[1]), manifest(sys.argv[2]), Path(sys.argv[3])
    quoted = [(full[len(PREFIX):] if full.startswith(PREFIX) else full, text)
              for full, text in Bridge(bridge.read_text()).sources]

    def read(where):
        return (root / where).read_text()

    failures, checked, absent = check(quoted, rows, read)
    if failures:
        sys.exit('FAIL: frame writeback bridge text:\n  ' + '\n  '.join(failures))
    wrong = controls(quoted, rows, read)
    if wrong:
        sys.exit('FAIL: frame writeback bridge text controls:\n  ' + '\n  '.join(wrong))
    print(f'PASS: frame writeback bridge text: {checked} quoted texts match the '
          f'runtime; {absent} describe code this tree does not have (6 controls)')


if __name__ == '__main__':
    main()
