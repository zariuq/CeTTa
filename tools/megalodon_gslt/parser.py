"""Authoring aid: writes the Megalodon parser presentations under
langdef/megalodon/ (term_parser_v1, document_v1, proof_script_v1,
notation_environment_v1, source_fold_v1).  Each Megalodon parsing function
(parser.ml) is written below as a short sequential program in the relational
syntax of tools/gslt_relational_syntax_v1.py, which compiles it into
deterministic relations.  Every parse function answers (MgParsedV1 value
rest), MgEofV1 (Megalodon's Lexer.Eof, which ends the document wherever it is
raised), or (MgErrorV1 e), Megalodon's ParsingError or Failure with its
message and positions."""



from gslt_relational_syntax_v1 import (B, Call, Case, Expect, Fail, Keep, Module, Pure, Push,  # noqa: F401
                                       Read, Ret, Tail, Vocabulary, cp)
from gslt_relational_syntax_v1 import Compiler as RelationalCompiler
from gslt_relational_syntax_v1 import lit as vocab_lit
from gslt_relational_syntax_v1 import mglist as vocab_mglist

MG = Vocabulary('Mg')


def lit(s):
    return vocab_lit(MG, s)


def mglist(items):
    return vocab_mglist(MG, items)


def Compiler(module, errors=False):
    return RelationalCompiler(module, MG, errors)


