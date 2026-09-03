:- module(finite_horn_gslt_v1,
          [ read_presentation/2,
            read_presentation_text/3,
            read_ground_term_text/2,
            admit_presentations/1,
            canonical_text/2,
            package_digest/2,
            quote_rule/2,
            quote_presentation_rules/2,
            reflect_presentations/2,
            render_term/2
          ]).

:- use_module(library(crypto)).
:- use_module(library(assoc)).
:- use_module(library(pairs)).
:- use_module(library(readutil)).
:- use_module(library(utf8)).

/*
  Independent PeTTa/SWI reader for the finite Horn GSLT v1 interchange.

  The carrier contains primitive symbols, integers, strings, variables, and
  finite applications.  A package supplies positive-arity operator
  declarations and named Horn clauses.  The v1 fragment has identity
  equations only.  This module validates and canonicalizes that data; it is
  not a guest-language parser and contains no guest-language policy.
*/

read_presentation(Path, Presentation) :-
    read_file_to_string(Path, Text, [encoding(utf8)]),
    read_presentation_text(Text, Path, Presentation).

read_presentation_text(Text, Source, Presentation) :-
    string_codes(Text, Codes),
    ( phrase(sexprs(Forms), Codes) -> true
    ; schema_error('~w: malformed S-expression input', [Source])
    ),
    ( Forms = [Root] -> true
    ; length(Forms, Count),
      schema_error('~w: expected one gslt-presentation-v1 form, found ~d',
                   [Source, Count])
    ),
    parse_presentation_root(Root, Source, Presentation).

read_ground_term_text(Text, Term) :-
    string_codes(Text, Codes),
    ( phrase(sexprs(Forms), Codes) -> true
    ; schema_error('malformed finite-Horn ground term', [])
    ),
    ( Forms = [Term], ground(Term) -> true
    ; schema_error('expected exactly one finite-Horn ground term', [])
    ).

parse_presentation_root(
    list([sym('gslt-presentation-v1'), sym(Name)|RawFields]), Source,
    presentation(Name, Operators, Rules, Source)) :-
    !,
    require_nonempty_atom(Name, Source, 'presentation name'),
    parse_fields(RawFields, Source, Fields),
    require_field(signature, Fields, Signature),
    require_field(equations, Fields, Equations),
    require_field(rewrites, Fields, Rewrites),
    parse_operators(Signature, Source, Operators),
    ( Equations == [] -> true
    ; schema_error(
          '~w: v1 admits identity equations only; authored equations are unsupported',
          [Source])
    ),
    parse_rules(Rewrites, Source, Rules).
parse_presentation_root(_, Source, _) :-
    schema_error('~w: expected gslt-presentation-v1 root', [Source]).

parse_fields(RawFields, Source, Fields) :-
    parse_fields(RawFields, Source, [], Fields0),
    keysort(Fields0, Fields),
    pairs_keys(Fields, Keys),
    required_fields(Required),
    ( Keys == Required -> true
    ; findall(Missing,
              ( member(Missing, Required), \+ memberchk(Missing, Keys) ),
              MissingFields),
      ( MissingFields = [_|_] ->
          atomic_list_concat(MissingFields, ', ', MissingText),
          schema_error('~w: missing presentation fields: ~w',
                       [Source, MissingText])
      ; schema_error('~w: malformed presentation fields', [Source])
      )
    ).

parse_fields([], _, Fields, Fields).
parse_fields([list([sym(Tag)|Payload])|RawFields], Source, Fields0, Fields) :-
    !,
    required_fields(Required),
    ( memberchk(Tag, Required) -> true
    ; schema_error('~w: unknown presentation field ~w', [Source, Tag])
    ),
    ( memberchk(Tag-_, Fields0) ->
        schema_error('~w: duplicate presentation field ~w', [Source, Tag])
    ; true
    ),
    parse_fields(RawFields, Source, [Tag-Payload|Fields0], Fields).
parse_fields([_|_], Source, _, _) :-
    schema_error('~w: malformed presentation field', [Source]).

