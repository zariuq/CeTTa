:- module(parser_pack_native_v1,
          [ parser_pack_native_parse_v1/8,
            parser_pack_native_parse_compilation_v1/6,
            'gslt2parse-native-v1'/7
          ]).

:- use_module(finite_horn_gslt_v1,
              [ package_digest/2,
                read_ground_term_text/2,
                render_term/2
              ]).
:- use_module(parser_pack_abi_export_v1,
              [ write_parser_pack_compilation_abi_v1/4
              ]).
:- use_module(parser_pack_eval,
              [ parser_pack_canonical_results/2,
                parser_pack_canonical_terms/2,
                parser_pack_digest/2,
                parser_pack_missing_states/3,
                parser_pack_validate/1
              ]).
:- use_module(parser_pack_forest,
              [ parser_pack_forest_certificate/6,
                parser_pack_forest_digest/2,
                parser_pack_forest_results/5,
                parser_pack_input_codepoints/2,
                parser_pack_replay_forest/5
              ]).
:- use_module(parser_pack_reference_compile,
              [ parser_pack_reference_compile/4
              ]).
:- use_module(library(pairs)).
:- use_module(library(utf8)).

:- prolog_load_context(directory, ModuleDirectory),
   directory_file_path(
       ModuleDirectory, 'native/parser_pack_native_ffi_v1.so', ForeignLibrary),
   use_foreign_library(ForeignLibrary).

/*
  Reflective PeTTa boundary for the generic native ParserPack engines.  The
  foreign layer transports canonical carrier terms only.  This module checks
  provenance, reconstructs the neutral forest, independently replays it, and
  exposes explicit resource outcomes.
*/

parser_pack_native_parse_v1(
    Backend, AbiPath, Pack, SourcePresentations, Start, Input,
    native_limits(WorkLimit, ReplayDepth, ResultLimit), Outcome) :-
    native_backend(Backend),
    must_be(atom, AbiPath),
    must_be(integer, WorkLimit),
    must_be(integer, ReplayDepth),
    must_be(integer, ResultLimit),
    WorkLimit > 0,
    ReplayDepth > 0,
    ResultLimit > 0,
    ( parser_pack_validate(Pack) ->
        ( parser_pack_input_codepoints(Input, InputValues),
          maplist(native_codepoint, InputValues, Codepoints) ->
            render_term(Start, StartText),
            string_codes(InputText, Codepoints),
            native_foreign_outcome(
                Backend, AbiPath, StartText, InputText,
                WorkLimit, ReplayDepth, ResultLimit, ForeignOutcome),
            native_checked_outcome(
                ForeignOutcome, Backend, Pack, SourcePresentations,
                Start, Input, Codepoints, ReplayDepth, Outcome)
        ; Outcome = invalid_input(codepoint_list)
        )
    ; Outcome = invalid_presentation(parser_pack)
    ).

native_codepoint(list([sym(cp), int(Codepoint)]), Codepoint).

parser_pack_native_parse_compilation_v1(
    Backend,
    Compilation,
    Start,
    Input,
    Limits,
    Outcome) :-
    Compilation = parser_pack_compilation(
        SourcePresentations, _, _, Pack),
    parser_pack_missing_states(Pack, [Start], Missing),
    ( Missing == [] -> Closure = closed ; Closure = partial ),
    setup_call_cleanup(
        tmp_file_stream(text, AbiPath, Stream),
        ( set_stream(Stream, encoding(utf8)),
          write_parser_pack_compilation_abi_v1(
              Compilation, Start, Closure, Stream),
          close(Stream),
          parser_pack_native_parse_v1(
              Backend, AbiPath, Pack, SourcePresentations,
              Start, Input, Limits, Outcome)
        ),
        ( catch(close(Stream), _, true),
          catch(delete_file(AbiPath), _, true)
        )).

'gslt2parse-native-v1'(
    Backend,
    PresentationRoot0,
    PathsSurface,
    StartSurface,
    InputSurface,
    LimitsSurface,
    SurfaceOutcome) :-
    surface_text(PresentationRoot0, PresentationRoot),
    surface_paths(PathsSurface, RelativePaths),
    surface_carrier(StartSurface, Start),
    surface_carrier(InputSurface, Input),
    surface_limits(LimitsSurface, Limits),
    parser_pack_reference_compile(
        PresentationRoot, RelativePaths, 4096, CompileOutcome),
    ( CompileOutcome = completed(Compilation) ->
        parser_pack_native_parse_compilation_v1(
            Backend, Compilation, Start, Input, Limits, Outcome)
    ; Outcome = CompileOutcome
    ),
    native_surface(Outcome, SurfaceOutcome).