def generate():
    # ---------------------------------------------------------------------------
    # Tokens: kinds and payloads

    TOKENS_NULLARY = ['LParen', 'RParen', 'Comma', 'Colon', 'Darr', 'Deq', 'In', 'Mem', 'Subeq', 'Vbar',
                      'LCBrace', 'RCBrace', 'LBrack', 'RBrack', 'At', 'If', 'Then', 'Else', 'Semicolon',
                      'Backslash', 'Dot', 'OpenCom', 'CloseCom', 'Salt', 'Opaque', 'Transparent', 'Treasure',
                      'Title', 'Author', 'Admitted', 'Admit', 'Aby', 'Section', 'End', 'LetDec', 'Var', 'Hyp',
                      'Param', 'Axiom', 'Prefix', 'Postfix', 'Infix', 'Binder', 'BinderPlus', 'Notation',
                      'Unicode', 'Superscript', 'Subscript', 'Def', 'Conjecture', 'Let', 'Assume', 'Apply',
                      'Claim', 'Prove', 'Exact', 'Qed', 'Text']
    TOKENS_ARG = ['Nam', 'String', 'QString', 'Theorem', 'SpecComm']


    def tk(name):
        return 'MgTk%sV1' % name


    def kd(name):
        return 'MgK%sV1' % name


    env_mod = Module('MegalodonNotationEnvironmentV1', '''
; Megalodon's notation environment (parser.ml, megalodon.ml), as data that a
; document's declarations transform.  Its seven tables are Megalodon's:
; prefix operators, postfix and infix operators, binders, and the four
; priority sets that keep prefix, right-infix and other operators apart.  A
; Section saves the tables and End restores them, which is what undoing
; every declaration since the Section does.  Priorities are decimal digits
; without leading zeros; the names _0, _1, ... are the postfix projections
; of metatuples at priority 1.  A refused declaration has no derivation.
''')
    term_mod = Module('MegalodonTermParserV1', '''
; Megalodon's term parser (parse_S_ and parse_S'_ of parser.ml, Chad E.
; Brown): priorities, prefix, postfix and infix operators and binders from
; the notation environment, application by juxtaposition, byte names \\xHH,
; let forms, set braces, tuples and if-then-else, with the trees (ltree) the
; parser builds.  A parse function answers (MgParsedV1 tree rest) or
; MgEofV1 when it asks for a token past the end.
''')
    doc_mod = Module('MegalodonDocumentV1', '''
; Megalodon's document items (parse_docitem of parser.ml): declarations,
; notations, sections, variables, hypotheses, special comments (* ... *) and
; theorems, read from tokens against the notation environment.
''')
    pf_mod = Module('MegalodonProofScriptV1', '''
; Megalodon's proof tactics (parse_pftacitem of parser.ml), aby included.
; The case readers keep parser.ml's reading exactly: read_case drops the
; last case of each group and read_cases the final group.
''')
    fold_mod = Module('MegalodonSourceFoldV1', '''
; Reading a Megalodon document: one item at a time from a reader (mode,
; token stream, notation environment), the declarations of each item
; transforming the environment before the next is read, and a theorem's
; tactics until Qed or Admitted.  A document read after signatures (-I)
; starts from their environment with no open section; the end of input ends
; the document, also inside an item.
''')

    for m in [env_mod, term_mod, doc_mod, pf_mod, fold_mod]:
        for n, a in [('cp', 1), ('different', 2), ('MgCpConsV1', 2), ('MgListConsV1', 2),
                     ('MgSomeV1', 1), ('MgPairV1', 2), ('MgParsedV1', 2), ('MgIntV1', 1),
                     ('MgReadTokenV1', 2), ('MgTokenReadV1', 2), ('MgPushedV1', 2), ('MgScalarsV1', 1),
                     ('MgStreamV1', 2), ('MgEvNilV1', 0)]:
            m.op(n, a)

    # Token kinds and payloads live with the term parser.
    for n in ['Nam', 'String', 'QString', 'Theorem', 'SpecComm']:
        term_mod.op(tk(n), 1)
    term_mod.op(tk('Num'), 4)
    term_mod.op('MgTokKindV1', 2)
    for n in TOKENS_NULLARY:
        term_mod.rule('mg-tok-kind-%s' % n.lower(), '(MgTokKindV1 %s %s)' % (tk(n), kd(n)))
    for n in TOKENS_ARG:
        term_mod.rule('mg-tok-kind-%s' % n.lower(), '(MgTokKindV1 (%s ?x) %s)' % (tk(n), kd(n)))
    term_mod.rule('mg-tok-kind-num', '(MgTokKindV1 (%s ?n ?w ?f ?e) %s)' % (tk('Num'), kd('Num')))
    for n in TOKENS_ARG:
        term_mod.op('Mg%sTextV1' % n, 2)
        term_mod.rule('mg-%s-text' % n.lower(), '(Mg%sTextV1 (%s ?x) ?x)' % (n, tk(n)))
    # A token stream is parser.ml's: tokens put back (TokStrBuff), then the
    # lexer's next token from the scalars (TokStrRest).  The stream keeps the
    # newlines and doc comments its reads passed (newest first), the events
    # lexer.mll's position counts besides bytes; a reader of items folds them
    # into Megalodon's line and char.
    term_mod.op('MgNextTokenV1', 2)
    for n, a in [('MgStreamReadV1', 3), ('MgAfterNewlineV1', 2), ('MgAfterCommentV1', 3),
                 ('MgEvNewlineV1', 2), ('MgEvCommentV1', 3), ('MgLexFailV1', 1), ('MgLexFailAtV1', 2),
                 ('MgErrorV1', 1), ('MgFailureV1', 2)]:
        term_mod.op(n, a)
    term_mod.rule('mg-read-token-pushed', '(MgReadTokenV1 (MgPushedV1 ?t ?s) (MgTokenReadV1 ?t ?s))')
    term_mod.rule('mg-read-token-stream', '(MgReadTokenV1 (MgStreamV1 ?input ?ev) ?r)',
                  ['(MgNextTokenV1 ?input ?read)', '(MgStreamReadV1 ?read ?ev ?r)'])
    term_mod.rule('mg-stream-read-newline', '(MgStreamReadV1 (MgAfterNewlineV1 ?at ?read) ?ev ?r)',
                  ['(MgStreamReadV1 ?read (MgEvNewlineV1 ?at ?ev) ?r)'])
    term_mod.rule('mg-stream-read-comment', '(MgStreamReadV1 (MgAfterCommentV1 ?open ?after ?read) ?ev ?r)',
                  ['(MgStreamReadV1 ?read (MgEvCommentV1 ?open ?after ?ev) ?r)'])
    term_mod.rule('mg-stream-read-token',
                  '(MgStreamReadV1 (MgTokenReadV1 ?t (MgScalarsV1 ?rest)) ?ev (MgTokenReadV1 ?t (MgStreamV1 ?rest ?ev)))')
    term_mod.rule('mg-stream-read-end', '(MgStreamReadV1 MgTokensEndV1 ?ev MgTokensEndV1)')
    # A lexing failure is Megalodon's Failure "lexing: empty token" where the
    # failed lexeme began: one scalar before ?at, after the events ?ev.
    term_mod.rule('mg-stream-read-lex-fail',
                  '(MgStreamReadV1 (MgLexFailV1 ?at) ?ev (MgErrorV1 (MgFailureV1 %s (MgLexFailAtV1 ?at ?ev))))'
                  % mglist([lit('lexing: empty token')]))

    # ---------------------------------------------------------------------------
    # Notation environment

    E = env_mod
    for n, a in [('MgEnvV1', 2), ('MgTablesV1', 7), ('MgSectionFrameV1', 2), ('MgPreopEntryV1', 2),
                 ('MgPostInfEntryV1', 3), ('MgBinderEntryV1', 2), ('MgPostInfV1', 2), ('MgPenvEntryV1', 3),
                 ('MgPenvLookupV1', 3), ('MgLookupPreopV1', 3), ('MgLookupPostInfV1', 3), ('MgLookupBinderV1', 3),
                 ('MgPrioMemberV1', 3), ('MgProjNameV1', 2), ('MgDigitsOnlyV1', 2), ('MgProjAfterV1', 3),
                 ('MgPostInfOfV1', 4), ('MgInitialEnvV1', 1), ('MgDigitV1', 2),
                 ('MgLevelLeqV1', 3), ('MgLengthCmpV1', 3), ('MgDigitsCmpV1', 3), ('MgDigitCmpV1', 3),
                 ('MgDigitsCmpAfterV1', 4), ('MgLeqOfV1', 2), ('MgLevelSuccV1', 2), ('MgIncDigitsV1', 3),
                 ('MgIncStepV1', 5), ('MgIncTopV1', 3), ('MgDigitSuccV1', 3), ('MgLevelZeroV1', 2),
                 ('MgStripZerosV1', 2), ('MgPriorityValidV1', 2), ('MgShortDigitsV1', 2), ('MgPriorityNonzeroV1', 2),
                 ('MgDeclarePostInfixV1', 5), ('MgDeclarePrefixV1', 4), ('MgDeclareBinderV1', 4),
                 ('MgPostInfPrefixV1', 7), ('MgPostInfRightV1', 6), ('MgPostInfOtherV1', 6),
                 ('MgPrefixPriorityV1', 5), ('MgPrefixBinderV1', 5), ('MgBinderPrefixV1', 5),
                 ('MgErrorV1', 1), ('MgFailureV1', 2),
                 ('MgEnterSectionV1', 3), ('MgEndSectionV1', 3), ('MgEndFrameV1', 4), ('MgNewFileEnvV1', 2),
                 ('MgRightCaseV1', 2), ('MgPostInfDeclareV1', 6), ('MgBinderTokenV1', 2), ('MgSignedV1', 2)]:
        E.op(n, a)

    DIG = '0123456789'
    for d in DIG:
        E.rule('mg-env-digit-%s' % d, '(MgDigitV1 %s MgYesV1)' % cp(d))
    E.rule('mg-env-digit-no', '(MgDigitV1 ?c MgNoV1)', ['(different ?c %s)' % cp(d) for d in DIG])

    # Lookups: the most recent entry for a name.
    E.rule('mg-lookup-preop-nil', '(MgLookupPreopV1 MgListNilV1 ?x MgNoneV1)')
    E.rule('mg-lookup-preop-here', '(MgLookupPreopV1 (MgListConsV1 (MgPreopEntryV1 ?x ?p) ?rest) ?x (MgSomeV1 ?p))')
    E.rule('mg-lookup-preop-next', '(MgLookupPreopV1 (MgListConsV1 (MgPreopEntryV1 ?y ?p) ?rest) ?x ?r)',
           ['(different ?y ?x)', '(MgLookupPreopV1 ?rest ?x ?r)'])
    E.rule('mg-lookup-postinf-nil', '(MgLookupPostInfV1 MgListNilV1 ?x MgNoneV1)')
    E.rule('mg-lookup-postinf-here', '(MgLookupPostInfV1 (MgListConsV1 (MgPostInfEntryV1 ?x ?p ?c) ?rest) ?x (MgSomeV1 (MgPostInfV1 ?p ?c)))')
    E.rule('mg-lookup-postinf-next', '(MgLookupPostInfV1 (MgListConsV1 (MgPostInfEntryV1 ?y ?p ?c) ?rest) ?x ?r)',
           ['(different ?y ?x)', '(MgLookupPostInfV1 ?rest ?x ?r)'])
    E.rule('mg-lookup-binder-nil', '(MgLookupBinderV1 MgListNilV1 ?x MgNoneV1)')
    E.rule('mg-lookup-binder-here', '(MgLookupBinderV1 (MgListConsV1 (MgBinderEntryV1 ?x ?t) ?rest) ?x (MgSomeV1 ?t))')
    E.rule('mg-lookup-binder-next', '(MgLookupBinderV1 (MgListConsV1 (MgBinderEntryV1 ?y ?t) ?rest) ?x ?r)',
           ['(different ?y ?x)', '(MgLookupBinderV1 ?rest ?x ?r)'])
    E.rule('mg-prio-member-nil', '(MgPrioMemberV1 MgListNilV1 ?p MgNoV1)')
    E.rule('mg-prio-member-here', '(MgPrioMemberV1 (MgListConsV1 ?p ?rest) ?p MgYesV1)')
    E.rule('mg-prio-member-next', '(MgPrioMemberV1 (MgListConsV1 ?q ?rest) ?p ?r)',
           ['(different ?q ?p)', '(MgPrioMemberV1 ?rest ?p ?r)'])

    # _N names are postfix projections at priority 1.
    E.rule('mg-proj-name-nil', '(MgProjNameV1 MgCpNilV1 MgNoV1)')
    E.rule('mg-proj-name-underscore', '(MgProjNameV1 (MgCpConsV1 (cp 95) ?rest) ?r)', ['(MgProjAfterV1 ?rest ?r ?r)'])
    E.rule('mg-proj-name-other', '(MgProjNameV1 (MgCpConsV1 ?c ?rest) MgNoV1)', ['(different ?c (cp 95))'])
    # MgProjAfterV1 digits answer answer: no digits after _ is not a projection
    E.rule('mg-proj-after-nil', '(MgProjAfterV1 MgCpNilV1 MgNoV1 MgNoV1)')
    E.rule('mg-proj-after-digits', '(MgProjAfterV1 (MgCpConsV1 ?c ?rest) ?r ?r)', ['(MgDigitV1 ?c ?k)', '(MgDigitsOnlyV1 (MgPairV1 ?k ?rest) ?r)'])
    E.rule('mg-digits-only-no', '(MgDigitsOnlyV1 (MgPairV1 MgNoV1 ?rest) MgNoV1)')
    E.rule('mg-digits-only-end', '(MgDigitsOnlyV1 (MgPairV1 MgYesV1 MgCpNilV1) MgYesV1)')
    E.rule('mg-digits-only-more', '(MgDigitsOnlyV1 (MgPairV1 MgYesV1 (MgCpConsV1 ?c ?rest)) ?r)',
           ['(MgDigitV1 ?c ?k)', '(MgDigitsOnlyV1 (MgPairV1 ?k ?rest) ?r)'])

    E.rule('mg-penv-lookup',
           '(MgPenvLookupV1 (MgEnvV1 (MgTablesV1 ?pre ?post ?bind ?pp ?npp ?rp ?nrp) ?secs) ?x (MgPenvEntryV1 ?a ?b ?c))',
           ['(MgLookupPreopV1 ?pre ?x ?a)', '(MgProjNameV1 ?x ?proj)', '(MgPostInfOfV1 ?proj ?post ?x ?b)',
            '(MgLookupBinderV1 ?bind ?x ?c)'])
    E.rule('mg-postinf-of-projection', '(MgPostInfOfV1 MgYesV1 ?post ?x (MgSomeV1 (MgPostInfV1 %s MgPostfixV1)))' % lit('1'))
    E.rule('mg-postinf-of-table', '(MgPostInfOfV1 MgNoV1 ?post ?x ?b)', ['(MgLookupPostInfV1 ?post ?x ?b)'])

    # The initial environment: fun and forall are binders, -> and ^ infix.
    E.rule('mg-initial-env',
           '(MgInitialEnvV1 (MgEnvV1 (MgTablesV1 MgListNilV1 %s %s MgListNilV1 %s %s MgListNilV1) MgListNilV1))' % (
               mglist(['(MgPostInfEntryV1 %s %s MgInfixNoneV1)' % (lit('^'), lit('790')),
                       '(MgPostInfEntryV1 %s %s MgInfixRightV1)' % (lit('->'), lit('800'))]),
               mglist(['(MgBinderEntryV1 %s MgTkCommaV1)' % lit('forall'),
                       '(MgBinderEntryV1 %s MgTkDarrV1)' % lit('fun')]),
               mglist([lit('790'), lit('800')]), mglist([lit('790'), lit('800')])))

    # Priorities: comparison, successor, normal form.
    for a in DIG:
        for b in DIG:
            c = 'MgLtV1' if a < b else ('MgEqV1' if a == b else 'MgGtV1')
            E.rule('mg-digit-cmp-%s-%s' % (a, b), '(MgDigitCmpV1 %s %s %s)' % (cp(a), cp(b), c))
    E.rule('mg-length-cmp-nil-nil', '(MgLengthCmpV1 MgCpNilV1 MgCpNilV1 MgEqV1)')
    E.rule('mg-length-cmp-nil-cons', '(MgLengthCmpV1 MgCpNilV1 (MgCpConsV1 ?y ?b) MgLtV1)')
    E.rule('mg-length-cmp-cons-nil', '(MgLengthCmpV1 (MgCpConsV1 ?x ?a) MgCpNilV1 MgGtV1)')
    E.rule('mg-length-cmp-cons-cons', '(MgLengthCmpV1 (MgCpConsV1 ?x ?a) (MgCpConsV1 ?y ?b) ?c)', ['(MgLengthCmpV1 ?a ?b ?c)'])
    E.rule('mg-digits-cmp-nil', '(MgDigitsCmpV1 MgCpNilV1 MgCpNilV1 MgEqV1)')
    E.rule('mg-digits-cmp-cons', '(MgDigitsCmpV1 (MgCpConsV1 ?x ?a) (MgCpConsV1 ?y ?b) ?c)',
           ['(MgDigitCmpV1 ?x ?y ?d)', '(MgDigitsCmpAfterV1 ?d ?a ?b ?c)'])
    E.rule('mg-digits-cmp-after-lt', '(MgDigitsCmpAfterV1 MgLtV1 ?a ?b MgLtV1)')
    E.rule('mg-digits-cmp-after-gt', '(MgDigitsCmpAfterV1 MgGtV1 ?a ?b MgGtV1)')
    E.rule('mg-digits-cmp-after-eq', '(MgDigitsCmpAfterV1 MgEqV1 ?a ?b ?c)', ['(MgDigitsCmpV1 ?a ?b ?c)'])
    E.op('MgLevelLeqLengthV1', 4)
    E.rule('mg-level-leq', '(MgLevelLeqV1 ?a ?b ?r)', ['(MgLengthCmpV1 ?a ?b ?c)', '(MgLevelLeqLengthV1 ?c ?a ?b ?r)'])
    E.rule('mg-level-leq-shorter', '(MgLevelLeqLengthV1 MgLtV1 ?a ?b MgYesV1)')
    E.rule('mg-level-leq-longer', '(MgLevelLeqLengthV1 MgGtV1 ?a ?b MgNoV1)')
    E.rule('mg-level-leq-same', '(MgLevelLeqLengthV1 MgEqV1 ?a ?b ?r)', ['(MgDigitsCmpV1 ?a ?b ?c)', '(MgLeqOfV1 ?c ?r)'])
    E.rule('mg-leq-of-lt', '(MgLeqOfV1 MgLtV1 MgYesV1)')
    E.rule('mg-leq-of-eq', '(MgLeqOfV1 MgEqV1 MgYesV1)')
    E.rule('mg-leq-of-gt', '(MgLeqOfV1 MgGtV1 MgNoV1)')
    for i, d in enumerate(DIG):
        nxt = DIG[(i + 1) % 10]
        E.rule('mg-digit-succ-%s' % d, '(MgDigitSuccV1 %s %s %s)' % (cp(d), cp(nxt), 'MgYesV1' if d == '9' else 'MgNoV1'))
    E.rule('mg-level-succ', '(MgLevelSuccV1 ?p ?q)', ['(MgIncDigitsV1 ?p ?s ?carry)', '(MgIncTopV1 ?carry ?s ?q)'])
    E.rule('mg-inc-digits-nil', '(MgIncDigitsV1 MgCpNilV1 MgCpNilV1 MgYesV1)')
    E.rule('mg-inc-digits-cons', '(MgIncDigitsV1 (MgCpConsV1 ?d ?rest) ?out ?carry)',
           ['(MgIncDigitsV1 ?rest ?inc ?c)', '(MgIncStepV1 ?c ?d ?inc ?out ?carry)'])
    E.rule('mg-inc-step-keep', '(MgIncStepV1 MgNoV1 ?d ?inc (MgCpConsV1 ?d ?inc) MgNoV1)')
    E.rule('mg-inc-step-carry', '(MgIncStepV1 MgYesV1 ?d ?inc (MgCpConsV1 ?e ?inc) ?carry)', ['(MgDigitSuccV1 ?d ?e ?carry)'])
    E.rule('mg-inc-top-keep', '(MgIncTopV1 MgNoV1 ?s ?s)')
    E.rule('mg-inc-top-carry', '(MgIncTopV1 MgYesV1 ?s (MgCpConsV1 (cp 49) ?s))')
    E.rule('mg-level-zero', '(MgLevelZeroV1 (MgCpConsV1 (cp 48) MgCpNilV1) MgYesV1)')
    E.rule('mg-level-zero-long', '(MgLevelZeroV1 (MgCpConsV1 (cp 48) (MgCpConsV1 ?d ?r)) MgNoV1)')
    E.rule('mg-level-nonzero', '(MgLevelZeroV1 (MgCpConsV1 ?d ?r) MgNoV1)', ['(different ?d (cp 48))'])
    E.rule('mg-strip-zeros-last', '(MgStripZerosV1 (MgCpConsV1 (cp 48) MgCpNilV1) (MgCpConsV1 (cp 48) MgCpNilV1))')
    E.rule('mg-strip-zeros-lead', '(MgStripZerosV1 (MgCpConsV1 (cp 48) (MgCpConsV1 ?d ?r)) ?out)',
           ['(MgStripZerosV1 (MgCpConsV1 ?d ?r) ?out)'])
    E.rule('mg-strip-zeros-done', '(MgStripZerosV1 (MgCpConsV1 ?d ?r) (MgCpConsV1 ?d ?r))', ['(different ?d (cp 48))'])
    # read_int_ts then 0 < p < 1000: the digits of a valid priority, else none
    E.rule('mg-priority-valid-negative', '(MgPriorityValidV1 (MgSignedV1 MgTrueV1 ?d) MgNoneV1)')
    E.rule('mg-priority-valid', '(MgPriorityValidV1 (MgSignedV1 MgFalseV1 ?d) ?r)',
           ['(MgStripZerosV1 ?d ?n)', '(MgShortDigitsV1 ?n ?short)', '(MgPriorityNonzeroV1 (MgPairV1 ?short ?n) ?r)'])
    E.rule('mg-short-one', '(MgShortDigitsV1 (MgCpConsV1 ?a MgCpNilV1) MgYesV1)')
    E.rule('mg-short-two', '(MgShortDigitsV1 (MgCpConsV1 ?a (MgCpConsV1 ?b MgCpNilV1)) MgYesV1)')
    E.rule('mg-short-three', '(MgShortDigitsV1 (MgCpConsV1 ?a (MgCpConsV1 ?b (MgCpConsV1 ?c MgCpNilV1))) MgYesV1)')
    E.rule('mg-short-long', '(MgShortDigitsV1 (MgCpConsV1 ?a (MgCpConsV1 ?b (MgCpConsV1 ?c (MgCpConsV1 ?d ?e)))) MgNoV1)')
    E.rule('mg-priority-long', '(MgPriorityNonzeroV1 (MgPairV1 MgNoV1 ?n) MgNoneV1)')
    E.rule('mg-priority-zero', '(MgPriorityNonzeroV1 (MgPairV1 MgYesV1 (MgCpConsV1 (cp 48) MgCpNilV1)) MgNoneV1)')
    E.rule('mg-priority-some', '(MgPriorityNonzeroV1 (MgPairV1 MgYesV1 (MgCpConsV1 ?d ?r)) (MgSomeV1 (MgCpConsV1 ?d ?r)))',
           ['(different ?d (cp 48))'])

    # declare_postinfix, declare_prefix, declare_binder (parser.ml)
    E.rule('mg-right-case-right', '(MgRightCaseV1 MgInfixRightV1 MgYesV1)')
    for c in ['MgPostfixV1', 'MgInfixNoneV1', 'MgInfixLeftV1']:
        E.rule('mg-right-case-%s' % c[2:-2].lower(), '(MgRightCaseV1 %s MgNoV1)' % c)
    # Each check in parser.ml's order; a failed one is megalodon.ml's
    # Failure where the declaration was read (the next read's MgItemStartV1).
    def taken(why):
        return '(MgErrorV1 (MgFailureV1 %s MgItemStartV1))' % mglist(
            ['?x', lit(' cannot be given priority '), '?p', lit(' since %s has this priority already.' % why)])
    T = '(MgEnvV1 (MgTablesV1 ?pre ?post ?bind ?pp ?npp ?rp ?nrp) ?secs)'
    E.rule('mg-declare-postinfix', '(MgDeclarePostInfixV1 %s ?x ?p ?pic ?out)' % T,
           ['(MgPrioMemberV1 ?pp ?p ?prefix)', '(MgRightCaseV1 ?pic ?right)',
            '(MgPostInfPrefixV1 ?prefix ?right %s ?x ?p ?pic ?out)' % T])
    E.rule('mg-postinf-prefix-taken', '(MgPostInfPrefixV1 MgYesV1 ?right %s ?x ?p ?pic %s)' % (T, taken('a prefix')))
    E.rule('mg-postinf-prefix-free', '(MgPostInfPrefixV1 MgNoV1 ?right %s ?x ?p ?pic ?out)' % T,
           ['(MgPostInfDeclareV1 ?right %s ?x ?p ?pic ?out)' % T])
    E.rule('mg-postinf-declare-right', '(MgPostInfDeclareV1 MgYesV1 %s ?x ?p ?pic ?out)' % T,
           ['(MgPrioMemberV1 ?nrp ?p ?m)', '(MgPostInfRightV1 ?m %s ?x ?p ?pic ?out)' % T])
    E.rule('mg-postinf-right-taken', '(MgPostInfRightV1 MgYesV1 %s ?x ?p ?pic %s)'
           % (T, taken('a nonright infix operator or postfix operator')))
    E.rule('mg-postinf-right-free',
           '(MgPostInfRightV1 MgNoV1 %s ?x ?p ?pic '
           '(MgEnvV1 (MgTablesV1 ?pre (MgListConsV1 (MgPostInfEntryV1 ?x ?p ?pic) ?post) ?bind ?pp (MgListConsV1 ?p ?npp) (MgListConsV1 ?p ?rp) ?nrp) ?secs))' % T)
    E.rule('mg-postinf-declare-other', '(MgPostInfDeclareV1 MgNoV1 %s ?x ?p ?pic ?out)' % T,
           ['(MgPrioMemberV1 ?rp ?p ?m)', '(MgPostInfOtherV1 ?m %s ?x ?p ?pic ?out)' % T])
    E.rule('mg-postinf-other-taken', '(MgPostInfOtherV1 MgYesV1 %s ?x ?p ?pic %s)' % (T, taken('a right infix operator')))
    E.rule('mg-postinf-other-free',
           '(MgPostInfOtherV1 MgNoV1 %s ?x ?p ?pic '
           '(MgEnvV1 (MgTablesV1 ?pre (MgListConsV1 (MgPostInfEntryV1 ?x ?p ?pic) ?post) ?bind ?pp (MgListConsV1 ?p ?npp) ?rp (MgListConsV1 ?p ?nrp)) ?secs))' % T)
    E.rule('mg-declare-prefix', '(MgDeclarePrefixV1 %s ?x ?p ?out)' % T,
           ['(MgPrioMemberV1 ?npp ?p ?m)', '(MgPrefixPriorityV1 ?m %s ?x ?p ?out)' % T])
    E.rule('mg-prefix-priority-taken', '(MgPrefixPriorityV1 MgYesV1 %s ?x ?p %s)' % (T, taken('an infix or postfix operator')))
    E.rule('mg-prefix-priority-free', '(MgPrefixPriorityV1 MgNoV1 %s ?x ?p ?out)' % T,
           ['(MgLookupBinderV1 ?bind ?x ?b)', '(MgPrefixBinderV1 ?b %s ?x ?p ?out)' % T])
    E.rule('mg-prefix-binder-taken', '(MgPrefixBinderV1 (MgSomeV1 ?tok) %s ?x ?p (MgErrorV1 (MgFailureV1 %s MgItemStartV1)))'
           % (T, mglist(['?x', lit(' is already a binder and cannot be used as a prefix operator.')])))
    E.rule('mg-prefix-binder-free',
           '(MgPrefixBinderV1 MgNoneV1 %s ?x ?p '
           '(MgEnvV1 (MgTablesV1 (MgListConsV1 (MgPreopEntryV1 ?x ?p) ?pre) ?post ?bind (MgListConsV1 ?p ?pp) ?npp ?rp (MgListConsV1 ?p ?nrp)) ?secs))' % T)
    E.rule('mg-binder-token-comma', '(MgBinderTokenV1 MgTrueV1 MgTkCommaV1)')
    E.rule('mg-binder-token-darr', '(MgBinderTokenV1 MgFalseV1 MgTkDarrV1)')
    E.rule('mg-declare-binder', '(MgDeclareBinderV1 %s ?x ?comma ?out)' % T,
           ['(MgLookupPreopV1 ?pre ?x ?p)', '(MgBinderPrefixV1 ?p %s ?x ?comma ?out)' % T])
    E.rule('mg-binder-prefix-taken', '(MgBinderPrefixV1 (MgSomeV1 ?q) %s ?x ?comma (MgErrorV1 (MgFailureV1 %s MgItemStartV1)))'
           % (T, mglist(['?x', lit(' is already a prefix operator and cannot be used as a binder.')])))
    E.rule('mg-binder-prefix-free',
           '(MgBinderPrefixV1 MgNoneV1 %s ?x ?comma '
           '(MgEnvV1 (MgTablesV1 ?pre ?post (MgListConsV1 (MgBinderEntryV1 ?x ?tok) ?bind) ?pp ?npp ?rp ?nrp) ?secs))' % T,
           ['(MgBinderTokenV1 ?comma ?tok)'])
    E.rule('mg-enter-section', '(MgEnterSectionV1 (MgEnvV1 ?tables ?secs) ?x (MgEnvV1 ?tables (MgListConsV1 (MgSectionFrameV1 ?x ?tables) ?secs)))')
    E.rule('mg-end-section', '(MgEndSectionV1 (MgEnvV1 ?tables (MgListConsV1 (MgSectionFrameV1 ?x ?saved) ?secs)) ?x (MgEnvV1 ?saved ?secs))')
    # megalodon.ml's End, where the item was read (MgItemStartV1 of the next
    # read): another section is open, or none
    E.rule('mg-end-section-other',
           '(MgEndSectionV1 (MgEnvV1 ?tables (MgListConsV1 (MgSectionFrameV1 ?y ?saved) ?secs)) ?x (MgErrorV1 (MgFailureV1 %s MgItemStartV1)))'
           % mglist([lit('Section '), '?y', lit(' cannot be ended with End '), '?x']), ['(different ?x ?y)'])
    E.rule('mg-end-section-none', '(MgEndSectionV1 (MgEnvV1 ?tables MgListNilV1) ?x (MgErrorV1 (MgFailureV1 %s MgItemStartV1)))'
           % mglist([lit('No Section To End')]))
    E.rule('mg-new-file-env', '(MgNewFileEnvV1 (MgEnvV1 ?tables ?secs) (MgEnvV1 ?tables MgListNilV1))')

    # ---------------------------------------------------------------------------
    # The term parser

    T = term_mod
    C = Compiler(T, errors=True)
    L1023, L1010, L500 = lit('1023'), lit('1010'), lit('500')
    for n, a in [('MgByteLV1', 1), ('MgStringLV1', 1), ('MgQStringLV1', 1), ('MgNaLV1', 1), ('MgNuLV1', 4),
                 ('MgLeLV1', 4), ('MgLeMLV1', 4), ('MgBiLV1', 4), ('MgPreoLV1', 2), ('MgPostoLV1', 2),
                 ('MgInfoLV1', 3), ('MgImplopLV1', 2), ('MgSepLV1', 4), ('MgRepLV1', 4), ('MgSepRepLV1', 5),
                 ('MgSetEnumLV1', 1), ('MgMTupleLV1', 2), ('MgParenLV1', 2), ('MgIfThenElseLV1', 3),
                 ('MgInfNamV1', 1), ('MgInfSetV1', 1), ('MgEnvV1', 2), ('MgTablesV1', 7), ('MgPenvEntryV1', 3),
                 ('MgPostInfV1', 2), ('MgPenvLookupV1', 3), ('MgLevelLeqV1', 3), ('MgLevelSuccV1', 2),
                 ('MgLevelZeroV1', 2), ('MgSignedV1', 2)]:
        T.op(n, a)

    LTREE = [('ByteL', 1), ('StringL', 1), ('QStringL', 1), ('NaL', 1), ('NuL', 4), ('LeL', 4), ('LeML', 4),
             ('BiL', 4), ('PreoL', 2), ('PostoL', 2), ('InfoL', 3), ('ImplopL', 2), ('SepL', 4), ('RepL', 4),
             ('SepRepL', 5), ('SetEnumL', 1), ('MTupleL', 2), ('ParenL', 2), ('IfThenElseL', 3)]


    def ltree_pattern(name, arity, prefix='?a'):
        return '(Mg%sV1 %s)' % (name, ' '.join('%s%d' % (prefix, i) for i in range(arity)))


    # binderishp
    T.op('MgBinderishV1', 2)
    for name, arity in LTREE:
        pat = ltree_pattern(name, arity)
        if name in ('BiL', 'LeL', 'LeML', 'IfThenElseL'):
            T.rule('mg-binderish-%s' % name.lower(), '(MgBinderishV1 %s MgYesV1)' % pat)
        elif name == 'PreoL':
            T.rule('mg-binderish-preol', '(MgBinderishV1 (MgPreoLV1 ?x ?a) ?r)', ['(MgBinderishV1 ?a ?r)'])
        elif name == 'InfoL':
            T.rule('mg-binderish-infol', '(MgBinderishV1 (MgInfoLV1 ?i ?a ?b) ?r)', ['(MgBinderishV1 ?b ?r)'])
        else:
            T.rule('mg-binderish-%s' % name.lower(), '(MgBinderishV1 %s MgNoV1)' % pat)

    # {x :e X | P} is read off the tree parsed before the bar
    T.op('MgSepShapeV1', 2); T.op('MgSepShapeAfterV1', 4); T.op('MgSeparationV1', 3)
    for name, arity in LTREE:
        pat = ltree_pattern(name, arity, '?s')
        if name == 'InfoL':
            T.rule('mg-sep-shape-infol-name', '(MgSepShapeV1 (MgInfoLV1 (MgInfNamV1 ?n) ?l ?a) MgNotSeparationV1)')
            T.rule('mg-sep-shape-infol-set', '(MgSepShapeV1 (MgInfoLV1 (MgInfSetV1 ?i) ?l ?a) ?s)', ['(MgSepShapeAfterV1 ?l ?i ?a ?s)'])
        else:
            T.rule('mg-sep-shape-%s' % name.lower(), '(MgSepShapeV1 %s MgNotSeparationV1)' % pat)
        if name == 'NaL':
            T.rule('mg-sep-shape-after-nal', '(MgSepShapeAfterV1 (MgNaLV1 ?x) ?i ?a (MgSeparationV1 ?x ?i ?a))')
        else:
            T.rule('mg-sep-shape-after-%s' % name.lower(), '(MgSepShapeAfterV1 %s ?i ?a MgNotSeparationV1)' % pat)

    # The wrapper set braces put around their set: itself, or an application.
    T.op('MgWrapV1', 3); T.op('MgWrapImplopV1', 1)
    T.rule('mg-wrap-id', '(MgWrapV1 MgWrapIdV1 ?t ?t)')
    T.rule('mg-wrap-implop', '(MgWrapV1 (MgWrapImplopV1 ?f) ?t (MgImplopLV1 ?f ?t))')

    # Names: ordinary, prefix, binder, infix-or-postfix (parse_S_ order), and
    # what parse_S'_ does with a name.
    T.op('MgNameClassV1', 2); T.op('MgPrefixClsV1', 1); T.op('MgBinderClsV1', 1)
    T.rule('mg-name-class-ordinary', '(MgNameClassV1 (MgPenvEntryV1 MgNoneV1 MgNoneV1 MgNoneV1) MgOrdinaryClsV1)')
    T.rule('mg-name-class-prefix', '(MgNameClassV1 (MgPenvEntryV1 (MgSomeV1 ?p) ?b ?c) (MgPrefixClsV1 ?p))')
    T.rule('mg-name-class-binder', '(MgNameClassV1 (MgPenvEntryV1 MgNoneV1 ?b (MgSomeV1 ?t)) (MgBinderClsV1 ?t))')
    T.rule('mg-name-class-infix', '(MgNameClassV1 (MgPenvEntryV1 MgNoneV1 (MgSomeV1 ?pi) MgNoneV1) MgInfixClsV1)')
    T.op('MgTailClassV1', 2); T.op('MgTailOpV1', 2)
    T.rule('mg-tail-class-op', '(MgTailClassV1 (MgPenvEntryV1 ?a (MgSomeV1 (MgPostInfV1 ?p ?pic)) ?c) (MgTailOpV1 ?p ?pic))')
    T.rule('mg-tail-class-ordinary', '(MgTailClassV1 (MgPenvEntryV1 MgNoneV1 MgNoneV1 MgNoneV1) MgTailOrdinaryV1)')
    T.rule('mg-tail-class-prefix', '(MgTailClassV1 (MgPenvEntryV1 (MgSomeV1 ?p) MgNoneV1 ?c) MgTailStopV1)')
    T.rule('mg-tail-class-binder', '(MgTailClassV1 (MgPenvEntryV1 MgNoneV1 MgNoneV1 (MgSomeV1 ?t)) MgTailStopV1)')
    T.op('MgOrdinaryV1', 2)
    T.rule('mg-ordinary-yes', '(MgOrdinaryV1 (MgPenvEntryV1 MgNoneV1 MgNoneV1 MgNoneV1) MgYesV1)')
    T.rule('mg-ordinary-prefix', '(MgOrdinaryV1 (MgPenvEntryV1 (MgSomeV1 ?p) ?b ?c) MgNoV1)')
    T.rule('mg-ordinary-postinf', '(MgOrdinaryV1 (MgPenvEntryV1 MgNoneV1 (MgSomeV1 ?b) ?c) MgNoV1)')
    T.rule('mg-ordinary-binder', '(MgOrdinaryV1 (MgPenvEntryV1 MgNoneV1 MgNoneV1 (MgSomeV1 ?c)) MgNoV1)')
    T.op('MgNamOrAscV1', 2)
    for k in ['Nam', 'Colon', 'Mem', 'Subeq']:
        T.rule('mg-nam-or-asc-%s' % k.lower(), '(MgNamOrAscV1 %s MgYesV1)' % kd(k))
    T.rule('mg-nam-or-asc-no', '(MgNamOrAscV1 ?k MgNoV1)', ['(different ?k %s)' % kd(k) for k in ['Nam', 'Colon', 'Mem', 'Subeq']])
    T.op('MgSameTokenV1', 3)
    T.rule('mg-same-token', '(MgSameTokenV1 ?t ?t MgYesV1)')
    T.rule('mg-same-token-no', '(MgSameTokenV1 ?t ?u MgNoV1)', ['(different ?t ?u)'])
    T.op('MgBinderSeparatorV1', 2)
    T.rule('mg-binder-separator-comma', '(MgBinderSeparatorV1 MgTkCommaV1 %s)' % lit(','))
    T.rule('mg-binder-separator-darr', '(MgBinderSeparatorV1 MgTkDarrV1 %s)' % lit('=>'))
    T.op('MgNumTreeV1', 2)
    T.rule('mg-num-tree', '(MgNumTreeV1 (MgTkNumV1 ?n ?w ?f ?e) (MgNuLV1 ?n ?w ?f ?e))')

    # \xHH names
    HEX = '0123456789ABCDEFabcdef'
    T.op('MgHexDigitV1', 2); T.op('MgHexByteV1', 2); T.op('MgHexAfterXV1', 2); T.op('MgHexPairV1', 3)
    T.op('MgHexValueV1', 1)
    for h in HEX:
        T.rule('mg-hex-digit-%d' % ord(h), '(MgHexDigitV1 %s (MgSomeV1 (MgHexValueV1 %s)))' % (cp(h), cp(h.upper() if h.isalpha() else h)))
    T.rule('mg-hex-digit-no', '(MgHexDigitV1 ?c MgNoneV1)', ['(different ?c %s)' % cp(h) for h in HEX])
    T.rule('mg-hex-byte-nil', '(MgHexByteV1 MgCpNilV1 MgNoneV1)')
    T.rule('mg-hex-byte-x', '(MgHexByteV1 (MgCpConsV1 (cp 120) ?rest) ?r)', ['(MgHexAfterXV1 ?rest ?r)'])
    T.rule('mg-hex-byte-other', '(MgHexByteV1 (MgCpConsV1 ?c ?rest) MgNoneV1)', ['(different ?c (cp 120))'])
    T.rule('mg-hex-after-x-nil', '(MgHexAfterXV1 MgCpNilV1 MgNoneV1)')
    T.rule('mg-hex-after-x-one', '(MgHexAfterXV1 (MgCpConsV1 ?a MgCpNilV1) MgNoneV1)')
    T.rule('mg-hex-after-x-two', '(MgHexAfterXV1 (MgCpConsV1 ?a (MgCpConsV1 ?b MgCpNilV1)) ?r)',
           ['(MgHexDigitV1 ?a ?ha)', '(MgHexDigitV1 ?b ?hb)', '(MgHexPairV1 ?ha ?hb ?r)'])
    T.rule('mg-hex-after-x-long', '(MgHexAfterXV1 (MgCpConsV1 ?a (MgCpConsV1 ?b (MgCpConsV1 ?c ?d))) MgNoneV1)')
    T.rule('mg-hex-pair-none-left', '(MgHexPairV1 MgNoneV1 ?b MgNoneV1)')
    T.rule('mg-hex-pair-none-right', '(MgHexPairV1 (MgSomeV1 ?a) MgNoneV1 MgNoneV1)')
    UPPER = '0123456789ABCDEF'
    for i, a in enumerate(UPPER):
        for j, b in enumerate(UPPER):
            T.rule('mg-hex-pair-%s%s' % (a.lower(), b.lower()),
                   '(MgHexPairV1 (MgSomeV1 (MgHexValueV1 %s)) (MgSomeV1 (MgHexValueV1 %s)) (MgSomeV1 (MgIntV1 %s)))' % (
                       cp(a), cp(b), lit(str(16 * i + j))))

    K = kd
    # parser.ml tok_to_str, for read_expected_ts's message
    TOK_TEXT = {'LParen': '(', 'RParen': ')', 'Comma': ',', 'Colon': ':', 'Darr': '=>', 'Deq': ':=', 'In': 'in',
                'Mem': ':e', 'Subeq': 'c=', 'Vbar': '|', 'LCBrace': '{', 'RCBrace': '}', 'LBrack': '[',
                'RBrack': ']', 'At': 'at', 'If': 'if', 'Then': 'then', 'Else': 'else', 'Semicolon': ';',
                'Backslash': '\\', 'Dot': '.', 'OpenCom': '(*', 'CloseCom': '*)'}

    def X(name):
        """read_expected_ts [TOK]"""
        return Expect(tk(name), Fail('Syntax error: unexpected token, expected ' + TOK_TEXT[name]))
    P = Pure


    def kind(t, k):
        return P('(MgTokKindV1 %s %s)' % (t, k), [k])


    def penv(x, pen):
        return P('(MgPenvLookupV1 ?env %s %s)' % (x, pen), [pen])


    def text(n, t, x):
        return P('(Mg%sTextV1 %s %s)' % (n, t, x), [x])


    # parse_Names: ordinary names
    C.function('MgParseNamesV1', [], [
        Read('?t'), kind('?t', '?k'),
        Case('?k', [B(K('Nam'), [text('Nam', '?t', '?x'), penv('?x', '?pen'), P('(MgOrdinaryV1 ?pen ?o)', ['?o']),
                                 Case('?o', [B('MgYesV1', [Call('MgParseNamesV1', [], '?xs'), Ret('(MgListConsV1 ?x ?xs)')]),
                                             B('MgNoV1', [Push('?t'), Ret('MgListNilV1')])])])],
             default=[Push('?t'), Ret('MgListNilV1')])])

    # parse_Comma_Names: , x , y ...
    C.function('MgParseCommaNamesV1', [], [
        Read('?t'), kind('?t', '?k'),
        Case('?k', [B(K('Comma'), [Read('?u'), kind('?u', '?ku'),
                                   Case('?ku', [B(K('Nam'), [text('Nam', '?u', '?x'), penv('?x', '?pen'), P('(MgOrdinaryV1 ?pen ?o)', ['?o']),
                                                             Case('?o', [B('MgYesV1', [Call('MgParseCommaNamesV1', [], '?xs'), Ret('(MgListConsV1 ?x ?xs)')]),
                                                                         B('MgNoV1', [Fail(['The name ', '?x', ' cannot be used here since it has a special meaning.'])])])])],
                                        default=[Fail('Expected a name here.')])])],
             default=[Push('?t'), Ret('MgListNilV1')])])

    # parse_N: , S , S ...
    C.function('MgParseNV1', [], [
        Read('?t'), kind('?t', '?k'),
        Case('?k', [B(K('Comma'), [Call('MgParseSV1', [L1023], '?a'), Call('MgParseNV1', [], '?as'), Ret('(MgListConsV1 ?a ?as)')])],
             default=[Push('?t'), Ret('MgListNilV1')])])

    ASC = [('Colon', 'MgAscTpV1'), ('Mem', 'MgAscSetV1'), ('Subeq', 'MgAscSubeqV1')]

    # parse_TVs: (x y : A) (z) ...
    C.function('MgParseTVsV1', [], [
        Read('?t'), kind('?t', '?k'),
        Case('?k', [B(K('LParen'), [Keep('?lp'), Call('MgParseNamesV1', [], '?xs'), Read('?u'), kind('?u', '?ku'),
                                    Case('?ku', [B(K('RParen'), [Call('MgParseTVsV1', [], '?vs'),
                                                                 Ret('(MgListConsV1 (MgPairV1 ?xs MgNoneV1) ?vs)')])] +
                                         [B(K(a), [Call('MgParseSV1', [L1010], '?b'),
                                                   Expect(tk('RParen'), Fail('Unmatched ( in bound variable declaration', '?lp')),
                                                   Call('MgParseTVsV1', [], '?vs'),
                                                   Ret('(MgListConsV1 (MgPairV1 ?xs (MgSomeV1 (MgPairV1 %s ?b))) ?vs)' % asc)])
                                          for a, asc in ASC],
                                         default=[Fail('Unmatched ( in bound variable declaration', '?lp')])])],
             default=[Push('?t'), Ret('MgListNilV1')])])


    def binder_after_names(x, mtok, li):
        # after parse_Names in the binder case; Ill-formed Binder from li on
        ill = Fail(['Ill-formed Binder ', x], li)
        branches = []
        for a, asc in ASC:
            branches.append(B(K(a), [Call('MgParseSV1', [L1010], '?b'), Read('?v'),
                                     P('(MgSameTokenV1 ?v %s ?same)' % mtok, ['?same']),
                                     Case('?same', [B('MgYesV1', [Call('MgParseSV1', [L1010], '?body'),
                                                                  P('(MgBinderSeparatorV1 %s ?sep)' % mtok, ['?sep']),
                                                                  Ret('(MgBiLV1 %s ?sep (MgListConsV1 (MgPairV1 ?xs (MgSomeV1 (MgPairV1 %s ?b))) MgListNilV1) ?body)' % (x, asc))]),
                                                    B('MgNoV1', [ill])])]))
        return [Read('?u'), kind('?u', '?ku'),
                Case('?ku', branches,
                     default=[P('(MgSameTokenV1 ?u %s ?same)' % mtok, ['?same']),
                              Case('?same', [B('MgYesV1', [Call('MgParseSV1', [L1010], '?body'),
                                                           P('(MgBinderSeparatorV1 %s ?sep)' % mtok, ['?sep']),
                                                           Ret('(MgBiLV1 %s ?sep (MgListConsV1 (MgPairV1 ?xs MgNoneV1) MgListNilV1) ?body)' % x)]),
                                             B('MgNoV1', [ill])])])]


    def hex_byte(after):
        return [Read('?u'), kind('?u', '?ku'),
                Case('?ku', [B(K('Nam'), [text('Nam', '?u', '?hx'), P('(MgHexByteV1 ?hx ?hb)', ['?hb']),
                                          Case('?hb', [B('(MgSomeV1 ?byte)', after, ['?byte']),
                                                       B('MgNoneV1', [Fail('Expected \\x followed by two hexits')])])])],
                     default=[Fail('Expected \\x followed by two hexits')])]


    # parse_S_ q
    C.function('MgParseSV1', ['?q'], [
        Read('?t'), kind('?t', '?k'),
        Case('?k', [
            B(K('Backslash'), hex_byte([Tail('MgParseSTailV1', ['?q', '(MgByteLV1 ?byte)'])])),
            B(K('String'), [text('String', '?t', '?s'), Tail('MgParseSTailV1', ['?q', '(MgStringLV1 ?s)'])]),
            B(K('QString'), [text('QString', '?t', '?s'), Tail('MgParseSTailV1', ['?q', '(MgQStringLV1 ?s)'])]),
            B(K('Nam'), [text('Nam', '?t', '?x'), penv('?x', '?pen'), P('(MgNameClassV1 ?pen ?cls)', ['?cls']),
                         Case('?cls', [
                             B('MgOrdinaryClsV1', [Tail('MgParseSTailV1', ['?q', '(MgNaLV1 ?x)'])]),
                             B('(MgPrefixClsV1 ?p)', [P('(MgLevelLeqV1 ?q ?p ?le)', ['?le']),
                                                      Case('?le', [B('MgNoV1', [P('(MgLevelSuccV1 ?p ?p1)', ['?p1']),
                                                                                Call('MgParseSV1', ['?p1'], '?a'),
                                                                                P('(MgBinderishV1 ?a ?bi)', ['?bi']),
                                                                                Case('?bi', [B('MgYesV1', [Ret('(MgPreoLV1 ?x ?a)')]),
                                                                                             B('MgNoV1', [Tail('MgParseSTailV1', ['?q', '(MgPreoLV1 ?x ?a)'])])])]),
                                                                   B('MgYesV1', [Fail(['Prefix operator ', '?x', ' needs parenthesis here'])])])], ['?p']),
                             B('(MgBinderClsV1 ?mtok)', [
                                 Keep('?li'), Read('?t0'), kind('?t0', '?k0'), P('(MgNamOrAscV1 ?k0 ?na)', ['?na']),
                                 Case('?na', [
                                     B('MgYesV1', [Push('?t0'), Call('MgParseNamesV1', [], '?xs')] + binder_after_names('?x', '?mtok', '?li')),
                                     B('MgNoV1', [Push('?t0'), Call('MgParseTVsV1', [], '?vs'), Read('?u'),
                                                  P('(MgSameTokenV1 ?u ?mtok ?same)', ['?same']),
                                                  Case('?same', [B('MgYesV1', [Call('MgParseSV1', [L1010], '?body'),
                                                                               P('(MgBinderSeparatorV1 ?mtok ?sep)', ['?sep']),
                                                                               Ret('(MgBiLV1 ?x ?sep ?vs ?body)')]),
                                                                 B('MgNoV1', [Fail(['Ill-formed Binder ', '?x'], '?li')])])])])], ['?mtok']),
                             B('MgInfixClsV1', [Fail(['Unexpected infix operator ', '?x'])])])]),
            B(K('Let'), [Keep('?li'), Read('?t0'), Read('?t1'), kind('?t0', '?k0'), kind('?t1', '?k1'),
                         Case('?k0', [
                             B(K('Nam'), [text('Nam', '?t0', '?x'),
                                          Case('?k1', [B(K('Deq'), [Call('MgParseSV1', [L1010], '?a'),
                                                                    Expect(tk('In'), Fail('Ill-formed let', '?li')),
                                                                    Call('MgParseSV1', [L1010], '?c'), Ret('(MgLeLV1 ?x MgNoneV1 ?a ?c)')])] +
                                               [B(K(a), [Call('MgParseSV1', [L1010], '?b'), Expect(tk('Deq'), Fail('Ill-formed let', '?li')),
                                                         Call('MgParseSV1', [L1010], '?a'), Expect(tk('In'), Fail('Ill-formed let', '?li')),
                                                         Call('MgParseSV1', [L1010], '?c'),
                                                         Ret('(MgLeLV1 ?x (MgSomeV1 (MgPairV1 %s ?b)) ?a ?c)' % asc)]) for a, asc in ASC],
                                               default=[Fail('Ill-formed let', '?li')])]),
                             B(K('LBrack'), [Case('?k1', [B(K('Nam'), [text('Nam', '?t1', '?x'), Call('MgParseCommaNamesV1', [], '?xs'),
                                                                       Expect(tk('RBrack'), Fail('Ill-formed let, expected ] here', '?li')),
                                                                       Expect(tk('Deq'), Fail('Ill-formed let, expected := here', '?li')),
                                                                       Call('MgParseSV1', [L1010], '?a'),
                                                                       Expect(tk('In'), Fail("Ill-formed let, expected 'in' here", '?li')),
                                                                       Call('MgParseSV1', [L1010], '?b'),
                                                                       Ret('(MgLeMLV1 ?x ?xs ?a ?b)')])],
                                                         default=[Fail('Ill-formed let', '?li')])])],
                              default=[Fail('Ill-formed let', '?li')])]),
            B(K('LParen'), [Keep('?lp'), Call('MgParseSV1', [L1023], '?a'), Call('MgParseNV1', [], '?as'),
                            Expect(tk('RParen'), Fail('Unmatched (', '?lp')),
                            Tail('MgParseSTailV1', ['?q', '(MgParenLV1 ?a ?as)'])]),
            B(K('Num'), [P('(MgNumTreeV1 ?t ?n)', ['?n']), Tail('MgParseSTailV1', ['?q', '?n'])]),
            B(K('LBrack'), [Keep('?lp'), Call('MgParseSV1', [L1023], '?a'), Call('MgParseNV1', [], '?as'),
                            Expect(tk('RBrack'), Fail('Unmatched [', '?lp')),
                            Tail('MgParseSTailV1', ['?q', '(MgMTupleLV1 ?a ?as)'])]),
            # parse_S_ keeps the position just after {
            B(K('LCBrace'), [Keep('?lp'), Read('?u'), kind('?u', '?ku'),
                             Case('?ku', [B(K('RCBrace'), [Tail('MgParseSTailV1', ['?q', '(MgSetEnumLV1 MgListNilV1)'])])],
                                  default=[Push('?u'), Tail('MgParseSetBracesV1', ['?q', 'MgWrapIdV1', '?lp'])])]),
            B(K('If'), [Keep('?li'), Call('MgParseSV1', [L1010], '?a'), Expect(tk('Then'), Fail('Missing then for if', '?li')),
                        Call('MgParseSV1', [L1010], '?b'), Expect(tk('Else'), Fail('Missing else for if', '?li')),
                        Call('MgParseSV1', [L1010], '?c'), Ret('(MgIfThenElseLV1 ?a ?b ?c)')]),
        ], default=[Fail('Unwritten case')])])

    # parse_S'_ q a
    C.function('MgParseSTailV1', ['?q', '?a'], [
        P('(MgLevelZeroV1 ?q ?z)', ['?z']),
        Case('?z', [
            B('MgYesV1', [Ret('?a')]),
            B('MgNoV1', [
                Read('?t'), kind('?t', '?k'),
                Case('?k', [
                    B(K('Mem'), [Tail('MgParseInfixV1', ['?q', L500, L500, '(MgInfSetV1 MgInfMemV1)', '?a', '?t'])]),
                    B(K('Subeq'), [Tail('MgParseInfixV1', ['?q', L500, L500, '(MgInfSetV1 MgInfSubqV1)', '?a', '?t'])]),
                    B(K('Backslash'), hex_byte([Tail('MgParseSTailV1', ['?q', '(MgImplopLV1 ?a (MgByteLV1 ?byte))'])])),
                    B(K('String'), [text('String', '?t', '?s'), Tail('MgParseSTailV1', ['?q', '(MgImplopLV1 ?a (MgStringLV1 ?s))'])]),
                    B(K('QString'), [text('QString', '?t', '?s'), Tail('MgParseSTailV1', ['?q', '(MgImplopLV1 ?a (MgQStringLV1 ?s))'])]),
                    B(K('Nam'), [text('Nam', '?t', '?x'), penv('?x', '?pen'), P('(MgTailClassV1 ?pen ?cls)', ['?cls']),
                                 Case('?cls', [
                                     B('(MgTailOpV1 ?p MgPostfixV1)', [P('(MgLevelLeqV1 ?q ?p ?le)', ['?le']),
                                                                        Case('?le', [B('MgYesV1', [Push('?t'), Ret('?a')]),
                                                                                     B('MgNoV1', [Tail('MgParseSTailV1', ['?q', '(MgPostoLV1 ?x ?a)'])])])], ['?p']),
                                     B('(MgTailOpV1 ?p MgInfixNoneV1)', [Tail('MgParseInfixV1', ['?q', '?p', '?p', '(MgInfNamV1 ?x)', '?a', '?t'])], ['?p']),
                                     B('(MgTailOpV1 ?p MgInfixLeftV1)', [Tail('MgParseInfixV1', ['?q', '?p', '?p', '(MgInfNamV1 ?x)', '?a', '?t'])], ['?p']),
                                     B('(MgTailOpV1 ?p MgInfixRightV1)', [P('(MgLevelSuccV1 ?p ?p1)', ['?p1']),
                                                                          Tail('MgParseInfixV1', ['?q', '?p', '?p1', '(MgInfNamV1 ?x)', '?a', '?t'])], ['?p']),
                                     B('MgTailOrdinaryV1', [Tail('MgParseSTailV1', ['?q', '(MgImplopLV1 ?a (MgNaLV1 ?x))'])]),
                                     B('MgTailStopV1', [Push('?t'), Ret('?a')])])]),
                    B(K('Num'), [P('(MgNumTreeV1 ?t ?n)', ['?n']), Tail('MgParseSTailV1', ['?q', '(MgImplopLV1 ?a ?n)'])]),
                    B(K('LParen'), [Keep('?lp'), Call('MgParseSV1', [L1023], '?b'), Call('MgParseNV1', [], '?cs'),
                                    Expect(tk('RParen'), Fail('Unmatched (', '?lp')),
                                    Tail('MgParseSTailV1', ['?q', '(MgImplopLV1 ?a (MgParenLV1 ?b ?cs))'])]),
                    # parse_S'_ keeps the position after the token that follows {
                    B(K('LCBrace'), [Read('?u'), kind('?u', '?ku'), Keep('?lp'),
                                     Case('?ku', [B(K('RCBrace'), [Tail('MgParseSTailV1', ['?q', '(MgImplopLV1 ?a (MgSetEnumLV1 MgListNilV1))'])])],
                                          default=[Push('?u'), Tail('MgParseSetBracesV1', ['?q', '(MgWrapImplopV1 ?a)', '?lp'])])]),
                    B(K('LBrack'), [Keep('?lp'), Call('MgParseSV1', [L1023], '?c'), Call('MgParseNV1', [], '?bs'),
                                    Expect(tk('RBrack'), Fail('Unmatched [', '?lp')),
                                    Tail('MgParseSTailV1', ['?q', '(MgImplopLV1 ?a (MgMTupleLV1 ?c ?bs))'])]),
                ], default=[Push('?t'), Ret('?a')])])])])

    # parse_S'_Infix: the operator token ?t is already read
    C.function('MgParseInfixV1', ['?q', '?p', '?pr', '?i', '?a', '?t'], [
        P('(MgLevelLeqV1 ?q ?p ?le)', ['?le']),
        Case('?le', [
            B('MgYesV1', [Push('?t'), Ret('?a')]),
            B('MgNoV1', [Call('MgParseSV1', ['?pr'], '?b'), P('(MgBinderishV1 ?b ?bi)', ['?bi']),
                         Case('?bi', [B('MgYesV1', [Ret('(MgInfoLV1 ?i ?a ?b)')]),
                                      B('MgNoV1', [Tail('MgParseSTailV1', ['?q', '(MgInfoLV1 ?i ?a ?b)'])])])])])])


    def rep_after(op):
        return [Call('MgParseSV1', [L1023], '?b'), Read('?v'), kind('?v', '?kv'),
                Case('?kv', [B(K('RCBrace'), [P('(MgWrapV1 ?g (MgRepLV1 ?x %s ?a ?b) ?w)' % op, ['?w']), Tail('MgParseSTailV1', ['?q', '?w'])]),
                             B(K('Comma'), [Call('MgParseSV1', [L1023], '?c'),
                                            Expect(tk('RCBrace'), Fail('Missing } at the end of set formed by replacement and separation', '?lp')),
                                            P('(MgWrapV1 ?g (MgSepRepLV1 ?x %s ?a ?b ?c) ?w)' % op, ['?w']), Tail('MgParseSTailV1', ['?q', '?w'])])],
                     default=[Fail('Expected , or }', '?lp')])]


    # parse_S_SetBraces: after { whose next token is not }; ?lp is the
    # position its errors start from
    NO_BOUND = Fail('Missing bound on set formed by replacement', '?lp')
    C.function('MgParseSetBracesV1', ['?q', '?g', '?lp'], [
        Call('MgParseSV1', [L1023], '?a'), Read('?t'), kind('?t', '?k'),
        Case('?k', [
            B(K('Vbar'), [P('(MgSepShapeV1 ?a ?shape)', ['?shape']),
                          Case('?shape', [
                              B('(MgSeparationV1 ?x ?i ?a2)', [Call('MgParseSV1', [L1023], '?b'),
                                                              Expect(tk('RCBrace'), Fail('Missing } at the end of set formed by separation', '?lp')),
                                                              P('(MgWrapV1 ?g (MgSepLV1 ?x ?i ?a2 ?b) ?w)', ['?w']),
                                                              Tail('MgParseSTailV1', ['?q', '?w'])], ['?x', '?i', '?a2']),
                              B('MgNotSeparationV1', [Read('?t2'), Read('?t3'), kind('?t2', '?k2'), kind('?t3', '?k3'),
                                                      Case('?k2', [B(K('Nam'), [text('Nam', '?t2', '?x'),
                                                                               Case('?k3', [B(K('Mem'), rep_after('MgInfMemV1')),
                                                                                            B(K('Subeq'), rep_after('MgInfSubqV1'))],
                                                                                    default=[NO_BOUND])])],
                                                           default=[NO_BOUND])])])]),
        ], default=[Push('?t'), Call('MgParseNV1', [], '?bs'), Expect(tk('RCBrace'), Fail('Unmatched {', '?lp')),
                    P('(MgWrapV1 ?g (MgSetEnumLV1 (MgListConsV1 ?a ?bs)) ?w)', ['?w']),
                    Tail('MgParseSTailV1', ['?q', '?w'])])])

    # ---------------------------------------------------------------------------
    # Document items

    D = doc_mod
    CD = Compiler(D, errors=True)
    for n, a in [(tk('Nam'), 1), (tk('String'), 1), (tk('Num'), 4), (tk('Theorem'), 1), (tk('SpecComm'), 1),
                 ('MgTokKindV1', 2), ('MgNamTextV1', 2), ('MgStringTextV1', 2), ('MgTheoremTextV1', 2),
                 ('MgSpecCommTextV1', 2), ('MgParseSV1', 4), ('MgParseNamesV1', 3), ('MgPenvLookupV1', 3),
                 ('MgSignedV1', 2), ('MgPriorityValidV1', 2)]:
        D.op(n, a)
    for n, a in [('MgAuthorV1', 2), ('MgTitleV1', 1), ('MgSaltV1', 1), ('MgOpaqueV1', 1), ('MgTransparentV1', 1),
                 ('MgTreasureV1', 1), ('MgShowProofTermsV1', 1), ('MgSectionV1', 1), ('MgEndV1', 1),
                 ('MgVarDeclV1', 3), ('MgLetDeclV1', 3), ('MgHypDeclV1', 2), ('MgPostInfixDeclV1', 4),
                 ('MgPrefixDeclV1', 3), ('MgBinderDeclV1', 5), ('MgNotationDeclV1', 2), ('MgUnicodeDeclV1', 2),
                 ('MgSubscriptDeclV1', 1), ('MgSuperscriptDeclV1', 1), ('MgParamDeclV1', 2), ('MgDefDeclV1', 3),
                 ('MgParamHashV1', 3), ('MgAxDeclV1', 2), ('MgThmDeclV1', 3)]:
        D.op(n, a)
    D.op('MgNumIntV1', 2)
    D.rule('mg-num-int', '(MgNumIntV1 (MgTkNumV1 ?n ?w MgNoneV1 MgNoneV1) (MgSomeV1 (MgSignedV1 ?n ?w)))')
    D.rule('mg-num-int-fraction', '(MgNumIntV1 (MgTkNumV1 ?n ?w (MgSomeV1 ?f) ?e) MgNoneV1)')
    D.rule('mg-num-int-exponent', '(MgNumIntV1 (MgTkNumV1 ?n ?w MgNoneV1 (MgSomeV1 ?e)) MgNoneV1)')
    D.op('MgAssocNameV1', 2)
    D.rule('mg-assoc-left', '(MgAssocNameV1 %s (MgSomeV1 MgInfixLeftV1))' % lit('left'))
    D.rule('mg-assoc-right', '(MgAssocNameV1 %s (MgSomeV1 MgInfixRightV1))' % lit('right'))
    D.rule('mg-assoc-other', '(MgAssocNameV1 ?y MgNoneV1)', ['(different ?y %s)' % lit('left'), '(different ?y %s)' % lit('right')])
    D.op('MgSpecCommKindV1', 2)
    D.rule('mg-spec-comm-show', '(MgSpecCommKindV1 %s (MgSomeV1 MgTrueV1))' % lit('ShowProofTerms'))
    D.rule('mg-spec-comm-hide', '(MgSpecCommKindV1 %s (MgSomeV1 MgFalseV1))' % lit('HideProofTerms'))
    D.rule('mg-spec-comm-other', '(MgSpecCommKindV1 ?s MgNoneV1)', ['(different ?s %s)' % lit('ShowProofTerms'), '(different ?s %s)' % lit('HideProofTerms')])
    D.op('MgRightFiveHundredV1', 3)


    def dP(premise, binds):
        return Pure(premise, binds)


    def dkind(t, k):
        return dP('(MgTokKindV1 %s %s)' % (t, k), [k])


    # read_name_ts, read_names_ts, read_string_ts, read_strings_ts, read_int_ts
    CD.function('MgReadNameV1', [], [Read('?t'), dkind('?t', '?k'),
                                      Case('?k', [B(K('Nam'), [dP('(MgNamTextV1 ?t ?x)', ['?x']), Ret('?x')])],
                                           default=[Fail('Name expected')])])
    CD.function('MgReadNamesV1', [], [Read('?t'), dkind('?t', '?k'),
                                       Case('?k', [B(K('Nam'), [dP('(MgNamTextV1 ?t ?x)', ['?x']), Call('MgReadNamesV1', [], '?xs'), Ret('(MgListConsV1 ?x ?xs)')])],
                                            default=[Push('?t'), Ret('MgListNilV1')])])
    CD.function('MgReadStringV1', [], [Read('?t'), dkind('?t', '?k'),
                                        Case('?k', [B(K('String'), [dP('(MgStringTextV1 ?t ?x)', ['?x']), Ret('?x')])],
                                             default=[Fail('String (in double quotes) expected')])])
    CD.function('MgReadStringsV1', [], [Read('?t'), dkind('?t', '?k'),
                                         Case('?k', [B(K('String'), [dP('(MgStringTextV1 ?t ?x)', ['?x']), Call('MgReadStringsV1', [], '?xs'), Ret('(MgListConsV1 ?x ?xs)')])],
                                              default=[Push('?t'), Ret('MgListNilV1')])])
    CD.function('MgReadIntV1', [], [Read('?t'), dkind('?t', '?k'),
                                     Case('?k', [B(K('Num'), [dP('(MgNumIntV1 ?t ?n)', ['?n']),
                                                              Case('?n', [B('(MgSomeV1 ?s)', [Ret('?s')], ['?s']),
                                                                          B('MgNoneV1', [Fail('Integer expected')])])])],
                                          default=[Fail('Integer expected')])])
    LTR = [L1023]


    def ltree(out):
        return Call('MgParseSV1', LTR, out)


    def comment_item(tokname, stmts):
        return B(K(tokname), stmts)


    # parse_docitem: an error between li, ch (where the item began) and here
    # is start='item'.
    ITEM_ERR = 'item'
    SEP_ERR = 'The token (either , or =>) separating the bound variables from the body is expected here.'
    CD.function('MgParseDocItemV1', [], [
        Read('?t'), dkind('?t', '?k'),
        Case('?k', [
            B(K('OpenCom'), [Read('?u'), dkind('?u', '?ku'),
                             Case('?ku', [
                                 B(K('Param'), [Call('MgReadNameV1', [], '?x'), Call('MgReadStringV1', [], '?y'), Read('?v'), dkind('?v', '?kv'),
                                                Case('?kv', [B(K('CloseCom'), [Ret('(MgParamHashV1 ?x ?y MgNoneV1)')]),
                                                             B(K('String'), [dP('(MgStringTextV1 ?v ?z)', ['?z']), X('CloseCom'),
                                                                             Ret('(MgParamHashV1 ?x ?y (MgSomeV1 ?z))')])],
                                                     default=[Fail('Syntax error: expected *) or string')])]),
                                 B(K('Unicode'), [Call('MgReadNameV1', [], '?x'), Call('MgReadStringsV1', [], '?ys'), X('CloseCom'),
                                                  Ret('(MgUnicodeDeclV1 ?x ?ys)')]),
                                 B(K('Subscript'), [Call('MgReadNameV1', [], '?x'), X('CloseCom'), Ret('(MgSubscriptDeclV1 ?x)')]),
                                 B(K('Superscript'), [Call('MgReadNameV1', [], '?x'), X('CloseCom'), Ret('(MgSuperscriptDeclV1 ?x)')]),
                                 B(K('Author'), [Call('MgReadStringV1', [], '?x'), Call('MgReadStringsV1', [], '?ys'), X('CloseCom'),
                                                 Ret('(MgAuthorV1 ?x ?ys)')]),
                                 B(K('Title'), [Call('MgReadStringV1', [], '?x'), X('CloseCom'), Ret('(MgTitleV1 ?x)')]),
                                 B(K('SpecComm'), [dP('(MgSpecCommTextV1 ?u ?s)', ['?s']), dP('(MgSpecCommKindV1 ?s ?show)', ['?show']),
                                                   Case('?show', [B('(MgSomeV1 ?b)', [X('CloseCom'), Ret('(MgShowProofTermsV1 ?b)')], ['?b']),
                                                                  B('MgNoneV1', [Fail('Syntax error', ITEM_ERR)])])]),
                                 B(K('Salt'), [Call('MgReadStringV1', [], '?x'), X('CloseCom'), Ret('(MgSaltV1 ?x)')]),
                                 B(K('Treasure'), [Call('MgReadStringV1', [], '?x'), X('CloseCom'), Ret('(MgTreasureV1 ?x)')]),
                             ], default=[Fail('Syntax error', ITEM_ERR)])]),
            B(K('Opaque'), [Call('MgReadNamesV1', [], '?xs'), X('Dot'), Ret('(MgOpaqueV1 ?xs)')]),
            B(K('Transparent'), [Call('MgReadNamesV1', [], '?xs'), X('Dot'), Ret('(MgTransparentV1 ?xs)')]),
            B(K('Param'), [Call('MgReadNameV1', [], '?x'), X('Colon'), ltree('?a'), X('Dot'), Ret('(MgParamDeclV1 ?x ?a)')]),
            B(K('Def'), [Call('MgReadNameV1', [], '?x'), Read('?u'), dkind('?u', '?ku'),
                         Case('?ku', [B(K('Colon'), [ltree('?a'), X('Deq'), ltree('?b'), X('Dot'),
                                                     Ret('(MgDefDeclV1 ?x (MgSomeV1 ?a) ?b)')]),
                                      B(K('Deq'), [ltree('?b'), X('Dot'), Ret('(MgDefDeclV1 ?x MgNoneV1 ?b)')])],
                              default=[Fail('Syntax error in definition declaration', ITEM_ERR)])]),
            B(K('Infix'), [Call('MgReadNameV1', [], '?x'), Call('MgReadIntV1', [], '?s'), dP('(MgPriorityValidV1 ?s ?pv)', ['?pv']),
                           Case('?pv', [B('(MgSomeV1 ?p)', [
                               Read('?u'), dkind('?u', '?ku'),
                               Case('?ku', [
                                   B(K('Nam'), [dP('(MgNamTextV1 ?u ?y)', ['?y']), dP('(MgAssocNameV1 ?y ?assoc)', ['?assoc']),
                                                Case('?assoc', [
                                                    B('(MgSomeV1 MgInfixLeftV1)', [X('Deq'), ltree('?a'), X('Dot'),
                                                                                   Ret('(MgPostInfixDeclV1 ?x ?a (MgIntV1 ?p) MgInfixLeftV1)')]),
                                                    B('(MgSomeV1 MgInfixRightV1)', [dP('(MgRightFiveHundredV1 ?p %s ?five)' % L500, ['?five']),
                                                                                    Case('?five', [B('MgNoV1', [X('Deq'), ltree('?a'), X('Dot'),
                                                                                                                Ret('(MgPostInfixDeclV1 ?x ?a (MgIntV1 ?p) MgInfixRightV1)')]),
                                                                                                   B('MgYesV1', [Fail('Right associative infix operators cannot have priority 500.', ITEM_ERR)])])]),
                                                    B('MgNoneV1', [Fail(['Infix operator priority can have declared associativity left or right, not ', '?y'], ITEM_ERR)])])]),
                                   B(K('Deq'), [ltree('?a'), X('Dot'), Ret('(MgPostInfixDeclV1 ?x ?a (MgIntV1 ?p) MgInfixNoneV1)')])],
                                   default=[Fail('Syntax error in infix declaration. Expected Infix <name> <priority> [left|right] := <term>.', ITEM_ERR)])], ['?p']),
                                        B('MgNoneV1', [Fail('Infix operator priority must be an positive integer < 1000', ITEM_ERR)])])]),
            B(K('Postfix'), [Call('MgReadNameV1', [], '?x'), Call('MgReadIntV1', [], '?s'), dP('(MgPriorityValidV1 ?s ?pv)', ['?pv']),
                             Case('?pv', [B('(MgSomeV1 ?p)', [X('Deq'), ltree('?a'), X('Dot'),
                                                              Ret('(MgPostInfixDeclV1 ?x ?a (MgIntV1 ?p) MgPostfixV1)')], ['?p']),
                                          B('MgNoneV1', [Fail('Postfix operator priority must be an positive integer < 1000', ITEM_ERR)])])]),
            B(K('Prefix'), [Call('MgReadNameV1', [], '?x'), Call('MgReadIntV1', [], '?s'), dP('(MgPriorityValidV1 ?s ?pv)', ['?pv']),
                            Case('?pv', [B('(MgSomeV1 ?p)', [dP('(MgRightFiveHundredV1 ?p %s ?five)' % L500, ['?five']),
                                                             Case('?five', [B('MgNoV1', [X('Deq'), ltree('?a'), X('Dot'),
                                                                                         Ret('(MgPrefixDeclV1 ?x ?a (MgIntV1 ?p))')]),
                                                                            B('MgYesV1', [Fail('Prefix operator priority must be an positive integer, not 500, and < 1000', ITEM_ERR)])])], ['?p']),
                                         B('MgNoneV1', [Fail('Prefix operator priority must be an positive integer, not 500, and < 1000', ITEM_ERR)])])]),
        ] + [B(K(tok), [Call('MgReadNameV1', [], '?x'), Read('?u'), dkind('?u', '?ku'),
                        Case('?ku', [B(K(sep), [X('Deq'), ltree('?a'), Read('?v'), dkind('?v', '?kv'),
                                                Case('?kv', [B(K('Dot'), [Ret('(MgBinderDeclV1 %s %s ?x ?a MgNoneV1)' % (plus, comma))]),
                                                             B(K('Semicolon'), [ltree('?b'), X('Dot'),
                                                                                Ret('(MgBinderDeclV1 %s %s ?x ?a (MgSomeV1 ?b))' % (plus, comma))])],
                                                     default=[Fail('Expected . or ; here.', ITEM_ERR)])])
                                     for sep, comma in [('Comma', 'MgTrueV1'), ('Darr', 'MgFalseV1')]],
                             default=[Fail(SEP_ERR, ITEM_ERR)])])
             for tok, plus in [('Binder', 'MgFalseV1'), ('BinderPlus', 'MgTrueV1')]] + [
            B(K('Notation'), [Call('MgReadNameV1', [], '?x'), Call('MgParseNamesV1', [], '?ys'), X('Dot'), Ret('(MgNotationDeclV1 ?x ?ys)')]),
            B(K('Section'), [Call('MgReadNameV1', [], '?x'), X('Dot'), Ret('(MgSectionV1 ?x)')]),
            B(K('End'), [Call('MgReadNameV1', [], '?x'), X('Dot'), Ret('(MgEndV1 ?x)')]),
            B(K('Var'), [Call('MgParseNamesV1', [], '?xs'), Read('?u'), dkind('?u', '?ku'),
                         Case('?ku', [B(K(a), [ltree('?a'), X('Dot'), Ret('(MgVarDeclV1 ?xs %s ?a)' % asc)]) for a, asc in ASC],
                              default=[Fail('Missing ascription in Variable declaration.', ITEM_ERR)])]),
            B(K('Hyp'), [Call('MgReadNameV1', [], '?x'), X('Colon'), ltree('?a'), X('Dot'), Ret('(MgHypDeclV1 ?x ?a)')]),
            B(K('LetDec'), [Call('MgReadNameV1', [], '?x'), Read('?u'), dkind('?u', '?ku'),
                            Case('?ku', [B(K(a), [ltree('?a'), X('Deq'), ltree('?b'), X('Dot'),
                                                  Ret('(MgLetDeclV1 ?x (MgSomeV1 (MgPairV1 %s ?a)) ?b)' % asc)]) for a, asc in ASC] +
                                 [B(K('Deq'), [ltree('?a'), X('Dot'), Ret('(MgLetDeclV1 ?x MgNoneV1 ?a)')])],
                                 default=[Fail('Syntax error in Let declaration.', ITEM_ERR)])]),
            B(K('Axiom'), [Call('MgReadNameV1', [], '?x'), X('Colon'), ltree('?a'), X('Dot'), Ret('(MgAxDeclV1 ?x ?a)')]),
            B(K('Theorem'), [dP('(MgTheoremTextV1 ?t ?c)', ['?c']), Call('MgReadNameV1', [], '?x'), X('Colon'), ltree('?a'),
                             X('Dot'), Ret('(MgThmDeclV1 ?c ?x ?a)')]),
        ], default=[Fail('Expected a Document Item', ITEM_ERR)])])
    # p = 500 refuses right infix operators and prefix operators
    D.rule('mg-right-500', '(MgRightFiveHundredV1 ?p ?p MgYesV1)')
    D.rule('mg-right-other', '(MgRightFiveHundredV1 ?p ?q MgNoV1)', ['(different ?p ?q)'])

    # ---------------------------------------------------------------------------
    # Proof tactics

    F = pf_mod
    CF = Compiler(F, errors=True)
    for n, a in [(tk('Nam'), 1), (tk('Num'), 4), ('MgTokKindV1', 2), ('MgNamTextV1', 2), ('MgParseSV1', 4),
                 ('MgParseNV1', 3), ('MgParseNamesV1', 3), ('MgReadNameV1', 3), ('MgNumIntV1', 2), ('MgSignedV1', 2),
                 ('MgStripZerosV1', 2), ('MgLevelZeroV1', 2)]:
        F.op(n, a)
    for n, a in [('MgPfStructV1', 1), ('MgExactV1', 1), ('MgLetTacV1', 2), ('MgAssumeTacV1', 2), ('MgSetTacV1', 3),
                 ('MgApplyTacV1', 1), ('MgClaimTacV1', 2), ('MgProveTacV1', 2), ('MgCasesTacV1', 2), ('MgWitnessTacV1', 1),
                 ('MgRewriteTacV1', 3), ('MgSpecialTacV1', 2), ('MgAbyV1', 1), ('MgCaseReadV1', 2)]:
        F.op(n, a)
    TAC_NAMES = ['-', '+', '*', 'set', 'cases', 'witness', 'rewrite', 'distinct', 'reflexivity', 'symmetry',
                 'transitivity', 'f_equal']
    F.op('MgTacticNameV1', 2); F.op('MgTacNameV1', 1)
    for i, n in enumerate(TAC_NAMES):
        F.rule('mg-tactic-name-%d' % i, '(MgTacticNameV1 %s (MgTacNameV1 %s))' % (lit(n), cp(chr(65 + i))))
    F.rule('mg-tactic-name-other', '(MgTacticNameV1 ?x MgTacNameOtherV1)', ['(different ?x %s)' % lit(n) for n in TAC_NAMES])
    F.op('MgReverseArrowV1', 2)
    F.rule('mg-reverse-arrow', '(MgReverseArrowV1 (MgTkNamV1 %s) MgYesV1)' % lit('<-'))
    F.rule('mg-reverse-arrow-no', '(MgReverseArrowV1 ?t MgNoV1)', ['(different ?t (MgTkNamV1 %s))' % lit('<-')])
    F.op('MgPositionV1', 2)
    F.rule('mg-position', '(MgPositionV1 (MgSomeV1 (MgSignedV1 MgFalseV1 ?d)) (MgSomeV1 ?d))')
    F.rule('mg-position-negative', '(MgPositionV1 (MgSomeV1 (MgSignedV1 MgTrueV1 ?d)) MgNoneV1)')
    F.rule('mg-position-not-int', '(MgPositionV1 MgNoneV1 MgNoneV1)')


    def fkind(t, k):
        return Pure('(MgTokKindV1 %s %s)' % (t, k), [k])


    def tacname(i):
        return '(MgTacNameV1 %s)' % cp(chr(65 + i))


    # the positions after `at`, ended by `.`, with parser.ml's messages for
    # rewrite <- (ARROW) and rewrite; a zero position has the <- message in
    # both, as there
    ZERO_POSITION = 'Syntax error: expected rewrite <- <pftrm> at <posints>.'
    for arrow, fname in [(True, 'MgRewritePositionsArrowV1'), (False, 'MgRewritePositionsV1')]:
        bad = 'Syntax error: expected rewrite %s<pftrm> at <posints>.' % ('<- ' if arrow else '')
        CF.function(fname, [], [
            Read('?t'), fkind('?t', '?k'),
            Case('?k', [B(K('Num'), [Pure('(MgNumIntV1 ?t ?n)', ['?n']), Pure('(MgPositionV1 ?n ?pos)', ['?pos']),
                                     Case('?pos', [B('(MgSomeV1 ?d)', [Pure('(MgStripZerosV1 ?d ?i)', ['?i']), Pure('(MgLevelZeroV1 ?i ?z)', ['?z']),
                                                                       Case('?z', [B('MgNoV1', [Call(fname, [], '?is'),
                                                                                               Ret('(MgListConsV1 (MgIntV1 ?i) ?is)')]),
                                                                                   B('MgYesV1', [Fail(ZERO_POSITION, 'item')])])], ['?d']),
                                                   B('MgNoneV1', [Fail(bad, 'item')])])]),
                        B(K('Dot'), [Ret('MgListNilV1')])],
                 default=[Fail(bad, 'item')])])


    def rewrite(rev, arrow):
        positions = 'MgRewritePositionsArrowV1' if arrow else 'MgRewritePositionsV1'
        return [Call('MgParseSV1', [L1023], '?a'), Read('?w'), fkind('?w', '?kw'),
                Case('?kw', [B(K('Dot'), [Ret('(MgRewriteTacV1 %s ?a MgListNilV1)' % rev)]),
                             B(K('At'), [Call(positions, [], '?is'), Ret('(MgRewriteTacV1 %s ?a ?is)' % rev)])],
                     default=[Fail('Syntax error: expected rewrite %s<pftrm> [at <posints>].' % ('<- ' if arrow else ''), 'item')])]


    # read_case: name : term, then , and the next case, or the token that ends
    # the group; the case before that token is dropped, as in parser.ml.
    CF.function('MgReadCaseV1', [], [
        Call('MgReadNameV1', [], '?x'), X('Colon'), Call('MgParseSV1', [L1023], '?b'), Read('?t'), fkind('?t', '?k'),
        Case('?k', [B(K('Comma'), [Call('MgReadCaseV1', [], '(MgCaseReadV1 ?xbs ?end)', ['?xbs', '?end']),
                                   Ret('(MgCaseReadV1 (MgListConsV1 (MgPairV1 ?x ?b) ?xbs) ?end)')])],
             default=[Ret('(MgCaseReadV1 MgListNilV1 ?t)')])])
    CF.function('MgReadCasesV1', [], [
        Call('MgReadCaseV1', [], '(MgCaseReadV1 ?xbs ?end)', ['?xbs', '?end']), fkind('?end', '?k'),
        Case('?k', [B(K('Vbar'), [Call('MgReadCasesV1', [], '?groups'), Ret('(MgListConsV1 ?xbs ?groups)')]),
                    B(K('Dot'), [Ret('MgListNilV1')])],
             default=[Fail('Expected | or .')])])

    TAC_ERR = 'item'
    CF.function('MgParsePfTacItemV1', [], [
        Read('?t'), fkind('?t', '?k'),
        Case('?k', [
            B(K('Nam'), [Pure('(MgNamTextV1 ?t ?x)', ['?x']), Pure('(MgTacticNameV1 ?x ?n)', ['?n']),
                         Case('?n', [
                             B(tacname(0), [Ret('(MgPfStructV1 (MgIntV1 %s))' % lit('1'))]),
                             B(tacname(1), [Ret('(MgPfStructV1 (MgIntV1 %s))' % lit('2'))]),
                             B(tacname(2), [Ret('(MgPfStructV1 (MgIntV1 %s))' % lit('3'))]),
                             B(tacname(3), [Call('MgReadNameV1', [], '?y'), Read('?u'), fkind('?u', '?ku'),
                                            Case('?ku', [B(K('Colon'), [Call('MgParseSV1', [L1023], '?a'), X('Deq'),
                                                                        Call('MgParseSV1', [L1023], '?b'), X('Dot'),
                                                                        Ret('(MgSetTacV1 ?y (MgSomeV1 ?a) ?b)')]),
                                                         B(K('Deq'), [Call('MgParseSV1', [L1023], '?b'), X('Dot'),
                                                                      Ret('(MgSetTacV1 ?y MgNoneV1 ?b)')])],
                                                 default=[Fail('Expected set <name> [: <tp>] := <tm>', TAC_ERR)])]),
                             B(tacname(4), [Call('MgParseSV1', [L1023], '?a'), X('Colon'), Call('MgReadCasesV1', [], '?groups'),
                                            Ret('(MgCasesTacV1 ?a ?groups)')]),
                             B(tacname(5), [Call('MgParseSV1', [L1023], '?a'), X('Dot'), Ret('(MgWitnessTacV1 ?a)')]),
                             B(tacname(6), [Read('?u'), Pure('(MgReverseArrowV1 ?u ?rev)', ['?rev']),
                                            Case('?rev', [B('MgYesV1', rewrite('MgTrueV1', True)),
                                                          B('MgNoV1', [Push('?u')] + rewrite('MgFalseV1', False))])]),
                         ] + [B(tacname(i), [X('Dot'), Ret('(MgSpecialTacV1 %s MgListNilV1)' % lit(TAC_NAMES[i]))])
                              for i in (7, 8, 9, 11)] + [
                             B(tacname(10), [Call('MgParseSV1', [L1023], '?a'), Call('MgParseNV1', [], '?as'), X('Dot'),
                                             Ret('(MgSpecialTacV1 %s (MgListConsV1 ?a ?as))' % lit('transitivity'))]),
                             B('MgTacNameOtherV1', [Fail('Expected a Proof Tactic', TAC_ERR)])])]),
            B(K('LCBrace'), [Ret('(MgPfStructV1 (MgIntV1 %s))' % lit('4'))]),
            B(K('RCBrace'), [Ret('(MgPfStructV1 (MgIntV1 %s))' % lit('5'))]),
            B(K('Qed'), [X('Dot'), Ret('MgQedV1')]),
            B(K('Admitted'), [X('Dot'), Ret('MgAdmittedV1')]),
            B(K('Admit'), [X('Dot'), Ret('MgAdmitV1')]),
            B(K('Aby'), [Call('MgParseNamesV1', [], '?xs'), X('Dot'), Ret('(MgAbyV1 ?xs)')]),
            B(K('Exact'), [Call('MgParseSV1', [L1023], '?a'), X('Dot'), Ret('(MgExactV1 ?a)')]),
        ] + [B(K(tok), [Call('MgParseNamesV1', [], '?xs'), Read('?u'), fkind('?u', '?ku'),
                        Case('?ku', [B(K('Colon'), [Call('MgParseSV1', [L1023], '?a'), X('Dot'),
                                                    Ret('(%s ?xs (MgSomeV1 ?a))' % cons)]),
                                     B(K('Dot'), [Ret('(%s ?xs MgNoneV1)' % cons)])] + extra,
                             default=[Fail(expected, TAC_ERR)])])
             for tok, cons, expected, extra in [
                 ('Let', 'MgLetTacV1', 'Expected let <names> [: <tp>]', []),
                 ('Assume', 'MgAssumeTacV1', 'Expected assume <names> [: <prop>]',
                  [B(K('Mem'), [Fail('Confused by :e after assume; probably need a space between : and e. '
                                     'Expected assume <names> [: <prop>]', TAC_ERR)])])]] + [
            B(K('Apply'), [Call('MgParseSV1', [L1023], '?a'), X('Dot'), Ret('(MgApplyTacV1 ?a)')]),
            B(K('Prove'), [Call('MgParseSV1', [L1023], '?a'), Call('MgParseNV1', [], '?as'), X('Dot'), Ret('(MgProveTacV1 ?a ?as)')]),
            B(K('Claim'), [Call('MgReadNameV1', [], '?x'), X('Colon'), Call('MgParseSV1', [L1023], '?a'), X('Dot'),
                           Ret('(MgClaimTacV1 ?x ?a)')]),
        ], default=[Fail('Expected a Proof Tactic', TAC_ERR)])])

    # ---------------------------------------------------------------------------
    # The source fold

    M = fold_mod
    for n, a in [('MgParseDocItemV1', 3), ('MgParsePfTacItemV1', 3), ('MgInitialEnvV1', 1), ('MgNewFileEnvV1', 2),
                 ('MgEnterSectionV1', 3), ('MgEndSectionV1', 3), ('MgDeclarePostInfixV1', 5), ('MgDeclarePrefixV1', 4),
                 ('MgDeclareBinderV1', 4), ('MgIntV1', 1), ('MgDocV1', 1), ('MgPfTacV1', 1),
                 ('MgItemsConsV1', 2),
                 ('MgEffectOfV1', 2), ('MgSlotV1', 3), ('MgApplyEffectV1', 3),
                 ('MgEnterEffectV1', 1), ('MgLeaveEffectV1', 1), ('MgPostInfixEffectV1', 3),
                 ('MgPrefixEffectV1', 2), ('MgBinderEffectV1', 2),
                 ('MgModeAfterDocV1', 2), ('MgModeAfterTacV1', 2),
                 ('MgReadDocumentV1', 2), ('MgReadSignaturesV1', 3), ('MgReadWithSignaturesV1', 3),
                 ('MgFilesConsV1', 2)]:
        M.op(n, a)
    for n, a in [('MgAuthorV1', 2), ('MgTitleV1', 1), ('MgSaltV1', 1), ('MgOpaqueV1', 1), ('MgTransparentV1', 1),
                 ('MgTreasureV1', 1), ('MgShowProofTermsV1', 1), ('MgSectionV1', 1), ('MgEndV1', 1),
                 ('MgVarDeclV1', 3), ('MgLetDeclV1', 3), ('MgHypDeclV1', 2), ('MgPostInfixDeclV1', 4),
                 ('MgPrefixDeclV1', 3), ('MgBinderDeclV1', 5), ('MgNotationDeclV1', 2), ('MgUnicodeDeclV1', 2),
                 ('MgSubscriptDeclV1', 1), ('MgSuperscriptDeclV1', 1), ('MgParamDeclV1', 2), ('MgDefDeclV1', 3),
                 ('MgParamHashV1', 3), ('MgAxDeclV1', 2), ('MgThmDeclV1', 3)]:
        M.op(n, a)
    DOCITEMS = [('Author', 2), ('Title', 1), ('Salt', 1), ('Opaque', 1), ('Transparent', 1), ('Treasure', 1),
                ('ShowProofTerms', 1), ('Section', 1), ('End', 1), ('VarDecl', 3), ('LetDecl', 3), ('HypDecl', 2),
                ('PostInfixDecl', 4), ('PrefixDecl', 3), ('BinderDecl', 5), ('NotationDecl', 2), ('UnicodeDecl', 2),
                ('SubscriptDecl', 1), ('SuperscriptDecl', 1), ('ParamDecl', 2), ('DefDecl', 3), ('ParamHash', 3),
                ('AxDecl', 2), ('ThmDecl', 3)]
    # A document item's effect on the notation environment, as megalodon.ml
    # evaluates it: what the item says (MgEffectOfV1), applied to the
    # environment (MgApplyEffectV1).
    EFFECTS = {
        'Section': ('(MgSectionV1 ?x)', '(MgEnterEffectV1 ?x)', ['(MgEnterSectionV1 ?env ?x ?out)']),
        'End': ('(MgEndV1 ?x)', '(MgLeaveEffectV1 ?x)', ['(MgEndSectionV1 ?env ?x ?out)']),
        'PostInfixDecl': ('(MgPostInfixDeclV1 ?x ?a (MgIntV1 ?p) ?pic)', '(MgPostInfixEffectV1 ?x ?p ?pic)',
                          ['(MgDeclarePostInfixV1 ?env ?x ?p ?pic ?out)']),
        'PrefixDecl': ('(MgPrefixDeclV1 ?x ?a (MgIntV1 ?p))', '(MgPrefixEffectV1 ?x ?p)',
                       ['(MgDeclarePrefixV1 ?env ?x ?p ?out)']),
        'BinderDecl': ('(MgBinderDeclV1 ?plus ?comma ?x ?a ?b)', '(MgBinderEffectV1 ?x ?comma)',
                       ['(MgDeclareBinderV1 ?env ?x ?comma ?out)']),
    }
    for name, (pat, eff, body) in EFFECTS.items():
        M.rule('mg-apply-effect-%s' % name.lower(), '(MgApplyEffectV1 %s ?env ?out)' % eff, body)
    for name, arity in DOCITEMS:
        if name in EFFECTS:
            M.rule('mg-effect-of-%s' % name.lower(), '(MgEffectOfV1 %s %s)' % (EFFECTS[name][0], EFFECTS[name][1]))
        else:
            M.rule('mg-effect-of-%s' % name.lower(), '(MgEffectOfV1 %s MgNoEffectV1)' % ltree_pattern(name, arity))
        M.rule('mg-mode-after-%s' % name.lower(), '(MgModeAfterDocV1 %s %s)' % (
            ltree_pattern(name, arity), 'MgProvingV1' if name == 'ThmDecl' else 'MgDocModeV1'))
    TACS = [('PfStruct', 1), ('Exact', 1), ('LetTac', 2), ('AssumeTac', 2), ('SetTac', 3), ('ApplyTac', 1), ('ClaimTac', 2),
            ('ProveTac', 2), ('CasesTac', 2), ('WitnessTac', 1), ('RewriteTac', 3), ('SpecialTac', 2), ('Aby', 1)]
    for name, arity in TACS:
        M.rule('mg-mode-after-%s' % name.lower(), '(MgModeAfterTacV1 %s MgProvingV1)' % ltree_pattern(name, arity))
    M.rule('mg-mode-after-admit', '(MgModeAfterTacV1 MgAdmitV1 MgProvingV1)')
    M.rule('mg-mode-after-qed', '(MgModeAfterTacV1 MgQedV1 MgDocModeV1)')
    M.rule('mg-mode-after-admitted', '(MgModeAfterTacV1 MgAdmittedV1 MgDocModeV1)')
    for n in ['MgPfStructV1', 'MgExactV1', 'MgApplyTacV1', 'MgWitnessTacV1', 'MgAbyV1']:
        M.op(n, 1)
    for n in ['MgLetTacV1', 'MgAssumeTacV1', 'MgClaimTacV1', 'MgProveTacV1', 'MgCasesTacV1', 'MgSpecialTacV1']:
        M.op(n, 2)
    for n in ['MgSetTacV1', 'MgRewriteTacV1']:
        M.op(n, 3)

    # A reader is (MgReaderV1 mode stream env).  MgReadItemV1 answers the
    # next item and the reader after it, (MgReaderEndV1 mode env) when the
    # stream ends (what the next file of a run starts from), or
    # (MgReaderErrorV1 e).  A document item takes effect as megalodon.ml
    # evaluates it, before the next item is read: the reader keeps its effect
    # pending (MgPendingV1 effect env), and the next read applies it first, so
    # an effect that fails follows its item.  An item without an effect leaves
    # the environment as it is.
    for n, a in [('MgReaderV1', 3), ('MgReaderEndV1', 2), ('MgItemV1', 2), ('MgReadItemV1', 2),
                 ('MgReaderStartV1', 2), ('MgReaderNextFileV1', 3), ('MgReadAllV1', 2), ('MgReadAllAfterV1', 2),
                 ('MgSkipToEndV1', 2), ('MgSkipAfterV1', 2), ('MgReadAfterDocV1', 3), ('MgReadAfterTacV1', 3),
                 ('MgReaderErrorV1', 1), ('MgPendingV1', 2), ('MgResolveV1', 2), ('MgReadResolvedV1', 4),
                 ('MgErrorV1', 1)]:
        M.op(n, a)
    M.rule('mg-read-item', '(MgReadItemV1 (MgReaderV1 ?mode ?s ?slot) ?o)',
           ['(MgResolveV1 ?slot ?r)', '(MgReadResolvedV1 ?r ?mode ?s ?o)'])
    M.rule('mg-resolve-pending', '(MgResolveV1 (MgPendingV1 ?eff ?env) ?r)', ['(MgApplyEffectV1 ?eff ?env ?r)'])
    M.rule('mg-slot-none', '(MgSlotV1 MgNoEffectV1 ?env ?env)')
    for eff in ['(MgEnterEffectV1 ?x)', '(MgLeaveEffectV1 ?x)', '(MgPostInfixEffectV1 ?x ?p ?pic)',
                '(MgPrefixEffectV1 ?x ?p)', '(MgBinderEffectV1 ?x ?comma)']:
        M.rule('mg-slot-%s' % eff[3:eff.index('EffectV1')].lower(), '(MgSlotV1 %s ?env (MgPendingV1 %s ?env))' % (eff, eff))
    M.rule('mg-resolve-env', '(MgResolveV1 (MgEnvV1 ?tables ?secs) (MgEnvV1 ?tables ?secs))')
    M.rule('mg-read-resolved-error', '(MgReadResolvedV1 (MgErrorV1 ?e) ?mode ?s (MgReaderErrorV1 ?e))')
    M.rule('mg-read-doc', '(MgReadResolvedV1 (MgEnvV1 ?tables ?secs) MgDocModeV1 ?s ?o)',
           ['(MgParseDocItemV1 ?s (MgEnvV1 ?tables ?secs) ?r)', '(MgReadAfterDocV1 ?r (MgEnvV1 ?tables ?secs) ?o)'])
    M.rule('mg-read-tac', '(MgReadResolvedV1 (MgEnvV1 ?tables ?secs) MgProvingV1 ?s ?o)',
           ['(MgParsePfTacItemV1 ?s (MgEnvV1 ?tables ?secs) ?r)', '(MgReadAfterTacV1 ?r (MgEnvV1 ?tables ?secs) ?o)'])
    M.rule('mg-read-doc-eof', '(MgReadAfterDocV1 MgEofV1 ?env (MgReaderEndV1 MgDocModeV1 ?env))')
    M.rule('mg-read-doc-item', '(MgReadAfterDocV1 (MgParsedV1 ?item ?rest) ?env (MgItemV1 (MgDocV1 ?item) (MgReaderV1 ?mode ?rest ?slot)))',
           ['(MgModeAfterDocV1 ?item ?mode)', '(MgEffectOfV1 ?item ?eff)', '(MgSlotV1 ?eff ?env ?slot)'])
    M.rule('mg-read-doc-error', '(MgReadAfterDocV1 (MgErrorV1 ?e) ?env (MgReaderErrorV1 ?e))')
    M.rule('mg-read-tac-eof', '(MgReadAfterTacV1 MgEofV1 ?env (MgReaderEndV1 MgProvingV1 ?env))')
    M.rule('mg-read-tac-item', '(MgReadAfterTacV1 (MgParsedV1 ?tac ?rest) ?env (MgItemV1 (MgPfTacV1 ?tac) (MgReaderV1 ?mode ?rest ?env)))',
           ['(MgModeAfterTacV1 ?tac ?mode)'])
    M.rule('mg-read-tac-error', '(MgReadAfterTacV1 (MgErrorV1 ?e) ?env (MgReaderErrorV1 ?e))')
    M.rule('mg-reader-start', '(MgReaderStartV1 ?scalars (MgReaderV1 MgDocModeV1 (MgStreamV1 ?scalars MgEvNilV1) ?env))',
           ['(MgInitialEnvV1 ?env)'])
    # The next file of a run starts with no open section; its mode carries
    # over, as megalodon.ml's does.
    M.rule('mg-reader-next-file', '(MgReaderNextFileV1 (MgReaderEndV1 ?mode ?env) ?scalars (MgReaderV1 ?mode (MgStreamV1 ?scalars MgEvNilV1) ?env1))',
           ['(MgNewFileEnvV1 ?env ?env1)'])
    M.rule('mg-read-all', '(MgReadAllV1 ?reader ?items)', ['(MgReadItemV1 ?reader ?o)', '(MgReadAllAfterV1 ?o ?items)'])
    M.rule('mg-read-all-end', '(MgReadAllAfterV1 (MgReaderEndV1 ?m ?e) MgItemsNilV1)')
    M.rule('mg-read-all-item', '(MgReadAllAfterV1 (MgItemV1 ?item ?next) (MgItemsConsV1 ?item ?items))', ['(MgReadAllV1 ?next ?items)'])
    M.rule('mg-skip-to-end', '(MgSkipToEndV1 ?reader ?end)', ['(MgReadItemV1 ?reader ?o)', '(MgSkipAfterV1 ?o ?end)'])
    M.rule('mg-skip-end', '(MgSkipAfterV1 (MgReaderEndV1 ?m ?e) (MgReaderEndV1 ?m ?e))')
    M.rule('mg-skip-item', '(MgSkipAfterV1 (MgItemV1 ?item ?next) ?end)', ['(MgSkipToEndV1 ?next ?end)'])
    M.rule('mg-read-document', '(MgReadDocumentV1 ?scalars ?items)',
           ['(MgReaderStartV1 ?scalars ?reader)', '(MgReadAllV1 ?reader ?items)'])
    # Signatures (-I) are read to their end first, their items unused.
    M.rule('mg-read-signatures-nil', '(MgReadSignaturesV1 MgFilesNilV1 ?end ?end)')
    M.rule('mg-read-signatures-cons', '(MgReadSignaturesV1 (MgFilesConsV1 ?scalars ?more) ?end0 ?end)',
           ['(MgReaderNextFileV1 ?end0 ?scalars ?reader)', '(MgSkipToEndV1 ?reader ?end1)',
            '(MgReadSignaturesV1 ?more ?end1 ?end)'])
    M.rule('mg-read-with-signatures', '(MgReadWithSignaturesV1 ?signatures ?scalars ?items)',
           ['(MgInitialEnvV1 ?env)', '(MgReadSignaturesV1 ?signatures (MgReaderEndV1 MgDocModeV1 ?env) ?end)',
            '(MgReaderNextFileV1 ?end ?scalars ?reader)', '(MgReadAllV1 ?reader ?items)'])

    return {fname: m.text().lstrip('\n') for m, fname in [
        (env_mod, 'notation_environment_v1.metta'), (term_mod, 'term_parser_v1.metta'),
        (doc_mod, 'document_v1.metta'), (pf_mod, 'proof_script_v1.metta'),
        (fold_mod, 'source_fold_v1.metta')]}
