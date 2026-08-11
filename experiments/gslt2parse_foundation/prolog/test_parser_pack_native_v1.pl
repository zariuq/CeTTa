:- module(test_parser_pack_native_v1,
          [ run_parser_pack_native_v1_gate/2
          ]).

:- use_module(parser_pack_eval,
              [ parser_pack_missing_states/3
              ]).
:- use_module(parser_pack_gll,
              [ parser_pack_gll_parse/6
              ]).
:- use_module(parser_pack_glr,
              [ parser_pack_glr_compile/4
              ]).
:- use_module(parser_pack_native_v1).
:- use_module(parser_pack_reference_compile,
              [ parser_pack_reference_compile/4
              ]).
:- use_module(parser_pack_abi_export_v1,
              [ write_parser_pack_compilation_abi_v1/4
              ]).
:- use_module(test_parser_pack_gll, []).
:- use_module(test_parser_pack_glr, []).
:- use_module(library(crypto)).

:- initialization(main, main).

main :-
    catch(run, Error, (print_message(error, Error), halt(1))),
    halt.

run :-
    current_prolog_flag(argv, Arguments),
    ( Arguments = [PresentationRoot] -> true
    ; throw(error(gate_failed(expected_presentation_root),
                  test_parser_pack_native_v1))
    ),
    run_parser_pack_native_v1_gate(PresentationRoot, Summary),
    Summary = summary(Cases, GLLExact, GLRExact, Replays,
                      Boundaries, MatrixDigest),
    format('(ParserPackNativePeTTaV1Summary ~d ~d ~d ~d ~d ~w 0)~n',
           [Cases, GLLExact, GLRExact, Replays,
            Boundaries, MatrixDigest]).

run_parser_pack_native_v1_gate(
    PresentationRoot,
    summary(CaseCount, CaseCount, CaseCount, Replays,
            Boundaries, MatrixDigest)) :-
    compile_native_plans(PresentationRoot, NativePlans, PeTTaPlans),
    test_parser_pack_gll:real_cases(Cases),
    maplist(native_matrix_case(NativePlans, PeTTaPlans), Cases, Rows),
    length(Rows, CaseCount),
    maplist(test_parser_pack_glr:real_row_text, Rows, RowTexts),
    atomics_to_string(RowTexts, "\n", Payload),
    crypto_data_hash(Payload, MatrixDigest, [algorithm(sha256)]),
    require(MatrixDigest ==
                '34791ce2f3fcf354d171ba43c09da37a6220cf203dc195b587cf8df9bac90141',
            exact_native_petta_matrix_digest),
    boundary_gate(PresentationRoot, NativePlans, Boundaries),
    Replays is CaseCount * 2,
    generic_purity_gate.

compile_native_plans(
    PresentationRoot,
    native_plans(Megalodon, Metamath, TPTP),
    real_plans(MegReal, MMReal, TPTPReal)) :-
    compile_native_language(
        PresentationRoot, megalodon,
        list([sym('pp-rel'), sym('mg-name-list')]),
        Megalodon, MegReal),
    compile_native_language(
        PresentationRoot, metamath,
        list([sym('pp-def'), sym(database)]),
        Metamath, MMReal),
    compile_native_language(
        PresentationRoot, tptp,
        list([sym('pp-def'), sym('tptp-file')]),
        TPTP, TPTPReal).

compile_native_language(
    PresentationRoot, Language, Start,
    native_language(Language, Compilation, Source, Pack, Start),
    language_plan(Language, Source, Pack, Start, GLRPlan)) :-
    test_parser_pack_gll:language_pack_case(
        Language, RelativePaths, _),
    parser_pack_reference_compile(
        PresentationRoot, RelativePaths, 4096,
        completed(Compilation)),
    Compilation = parser_pack_compilation(Source, _, _, Pack),
    parser_pack_glr_compile(Pack, Start, 100000,
                            completed(GLRPlan)).