native_backend(gll).
native_backend(glr).

native_foreign_outcome(
    Backend, AbiPath, StartText, InputText,
    WorkLimit, ReplayDepth, ResultLimit, Outcome) :-
    catch(
        ( parser_pack_native_call_v1(
              Backend, AbiPath, StartText, InputText,
              WorkLimit, ReplayDepth, ResultLimit, ResponseText),
          Outcome = response(ResponseText)
        ),
        error(parser_pack_native_error(Message), _),
        Outcome = error(Message)).

native_checked_outcome(
    error(Message), _, _, _, _, _, _, _,
    invalid_native_request(Message)).
native_checked_outcome(
    response(ResponseText), Backend, Pack, SourcePresentations,
    Start, Input, Codepoints, ReplayDepth, Outcome) :-
    ( catch(
          ( read_ground_term_text(ResponseText, Response),
            response_field_pairs(Response, Fields),
            validate_response_identity(
                Fields, Backend, Pack, SourcePresentations),
            checked_response_outcome(
                Fields, Backend, Pack, SourcePresentations,
                Start, Input, Codepoints, ReplayDepth, CheckedOutcome)
          ),
          Error,
          Validation = exception(Error)) ->
        ( var(Validation) -> Outcome = CheckedOutcome
        ; Validation = exception(Error),
          Outcome = invalid_native_response(Error)
        )
    ; Outcome = invalid_native_response(validation_failed)
    ).

response_field_pairs(
    list([sym('pp-native-response-v1')|RawFields]), Fields) :-
    maplist(response_field_pair, RawFields, Pairs),
    keysort(Pairs, Fields),
    pairs_keys(Fields, Keys),
    sort(Keys, UniqueKeys),
    Keys == UniqueKeys.

response_field_pair(list([sym(Name), Value]), Name-Value) :-
    atom(Name).

response_field(Fields, Name, Value) :-
    memberchk(Name-Value, Fields).

validate_response_identity(Fields, Backend, Pack, SourcePresentations) :-
    response_field(Fields, backend, sym(Backend)),
    response_field(Fields, 'source-digest', str(SourceDigestText)),
    response_field(Fields, 'compiler-digest', str(CompilerDigestText)),
    response_field(Fields, 'environment-digest', str(EnvironmentDigestText)),
    response_field(Fields, 'pack-digest', str(PackDigestText)),
    valid_digest_text(SourceDigestText),
    valid_digest_text(CompilerDigestText),
    valid_digest_text(EnvironmentDigestText),
    valid_digest_text(PackDigestText),
    parser_pack_digest(Pack, PackDigest),
    atom_string(PackDigest, PackDigestText),
    SourcePresentations = [_|_],
    package_digest(SourcePresentations, SourceDigest),
    atom_string(SourceDigest, SourceDigestText).

valid_digest_text(Text) :-
    string_codes(Text, Codes),
    length(Codes, 64),
    maplist(lower_hex_code, Codes).

lower_hex_code(Code) :-
    between(0'0, 0'9, Code), !.
lower_hex_code(Code) :-
    between(0'a, 0'f, Code).

checked_response_outcome(
    Fields, Backend, Pack, SourcePresentations,
    Start, Input, Codepoints, ReplayDepth, Outcome) :-
    response_field(Fields, outcome, sym(ResponseOutcome)),
    ( ResponseOutcome == completed ->
        completed_response_keys(Fields),
        checked_completed_response(
            Fields, Backend, Pack, SourcePresentations,
            Start, Input, Codepoints, ReplayDepth, Outcome)
    ; noncompleted_response_keys(Fields),
      response_field(Fields, detail, str(Detail)),
      typed_native_outcome(ResponseOutcome, Backend, Detail, Outcome)
    ).

completed_response_keys(Fields) :-
    pairs_keys(Fields,
               [ backend,
                 'byte-offsets',
                 'compiler-digest',
                 decision,
                 'decoded-bytes',
                 detail,
                 'environment-digest',
                 expected,
                 'farthest-byte',
                 'farthest-scalar',
                 forest,
                 'forest-digest',
                 'graph-nodes',
                 'input-bytes',
                 'input-scalars',
                 outcome,
                 'pack-digest',
                 results,
                 'source-digest',
                 'source-passes',
                 'work-items'
               ]).

noncompleted_response_keys(Fields) :-
    pairs_keys(Fields,
               [ backend,
                 'compiler-digest',
                 detail,
                 'environment-digest',
                 outcome,
                 'pack-digest',
                 'source-digest'
               ]).