required_fields([equations, rewrites, signature]).

require_field(Name, Fields, Payload) :-
    memberchk(Name-Payload, Fields).

parse_operators(Forms, Source, Operators) :-
    maplist(parse_operator(Source), Forms, Operators),
    maplist(operator_key, Operators, Keys),
    duplicate_key(Keys, Duplicate),
    ( nonvar(Duplicate) ->
        Duplicate = Name-Arity,
        schema_error('~w: duplicate operator ~w/~d', [Source, Name, Arity])
    ; true
    ).

parse_operator(_Source, list([sym(operator), sym(Name), int(Arity)]),
               operator(Name, Arity)) :-
    atom(Name),
    Arity >= 0,
    !.
parse_operator(Source, _, _) :-
    schema_error(
        '~w: operator declaration must be (operator NAME NONNEGATIVE-ARITY)',
        [Source]).

operator_key(operator(Name, Arity), Name-Arity).

parse_rules(Forms, Source, Rules) :-
    maplist(parse_rule(Source), Forms, Rules),
    maplist(rule_name, Rules, Names),
    duplicate_key(Names, Duplicate),
    ( nonvar(Duplicate) ->
        schema_error('~w: duplicate rule name ~w', [Source, Duplicate])
    ; true
    ).

parse_rule(Source,
           list([sym(rule), sym(Name),
                 list([sym(head), Head]),
                 list([sym(body)|Body])]),
           rule(Name, Head, Body)) :-
    require_nonempty_atom(Name, Source, 'rule name'),
    !.
parse_rule(Source, Form, _) :-
    sexpr_text(Form, Text),
    schema_error('~w: malformed rule: ~s', [Source, Text]).

rule_name(rule(Name, _, _), Name).

/*
  Ground quotation for compiler input.  Source variables become local Peano
  indices in first-occurrence order across the head and then the body.  The
  quoted result is therefore alpha-stable and can be passed through the same
  first-order Horn interface without accidentally treating source variables as
  compiler metavariables.
*/

quote_presentation_rules(presentation(_, _, Rules0, _), QuotedRules) :-
    map_list_to_pairs(rule_sort_key, Rules0, RulePairs),
    keysort(RulePairs, SortedRulePairs),
    pairs_values(SortedRulePairs, Rules),
    maplist(quote_rule, Rules, QuotedRuleList),
    quoted_list(QuotedRuleList, QuotedRules).

quoted_list([], sym('q-nil')).
quoted_list([Head|Tail], list([sym('q-cons'), Head, QuotedTail])) :-
    quoted_list(Tail, QuotedTail).

reflect_presentations(Presentations,
                      presentation(ReflectionName, [], ReflectedRules,
                                   reflected(SourceDigest))) :-
    package_digest(Presentations, SourceDigest),
    format(atom(ReflectionName), 'ReflectedFiniteHorn_~w', [SourceDigest]),
    map_list_to_pairs(presentation_sort_key, Presentations, PresentationPairs),
    keysort(PresentationPairs, SortedPresentationPairs),
    pairs_values(SortedPresentationPairs, SortedPresentations),
    reflected_rules(SortedPresentations, SourceDigest, 0, _, ReflectedRules).

reflected_rules([], _, Index, Index, []).
reflected_rules([presentation(Name, _, Rules0, _)|Presentations],
                Digest, Index0, Index, ReflectedRules) :-
    map_list_to_pairs(rule_sort_key, Rules0, RulePairs),
    keysort(RulePairs, SortedRulePairs),
    pairs_values(SortedRulePairs, Rules),
    reflected_presentation_rules(
        Rules, Name, Digest, Index0, Index1, CurrentRules),
    reflected_rules(
        Presentations, Digest, Index1, Index, RemainingRules),
    append(CurrentRules, RemainingRules, ReflectedRules).

