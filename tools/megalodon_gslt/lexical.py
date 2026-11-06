"""Megalodon's lexer.mll, rule for rule, as a deterministic GSLT relation
from Unicode scalars to tokens (langdef/megalodon/lexical_v1.metta)."""

def cp(c):
    return '(cp %d)' % (ord(c) if isinstance(c, str) else c)

def lit(s):
    out = 'MgCpNilV1'
    for ch in reversed(s):
        out = '(MgCpConsV1 %s %s)' % (cp(ch), out)
    return out

def generate():
    rules = []
    ops = {}
    def op(name, arity):
        ops[name] = arity
    def rule(name, head, body=()):
        rules.append((name, head, list(body)))

    IDENT_START = '_' + ''.join(chr(c) for c in range(ord('a'), ord('z') + 1)) + ''.join(chr(c) for c in range(ord('A'), ord('Z') + 1))
    IDENT_REST = IDENT_START + "'" + '0123456789'
    DIGITS = '0123456789'
    QUOTE_SYMS = '_+-*^~=<>/\\'
    WHITESPACE = [32, 9, 13]

    SIMPLE = {  # one character, one token, nothing longer starts with it
        '[': 'MgTkLBrackV1', ']': 'MgTkRBrackV1', '{': 'MgTkLCBraceV1', '}': 'MgTkRCBraceV1',
        '|': 'MgTkVbarV1', '.': 'MgTkDotV1', ';': 'MgTkSemicolonV1', ',': 'MgTkCommaV1',
        ')': 'MgTkRParenV1',
    }
    SIMPLE_NAM = ['~', '+', '^', '!']
    SPECIAL = {'(': 'MgClsLParenV1', '*': 'MgClsStarV1', '"': 'MgClsDQuoteV1',
               '?': 'MgClsQMarkV1', ':': 'MgClsColonV1', '\\': 'MgClsBackslashV1',
               '=': 'MgClsEqualsV1', '<': 'MgClsLessV1', '>': 'MgClsGreaterV1',
               '/': 'MgClsSlashV1', '-': 'MgClsMinusV1', "'": 'MgClsApostropheV1'}

    for name, arity in [('cp', 1), ('cons', 2), ('different', 2),
                        ('MgCpConsV1', 2), ('MgTokConsV1', 2), ('MgTkNamV1', 1),
                        ('MgTkStringV1', 1), ('MgTkQStringV1', 1), ('MgTkNumV1', 4),
                        ('MgTkTheoremV1', 1), ('MgTkSpecCommV1', 1), ('MgSomeV1', 1),
                        ('MgClsSimpleV1', 1),
                        ('MgDocAcceptV1', 1), ('MgQuotedV1', 2),
                        ('MgAfterNewlineV1', 2), ('MgAfterCommentV1', 3), ('MgLexFailV1', 1)]:
        op(name, arity)

    # ---- character classes --------------------------------------------------
    op('MgCharClassV1', 2)
    classified = []
    for c in WHITESPACE:
        rule('mg-char-space-%d' % c, '(MgCharClassV1 (cp %d) MgClsSpaceV1)' % c); classified.append(c)
    rule('mg-char-newline-10', '(MgCharClassV1 (cp 10) MgClsNewlineV1)'); classified.append(10)
    for ch, tok in SIMPLE.items():
        rule('mg-char-simple-%d' % ord(ch), '(MgCharClassV1 %s (MgClsSimpleV1 %s))' % (cp(ch), tok)); classified.append(ord(ch))
    for ch in SIMPLE_NAM:
        rule('mg-char-simple-nam-%d' % ord(ch), '(MgCharClassV1 %s (MgClsSimpleV1 (MgTkNamV1 %s)))' % (cp(ch), lit(ch))); classified.append(ord(ch))
    for ch, cls in SPECIAL.items():
        rule('mg-char-special-%d' % ord(ch), '(MgCharClassV1 %s %s)' % (cp(ch), cls)); classified.append(ord(ch))
    for ch in DIGITS:
        rule('mg-char-digit-%d' % ord(ch), '(MgCharClassV1 %s MgClsDigitV1)' % cp(ch)); classified.append(ord(ch))
    for ch in IDENT_START:
        rule('mg-char-word-%d' % ord(ch), '(MgCharClassV1 %s MgClsWordV1)' % cp(ch)); classified.append(ord(ch))
    rule('mg-char-invalid', '(MgCharClassV1 ?c MgClsInvalidV1)',
         ['(different ?c (cp %d))' % c for c in classified])

    def yes_no(relname, chars, rulestem):
        op(relname, 2)
        for ch in chars:
            rule('%s-%d' % (rulestem, ord(ch)), '(%s %s MgYesV1)' % (relname, cp(ch)))
        rule('%s-no' % rulestem, '(%s ?c MgNoV1)' % relname,
             ['(different ?c %s)' % cp(ch) for ch in chars])

    yes_no('MgIdentRestClassV1', IDENT_REST, 'mg-ident-rest')
    yes_no('MgDigitClassV1', DIGITS, 'mg-digit')
    yes_no('MgQuoteSymClassV1', QUOTE_SYMS, 'mg-quote-sym')
    yes_no('MgExponentMarkClassV1', 'eE', 'mg-exponent-mark')

    # ---- the next scalar ------------------------------------------------------
    # A rule that may leave the next scalar to the following token keeps the
    # list it was given: a list is taken apart, never rebuilt, so the reading
    # is the same where `cons` is a list cell (SWI-PeTTa) and where it is a
    # constructor (CeTTa).
    op('MgPeekV1', 2); op('MgNextV1', 2)
    rule('mg-peek-end', '(MgPeekV1 nil MgAtEndV1)')
    rule('mg-peek-next', '(MgPeekV1 (cons ?c ?r) (MgNextV1 ?c ?r))')

    # ---- maximal runs ---------------------------------------------------------
    def run_relation(relname, classrel, stem):
        op(relname, 3)
        op(relname + 'Peek', 4)
        op(relname + 'Class', 6)
        rule(stem, '(%s ?input ?run ?after)' % relname,
             ['(MgPeekV1 ?input ?p)', '(%sPeek ?p ?input ?run ?after)' % relname])
        rule(stem + '-end', '(%sPeek MgAtEndV1 ?input MgCpNilV1 ?input)' % relname)
        rule(stem + '-next', '(%sPeek (MgNextV1 ?c ?r) ?input ?run ?after)' % relname,
             ['(%s ?c ?k)' % classrel, '(%sClass ?k ?c ?r ?input ?run ?after)' % relname])
        rule(stem + '-yes', '(%sClass MgYesV1 ?c ?r ?input (MgCpConsV1 ?c ?run) ?after)' % relname,
             ['(%s ?r ?run ?after)' % relname])
        rule(stem + '-no', '(%sClass MgNoV1 ?c ?r ?input MgCpNilV1 ?input)' % relname)

    run_relation('MgWordRunV1', 'MgIdentRestClassV1', 'mg-word-run')
    run_relation('MgDigitRunV1', 'MgDigitClassV1', 'mg-digit-run')

    # ---- one token ------------------------------------------------------------
    # MgNextTokenV1 scalars answers (MgTokenReadV1 token (MgScalarsV1 rest))
    # or MgTokensEndV1, after layout; MgLexV1 is the whole token list.
    op('MgNextTokenV1', 2); op('MgLexPeekV1', 2); op('MgTokenReadV1', 2); op('MgScalarsV1', 1)
    op('MgLexClassV1', 4); op('MgLexV1', 2); op('MgLexAfterV1', 2)
    rule('mg-next-token', '(MgNextTokenV1 ?input ?toks)', ['(MgPeekV1 ?input ?p)', '(MgLexPeekV1 ?p ?toks)'])
    rule('mg-next-token-end', '(MgLexPeekV1 MgAtEndV1 MgTokensEndV1)')
    rule('mg-next-token-scalar', '(MgLexPeekV1 (MgNextV1 ?c ?r) ?toks)',
         ['(MgCharClassV1 ?c ?k)', '(MgLexClassV1 ?k ?c ?r ?toks)'])
    rule('mg-lex-space', '(MgLexClassV1 MgClsSpaceV1 ?c ?r ?toks)', ['(MgNextTokenV1 ?r ?toks)'])
    # A newline and a doc comment are recorded around what follows them:
    # lexer.mll counts its position by them (incr lineno, update_pos), and by
    # bytes everywhere else.
    rule('mg-lex-newline', '(MgLexClassV1 MgClsNewlineV1 ?c ?r (MgAfterNewlineV1 ?r ?toks))', ['(MgNextTokenV1 ?r ?toks)'])
    rule('mg-lex-simple', '(MgLexClassV1 (MgClsSimpleV1 ?tok) ?c ?r (MgTokenReadV1 ?tok (MgScalarsV1 ?r)))')
    # What lexer.mll has no rule for fails, as ocamllex's "lexing: empty
    # token": (MgLexFailV1 ?r), ?r just after the scalar the lexeme began with.
    rule('mg-lex-invalid', '(MgLexClassV1 MgClsInvalidV1 ?c ?r (MgLexFailV1 ?r))')
    rule('mg-lex', '(MgLexV1 ?input ?toks)', ['(MgNextTokenV1 ?input ?read)', '(MgLexAfterV1 ?read ?toks)'])
    rule('mg-lex-done', '(MgLexAfterV1 MgTokensEndV1 MgTokNilV1)')
    rule('mg-lex-after-newline', '(MgLexAfterV1 (MgAfterNewlineV1 ?at ?read) ?toks)', ['(MgLexAfterV1 ?read ?toks)'])
    rule('mg-lex-after-comment', '(MgLexAfterV1 (MgAfterCommentV1 ?open ?after ?read) ?toks)', ['(MgLexAfterV1 ?read ?toks)'])
    rule('mg-lex-more', '(MgLexAfterV1 (MgTokenReadV1 ?tok (MgScalarsV1 ?rest)) (MgTokConsV1 ?tok ?toks))',
         ['(MgLexV1 ?rest ?toks)'])

    def emit(tok, rest):
        return '(MgTokenReadV1 %s (MgScalarsV1 %s))' % (tok, rest), []

    def follow_rule(stem, relname, cases, default_tok):
        """After one character: each (next scalar, token, continuation) wins over
    the default token, which leaves the next scalar to the following token."""
        op(relname, 2)
        op(relname + 'Peek', 3)
        rule(stem, '(%s ?input ?toks)' % relname,
             ['(MgPeekV1 ?input ?p)', '(%sPeek ?p ?input ?toks)' % relname])
        rule(stem + '-end', '(%sPeek MgAtEndV1 ?input (MgTokenReadV1 %s (MgScalarsV1 ?input)))' % (relname, default_tok))
        for nxt, tok, cont in cases:
            if cont is None:
                h, b = emit(tok, '?r')
                rule('%s-%d' % (stem, ord(nxt)), '(%sPeek (MgNextV1 %s ?r) ?input %s)' % (relname, cp(nxt), h), b)
            else:
                rule('%s-%d' % (stem, ord(nxt)), '(%sPeek (MgNextV1 %s ?r) ?input ?toks)' % (relname, cp(nxt)),
                     ['(%s ?r ?toks)' % cont])
        h, b = emit(default_tok, '?input')
        rule(stem + '-other', '(%sPeek (MgNextV1 ?c ?r) ?input %s)' % (relname, h),
             ['(different ?c %s)' % cp(n) for n, _, _ in cases] + b)

    # '(' : doc comments are layout; "(*" opens a special comment; else LPAREN.
    op('MgLexAfterDocV1', 3)
    op('MgLexParenStarV1', 2); op('MgLexParenStarPeekV1', 3)
    rule('mg-lex-lparen', '(MgLexClassV1 MgClsLParenV1 ?c ?r ?toks)', ['(MgLexParenV1 ?r ?toks)'])
    follow_rule('mg-lex-paren-next', 'MgLexParenV1', [('*', None, 'MgLexParenStarV1')], 'MgTkLParenV1')
    rule('mg-lex-paren-star', '(MgLexParenStarV1 ?input ?toks)',
         ['(MgPeekV1 ?input ?p)', '(MgLexParenStarPeekV1 ?p ?input ?toks)'])
    rule('mg-lex-paren-star-end', '(MgLexParenStarPeekV1 MgAtEndV1 ?input (MgTokenReadV1 MgTkOpenComV1 (MgScalarsV1 ?input)))')
    rule('mg-lex-paren-star-star', '(MgLexParenStarPeekV1 (MgNextV1 (cp 42) ?r) ?input ?toks)',
         ['(MgDocS0V1 ?r ?outcome)', '(MgLexAfterDocV1 ?outcome ?input ?toks)'])
    h, b = emit('MgTkOpenComV1', '?input')
    rule('mg-lex-paren-star-plain', '(MgLexParenStarPeekV1 (MgNextV1 ?c ?r) ?input %s)' % h, ['(different ?c (cp 42))'] + b)
    # ?star is just after "(*": the comment began two scalars before it.
    rule('mg-lex-doc-comment', '(MgLexAfterDocV1 (MgDocAcceptV1 ?after) ?star (MgAfterCommentV1 ?star ?after ?toks))',
         ['(MgNextTokenV1 ?after ?toks)'])
    h, b = emit('MgTkOpenComV1', '?star')
    rule('mg-lex-open-comment', '(MgLexAfterDocV1 MgDocFailV1 ?star %s)' % h, b)

    # Doc comment tail after "(**": \**[^*]*\*+\) as its automaton.
    for st in ['MgDocS0V1', 'MgDocS1V1', 'MgDocS2V1', 'MgDocS3V1']:
        op(st, 2)
    rule('mg-doc-s0-end', '(MgDocS0V1 nil MgDocFailV1)')
    rule('mg-doc-s0-star', '(MgDocS0V1 (cons (cp 42) ?r) ?o)', ['(MgDocS1V1 ?r ?o)'])
    rule('mg-doc-s0-other', '(MgDocS0V1 (cons ?c ?r) ?o)', ['(different ?c (cp 42))', '(MgDocS2V1 ?r ?o)'])
    rule('mg-doc-s1-end', '(MgDocS1V1 nil MgDocFailV1)')
    rule('mg-doc-s1-star', '(MgDocS1V1 (cons (cp 42) ?r) ?o)', ['(MgDocS1V1 ?r ?o)'])
    rule('mg-doc-s1-close', '(MgDocS1V1 (cons (cp 41) ?r) (MgDocAcceptV1 ?r))')
    rule('mg-doc-s1-other', '(MgDocS1V1 (cons ?c ?r) ?o)', ['(different ?c (cp 42))', '(different ?c (cp 41))', '(MgDocS2V1 ?r ?o)'])
    rule('mg-doc-s2-end', '(MgDocS2V1 nil MgDocFailV1)')
    rule('mg-doc-s2-star', '(MgDocS2V1 (cons (cp 42) ?r) ?o)', ['(MgDocS3V1 ?r ?o)'])
    rule('mg-doc-s2-other', '(MgDocS2V1 (cons ?c ?r) ?o)', ['(different ?c (cp 42))', '(MgDocS2V1 ?r ?o)'])
    rule('mg-doc-s3-end', '(MgDocS3V1 nil MgDocFailV1)')
    rule('mg-doc-s3-star', '(MgDocS3V1 (cons (cp 42) ?r) ?o)', ['(MgDocS3V1 ?r ?o)'])
    rule('mg-doc-s3-close', '(MgDocS3V1 (cons (cp 41) ?r) (MgDocAcceptV1 ?r))')
    rule('mg-doc-s3-other', '(MgDocS3V1 (cons ?c ?r) MgDocFailV1)', ['(different ?c (cp 42))', '(different ?c (cp 41))'])

    # '*' : "*)" closes a special comment; else the name *.
    rule('mg-lex-star', '(MgLexClassV1 MgClsStarV1 ?c ?r ?toks)', ['(MgLexStarV1 ?r ?toks)'])
    follow_rule('mg-lex-star-next', 'MgLexStarV1', [(')', 'MgTkCloseComV1', None)], '(MgTkNamV1 %s)' % lit('*'))

    # Strings and question strings: everything up to the closing delimiter.
    # An unclosed one is no lexeme: its text so far, and MgUnclosedV1 after it.
    op('MgDelimitedV1', 4); op('MgDelimitedReadV1', 4)
    rule('mg-delimited-close', '(MgDelimitedV1 ?d (cons ?d ?r) MgCpNilV1 ?r)')
    rule('mg-delimited-scalar', '(MgDelimitedV1 ?d (cons ?c ?r) (MgCpConsV1 ?c ?text) ?after)',
         ['(different ?c ?d)', '(MgDelimitedV1 ?d ?r ?text ?after)'])
    rule('mg-delimited-unclosed', '(MgDelimitedV1 ?d nil MgCpNilV1 MgUnclosedV1)')
    rule('mg-delimited-read-unclosed', '(MgDelimitedReadV1 ?r ?tok MgUnclosedV1 (MgLexFailV1 ?r))')
    rule('mg-delimited-read', '(MgDelimitedReadV1 ?r ?tok ?after (MgTokenReadV1 ?tok (MgScalarsV1 ?after)))',
         ['(different ?after MgUnclosedV1)'])
    rule('mg-lex-string', '(MgLexClassV1 MgClsDQuoteV1 ?c ?r ?toks)',
         ['(MgDelimitedV1 (cp 34) ?r ?text ?after)', '(MgDelimitedReadV1 ?r (MgTkStringV1 ?text) ?after ?toks)'])
    rule('mg-lex-question-string', '(MgLexClassV1 MgClsQMarkV1 ?c ?r ?toks)',
         ['(MgDelimitedV1 (cp 63) ?r ?text ?after)', '(MgDelimitedReadV1 ?r (MgTkQStringV1 ?text) ?after ?toks)'])

    # Quoted operator names 'S*' and :S*:, S = _+-*^~=<>/\.
    op('MgQuotedTailV1', 3); op('MgQuotedTailClassV1', 5); op('MgQuotedPrefixV1', 3)
    rule('mg-quoted-end', '(MgQuotedTailV1 ?d nil MgNotQuotedV1)')
    rule('mg-quoted-close', '(MgQuotedTailV1 ?d (cons ?d ?r) (MgQuotedV1 (MgCpConsV1 ?d MgCpNilV1) ?r))')
    rule('mg-quoted-scalar', '(MgQuotedTailV1 ?d (cons ?c ?r) ?o)',
         ['(different ?c ?d)', '(MgQuoteSymClassV1 ?c ?k)', '(MgQuotedTailClassV1 ?k ?d ?c ?r ?o)'])
    rule('mg-quoted-symbol', '(MgQuotedTailClassV1 MgYesV1 ?d ?c ?r ?o)',
         ['(MgQuotedTailV1 ?d ?r ?inner)', '(MgQuotedPrefixV1 ?c ?inner ?o)'])
    rule('mg-quoted-other', '(MgQuotedTailClassV1 MgNoV1 ?d ?c ?r MgNotQuotedV1)')
    rule('mg-quoted-prefix-yes', '(MgQuotedPrefixV1 ?c (MgQuotedV1 ?text ?after) (MgQuotedV1 (MgCpConsV1 ?c ?text) ?after))')
    rule('mg-quoted-prefix-no', '(MgQuotedPrefixV1 ?c MgNotQuotedV1 MgNotQuotedV1)')

    op('MgLexApostropheV1', 3)
    rule('mg-lex-apostrophe', '(MgLexClassV1 MgClsApostropheV1 ?c ?r ?toks)',
         ['(MgQuotedTailV1 (cp 39) ?r ?o)', '(MgLexApostropheV1 ?o ?r ?toks)'])
    h, b = emit('(MgTkNamV1 (MgCpConsV1 (cp 39) ?text))', '?after')
    rule('mg-lex-apostrophe-quoted', '(MgLexApostropheV1 (MgQuotedV1 ?text ?after) ?r %s)' % h, b)
    h, b = emit('(MgTkNamV1 %s)' % lit("'"), '?r')
    rule('mg-lex-apostrophe-name', '(MgLexApostropheV1 MgNotQuotedV1 ?r %s)' % h, b)

    # ':' : a quoted name, else :e, else :=, else the colon.
    op('MgLexColonV1', 3)
    rule('mg-lex-colon', '(MgLexClassV1 MgClsColonV1 ?c ?r ?toks)',
         ['(MgQuotedTailV1 (cp 58) ?r ?o)', '(MgLexColonV1 ?o ?r ?toks)'])
    h, b = emit('(MgTkNamV1 (MgCpConsV1 (cp 58) ?text))', '?after')
    rule('mg-lex-colon-quoted', '(MgLexColonV1 (MgQuotedV1 ?text ?after) ?r %s)' % h, b)
    rule('mg-lex-colon-unquoted', '(MgLexColonV1 MgNotQuotedV1 ?r ?toks)', ['(MgLexColonPlainV1 ?r ?toks)'])
    follow_rule('mg-lex-colon-next', 'MgLexColonPlainV1', [('e', 'MgTkMemV1', None), ('=', 'MgTkDeqV1', None)], 'MgTkColonV1')

    rule('mg-lex-backslash', '(MgLexClassV1 MgClsBackslashV1 ?c ?r ?toks)', ['(MgLexBackslashV1 ?r ?toks)'])
    follow_rule('mg-lex-backslash-next', 'MgLexBackslashV1', [('/', None, 'MgLexBackslashSlashV1')], 'MgTkBackslashV1')
    follow_rule('mg-lex-backslash-slash-next', 'MgLexBackslashSlashV1', [('_', '(MgTkNamV1 %s)' % lit('\\/_'), None)], '(MgTkNamV1 %s)' % lit('\\/'))

    rule('mg-lex-equals', '(MgLexClassV1 MgClsEqualsV1 ?c ?r ?toks)', ['(MgLexEqualsV1 ?r ?toks)'])
    follow_rule('mg-lex-equals-next', 'MgLexEqualsV1', [('>', 'MgTkDarrV1', None)], '(MgTkNamV1 %s)' % lit('='))

    rule('mg-lex-less', '(MgLexClassV1 MgClsLessV1 ?c ?r ?toks)', ['(MgLexLessV1 ?r ?toks)'])
    follow_rule('mg-lex-less-next', 'MgLexLessV1',
                [('-', None, 'MgLexLessMinusV1'), ('=', None, 'MgLexLessEqualsV1'), ('>', '(MgTkNamV1 %s)' % lit('<>'), None)],
                '(MgTkNamV1 %s)' % lit('<'))
    follow_rule('mg-lex-less-minus-next', 'MgLexLessMinusV1', [('>', '(MgTkNamV1 %s)' % lit('<->'), None)], '(MgTkNamV1 %s)' % lit('<-'))
    follow_rule('mg-lex-less-equals-next', 'MgLexLessEqualsV1', [('>', '(MgTkNamV1 %s)' % lit('<=>'), None)], '(MgTkNamV1 %s)' % lit('<='))

    rule('mg-lex-greater', '(MgLexClassV1 MgClsGreaterV1 ?c ?r ?toks)', ['(MgLexGreaterV1 ?r ?toks)'])
    follow_rule('mg-lex-greater-next', 'MgLexGreaterV1', [('=', '(MgTkNamV1 %s)' % lit('>='), None)], '(MgTkNamV1 %s)' % lit('>'))

    # '/' : only /:e /c= /\ /\_ are tokens; a slash alone is not, and fails.
    op('MgLexSlashV1', 2); op('MgLexSlashPeekV1', 3); op('MgLexSlashSecondV1', 4)
    rule('mg-lex-slash', '(MgLexClassV1 MgClsSlashV1 ?c ?r ?toks)', ['(MgLexSlashV1 ?r ?toks)'])
    rule('mg-lex-slash-peek', '(MgLexSlashV1 ?input ?toks)', ['(MgPeekV1 ?input ?p)', '(MgLexSlashPeekV1 ?p ?input ?toks)'])
    rule('mg-lex-slash-end', '(MgLexSlashPeekV1 MgAtEndV1 ?input (MgLexFailV1 ?input))')
    for first, second, word in [(':', 'e', '/:e'), ('c', '=', '/c=')]:
        rule('mg-lex-slash-%d' % ord(first), '(MgLexSlashPeekV1 (MgNextV1 %s ?r) ?input ?toks)' % cp(first),
             ['(MgPeekV1 ?r ?p)', '(MgLexSlashSecondV1 %s ?p ?input ?toks)' % cp(first)])
        h, b = emit('(MgTkNamV1 %s)' % lit(word), '?r')
        rule('mg-lex-slash-%d-%d' % (ord(first), ord(second)),
             '(MgLexSlashSecondV1 %s (MgNextV1 %s ?r) ?input %s)' % (cp(first), cp(second), h), b)
        rule('mg-lex-slash-%d-end' % ord(first), '(MgLexSlashSecondV1 %s MgAtEndV1 ?input (MgLexFailV1 ?input))' % cp(first))
        rule('mg-lex-slash-%d-other' % ord(first),
             '(MgLexSlashSecondV1 %s (MgNextV1 ?c ?r) ?input (MgLexFailV1 ?input))' % cp(first),
             ['(different ?c %s)' % cp(second)])
    rule('mg-lex-slash-backslash', '(MgLexSlashPeekV1 (MgNextV1 (cp 92) ?r) ?input ?toks)', ['(MgLexSlashBackslashV1 ?r ?toks)'])
    rule('mg-lex-slash-other', '(MgLexSlashPeekV1 (MgNextV1 ?c ?r) ?input (MgLexFailV1 ?input))',
         ['(different ?c (cp 58))', '(different ?c (cp 99))', '(different ?c (cp 92))'])
    follow_rule('mg-lex-slash-backslash-next', 'MgLexSlashBackslashV1', [('_', '(MgTkNamV1 %s)' % lit('/\\_'), None)], '(MgTkNamV1 %s)' % lit('/\\'))

    # '-' : -> ; a negative number ; else the name -.
    op('MgLexMinusV1', 2); op('MgLexMinusPeekV1', 3); op('MgLexMinusClassV1', 3)
    rule('mg-lex-minus', '(MgLexClassV1 MgClsMinusV1 ?c ?r ?toks)', ['(MgLexMinusV1 ?r ?toks)'])
    rule('mg-lex-minus-next', '(MgLexMinusV1 ?input ?toks)', ['(MgPeekV1 ?input ?p)', '(MgLexMinusPeekV1 ?p ?input ?toks)'])
    rule('mg-lex-minus-end', '(MgLexMinusPeekV1 MgAtEndV1 ?input (MgTokenReadV1 (MgTkNamV1 %s) (MgScalarsV1 ?input)))' % lit('-'))
    h, b = emit('(MgTkNamV1 %s)' % lit('->'), '?r')
    rule('mg-lex-arrow', '(MgLexMinusPeekV1 (MgNextV1 (cp 62) ?r) ?input %s)' % h, b)
    rule('mg-lex-minus-scalar', '(MgLexMinusPeekV1 (MgNextV1 ?c ?r) ?input ?toks)',
         ['(different ?c (cp 62))', '(MgDigitClassV1 ?c ?k)', '(MgLexMinusClassV1 ?k ?input ?toks)'])
    rule('mg-lex-minus-number', '(MgLexMinusClassV1 MgYesV1 ?input ?toks)', ['(MgLexNumberV1 MgTrueV1 ?input ?toks)'])
    h, b = emit('(MgTkNamV1 %s)' % lit('-'), '?input')
    rule('mg-lex-minus-name', '(MgLexMinusClassV1 MgNoV1 ?input %s)' % h, b)

    # Numbers: digits, an optional fraction, an optional exponent, longest
    # match; num_of_string's payload, whose exponent of a negative number is a
    # minus sign and the exponent's digits after the first.
    for name, arity in [('MgLexNumberV1', 3), ('MgNumberAfterWholeV1', 4), ('MgNumberAfterWholePeekV1', 5),
                        ('MgNumberDotV1', 5), ('MgNumberDotPeekV1', 6), ('MgNumberDotClassV1', 6),
                        ('MgNumberExponentV1', 5), ('MgNumberExponentPeekV1', 6), ('MgNumberExponentMarkV1', 7),
                        ('MgNumberExponentAfterMarkV1', 7), ('MgNumberExponentDigitV1', 7), ('MgExponentValueV1', 3)]:
        op(name, arity)
    rule('mg-lex-digit', '(MgLexClassV1 MgClsDigitV1 ?c ?r ?toks)', ['(MgLexNumberFromV1 MgFalseV1 ?c ?r ?toks)'])
    op('MgLexNumberFromV1', 4)
    # the digit is already taken apart; its run starts with it
    rule('mg-lex-number-from', '(MgLexNumberFromV1 ?neg ?c ?r ?toks)',
         ['(MgDigitRunV1 ?r ?run ?after)', '(MgNumberAfterWholeV1 ?neg (MgCpConsV1 ?c ?run) ?after ?toks)'])
    rule('mg-lex-number', '(MgLexNumberV1 ?neg ?input ?toks)',
         ['(MgDigitRunV1 ?input ?whole ?r)', '(MgNumberAfterWholeV1 ?neg ?whole ?r ?toks)'])
    rule('mg-number-after-whole', '(MgNumberAfterWholeV1 ?neg ?whole ?input ?toks)',
         ['(MgPeekV1 ?input ?p)', '(MgNumberAfterWholePeekV1 ?p ?neg ?whole ?input ?toks)'])
    h, b = emit('(MgTkNumV1 ?neg ?whole MgNoneV1 MgNoneV1)', '?input')
    rule('mg-number-end', '(MgNumberAfterWholePeekV1 MgAtEndV1 ?neg ?whole ?input %s)' % h, b)
    rule('mg-number-fraction-mark', '(MgNumberAfterWholePeekV1 (MgNextV1 (cp 46) ?r) ?neg ?whole ?input ?toks)',
         ['(MgNumberDotV1 ?neg ?whole ?r ?input ?toks)'])
    rule('mg-number-no-fraction', '(MgNumberAfterWholePeekV1 (MgNextV1 ?c ?r) ?neg ?whole ?input ?toks)',
         ['(different ?c (cp 46))', '(MgNumberExponentV1 ?neg ?whole MgNoneV1 ?input ?toks)'])
    # after "digits." : a fraction needs a digit; otherwise the dot is the next token
    rule('mg-number-dot', '(MgNumberDotV1 ?neg ?whole ?afterdot ?dot ?toks)',
         ['(MgPeekV1 ?afterdot ?p)', '(MgNumberDotPeekV1 ?p ?neg ?whole ?afterdot ?dot ?toks)'])
    h, b = emit('(MgTkNumV1 ?neg ?whole MgNoneV1 MgNoneV1)', '?dot')
    rule('mg-number-dot-end', '(MgNumberDotPeekV1 MgAtEndV1 ?neg ?whole ?afterdot ?dot %s)' % h, b)
    rule('mg-number-dot-scalar', '(MgNumberDotPeekV1 (MgNextV1 ?c ?r) ?neg ?whole ?afterdot ?dot ?toks)',
         ['(MgDigitClassV1 ?c ?k)', '(MgNumberDotClassV1 ?k ?neg ?whole ?afterdot ?dot ?toks)'])
    rule('mg-number-fraction', '(MgNumberDotClassV1 MgYesV1 ?neg ?whole ?digits ?dot ?toks)',
         ['(MgDigitRunV1 ?digits ?fraction ?r)', '(MgNumberExponentV1 ?neg ?whole (MgSomeV1 ?fraction) ?r ?toks)'])
    h, b = emit('(MgTkNumV1 ?neg ?whole MgNoneV1 MgNoneV1)', '?dot')
    rule('mg-number-dot-not-fraction', '(MgNumberDotClassV1 MgNoV1 ?neg ?whole ?digits ?dot %s)' % h, b)
    # an exponent: e or E and at least one digit; otherwise the mark is not taken
    rule('mg-number-exponent', '(MgNumberExponentV1 ?neg ?whole ?fraction ?input ?toks)',
         ['(MgPeekV1 ?input ?p)', '(MgNumberExponentPeekV1 ?p ?neg ?whole ?fraction ?input ?toks)'])
    h, b = emit('(MgTkNumV1 ?neg ?whole ?fraction MgNoneV1)', '?input')
    rule('mg-number-exponent-end', '(MgNumberExponentPeekV1 MgAtEndV1 ?neg ?whole ?fraction ?input %s)' % h, b)
    rule('mg-number-exponent-scalar', '(MgNumberExponentPeekV1 (MgNextV1 ?c ?r) ?neg ?whole ?fraction ?input ?toks)',
         ['(MgExponentMarkClassV1 ?c ?k)', '(MgNumberExponentMarkV1 ?k ?neg ?whole ?fraction ?r ?input ?toks)'])
    h, b = emit('(MgTkNumV1 ?neg ?whole ?fraction MgNoneV1)', '?input')
    rule('mg-number-exponent-none', '(MgNumberExponentMarkV1 MgNoV1 ?neg ?whole ?fraction ?r ?input %s)' % h, b)
    rule('mg-number-exponent-mark', '(MgNumberExponentMarkV1 MgYesV1 ?neg ?whole ?fraction ?r ?input ?toks)',
         ['(MgPeekV1 ?r ?p)', '(MgNumberExponentAfterMarkV1 ?p ?neg ?whole ?fraction ?r ?input ?toks)'])
    h, b = emit('(MgTkNumV1 ?neg ?whole ?fraction MgNoneV1)', '?input')
    rule('mg-number-exponent-mark-end', '(MgNumberExponentAfterMarkV1 MgAtEndV1 ?neg ?whole ?fraction ?r ?input %s)' % h, b)
    rule('mg-number-exponent-mark-scalar', '(MgNumberExponentAfterMarkV1 (MgNextV1 ?d ?s) ?neg ?whole ?fraction ?r ?input ?toks)',
         ['(MgDigitClassV1 ?d ?k)', '(MgNumberExponentDigitV1 ?k ?neg ?whole ?fraction ?r ?input ?toks)'])
    rule('mg-number-exponent-digits', '(MgNumberExponentDigitV1 MgYesV1 ?neg ?whole ?fraction ?digits ?input (MgTokenReadV1 (MgTkNumV1 ?neg ?whole ?fraction (MgSomeV1 ?exponent)) (MgScalarsV1 ?r)))',
         ['(MgDigitRunV1 ?digits ?run ?r)', '(MgExponentValueV1 ?neg ?run ?exponent)'])
    h, b = emit('(MgTkNumV1 ?neg ?whole ?fraction MgNoneV1)', '?input')
    rule('mg-number-exponent-not-digit', '(MgNumberExponentDigitV1 MgNoV1 ?neg ?whole ?fraction ?digits ?input %s)' % h, b)
    rule('mg-exponent-positive', '(MgExponentValueV1 MgFalseV1 ?run ?run)')
    rule('mg-exponent-negative', '(MgExponentValueV1 MgTrueV1 (MgCpConsV1 ?first ?rest) (MgCpConsV1 (cp 45) ?rest))')

    # Words: an identifier, a keyword, or exists! Binder+ c= when the next
    # scalar extends them, as the longest match does.
    KEYWORDS = [('at', 'MgTkAtV1'), ('if', 'MgTkIfV1'), ('then', 'MgTkThenV1'), ('else', 'MgTkElseV1'),
                ('let', 'MgTkLetV1'), ('assume', 'MgTkAssumeV1'), ('apply', 'MgTkApplyV1'),
                ('claim', 'MgTkClaimV1'), ('prove', 'MgTkProveV1'), ('in', 'MgTkInV1'),
                ('Section', 'MgTkSectionV1'), ('End', 'MgTkEndV1'), ('Let', 'MgTkLetDecV1'),
                ('Variable', 'MgTkVarV1'), ('Hypothesis', 'MgTkHypV1'), ('Parameter', 'MgTkParamV1'),
                ('Axiom', 'MgTkAxiomV1')]
    KEYWORDS += [(k, '(MgTkTheoremV1 %s)' % lit(k)) for k in
                 ['Lemma', 'Theorem', 'Example', 'Fact', 'Remark', 'Corollary', 'Proposition', 'Property']]
    KEYWORDS += [('exact', 'MgTkExactV1'), ('Qed', 'MgTkQedV1'), ('Conjecture', 'MgTkConjectureV1'),
                 ('Definition', 'MgTkDefV1'), ('Infix', 'MgTkInfixV1'), ('Postfix', 'MgTkPostfixV1'),
                 ('Prefix', 'MgTkPrefixV1'), ('Notation', 'MgTkNotationV1'), ('Unicode', 'MgTkUnicodeV1'),
                 ('Subscript', 'MgTkSubscriptV1'), ('Superscript', 'MgTkSuperscriptV1')]
    KEYWORDS += [(k, '(MgTkSpecCommV1 %s)' % lit(k)) for k in ['ShowProofTerms', 'HideProofTerms', 'Verbose']]
    KEYWORDS += [('Salt', 'MgTkSaltV1'), ('Opaque', 'MgTkOpaqueV1'), ('Transparent', 'MgTkTransparentV1'),
                 ('Treasure', 'MgTkTreasureV1'), ('Title', 'MgTkTitleV1'), ('Author', 'MgTkAuthorV1'),
                 ('Admitted', 'MgTkAdmittedV1'), ('admit', 'MgTkAdmitV1'), ('aby', 'MgTkAbyV1'), ('TEXT', 'MgTkTextV1')]
    GLUE = [('exists', '!', '(MgTkNamV1 %s)' % lit('exists!'), '(MgTkNamV1 %s)' % lit('exists'), 'MgWordExistsV1'),
            ('Binder', '+', 'MgTkBinderPlusV1', 'MgTkBinderV1', 'MgWordBinderV1'),
            ('c', '=', 'MgTkSubeqV1', '(MgTkNamV1 %s)' % lit('c'), 'MgWordCV1')]
    op('MgLexWordV1', 3); op('MgWordKindV1', 2); op('MgWordTokenV1', 4); op('MgKeywordV1', 2)
    rule('mg-lex-word', '(MgLexClassV1 MgClsWordV1 ?c ?r ?out)',
         ['(MgWordRunV1 ?r ?run ?after)', '(MgWordKindV1 (MgCpConsV1 ?c ?run) ?kind)',
          '(MgWordTokenV1 ?kind (MgCpConsV1 ?c ?run) ?after ?out)'])
    for word, nxt, glued, alone, kind in GLUE:
        rule('mg-word-kind-%s' % kind, '(MgWordKindV1 %s %s)' % (lit(word), kind))
    rule('mg-word-kind-plain', '(MgWordKindV1 ?w MgWordPlainV1)', ['(different ?w %s)' % lit(g[0]) for g in GLUE])
    op('MgWordGlueV1', 4)
    for word, nxt, glued, alone, kind in GLUE:
        rule('mg-word-glue-%s' % kind, '(MgWordTokenV1 %s ?w ?after ?out)' % kind,
             ['(MgPeekV1 ?after ?p)', '(MgWordGlueV1 %s ?p ?after ?out)' % kind])
        rule('mg-word-glue-end-%s' % kind, '(MgWordGlueV1 %s MgAtEndV1 ?after (MgTokenReadV1 %s (MgScalarsV1 ?after)))' % (kind, alone))
        rule('mg-word-glued-%s' % kind, '(MgWordGlueV1 %s (MgNextV1 %s ?r) ?after (MgTokenReadV1 %s (MgScalarsV1 ?r)))' % (kind, cp(nxt), glued))
        rule('mg-word-alone-%s' % kind, '(MgWordGlueV1 %s (MgNextV1 ?c ?r) ?after (MgTokenReadV1 %s (MgScalarsV1 ?after)))' % (kind, alone),
             ['(different ?c %s)' % cp(nxt)])
    rule('mg-word-plain', '(MgWordTokenV1 MgWordPlainV1 ?w ?after (MgTokenReadV1 ?tok (MgScalarsV1 ?after)))',
         ['(MgKeywordV1 ?w ?tok)'])
    for word, tok in KEYWORDS:
        rule('mg-keyword-%s' % word, '(MgKeywordV1 %s %s)' % (lit(word), tok))
    rule('mg-keyword-name', '(MgKeywordV1 ?w (MgTkNamV1 ?w))', ['(different ?w %s)' % lit(k) for k, _ in KEYWORDS])

    for name, head, body in rules:
        for word in head.replace('(', ' ').replace(')', ' ').split():
            pass

    def sig():
        lines = []
        for name in sorted(ops, key=lambda n: (n not in ('cp', 'cons', 'different'), n)):
            lines.append('    (operator %s %d)' % (name, ops[name]))
        return '\n'.join(lines)

    out = []
    out.append('''; Megalodon's lexer (lexer.mll, Chad E. Brown) as a relation from a
; document's Unicode scalars to its tokens, one token per read
; (MgNextTokenV1), as parser.ml reads its token stream.  Each scalar is classified by a
; total function; the longest-match rule of ocamllex, with ties to the earlier
; rule, becomes a decision on the next scalars, so exactly one reading
; exists for a lexable document.  Where lexer.mll has no rule (a lone
; slash, an unclosed string, a scalar no rule begins with), the read is
; (MgLexFailV1 AFTER), AFTER just after the lexeme's first scalar:
; Megalodon's "lexing: empty token" there.  Whitespace and doc comments
; (** ... *) are layout; a
; newline and a doc comment are recorded around the read that follows them
; (MgAfterNewlineV1 just after the newline, MgAfterCommentV1 just after its
; "(*" and just after it), for lexer.mll counts its position by them.  A
; NUM token carries num_of_string's payload.  The scalars are taken apart and
; never rebuilt (MgPeekV1), and every list built here has its own
; constructors, so the reading is the same in every PeTTa.
(gslt-presentation-v1 MegalodonLexicalV1
  (signature
%s)
  (equations)
  (rewrites''' % sig())
    for name, head, body in rules:
        if body:
            out.append('    (rule %s\n      (head %s)\n      (body\n        %s))' % (name, head, '\n        '.join(body)))
        else:
            out.append('    (rule %s\n      (head %s)\n      (body))' % (name, head))
    out[-1] += '))'
    return '\n'.join(out) + '\n'