native_matrix_case(
    NativePlans, PeTTaPlans,
    Case,
    NativeRow) :-
    Case = real_case(Language, Label, _, Start, Codes, _),
    select_native_language(
        NativePlans, Language, Compilation, Source, Pack, Start),
    test_parser_pack_gll:codepoints_node(Codes, Input),
    test_parser_pack_glr:real_case_row(PeTTaPlans, Case, PeTTaRow),
    parser_pack_gll_parse(
        Pack, Source, Start, Input, 1000000,
        completed(PeTTaParserResult)),
    native_case_backend(
        gll, Compilation, Source, Pack, Start, Input,
        Language, Label, GLLRow, GLLParserResult),
    native_case_backend(
        glr, Compilation, Source, Pack, Start, Input,
        Language, Label, GLRRow, GLRParserResult),
    require(GLLParserResult == PeTTaParserResult,
            exact_c_gll_petta_forest(Language, Label)),
    require(GLRParserResult == PeTTaParserResult,
            exact_c_glr_petta_forest(Language, Label)),
    require(GLLRow == PeTTaRow,
            exact_c_gll_petta_row(Language, Label)),
    require(GLRRow == PeTTaRow,
            exact_c_glr_petta_row(Language, Label)),
    NativeRow = PeTTaRow.

native_case_backend(
    Backend, Compilation, _, _, Start, Input,
    Language, Label,
    real_row(Language, Label, Decision, Results, ForestDigest),
    ParserResult) :-
    parser_pack_native_parse_compilation_v1(
        Backend, Compilation, Start, Input,
        native_limits(2000000, 4096, 65536),
        Outcome),
    require(
        Outcome = completed(native_parser_result_v1(
            backend(Backend),
            semantic_results(Results),
            parser_result(ParserResult),
            spans(byte_offsets(_)),
            accounting(input_bytes(_), input_scalars(_),
                       decoded_bytes(_), source_passes(1)),
            engine(work_items(_), graph_nodes(_)),
            provenance(source_digest(_), compiler_digest(_),
                       environment_digest(_), pack_digest(_)),
            forest_digest(ForestDigest))),
        native_backend_completed(Backend, Language, Label)),
    test_parser_pack_gll:result_decision(ParserResult, Decision).

select_native_language(
    native_plans(
        native_language(Language, Compilation, Source, Pack, Start), _, _),
    Language, Compilation, Source, Pack, Start) :- !.
select_native_language(
    native_plans(
        _, native_language(Language, Compilation, Source, Pack, Start), _),
    Language, Compilation, Source, Pack, Start) :- !.
select_native_language(
    native_plans(
        _, _, native_language(Language, Compilation, Source, Pack, Start)),
    Language, Compilation, Source, Pack, Start).