reflected_presentation_rules([], _, _, Index, Index, []).
reflected_presentation_rules([Rule|Rules], PresentationName, Digest,
                             Index0, Index,
                             [ReflectedRule|ReflectedRules]) :-
    quote_rule(Rule, QuotedRule),
    format(atom(ReflectedName), 'reflected-~w-~d', [Digest, Index0]),
    ReflectedRule =
        rule(ReflectedName,
             list([sym('source-rule'),
                   list([sym('q-sym'), sym(PresentationName)]),
                   QuotedRule]),
             []),
    Index1 is Index0 + 1,
    reflected_presentation_rules(
        Rules, PresentationName, Digest, Index1, Index, ReflectedRules).

quote_rule(rule(Name, Head, Body),
           list([sym('q-rule'),
                 list([sym('q-sym'), sym(Name)]),
                 QuotedHead,
                 QuotedBody])) :-
    empty_assoc(Empty),
    quote_term(Head, quote_state(Empty, 0), State1, QuotedHead),
    quote_terms(Body, State1, _, QuotedBody).

quote_terms([], State, State, sym('q-nil')).
quote_terms([Term|Terms], State0, State,
            list([sym('q-cons'), QuotedTerm, QuotedTerms])) :-
    quote_term(Term, State0, State1, QuotedTerm),
    quote_terms(Terms, State1, State, QuotedTerms).

quote_term(sym(Name), State, State,
           list([sym('q-sym'), sym(Name)])) :-
    !.
quote_term(int(Value), State, State,
           list([sym('q-int'), int(Value)])) :-
    !.
quote_term(str(Text), State, State,
           list([sym('q-str'), str(Text)])) :-
    !.
quote_term(var(Name), quote_state(Environment0, Next0), State,
           list([sym('q-var'), IndexTerm])) :-
    !,
    ( get_assoc(Name, Environment0, Index) ->
        Environment = Environment0,
        Next = Next0
    ; Index = Next0,
      put_assoc(Name, Environment0, Index, Environment),
      Next is Next0 + 1
    ),
    peano_index(Index, IndexTerm),
    State = quote_state(Environment, Next).
quote_term(list([sym(Name)|Arguments]), State0, State,
           list([sym('q-app'),
                 list([sym('q-sym'), sym(Name)]),
                 QuotedArguments])) :-
    !,
    quote_terms(Arguments, State0, State, QuotedArguments).
quote_term(Node, _, _, _) :-
    throw(error(malformed_horn_term(Node), finite_horn_gslt_v1)).

peano_index(0, sym('q-zero')) :-
    !.
peano_index(Index, list([sym('q-succ'), Previous])) :-
    Index > 0,
    Prior is Index - 1,
    peano_index(Prior, Previous).

admit_presentations(Presentations) :-
    ( Presentations = [_|_] -> true
    ; schema_error('at least one presentation is required', [])
    ),
    maplist(presentation_name, Presentations, Names),
    duplicate_key(Names, DuplicatePresentation),
    ( nonvar(DuplicatePresentation) ->
        schema_error('duplicate presentation name ~w', [DuplicatePresentation])
    ; true
    ),
    findall(Key,
            ( member(presentation(_, Operators, _, _), Presentations),
              member(Operator, Operators),
              operator_key(Operator, Key)
            ),
            RawOperatorKeys),
    sort(RawOperatorKeys, OperatorKeys),
    findall(Name,
            ( member(presentation(_, _, Rules, _), Presentations),
              member(Rule, Rules),
              rule_name(Rule, Name)
            ),
            RuleNames),
    duplicate_key(RuleNames, DuplicateRule),
    ( nonvar(DuplicateRule) ->
        schema_error('duplicate composed rule name ~w', [DuplicateRule])
    ; true
    ),
    maplist(validate_presentation_rules(OperatorKeys), Presentations).

presentation_name(presentation(Name, _, _, _), Name).

validate_presentation_rules(
    Operators, presentation(_, _, Rules, Source)) :-
    maplist(validate_rule(Operators, Source), Rules).