typed_native_outcome(
    'unsupported-open-pack', _, Detail,
    unsupported(open_pack(Detail))).
typed_native_outcome(
    'recognizer-limit', Backend, Detail,
    resource_exhausted(native_recognizer(Backend, Detail))).
typed_native_outcome(
    'replay-depth', _, Detail,
    resource_exhausted(native_replay_depth(Detail))).
typed_native_outcome(
    'result-limit', _, Detail,
    resource_exhausted(native_results(Detail))).

checked_completed_response(
    Fields, Backend, Pack, SourcePresentations,
    Start, Input, Codepoints, ReplayDepth,
    completed(native_parser_result_v1(
        backend(Backend),
        semantic_results(SemanticResults),
        parser_result(ParserResult),
        spans(byte_offsets(ByteOffsets)),
        accounting(input_bytes(InputBytes),
                   input_scalars(InputScalars),
                   decoded_bytes(DecodedBytes),
                   source_passes(SourcePasses)),
        engine(work_items(WorkItems), graph_nodes(GraphNodes)),
        provenance(source_digest(SourceDigest),
                   compiler_digest(CompilerDigest),
                   environment_digest(EnvironmentDigest),
                   pack_digest(PackDigest)),
        forest_digest(ForestDigest)))) :-
    response_field(Fields, decision, sym(Decision)),
    memberchk(Decision, [accepted, rejected]),
    response_field(Fields, forest, CanonicalForest),
    canonical_native_forest(CanonicalForest, Start, Forest),
    response_field(Fields, results, ResultsNode),
    carrier_list(ResultsNode, CanonicalNativeResults),
    maplist(canonical_native_result,
            CanonicalNativeResults, NativeSemanticResults),
    parser_pack_canonical_results(
        NativeSemanticResults, NativeSemanticResults),
    response_field(Fields, expected, ExpectedNode),
    carrier_list(ExpectedNode, Expected),
    parser_pack_canonical_terms(Expected, Expected),
    response_field(Fields, 'byte-offsets', ByteOffsetsNode),
    carrier_integer_list(ByteOffsetsNode, ByteOffsets),
    utf8_input_accounting(Codepoints, ExpectedOffsets, ExpectedInputBytes),
    ByteOffsets == ExpectedOffsets,
    response_field(Fields, 'input-bytes', int(InputBytes)),
    response_field(Fields, 'input-scalars', int(InputScalars)),
    response_field(Fields, 'decoded-bytes', int(DecodedBytes)),
    response_field(Fields, 'source-passes', int(SourcePasses)),
    length(Codepoints, InputScalars),
    InputBytes =:= ExpectedInputBytes,
    DecodedBytes =:= InputBytes,
    SourcePasses =:= 1,
    response_field(Fields, 'farthest-scalar', int(FarthestScalar)),
    response_field(Fields, 'farthest-byte', int(FarthestByte)),
    nth0(FarthestScalar, ByteOffsets, FarthestByte),
    response_field(Fields, 'work-items', int(WorkItems)),
    response_field(Fields, 'graph-nodes', int(GraphNodes)),
    WorkItems >= 0,
    GraphNodes >= 0,
    response_field(Fields, 'forest-digest', str(ForestDigestText)),
    parser_pack_forest_digest(Forest, ForestDigest),
    atom_string(ForestDigest, ForestDigestText),
    Forest = parser_forest(_, _, Roots, _, Choices),
    length(Roots, RootCount),
    length(Choices, ChoiceCount),
    findall(Label,
            member(parser_choice(_, Label, _, _, _), Choices),
            RawLabels),
    parser_pack_canonical_terms(RawLabels, UsedLabels),
    parser_pack_forest_certificate(
        Pack, SourcePresentations, Start, Input, Forest, Certificate),
    ParserResult = parser_result(
        decision(Decision),
        coverage(farthest(FarthestScalar),
                 expected(Expected),
                 productions(UsedLabels)),
        ambiguity(roots(RootCount), packed_choices(ChoiceCount)),
        Forest,
        evidence(forest_certificate(Certificate))),
    parser_pack_replay_forest(
        Pack, SourcePresentations, Start, Input, ParserResult),
    parser_pack_forest_results(
        Pack, Input, Forest, ReplayDepth,
        completed(PeTTaSemanticResults)),
    NativeSemanticResults == PeTTaSemanticResults,
    SemanticResults = PeTTaSemanticResults,
    response_field(Fields, 'source-digest', str(SourceDigestText)),
    response_field(Fields, 'compiler-digest', str(CompilerDigestText)),
    response_field(Fields, 'environment-digest', str(EnvironmentDigestText)),
    response_field(Fields, 'pack-digest', str(PackDigestText)),
    atom_string(SourceDigest, SourceDigestText),
    atom_string(CompilerDigest, CompilerDigestText),
    atom_string(EnvironmentDigest, EnvironmentDigestText),
    atom_string(PackDigest, PackDigestText).