boundary_gate(PresentationRoot, NativePlans, 12) :-
    prime_compilation(PresentationRoot, PrimeCompilation, PrimeStart),
    forall(
        member(Backend, [gll, glr]),
        ( parser_pack_native_parse_compilation_v1(
              Backend, PrimeCompilation, PrimeStart, sym(nil),
              native_limits(2000000, 4096, 65536), PrimeOutcome),
          require(PrimeOutcome = unsupported(open_pack(_)),
                  prime_open_pack_fails_closed(Backend))
        )),

    select_native_language(
        NativePlans, megalodon,
        MegCompilation, MegSource, MegPack, MegStart),
    test_parser_pack_gll:codepoints_node([97], MegInput),
    test_parser_pack_gll:codepoints_node([97, 32, 98], MegTwoInput),
    forall(
        member(Backend, [gll, glr]),
        ( parser_pack_native_parse_compilation_v1(
              Backend, MegCompilation, MegStart, MegInput,
              native_limits(1, 4096, 65536), ResourceOutcome),
          require(
              ResourceOutcome = resource_exhausted(
                  native_recognizer(Backend, _)),
              recognizer_resource_is_typed(Backend))
        )),

    SurrogateInput = list([
        sym(cons), list([sym(cp), int(0xd800)]), sym(nil)]),
    parser_pack_native_parse_compilation_v1(
        gll, MegCompilation, MegStart, SurrogateInput,
        native_limits(2000000, 4096, 65536), UnicodeOutcome),
    require(UnicodeOutcome == invalid_input(codepoint_list),
            surrogate_input_rejected),

    test_parser_pack_gll:invalid_slot_pack(InvalidPack, InvalidStart),
    parser_pack_native_parse_v1(
        gll, unused, InvalidPack, [], InvalidStart, sym(nil),
        native_limits(100, 64, 64), InvalidPackOutcome),
    require(InvalidPackOutcome == invalid_presentation(parser_pack),
            invalid_pack_rejected_before_native_call),

    select_native_language(
        NativePlans, metamath,
        _, MMSource, MMPack, _),
    with_compilation_abi(
        MegCompilation, MegPack, MegStart,
        mismatched_provenance_gate(
            MegStart, MegInput, MMPack, MMSource)),

    parser_pack_native_v1:native_checked_outcome(
        response("not-a-canonical-response"),
        gll, MegPack, MegSource, MegStart, MegInput, [97], 4096,
        MalformedResponseOutcome),
    require(
        MalformedResponseOutcome = invalid_native_response(_),
        malformed_response_fails_closed),

    forall(
        member(Backend, [gll, glr]),
        ( parser_pack_native_parse_compilation_v1(
              Backend, MegCompilation, MegStart, MegInput,
              native_limits(2000000, 1, 65536), ShallowOutcome),
          require(
              ShallowOutcome = completed(_),
              finite_dependency_ignores_prefix_depth(Backend))
        )),

    forall(
        member(Backend, [gll, glr]),
        ( parser_pack_native_parse_compilation_v1(
              Backend, MegCompilation, MegStart, MegTwoInput,
              native_limits(2000000, 1, 65536), ReplayDepthOutcome),
          require(
              ReplayDepthOutcome = resource_exhausted(
                  native_replay_depth(_)),
              replay_prefix_depth_resource_is_typed(Backend))
        )).

prime_compilation(PresentationRoot, Compilation, Start) :-
    test_parser_pack_gll:language_pack_case(
        prime, RelativePaths, StartName),
    parser_pack_reference_compile(
        PresentationRoot, RelativePaths, 4096,
        completed(Compilation)),
    Start = list([sym('pp-def'), sym(StartName)]).

with_compilation_abi(Compilation, Pack, Start, Goal) :-
    parser_pack_missing_states(Pack, [Start], Missing),
    ( Missing == [] -> Closure = closed ; Closure = partial ),
    setup_call_cleanup(
        tmp_file_stream(text, AbiPath, Stream),
        ( set_stream(Stream, encoding(utf8)),
          write_parser_pack_compilation_abi_v1(
              Compilation, Start, Closure, Stream),
          close(Stream),
          call(Goal, AbiPath)
        ),
        ( catch(close(Stream), _, true),
          catch(delete_file(AbiPath), _, true)
        )).

mismatched_provenance_gate(
    Start, Input, MMPack, MMSource, AbiPath) :-
    parser_pack_native_parse_v1(
        gll, AbiPath, MMPack, MMSource, Start, Input,
        native_limits(2000000, 4096, 65536), Outcome),
    require(Outcome == invalid_native_response(validation_failed),
            mismatched_provenance_fails_closed).

generic_purity_gate :-
    source_file(
        parser_pack_native_v1:parser_pack_native_parse_v1(
            _, _, _, _, _, _, _, _),
        Path),
    read_file_to_string(Path, Text, [encoding(utf8)]),
    downcase_atom(Text, Lower),
    forall(
        member(Guest, [metamath, megalodon, tptp, prime]),
        require(\+ sub_atom(Lower, _, _, _, Guest),
                generic_native_surface_names_guest(Path, Guest))).

require(Condition, Label) :-
    ( call(Condition) -> true
    ; throw(error(gate_failed(Label), test_parser_pack_native_v1))
    ).