validate_rule(Operators, Source, rule(Name, Head, Body)) :-
    ( Head = list([_|_]) -> true
    ; schema_error('~w: rule ~w head must be an application', [Source, Name])
    ),
    validate_term(Head, Operators, Source, Name, head),
    validate_body(Body, 0, Operators, Source, Name).

validate_body([], _, _, _, _).
validate_body([Goal|Goals], Index, Operators, Source, Rule) :-
    ( Goal = list([_|_]) -> true
    ; schema_error('~w: body[~d] of rule ~w must be an application',
                   [Source, Index, Rule])
    ),
    validate_term(Goal, Operators, Source, Rule, body(Index)),
    Next is Index + 1,
    validate_body(Goals, Next, Operators, Source, Rule).

validate_term(var(Name), _, Source, Rule, Location) :-
    !,
    ( (Name == '_' ; Name == '') ->
        schema_error('~w: anonymous variable at ~w in rule ~w',
                     [Source, Location, Rule])
    ; true
    ).
validate_term(list([]), _, Source, Rule, Location) :-
    !,
    schema_error('~w: empty term at ~w in rule ~w',
                 [Source, Location, Rule]).
validate_term(list([sym(Head)|Arguments]), Operators, Source, Rule, Location) :-
    !,
    length(Arguments, Arity),
    ( memberchk(Head-Arity, Operators) -> true
    ; schema_error('~w: undeclared operator ~w/~d at ~w in rule ~w',
                   [Source, Head, Arity, Location, Rule])
    ),
    validate_arguments(Arguments, 0, Head, Operators, Source, Rule, Location).
validate_term(list([_|_]), _, Source, Rule, Location) :-
    !,
    schema_error('~w: ~w head in rule ~w must be a symbol',
                 [Source, Location, Rule]).
validate_term(_, _, _, _, _).

validate_arguments([], _, _, _, _, _, _).
validate_arguments([Term|Terms], Index, Head, Operators, Source, Rule,
                   Location) :-
    ChildLocation = child(Location, Head, Index),
    validate_term(Term, Operators, Source, Rule, ChildLocation),
    Next is Index + 1,
    validate_arguments(Terms, Next, Head, Operators, Source, Rule, Location).

canonical_text(Presentation, Text) :-
    canonical_form(Presentation, Form),
    sexpr_text(Form, Body),
    string_concat(Body, "\n", Text).

canonical_form(presentation(Name, Operators0, Rules0, _),
               list([sym('gslt-presentation-v1'), sym(Name),
                     list([sym(signature)|OperatorForms]),
                     list([sym(equations)]),
                     list([sym(rewrites)|RuleForms])])) :-
    map_list_to_pairs(operator_sort_key, Operators0, OperatorPairs),
    keysort(OperatorPairs, SortedOperatorPairs),
    pairs_values(SortedOperatorPairs, Operators),
    maplist(operator_form, Operators, OperatorForms),
    map_list_to_pairs(rule_sort_key, Rules0, RulePairs),
    keysort(RulePairs, SortedRulePairs),
    pairs_values(SortedRulePairs, Rules),
    maplist(rule_form, Rules, RuleForms).

operator_sort_key(operator(Name, Arity), Name-Arity).
rule_sort_key(rule(Name, _, _), Name).

operator_form(operator(Name, Arity),
              list([sym(operator), sym(Name), int(Arity)])).
rule_form(rule(Name, Head, Body),
          list([sym(rule), sym(Name),
                list([sym(head), Head]),
                list([sym(body)|Body])])).

package_digest(Presentations, Digest) :-
    admit_presentations(Presentations),
    map_list_to_pairs(presentation_sort_key, Presentations, Pairs),
    keysort(Pairs, SortedPairs),
    pairs_values(SortedPairs, Sorted),
    string_codes("FiniteHornGSLTPackageV1", Header),
    package_bytes(Sorted, Payload),
    append(Header, [0|Payload], Bytes),
    crypto_data_hash(Bytes, Digest,
                     [algorithm(sha256), encoding(octet)]).