canonical_native_forest(
    list([sym('pf-v1'), Start, int(InputLength),
          RootsNode, NodesNode, ChoicesNode]),
    Start,
    parser_forest(Start, InputLength, Roots, Nodes, Choices)) :-
    carrier_list(RootsNode, CanonicalRoots),
    carrier_list(NodesNode, CanonicalNodes),
    carrier_list(ChoicesNode, CanonicalChoices),
    maplist(canonical_native_node, CanonicalRoots, Roots),
    maplist(canonical_native_node, CanonicalNodes, Nodes),
    maplist(canonical_native_choice, CanonicalChoices, Choices).

canonical_native_node(
    list([sym('pf-symbol'), State, int(Left), int(Right)]),
    parser_symbol(State, Left, Right)).
canonical_native_node(
    list([sym('pf-intermediate'), Label, int(Dot), int(Left), int(Right)]),
    parser_intermediate(Label, Dot, Left, Right)).
canonical_native_node(
    list([sym('pf-terminal'), Matcher, Value, int(Left), int(Right)]),
    parser_terminal(Matcher, Value, Left, Right)).
canonical_native_node(
    list([sym('pf-epsilon'), int(Position)]),
    parser_epsilon(Position)).

canonical_native_choice(
    list([sym('pf-choice'), ParentNode, Label,
          PrefixNode, ChildNode, int(Pivot)]),
    parser_choice(Parent, Label, Prefix, Child, Pivot)) :-
    canonical_native_node(ParentNode, Parent),
    ( PrefixNode == sym('pf-none') -> Prefix = none
    ; canonical_native_node(PrefixNode, Prefix)
    ),
    canonical_native_node(ChildNode, Child).

canonical_native_result(
    list([sym(result), Value, Rest]),
    result(Value, Rest)).

carrier_list(sym(nil), []).
carrier_list(list([sym(cons), Value, Rest]), [Value|Values]) :-
    carrier_list(Rest, Values).

carrier_integer_list(Node, Integers) :-
    carrier_list(Node, IntegerNodes),
    maplist(integer_node, IntegerNodes, Integers).

integer_node(int(Integer), Integer) :-
    integer(Integer),
    Integer >= 0.

utf8_input_accounting(Codepoints, Offsets, ByteLength) :-
    utf8_input_accounting(Codepoints, 0, Offsets, ByteLength).

utf8_input_accounting([], Offset, [Offset], Offset).
utf8_input_accounting([Codepoint|Codepoints], Offset0,
                      [Offset0|Offsets], ByteLength) :-
    phrase(utf8_codes([Codepoint]), Bytes),
    length(Bytes, Width),
    Offset is Offset0 + Width,
    utf8_input_accounting(Codepoints, Offset, Offsets, ByteLength).

surface_text(Value, Text) :-
    ( string(Value) -> Text = Value
    ; atom(Value) -> atom_string(Value, Text)
    ).

surface_paths([paths|RawPaths], Paths) :-
    RawPaths = [_|_],
    maplist(surface_text, RawPaths, Paths).

surface_limits([native_limits, WorkLimit, ReplayDepth, ResultLimit],
               native_limits(WorkLimit, ReplayDepth, ResultLimit)).

surface_carrier(Value, str(Value)) :-
    string(Value), !.
surface_carrier(Value, int(Value)) :-
    integer(Value), !.
surface_carrier(Value, sym(Value)) :-
    atom(Value), !.
surface_carrier(Values, list(CarrierValues)) :-
    is_list(Values),
    maplist(surface_carrier, Values, CarrierValues).

native_surface(sym(Value), Value) :- !.
native_surface(int(Value), Value) :- !.
native_surface(str(Value), Value) :- !.
native_surface(list(Values), SurfaceValues) :-
    !,
    maplist(native_surface, Values, SurfaceValues).
native_surface(Value, Value) :-
    atomic(Value), !.
native_surface(Values, [list|SurfaceValues]) :-
    is_list(Values),
    !,
    maplist(native_surface, Values, SurfaceValues).
native_surface(Term, [Functor|SurfaceArguments]) :-
    compound(Term),
    Term =.. [Functor|Arguments],
    maplist(native_surface, Arguments, SurfaceArguments).