presentation_sort_key(presentation(Name, _, _, _), Name).

package_bytes([], []).
package_bytes([Presentation|Presentations], Bytes) :-
    canonical_text(Presentation, Text),
    string_codes(Text, ScalarCodes),
    phrase(utf8_codes(ScalarCodes), Payload),
    length(Payload, Length),
    uint64_be(Length, LengthBytes),
    package_bytes(Presentations, Rest),
    append(LengthBytes, Payload, Prefix),
    append(Prefix, Rest, Bytes).

uint64_be(Value, Bytes) :-
    Value >= 0,
    Value < 18446744073709551616,
    findall(Byte,
            ( between(0, 7, Index),
              Shift is (7 - Index) * 8,
              Byte is (Value >> Shift) /\ 255
            ),
            Bytes).

sexpr_text(Term, Text) :-
    phrase(render_sexpr(Term), Codes),
    string_codes(Text, Codes).

render_term(Term, Text) :-
    sexpr_text(Term, Text).

render_sexpr(sym(Name)) -->
    { atom_codes(Name, Codes), Codes = [_|_], maplist(safe_symbol_code, Codes) },
    Codes.
render_sexpr(var(Name)) -->
    { atom_codes(Name, Codes),
      Codes = [_|_],
      Name \== '_',
      maplist(safe_symbol_code, Codes)
    },
    "?", Codes.
render_sexpr(str(Text)) -->
    { string_codes(Text, Codes), maplist(unicode_scalar, Codes) },
    "\"", render_string_codes(Codes), "\"".
render_sexpr(int(Value)) -->
    { number_codes(Value, Codes) },
    Codes.
render_sexpr(list(Items)) -->
    "(", render_items(Items), ")".

render_items([]) --> [].
render_items([Item|Items]) -->
    render_sexpr(Item),
    render_more_items(Items).

render_more_items([]) --> [].
render_more_items([Item|Items]) -->
    " ", render_sexpr(Item),
    render_more_items(Items).

render_string_codes([]) --> [].
render_string_codes([Code|Codes]) -->
    render_string_code(Code),
    render_string_codes(Codes).

render_string_code(0'") --> "\\\"", !.
render_string_code(0'\\) --> "\\\\", !.
render_string_code(8) --> "\\b", !.
render_string_code(12) --> "\\f", !.
render_string_code(10) --> "\\n", !.
render_string_code(13) --> "\\r", !.
render_string_code(9) --> "\\t", !.
render_string_code(Code) -->
    { Code < 32,
      format(string(Hex), '~|~`0t~16r~4+', [Code]),
      string_codes(Hex, HexCodes)
    },
    "\\u", HexCodes,
    !.
render_string_code(Code) --> [Code].

safe_symbol_code(Code) :-
    \+ code_type(Code, space),
    \+ memberchk(Code, [0'(, 0'), 0';, 0'"]).

unicode_scalar(Code) :-
    integer(Code),
    Code >= 0,
    Code =< 0x10ffff,
    \+ (Code >= 0xd800, Code =< 0xdfff).

duplicate_key(Keys, Duplicate) :-
    msort(Keys, Sorted),
    ( append(_, [Duplicate, Duplicate|_], Sorted) -> true
    ; var(Duplicate)
    ).

require_nonempty_atom(Value, Source, Context) :-
    ( atom(Value), Value \== '' -> true
    ; schema_error('~w: expected nonempty symbol for ~w', [Source, Context])
    ).

schema_error(Format, Arguments) :-
    format(string(Message), Format, Arguments),
    throw(error(gslt_schema_error(Message), finite_horn_gslt_v1)).

/* ---------- Strict S-expression reader -------------------------------- */

sexprs(Terms) --> layout, sexprs_after_layout(Terms).

sexprs_after_layout([]) --> eos, !.
sexprs_after_layout([Term|Terms]) -->
    sexpr(Term),
    layout,
    sexprs_after_layout(Terms).

sexpr(list(Items)) --> "(", layout, sexpr_items(Items), ")", !.
sexpr(str(Text)) --> string_literal(Codes), { string_codes(Text, Codes) }, !.
sexpr(Term) --> raw_token(Codes), { token_term(Codes, Term) }.

sexpr_items([]) --> peek_close, !.
sexpr_items([Term|Terms]) -->
    sexpr(Term),
    layout,
    sexpr_items(Terms).

peek_close([0')|Rest], [0')|Rest]).

raw_token([Code|Codes]) -->
    [Code],
    { token_code(Code) },
    raw_token_tail(Codes).

raw_token_tail([Code|Codes]) -->
    [Code],
    { token_code(Code) },
    !,
    raw_token_tail(Codes).
raw_token_tail([]) --> [].

token_code(Code) :-
    \+ code_type(Code, space),
    \+ memberchk(Code, [0'(, 0'), 0';, 0'"]).

token_term([0'?|NameCodes], var(Name)) :-
    NameCodes = [_|_],
    !,
    atom_codes(Name, NameCodes).
token_term(Codes, int(Integer)) :-
    integer_token_codes(Codes),
    !,
    number_codes(Integer, Codes).
token_term(Codes, sym(Symbol)) :-
    atom_codes(Symbol, Codes).

integer_token_codes([0'-|Digits]) :-
    Digits = [_|_],
    maplist(decimal_digit, Digits),
    !.
integer_token_codes(Digits) :-
    Digits = [_|_],
    maplist(decimal_digit, Digits).

decimal_digit(Code) :-
    Code >= 0'0,
    Code =< 0'9.

string_literal(Codes) --> "\"", string_body(Codes), "\"".

string_body([]) --> peek_quote, !.
string_body([Code|Codes]) -->
    string_character(Code),
    string_body(Codes).

peek_quote([0'"|Rest], [0'"|Rest]).

string_character(Code) --> "\\", !, escaped_character(Code).
string_character(Code) -->
    [Code],
    { Code >= 32,
      Code =\= 0'",
      Code =\= 0'\\,
      unicode_scalar(Code)
    }.

escaped_character(0'") --> "\"".
escaped_character(0'\\) --> "\\".
escaped_character(0'/) --> "/".
escaped_character(8) --> "b".
escaped_character(12) --> "f".
escaped_character(10) --> "n".
escaped_character(13) --> "r".
escaped_character(9) --> "t".
escaped_character(Code) -->
    "u",
    hex_quad(First),
    unicode_escape_tail(First, Code).

unicode_escape_tail(High, Code) -->
    { High >= 0xd800, High =< 0xdbff },
    !,
    "\\u",
    hex_quad(Low),
    { Low >= 0xdc00,
      Low =< 0xdfff,
      Code is 0x10000 + ((High - 0xd800) << 10) + (Low - 0xdc00)
    }.
unicode_escape_tail(Low, _) -->
    { Low >= 0xdc00, Low =< 0xdfff },
    !,
    { fail }.
unicode_escape_tail(Code, Code) -->
    { unicode_scalar(Code) }.

hex_quad(Value) -->
    hex_digit(A), hex_digit(B), hex_digit(C), hex_digit(D),
    { Value is (A << 12) + (B << 8) + (C << 4) + D }.

hex_digit(Value) --> [Code], { hex_value(Code, Value) }.

hex_value(Code, Value) :-
    Code >= 0'0,
    Code =< 0'9,
    !,
    Value is Code - 0'0.
hex_value(Code, Value) :-
    Code >= 0'a,
    Code =< 0'f,
    !,
    Value is Code - 0'a + 10.
hex_value(Code, Value) :-
    Code >= 0'A,
    Code =< 0'F,
    Value is Code - 0'A + 10.

layout --> layout_item, !, layout.
layout --> [].

layout_item --> [Code], { code_type(Code, space) }.
layout_item --> ";", comment_tail.

comment_tail --> "\n", !.
comment_tail --> [_], !, comment_tail.
comment_tail --> eos.

eos([], []).
